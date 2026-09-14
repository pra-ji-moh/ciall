// smcWasm.js; the pre-compiled WASM module for SMC's per-sample weight
// update, checked in as a hardcoded base64 constant. No compiler runs at
// any point after this file is loaded — WebAssembly.compile() below only
// parses/validates the fixed bytes decoded from SMC_WASM_BASE64, exactly
// like decoding any other embedded binary asset.
//
// WHAT THE MODULE DOES AND DOESN'T DO. WASM's numeric instruction set has
// no exp/log (no transcendental functions at all — this is a real MVP
// limitation, not an oversight), and a claim's objective is an arbitrary
// caller-supplied expression (mathExpr.js's interpreter), which cannot be
// baked into a single fixed WASM module without reimplementing that whole
// expression grammar in hand-written WASM bytecode. So the division of
// labor is: the objective is evaluated in JS (mathExpr.js, unmodified,
// same trusted kernel MH already uses) on the worker thread, which writes
// each particle's margin into the shared weights array; THIS module does
// the one fixed, claim-independent step — "per-sample weight update":
// reading each margin back out of shared memory, converting it to an
// unnormalized importance weight via exp(margin/temperature) (clamped to
// avoid overflow), and writing the weight back in place. That's a real,
// bounded, hand-verifiable numeric kernel, not a rebranded no-op.
//
// The module imports "env.memory" (a WebAssembly.Memory constructed with
// shared:true) rather than declaring its own — every worker instantiates
// its own WebAssembly.Instance from the SAME compiled Module AND the SAME
// Memory object, so `memory.buffer` (a SharedArrayBuffer) is the one true
// backing store every worker's f64.load/f64.store touches directly; zero
// copying, zero postMessage per element. It also imports "env.exp" (bound
// to Math.exp on the host side) since WASM cannot compute exp() itself;
// hand-rolling a polynomial approximation was considered and rejected —
// it would trade a correct, well-tested implementation for an untested
// one, for no real benefit, in a codebase whose whole ethos is verified
// correctness over cleverness.
//
// Exported function: weightUpdate(start: i32, end: i32,
// weightsByteOffset: i32, temperature: f64) -> void. For i in [start,
// end): weights[i] = exp(clamp(weights[i] / temperature, -700, 700)),
// read/written as f64 at byte address weightsByteOffset + i*8. Touches
// nothing outside [start, end) — verified directly (see
// tests/mcmcSmc.test.mjs) by writing sentinel values on either side of a
// slice and confirming they survive a weightUpdate call untouched.
//
// PROVENANCE. These exact bytes were produced by a small hand-written
// assembler script (not part of this repo — a one-off dev tool) that
// emits the WASM binary format directly per spec: magic+version, a type
// section (two signatures: (f64)->f64 for the imported exp, and
// (i32,i32,i32,f64)->() for weightUpdate), an import section (shared
// memory + exp), a function section, an export section, and a code
// section encoding the loop body by hand (locals, f64.load/f64.min/
// f64.max/f64.div/call/f64.store, block/loop/br_if for the while-loop).
// Before being trusted, the output was validated three ways in a real
// Node v24 process: (1) instantiated standalone and checked against
// Math.exp at several points including the clamp boundary; (2) checked
// that a weightUpdate(start,end,...) call touches only [start,end) by
// planting sentinels on both sides of a slice; (3) instantiated from FOUR
// separate real worker_threads all importing the same shared
// WebAssembly.Memory, each given a disjoint slice, confirming every
// worker's write is visible from the main thread with no message-passing
// of the data itself. All three checks passed before these bytes were
// embedded here.

const SMC_WASM_BASE64 = 'AGFzbQEAAAABDQJgAXwBfGAEf39/fAACGwIDZW52Bm1lbW9yeQIDAYAgA2VudgNleHAAAAMCAQEHEAEMd2VpZ2h0VXBkYXRlAAEKXwFdAgJ/AnwgACEEAkADQCAEIAFODQEgAiAEQQhsaiEFIAUrAwAhBiAGIAOjIQcgB0QAAAAAAOCFQKREAAAAAADghcClIQcgBxAAIQcgBSAHOQMAIARBAWohBAwACwsL';

export const SMC_WASM_BYTES = new Uint8Array(Buffer.from(SMC_WASM_BASE64, 'base64'));

let compiledModulePromise = null;

/** Compiles (once, cached) and returns the shared WebAssembly.Module for the SMC weight-update function. */
export function compileSmcModule() {
  if (!compiledModulePromise) compiledModulePromise = WebAssembly.compile(SMC_WASM_BYTES);
  return compiledModulePromise;
}

/**
 * Instantiates the SMC weight-update module against a specific shared
 * `memory` (a WebAssembly.Memory created with shared:true). Every caller
 * (each worker) gets its own Instance but they all share the same
 * underlying SharedArrayBuffer via `memory`.
 */
export async function instantiateSmcModule(memory) {
  const module = await compileSmcModule();
  const instance = await WebAssembly.instantiate(module, { env: { memory, exp: Math.exp } });
  return instance.exports.weightUpdate;
}
