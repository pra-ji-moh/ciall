// eventStream.test.mjs; upgrade 7 coverage — routing, ordering,
// back-pressure, and backward-compatibility for eventStream.js, the
// module behind bin/ciall.mjs's generic stream-motion event mode.
//
// dispatchSlow is injected as a controllable mock throughout (never the
// real kernelWorkerPool.js worker threads): these tests are about the
// ROUTING/ORDERING/BACK-PRESSURE machinery itself, which is orthogonal
// to whether a real worker thread is doing the work. bin/ciall.mjs
// wires the real runKernelTask in production; that wiring is exercised
// separately by hand (see the CLI's own header comment for usage) since
// spinning up real worker threads in every test run would make this
// suite slow and racy for no correctness benefit.

import test from 'node:test';
import assert from 'node:assert/strict';
import { createEventStreamProcessor, createRingBuffer, MOTION_TYPE } from '../src/lib/eventStream.js';
import { createStreamingTrajectoryVerifier } from '../src/domains/motion.js';

// A controllable async kernel dispatcher: every call is recorded and
// returns a promise the test resolves/rejects by hand, on its own
// schedule -- this is what lets the ordering and back-pressure tests
// force specific out-of-order completions deterministically instead of
// racing real timers.
function makeControllableDispatch() {
  const calls = [];
  function dispatchSlow(kernelId, spec) {
    let resolve, reject;
    const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
    calls.push({ kernelId, spec, resolve, reject });
    return promise;
  }
  return { dispatchSlow, calls };
}

function collectOutput() {
  const lines = [];
  return { onOutput: (line) => lines.push(JSON.parse(line)), lines };
}

// Lets a test wait for any pending .then()/.finally() microtasks (e.g.
// after manually resolving a mocked dispatchSlow promise) to actually
// run before asserting on their effects.
function flushMicrotasks() {
  return new Promise((resolve) => setImmediate(resolve));
}

test('routing: fast kernel types run synchronously through the real kernel and produce the real verdict', () => {
  const { dispatchSlow } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput });

  processor.processLine(JSON.stringify({ type: 'boundary-check', payload: { kind: 'allowlist', target: 'a', boundary: ['a', 'b'] }, ts: 10 }));
  processor.processLine(JSON.stringify({ type: 'boundary-check', payload: { kind: 'allowlist', target: 'z', boundary: ['a', 'b'] }, ts: 11 }));
  processor.processLine(JSON.stringify({ type: 'order-consistency', payload: { relations: [] }, ts: 12 }));

  assert.equal(lines.length, 3, 'sync kernels resolve immediately, no dispatch/await needed');
  assert.equal(lines[0].result.verdict, 'held');
  assert.equal(lines[1].result.verdict, 'violated');
  assert.equal(lines[1].ts, 11);
  assert.deepEqual(lines[2].result, []);
});

test('routing: slow kernel types are dispatched to dispatchSlow with the correct kernelId and a normalized spec, never run synchronously', async () => {
  const { dispatchSlow, calls } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput });

  for (const type of ['mcmc', 'dynamics', 'numeric-check', 'combinatorial']) {
    processor.processLine(JSON.stringify({ type, payload: { kind: 'none', reason: 'routing test' }, ts: 1 }));
  }

  assert.equal(lines.length, 0, "nothing resolves until dispatchSlow's promises do");
  assert.equal(calls.length, 4);
  assert.deepEqual(calls.map((c) => c.kernelId), ['mcmc', 'dynamics', 'numeric-check', 'combinatorial']);
  for (const c of calls) assert.equal(c.spec.kind, 'none', 'normalize() ran on the main thread before dispatch');

  calls.forEach((c, i) => c.resolve({ verdict: 'inconclusive', reason: 'routing test', evaluations: 0, i }));
  await flushMicrotasks();
  assert.equal(lines.length, 4);
});

test('routing: an unknown event type produces a per-event error, not a crash, and does not block later events', () => {
  const { dispatchSlow } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput });

  processor.processLine(JSON.stringify({ type: 'not-a-real-kernel', payload: {}, ts: 1 }));
  processor.processLine(JSON.stringify({ type: 'boundary-check', payload: { kind: 'allowlist', target: 'a', boundary: ['a'] }, ts: 2 }));

  assert.equal(lines.length, 2);
  assert.ok(lines[0].error.includes('unknown event type'));
  assert.equal(lines[1].result.verdict, 'held');
});

