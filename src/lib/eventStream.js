// eventStream.js; upgrade 7 — generic typed event-stream routing,
// back-pressure, and in-order output sequencing behind bin/ciall.mjs's
// stream-motion command. Pure logic, no stdin/stdout/worker-thread
// dependency baked in: bin/ciall.mjs wires this to real stdin and the
// real kernelWorkerPool.js; tests wire it to a controllable mock
// dispatcher instead, so ordering and back-pressure can be verified
// deterministically without spinning up real worker threads.
//
// ROUTING. Every kernelRegistry.js kernel is classified 'sync' (runs on
// the calling thread, target latency <1ms: consistency,
// order-consistency, boundary-check, domain-of-validity, neuromorphic-
// power — a small forward-chaining fixpoint, a small graph check, a
// string/array comparison, a literal no-op, and a fixed arithmetic
// comparison against constants, respectively) or 'async' (dispatched
// to the worker pool: mcmc, dynamics, numeric-check, combinatorial —
// every kernel that declares its OWN internal wall-clock time budget in
// its own source, meaning it can legitimately take real time and must
// never run on the thread reading stdin).
//
// DISCLOSURE (ground rule 4/5): the task's own fast/slow examples name
// four fast kernels (dimensionalAnalysis, boundaryKernel,
// orderConsistency, measurementTension) and two slow ones (mcmcSearch,
// dynamicsCheck). Two of those four fast names — dimensionalAnalysis
// and measurementTension — are not standalone kernelRegistry entries;
// they are sub-checks inside consistencyKernel.js's own
// findContradictions/summarizeConsistency (see registry.test.mjs's
// "dimensional analysis"/"measurement tension" tests, which exercise
// them through the 'consistency' kernel id, not a separate one).
// Routing by the ACTUAL registered kernel ids, 'consistency' covers
// both. That leaves two registered kernels the task's list never
// mentions — numeric-check and combinatorial — both of which declare
// their own TIME_BUDGET_MS in their own source, exactly like mcmc and
// dynamics. Classified 'async' rather than assumed fast: a kernel this
// codebase itself budgets real wall-clock time for is, by its own
// author's admission, not a sub-millisecond check, and guessing "fast"
// wrong would violate "main thread never blocks" in exactly the case
// that requirement exists to prevent.
//
// MOTION, SPECIALLY. 'motion' is not a kernelRegistry entry at all —
// it's the pre-existing, STATEFUL createStreamingTrajectoryVerifier
// (src/domains/motion.js), which accumulates a running verdict across
// every sample it has ever seen, unlike every registry kernel's
// stateless normalize(payload)->run(spec). It is handled as a third,
// hardcoded routing case rather than forced into the registry's
// stateless shape.

import { listKernels, getKernel } from './kernelRegistry.js';
import { createStreamingTrajectoryVerifier } from '../domains/motion.js';

export const MOTION_TYPE = 'motion';
export const DEFAULT_RING_CAPACITY = 1024;
export const DEFAULT_POOL_SIZE = 4; // matches kernelWorkerPool.js's own POOL_SIZE; callers should pass the real one

const FAST_KERNEL_IDS = new Set(['consistency', 'order-consistency', 'boundary-check', 'domain-of-validity', 'neuromorphic-power', 'event-camera-pixel']);

/** Map<kernelName, {mode:'sync'|'async', kernelId}> — built fresh each call so it always reflects the live registry. */
export function buildKernelRouter() {
  const router = new Map();
  for (const kernel of listKernels()) {
    router.set(kernel.id, { mode: FAST_KERNEL_IDS.has(kernel.id) ? 'sync' : 'async', kernelId: kernel.id });
  }
  return router;
}

// A real (not JS-array-push/shift) circular buffer: fixed pre-allocated
// backing array, O(1) push/shift via head/tail indices, never
// reallocates or shifts existing elements. push() evicts and returns
// the OLDEST slot first when full — "drops oldest on overflow" is a
// property of this data structure, not a special case layered on top.
export function createRingBuffer(capacity) {
  if (!Number.isInteger(capacity) || capacity < 1) throw new Error('ring buffer capacity must be a positive integer');
  const slots = new Array(capacity);
  let head = 0, tail = 0, count = 0;
  return {
    capacity,
    get size() { return count; },
    isEmpty() { return count === 0; },
    isFull() { return count === capacity; },
    // Enqueues `item` at the tail. If already full, evicts and returns
    // the current head (oldest) item first to make room; otherwise
    // returns undefined.
    push(item) {
      let evicted;
      if (count === capacity) {
        evicted = slots[head];
        slots[head] = undefined;
        head = (head + 1) % capacity;
        count--;
      }
      slots[tail] = item;
      tail = (tail + 1) % capacity;
      count++;
      return evicted;
    },
    shift() {
      if (count === 0) return undefined;
      const item = slots[head];
      slots[head] = undefined;
      head = (head + 1) % capacity;
      count--;
      return item;
    },
  };
}

