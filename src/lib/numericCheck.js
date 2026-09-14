// numericCheck.js; the deterministic falsification engine.
//
// The deepest honest criticism of LLM reasoning tools: the model ASSERTS
// numeric facts, it never COMPUTES them. This module splits those roles.
// Ciall designs the sharpest numeric check for a claim (an identity, an
// inequality, or an integer counterexample search) as a declarative spec;
// the CLIENT then executes it; thousands of evaluations through
// mathExpr.js's parser, deterministic, seeded, reproducible, on-device.
// The model can be wrong about what to check; it can no longer be wrong
// about the arithmetic. A red result here is not an opinion.
//
// Same consent/architecture note as everywhere: runs entirely in the
// browser. No data leaves the device to run a check.

import { compileExpr, compileExprExact, EXACT_FUNCTION_NAMES } from './mathExpr.js';

// Wall-clock circuit breaker: exact-mode primitives (factorial, choose,
// isprime) are bounds-checked individually (mathExpr.js), but a SAMPLING
// LOOP calling a slow-but-legal exact function thousands of times could
// still take too long. Rather than hand-tune a perf budget per function,
// bound the whole loop by elapsed time; a partial-coverage result
// reported honestly beats a frozen tab or a fake full-coverage claim.
const TIME_BUDGET_MS = 4000;

// mulberry32; tiny seeded PRNG so every check is exactly reproducible.
// Fixed seed, recorded in the result: anyone can re-run the same points.
export const CHECK_SEED = 0xC1A11; // "CIALL"
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

export function buildNumericCheckPrompt(node) {
  return `Design the single sharpest NUMERIC check for this claim; the computation a hostile reviewer would run first. The platform will execute it deterministically; you design, you do not compute.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Return ONLY JSON, one of:
{"kind":"identity","note":"what this tests","lhs":"sin(x)^2+cos(x)^2","rhs":"1","vars":[{"name":"x","domain":[-10,10]}],"tolerance":1e-9}
{"kind":"inequality","note":"lhs <= rhs must hold","lhs":"log(1+x)","rhs":"x","vars":[{"name":"x","domain":[0,50]}]}
{"kind":"integer_search","note":"expr must be >= 0 (or exactly 0 if expect=zero) for every n","expr":"n^2 - n + 41","expect":"nonnegative","range":[1,5000]}
{"kind":"density","note":"what property this measures","expr":"a boolean-valued expression in n (nonzero = property holds)","range":[1,5000]}

The "density" kind is for EXISTENCE/FREQUENCY questions ("do these get rare, or is there a positive proportion?"); it does not pass/fail, it MEASURES what fraction of n in range satisfy expr(n) != 0, over real deterministic evaluation. Use this instead of identity/inequality when the actual dispute is about how COMMON something is, not whether a single equation holds.

FLOAT mode (default) expressions use: + - * / % ^, sin cos tan asin acos atan atan2 sinh cosh tanh exp log ln log2 log10 sqrt cbrt abs sign floor ceil round min max pow mod, constants pi e tau. Use this for continuous-function claims.

MULTI-STEP PROCESSES (float mode only): "lhs"/"rhs"/"expr" may use let-bindings to name intermediate quantities instead of inlining one enormous expression: "let m1 = ...; let v1 = ...; ...; FINAL_EXPRESSION". Each binding is "let NAME = EXPR;" and may only reference variables and EARLIER bindings, never itself or a later one; names must be unique and cannot reuse a variable name. Use this to unroll a recurrence (an update rule applied several times) a few steps at a time.

EXACT mode; add "mode":"exact" to ANY of the three shapes above; switches to arbitrary-precision BigInt arithmetic with NO floating-point error, for claims about divisibility, factorials, binomial coefficients, or number theory that floats cannot represent correctly (factorial(400) already overflows double precision). Exact-mode functions: ${EXACT_FUNCTION_NAMES.join(', ')}; where factorial(n) is n!, choose(n,k)/binom(n,k) is the binomial coefficient, valuation(n,p) is the p-adic valuation (largest k with p^k | n), and carries(m,p) is Kummer's carry-count for m+m in base p (equal to valuation(choose(2m,m), p); use this instead of computing the binomial coefficient directly when m is large). Variables and literals must be integers in exact mode. Use "/" for exact integer division and "mod" for remainder, never for continuous quantities.

Rules: up to 3 vars for identity/inequality, named however you like (matching the "vars" you declare). For integer_search and density, the engine binds EXACTLY ONE variable and its name is always "n"; "expr" must use "n" and no other identifier, never a descriptive name like "count" or "k". Pick the check that would actually KILL the claim if it's false; test the fragile step, not a triviality. Use exact mode whenever the claim involves factorials, binomial coefficients, divisibility, or exact integer identities; a float check on such a claim can silently lie. If the claim genuinely cannot be reduced to a numeric check, return {"kind":"none","reason":"..."}.`;
}

