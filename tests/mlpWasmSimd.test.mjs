// mlpWasmSimd.test.mjs; upgrade 10 — validates the SIMD-vectorized MLP
// forward pass against the existing scalar mlpWasm.js: correctness
// across many dimensions (including odd inputDim, which exercises the
// scalar tail in layer 2's paired k-loop) and seeds, plus an HONEST
// (not asserted-to-a-flattering-threshold) wall-clock comparison —
// this repo's own benchmark discipline is to measure and report the
// real number, not gate on one that might not hold reliably. See
// mlpWasmSimd.js's file header for why the real result is modest and
// dimension-dependent, not a blanket 2x.

import test from 'node:test';
import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';
import { instantiateMlpModule, HIDDEN_DIM } from '../src/lib/mlpWasm.js';
import { instantiateMlpSimdModule, MLP_SIMD_WASM_BYTES } from '../src/lib/mlpWasmSimd.js';

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => { a |= 0; a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

function buildInputs(inputDim, seed) {
  const rand = mulberry32(seed);
  return {
    x: Array.from({ length: inputDim }, () => rand() * 4 - 2),
    w1: Array.from({ length: inputDim * HIDDEN_DIM }, () => rand() * 2 - 1),
    b1: Array.from({ length: HIDDEN_DIM }, () => rand() * 2 - 1),
    w2: Array.from({ length: HIDDEN_DIM * inputDim }, () => rand() * 2 - 1),
    b2: Array.from({ length: inputDim }, () => rand() * 2 - 1),
  };
}

function layoutOffsets(inputDim) {
  let off = 0;
  const xOff = off; off += inputDim * 8;
  const w1Off = off; off += inputDim * HIDDEN_DIM * 8;
  const b1Off = off; off += HIDDEN_DIM * 8;
  const w2Off = off; off += HIDDEN_DIM * inputDim * 8;
  const b2Off = off; off += inputDim * 8;
  const hiddenOff = off; off += HIDDEN_DIM * 8;
  const outOff = off; off += inputDim * 8;
  return { xOff, w1Off, b1Off, w2Off, b2Off, hiddenOff, outOff, totalBytes: off };
}

function runBoth(inputDim, seed) {
  const { x, w1, b1, w2, b2 } = buildInputs(inputDim, seed);
  const layout = layoutOffsets(inputDim);
  const pages = Math.max(1, Math.ceil(layout.totalBytes / 65536) + 1);

  const memScalar = new WebAssembly.Memory({ initial: pages, maximum: pages, shared: true });
  const viewScalar = new Float64Array(memScalar.buffer);
  viewScalar.set(x, layout.xOff / 8);
  viewScalar.set(w1, layout.w1Off / 8);
  viewScalar.set(b1, layout.b1Off / 8);
  viewScalar.set(w2, layout.w2Off / 8);
  viewScalar.set(b2, layout.b2Off / 8);
  const mlpForward = instantiateMlpModule(memScalar);
  mlpForward(layout.xOff, layout.w1Off, layout.b1Off, layout.w2Off, layout.b2Off, layout.hiddenOff, layout.outOff, inputDim);
  const outScalar = Array.from(viewScalar.subarray(layout.outOff / 8, layout.outOff / 8 + inputDim));

  const memSimd = new WebAssembly.Memory({ initial: pages, maximum: pages, shared: true });
  const viewSimd = new Float64Array(memSimd.buffer);
  viewSimd.set(x, layout.xOff / 8);
  viewSimd.set(w1, layout.w1Off / 8);
  viewSimd.set(b1, layout.b1Off / 8);
  viewSimd.set(w2, layout.w2Off / 8);
  viewSimd.set(b2, layout.b2Off / 8);
  const mlpForwardSimd = instantiateMlpSimdModule(memSimd);
  mlpForwardSimd(layout.xOff, layout.w1Off, layout.b1Off, layout.w2Off, layout.b2Off, layout.hiddenOff, layout.outOff, inputDim);
  const outSimd = Array.from(viewSimd.subarray(layout.outOff / 8, layout.outOff / 8 + inputDim));

  return { outScalar, outSimd };
}

test('WebAssembly.validate() accepts the SIMD module', () => {
  assert.ok(WebAssembly.validate(MLP_SIMD_WASM_BYTES));
});

test('correctness: SIMD output matches scalar mlpForward across dims 1..24 (odd AND even -- odd exercises the scalar tail) and multiple seeds, well under the 1e-8 relative bound this repo already enforces for the smooth integrator', () => {
  let worstRel = 0;
  for (const inputDim of [1, 2, 3, 4, 5, 7, 8, 15, 16, 23, 24]) {
    for (const seed of [1, 2, 3, 42, 999]) {
      const { outScalar, outSimd } = runBoth(inputDim, seed);
      for (let i = 0; i < inputDim; i++) {
        const diff = Math.abs(outScalar[i] - outSimd[i]);
        const rel = Math.abs(outScalar[i]) > 1e-9 ? diff / Math.abs(outScalar[i]) : diff;
        worstRel = Math.max(worstRel, rel);
      }
    }
  }
  assert.ok(worstRel < 1e-8, `worst relative diff ${worstRel} exceeds the 1e-8 bound`);
});

test('correctness: odd inputDim specifically exercises the scalar tail in layer 2 and still matches', () => {
  for (const inputDim of [1, 3, 5, 7, 9, 11, 23]) {
    const { outScalar, outSimd } = runBoth(inputDim, 7);
    for (let i = 0; i < inputDim; i++) {
      assert.ok(Math.abs(outScalar[i] - outSimd[i]) < 1e-8, `inputDim=${inputDim} index ${i}: scalar=${outScalar[i]} simd=${outSimd[i]}`);
    }
  }
});

test('honest benchmark: real, measured, dimension-dependent speedup — reported, not gated on a specific ratio (see mlpWasmSimd.js for why the real number is modest, not a flat 2x)', () => {
  const REPS = 20000;
  for (const inputDim of [1, 8, 24]) {
    const { x, w1, b1, w2, b2 } = buildInputs(inputDim, 1);
    const layout = layoutOffsets(inputDim);
    const pages = Math.max(1, Math.ceil(layout.totalBytes / 65536) + 1);

    const memA = new WebAssembly.Memory({ initial: pages, maximum: pages, shared: true });
    const viewA = new Float64Array(memA.buffer);
    viewA.set(x, layout.xOff / 8); viewA.set(w1, layout.w1Off / 8); viewA.set(b1, layout.b1Off / 8); viewA.set(w2, layout.w2Off / 8); viewA.set(b2, layout.b2Off / 8);
    const scalarFn = instantiateMlpModule(memA);
    for (let i = 0; i < 500; i++) scalarFn(layout.xOff, layout.w1Off, layout.b1Off, layout.w2Off, layout.b2Off, layout.hiddenOff, layout.outOff, inputDim);
    const t0 = performance.now();
    for (let i = 0; i < REPS; i++) scalarFn(layout.xOff, layout.w1Off, layout.b1Off, layout.w2Off, layout.b2Off, layout.hiddenOff, layout.outOff, inputDim);
    const scalarMs = performance.now() - t0;

    const memB = new WebAssembly.Memory({ initial: pages, maximum: pages, shared: true });
    const viewB = new Float64Array(memB.buffer);
    viewB.set(x, layout.xOff / 8); viewB.set(w1, layout.w1Off / 8); viewB.set(b1, layout.b1Off / 8); viewB.set(w2, layout.w2Off / 8); viewB.set(b2, layout.b2Off / 8);
    const simdFn = instantiateMlpSimdModule(memB);
    for (let i = 0; i < 500; i++) simdFn(layout.xOff, layout.w1Off, layout.b1Off, layout.w2Off, layout.b2Off, layout.hiddenOff, layout.outOff, inputDim);
    const t1 = performance.now();
    for (let i = 0; i < REPS; i++) simdFn(layout.xOff, layout.w1Off, layout.b1Off, layout.w2Off, layout.b2Off, layout.hiddenOff, layout.outOff, inputDim);
    const simdMs = performance.now() - t1;

    console.log(`  inputDim=${inputDim}: scalar ${(scalarMs / REPS * 1000).toFixed(3)}us/call, simd ${(simdMs / REPS * 1000).toFixed(3)}us/call, ratio ${(scalarMs / simdMs).toFixed(2)}x`);
  }
  // No assertion on the ratio itself -- this test's job is to report the
  // real number every run, not to gate CI on a specific speedup that
  // small-workload microbenchmarks can't reliably guarantee run to run.
  assert.ok(true);
});
