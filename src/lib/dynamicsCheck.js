// dynamicsCheck.js; deterministic trajectory-equivalence checking, the
// instrument that gives CLAIMS ABOUT DYNAMICS a computed tier.
//
// WHY THIS EXISTS. The existing kernels decide identities, inequalities,
// integer searches, and static consistency. None of them can touch the
// commonest claim in dynamical-systems work: "system B reduces to system
// A under conditions C". That claim was the load-bearing theorem of a
// real paper run through Ciall, and the model tier could only paraphrase
// the paper's own assertion back at it. Yet the claim is CHECKABLE: put
// both systems on the same seeded initial conditions, integrate both on
// this device, and watch whether the mapped trajectories agree. If they
// ever disagree beyond tolerance, that is a concrete counterexample with
// coordinates, a time, and a component; new information by construction,
// because nobody plugged in that initial condition before.
//
// THE HONESTY CONTRACT, same shape as every other instrument here:
//   - "diverged" is decisive ONLY after it survives its own audit: the
//     divergence must persist when the trial is re-integrated at half
//     the step size. A fixed-step integrator can manufacture divergence
//     on stiff dynamics, and reporting an integration artifact as a
//     counterexample to a published theorem is the worst failure this
//     file could produce. If halving the step kills the divergence, the
//     verdict is 'inconclusive', named as a step-size artifact.
//   - "matched" is WEAK and says so: agreement along a handful of seeded
//     trajectories at one step size proves nothing about all initial
//     conditions. The result carries this caveat permanently.
//   - Everything is seeded (mulberry32, same PRNG discipline as the MCMC
//     and numeric kernels) and wall-clock bounded, so a re-run reproduces
//     every trial bit for bit.
//
// The model DESIGNS the spec (writes out both systems' derivatives
// component by component, the initial-condition embedding, and the
// comparison observables); the device EXECUTES it. The model can design
// the wrong check; it can no longer be wrong about what the trajectories
// actually do.

import { compileExpr } from './mathExpr.js';
import { createIntegrator } from './rk45.js';
import { createMlpVectorField, validateMlpWeights } from './mlpVectorField.js';

export const DYNAMICS_SEED = 0xD15C ^ 0x5EED; // distinct stream from CHECK_SEED and MCMC_SEED
const MAX_STATE_VARS = 24;      // 3 spins x 3 components x 2 systems fits well inside
const MAX_TRIALS = 8;
const MAX_STEPS_PER_TRIAL = 20000;
const TIME_BUDGET_MS = 4500;

// SMOOTH INTEGRATOR MODE (upgrade 3): {integrator:'smooth'} on the spec
// swaps the analytically-specified derivA/derivB (mathExpr expressions)
// for a 2-layer MLP vector field (mlpA/mlpB weights, evaluated via
// mlpWasm.js) and swaps the fixed-step RK4 stepper for adaptive RK45
// (Dormand-Prince, rk45.js). Everything else — stateA/stateB naming,
// seeded initial conditions, compareA/compareB observables, the
// decisiveness/step-halving audit — is untouched and shared with RK4
// mode via the small helpers below, not reimplemented. Default (no
// `integrator` field) is RK4, completely unchanged from before this
// upgrade: every line of the original normalizeDynamicsSpec/
// executeDynamicsCheck/runTrial/rk4Step stays exactly as it was.
const SMOOTH_DEFAULT_RTOL = 1e-9;
const SMOOTH_DEFAULT_ATOL = 1e-9;

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ---- Shared validation helpers ----
// Mechanically extracted from the original (pre-upgrade-3)
// normalizeDynamicsSpec, in the same order, with the same behavior —
// used by BOTH the RK4 path and the smooth path below so a future change
// to (say) how initA ranges are validated can't silently drift between
// the two modes. None of this changes what valid/invalid RK4 specs do;
// it's the same checks, just named.
function validateStateNames(raw) {
  const names = (arr, label) => {
    if (!Array.isArray(arr) || arr.length === 0) throw new Error(`${label} must be a non-empty array`);
    const out = arr.map((n) => String(n));
    if (new Set(out).size !== out.length) throw new Error(`Duplicate names in ${label}`);
    return out;
  };
  const stateA = names(raw.stateA, 'stateA');
  const stateB = names(raw.stateB, 'stateB');
  if (stateA.length + stateB.length > MAX_STATE_VARS) throw new Error(`State too large; ${MAX_STATE_VARS} variables max across both systems`);
  if (stateA.some((n) => stateB.includes(n))) throw new Error('stateA and stateB must not share variable names');
  return { stateA, stateB };
}

