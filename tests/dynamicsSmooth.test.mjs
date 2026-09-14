// dynamicsSmooth.test.mjs; upgrade 3 coverage — the smooth integrator
// mode (adaptive RK45 over an MLP vector field) in dynamicsCheck.js,
// alongside (never replacing) the existing fixed-step RK4 mode. Also
// covers rk45.js and mlpWasm.js/mlpVectorField.js directly, since
// neither had a dedicated test file before this upgrade added them.

import test from 'node:test';
import assert from 'node:assert/strict';

import { createIntegrator } from '../src/lib/rk45.js';
import { MLP_WASM_BYTES, HIDDEN_DIM, instantiateMlpModule } from '../src/lib/mlpWasm.js';
import { createMlpVectorField, validateMlpWeights } from '../src/lib/mlpVectorField.js';
import { normalizeDynamicsSpec, executeDynamicsCheck } from '../src/lib/dynamicsCheck.js';

// Builds MLP weights that reproduce a LINEAR map f(y) = A*y + c to
// O(epsilon^2) precision, via a tiny-epsilon tanh-linearization: hidden
// unit j (for j < D) acts as a near-identity passthrough of y[j]
// (tanh(epsilon*y[j]) ~ epsilon*y[j] for small epsilon), and the second
// layer undoes the epsilon scaling while mixing through A. This is how
// the accuracy-regression tests below construct an MLP whose vector
// field is KNOWN (not learned, not approximated by fitting) to match a
// specific analytical system used in the equivalent RK4-mode spec.
// Validated directly (below) to stay well under the 1e-8 bound this
// upgrade requires, at epsilon=1e-5.
function linearToMlpWeights(A, c, D, epsilon = 1e-5) {
  const w1 = new Array(D * HIDDEN_DIM).fill(0);
  const b1 = new Array(HIDDEN_DIM).fill(0);
  const w2 = new Array(HIDDEN_DIM * D).fill(0);
  const b2 = new Array(D).fill(0);
  for (let i = 0; i < D; i++) w1[i * HIDDEN_DIM + i] = epsilon;
  for (let j = 0; j < D; j++) for (let k = 0; k < D; k++) w2[j * D + k] = A[k][j] / epsilon;
  for (let k = 0; k < D; k++) b2[k] = c[k];
  return { w1, b1, w2, b2 };
}

// ---- rk45.js ----

test('RK45: matches the closed-form solution of an undamped harmonic oscillator', () => {
  const integ = createIntegrator(2, { atol: 1e-13, rtol: 1e-13 });
  const field = (t, y, out) => { out[0] = y[1]; out[1] = -y[0]; };
  const y = new Float64Array([1.3, -0.7]);
  let t = 0, h = 0.01;
  for (let cp = 1; cp <= 10; cp++) { const r = integ.advanceTo(field, y, t, cp, h); t = r.t; h = r.h; }
  const T = 10, x0 = 1.3, v0 = -0.7;
  const exactX = x0 * Math.cos(T) + v0 * Math.sin(T);
  const exactV = -x0 * Math.sin(T) + v0 * Math.cos(T);
  assert.ok(Math.abs(y[0] - exactX) / Math.abs(exactX) < 1e-10);
  assert.ok(Math.abs(y[1] - exactV) / Math.abs(exactV) < 1e-10);
});

test('RK45: a non-finite vector field fails fast rather than grinding through maxSteps', () => {
  const integ = createIntegrator(1, { maxSteps: 50 });
  const field = (t, y, out) => { out[0] = NaN; };
  const y = new Float64Array([1]);
  assert.throws(() => integ.advanceTo(field, y, 0, 1, 0.1), /non-finite/);
});

// ---- mlpWasm.js / mlpVectorField.js ----

test('MLP WASM forward pass matches a plain-JS reference implementation across several input dimensions', () => {
  function refForward(x, w1, b1, w2, b2, D) {
    const hidden = new Array(HIDDEN_DIM);
    for (let j = 0; j < HIDDEN_DIM; j++) {
      let s = b1[j];
      for (let i = 0; i < D; i++) s += x[i] * w1[i * HIDDEN_DIM + j];
      hidden[j] = Math.tanh(s);
    }
    const out = new Array(D);
    for (let k = 0; k < D; k++) {
      let s = b2[k];
      for (let j = 0; j < HIDDEN_DIM; j++) s += hidden[j] * w2[j * D + k];
      out[k] = s;
    }
    return out;
  }

  for (const D of [1, 2, 8, 24]) {
    const rand = (seed => () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return (seed / 0x7fffffff) * 2 - 1; })(D * 999 + 7);
    const x = Array.from({ length: D }, () => rand());
    const w1 = Array.from({ length: D * HIDDEN_DIM }, () => rand() * 0.1);
    const b1 = Array.from({ length: HIDDEN_DIM }, () => rand() * 0.1);
    const w2 = Array.from({ length: HIDDEN_DIM * D }, () => rand() * 0.1);
    const b2 = Array.from({ length: D }, () => rand() * 0.1);

    const weights = validateMlpWeights({ w1, b1, w2, b2 }, D, 'test');
    const evalField = createMlpVectorField(weights, D);
    const out = new Float64Array(D);
    evalField(0, Float64Array.from(x), out);
    const ref = refForward(x, w1, b1, w2, b2, D);
    for (let i = 0; i < D; i++) {
      assert.ok(Math.abs(out[i] - ref[i]) < 1e-12, `dim ${D} component ${i}: wasm=${out[i]} ref=${ref[i]}`);
    }
  }
});