// Built for the contradiction-resolution flow: two branches assert
// mutually exclusive things, and the honest fix for "the human has to
// blindly pick" is to compute which side the evidence actually favors
// FIRST. Prefers the density kind for existence/frequency disputes
// (exactly the shape "sufficient density" vs "too rare" arguments take).
export function buildDecisiveCheckPrompt(branchAText, branchBText) {
  return `Two branches in this investigation assert MUTUALLY EXCLUSIVE things:
A: "${branchAText}"
B: "${branchBText}"

Design ONE computation whose result would favor A or favor B; not a coin flip, actual evidence. If this is an existence/frequency dispute (one side says "enough of these exist," the other says "too rare"), use "density" mode to MEASURE the actual empirical frequency up to a real bound; that is direct evidence for whichever side the number supports. Otherwise use identity/inequality/integer_search as usual.

Return ONLY JSON (same shapes as a numeric check, mode:"exact" available for factorial/binomial/number-theory). For "density" and "integer_search", "expr" must use "n" as its only variable; the engine binds exactly that name, never a descriptive one like "count":
{"kind":"density","note":"what this measures and which side a high/low result favors","expr":"an expression in n only","range":[a,b],"mode":"exact"}
{"kind":"identity"|"inequality"|"integer_search", ...same shape as other numeric checks}
{"kind":"none","reason":"why no computation can discriminate between A and B; this itself is a real finding: it means the dispute is genuinely open at this level of analysis, not a coin flip to hide that fact"}`;
}

export function normalizeCheckSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Check spec is not an object');
  const note = String(raw.note || '').slice(0, 200);
  const exact = raw.mode === 'exact';
  const compile = exact ? compileExprExact : compileExpr;
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };

  if (raw.kind === 'identity' || raw.kind === 'inequality') {
    const vars = (raw.vars || []).slice(0, 3).map((v) => {
      const d = v.domain;
      if (!Array.isArray(d) || d.length !== 2 || !d.every(Number.isFinite) || d[0] >= d[1]) throw new Error(`Bad domain for var "${v.name}"`);
      if (exact && !(Number.isInteger(d[0]) && Number.isInteger(d[1]))) throw new Error(`Exact mode requires an integer domain for var "${v.name}"`);
      return { name: String(v.name), domain: [d[0], d[1]] };
    });
    if (vars.length === 0) throw new Error('Check needs at least one variable');
    const names = vars.map((v) => v.name);
    compile(raw.lhs, names);
    compile(raw.rhs, names);
    return {
      kind: raw.kind, note, mode: exact ? 'exact' : 'float',
      lhs: String(raw.lhs), rhs: String(raw.rhs), vars,
      tolerance: Number.isFinite(raw.tolerance) && raw.tolerance > 0 ? raw.tolerance : 1e-9,
    };
  }
  if (raw.kind === 'integer_search' || raw.kind === 'density') {
    compile(raw.expr, ['n']);
    const r = raw.range;
    if (!Array.isArray(r) || r.length !== 2 || !r.every(Number.isFinite) || r[0] >= r[1]) throw new Error('Bad integer range');
    const n0 = Math.round(r[0]);
    const n1 = Math.min(Math.round(r[1]), n0 + 200000); // hard cap: 200k evaluations
    if (raw.kind === 'density') return { kind: 'density', note, mode: exact ? 'exact' : 'float', expr: String(raw.expr), range: [n0, n1] };
    return { kind: 'integer_search', note, mode: exact ? 'exact' : 'float', expr: String(raw.expr), expect: raw.expect === 'zero' ? 'zero' : 'nonnegative', range: [n0, n1] };
  }
  throw new Error(`Unknown check kind "${raw.kind}"`);
}

const SAMPLES = 4000;

/**
 * Executes a normalized spec. Pure, synchronous, deterministic.
 * Returns { verdict: 'held'|'violated'|'inconclusive', ... evidence }.
 */
