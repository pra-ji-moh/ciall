// eventCameraPixel.test.mjs; upgrade 10 — validates the event-camera
// pixel simulator against the real DVS mechanism's defining properties
// (log-intensity threshold crossing, burst-fires one event per full
// threshold-worth of change, no event for the reference sample), and
// the verification kernel's ability to catch a claimed event log that
// deviates from what that mechanism actually produces.

import test from 'node:test';
import assert from 'node:assert/strict';
import { simulatePixelEvents, normalizeEventCameraSpec, verifyEventLog } from '../src/lib/eventCameraPixel.js';
import { getKernel, listKernels } from '../src/lib/kernelRegistry.js';

test('registered as the 10th kernel', () => {
  const ids = listKernels().map((k) => k.id);
  assert.ok(ids.includes('event-camera-pixel'));
  const kernel = getKernel('event-camera-pixel');
  assert.equal(typeof kernel.normalize, 'function');
  assert.equal(typeof kernel.run, 'function');
});

test('no event fires for the reference (first) sample, and none fires while intensity stays within one threshold of the reference', () => {
  const events = simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 1, intensity: 100.5 }], { threshold: 1.0 }); // tiny log-change, well under threshold 1.0
  assert.deepEqual(events, []);
});

test('a single ON event fires exactly when log-intensity crosses the threshold, at the crossing sample\'s timestamp', () => {
  const threshold = 0.5;
  const jumpIntensity = 100 * Math.exp(threshold * 1.2); // just over one threshold-worth of increase
  const events = simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 5, intensity: jumpIntensity }], { threshold });
  assert.equal(events.length, 1);
  assert.equal(events[0].polarity, 'ON');
  assert.equal(events[0].t, 5);
});

test('a single OFF event fires for a decrease past threshold', () => {
  const threshold = 0.5;
  const dropIntensity = 100 * Math.exp(-threshold * 1.2);
  const events = simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 3, intensity: dropIntensity }], { threshold });
  assert.equal(events.length, 1);
  assert.equal(events[0].polarity, 'OFF');
});

test('a large jump fires a BURST of events -- one per full threshold-worth of change, not one event regardless of jump size (the real per-pixel mechanism, not a simplification of it)', () => {
  const threshold = 0.5;
  // A jump of exactly 3.5x the threshold in log-space should fire 3 ON events
  // (3 full threshold-worths crossed; the remaining 0.5x is not enough for a 4th).
  const bigJumpIntensity = 100 * Math.exp(threshold * 3.5);
  const events = simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 10, intensity: bigJumpIntensity }], { threshold });
  assert.equal(events.length, 3);
  events.forEach((e) => { assert.equal(e.polarity, 'ON'); assert.equal(e.t, 10); });
});

test('reference resets by exactly +/-threshold per event, not to the new sample value -- verified by continuing the signal and checking the NEXT event fires at the mechanistically-correct point', () => {
  const threshold = 0.5;
  // First sample jumps by 1.2x threshold (1 event, reference now at ref0+0.5).
  // Second sample needs another FULL threshold worth of drift from THAT
  // reset reference (ref0+0.5), not from the actual peak reached, to fire again.
  const ref0 = Math.log(100);
  const afterFirstJump = Math.exp(ref0 + threshold * 1.2); // 1 ON event, new internal ref = ref0+0.5
  const notEnoughYet = Math.exp(ref0 + threshold * 1.2 + threshold * 0.3); // +0.3 more -- still short of a full threshold from ref0+0.5
  const enoughNow = Math.exp(ref0 + threshold * 1.2 + threshold * 1.0); // +1.0 more -- crosses a second full threshold from ref0+0.5

  const events1 = simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 1, intensity: afterFirstJump }, { t: 2, intensity: notEnoughYet }], { threshold });
  assert.equal(events1.length, 1, 'no second event yet -- confirms the reference reset to ref+threshold, not to the peak value reached');

  const events2 = simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 1, intensity: afterFirstJump }, { t: 2, intensity: enoughNow }], { threshold });
  assert.equal(events2.length, 2, 'a second event fires once a full additional threshold-worth of drift accumulates from the RESET reference');
});

test('simulatePixelEvents rejects non-positive intensity, non-ascending t, and a non-positive threshold', () => {
  assert.throws(() => simulatePixelEvents([{ t: 0, intensity: 0 }], { threshold: 0.5 }), /positive/);
  assert.throws(() => simulatePixelEvents([{ t: 5, intensity: 1 }, { t: 1, intensity: 1 }], { threshold: 0.5 }), /ascending/);
  assert.throws(() => simulatePixelEvents([{ t: 0, intensity: 1 }, { t: 1, intensity: 1 }], { threshold: 0 }), /threshold/);
});

