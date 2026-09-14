// decisionHelper.js; upgrade 15 — a real, generic probabilistic
// decision-support engine: given several candidate branches (options),
// each with an outcome expression over a parameter range, estimates
// each branch's failure probability via seeded-stochastic Monte Carlo
// sampling and reports the WORST concrete point found for each branch
// (its "fault point" — the parameter combination most likely to break
// it), then RANKS branches by estimated success probability.
//
// Reuses this repo's existing, already-tested primitives rather than
// reinventing them: `mulberry32` (the seeded PRNG every other
// stochastic kernel in this repo already uses, so this instrument's
// randomness has the same reproducibility guarantee as `mcmc`),
// `compileExpr` (the same safe, non-eval expression compiler
// `mathExpr.js`/`numericCheck.js` already use), and `hashValue` (the
// same FNV-1a seed-derivation `mcmcSearch.js`'s own per-chain seeding
// uses) — this file adds zero new randomness or parsing machinery.
//
// HONESTY, same discipline as every kernel here: this is a
// probabilistic ESTIMATE from sampling, not a proof and not a
// guarantee. It NEVER makes the decision itself — `evaluateDecision`
// returns a ranking and a "recommendation" field that names the
// top-ranked branch and WHY, but the actual choice stays a human call,
// same as every other verification instrument in this repo surfacing a
// finding rather than acting on it.

import { compileExpr } from './mathExpr.js';
import { mulberry32 } from './mcmcSearch.js';
import { hashValue } from './fnv1a.js';

const MAX_BRANCHES = 20;
const MAX_SAMPLES = 20000;
const DEFAULT_SAMPLES = 2000;
const DECISION_SEED = 0xDEC1DE ^ 0x5EED; // distinct constant from MCMC_SEED/CHECK_SEED, same naming convention

function seedFor(masterSeed, branchId) {
  // Derives a distinct, deterministic 32-bit seed per branch from the
  // spec-level seed and the branch's own id -- same principle as
  // mcmcSearch.js's combineSeed, reusing hashValue instead of
  // reimplementing a second seed-mixing function.
  const mixed = hashValue({ masterSeed, branchId });
  return Number(mixed & 0xFFFFFFFFn);
}

export function normalizeDecisionSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Decision spec is not an object');
  const branchesRaw = Array.isArray(raw.branches) ? raw.branches.slice(0, MAX_BRANCHES) : [];
  if (branchesRaw.length < 2) throw new Error('Decision spec needs at least 2 branches to compare');

  const seenIds = new Set();
  const branches = branchesRaw.map((b, i) => {
    const id = String(b.id || `branch${i + 1}`).slice(0, 100);
    if (seenIds.has(id)) throw new Error(`Decision spec has a duplicate branch id "${id}"`);
    seenIds.add(id);

    const paramsRaw = Array.isArray(b.params) ? b.params.slice(0, 10) : [];
    if (paramsRaw.length === 0) throw new Error(`branch "${id}" needs at least 1 param`);
    const params = paramsRaw.map((p) => {
      const name = String(p.name || '').trim();
      if (!/^[A-Za-z_]\w*$/.test(name)) throw new Error(`branch "${id}": param name "${p.name}" is not a valid identifier`);
      const domain = Array.isArray(p.domain) ? p.domain.map(Number) : null;
      if (!domain || domain.length !== 2 || !domain.every(Number.isFinite) || domain[0] >= domain[1]) {
        throw new Error(`branch "${id}": param "${name}" needs a finite domain [lo, hi] with lo < hi`);
      }
      return { name, domain, integer: Boolean(p.integer) };
    });

    const objective = String(b.objective || '');
    compileExpr(objective, params.map((p) => p.name)); // parse NOW; fail loud before any sampling starts, same discipline as mcmcSearch.js's normalize

    return { id, label: String(b.label || id).slice(0, 200), params, objective };
  });

  const samples = Number.isInteger(raw.samples) && raw.samples > 0 ? Math.min(raw.samples, MAX_SAMPLES) : DEFAULT_SAMPLES;
  const seed = Number.isInteger(raw.seed) ? raw.seed : DECISION_SEED;

  return { kind: 'decision', branches, samples, seed };
}