function validateParamsObj(raw) {
  const params = {};
  for (const [k, v] of Object.entries(raw.params || {})) {
    if (!Number.isFinite(v)) throw new Error(`Param "${k}" is not a finite number`);
    params[k] = v;
  }
  return { params, paramNames: Object.keys(params) };
}

function validateInitB(raw, stateB, stateA, paramNames) {
  if (!Array.isArray(raw.initB) || raw.initB.length !== stateB.length) throw new Error(`initB must have exactly ${stateB.length} entries`);
  return raw.initB.map((src) => compileExpr(String(src), [...stateA, ...paramNames])); // parse NOW; fail loud
}

function validateCompare(raw, stateA, stateB, paramNames) {
  if (!Array.isArray(raw.compareA) || !Array.isArray(raw.compareB) || raw.compareA.length !== raw.compareB.length || raw.compareA.length === 0) {
    throw new Error('compareA and compareB must be non-empty arrays of equal length');
  }
  const compareA = raw.compareA.map((s) => compileExpr(String(s), [...stateA, ...paramNames]));
  const compareB = raw.compareB.map((s) => compileExpr(String(s), [...stateB, ...paramNames]));
  return { compareA, compareB };
}

function validateInitA(raw, stateA) {
  if (!Array.isArray(raw.initA) || raw.initA.length !== stateA.length) throw new Error(`initA must have exactly ${stateA.length} entries`);
  return raw.initA.map((ic, i) => {
    const r = ic?.range;
    if (!Array.isArray(r) || r.length !== 2 || !r.every(Number.isFinite) || r[0] > r[1]) throw new Error(`Bad range for initA[${i}]`);
    return { range: [r[0], r[1]] };
  });
}

function validateTiming(raw) {
  const T = Number(raw.T), dt = Number(raw.dt), tolerance = Number(raw.tolerance);
  if (!(T > 0) || !(dt > 0)) throw new Error('T and dt must be positive');
  if (Math.ceil(T / dt) > MAX_STEPS_PER_TRIAL) throw new Error(`T/dt exceeds ${MAX_STEPS_PER_TRIAL} steps`);
  if (!(tolerance > 0)) throw new Error('tolerance must be positive');
  return { T, dt, tolerance };
}

const clampInt = (v, lo, hi, dflt) => (Number.isFinite(v) ? Math.max(lo, Math.min(hi, Math.round(v))) : dflt);

/**
 * Spec shape (model-written, therefore attacker-shaped; everything is
 * validated and compiled NOW, loudly, before any integration starts):
 * {
 *   kind: 'dynamics_equivalence', note,
 *   stateA: ['th1', ...],            // state variable names, system A
 *   derivA: ['expr in stateA+params', ...],   // dA_i/dt, one per state var
 *   stateB: ['s1x', ...],
 *   derivB: ['expr in stateB+params', ...],
 *   initA:  [{ range: [lo, hi] }, ...],       // seeded-random ICs, one per A var
 *   initB:  ['expr in stateA vars', ...],     // B's ICs EMBED A's (e.g. cos(th1))
 *   compareA: ['expr in stateA', ...],        // observables, pairwise vs compareB
 *   compareB: ['expr in stateB', ...],
 *   params: { name: number, ... },
 *   T, dt, tolerance, trials?
 * }
 *
 * OR, smooth mode (upgrade 3, opt in with integrator:'smooth'):
 * { ...same stateA/stateB/initA/initB/compareA/compareB/params/T/dt/
 *   tolerance/trials as above, integrator:'smooth',
 *   mlpA: {w1,b1,w2,b2}, mlpB: {w1,b1,w2,b2},   // replace derivA/derivB
 *   rtol?, atol? }                              // RK45 tolerances
 * `dt` doubles as the initial adaptive step-size guess AND the
 * comparison-checkpoint spacing (T/dt checkpoints, same resolution
 * concept as RK4's fixed-step comparison count).
 */
