// orchestrator.js; Phase 2 of VISION-PLTR.md — within-substrate autonomy.
//
// WHAT THIS IS. Runs a claim through every applicable kernel back-to-back
// and returns one consolidated report, instead of a caller manually
// invoking each kernel and reading each result in isolation. That's the
// whole scope of "autonomy" here: sequencing already-designed specs
// through their kernels, aggregating verdicts, and never letting one
// kernel's failure hide another's result.
//
// WHAT THIS DELIBERATELY IS NOT. This substrate has no model client (see
// README: "no Anthropic API client" is a stated boundary, not an
// oversight). Every kernel's `buildPrompt` step still needs a model to
// turn a claim into a structured spec; that step is the caller's
// responsibility, injected as `designSpec`, so this file stays
// model-provider-agnostic. The orchestrator's own autonomy is scoped to
// what happens AFTER a spec exists: normalize -> run -> aggregate, done
// for every applicable kernel without a human clicking through each one.
//
// PARALLEL KERNEL EXECUTION (upgrade 1). `designSpec` stays sequential —
// it's caller-owned and often a real model call, so kernel N+1's spec
// design can start while kernel N's run() is already executing in a
// worker, but two designSpec calls are never raced against each other.
// Once a kernel's spec is normalized, its run() — pure, synchronous,
// stateless per kernelRegistry.js's contract — is handed to a pre-warmed
// worker pool (kernelWorkerPool.js) instead of being called in-process,
// so independent kernels actually execute concurrently. Every dispatched
// run() is tracked by the dispatching kernel's position in `kernels`, and
// the final ran/skipped/failed arrays are always assembled by walking
// `kernels` in that original order — never completion order — so a
// parallel run is bit-for-bit identical, report-shape included, to the
// sequential one. Pass `{ parallel: false }` to skip the pool entirely
// and run every kernel's run() in-process, exactly as before this
// upgrade.
//
// A process that calls runPipeline with parallel:true (the default) and
// wants to exit afterward — a script, a test file — must import and call
// `shutdownPool()` from kernelWorkerPool.js itself when done; see that
// file's SHUTDOWN note for why. A long-running process (a server) can
// just leave the pool running for its whole lifetime, which is the
// intended steady-state use.
//
// SELF-PARALLEL KERNELS (upgrade 2). A normalized spec may set
// `selfParallel: true` (mcmcSearch.js's SMC mode does) to opt OUT of the
// outer per-kernel pool dispatch above and have its run() awaited
// directly on the calling thread instead — for a kernel whose own run()
// already drives kernelWorkerPool.js's pool internally, dispatching it
// into one of that same pool's workers first would nest a 4-way fan-out
// inside a single worker rather than running it alongside the pool's
// other three. Every kernel without this flag (every kernel except SMC
// mode, as of this upgrade) is completely unaffected.

import { listKernels, kernelsForDomain } from './kernelRegistry.js';
import { runKernelTask } from './kernelWorkerPool.js';
import { callKernelRemote } from './rpc/kernelServiceClient.js';

// DISTRIBUTED KERNEL OFFLOADING (upgrade 8). CIALL_REMOTE_KERNELS routes
// named kernels to a separate ciall-serve node instead of running them
// locally (in-process or in the local worker pool) — everything else
// about runPipeline is unaffected: designSpec is still caller-owned and
// sequential, the report shape is identical either way, and a kernel
// NOT named in the env var runs exactly as it always has.
//
// Format: "kernelName:host:port,kernelName2:host2:port2,...". Read
// FRESH on every runPipeline() call (matching sandboxConfig.js's
// getSandboxPath(), which re-reads CIALL_SANDBOX_PATH per call rather
// than caching it at module load) rather than once at import time —
// cheap (a short string split), and it means a long-running process
// picks up a changed routing table on its very next call instead of
// needing a restart, and tests never have to fight ESM's module-cache
// timing to exercise a different value.
function parseRemoteKernelsEnv(raw) {
  const map = new Map();
  if (!raw) return map;
  for (const entry of raw.split(',').map((s) => s.trim()).filter(Boolean)) {
    const parts = entry.split(':');
    if (parts.length !== 3) throw new Error(`CIALL_REMOTE_KERNELS: malformed entry "${entry}", expected kernelName:host:port`);
    const [kernelName, host, portStr] = parts;
    const port = Number(portStr);
    if (!Number.isInteger(port) || port <= 0 || port > 65535) throw new Error(`CIALL_REMOTE_KERNELS: bad port in "${entry}"`);
    map.set(kernelName, { host, port });
  }
  return map;
}

const REMOTE_RETRIES = 3;
const REMOTE_RETRY_BACKOFF_MS = 50;