/**
 * Evaluates one branch: samples its parameter domain `samples` times
 * (seeded, reproducible), treats the objective's sign as this repo's
 * existing convention already does elsewhere (>=0 holds/succeeds, <0
 * violates/fails), and returns the estimated failure probability plus
 * the single WORST (most negative margin) point actually sampled — a
 * concrete, reproducible fault point, not just a probability number.
 */
export function evaluateBranch(branch, { samples, seed }) {
  const compiled = compileExpr(branch.objective, branch.params.map((p) => p.name));
  const rand = mulberry32(seedFor(seed, branch.id));

  let failures = 0;
  let worstMargin = Infinity;
  let worstPoint = null;

  for (let i = 0; i < samples; i++) {
    const point = {};
    for (const p of branch.params) {
      const raw = p.domain[0] + rand() * (p.domain[1] - p.domain[0]);
      point[p.name] = p.integer ? Math.round(raw) : raw;
    }
    let margin;
    try {
      const v = compiled(point);
      margin = Number.isFinite(v) ? v : -Infinity;
    } catch {
      margin = -Infinity;
    }
    if (margin < 0) failures++;
    if (margin < worstMargin) { worstMargin = margin; worstPoint = { ...point }; }
  }

  return {
    branchId: branch.id,
    label: branch.label,
    samples,
    failureProbability: failures / samples,
    successProbability: (samples - failures) / samples,
    faultPoint: worstPoint ? { point: worstPoint, margin: worstMargin } : null,
  };
}

/**
 * Evaluates every branch in a normalized spec and ranks them by
 * estimated success probability (ties broken by the less-negative
 * worst-case margin — a branch that fails less BADLY when it fails is
 * preferred at equal failure rates). Returns the full per-branch
 * results, the ranking, and a `recommendation` naming the top-ranked
 * branch and why — never an instruction to act on it.
 */
export function evaluateDecision(spec) {
  const results = spec.branches.map((b) => evaluateBranch(b, { samples: spec.samples, seed: spec.seed }));

  const ranked = [...results].sort((a, b) => {
    if (b.successProbability !== a.successProbability) return b.successProbability - a.successProbability;
    const am = a.faultPoint ? a.faultPoint.margin : -Infinity;
    const bm = b.faultPoint ? b.faultPoint.margin : -Infinity;
    return bm - am;
  });

  const top = ranked[0] || null;
  return {
    branches: results,
    ranking: ranked.map((r) => r.branchId),
    recommendation: top ? {
      branchId: top.branchId,
      label: top.label,
      reason: `highest estimated success probability (${(top.successProbability * 100).toFixed(1)}%) across ${top.samples} seeded Monte Carlo samples of its stated parameter ranges`,
    } : null,
    honesty: 'This is a probabilistic ESTIMATE from seeded-stochastic sampling of the stated parameter ranges, reproducible bit-for-bit given the same seed -- not a proof, not a guarantee, and not a decision. The ranking and recommendation are findings to inform a human choice, the same as every other verification instrument in this repo; nothing here acts on this ranking automatically.',
  };
}

export function buildDecisionPrompt(node) {
  return `Design a DECISION-SUPPORT spec comparing multiple candidate branches for this situation: for each branch, express its outcome as a numeric objective over stated parameters, where a value >=0 means the branch's stated goal holds/succeeds and <0 means it fails, so this instrument can Monte Carlo sample the parameter ranges and estimate each branch's failure probability plus its worst concrete fault point.

SITUATION: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Return ONLY JSON: {"kind":"decision","branches":[{"id":"<short id>","label":"<human label>","params":[{"name":"<identifier>","domain":[<lo>,<hi>],"integer":<bool, optional>}, ...],"objective":"<expression in the param names; >=0 means this branch's goal holds>"}, ...],"samples":<optional int, default 2000>}
Rules: at least 2 branches, each with at least 1 param. The objective must be expressible in this repo's existing safe expression grammar (arithmetic, comparisons collapsed to a signed margin, no function calls beyond the already-supported math functions).`;
}
