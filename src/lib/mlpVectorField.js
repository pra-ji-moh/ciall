// mlpVectorField.js; glue between mlpWasm.js (the compiled forward-pass
// module) and rk45.js (the generic stepper) for the smooth integrator
// (upgrade 3). rk45.js knows nothing about WASM or weight layout — it
// just calls evalField(t, y, out); this file is what builds that
// function for a specific MLP weight set.

import { instantiateMlpModule, HIDDEN_DIM } from './mlpWasm.js';

const MAX_PAGES = 4096;

/**
 * Validates a raw MLP weight spec against the required 2-layer
 * architecture for a system with `inputDim` state variables:
 * w1: inputDim*HIDDEN_DIM, b1: HIDDEN_DIM, w2: HIDDEN_DIM*inputDim,
 * b2: inputDim, every entry a finite number. Throws loudly on any
 * mismatch (model-written input, same discipline as every other spec
 * normalizer in this repo). Returns Float64Arrays ready to hand to
 * createMlpVectorField.
 */
export function validateMlpWeights(raw, inputDim, label) {
  const need = (name, expectedLen) => {
    const arr = raw?.[name];
    if (!Array.isArray(arr) && !(arr instanceof Float64Array)) throw new Error(`${label}.${name} must be an array`);
    if (arr.length !== expectedLen) throw new Error(`${label}.${name} must have exactly ${expectedLen} entries, got ${arr.length}`);
    const out = Float64Array.from(arr);
    for (let i = 0; i < out.length; i++) if (!Number.isFinite(out[i])) throw new Error(`${label}.${name}[${i}] is not a finite number`);
    return out;
  };
  return {
    w1: need('w1', inputDim * HIDDEN_DIM),
    b1: need('b1', HIDDEN_DIM),
    w2: need('w2', HIDDEN_DIM * inputDim),
    b2: need('b2', inputDim),
  };
}

function pagesFor(floatCount) {
  return Math.ceil((floatCount * 8) / 65536); // WASM page = 64KiB
}

/**
 * Builds a reusable `evalField(t, y, out)` function (the shape rk45.js's
 * advanceTo expects) backed by one WASM instance for this
 * (weights, inputDim) pair. Every WASM-side buffer — x, W1, b1, W2, b2,
 * the hidden-layer scratch, and out — is allocated ONCE here and reused
 * for every call; only the small state vector itself (`y`, `out`, each
 * `inputDim` floats) is copied in and out per call, keeping rk45.js
 * fully WASM-agnostic at negligible cost for state vectors this small
 * (at most MAX_STATE_VARS from dynamicsCheck.js).
 */
export function createMlpVectorField(weights, inputDim) {
  let off = 0; // running FLOAT index into the shared memory
  const xIdx = off; off += inputDim;
  const w1Idx = off; off += inputDim * HIDDEN_DIM;
  const b1Idx = off; off += HIDDEN_DIM;
  const w2Idx = off; off += HIDDEN_DIM * inputDim;
  const b2Idx = off; off += inputDim;
  const hiddenIdx = off; off += HIDDEN_DIM;
  const outIdx = off; off += inputDim;

  const memory = new WebAssembly.Memory({ initial: pagesFor(off), maximum: MAX_PAGES, shared: true });
  const mlpForward = instantiateMlpModule(memory);
  const view = new Float64Array(memory.buffer);

  view.set(weights.w1, w1Idx);
  view.set(weights.b1, b1Idx);
  view.set(weights.w2, w2Idx);
  view.set(weights.b2, b2Idx);

  const xView = new Float64Array(memory.buffer, xIdx * 8, inputDim);
  const outView = new Float64Array(memory.buffer, outIdx * 8, inputDim);
  const xOff = xIdx * 8, w1Off = w1Idx * 8, b1Off = b1Idx * 8, w2Off = w2Idx * 8, b2Off = b2Idx * 8, hiddenOff = hiddenIdx * 8, outOff = outIdx * 8;

  return function evalField(t, y, out) {
    xView.set(y);
    mlpForward(xOff, w1Off, b1Off, w2Off, b2Off, hiddenOff, outOff, inputDim);
    out.set(outView);
  };
}