function safeParseLine(line) {
  try {
    return { ok: true, value: JSON.parse(line) };
  } catch (e) {
    return { ok: false, error: e.message };
  }
}

// Existing stream-motion schema ({"t":...,"value":...}, no "type" field
// at all) is treated as {type:'motion', payload:{...}} — the literal
// upgrade 7 backward-compatibility requirement. Anything with an
// explicit "type" string is the new generic shape, used as-is.
function toTypedEvent(parsed) {
  if (parsed && typeof parsed === 'object' && !Array.isArray(parsed) && typeof parsed.type === 'string') {
    return { type: parsed.type, payload: parsed.payload, ts: Number.isFinite(parsed.ts) ? parsed.ts : undefined };
  }
  return { type: MOTION_TYPE, payload: parsed, ts: undefined };
}

/**
 * Creates a stateful event-stream processor.
 *
 * Options:
 *  - dispatchSlow(kernelId, spec): Promise<result> — REQUIRED. Runs an
 *    'async' kernel; production callers pass kernelWorkerPool.js's
 *    runKernelTask. Injectable so tests can control timing without real
 *    worker threads.
 *  - poolSize: how many 'async' dispatches may be in flight at once
 *    before new ones queue in the ring buffer (default 4, matching
 *    kernelWorkerPool.js's POOL_SIZE — pass the real constant in
 *    production).
 *  - ringCapacity: back-pressure ring buffer size (default 1024).
 *  - onOutput(line): called with each JSON-stringified NDJSON output
 *    line, in ARRIVAL order (see ORDERING below).
 *  - onDrop(totalDropped, droppedEvent): called once per event the ring
 *    buffer evicted to make room, with the running total.
 *  - motion: { modelExpr, tolerance, params? } | undefined — if given,
 *    an eager createStreamingTrajectoryVerifier is built up front,
 *    exactly matching pre-upgrade-7 behavior when --model/--tolerance
 *    were supplied. If omitted, a 'motion' event arriving later
 *    produces a per-event error result instead of crashing the stream
 *    — --model/--tolerance are no longer unconditionally required, since
 *    a stream that never contains motion events has no need of them;
 *    see bin/ciall.mjs's own note on this relaxation.
 *
 * ORDERING. Output must land in arrival order even though 'async'
 * events resolve out of order and possibly long after a 'sync' event
 * that arrived right behind them. Each event claims an output SLOT the
 * instant it arrives (before any dispatch happens); a slot fills the
 * moment its own result is ready (immediately for 'sync', whenever the
 * worker/mock resolves for 'async'). onOutput fires for a slot only
 * once every EARLIER slot has also filled — so a fast event's line can
 * sit buffered behind a still-pending slow event ahead of it, but
 * reading more input is NEVER blocked by this: a slot fills in the
 * background (a resolved promise callback), and flush() is re-checked
 * every time any slot completes, from wherever that completion happens.
 */
