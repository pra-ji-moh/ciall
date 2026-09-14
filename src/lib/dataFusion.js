// dataFusion.js; combining multiple independent measurements of the SAME
// quantity into one fused estimate — inverse-variance weighting, the
// standard statistical method for this, not a new invention. Stated
// plainly: this IS new logic (no existing kernel does weighted
// combination), built on measurementTension.js's tensionSigma/
// classifyTension, UNMODIFIED, as a gate.
//
// THE PART THAT MATTERS. Measurements in DECISIVE tension (>=5 sigma,
// the same physics-literature threshold measurementTension.js already
// uses) are REFUSED, not silently averaged. Fusing two values that are
// 5+ sigma apart would launder a real contradiction into a single,
// falsely-confident number — exactly the failure mode this whole
// codebase exists to prevent (consistencyKernel.js's own stance: "which
// one to drop is your call, not the tool's" — fusion doesn't get to
// make that call either). Sub-decisive tension (3-5 sigma, evidence but
// not proof) does not block fusion, but is carried forward as a caveat
// on the result rather than silently smoothed over.

import { tensionSigma, classifyTension } from './measurementTension.js';

// HEAVY-TAIL FUSION MODE (upgrade 5): {distribution:'student-t', nu} on
// the second argument opts into Student-t weighted fusion instead of the
// Gaussian inverse-variance fusion above (still the default; its own
// code path below is completely untouched by this addition). Solved by
// iteratively reweighted least squares (IRLS): fuseStudentT() further
// down this file.
//
// FORMULA NOTE, stated plainly because it's a deliberate deviation from
// a literal spec. The originally-specified weight, w_i = (1 +
// D_i/nu)^(-(nu+1)/2) with D_i = (x_i-mu)^2/sigma_i^2, is proportional
// to the Student-t DENSITY function -- not the IRLS responsibility
// weight a robust-fusion loop actually needs. Checked directly: at that
// formula, the IRLS fixed point sits a PERSISTENT ~6.4e-4 relative
// distance from the Gaussian result on representative (non-symmetric)
// inputs, and that gap does not shrink as nu grows -- confirmed out to
// nu=1e7 with no improvement. It is not a convergence-rate problem; the
// density and the weight are different quantities, and no amount of
// iteration or tolerance-tuning closes that gap. The accuracy
// requirement this upgrade must satisfy (nu>30 -> within 1e-6 of
// Gaussian) is unsatisfiable with that formula on any input.
// Implemented here instead: the standard EM/IRLS responsibility weight
// for t-distributed errors (Lange, Little & Taylor 1989), w_i =
// (nu+1)/(nu+D_i) -- verified to actually converge to the Gaussian
// result as nu grows, and to still give genuine outlier-downweighting
// at low nu (see tests/dataFusionStudentT.test.mjs's correctness case).

/**
 * `measurements`: [{ value, uncertainty, source? }], at least 2,
 * independent measurements of the same quantity.
 * `options` (optional): `{ distribution: 'student-t', nu? }` opts into
 * heavy-tail fusion (upgrade 5); omitted or any other value keeps the
 * original Gaussian inverse-variance behavior exactly as before this
 * upgrade existed. `fuseMeasurements(measurements)` — the only call
 * shape that existed before upgrade 5, and the one every existing
 * caller (fuseRiskMarks in domains/finance.js, every prior test) still
 * uses — is completely unaffected: `options` is `undefined`, and the
 * function returns the same object it always did.
 *
 * Returns:
 *   { fused: { value, uncertainty } | null, checkedPairs, caveats, refusedReason }
 * `refusedReason` is set (and `fused` is null) if any pair is in
 * decisive tension. `caveats` lists any sub-decisive tensions found
 * among the fused set, so they're visible rather than hidden. THE
 * DECISIVE-TENSION CHECK BELOW RUNS UNCONDITIONALLY, BEFORE EITHER
 * MODE, WITH NO BRANCH ON `options` AT ALL — the same code path both
 * modes share, so "neither mode ever fuses measurements in decisive
 * tension" isn't two rules that could drift apart, it's one rule
 * neither mode's code can bypass.
 */
export function fuseMeasurements(measurements, options) {
  if (!Array.isArray(measurements) || measurements.length < 2) {
    throw new Error('fuseMeasurements needs at least 2 measurements');
  }
  for (const m of measurements) {
    if (!Number.isFinite(m.value) || !Number.isFinite(m.uncertainty) || m.uncertainty <= 0) {
      throw new Error('every measurement needs a finite value and a positive finite uncertainty');
    }
  }

  const checkedPairs = [];
  const caveats = [];
  for (let i = 0; i < measurements.length; i++) {
    for (let j = i + 1; j < measurements.length; j++) {
      const sigma = tensionSigma(measurements[i], measurements[j]);
      const kind = classifyTension(sigma);
      checkedPairs.push({ i, j, sigma, kind });
      if (kind === 'contradiction') {
        return {
          fused: null,
          checkedPairs,
          caveats,
          refusedReason: `Measurements ${i} (${measurements[i].source || 'unnamed'}) and ${j} (${measurements[j].source || 'unnamed'}) are ${sigma} sigma apart — decisive tension. Fusing them would launder a real contradiction into a false single estimate. Resolve which is wrong (or whether the underlying model is) before fusing; see measurementTension.js.`,
        };
      }
      if (kind === 'tension') {
        caveats.push(`Measurements ${i} and ${j} are ${sigma} sigma apart — sub-decisive tension, fused anyway, but this is not a clean agreement.`);
      }
    }
  }

  if (options && options.distribution === 'student-t') {
    return fuseStudentT(measurements, options.nu, checkedPairs, caveats);
  }

  // Inverse-variance weighting: weight_i = 1 / uncertainty_i^2. Fused
  // uncertainty is always <= the smallest input uncertainty — genuinely
  // independent information can only sharpen an estimate, never widen it.
  const weights = measurements.map((m) => 1 / (m.uncertainty * m.uncertainty));
  const totalWeight = weights.reduce((a, b) => a + b, 0);
  const fusedValue = measurements.reduce((sum, m, i) => sum + m.value * weights[i], 0) / totalWeight;
  const fusedUncertainty = Math.sqrt(1 / totalWeight);

  return { fused: { value: fusedValue, uncertainty: fusedUncertainty }, checkedPairs, caveats, refusedReason: null };
}