function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// Retries a remote call up to REMOTE_RETRIES times with a fixed 50ms
// backoff on CONNECTION failure specifically (the spec's own literal
// wording: "3 retries with 50ms backoff on connection failure"). A
// remote call that connects fine but comes back with a kernel-level
// error (bad claim, unknown kernel — err.isRemoteKernelError, see
// kernelServiceClient.js) is NOT a connection failure: it is returned
// immediately, not retried (retrying would just get the identical
// answer three more times, 150ms slower) and — critically — is NOT
// eligible for local fallback either, since falling back would silently
// re-run the exact same claim locally and hit the exact same validation
// error, masking a real, meaningful result as if it were a network
// hiccup. Only genuine connection/transport failures exhaust their
// retries and fall through to local execution.
//
// Returns one of:
//   { ok: true, result, wallClockMs }                 — remote success
//   { ok: false, kind: 'kernel-error', error }         — reached the server, it rejected the claim: a real failure, do not fall back
//   { ok: false, kind: 'connection-failure', error }   — never reached the server after every retry: fall back to local
async function callRemoteWithRetry(host, port, kernelId, rawSpec) {
  let lastError;
  for (let attempt = 0; attempt < REMOTE_RETRIES; attempt++) {
    try {
      const { result, wallClockMs } = await callKernelRemote(host, port, kernelId, rawSpec);
      return { ok: true, result, wallClockMs };
    } catch (err) {
      if (err.isRemoteKernelError) return { ok: false, kind: 'kernel-error', error: err };
      lastError = err;
      if (attempt < REMOTE_RETRIES - 1) await sleep(REMOTE_RETRY_BACKOFF_MS);
    }
  }
  return { ok: false, kind: 'connection-failure', error: lastError };
}

/**
 * Runs `claim` through every kernel in `kernelIds` (or every kernel
 * tagged with `domain`, or every registered kernel if neither is given).
 *
 * `designSpec(kernel, claim)` is called once per kernel to obtain the
 * model-designed raw spec (the caller owns the model call; this file
 * never makes one). It may be sync or async. If it throws or returns
 * null/undefined, that kernel is skipped and recorded as skipped, not
 * silently dropped — the aggregate report always accounts for every
 * kernel it was asked to run.
 *
 * `parallel` (default true) dispatches each kernel's run() step to a
 * pre-warmed worker pool so independent kernels execute concurrently;
 * pass `{ parallel: false }` to run every kernel's run() in-process,
 * sequentially, exactly as this function behaved before upgrade 1.
 * Either way the returned report is identical for the same inputs.
 *
 * Returns:
 *   {
 *     claim,
 *     ran: [{ kernelId, verdict, result, spec }],   // completed, in order
 *     skipped: [{ kernelId, reason }],               // spec design failed or was declined
 *     failed: [{ kernelId, reason }],                 // normalize/run threw
 *     anyViolated: boolean,                            // true if ANY kernel found a violation/contradiction
 *   }
 *
 * A kernel counts as "violated" if its run() result has verdict
 * 'violated' or 'inconsistent', or (for consistency/order-consistency,
 * whose run() returns an array of findings rather than a verdict object)
 * a non-empty array.
 */