export function createEventStreamProcessor(opts = {}) {
  const {
    dispatchSlow,
    poolSize = DEFAULT_POOL_SIZE,
    ringCapacity = DEFAULT_RING_CAPACITY,
    onOutput = () => {},
    onDrop = () => {},
    motion,
  } = opts;
  if (typeof dispatchSlow !== 'function') throw new Error('createEventStreamProcessor needs a dispatchSlow(kernelId, spec) function');

  const router = buildKernelRouter();
  const ring = createRingBuffer(ringCapacity);
  let inFlight = 0;
  let droppedTotal = 0;

  const motionVerifier = motion ? createStreamingTrajectoryVerifier(motion) : null;
  let motionSampleCount = 0;

  // ── output ordering ────────────────────────────────────────────────
  let seq = 0;
  const slots = new Map(); // seq -> { done, line }
  let nextFlush = 0;

  function claimSlot() {
    const mySeq = seq++;
    slots.set(mySeq, { done: false, line: null });
    return mySeq;
  }
  function fillSlot(mySeq, line) {
    const slot = slots.get(mySeq);
    slot.done = true;
    slot.line = line;
    flush();
  }
  function flush() {
    while (slots.has(nextFlush) && slots.get(nextFlush).done) {
      onOutput(slots.get(nextFlush).line);
      slots.delete(nextFlush);
      nextFlush++;
    }
    settleDrainWaiters();
  }

  // ── async dispatch + back-pressure ─────────────────────────────────
  function drainRing() {
    while (inFlight < poolSize && !ring.isEmpty()) runSlow(ring.shift());
  }

  function runSlow(task) {
    inFlight++;
    Promise.resolve(dispatchSlow(task.kernelId, task.spec))
      .then((result) => fillSlot(task.seq, JSON.stringify({ type: task.type, ts: task.ts, result })))
      .catch((err) => fillSlot(task.seq, JSON.stringify({ type: task.type, ts: task.ts, error: err.message })))
      .finally(() => { inFlight--; drainRing(); });
  }

  function emitError(mySeq, type, ts, message) {
    fillSlot(mySeq, JSON.stringify({ type, ts, error: message }));
  }

  function handleMotion(mySeq, type, ts, payload) {
    if (!motionVerifier) {
      emitError(mySeq, type, ts, 'motion event received but no --model/--tolerance were provided at startup');
      return;
    }
    try {
      const result = motionVerifier.observe(payload);
      motionSampleCount++;
      fillSlot(mySeq, JSON.stringify({ type, ts, result }));
    } catch (e) {
      emitError(mySeq, type, ts, e.message);
    }
  }

  function processEvent(rawEvent) {
    const mySeq = claimSlot();
    const ts = rawEvent.ts ?? Date.now();
    const { type, payload } = rawEvent;

    if (type === MOTION_TYPE) { handleMotion(mySeq, type, ts, payload); return; }

    const entry = router.get(type);
    if (!entry) { emitError(mySeq, type, ts, `unknown event type "${type}"`); return; }

    const kernel = getKernel(entry.kernelId);
    let spec;
    try {
      spec = kernel.normalize(payload);
    } catch (e) {
      emitError(mySeq, type, ts, `normalize failed: ${e.message}`);
      return;
    }

    if (entry.mode === 'sync') {
      try {
        const result = kernel.run(spec);
        fillSlot(mySeq, JSON.stringify({ type, ts, result }));
      } catch (e) {
        emitError(mySeq, type, ts, e.message);
      }
      return;
    }

    const task = { seq: mySeq, type, ts, kernelId: entry.kernelId, spec };
    if (inFlight < poolSize) {
      runSlow(task);
    } else {
      const evicted = ring.push(task);
      if (evicted) {
        droppedTotal++;
        onDrop(droppedTotal, evicted);
        emitError(evicted.seq, evicted.type, evicted.ts, 'dropped: back-pressure ring buffer overflow');
      }
    }
  }

  // ── the public per-line entry point ────────────────────────────────
  function processLine(line) {
    const parsed = safeParseLine(line);
    if (!parsed.ok) {
      onOutput(JSON.stringify({ error: `unparseable line: ${parsed.error}` }));
      return;
    }
    processEvent(toTypedEvent(parsed.value));
  }

  // ── drain (for a caller that needs to know "every submitted event has
  // now been flushed to onOutput", e.g. before tearing down the worker
  // pool a still-in-flight 'async' dispatch depends on) ───────────────
  let ended = false;
  let drainWaiters = [];
  function isDrained() { return ended && slots.size === 0; }
  function settleDrainWaiters() {
    if (!isDrained() || drainWaiters.length === 0) return;
    const waiters = drainWaiters;
    drainWaiters = [];
    for (const resolve of waiters) resolve();
  }

  return {
    processLine,
    // Call once no more processLine() calls will happen. Returns a
    // Promise that resolves once every event submitted so far has
    // flushed to onOutput — including any still-in-flight 'async'
    // dispatches and anything still sitting in the back-pressure ring
    // buffer. Safe to call from a `stdin.on('end', ...)` handler.
    end() {
      ended = true;
      if (isDrained()) return Promise.resolve();
      return new Promise((resolve) => drainWaiters.push(resolve));
    },
    droppedTotal: () => droppedTotal,
    pendingCount: () => slots.size,
    ringSize: () => ring.size,
    motionStatus: () => (motionVerifier && motionSampleCount > 0 ? motionVerifier.status() : null),
  };
}
