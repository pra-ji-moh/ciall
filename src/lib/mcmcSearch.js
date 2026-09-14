// mcmcSearch.js; MCMC counterexample search: a REAL Metropolis-Hastings
// sampler, run deterministically on-device against the trusted kernel.
//
// What this adds beyond numericCheck.js's independent uniform sampling:
// independent sampling wastes almost every evaluation when the violating
// region is small; 4000 uniform darts at a target occupying 0.1% of the
// domain expect 4 hits. A Markov chain instead CLIMBS: each step proposes
// a move near the current point and accepts it with probability
// min(1, exp(Δ/T)) where Δ is the change in the violation margin. The
// chain spends its evaluations concentrated where the claim is weakest,
// which is exactly where counterexamples live. Same trusted-kernel
// discipline as every computed instrument: the objective is compiled by
// mathExpr.js's whitelisted grammar (no eval, no network, CSP-safe), the
// randomness is a seeded PRNG (bit-for-bit reproducible), and the whole
// run is wall-clock bounded.
//
// HONESTY CONTRACT, the part that matters more than the algorithm:
//  - "violated" comes with a concrete point you can plug back in by hand
//; that verdict is as strong as any numeric check's.
//  - "held" is WEAKER than a grid/uniform "held": an adaptive sampler
//    explores where its chain happened to walk. No counterexample found
//    means "none found along these chains," never "none exists." The
//    result object carries this caveat permanently; the report prints it.
//  - Convergence is MEASURED, not assumed: multiple independent chains
//    from different seeds, with the Gelman-Rubin R-hat statistic over
//    their objective traces. R-hat far above 1 means the chains disagree
//    about the landscape; the verdict is then downgraded honestly.
//  - Adaptation (step-size tuning toward a healthy acceptance rate) runs
//    ONLY during burn-in and is frozen after. Adapting forever would
//    break the Markov property the sampler's guarantees rest on; a
//    subtle correctness point, encoded here rather than remembered.
//
// The model DESIGNS the search (parameters, domains, objective); the
// device EXECUTES it. The model can be wrong about what to search; it
// can no longer be wrong about the arithmetic.

import { compileExpr } from './mathExpr.js';
import { hashKey } from './fnv1a.js';

export const MCMC_SEED = 0xC1A11 ^ 0x5EED; // distinct from CHECK_SEED; different instrument, different stream
const TIME_BUDGET_MS = 4500;
const MAX_PARAMS = 4;
const MAX_CHAINS = 4;
const MAX_SAMPLES_PER_CHAIN = 4000;
const MAX_TOTAL_EVALS = 20000;

// SMC mode (upgrade 2): a parallel Sequential Monte Carlo sampler,
// alongside (never replacing) the MH sampler above. See the SMC MODE
// block further down for the full design note.
const SMC_WORKER_COUNT = 4; // matches kernelWorkerPool.js's fixed pool size
const SMC_MAX_N = 20000; // same order of magnitude as MH's MAX_TOTAL_EVALS
const SMC_MAX_GENERATIONS = 30;
const SMC_TIME_BUDGET_MS = 4500; // same wall-clock discipline as MH's TIME_BUDGET_MS
const SMC_TEMPERATURE_INITIAL = 10;
const SMC_TEMPERATURE_FINAL = 0.05; // geometric cooling toward a sharp target, standard annealed-SMC practice
// Random-walk MH acceptance sweet spot: ~44% optimal in 1D, ~23.4% as
// dimension grows (Roberts/Gelman/Gilks); target the dimension-blended
// middle; exactness here matters far less than avoiding the two failure
// modes (≈0%: frozen chain; ≈100%: diffusing without ever climbing).
const TARGET_ACCEPT_1D = 0.44;
const TARGET_ACCEPT_ND = 0.234;

// mulberry32; same tiny seeded PRNG the numeric engine uses. Every
// chain gets its own deterministic stream derived from MCMC_SEED, so a
// re-run reproduces every proposal, every acceptance, bit for bit.
// Exported (in addition to MH's own internal use) so the SMC worker path
// (kernelWorker.js) derives particle randomness from the identical PRNG
// algorithm rather than a second, potentially-diverging implementation.
export function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// Box-Muller over the seeded uniform stream; Gaussian proposals without
// Math.random() anywhere (Math.random is banned in this file: it would
// silently destroy reproducibility, the property that makes a "computed"
// finding re-checkable by anyone). Exported for the same reason as
// mulberry32 above.
export function makeGaussian(rand) {
  let spare = null;
  return () => {
    if (spare !== null) { const s = spare; spare = null; return s; }
    let u = 0, v = 0, s = 0;
    do {
      u = rand() * 2 - 1;
      v = rand() * 2 - 1;
      s = u * u + v * v;
    } while (s === 0 || s >= 1);
    const m = Math.sqrt((-2 * Math.log(s)) / s);
    spare = v * m;
    return u * m;
  };
}

// Reflect a proposal back into [lo, hi]. Reflection (not clamping) keeps
// the proposal distribution symmetric; q(x'|x) = q(x|x'); which is the
// condition that lets the simple Metropolis acceptance ratio stand in
// for the full Hastings correction. Clamping piles probability mass onto
// the boundary and silently biases the walk toward edges.
function reflect(x, lo, hi) {
  const width = hi - lo;
  if (width <= 0) return lo;
  let y = x;
  for (let i = 0; i < 8 && (y < lo || y > hi); i++) {
    if (y < lo) y = lo + (lo - y);
    if (y > hi) y = hi - (y - hi);
  }
  // Pathological overshoot (proposal many widths out); fall back to a
  // wrap into the domain rather than looping forever.
  if (y < lo || y > hi) {
    const t = ((y - lo) % width + width) % width;
    y = lo + t;
  }
  return y;
}