test('MLP WASM: a call only ever touches its own x/W1/b1/W2/b2/hidden/out regions, never the bytes around them', () => {
  const memory = new WebAssembly.Memory({ initial: 4, maximum: 4096, shared: true });
  const mlpForward = instantiateMlpModule(memory);
  const view = new Float64Array(memory.buffer);
  const D = 3;
  // Lay everything out with sentinel gaps before/after each region.
  const SENTINEL = 0xDEAD;
  view.fill(SENTINEL);
  let off = 4; // leading guard floats
  const xOff = off * 8; off += D;
  off += 2; // guard
  const w1Off = off * 8; off += D * HIDDEN_DIM;
  off += 2;
  const b1Off = off * 8; off += HIDDEN_DIM;
  off += 2;
  const w2Off = off * 8; off += HIDDEN_DIM * D;
  off += 2;
  const b2Off = off * 8; off += D;
  off += 2;
  const hiddenOff = off * 8; off += HIDDEN_DIM;
  off += 2;
  const outOff = off * 8; off += D;
  off += 4; // trailing guard

  // Re-fill sentinel everywhere, then set only the real inputs to non-sentinel values.
  view.fill(SENTINEL);
  for (let i = 0; i < D; i++) view[xOff / 8 + i] = 0.1 * (i + 1);
  for (let i = 0; i < D * HIDDEN_DIM; i++) view[w1Off / 8 + i] = 0.01;
  for (let i = 0; i < HIDDEN_DIM; i++) view[b1Off / 8 + i] = 0;
  for (let i = 0; i < HIDDEN_DIM * D; i++) view[w2Off / 8 + i] = 0.01;
  for (let i = 0; i < D; i++) view[b2Off / 8 + i] = 0;

  mlpForward(xOff, w1Off, b1Off, w2Off, b2Off, hiddenOff, outOff, D);

  // Everything strictly before xOff and strictly after outOff's region must still be SENTINEL.
  const firstTouchedIdx = xOff / 8;
  const lastTouchedIdx = outOff / 8 + D - 1;
  for (let i = 0; i < firstTouchedIdx; i++) assert.equal(view[i], SENTINEL, `byte before region corrupted at float index ${i}`);
  for (let i = lastTouchedIdx + 1; i < view.length; i++) assert.equal(view[i], SENTINEL, `byte after region corrupted at float index ${i}`);
});

test('linearToMlpWeights: reproduces a target linear map to well under 1e-8 relative error at epsilon=1e-5', () => {
  const A = [[0, 1], [-1, -0.1]]; // damped oscillator
  const c = [0, 0];
  const weights = validateMlpWeights(linearToMlpWeights(A, c, 2, 1e-5), 2, 'test');
  const evalField = createMlpVectorField(weights, 2);
  for (const y of [[1.3, -0.7], [0.5, 0.5], [-1.9, 1.9]]) {
    const out = new Float64Array(2);
    evalField(0, Float64Array.from(y), out);
    const exact = [A[0][0] * y[0] + A[0][1] * y[1], A[1][0] * y[0] + A[1][1] * y[1]];
    for (let i = 0; i < 2; i++) {
      const rel = Math.abs(out[i] - exact[i]) / Math.max(1e-12, Math.abs(exact[i]));
      assert.ok(rel < 1e-8, `rel=${rel} at y=${y}`);
    }
  }
});

// ---- dynamicsCheck.js normalize ----

test('normalizeDynamicsSpec: RK4 mode (no integrator field) is unaffected by smooth mode existing', () => {
  const spec = normalizeDynamicsSpec({
    kind: 'dynamics_equivalence',
    stateA: ['th', 'w'], stateB: ['s', 'p'],
    derivA: ['w', '-th'], derivB: ['p', '-s'],
    initA: [{ range: [-1, 1] }, { range: [-1, 1] }], initB: ['th', 'w'],
    compareA: ['th', 'w'], compareB: ['s', 'p'],
    params: {}, T: 5, dt: 0.01, tolerance: 1e-4,
  });
  assert.equal(spec.integrator, undefined);
  assert.ok(Array.isArray(spec.derivA));
});