export function normalizeDynamicsSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Dynamics spec is not an object');
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };
  if (raw.kind !== 'dynamics_equivalence') throw new Error(`Unknown dynamics spec kind "${raw.kind}"`);

  if (raw.integrator === 'smooth') return normalizeSmoothSpec(raw);

  const { stateA, stateB } = validateStateNames(raw);
  const { params, paramNames } = validateParamsObj(raw);

  const exprList = (arr, label, expectedLen, vars) => {
    if (!Array.isArray(arr) || arr.length !== expectedLen) throw new Error(`${label} must have exactly ${expectedLen} entries`);
    return arr.map((src) => compileExpr(String(src), vars)); // parse NOW; fail loud
  };
  const derivA = exprList(raw.derivA, 'derivA', stateA.length, [...stateA, ...paramNames]);
  const derivB = exprList(raw.derivB, 'derivB', stateB.length, [...stateB, ...paramNames]);
  const initB = validateInitB(raw, stateB, stateA, paramNames);
  const { compareA, compareB } = validateCompare(raw, stateA, stateB, paramNames);
  const initA = validateInitA(raw, stateA);
  const { T, dt, tolerance } = validateTiming(raw);

  return {
    kind: 'dynamics_equivalence',
    note: String(raw.note || '').slice(0, 200),
    stateA, stateB, derivA, derivB, initA, initB, compareA, compareB,
    params, T, dt, tolerance,
    trials: clampInt(raw.trials, 1, MAX_TRIALS, 3),
    // Raw sources kept so the result (and any exported report) can show
    // exactly what was integrated, not a paraphrase of it.
    sources: {
      derivA: raw.derivA.map(String), derivB: raw.derivB.map(String),
      initB: raw.initB.map(String), compareA: raw.compareA.map(String), compareB: raw.compareB.map(String),
    },
  };
}

function normalizeSmoothSpec(raw) {
  const { stateA, stateB } = validateStateNames(raw);
  const { params, paramNames } = validateParamsObj(raw);
  const initB = validateInitB(raw, stateB, stateA, paramNames);
  const { compareA, compareB } = validateCompare(raw, stateA, stateB, paramNames);
  const initA = validateInitA(raw, stateA);
  const { T, dt, tolerance } = validateTiming(raw);

  const mlpA = validateMlpWeights(raw.mlpA, stateA.length, 'mlpA');
  const mlpB = validateMlpWeights(raw.mlpB, stateB.length, 'mlpB');
  const rtol = Number.isFinite(raw.rtol) && raw.rtol > 0 ? raw.rtol : SMOOTH_DEFAULT_RTOL;
  const atol = Number.isFinite(raw.atol) && raw.atol > 0 ? raw.atol : SMOOTH_DEFAULT_ATOL;

  return {
    kind: 'dynamics_equivalence',
    integrator: 'smooth',
    note: String(raw.note || '').slice(0, 200),
    stateA, stateB, initA, initB, compareA, compareB,
    params, T, dt, tolerance, rtol, atol,
    trials: clampInt(raw.trials, 1, MAX_TRIALS, 3),
    mlpA, mlpB,
    sources: {
      initB: raw.initB.map(String), compareA: raw.compareA.map(String), compareB: raw.compareB.map(String),
    },
  };
}

// Classic fixed-step RK4 over a named-variable state; no adaptivity on
// purpose: determinism and reproducibility outrank efficiency here, and
// the half-step audit below covers the step-size risk honestly.
function rk4Step(state, names, derivs, params, dt) {
  const evalDerivs = (s) => {
    const env = { ...params };
    for (let i = 0; i < names.length; i++) env[names[i]] = s[i];
    return derivs.map((f) => f(env));
  };
  const add = (s, k, h) => s.map((v, i) => v + h * k[i]);
  const k1 = evalDerivs(state);
  const k2 = evalDerivs(add(state, k1, dt / 2));
  const k3 = evalDerivs(add(state, k2, dt / 2));
  const k4 = evalDerivs(add(state, k3, dt));
  return state.map((v, i) => v + (dt / 6) * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]));
}

