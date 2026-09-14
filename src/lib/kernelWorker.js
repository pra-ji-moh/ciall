// kernelWorker.js; one worker thread's task-execution loop for
// kernelWorkerPool.js. Node gives every worker thread its own module
// registry, so every import below is a fresh, independent instance per
// worker — consistent with "kernels share zero state," nothing needs to
// be shared across threads except the SMC WebAssembly.Memory, which is
// shared on purpose (see smcWasm.js). Two task types:
//   'kernel-run'  (upgrade 1) — runs exactly one kernel.run(spec) call.
//   'smc-slice'   (upgrade 2) — runs one generation's move + evaluate +
//                 WASM-reweight step over one slice of SMC particles.

import { parentPort } from 'node:worker_threads';
import { getKernel } from './kernelRegistry.js';
import { compileExpr } from './mathExpr.js';
import { mulberry32, makeGaussian, combineSeed } from './mcmcSearch.js';
import { instantiateSmcModule } from './smcWasm.js';

// Per-worker caches, alive for the worker's whole lifetime (not per
// task): compiling an objective expression or instantiating the WASM
// module against a given shared memory only needs to happen once, no
// matter how many generations of the same (or a later) SMC search this
// worker takes slices from.
const compiledObjectives = new Map(); // "exprSource|var,names" -> compiled fn
const weightUpdateByMemory = new WeakMap(); // WebAssembly.Memory -> weightUpdate export

function getCompiledObjective(exprSource, varNames) {
  const key = `${exprSource}|${varNames.join(',')}`;
  let fn = compiledObjectives.get(key);
  if (!fn) {
    fn = compileExpr(exprSource, varNames);
    compiledObjectives.set(key, fn);
  }
  return fn;
}

async function getWeightUpdate(memory) {
  let fn = weightUpdateByMemory.get(memory);
  if (!fn) {
    fn = await instantiateSmcModule(memory);
    weightUpdateByMemory.set(memory, fn);
  }
  return fn;
}

async function runSmcSlice({ memory, exprSource, varNames, params, D, N, start, end, samplesByteOffset, weightsByteOffset, temperature, moveScaleFactor, masterSeed, generation, isFirstGeneration }) {
  const objective = getCompiledObjective(exprSource, varNames);
  const weightUpdate = await getWeightUpdate(memory);
  // Explicit lengths (not "rest of buffer"): the two views must stay
  // scoped to exactly their own region, or a bug elsewhere in this file
  // could silently read/write across into the other array's bytes with
  // no bounds error to catch it.
  const samples = new Float64Array(memory.buffer, samplesByteOffset, N * D);
  const weights = new Float64Array(memory.buffer, weightsByteOffset, N);

  let evaluations = 0;
  let bestMargin = null;
  let bestPoint = null;

  for (let i = start; i < end; i++) {
    const rand = mulberry32(combineSeed(masterSeed, i, generation));
    const point = {};
    if (isFirstGeneration) {
      // Deterministic per-particle initial position, uniform in domain.
      for (const p of params) {
        const raw = p.domain[0] + rand() * (p.domain[1] - p.domain[0]);
        point[p.name] = p.integer ? Math.round(raw) : raw;
      }
    } else {
      // Perturb the resampled position from the previous generation with
      // a small seeded Gaussian step, clipped back into the domain. No
      // Metropolis correction here (unlike MH): SMC's importance weights
      // (via the WASM reweight step) and resampling are what keep the
      // particle population honest, not a per-step accept/reject test.
      const gauss = makeGaussian(rand);
      for (let d = 0; d < params.length; d++) {
        const p = params[d];
        const width = p.domain[1] - p.domain[0];
        const scale = Math.max(width * 0.15 * moveScaleFactor, p.integer ? 1 : 1e-12);
        let step = gauss() * scale;
        if (p.integer) { step = Math.round(step); if (step === 0) step = rand() < 0.5 ? -1 : 1; }
        let v = samples[i * D + d] + step;
        v = Math.min(p.domain[1], Math.max(p.domain[0], v));
        point[p.name] = p.integer ? Math.round(v) : v;
      }
    }

    for (let d = 0; d < params.length; d++) samples[i * D + d] = point[params[d].name];

    let margin;
    try {
      const v = objective(point);
      margin = Number.isFinite(v) ? v : -Infinity;
    } catch {
      margin = -Infinity;
    }
    weights[i] = margin;
    evaluations++;
    if (margin !== -Infinity && (bestMargin === null || margin > bestMargin)) {
      bestMargin = margin;
      bestPoint = { ...point };
    }
  }

  weightUpdate(start, end, weightsByteOffset, temperature);

  return { evaluations, bestMargin, bestPoint };
}

parentPort.on('message', async (msg) => {
  const { taskId } = msg;
  try {
    let result;
    if (msg.type === 'smc-slice') {
      result = await runSmcSlice(msg);
    } else {
      result = await getKernel(msg.kernelId).run(msg.spec);
    }
    parentPort.postMessage({ taskId, ok: true, result });
  } catch (e) {
    parentPort.postMessage({ taskId, ok: false, error: e.message });
  }
});