// Shared by both normalizeMcmcSpec's MH path and its SMC path (upgrade
// 2) — identical validation either way, factored out so SMC mode can't
// silently drift from MH's param-safety rules. Behavior for MH callers
// is byte-for-byte what the inline version used to do.
function validateParams(raw) {
  const params = (raw.params || []).slice(0, MAX_PARAMS).map((p) => {
    const d = p?.domain;
    if (!p?.name || typeof p.name !== 'string') throw new Error('Every param needs a name');
    if (!Array.isArray(d) || d.length !== 2 || !d.every(Number.isFinite) || d[0] >= d[1]) throw new Error(`Bad domain for param "${p.name}"`);
    if (p.integer && !(Number.isInteger(d[0]) && Number.isInteger(d[1]))) throw new Error(`Integer param "${p.name}" needs an integer domain`);
    return { name: String(p.name), domain: [d[0], d[1]], integer: Boolean(p.integer) };
  });
  if (params.length === 0) throw new Error('MCMC search needs at least one parameter');
  const names = params.map((p) => p.name);
  if (new Set(names).size !== names.length) throw new Error('Duplicate parameter names');
  return params;
}

/**
 * Validates and normalizes a model-designed MCMC spec. Mirrors
 * normalizeCheckSpec's philosophy: compile everything NOW (parse errors
 * surface here, loudly), clamp every knob the model controls (the spec
 * is model-written, therefore attacker-shaped from the engine's point of
 * view), and return a plain serializable object.
 *
 * Spec shape (MH, the default):
 * { kind: 'mcmc_search', note,
 *   params: [{ name, domain: [lo, hi], integer? }],   // 1..4
 *   objective: 'expr in the params',  // VIOLATION MARGIN: > 0 ⇒ claim violated at this point
 *   temperature?, chains?, samples?, burnIn? }
 *
 * Spec shape (SMC, upgrade 2 — opt in with mode:'smc'):
 * { kind: 'mcmc_search', mode: 'smc', note, params, objective, N }
 * Same params/objective rules as MH; N is the particle count (rounded up
 * to a multiple of SMC_WORKER_COUNT for even worker slicing, clamped to
 * [SMC_WORKER_COUNT, SMC_MAX_N]).
 */
export function normalizeMcmcSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('MCMC spec is not an object');
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };
  if (raw.kind !== 'mcmc_search') throw new Error(`Unknown MCMC spec kind "${raw.kind}"`);

  const params = validateParams(raw);
  const names = params.map((p) => p.name);
  compileExpr(raw.objective, names); // parse NOW; fail loud before any sampling starts

  if (raw.mode === 'smc') {
    const requestedN = Number.isFinite(raw.N) ? Math.round(raw.N) : SMC_WORKER_COUNT * 250;
    const clampedN = Math.max(SMC_WORKER_COUNT, Math.min(requestedN, SMC_MAX_N));
    const N = Math.ceil(clampedN / SMC_WORKER_COUNT) * SMC_WORKER_COUNT;
    return {
      kind: 'mcmc_search',
      mode: 'smc',
      note: String(raw.note || '').slice(0, 200),
      params,
      objective: String(raw.objective),
      N,
      // Tells orchestrator.js not to hand this spec's run() to the outer
      // per-kernel worker pool: SMC's own run() drives that SAME pool
      // itself (see mcmcSearch.js's SMC MODE section), and dispatching it
      // into one of the pool's own workers first would nest a 4-worker
      // fan-out inside a single pool worker instead of running it
      // alongside the other three, workers oversubscribing the machine's
      // 4 logical CPUs for no benefit. Every other kernel's spec has no
      // such flag and is completely unaffected.
      selfParallel: true,
    };
  }

  const clampInt = (v, lo, hi, dflt) => Number.isFinite(v) ? Math.max(lo, Math.min(hi, Math.round(v))) : dflt;
  return {
    kind: 'mcmc_search',
    note: String(raw.note || '').slice(0, 200),
    params,
    objective: String(raw.objective),
    temperature: Number.isFinite(raw.temperature) && raw.temperature > 0 ? Math.min(raw.temperature, 100) : 1,
    chains: clampInt(raw.chains, 2, MAX_CHAINS, 3),
    samples: clampInt(raw.samples, 100, MAX_SAMPLES_PER_CHAIN, 1500),
    burnIn: clampInt(raw.burnIn, 50, 2000, 400),
  };
}