export function executeCheck(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason, evaluations: 0 };
  const exact = spec.mode === 'exact';
  const compile = exact ? compileExprExact : compileExpr;
  const startedAt = Date.now();

  if (spec.kind === 'identity' || spec.kind === 'inequality') {
    const names = spec.vars.map((v) => v.name);
    const lhs = compile(spec.lhs, names);
    const rhs = compile(spec.rhs, names);
    const rand = mulberry32(CHECK_SEED);
    let evaluated = 0;
    let violations = 0;
    let worst = null; // largest offense (float mode only; exact mode has no "how much", just yes/no)
    let timedOut = false;
    for (let i = 0; i < SAMPLES; i++) {
      // Checked EVERY iteration, not throttled: exact-mode evaluations
      // (factorial, choose) can be arbitrarily expensive per call, so a
      // coarse "every 50th" check can let dozens of expensive calls run
      // between checks; which is exactly what caused a real multi-
      // minute hang during testing. Date.now() itself costs nothing
      // measurable next to any real computation here.
      if (Date.now() - startedAt > TIME_BUDGET_MS) { timedOut = true; break; }
      const point = {};
      for (const v of spec.vars) {
        const raw = v.domain[0] + rand() * (v.domain[1] - v.domain[0]);
        point[v.name] = exact ? Math.round(raw) : raw;
      }
      let l, r;
      try { l = lhs(point); r = rhs(point); } catch { continue; }
      if (exact) {
        evaluated++;
        const violated = spec.kind === 'identity' ? l !== r : l > r; // exact: no tolerance, no floating slop
        if (violated) {
          violations++;
          if (!worst) worst = { point, lhs: l.toString(), rhs: r.toString() };
        }
        continue;
      }
      if (!Number.isFinite(l) || !Number.isFinite(r)) continue;
      evaluated++;
      const offense = spec.kind === 'identity'
        // relative deviation, so identities in the millions aren't judged at 1e-9 absolute
        ? Math.abs(l - r) / (1 + Math.abs(l) + Math.abs(r)) - spec.tolerance
        : l - r - 1e-12 * (1 + Math.abs(r)); // inequality: lhs <= rhs
      if (offense > 0) {
        violations++;
        if (!worst || offense > worst.offense) worst = { offense, point, lhs: l, rhs: r };
      }
    }
    if (evaluated < (timedOut ? 1 : SAMPLES * 0.2)) return { verdict: 'inconclusive', reason: `Only ${evaluated}/${SAMPLES} sample points were finite; the domain mostly hits poles or invalid regions.`, evaluations: evaluated };
    return {
      verdict: violations === 0 ? 'held' : 'violated',
      evaluations: evaluated,
      violations,
      worst: worst ? { point: worst.point, lhs: worst.lhs, rhs: worst.rhs } : null,
      seed: CHECK_SEED,
      mode: exact ? 'exact' : 'float',
      partial: timedOut ? `stopped after ${TIME_BUDGET_MS}ms; ${evaluated} of ${SAMPLES} samples evaluated, not the full seed sequence` : null,
    };
  }

  if (spec.kind === 'density') {
    // Existence/frequency questions ("do these get rare, or is there a
    // positive proportion?") don't have a pass/fail answer; they have a
    // MEASURED fraction. This is the honest fix for letting a human
    // blindly pick between two competing existence claims: compute the
    // actual empirical density up to a real bound instead of guessing.
    const fn = compile(spec.expr, ['n']);
    let evaluated = 0;
    let matched = 0;
    let timedOut = false;
    const firstMatches = [];
    for (let n = spec.range[0]; n <= spec.range[1]; n++) {
      if (Date.now() - startedAt > TIME_BUDGET_MS) { timedOut = true; break; }
      let v;
      try { v = fn({ n }); } catch { continue; }
      evaluated++;
      const truthy = exact ? v !== 0n : (Number.isFinite(v) && v !== 0);
      if (truthy) { matched++; if (firstMatches.length < 8) firstMatches.push(n); }
    }
    if (evaluated === 0) return { verdict: 'inconclusive', reason: 'No finite evaluations in range.', evaluations: 0 };
    return {
      verdict: 'measured', density: matched / evaluated, matched, evaluations: evaluated, firstMatches, seed: CHECK_SEED,
      mode: exact ? 'exact' : 'float',
      partial: timedOut ? `stopped after ${TIME_BUDGET_MS}ms; measured over ${evaluated} of the requested range, not the full bound` : null,
    };
  }

  // integer_search
  const fn = compile(spec.expr, ['n']);
  let evaluated = 0;
  let violations = 0;
  let first = null;
  let timedOut = false;
  for (let n = spec.range[0]; n <= spec.range[1]; n++) {
    // Same reasoning as the identity/inequality loop above: check every
    // iteration, never throttled; a single exact-mode evaluation can be
    // arbitrarily expensive (factorial, choose).
    if (Date.now() - startedAt > TIME_BUDGET_MS) { timedOut = true; break; }
    let v;
    try { v = fn({ n }); } catch { continue; }
    if (exact) {
      evaluated++;
      const bad = spec.expect === 'zero' ? v !== 0n : v < 0n;
      if (bad) {
        violations++;
        if (!first) first = { n, value: v.toString() };
        if (violations >= 25) break;
      }
      continue;
    }
    if (!Number.isFinite(v)) continue;
    evaluated++;
    const bad = spec.expect === 'zero' ? Math.abs(v) > 1e-9 : v < -1e-9;
    if (bad) {
      violations++;
      if (!first) first = { n, value: v };
      if (violations >= 25) break; // enough evidence
    }
  }
  if (evaluated === 0) return { verdict: 'inconclusive', reason: 'No finite evaluations in range.', evaluations: 0 };
  return {
    verdict: violations === 0 ? 'held' : 'violated', evaluations: evaluated, violations, first, seed: CHECK_SEED,
    mode: exact ? 'exact' : 'float',
    partial: timedOut ? `stopped after ${TIME_BUDGET_MS}ms; evaluated n up to a point short of the full requested range` : null,
  };
}