test('kernel: a claimed event log that EXACTLY matches the real mechanism holds', () => {
  const threshold = 0.5;
  const intensitySamples = [{ t: 0, intensity: 100 }, { t: 1, intensity: 100 * Math.exp(threshold * 1.5) }];
  const realEvents = simulatePixelEvents(intensitySamples, { threshold });
  const spec = normalizeEventCameraSpec({ kind: 'event_camera_check', intensitySamples, threshold, claimedEvents: realEvents });
  const result = verifyEventLog(spec);
  assert.equal(result.verdict, 'held');
  assert.equal(result.eventCount, realEvents.length);
});

test('kernel: a claimed event log with a wrong polarity is caught, with a specific first-mismatch diagnosis', () => {
  const threshold = 0.5;
  const intensitySamples = [{ t: 0, intensity: 100 }, { t: 1, intensity: 100 * Math.exp(threshold * 1.5) }]; // real events are ON
  const wrongClaim = [{ t: 1, polarity: 'OFF' }]; // claims OFF instead
  const spec = normalizeEventCameraSpec({ kind: 'event_camera_check', intensitySamples, threshold, claimedEvents: wrongClaim });
  const result = verifyEventLog(spec);
  assert.equal(result.verdict, 'violated');
  assert.equal(result.firstMismatch.index, 0);
  assert.match(result.firstMismatch.reason, /polarity:OFF.*polarity:ON/);
});

test('upgrade 11 bounded-execution audit: an adversarially extreme intensity jump does not burst an unbounded number of events -- it throws a specific, bounded error rather than hanging or exhausting memory', () => {
  const threshold = 1e-6; // tiny threshold
  const extremeIntensity = 100 * Math.exp(1); // an absurd ratio relative to the threshold
  assert.throws(
    () => simulatePixelEvents([{ t: 0, intensity: 100 }, { t: 1, intensity: extremeIntensity }], { threshold }),
    /MAX_BURST_PER_TRANSITION/,
  );
});

test('kernel: the same adversarial burst input is caught gracefully by verifyEventLog as "inconclusive", never an uncaught crash', () => {
  const threshold = 1e-6;
  const extremeIntensity = 100 * Math.exp(1);
  const spec = normalizeEventCameraSpec({
    kind: 'event_camera_check',
    intensitySamples: [{ t: 0, intensity: 100 }, { t: 1, intensity: extremeIntensity }],
    threshold,
    claimedEvents: [],
  });
  const result = verifyEventLog(spec);
  assert.equal(result.verdict, 'inconclusive');
  assert.match(result.reason, /bounded-execution limit/);
});

test('kernel: a claimed event log missing events from a burst is caught (undercounting a large jump)', () => {
  const threshold = 0.5;
  const intensitySamples = [{ t: 0, intensity: 100 }, { t: 1, intensity: 100 * Math.exp(threshold * 3.5) }]; // real: 3 ON events
  const underclaimed = [{ t: 1, polarity: 'ON' }]; // claims only 1
  const spec = normalizeEventCameraSpec({ kind: 'event_camera_check', intensitySamples, threshold, claimedEvents: underclaimed });
  const result = verifyEventLog(spec);
  assert.equal(result.verdict, 'violated');
  assert.equal(result.realEventCount, 3);
  assert.equal(result.claimedEventCount, 1);
});

test('kind:"none" is a clean escape hatch', () => {
  const spec = normalizeEventCameraSpec({ kind: 'none', reason: 'not a checkable pixel claim' });
  const result = verifyEventLog(spec);
  assert.equal(result.verdict, 'inconclusive');
});

test('normalize rejects malformed input loudly: too few samples, bad polarity, unknown kind', () => {
  assert.throws(() => normalizeEventCameraSpec({ kind: 'event_camera_check', intensitySamples: [{ t: 0, intensity: 1 }], threshold: 0.5, claimedEvents: [] }), /at least 2 samples/);
  assert.throws(() => normalizeEventCameraSpec({ kind: 'event_camera_check', intensitySamples: [{ t: 0, intensity: 1 }, { t: 1, intensity: 2 }], threshold: 0.5, claimedEvents: [{ t: 1, polarity: 'SIDEWAYS' }] }), /polarity must be/);
  assert.throws(() => normalizeEventCameraSpec({ kind: 'not-real' }), /Unknown event camera spec kind/);
});