// Gelman-Rubin R-hat over the chains' objective traces; the standard
// "did independent chains converge to the same landscape?" diagnostic.
// Computed on the post-burn-in samples of the scalar objective. ~1.0 ⇒
// chains agree; > ~1.1 ⇒ they don't, and any verdict from them is
// exploratory at best. Reported ALWAYS, never hidden.
function gelmanRubin(chains) {
  const m = chains.length;
  const n = Math.min(...chains.map((c) => c.length));
  if (m < 2 || n < 10) return null;
  const trimmed = chains.map((c) => c.slice(c.length - n));
  const means = trimmed.map((c) => c.reduce((a, b) => a + b, 0) / n);
  const grand = means.reduce((a, b) => a + b, 0) / m;
  const B = (n / (m - 1)) * means.reduce((a, mu) => a + (mu - grand) ** 2, 0);
  const W = trimmed.reduce((acc, c, j) => {
    const mu = means[j];
    return acc + c.reduce((a, x) => a + (x - mu) ** 2, 0) / (n - 1);
  }, 0) / m;
  if (W === 0) return B === 0 ? 1 : Infinity; // degenerate: frozen chains
  const varPlus = ((n - 1) / n) * W + B / n;
  return Math.sqrt(varPlus / W);
}

// ============================================================
// SPARSE INCREMENTAL RECOMPUTATION (upgrade 6) — MH chain warm-start.
// ============================================================
//
// Transparent to callers, same as consistencyKernel.js's cache: no flag,
// no interface change. A structural FINGERPRINT identifies "the same
// search" across calls -- param names, their integer-ness, and the
// objective expression string, deliberately EXCLUDING domain bounds and
// the temperature/chains/samples/burnIn knobs. Those are the "bounds/
// tolerance" the model tunes between calls while still searching the
// same underlying question; the fingerprint is about WHAT is being
// searched, not the current dial settings. Two calls with the same
// fingerprint resume each chain's Markov state (its PRNG stream, current
// position, adapted proposal scale, and measured landscape spread)
// instead of starting over; a fingerprint mismatch (different params or
// objective -- a genuinely different search) discards the cache and
// runs cold, exactly as if no cache existed.
//
// SCOPE NOTE, disclosed rather than silently narrowed: this cache covers
// the MH sampler only, not SMC mode. SMC's state is a particle swarm
// living in shared WASM memory and driven through the worker pool
// (upgrade 2's selfParallel path) -- warm-starting it safely would mean
// caching and re-injecting positions across that worker boundary, a
// materially different and higher-risk problem than resuming a handful
// of in-process closures. The task's own phrasing ("position, log-
// probability, step count") maps directly onto a Markov chain, not a
// particle population, so MH is where this feature's literal spec
// applies most naturally; SMC mode continues to run a full particle
// re-initialization on every call, unchanged from upgrade 2.
//
// Per ground rule 3 (no kernel-specific special-casing in
// orchestrator.js): this cache lives entirely inside mcmcSearch.js.
// orchestrator.js still just calls run(spec) and gets a result back; it
// has no idea a cache exists.

const MCMC_CACHE_MAX_ENTRIES = 1000;
const mhChainCache = new Map(); // fingerprint -> ChainState[], LRU by insertion/access order

function mhLruTouch(key) {
  const value = mhChainCache.get(key);
  if (value === undefined) return undefined;
  mhChainCache.delete(key);
  mhChainCache.set(key, value);
  return value;
}

function mhLruSet(key, value) {
  mhChainCache.delete(key);
  mhChainCache.set(key, value);
  if (mhChainCache.size > MCMC_CACHE_MAX_ENTRIES) {
    const oldest = mhChainCache.keys().next().value;
    mhChainCache.delete(oldest);
  }
}

// Structural identity only -- see the block comment above for why domain
// bounds and temperature/chains/samples/burnIn are excluded.
function mhFingerprint(spec) {
  return hashKey({
    params: spec.params.map((p) => ({ name: p.name, integer: p.integer })),
    objective: spec.objective,
  });
}

// EVERY field that can actually change a chain's trajectory -- domain
// bounds, temperature, chains, samples, burnIn, and the objective/params
// (redundant with the fingerprint, harmless to repeat). Deliberately
// excludes `note`: a free-text description a caller may reword between
// calls without touching the search itself, which must NOT look like an
// edit -- see the exact-repeat handling above this call site.
function mhSpecIdentity(spec) {
  return hashKey({
    params: spec.params,
    objective: spec.objective,
    temperature: spec.temperature,
    chains: spec.chains,
    samples: spec.samples,
    burnIn: spec.burnIn,
  });
}

/**
 * Executes a normalized mcmc_search spec: MH (the default) or, if
 * spec.mode === 'smc', the parallel SMC sampler below. MH's own path is
 * completely untouched by this branch — same function, same body, same
 * synchronous return. SMC's path returns a Promise (it dispatches real
 * worker_threads tasks and must await them); callers that always
 * `await run(spec)` — as orchestrator.js does since upgrade 2 — get the
 * right answer either way, since awaiting a plain object is a no-op.
 */
export function executeMcmcSearch(spec) {
  if (spec.mode === 'smc') return executeSmcSearch(spec);
  return executeMcmcSearchMH(spec);
}

/**
 * The original Metropolis-Hastings search. Pure, synchronous,
 * deterministic; same execution contract as executeCheck. Returns
 * { verdict: 'violated'|'held'|'inconclusive', ...evidence }. Unchanged
 * by upgrade 2 — every line below is exactly what executeMcmcSearch used
 * to be before SMC mode existed.
 */