test('ordering: output lands in ARRIVAL order even when a slow event completes after a later fast event', async () => {
  const { dispatchSlow, calls } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput });

  // event 0: slow (mcmc), will resolve LAST.
  processor.processLine(JSON.stringify({ type: 'mcmc', payload: { kind: 'none', reason: 'r0' }, ts: 0 }));
  // event 1: fast, resolves INSTANTLY -- but must still print AFTER event 0.
  processor.processLine(JSON.stringify({ type: 'boundary-check', payload: { kind: 'allowlist', target: 'a', boundary: ['a'] }, ts: 1 }));
  // event 2: slow (dynamics), resolves BEFORE event 0 -- must still print AFTER event 1.
  processor.processLine(JSON.stringify({ type: 'dynamics', payload: { kind: 'none', reason: 'r2' }, ts: 2 }));

  assert.equal(lines.length, 0, "events 1 and 2 are both blocked behind event 0's still-open slot");

  // Resolve out of arrival order: event 2 first, then event 0.
  calls.find((c) => c.spec.reason === 'r2').resolve({ verdict: 'inconclusive', reason: 'r2', evaluations: 0 });
  await flushMicrotasks();
  assert.equal(lines.length, 0, 'event 2 finished but is still buffered behind event 0');

  calls.find((c) => c.spec.reason === 'r0').resolve({ verdict: 'inconclusive', reason: 'r0', evaluations: 0 });
  await flushMicrotasks();

  assert.equal(lines.length, 3);
  assert.deepEqual(lines.map((l) => l.ts), [0, 1, 2], 'output order matches ARRIVAL order, not completion order');
  assert.equal(lines[0].result.reason, 'r0');
  assert.equal(lines[2].result.reason, 'r2');
});

test('ring buffer: a real fixed-capacity FIFO, O(1) push/shift, evicts and returns the OLDEST item on overflow', () => {
  const ring = createRingBuffer(3);
  assert.equal(ring.push('a'), undefined);
  assert.equal(ring.push('b'), undefined);
  assert.equal(ring.push('c'), undefined);
  assert.ok(ring.isFull());
  assert.equal(ring.push('d'), 'a', 'evicts the oldest (a) to make room for d');
  assert.equal(ring.size, 3);
  assert.equal(ring.shift(), 'b');
  assert.equal(ring.shift(), 'c');
  assert.equal(ring.shift(), 'd');
  assert.ok(ring.isEmpty());
});

test('back-pressure: events beyond poolSize+ringCapacity drop the OLDEST queued event, log the drop, and never block newer events', async () => {
  const { dispatchSlow, calls } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const drops = [];
  const processor = createEventStreamProcessor({
    dispatchSlow, onOutput, poolSize: 2, ringCapacity: 2,
    onDrop: (total, evicted) => drops.push({ total, reason: evicted.spec.reason }),
  });

  // 2 in flight (dispatched immediately) + 2 queued in the ring = capacity exactly full.
  for (let i = 0; i < 4; i++) {
    processor.processLine(JSON.stringify({ type: 'mcmc', payload: { kind: 'none', reason: `r${i}` }, ts: i }));
  }
  assert.equal(calls.length, 2, 'only poolSize dispatches actually reach dispatchSlow so far');
  assert.equal(processor.ringSize(), 2);

  // A 5th event overflows the ring: r2 (the oldest QUEUED, not in-flight, event) must be dropped.
  processor.processLine(JSON.stringify({ type: 'mcmc', payload: { kind: 'none', reason: 'r4' }, ts: 4 }));
  assert.equal(drops.length, 1);
  assert.equal(drops[0].reason, 'r2', 'the oldest event WAITING in the ring is dropped, not the oldest overall or the newest');
  assert.equal(processor.ringSize(), 2, 'ring stays at capacity: one evicted, one admitted');

  // The dropped event's output SLOT is filled immediately (it doesn't
  // wait on anything), but it's slot index 2 -- behind r0 and r1, still
  // in flight -- so arrival-order output correctly holds it back rather
  // than flushing it early. Confirmed once r0/r1 resolve, below.
  await flushMicrotasks();
  assert.equal(lines.length, 0, "the dropped event's line is filled but still buffered behind still-pending r0/r1");

  // Resolve the 2 in-flight (r0, r1) -- this should drain the ring
  // (r3, then r4) into the now-free pool slots automatically.
  calls[0].resolve({ verdict: 'inconclusive', reason: 'r0', evaluations: 0 });
  calls[1].resolve({ verdict: 'inconclusive', reason: 'r1', evaluations: 0 });
  await flushMicrotasks();
  assert.equal(calls.length, 4, 'draining the ring dispatched r3 and r4 once slots freed up');
  assert.deepEqual(calls.slice(2).map((c) => c.spec.reason), ['r3', 'r4']);

  // r0 and r1 flushed now; r2's dropped-marker line finally flushes
  // right behind them -- downstream ordering was never stuck, just
  // correctly waiting its turn. r3/r4 are still in flight (never
  // resolved in this test), so nothing past r2 flushes yet.
  assert.equal(lines.length, 3);
  assert.deepEqual(lines.map((l) => l.ts), [0, 1, 2]);
  assert.equal(lines[2].error.includes('dropped'), true);

  // Resolving r3/r4 lets the rest flush too, confirming the ring-drained
  // dispatches are wired into the SAME ordering/output machinery.
  calls[2].resolve({ verdict: 'inconclusive', reason: 'r3', evaluations: 0 });
  calls[3].resolve({ verdict: 'inconclusive', reason: 'r4', evaluations: 0 });
  await flushMicrotasks();
  assert.equal(lines.length, 5);
  assert.deepEqual(lines.map((l) => l.ts), [0, 1, 2, 3, 4]);
});

