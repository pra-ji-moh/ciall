// failSafeWrapper.js; upgrade 14 — a real, externally-enforced hard
// deadline around a kernel call, one concrete answer to "no fail-safe
// architecture at all" (named when asked what would go wrong equipping
// this into an aircraft).
//
// WHY THIS IS DIFFERENT FROM EVERY KERNEL'S EXISTING TIME_BUDGET_MS.
// Every budget-checking kernel in this repo (mcmcSearch.js,
// satKernel.js, numericCheck.js) checks its OWN clock, on its OWN
// thread, and gives up cooperatively when the budget is spent. That
// depends on the kernel's own code actually reaching that check — an
// infinite loop bug, or a GC pause long enough to starve the check
// itself, blows through it with nothing to catch it. This file enforces
// a deadline from a SEPARATE thread that does not depend on the kernel
// under test cooperating at all: if the dedicated worker running the
// kernel hasn't replied by the deadline, it is forcibly
// `worker.terminate()`d and a defined FAIL_SAFE_VERDICT is returned —
// the same "structurally separate check" principle every kernel in
// this repo already applies to the CLAIM it verifies, applied here to
// the KERNEL'S OWN EXECUTION.
//
// WHAT THIS IS NOT, disclosed plainly (same discipline as
// physicalActionGate.js/CERTIFICATION-GAPS.md): this does NOT make
// this codebase real-time-safe. Worker spawn and terminate() latency
// are OS-scheduler-dependent and NOT proven bounded anywhere in this
// file — a sufficiently loaded machine could make even "terminate on
// deadline" itself late. It is not a certified fail-safe architecture,
// has no redundant/voting channel, and has never run on real avionics
// hardware. It is one real, working, tested building block toward the
// SHAPE such an architecture would need, honestly scoped exactly that
// far and no further.

import { Worker } from 'node:worker_threads';

const WORKER_URL = new URL('./kernelWorker.js', import.meta.url); // reuses the EXISTING worker entry point kernelWorkerPool.js already relies on -- no new worker script, no new "how does a kernel run in a thread" logic to get wrong a second time.

export const FAIL_SAFE_REASON = Object.freeze({
  DEADLINE_EXCEEDED: 'deadline-exceeded',
  KERNEL_ERROR: 'kernel-error',
  WORKER_CRASHED: 'worker-crashed',
});

/**
 * Runs `kernelId.run(spec)` (spec must already be normalized, same
 * contract as kernelWorkerPool.js's runKernelTask) inside a FRESH,
 * DEDICATED worker thread (never the shared pool — forcibly terminating
 * a pooled worker mid-task would corrupt that pool's slot bookkeeping
 * for whatever ELSE might be queued on it; a dedicated one-shot worker
 * has no such shared state to protect, at the cost of real spawn
 * latency this file makes no claim about bounding).
 *
 * Resolves — NEVER rejects, NEVER hangs past `deadlineMs` — with either
 * `{verdict: 'completed', result}` or `{verdict: 'fail-safe', reason,
 * ...}`. A caller integrating this into a control loop can treat
 * `verdict !== 'completed'` as the single condition to fall back to a
 * defined safe state, without needing to separately handle a timeout,
 * a thrown error, and a crashed worker as three different code paths.
 */
export function runWithHardDeadline(kernelId, spec, { deadlineMs = 1000 } = {}) {
  if (typeof kernelId !== 'string' || !kernelId) throw new Error('runWithHardDeadline needs a non-empty string kernelId');
  if (!Number.isFinite(deadlineMs) || deadlineMs <= 0) throw new Error('runWithHardDeadline needs a positive finite deadlineMs');

  return new Promise((resolve) => {
    let settled = false;
    const worker = new Worker(WORKER_URL);

    const settle = (value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      worker.terminate().catch(() => {}); // best-effort cleanup; the verdict is already decided either way
      resolve(value);
    };

    const timer = setTimeout(() => {
      settle({ verdict: 'fail-safe', reason: FAIL_SAFE_REASON.DEADLINE_EXCEEDED, deadlineMs });
    }, deadlineMs);

    worker.once('message', (msg) => {
      if (msg.ok) settle({ verdict: 'completed', result: msg.result });
      else settle({ verdict: 'fail-safe', reason: FAIL_SAFE_REASON.KERNEL_ERROR, error: msg.error });
    });

    worker.once('error', (err) => {
      settle({ verdict: 'fail-safe', reason: FAIL_SAFE_REASON.WORKER_CRASHED, error: err.message });
    });

    worker.postMessage({ taskId: 1, type: 'kernel-run', kernelId, spec });
  });
}