function executeMcmcSearchMH(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason, evaluations: 0 };
  const objective = compileExpr(spec.objective, spec.params.map((p) => p.name));
  const dims = spec.params.length;
  const targetAccept = dims === 1 ? TARGET_ACCEPT_1D : TARGET_ACCEPT_ND;
  const startedAt = Date.now();

  let evaluations = 0;
  let timedOut = false;
  const evalObjective = (point) => {
    evaluations++;
    try {
      const v = objective(point);
      return Number.isFinite(v) ? v : -Infinity; // failed/undefined region: never accepted, never "violating"
    } catch {
      return -Infinity;
    }
  };

  let best = null; // { point, margin }; the single most-violating point across ALL chains
  const chainTraces = []; // per-chain post-burn-in objective series (for R-hat)
  const bestTrace = []; // thinned best-so-far series for the UI trace plot
  let accepted = 0;
  let proposed = 0;
  const acceptRates = [];

  // Upgrade 6: resume each chain from where the last call on this SAME
  // structural search (see mhFingerprint above) left it, instead of
  // reseeding from scratch -- but ONLY when something about the spec
  // actually changed since that last call. A call with a spec that is
  // byte-for-byte IDENTICAL to the one already cached under this
  // fingerprint runs fully cold, exactly as if no cache existed: this is
  // the same idempotence discipline consistencyKernel.js's fixpoint
  // needed (see its round-0 fix) -- warm-starting must never be
  // observable on an unchanged input, only on a genuinely edited one. A
  // repeat call with the exact same spec is deterministic anyway (each
  // chain's start seed is a fixed function of MCMC_SEED and its index,
  // never of prior calls), so running it cold reproduces the identical
  // trajectory a truly fresh module instance would, which is exactly
  // what callers doing "run the same spec twice" are entitled to expect.
  // cachedChains[c] is undefined for any index beyond what a previous
  // warm-eligible call actually reached (more chains requested now than
  // were cached, or that call timed out partway through) -- those chains
  // simply run cold too.
  const fingerprint = mhFingerprint(spec);
  const specHash = mhSpecIdentity(spec);
  const cachedEntry = mhLruTouch(fingerprint);
  const cachedChains = cachedEntry && cachedEntry.specHash !== specHash ? cachedEntry.chains : null;
  const chainStates = cachedChains ? cachedChains.slice() : [];

  for (let c = 0; c < spec.chains; c++) {
    const warm = chainStates[c];

    // Independent deterministic stream per chain; chains must be
    // genuinely independent for R-hat to mean anything, but each stream
    // still derives from the fixed instrument seed for reproducibility.
    // A warm chain reuses its PRNG closures AS-IS -- resuming the exact
    // same stream where it left off, not reseeding it -- so continuing a
    // chain across calls is indistinguishable from having run it as one
    // longer chain in the first place.
    const rand = warm ? warm.rand : mulberry32((MCMC_SEED + 0x9E3779B9 * (c + 1)) >>> 0);
    const gauss = warm ? warm.gauss : makeGaussian(rand);

    const widths = spec.params.map((p) => p.domain[1] - p.domain[0]);

    // Start point: uniform in domain for a fresh chain (overdispersed
    // starts are what makes multi-chain R-hat informative); a warm chain
    // instead resumes its last position, reflected back into the domain
    // in case bounds narrowed since the previous call.
    const state = {};
    for (const p of spec.params) {
      if (warm) {
        state[p.name] = p.integer ? Math.round(reflect(warm.state[p.name], p.domain[0], p.domain[1])) : reflect(warm.state[p.name], p.domain[0], p.domain[1]);
      } else {
        const raw = p.domain[0] + rand() * (p.domain[1] - p.domain[0]);
        state[p.name] = p.integer ? Math.round(raw) : raw;
      }
    }
    let stateVal = evalObjective(state);

    // Per-param proposal scales, adapted during burn-in only; and hard-
    // capped at the domain width: without the cap, a persistently-high
    // acceptance rate compounds the multiplicative nudge for hundreds of
    // burn-in steps and the scale explodes until every proposal wraps
    // the whole domain, silently degrading the chain into plain uniform
    // sampling (observed in testing: acceptance pinned at 93%, behavior
    // indistinguishable from independent darts). A warm chain reuses its
    // already-adapted scale, reclamped to the current domain width.
    const scales = spec.params.map((p, d) => {
      const cap = p.integer ? 1 : 1e-12;
      return warm ? Math.min(Math.max(warm.scales[d], cap), widths[d]) : Math.max(widths[d] * 0.15, cap);
    });

    // Scale-invariant temperature: a fixed T=1 is meaningless against an
    // objective whose variations are 0.001 (accepts everything; pure
    // diffusion) or 10^6 (rejects everything; frozen chain). Measure
    // the landscape's own spread with a Welford running variance during
    // burn-in and set the effective temperature from it; spec.temperature
    // then acts as a multiplier on the landscape's natural scale rather
    // than an absolute number the model must guess correctly. A warm
    // chain carries its Welford accumulator forward (the landscape's
    // shape doesn't change just because bounds or temperature did) and
    // re-derives effT from it under the CURRENT spec.temperature, so a
    // temperature-only edit between calls still takes effect immediately.
    let wCount = warm ? warm.wCount : 0;
    let wMean = warm ? warm.wMean : 0;
    let wM2 = warm ? warm.wM2 : 0;
    const observe = (v) => { wCount++; const d1 = v - wMean; wMean += d1 / wCount; wM2 += d1 * (v - wMean); };
    let effT = spec.temperature; // refined below if there's enough history, frozen after burn-in (same freeze rule as scale adaptation)
    if (wCount > 10) {
      const std = Math.sqrt(wM2 / (wCount - 1));
      effT = spec.temperature * Math.max(std / 3, 1e-9);
    }

    const trace = [];
    let chainAccepted = 0;
    let chainProposed = 0;
    // A warm chain's scale/effT are already the frozen, post-burn-in
    // values a fresh run would have spent spec.burnIn evaluations
    // climbing toward -- so it skips burn-in entirely and goes straight
    // to sample collection. This is the concrete cost this cache saves:
    // spec.burnIn evaluations (50-2000, default 400) not re-spent
    // re-deriving an adaptation a previous call already did.
    const burnInEnd = warm ? 0 : spec.burnIn;
    const total = warm ? spec.samples : spec.burnIn + spec.samples;

    for (let i = 0; i < total; i++) {
      if (evaluations >= MAX_TOTAL_EVALS || Date.now() - startedAt > TIME_BUDGET_MS) { timedOut = true; break; }

      // Propose: symmetric Gaussian step per dimension, reflected into
      // the domain; integer params take a discretized step of at least 1.
      const prop = {};
      spec.params.forEach((p, d) => {
        let step = gauss() * scales[d];
        if (p.integer) {
          step = Math.round(step);
          if (step === 0) step = rand() < 0.5 ? -1 : 1; // never propose "stay put"; wastes the evaluation
        }
        prop[p.name] = reflect(state[p.name] + step, p.domain[0], p.domain[1]);
        if (p.integer) prop[p.name] = Math.round(prop[p.name]);
      });

      const propVal = evalObjective(prop);
      chainProposed++;
      if (propVal !== -Infinity && i < burnInEnd) observe(propVal);
      // Log-space Metropolis acceptance: min(1, exp(Δ/T)). Working with
      // the margin difference directly (rather than a density RATIO, as
      // naive implementations do) cannot overflow, cannot divide by
      // zero, and handles -Infinity (invalid region) for free.
      const accept = propVal >= stateVal || rand() < Math.exp((propVal - stateVal) / effT);
      if (accept && propVal !== -Infinity) {
        Object.assign(state, prop);
        stateVal = propVal;
        chainAccepted++;
      }

      if (stateVal !== -Infinity && (!best || stateVal > best.margin)) {
        best = { point: { ...state }, margin: stateVal };
      }

      if (i < burnInEnd) {
        // Temperature from the landscape's measured spread (see above),
        // then Robbins-Monro-style scale adaptation toward the target
        // acceptance; both burn-in ONLY, frozen after (see module
        // comment on why adapting forever is wrong).
        if (wCount > 10) {
          const std = Math.sqrt(wM2 / (wCount - 1));
          effT = spec.temperature * Math.max(std / 3, 1e-9);
        }
        const rate = chainProposed > 0 ? chainAccepted / chainProposed : 0;
        const nudge = Math.exp((rate - targetAccept) * 0.05);
        for (let d = 0; d < scales.length; d++) {
          scales[d] = Math.min(Math.max(scales[d] * nudge, spec.params[d].integer ? 1 : 1e-12), widths[d]);
        }
      } else if (stateVal !== -Infinity) {
        trace.push(stateVal);
      }

      if (i % Math.max(1, Math.floor(total / 40)) === 0) {
        bestTrace.push(best ? best.margin : null);
      }
    }

    accepted += chainAccepted;
    proposed += chainProposed;
    acceptRates.push(chainProposed > 0 ? chainAccepted / chainProposed : 0);
    if (trace.length > 0) chainTraces.push(trace);

    // Carry this chain's end-of-call state forward under the search's
    // fingerprint, whether it ran warm or cold, so the NEXT call (if its
    // fingerprint still matches) can resume from here. A chain that hit
    // the time/eval budget partway through is still a valid resumable
    // state -- it just didn't get through all `total` iterations this
    // call.
    chainStates[c] = { rand, gauss, state: { ...state }, scales: scales.slice(), wCount, wMean, wM2 };
    if (timedOut) break;
  }
  // Trim to the chain count actually in play; a previous call's cache
  // under this fingerprint made with MORE chains than this call requests
  // would otherwise leave stale, never-updated extra entries sitting
  // around indefinitely. specHash is stored alongside so the NEXT call
  // can tell a genuine edit apart from an exact repeat (see above).
  mhLruSet(fingerprint, { specHash, chains: chainStates.slice(0, spec.chains) });

  const rHat = gelmanRubin(chainTraces);
  const acceptanceRate = proposed > 0 ? accepted / proposed : 0;
  const converged = rHat !== null && rHat < 1.1;

  if (!best || evaluations < 50) {
    return {
      verdict: 'inconclusive',
      reason: `Only ${evaluations} evaluations completed; the objective failed almost everywhere in the stated domain.`,
      evaluations, seed: MCMC_SEED,
    };
  }

  const violated = best.margin > 0;
  // Round the reported point for readability; but re-verify the margin
  // AT the rounded point, so the number a human plugs back in by hand is
  // the number the verdict actually rests on (a point rounded for
  // display that no longer violates would be a false counterexample).
  let reportPoint = best.point;
  if (violated) {
    const rounded = {};
    for (const p of spec.params) rounded[p.name] = p.integer ? Math.round(best.point[p.name]) : Number(best.point[p.name].toPrecision(6));
    try {
      const v = objective(rounded);
      if (Number.isFinite(v) && v > 0) reportPoint = rounded;
    } catch { /* keep the full-precision point */ }
  }

  return {
    verdict: violated ? 'violated' : 'held',
    bestPoint: reportPoint,
    bestMargin: violated && reportPoint !== best.point ? objective(reportPoint) : best.margin,
    evaluations,
    chainsRun: acceptRates.length,
    acceptanceRate: Number(acceptanceRate.toFixed(3)),
    rHat: rHat === null ? null : Number(rHat.toFixed(3)),
    converged,
    trace: bestTrace,
    seed: MCMC_SEED,
    partial: timedOut ? `stopped at the ${TIME_BUDGET_MS}ms/${MAX_TOTAL_EVALS}-evaluation budget; coverage is whatever the chains reached, not the full plan` : null,
    // The permanent honesty note, carried IN the result so no renderer
    // can forget it: an adaptive search that found nothing proves less
    // than a grid that found nothing, and neither proves absence.
    honesty: violated
      ? 'The counterexample point above is concrete. Plug it into the claim by hand to confirm.'
      : `No violating point found along ${acceptRates.length} chains (${evaluations} evaluations)${converged ? '' : rHat === null ? ', convergence could not be assessed' : `, R-hat ${rHat.toFixed(2)}; chains did NOT converge, treat as exploratory`}. Absence of a found counterexample is not evidence of absence.`,
  };
}