function runTrial(spec, a0, dt) {
  const { stateA, stateB, derivA, derivB, params } = spec;
  const envA0 = { ...params };
  stateA.forEach((n, i) => { envA0[n] = a0[i]; });
  let a = a0.slice();
  let b = spec.initB.map((f) => f(envA0));

  const steps = Math.ceil(spec.T / dt);
  let worst = { deviation: 0, t: 0, observable: 0 };
  for (let s = 0; s <= steps; s++) {
    const envA = { ...params }; stateA.forEach((n, i) => { envA[n] = a[i]; });
    const envB = { ...params }; stateB.forEach((n, i) => { envB[n] = b[i]; });
    for (let c = 0; c < spec.compareA.length; c++) {
      const va = spec.compareA[c](envA);
      const vb = spec.compareB[c](envB);
      if (!Number.isFinite(va) || !Number.isFinite(vb)) {
        return { blewUp: true, t: s * dt };
      }
      const d = Math.abs(va - vb);
      if (d > worst.deviation) worst = { deviation: d, t: s * dt, observable: c };
    }
    if (s < steps) {
      a = rk4Step(a, stateA, derivA, params, dt);
      b = rk4Step(b, stateB, derivB, params, dt);
    }
  }
  return { blewUp: false, worst };
}

// THE AUDIT, shared by both RK4 and smooth mode: a divergence found at
// nominal resolution only counts once it survives a stricter
// re-integration — dt/2 for RK4, halved tolerances (+ halved initial
// step guess) for smooth, since an adaptive stepper has no single dt to
// halve. Both modes call this SAME function with their own audit run, so
// the actual decision arithmetic — what "survived" means — is one piece
// of code, not two copies that could drift apart.
function decisivenessSurvived(auditRun, nominalDeviation, tolerance) {
  return !auditRun.blewUp && auditRun.worst.deviation > tolerance && auditRun.worst.deviation > nominalDeviation * 0.5;
}

/**
 * Executes a normalized dynamics_equivalence spec: RK4 (the default) or,
 * if spec.integrator === 'smooth', the adaptive RK45 + MLP path below.
 * RK4's own path is untouched by this dispatch — same function body,
 * same synchronous return, as before this upgrade.
 */
export function executeDynamicsCheck(spec) {
  if (spec.integrator === 'smooth') return executeDynamicsCheckSmooth(spec);
  return executeDynamicsCheckRK4(spec);
}

/**
 * The original fixed-step RK4 check. Pure, synchronous, deterministic.
 * Returns { verdict: 'diverged'|'matched'|'inconclusive', ...evidence }.
 * 'diverged' means: at the reported initial condition the two systems'
 * observables disagree beyond tolerance, and the divergence SURVIVED
 * re-integration at half the step size. Unchanged by upgrade 3 — every
 * line below is exactly what executeDynamicsCheck used to be before
 * smooth mode existed.
 */