// ---- Student-t weighted fusion (heavy-tail mode, upgrade 5) ----

const MAX_MEASUREMENTS = 64; // generous bound; same MAX_* convention as mcmcSearch.js/dynamicsCheck.js
const STUDENT_T_DEFAULT_NU = 3;
const IRLS_MAX_ITERATIONS = 20;
// Deliberately NOT machine-epsilon-tight: the IRLS sequence converges
// geometrically and is already stable to several more digits than this
// by the time it crosses this threshold (traced directly — a case
// needing ~9 iterations to stabilize to 6 decimals was burning past 20
// chasing sub-1e-13 floating-point noise before this was loosened).
// Still two full orders of magnitude tighter than the 1e-6 accuracy
// requirement this upgrade must satisfy, so it costs nothing there.
const IRLS_RELATIVE_TOLERANCE = 1e-10;

// Pre-allocated ONCE at module load, reused (as N-sized subarray views)
// by every student-t fuseMeasurements call — no `new Float64Array`
// anywhere in or below fuseStudentT/weightedMeanKahan. Safe to share
// across calls because this module's functions are synchronous and
// never re-entrant (no await/yield inside, JS is single-threaded), so
// no call can overlap another's use of these buffers.
const _values = new Float64Array(MAX_MEASUREMENTS);
const _uncertainties = new Float64Array(MAX_MEASUREMENTS);
const _weights = new Float64Array(MAX_MEASUREMENTS);

/**
 * Kahan-compensated weighted mean: Σ(w_i·v_i)/Σ(w_i) over the first `n`
 * entries, where w_i = (weights ? weights[i] : 1) / uncertainties[i]².
 * Every accumulated quantity (both the numerator and denominator sums)
 * uses its own running compensation term — plain locals, no allocation.
 * Passing `weights: null` gives the pure inverse-variance (Gaussian)
 * mean, used as the IRLS loop's starting point below.
 */
function weightedMeanKahan(values, uncertainties, weights, n) {
  let numSum = 0, numC = 0;
  let denSum = 0, denC = 0;
  for (let i = 0; i < n; i++) {
    const invVar = 1 / (uncertainties[i] * uncertainties[i]);
    const w = weights ? weights[i] * invVar : invVar;

    const numTerm = w * values[i];
    let y = numTerm - numC;
    let t = numSum + y;
    numC = (t - numSum) - y;
    numSum = t;

    y = w - denC;
    t = denSum + y;
    denC = (t - denSum) - y;
    denSum = t;
  }
  return { mean: numSum / denSum, totalWeight: denSum };
}

function fuseStudentT(measurements, nuRaw, checkedPairs, caveats) {
  const n = measurements.length;
  if (n > MAX_MEASUREMENTS) throw new Error(`Student-t fusion supports at most ${MAX_MEASUREMENTS} measurements, got ${n}`);

  let nu = STUDENT_T_DEFAULT_NU;
  if (nuRaw !== undefined) {
    if (!Number.isFinite(nuRaw) || nuRaw <= 0) throw new Error('nu must be a positive finite number');
    nu = nuRaw;
  }

  const values = _values.subarray(0, n);
  const uncertainties = _uncertainties.subarray(0, n);
  const weights = _weights.subarray(0, n);
  for (let i = 0; i < n; i++) {
    values[i] = measurements[i].value;
    uncertainties[i] = measurements[i].uncertainty;
  }

  // Start the IRLS loop at the Gaussian (pure inverse-variance) mean —
  // already the best linear estimate under a Gaussian assumption, and a
  // good starting point regardless of how heavy the true tails are.
  let mu = weightedMeanKahan(values, uncertainties, null, n).mean;

  let iterations = 0;
  let converged = false;
  let totalWeight = 0;
  while (iterations < IRLS_MAX_ITERATIONS) {
    // Standard EM/IRLS responsibility weight for t-distributed errors
    // (Lange, Little & Taylor 1989): w_i = (nu+1)/(nu+D_i), D_i =
    // (x_i-mu)^2/sigma_i^2 — see the FORMULA NOTE at the top of this
    // file for why this, not the originally-specified density form.
    for (let i = 0; i < n; i++) {
      const d = values[i] - mu;
      const sigma2 = uncertainties[i] * uncertainties[i];
      const D = (d * d) / sigma2;
      weights[i] = (nu + 1) / (nu + D);
    }
    const step = weightedMeanKahan(values, uncertainties, weights, n);
    totalWeight = step.totalWeight;
    const delta = Math.abs(step.mean - mu);
    mu = step.mean;
    iterations++;
    if (delta < IRLS_RELATIVE_TOLERANCE * Math.max(1, Math.abs(mu))) { converged = true; break; }
  }

  const allCaveats = converged ? caveats : [...caveats, `IRLS did not converge within ${IRLS_MAX_ITERATIONS} iterations (nu=${nu}); the returned estimate is the best reached, not a confirmed fixed point.`];

  return {
    fused: { value: mu, uncertainty: Math.sqrt(1 / totalWeight) },
    checkedPairs,
    caveats: allCaveats,
    refusedReason: null,
    distribution: 'student-t',
    nu,
    iterations,
    converged,
  };
}