// ============================================================
// SMC MODE (upgrade 2) — parallel Sequential Monte Carlo sampler.
// ============================================================
//
// WHAT THIS IS. N particles, spread across the SAME 4-worker pool
// kernelWorkerPool.js already runs upgrade 1's kernel dispatch through
// (never a second, nested pool). Each generation: every worker (i) moves
// its own slice of particles [i*N/4, (i+1)*N/4) with a small seeded
// random-walk step, (ii) evaluates the claim's objective at each moved
// particle via mathExpr.js — the exact same trusted expression engine MH
// uses, recompiled once per worker per objective string, cached, (iii)
// writes each margin into the shared weights array, then (iv) calls the
// hand-verified WASM function (smcWasm.js) to convert its slice's margins
// into unnormalized importance weights in place: weight = exp(margin /
// temperature), clamped against overflow. No inter-worker communication
// happens during that — every worker only ever touches its own slice.
// Once all 4 workers finish a generation, the MAIN thread (never a
// worker) does systematic resampling — one O(N) pass over the now-full
// weight vector, using only the two buffers pre-allocated once at search
// start (ancestorIndices, scratchPositions; see executeSmcSearch) — then
// the next generation's move step starts from the resampled particles.
// Temperature anneals geometrically from SMC_TEMPERATURE_INITIAL down to
// SMC_TEMPERATURE_FINAL across the run, sharpening the target
// distribution over time (standard annealed-SMC practice) rather than
// searching at a single fixed scale for the whole budget.
//
// WHY run() IS ASYNC HERE, AND WHY THAT'S SAFE. kernelRegistry.js
// documents every kernel's run() as "pure and synchronous" — true for
// every kernel including MH, and still true in spirit for SMC in that it
// has no observable side effects beyond its own private WASM memory, but
// mechanically it has to be async: it dispatches real worker_threads
// tasks and must await their completion every generation. orchestrator.js
// already awaits every kernel's run() unconditionally since upgrade 2 (a
// no-op for every synchronous kernel, MH included), so this is a
// transparent extension of the existing contract, not a break of it.
//
// REPRODUCIBILITY. Every particle's randomness (initial position, and
// each generation's move step) derives from combineSeed(masterSeed,
// particleIndex, generation) — never from anything scheduling-dependent
// (worker order, task completion order, timing). Two runs with the same
// spec.N/params/objective/seed produce bit-for-bit identical particle
// trajectories and results, regardless of how the pool happens to
// schedule the 4 slice tasks across generations. Systematic resampling's
// own randomness (one uniform draw per generation) is likewise seeded
// from combineSeed(masterSeed, 'resample', generation), never Math.random.
//
// This deliberately deviates from the literal "seed = masterSeed +
// sampleIndex" formula by also folding in the generation number: with
// ONLY masterSeed+sampleIndex, every particle would reseed to the exact
// same PRNG state at the start of every generation and therefore draw
// the IDENTICAL "random" step each time — not a random walk at all, just
// one fixed offset applied repeatedly. combineSeed keeps masterSeed and
// sampleIndex as the two dominant inputs (so "seed depends on the master
// seed and which sample" still holds) while making each generation's
// draw independent of the others, which a working move step requires.