export async function runPipeline(claim, { kernelIds, domain, designSpec, parallel = true } = {}) {
  if (!claim || typeof claim !== 'object') throw new Error('runPipeline needs a claim object');
  if (typeof designSpec !== 'function') throw new Error('runPipeline needs a designSpec(kernel, claim) function; this substrate does not call a model itself');

  const remoteKernels = parseRemoteKernelsEnv(process.env.CIALL_REMOTE_KERNELS);

  const kernels = kernelIds
    ? kernelIds.map((id) => listKernels().find((k) => k.id === id)).filter(Boolean)
    : domain
      ? kernelsForDomain(domain)
      : listKernels();

  // One outcome slot per kernel, in `kernels` order. Filled in as each
  // phase completes (some synchronously in the loop below, some later
  // when a dispatched run() resolves out of order) — the final
  // ran/skipped/failed arrays are always built from this in kernel
  // order, so completion order can never leak into the report shape.
  const outcomes = new Array(kernels.length);
  const runTasks = []; // { index, spec, promise }, only used when parallel

  for (let i = 0; i < kernels.length; i++) {
    const kernel = kernels[i];
    let raw;
    try {
      raw = await designSpec(kernel, claim);
    } catch (e) {
      outcomes[i] = { type: 'skipped', reason: `spec design threw: ${e.message}` };
      continue;
    }
    if (raw === null || raw === undefined) {
      outcomes[i] = { type: 'skipped', reason: 'designSpec returned nothing (kernel declined this claim)' };
      continue;
    }

    // DISTRIBUTED KERNEL OFFLOADING (upgrade 8): a kernel named in
    // CIALL_REMOTE_KERNELS is sent the RAW (not locally normalized)
    // spec — the remote ciall-serve node normalizes it itself, exactly
    // as any direct KernelService.Run caller would (see
    // kernelServiceServer.js), so this repo's normalize/clamp logic
    // only ever runs in ONE place for a given call, never twice with a
    // chance to disagree. Success short-circuits the rest of this
    // iteration entirely (no local normalize, no worker-pool dispatch)
    // — a genuine kernel-level error (bad claim, reached the server)
    // is a real `failed` outcome, not retried or masked; only a true
    // connection failure, after exhausting every retry, falls through
    // to the untouched local path below, so a network problem can
    // never be the reason a verification call fails (the spec's own
    // literal requirement).
    if (remoteKernels.has(kernel.id)) {
      const { host, port } = remoteKernels.get(kernel.id);
      const remote = await callRemoteWithRetry(host, port, kernel.id, raw);
      if (remote.ok) {
        outcomes[i] = { type: 'ran', result: remote.result, spec: raw };
        continue;
      }
      if (remote.kind === 'kernel-error') {
        outcomes[i] = { type: 'failed', reason: remote.error.message };
        continue;
      }
      console.error(`CIALL_REMOTE_KERNELS: "${kernel.id}" at ${host}:${port} unreachable after ${REMOTE_RETRIES} attempts (${remote.error.message}); falling back to local execution`);
      // fall through to local normalize/run below, exactly as if this kernel weren't remote-routed at all
    }

    let spec;
    try {
      spec = kernel.normalize(raw);
    } catch (e) {
      outcomes[i] = { type: 'failed', reason: e.message };
      continue;
    }

    if (parallel && !spec.selfParallel) {
      // Fire-and-forget from this loop's perspective: dispatch now, keep
      // designing the next kernel's spec while this one runs in a
      // worker, collect every result together after the loop.
      runTasks.push({ index: i, spec, promise: runKernelTask(kernel.id, spec) });
    } else {
      // Either the sequential fallback, or a kernel whose own spec set
      // `selfParallel` (upgrade 2: SMC mode) because IT drives the same
      // worker pool internally and must run on the calling thread, not
      // get dispatched into one of the pool's own workers first — that
      // would nest a 4-way fan-out inside a single pool worker instead
      // of running it alongside the pool's other three. `await` is a
      // no-op for every synchronous kernel (MH included) and the only
      // way an async kernel like SMC can be awaited correctly here.
      try {
        const result = await kernel.run(spec);
        outcomes[i] = { type: 'ran', result, spec };
      } catch (e) {
        outcomes[i] = { type: 'failed', reason: e.message };
      }
    }
  }

  if (runTasks.length > 0) {
    const settled = await Promise.allSettled(runTasks.map((t) => t.promise));
    settled.forEach((s, j) => {
      const { index, spec } = runTasks[j];
      outcomes[index] = s.status === 'fulfilled'
        ? { type: 'ran', result: s.value, spec }
        : { type: 'failed', reason: s.reason.message };
    });
  }

  const ran = [];
  const skipped = [];
  const failed = [];
  kernels.forEach((kernel, i) => {
    const outcome = outcomes[i];
    if (outcome.type === 'ran') ran.push({ kernelId: kernel.id, verdict: verdictOf(outcome.result), result: outcome.result, spec: outcome.spec });
    else if (outcome.type === 'skipped') skipped.push({ kernelId: kernel.id, reason: outcome.reason });
    else failed.push({ kernelId: kernel.id, reason: outcome.reason });
  });

  return {
    claim,
    ran,
    skipped,
    failed,
    anyViolated: ran.some((r) => r.verdict === 'violated'),
  };
}

// Kernels return two different shapes: a { verdict, ... } object (mcmc,
// numeric-check, dynamics), or a plain array of findings (consistency,
// order-consistency — an empty array means nothing was found, a non-empty
// one means it was). Normalize both into the same three-way verdict so
// the aggregate report doesn't need to know which shape a given kernel
// used.
function verdictOf(result) {
  if (Array.isArray(result)) return result.length > 0 ? 'violated' : 'held';
  if (result && typeof result === 'object' && 'verdict' in result) {
    if (result.verdict === 'violated' || result.verdict === 'inconsistent') return 'violated';
    if (result.verdict === 'held' || result.verdict === 'no-contradiction-found') return 'held';
    return 'inconclusive';
  }
  return 'inconclusive';
}

/**
 * A short human-readable summary of a runPipeline() report — not a
 * replacement for reading the full report, just what a caller would show
 * first. Mirrors the honesty discipline the kernels themselves use: never
 * says "verified" or "safe", only what was found or wasn't.
 */
export function summarizePipeline(report) {
  const total = report.ran.length + report.skipped.length + report.failed.length;
  if (report.anyViolated) {
    const hit = report.ran.filter((r) => r.verdict === 'violated').map((r) => r.kernelId);
    return `${hit.length} of ${total} kernels found a violation: ${hit.join(', ')}.`;
  }
  const heldCount = report.ran.filter((r) => r.verdict === 'held').length;
  return `No violation found across ${heldCount} completed kernel(s) (${report.skipped.length} skipped, ${report.failed.length} failed). Absence of a found violation is not proof none exists.`;
}
