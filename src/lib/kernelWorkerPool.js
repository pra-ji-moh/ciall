// kernelWorkerPool.js; parallel execution backend for orchestrator.js AND
// (upgrade 2) mcmcSearch.js's SMC sampler — ONE pool, two task shapes,
// never a nested second pool. Each worker recognizes the task's `type`
// (see kernelWorker.js): 'kernel-run' runs a whole kernel.run(spec) call
// (upgrade 1's original use); 'smc-slice' runs one generation's move +
// evaluate + WASM-reweight step over a slice of SMC particles (upgrade
// 2). Both share the exact same POOL_SIZE workers, dispatch queue, and
// shutdown discipline below.
//
// A fixed pool of POOL_SIZE worker threads, spawned lazily on the first
// task ever dispatched to it and kept alive for the process lifetime —
// zero spawn cost per claim after that. Each worker runs kernelWorker.js,
// which imports kernelRegistry.js on its own (kernels share zero state,
// so nothing needs to be shared across threads) and executes exactly one
// task per message.
//
// Kernel specs carry free-form strings and nested objects (math-
// expression text, commitment arrays), not numeric vectors, so tasks are
// handed to workers via postMessage's structured-clone rather than a raw
// SharedArrayBuffer: a spec this shape would need a bespoke binary
// encoder/decoder to fit a Float64Array — a worse, hand-rolled
// serialization format invented to avoid the good one Node already gives
// every worker for free, for no real benefit given how small a single
// spec is.
//
// SHUTDOWN. "Kept alive for the process lifetime" is a real design
// choice, not an oversight: a long-running process (a server, a REPL, a
// CLI doing several claims in one run) is meant to just keep using the
// same pool for as long as it runs. Each worker is unref()'d at creation
// as a best-effort nicety, but that is NOT reliable once a message has
// actually round-tripped through it — verified empirically on this
// platform (Node v24, Windows): a worker that has completed even one
// task stops honoring an earlier unref() and keeps the process alive
// indefinitely, regardless of how many more times unref() is called
// afterward. Any process that wants to exit after it's done with the
// pool (a one-shot script, a test file) MUST call shutdownPool()
// explicitly — do not rely on unref() for that.

import { Worker } from 'node:worker_threads';

export const POOL_SIZE = 4;
const WORKER_URL = new URL('./kernelWorker.js', import.meta.url);

let pool = null; // { workers: [{ worker, busy, taskId }] }
let nextTaskId = 1;
const pendingTasks = new Map(); // taskId -> { resolve, reject }
const queue = []; // tasks waiting for a free worker: { taskId, kernelId, spec }

function dispatch(slotIndex, task) {
  const slot = pool.workers[slotIndex];
  slot.busy = true;
  slot.taskId = task.taskId;
  slot.worker.postMessage(task);
}

function pump(slotIndex) {
  if (queue.length === 0) return;
  if (pool.workers[slotIndex].busy) return;
  dispatch(slotIndex, queue.shift());
}

function makeWorker(slotIndex) {
  const worker = new Worker(WORKER_URL);
  worker.unref(); // best-effort only; see the SHUTDOWN note above
  worker.on('message', (msg) => {
    const entry = pendingTasks.get(msg.taskId);
    pendingTasks.delete(msg.taskId);
    pool.workers[slotIndex].busy = false;
    pool.workers[slotIndex].taskId = null;
    if (entry) {
      if (msg.ok) entry.resolve(msg.result);
      else entry.reject(new Error(msg.error));
    }
    pump(slotIndex);
  });
  worker.on('error', (err) => {
    const slot = pool.workers[slotIndex];
    const failedTaskId = slot.taskId;
    if (failedTaskId != null) {
      const entry = pendingTasks.get(failedTaskId);
      pendingTasks.delete(failedTaskId);
      if (entry) entry.reject(err);
    }
    // A crashed worker is replaced so the pool stays at POOL_SIZE rather
    // than silently degrading.
    pool.workers[slotIndex] = { worker: makeWorker(slotIndex), busy: false, taskId: null };
    pump(slotIndex);
  });
  return worker;
}

function ensurePool() {
  if (pool) return pool;
  pool = { workers: [] };
  for (let i = 0; i < POOL_SIZE; i++) {
    pool.workers.push({ worker: makeWorker(i), busy: false, taskId: null });
  }
  return pool;
}

// Shared by both public entry points below: assigns a taskId, tracks its
// promise, and either dispatches to an idle worker now or queues it.
function submitTask(payload) {
  ensurePool();
  const taskId = nextTaskId++;
  const promise = new Promise((resolve, reject) => pendingTasks.set(taskId, { resolve, reject }));
  const task = { taskId, ...payload };
  const idleIndex = pool.workers.findIndex((s) => !s.busy);
  if (idleIndex === -1) queue.push(task);
  else dispatch(idleIndex, task);
  return promise;
}

/**
 * Runs kernel `kernelId` on already-normalized `spec` in the worker
 * pool; resolves with exactly the value `kernel.run(spec)` would return
 * if called in-process, or rejects with an Error carrying the same
 * `.message` a synchronous throw would have had.
 */
export function runKernelTask(kernelId, spec) {
  return submitTask({ type: 'kernel-run', kernelId, spec });
}

/**
 * Runs one SMC generation's slice step (move + evaluate + WASM reweight
 * over [params.start, params.end)) in the worker pool; resolves with
 * { evaluations, bestMargin, bestPoint } for that slice — see
 * kernelWorker.js for exactly what runs. `params.memory` must be a
 * WebAssembly.Memory created with shared:true; it's passed through
 * postMessage as an object reference (Node shares it, never copies it).
 */
export function runSmcSlice(params) {
  return submitTask({ type: 'smc-slice', ...params });
}

/**
 * Terminates every pooled worker. Required, not optional, for a process
 * that used the parallel path to exit promptly afterward — see the
 * SHUTDOWN note at the top of this file for why unref() alone does not
 * reliably do this. Safe to call even if the pool was never started.
 */
export async function shutdownPool() {
  if (!pool) return;
  const workers = pool.workers.map((s) => s.worker);
  pool = null;
  queue.length = 0;
  await Promise.all(workers.map((w) => w.terminate()));
}