import { instantiateSmcModule } from './smcWasm.js';
import { runSmcSlice } from './kernelWorkerPool.js';

// splitmix32-ish finalizer; deterministic, non-cryptographic, just needs
// good avalanche behavior so nearby (masterSeed, sampleIndex, generation)
// triples don't produce correlated streams. Exported so kernelWorker.js
// derives identical per-particle seeds without duplicating this logic.
export function combineSeed(masterSeed, sampleIndex, generation) {
  let h = (masterSeed ^ 0x9E3779B9) >>> 0;
  h = Math.imul(h ^ (sampleIndex >>> 0), 0x85EBCA6B) >>> 0;
  h = Math.imul(h ^ (generation >>> 0), 0xC2B2AE35) >>> 0;
  h = (h ^ (h >>> 16)) >>> 0;
  return h;
}

// Systematic resampling's one uniform draw per generation is seeded the
// same way as any particle's, just under a sample index no real particle
// ever uses (valid indices are 0..N-1, N capped at SMC_MAX_N well below
// this sentinel), so it can't collide with any particle's own stream.
const RESAMPLE_SEED_INDEX = 0xFFFFFFFF;
function resampleSeedFor(masterSeed, generation) {
  return combineSeed(masterSeed, RESAMPLE_SEED_INDEX, generation);
}

