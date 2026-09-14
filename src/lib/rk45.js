// rk45.js; adaptive-step Dormand-Prince RK45, the smooth integrator's
// stepping algorithm (upgrade 3). Generic — knows nothing about MLPs or
// WASM, just a caller-supplied evalField(t, y, out) that fills `out`
// with dy/dt at (t, y). dynamicsCheck.js's smooth mode wires this up to
// mlpWasm.js's forward pass; tests/rk45.test.mjs exercises it directly
// against closed-form solutions.
//
// Butcher tableau: the standard Dormand-Prince RK45 (the same one behind
// MATLAB's ode45 and scipy's RK45) — 7 stages, a 5th-order solution used
// to advance the state and an embedded 4th-order solution used only to
// estimate local error and drive step-size control. Validated directly
// against a closed-form solution (undamped harmonic oscillator) before
// being trusted: relative error ~3e-13 over t in [0,10] at tight
// tolerances, consistent with the method's known order of accuracy.
//
// Stage vectors k1-k7 (this file's `k[0..6]`), plus the working buffers
// `ytmp`/`y5`, are allocated ONCE per createIntegrator() call and reused
// for every step of every trial that integrator handles — never
// reallocated inside the step loop.

const C = [0, 1 / 5, 3 / 10, 4 / 5, 8 / 9, 1, 1];
const A = [
  [],
  [1 / 5],
  [3 / 40, 9 / 40],
  [44 / 45, -56 / 15, 32 / 9],
  [19372 / 6561, -25360 / 2187, 64448 / 6561, -212 / 729],
  [9017 / 3168, -355 / 33, 46732 / 5247, 49 / 176, -5103 / 18656],
  [35 / 384, 0, 500 / 1113, 125 / 192, -2187 / 6784, 11 / 84],
];
const B5 = [35 / 384, 0, 500 / 1113, 125 / 192, -2187 / 6784, 11 / 84, 0];
const B4 = [5179 / 57600, 0, 7571 / 16695, 393 / 640, -92097 / 339200, 187 / 2100, 1 / 40];

const DEFAULT_OPTS = { atol: 1e-9, rtol: 1e-9, safety: 0.9, minFactor: 0.2, maxFactor: 5, maxSteps: 100000 };

/**
 * Creates a reusable RK45 integrator for a `dim`-dimensional state.
 * `opts` overrides atol/rtol/safety/minFactor/maxFactor/maxSteps.
 * Every buffer (k1-k7, ytmp, y5) is allocated here, once, and reused by
 * every call to `advanceTo` this integrator ever makes.
 */
export function createIntegrator(dim, opts = {}) {
  const { atol, rtol, safety, minFactor, maxFactor, maxSteps } = { ...DEFAULT_OPTS, ...opts };
  const k = Array.from({ length: 7 }, () => new Float64Array(dim));
  const ytmp = new Float64Array(dim);
  const y5 = new Float64Array(dim);

  // One attempted step from (t, y) with trial step size h. Fills k[0..6]
  // and y5 (the candidate 5th-order state); returns the scaled error
  // norm (<=1 means accept). Never mutates `y` itself.
  function attemptStep(evalField, t, y, h) {
    for (let s = 0; s < 7; s++) {
      for (let d = 0; d < dim; d++) {
        let acc = y[d];
        for (let j = 0; j < s; j++) acc += h * A[s][j] * k[j][d];
        ytmp[d] = acc;
      }
      evalField(t + C[s] * h, ytmp, k[s]);
    }
    let errNorm = 0;
    for (let d = 0; d < dim; d++) {
      let y5d = y[d];
      let errd = 0;
      for (let j = 0; j < 7; j++) { y5d += h * B5[j] * k[j][d]; errd += h * (B5[j] - B4[j]) * k[j][d]; }
      y5[d] = y5d;
      const scale = atol + rtol * Math.max(Math.abs(y[d]), Math.abs(y5d));
      errNorm += (errd / scale) ** 2;
    }
    return Math.sqrt(errNorm / dim);
  }

  /**
   * Advances `y` (a Float64Array, mutated in place) from `t` to
   * `tTarget`, taking as many adaptive steps as needed, clamping the
   * final substep so it lands exactly on `tTarget`. `h` is the initial
   * step-size guess for this call (typically the previous call's
   * returned `h`, so step size carries over smoothly between
   * checkpoints instead of resetting every time).
   *
   * Returns { t: tTarget, h: nextStepSizeGuess, steps, rejections }.
   */
  function advanceTo(evalField, y, t, tTarget, h) {
    let steps = 0;
    let rejections = 0;
    while (t < tTarget - 1e-13) {
      let hUse = h;
      if (t + hUse > tTarget) hUse = tTarget - t;
      const errNorm = attemptStep(evalField, t, y, hUse);
      // A non-finite vector field (the MLP producing NaN/Infinity, or a
      // step that blows the state up past what f64 can represent) makes
      // errNorm NaN forever; `errNorm <= 1` is always false for NaN, so
      // without this check the loop would grind through up to maxSteps
      // rejected steps before giving up instead of failing fast.
      if (!Number.isFinite(errNorm)) throw new Error(`RK45: non-finite error estimate at t=${t} (vector field likely produced NaN/Infinity)`);
      if (errNorm <= 1) {
        t += hUse;
        y.set(y5);
        steps++;
        const factor = errNorm === 0 ? maxFactor : Math.min(maxFactor, Math.max(minFactor, safety * errNorm ** (-1 / 5)));
        h = hUse * factor;
      } else {
        rejections++;
        h = hUse * Math.max(minFactor, safety * errNorm ** (-1 / 5));
      }
      if (steps + rejections > maxSteps) throw new Error(`RK45 exceeded maxSteps (${maxSteps}) advancing to t=${tTarget}`);
    }
    return { t: tTarget, h, steps, rejections };
  }

  return { advanceTo };
}