function executeDynamicsCheckRK4(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason, trialsRun: 0 };
  const rand = mulberry32(DYNAMICS_SEED);
  const startedAt = Date.now();

  const trials = [];
  let worstOverall = null;

  for (let t = 0; t < spec.trials; t++) {
    if (Date.now() - startedAt > TIME_BUDGET_MS) break;
    const a0 = spec.initA.map(({ range }) => range[0] + rand() * (range[1] - range[0]));
    const run = runTrial(spec, a0, spec.dt);

    if (run.blewUp) {
      // Non-finite values are an integration failure, not a finding.
      trials.push({ a0, blewUp: true, t: run.t });
      continue;
    }

    const record = { a0, maxDeviation: run.worst.deviation, at: run.worst.t, observable: run.worst.observable };
    trials.push(record);
    if (!worstOverall || record.maxDeviation > worstOverall.maxDeviation) worstOverall = record;

    if (record.maxDeviation > spec.tolerance) {
      // THE AUDIT: a fixed-step integrator can fake divergence on stiff
      // dynamics. Re-run this exact trial at dt/2; the finding stands
      // only if most of the deviation survives.
      const audit = runTrial(spec, a0, spec.dt / 2);
      const survived = decisivenessSurvived(audit, record.maxDeviation, spec.tolerance);
      if (survived) {
        return {
          verdict: 'diverged',
          witness: {
            initialCondition: Object.fromEntries(spec.stateA.map((n, i) => [n, Number(a0[i].toPrecision(6))])),
            maxDeviation: record.maxDeviation,
            atTime: record.at,
            observableIndex: record.observable,
            observableA: spec.sources.compareA[record.observable],
            observableB: spec.sources.compareB[record.observable],
            confirmedAtHalfStep: audit.worst.deviation,
          },
          trialsRun: trials.length,
          seed: DYNAMICS_SEED,
          honesty: 'The initial condition above is concrete; integrate both systems from it yourself to confirm. The divergence persisted at half the step size, so it is a property of the dynamics as specified, not of the integrator.',
        };
      }
      return {
        verdict: 'inconclusive',
        reason: `A deviation of ${record.maxDeviation.toPrecision(3)} appeared at dt=${spec.dt} but ${audit.blewUp ? 'the half-step re-integration failed' : `shrank to ${audit.worst.deviation.toPrecision(3)} at dt=${spec.dt / 2}`}; this is a step-size artifact, not a counterexample, and is reported as neither match nor divergence.`,
        trialsRun: trials.length,
        seed: DYNAMICS_SEED,
      };
    }
  }

  const clean = trials.filter((t) => !t.blewUp);
  if (clean.length === 0) {
    return { verdict: 'inconclusive', reason: 'Every trial produced non-finite values; the spec as written cannot be integrated.', trialsRun: trials.length, seed: DYNAMICS_SEED };
  }
  return {
    verdict: 'matched',
    maxDeviation: worstOverall.maxDeviation,
    tolerance: spec.tolerance,
    trialsRun: clean.length,
    seed: DYNAMICS_SEED,
    // The permanent caveat, carried IN the result so no renderer can
    // forget it; mirrors the MCMC honesty note word for word in spirit.
    honesty: `The mapped observables agreed within ${spec.tolerance} across ${clean.length} seeded trajectories at step ${spec.dt}. That is evidence along these trajectories only; it is not a proof of equivalence for all initial conditions, and it inherits the integrator's finite accuracy.`,
  };
}

// One trial, smooth mode: integrate both systems with adaptive RK45 over
// their MLP vector fields, comparing at a fixed set of checkpoint times
// (T/dt of them, same resolution concept as RK4's per-step comparison).
// Between checkpoints each system's stepper is free to take however many
// adaptive substeps it needs — `advanceTo` handles that internally and
// clamps the final substep to land exactly on the checkpoint, so A and B
// are always compared at IDENTICAL times despite integrating
// independently. `integA`/`integB`/`evalFieldA`/`evalFieldB` are built
// once per executeDynamicsCheckSmooth call and passed in here, reused
// across every trial — never rebuilt per trial or per step.
function runTrialSmooth(spec, a0, h0, integA, integB, evalFieldA, evalFieldB) {
  const { stateA, stateB, params } = spec;
  const envA0 = { ...params };
  stateA.forEach((n, i) => { envA0[n] = a0[i]; });

  const a = Float64Array.from(a0);
  const b = Float64Array.from(spec.initB.map((f) => f(envA0)));

  const numCheckpoints = Math.max(1, Math.ceil(spec.T / spec.dt));
  const checkpointDt = spec.T / numCheckpoints;
  let tA = 0, hA = h0;
  let tB = 0, hB = h0;

  let worst = { deviation: 0, t: 0, observable: 0 };
  for (let s = 0; s <= numCheckpoints; s++) {
    const envA = { ...params }; stateA.forEach((n, i) => { envA[n] = a[i]; });
    const envB = { ...params }; stateB.forEach((n, i) => { envB[n] = b[i]; });
    for (let c = 0; c < spec.compareA.length; c++) {
      const va = spec.compareA[c](envA);
      const vb = spec.compareB[c](envB);
      if (!Number.isFinite(va) || !Number.isFinite(vb)) return { blewUp: true, t: s * checkpointDt };
      const d = Math.abs(va - vb);
      if (d > worst.deviation) worst = { deviation: d, t: s * checkpointDt, observable: c };
    }
    if (s < numCheckpoints) {
      const target = (s + 1) * checkpointDt;
      try {
        const rA = integA.advanceTo(evalFieldA, a, tA, target, hA);
        tA = rA.t; hA = rA.h;
        const rB = integB.advanceTo(evalFieldB, b, tB, target, hB);
        tB = rB.t; hB = rB.h;
      } catch {
        // RK45 gave up (non-finite field, or exceeded its step budget) —
        // an integration failure, not a finding, exactly like RK4's
        // non-finite-observable case above.
        return { blewUp: true, t: target };
      }
    }
  }
  return { blewUp: false, worst };
}