function pagesFor(floatCount) {
  const bytes = floatCount * 8;
  return Math.ceil(bytes / 65536); // WASM page = 64KiB
}

/**
 * The parallel SMC sampler. Async (see design note above). Returns the
 * same result shape as MH — { verdict, bestPoint, bestMargin,
 * evaluations, seed, ... } — plus SMC-specific fields (mode, particles,
 * generationsRun, effectiveSampleSize) so a caller reading the report
 * doesn't need to know which sampler produced it to get the verdict, but
 * can tell which one did from the fields present.
 */
async function executeSmcSearch(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason, evaluations: 0 };

  const N = spec.N;
  const D = spec.params.length;
  const objectiveNames = spec.params.map((p) => p.name);
  const objective = compileExpr(spec.objective, objectiveNames); // main-thread copy, used only for the final report-point re-verification (mirrors MH)

  const memory = new WebAssembly.Memory({ initial: pagesFor(N * D + N), maximum: pagesFor(N * D + N), shared: true });
  const samplesByteOffset = 0;
  const weightsByteOffset = N * D * 8;
  const weights = new Float64Array(memory.buffer, weightsByteOffset, N);

  // Pre-allocated ONCE, reused every generation; no `new Float64Array`
  // inside the generation loop, per the hot-path allocation rule.
  const ancestorIndices = new Int32Array(N);
  const scratchPositions = new Float64Array(N * D);
  const samplesView = new Float64Array(memory.buffer, samplesByteOffset, N * D); // main-thread view, used only for resampling's read/copy

  const startedAt = Date.now();
  let generation = 0;
  let totalEvaluations = 0;
  let best = null; // { point, margin }
  const marginTrace = []; // best margin per generation, mirrors MH's bestTrace
  let timedOut = false;
  let resampleSkippedGenerations = 0;
  let lastEss = null;

  const sliceBounds = Array.from({ length: SMC_WORKER_COUNT }, (_, w) => [
    Math.floor((w * N) / SMC_WORKER_COUNT),
    Math.floor(((w + 1) * N) / SMC_WORKER_COUNT),
  ]);

  for (; generation < SMC_MAX_GENERATIONS; generation++) {
    if (Date.now() - startedAt > SMC_TIME_BUDGET_MS) { timedOut = true; break; }

    const progress = SMC_MAX_GENERATIONS > 1 ? generation / (SMC_MAX_GENERATIONS - 1) : 1;
    const temperature = SMC_TEMPERATURE_INITIAL * Math.pow(SMC_TEMPERATURE_FINAL / SMC_TEMPERATURE_INITIAL, progress);
    // Move-step scale anneals alongside temperature: broad (domain-wide)
    // exploration early, narrow local refinement late, the same shape as
    // MH's own burn-in scale adaptation. Without this a fixed 15%-of-
    // domain-width step never shrinks, so on a wide domain SMC keeps
    // taking huge random jumps every generation even after the particle
    // population has already localized around a promising region.
    const moveScaleFactor = Math.sqrt(temperature / SMC_TEMPERATURE_INITIAL);

    const sliceResults = await Promise.all(sliceBounds.map(([start, end]) => runSmcSlice({
      memory,
      exprSource: spec.objective,
      varNames: objectiveNames,
      params: spec.params,
      D,
      N,
      start,
      end,
      samplesByteOffset,
      weightsByteOffset,
      temperature,
      moveScaleFactor,
      masterSeed: MCMC_SEED,
      generation,
      isFirstGeneration: generation === 0,
    })));

    for (const r of sliceResults) {
      totalEvaluations += r.evaluations;
      if (r.bestMargin !== null && (!best || r.bestMargin > best.margin)) {
        best = { point: r.bestPoint, margin: r.bestMargin };
      }
    }
    marginTrace.push(best ? best.margin : null);

    // Systematic resampling — one O(N) pass, main thread only, using the
    // pre-allocated scratch buffers. weights[] is turned into its own
    // prefix sum in place (no extra buffer needed for the cumulative
    // array), then consumed; the next generation's WASM step overwrites
    // it fresh with new margins regardless, so reusing it here costs
    // nothing.
    let total = 0;
    for (let i = 0; i < N; i++) { total += weights[i]; weights[i] = total; }

    if (!Number.isFinite(total) || total <= 0) {
      // Every particle's weight collapsed to (numerically) zero — e.g. a
      // uniformly hopeless region under this generation's temperature.
      // Resampling would divide by zero; skip it honestly and keep the
      // current particles rather than fabricate a result.
      resampleSkippedGenerations++;
      lastEss = 0;
      continue;
    }

    // Effective sample size from the (already-collapsed-to-cumulative)
    // weights: recover each weight as a consecutive difference, NORMALIZE
    // by total before squaring, sum of squares gives ESS = 1/sum(wn^2),
    // the standard SMC diagnostic (1 = totally degenerate, N = perfectly
    // balanced). Normalizing first (rather than squaring the raw weights
    // and dividing by total^2) matters in practice: at low annealed
    // temperatures every raw weight can be individually so small that
    // squaring it underflows to exactly 0 even though the weights are
    // perfectly fine relative to each other — normalized weights stay in
    // a sane range regardless of how small the raw magnitudes get.
    let sumSq = 0;
    let prev = 0;
    for (let i = 0; i < N; i++) { const wn = (weights[i] - prev) / total; prev = weights[i]; sumSq += wn * wn; }
    lastEss = sumSq > 0 ? 1 / sumSq : 0;

    const u0 = mulberry32(resampleSeedFor(MCMC_SEED, generation))() * (total / N);
    let j = 0;
    for (let k = 0; k < N; k++) {
      const threshold = u0 + k * (total / N);
      while (j < N - 1 && weights[j] < threshold) j++;
      ancestorIndices[k] = j;
    }
    for (let k = 0; k < N; k++) {
      const src = ancestorIndices[k] * D;
      const dst = k * D;
      for (let d = 0; d < D; d++) scratchPositions[dst + d] = samplesView[src + d];
    }
    samplesView.set(scratchPositions);
  }

  if (!best || totalEvaluations < 50) {
    return {
      verdict: 'inconclusive',
      reason: `Only ${totalEvaluations} evaluations completed across ${generation} generation(s); the objective failed almost everywhere in the stated domain.`,
      evaluations: totalEvaluations, seed: MCMC_SEED, mode: 'smc', particles: N,
    };
  }

  const violated = best.margin > 0;
  let reportPoint = best.point;
  if (violated) {
    const rounded = {};
    for (const p of spec.params) rounded[p.name] = p.integer ? Math.round(best.point[p.name]) : Number(best.point[p.name].toPrecision(6));
    try {
      const v = objective(rounded);
      if (Number.isFinite(v) && v > 0) reportPoint = rounded;
    } catch { /* keep the full-precision point */ }
  }

  return {
    verdict: violated ? 'violated' : 'held',
    bestPoint: reportPoint,
    bestMargin: violated && reportPoint !== best.point ? objective(reportPoint) : best.margin,
    evaluations: totalEvaluations,
    mode: 'smc',
    particles: N,
    generationsRun: generation,
    effectiveSampleSize: lastEss === null ? null : Number(lastEss.toFixed(1)),
    trace: marginTrace,
    seed: MCMC_SEED,
    partial: timedOut ? `stopped at the ${SMC_TIME_BUDGET_MS}ms/${SMC_MAX_GENERATIONS}-generation budget; coverage is whatever the particles reached, not the full plan` : null,
    resampleSkippedGenerations: resampleSkippedGenerations || undefined,
    honesty: violated
      ? 'The counterexample point above is concrete. Plug it into the claim by hand to confirm.'
      : `No violating point found across ${N} particles over ${generation} generation(s) (${totalEvaluations} evaluations)${lastEss !== null ? `, effective sample size ${lastEss.toFixed(1)}/${N} at the final generation` : ''}. Absence of a found counterexample is not evidence of absence.`,
  };
}