test('backward compatibility: a legacy {t,value} line (no "type" field) is treated as {type:"motion",payload:{...}} and matches the original verifier exactly', async () => {
  const { dispatchSlow } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const modelExpr = '5*t';
  const tolerance = 0.5;
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput, motion: { modelExpr, tolerance } });

  // Independent reference verifier, called the OLD way (pre-upgrade-7),
  // with the exact same samples in the exact same order.
  const reference = createStreamingTrajectoryVerifier({ modelExpr, tolerance });
  const samples = [{ t: 1, value: 5 }, { t: 2, value: 11 }, { t: 3, value: 14.9 }];

  const expected = samples.map((s) => reference.observe(s));

  for (const s of samples) processor.processLine(JSON.stringify(s)); // no "type" field -- the legacy shape

  assert.equal(lines.length, 3);
  lines.forEach((line, i) => {
    assert.equal(line.type, MOTION_TYPE);
    assert.deepEqual(line.result, expected[i]);
  });

  const status = processor.motionStatus();
  const refStatus = reference.status();
  assert.equal(status.verdict, refStatus.verdict);
  assert.equal(status.sampleCount, refStatus.sampleCount);
});

test('backward compatibility: an explicit {"type":"motion","payload":{...}} line behaves identically to the legacy untyped line', () => {
  const { dispatchSlow } = makeControllableDispatch();
  const modelExpr = '2*t + 1';
  const tolerance = 1;

  const legacyOut = collectOutput();
  const legacy = createEventStreamProcessor({ dispatchSlow, onOutput: legacyOut.onOutput, motion: { modelExpr, tolerance } });
  legacy.processLine(JSON.stringify({ t: 4, value: 9 }));

  const explicitOut = collectOutput();
  const explicit = createEventStreamProcessor({ dispatchSlow, onOutput: explicitOut.onOutput, motion: { modelExpr, tolerance } });
  explicit.processLine(JSON.stringify({ type: 'motion', payload: { t: 4, value: 9 } }));

  assert.deepEqual(legacyOut.lines[0].result, explicitOut.lines[0].result);
});

test('a motion event arriving with no --model/--tolerance configured produces a per-event error, not a crash', () => {
  const { dispatchSlow } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput }); // no `motion` option

  processor.processLine(JSON.stringify({ t: 1, value: 2 }));
  assert.equal(lines.length, 1);
  assert.ok(lines[0].error.includes('no --model/--tolerance'));
});

test('end(): resolves only once every submitted event (including still-in-flight async ones) has flushed', async () => {
  const { dispatchSlow, calls } = makeControllableDispatch();
  const { onOutput, lines } = collectOutput();
  const processor = createEventStreamProcessor({ dispatchSlow, onOutput });

  processor.processLine(JSON.stringify({ type: 'mcmc', payload: { kind: 'none', reason: 'r0' }, ts: 0 }));
  let ended = false;
  const endPromise = processor.end().then(() => { ended = true; });

  await flushMicrotasks();
  assert.equal(ended, false, 'must not resolve while an event is still in flight');

  calls[0].resolve({ verdict: 'inconclusive', reason: 'r0', evaluations: 0 });
  await endPromise;
  assert.equal(ended, true);
  assert.equal(lines.length, 1);
});