/**
 * The smooth integrator: adaptive RK45 over an MLP vector field instead
 * of fixed-step RK4 over analytical derivatives. Same result shape as
 * RK4 mode, same decisiveness audit (decisivenessSurvived, shared code
 * above) — a divergence must survive a stricter re-integration
 * (tolerances halved, initial step guess halved: the adaptive-stepper
 * translation of "re-run at half the step size") before it counts.
 */
function executeDynamicsCheckSmooth(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason, trialsRun: 0 };
  const rand = mulberry32(DYNAMICS_SEED);
  const startedAt = Date.now();

  const evalFieldA = createMlpVectorField(spec.mlpA, spec.stateA.length);
  const evalFieldB = createMlpVectorField(spec.mlpB, spec.stateB.length);
  const integA = createIntegrator(spec.stateA.length, { rtol: spec.rtol, atol: spec.atol });
  const integB = createIntegrator(spec.stateB.length, { rtol: spec.rtol, atol: spec.atol });
  const auditIntegA = createIntegrator(spec.stateA.length, { rtol: spec.rtol / 2, atol: spec.atol / 2 });
  const auditIntegB = createIntegrator(spec.stateB.length, { rtol: spec.rtol / 2, atol: spec.atol / 2 });

  const trials = [];
  let worstOverall = null;

  for (let t = 0; t < spec.trials; t++) {
    if (Date.now() - startedAt > TIME_BUDGET_MS) break;
    const a0 = spec.initA.map(({ range }) => range[0] + rand() * (range[1] - range[0]));
    const run = runTrialSmooth(spec, a0, spec.dt, integA, integB, evalFieldA, evalFieldB);

    if (run.blewUp) {
      trials.push({ a0, blewUp: true, t: run.t });
      continue;
    }

    const record = { a0, maxDeviation: run.worst.deviation, at: run.worst.t, observable: run.worst.observable };
    trials.push(record);
    if (!worstOverall || record.maxDeviation > worstOverall.maxDeviation) worstOverall = record;

    if (record.maxDeviation > spec.tolerance) {
      // THE AUDIT (see decisivenessSurvived above): tighter tolerances
      // and a smaller initial step guess, the adaptive-stepper analogue
      // of RK4's "re-run at dt/2."
      const audit = runTrialSmooth(spec, a0, spec.dt / 2, auditIntegA, auditIntegB, evalFieldA, evalFieldB);
      const survived = decisivenessSurvived(audit, record.maxDeviation, spec.tolerance);
      if (survived) {
        return {
          verdict: 'diverged',
          witness: {
            initialCondition: Object.fromEntries(spec.stateA.map((n, i) => [n, Number(a0[i].toPrecision(6))])),
            maxDeviation: record.maxDeviation,
            atTime: record.at,
            observableIndex: record.observable,
            observableA: spec.sources.compareA[record.observable],
            observableB: spec.sources.compareB[record.observable],
            confirmedAtHalfStep: audit.worst.deviation,
          },
          trialsRun: trials.length,
          seed: DYNAMICS_SEED,
          integrator: 'smooth',
          honesty: 'The initial condition above is concrete; integrate both systems from it yourself to confirm. The divergence persisted under a tighter adaptive tolerance, so it is a property of the dynamics as specified, not of the integrator.',
        };
      }
      return {
        verdict: 'inconclusive',
        reason: `A deviation of ${record.maxDeviation.toPrecision(3)} appeared at rtol=${spec.rtol} but ${audit.blewUp ? 'the tightened re-integration failed' : `shrank to ${audit.worst.deviation.toPrecision(3)} at rtol=${spec.rtol / 2}`}; this is a step-size artifact, not a counterexample, and is reported as neither match nor divergence.`,
        trialsRun: trials.length,
        seed: DYNAMICS_SEED,
        integrator: 'smooth',
      };
    }
  }

  const clean = trials.filter((t) => !t.blewUp);
  if (clean.length === 0) {
    return { verdict: 'inconclusive', reason: 'Every trial produced non-finite values; the spec as written cannot be integrated.', trialsRun: trials.length, seed: DYNAMICS_SEED, integrator: 'smooth' };
  }
  return {
    verdict: 'matched',
    maxDeviation: worstOverall.maxDeviation,
    tolerance: spec.tolerance,
    trialsRun: clean.length,
    seed: DYNAMICS_SEED,
    integrator: 'smooth',
    honesty: `The mapped observables agreed within ${spec.tolerance} across ${clean.length} seeded trajectories (adaptive RK45, rtol=${spec.rtol}). That is evidence along these trajectories only; it is not a proof of equivalence for all initial conditions, and it inherits the integrator's finite accuracy.`,
  };
}