// The prompt the model answers to DESIGN a search; mirrors the numeric
// check prompt's discipline: exact JSON shapes, exact constraints, an
// explicit "none" escape hatch so an unsuitable claim gets an honest
// refusal instead of a forced spec.
export function buildMcmcPrompt(node) {
  return `Design an MCMC COUNTEREXAMPLE SEARCH for this claim: an adaptive random-walk search that concentrates thousands of deterministic evaluations where the claim looks weakest. You design the search space and objective; the device executes it against the trusted arithmetic kernel.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Define:
- "params": 1-${MAX_PARAMS} parameters with finite numeric domains, the quantities the claim ranges over. Mark integer-valued ones with "integer": true.
- "objective": a single expression in those params, the VIOLATION MARGIN. It must be POSITIVE exactly where the claim is VIOLATED, and negative (more negative = safer) where it holds. Example: for a claim "f(x) stays below g(x)", the margin is "f(x) - g(x)". The sampler climbs this margin; if it finds any point with margin > 0, that point is a concrete counterexample.

Float expression grammar: + - * / % ^, sin cos tan asin acos atan atan2 sinh cosh tanh exp log ln log2 log10 sqrt cbrt abs sign floor ceil round min max pow mod, constants pi e tau. No equality operators, encode "equals k" as "-abs(x - k) + epsilon" style margins.

MULTI-STEP PROCESSES: "objective" may use let-bindings to name intermediate quantities instead of inlining one enormous expression: "let m1 = ...; let v1 = ...; ...; FINAL_MARGIN". Each binding is "let NAME = EXPR;" and may only reference params and EARLIER bindings, never itself or a later one; names must be unique and cannot reuse a param name. Use this to unroll a recurrence (an update rule applied several times) a few steps at a time, rather than inlining the whole thing into one line. EVERY declared param must actually affect the objective's value somewhere in the expression; a param the objective never uses wastes a search dimension and the spec will be rejected.

Return ONLY JSON, one of:
{"kind":"mcmc_search","note":"what this searches for and why the margin is shaped this way","params":[{"name":"x","domain":[0,100]},{"name":"n","domain":[1,5000],"integer":true}],"objective":"...","temperature":1}
{"kind":"none","reason":"why this claim cannot be reduced to a violation-margin search, an honest refusal beats a fake spec"}

Rules: the margin must genuinely encode the claim's failure, not a proxy for interestingness. If the claim involves exact divisibility, factorials, or BigInt-scale integers, return "none"; this instrument is float-only; the exact-mode numeric check is the right tool there.`;
}