test('normalizeDynamicsSpec: smooth mode rejects a malformed MLP weight shape', () => {
  assert.throws(() => normalizeDynamicsSpec({
    kind: 'dynamics_equivalence', integrator: 'smooth',
    stateA: ['th', 'w'], stateB: ['s', 'p'],
    mlpA: { w1: [1, 2], b1: [], w2: [], b2: [] }, // wrong length
    mlpB: { w1: new Array(2 * 64).fill(0), b1: new Array(64).fill(0), w2: new Array(64 * 2).fill(0), b2: [0, 0] },
    initA: [{ range: [-1, 1] }, { range: [-1, 1] }], initB: ['th', 'w'],
    compareA: ['th', 'w'], compareB: ['s', 'p'],
    params: {}, T: 5, dt: 0.01, tolerance: 1e-4,
  }), /mlpA\.w1/);
});

test('executeDynamicsCheck: both RK4 and smooth mode return a plain synchronous object, not a Promise', () => {
  const A = [[0, 1], [-1, 0]];
  const rk4Spec = normalizeDynamicsSpec({
    kind: 'dynamics_equivalence',
    stateA: ['th', 'w'], stateB: ['s', 'p'],
    derivA: ['w', '-th'], derivB: ['p', '-s'],
    initA: [{ range: [-1, 1] }, { range: [-1, 1] }], initB: ['th', 'w'],
    compareA: ['th', 'w'], compareB: ['s', 'p'],
    params: {}, T: 2, dt: 0.01, tolerance: 1e-4, trials: 1,
  });
  const rk4Result = executeDynamicsCheck(rk4Spec);
  assert.equal(rk4Result instanceof Promise, false);

  const smoothSpec = normalizeDynamicsSpec({
    kind: 'dynamics_equivalence', integrator: 'smooth',
    stateA: ['th', 'w'], stateB: ['s', 'p'],
    mlpA: linearToMlpWeights(A, [0, 0], 2), mlpB: linearToMlpWeights(A, [0, 0], 2),
    initA: [{ range: [-1, 1] }, { range: [-1, 1] }], initB: ['th', 'w'],
    compareA: ['th', 'w'], compareB: ['s', 'p'],
    params: {}, T: 2, dt: 0.01, tolerance: 1e-4, trials: 1,
  });
  const smoothResult = executeDynamicsCheck(smoothSpec);
  assert.equal(smoothResult instanceof Promise, false);
});

// ---- Accuracy regression: smooth mode vs RK4 mode, same underlying dynamics ----

function accuracyCase(name, A, B, tolerance) {
  test(`accuracy regression (${name}): smooth mode matches RK4 mode within 1e-8 relative error`, () => {
    const baseSpec = {
      kind: 'dynamics_equivalence',
      stateA: ['th', 'w'], stateB: ['s', 'p'],
      initA: [{ range: [-2, 2] }, { range: [-2, 2] }], initB: ['th', 'w'],
      compareA: ['th', 'w'], compareB: ['s', 'p'],
      params: {}, T: 8, dt: 0.005, tolerance, trials: 4,
    };
    const exprFor = (M) => [`${M[0][0]}*th + ${M[0][1]}*w`, `${M[1][0]}*th + ${M[1][1]}*w`];
    const [dA0, dA1] = exprFor(A);
    const exprForB = (M) => [`${M[0][0]}*s + ${M[0][1]}*p`, `${M[1][0]}*s + ${M[1][1]}*p`];
    const [dB0, dB1] = exprForB(B);

    const rk4Spec = normalizeDynamicsSpec({ ...baseSpec, derivA: [dA0, dA1], derivB: [dB0, dB1] });
    const rk4Result = executeDynamicsCheck(rk4Spec);

    const smoothSpec = normalizeDynamicsSpec({
      ...baseSpec, integrator: 'smooth',
      mlpA: linearToMlpWeights(A, [0, 0], 2), mlpB: linearToMlpWeights(B, [0, 0], 2),
    });
    const smoothResult = executeDynamicsCheck(smoothSpec);

    assert.equal(rk4Result.verdict, smoothResult.verdict, `verdicts differ: RK4=${rk4Result.verdict} smooth=${smoothResult.verdict}`);

    const rk4Dev = rk4Result.maxDeviation ?? rk4Result.witness?.maxDeviation;
    const smoothDev = smoothResult.maxDeviation ?? smoothResult.witness?.maxDeviation;
    assert.ok(rk4Dev != null && smoothDev != null, 'both results must carry a maxDeviation to compare');
    const rel = Math.abs(rk4Dev - smoothDev) / Math.max(1e-300, Math.abs(rk4Dev));
    assert.ok(rel < 1e-8, `relative deviation ${rel} exceeds 1e-8 (RK4=${rk4Dev}, smooth=${smoothDev})`);
  });
}

// Case 1: a "matched" verdict (generous tolerance) on two slightly-detuned oscillators.
accuracyCase('detuned oscillators, matched', [[0, 1], [-1, 0]], [[0, 1], [-1.02, 0]], 1e6);
// Case 2: a "diverged" verdict (tight tolerance) on the same pair.
accuracyCase('detuned oscillators, diverged', [[0, 1], [-1, 0]], [[0, 1], [-1.02, 0]], 1e-4);
// Case 3: a damped vs undamped pair, different qualitative behavior.
accuracyCase('damped vs undamped, diverged', [[0, 1], [-1, -0.1]], [[0, 1], [-1, 0]], 1e-4);