// The prompt the model answers to DESIGN a dynamics check; same
// discipline as the MCMC and numeric prompts: exact JSON shape, every
// derivative written out component by component, an explicit "none"
// escape hatch so an unsuitable claim gets an honest refusal.
export function buildDynamicsPrompt(node) {
  return `Design a DYNAMICS EQUIVALENCE CHECK for this claim, if it asserts that one dynamical system reduces to, reproduces, or is equivalent to another under stated conditions. The device will integrate BOTH systems deterministically (seeded RK4, on-device) from shared random initial conditions and compare chosen observables along the trajectories. A confirmed divergence is a concrete counterexample; agreement is weak supporting evidence and will be reported as weak.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Requirements:
- Write every derivative EXPLICITLY, one expression per state component, using only: + - * / % ^, sin cos tan asin acos atan atan2 sinh cosh tanh exp log ln sqrt abs sign floor ceil round min max pow mod, constants pi e tau, your named params, and the state variable names. No summation notation; expand sums by hand. Keep systems small (2-4 oscillators / spins) so expansion stays manageable.
- "initA": a seeded-random range per system-A variable. "initB": expressions embedding A's initial state into B (the reduction's stated correspondence).
- "compareA"/"compareB": observables that SHOULD agree if the claim holds. Prefer coordinates over raw angles (cos/sin of an angle rather than the angle) so wrap-around cannot fake a divergence.
- "tolerance": generous enough that integrator error cannot cross it (1e-6 is typical for smooth systems at dt <= 0.01 over T <= 20).

Respond ONLY with JSON, one of:
{"kind":"dynamics_equivalence","note":"what reduction is being tested","stateA":["th1","th2"],"derivA":["...","..."],"stateB":["s1x","s1y","s2x","s2y"],"derivB":["...","...","...","..."],"initA":[{"range":[0,6.28]},{"range":[0,6.28]}],"initB":["cos(th1)","sin(th1)","cos(th2)","sin(th2)"],"compareA":["cos(th1)","sin(th1)"],"compareB":["s1x","s1y"],"params":{"w1":1.0,"w2":1.3,"lam":2.0},"T":10,"dt":0.005,"tolerance":1e-6,"trials":3}
{"kind":"none","reason":"why this claim is not a testable dynamics-reduction statement; an honest refusal beats a forced spec"}`;
}
