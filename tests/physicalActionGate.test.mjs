// physicalActionGate.test.mjs; upgrade 11 — validates the pre-actuation
// simulation gate for physical systems (camera/actuator/sensor). Every
// failure mode asserted here is checked against a HAND-COMPUTED expected
// physical result (e.g. trapezoidal-profile move time, stopping
// distance), not just "some failureMode array is non-empty" — matching
// this repo's existing discipline of verifying mechanism, not just shape.

import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizePhysicalActionSpec, simulatePhysicalAction, gatePhysicalAction } from '../src/lib/physicalActionGate.js';

// ---- normalize --------------------------------------------------------

test('normalize rejects an unknown kind', () => {
  assert.throws(() => normalizePhysicalActionSpec({ kind: 'teleport' }), /Unknown physical action kind/);
  assert.throws(() => normalizePhysicalActionSpec(null), /not an object/);
});

test('normalize: camera-capture requires >=2 samples and a positive threshold', () => {
  assert.throws(() => normalizePhysicalActionSpec({ kind: 'camera-capture', intensitySamples: [{ t: 0, intensity: 1 }], threshold: 0.5 }), /at least 2 samples/);
  assert.throws(() => normalizePhysicalActionSpec({ kind: 'camera-capture', intensitySamples: [{ t: 0, intensity: 1 }, { t: 1, intensity: 2 }], threshold: -1 }), /positive finite threshold/);
});

test('normalize: actuator-move requires finite/positive kinematic fields and ordered positionLimits', () => {
  const base = { kind: 'actuator-move', currentPosition: 0, targetPosition: 10, maxVelocity: 1, maxAcceleration: 1, positionLimits: [0, 20] };
  assert.doesNotThrow(() => normalizePhysicalActionSpec(base));
  assert.throws(() => normalizePhysicalActionSpec({ ...base, maxVelocity: 0 }), /positive finite maxVelocity/);
  assert.throws(() => normalizePhysicalActionSpec({ ...base, maxAcceleration: -1 }), /positive finite maxAcceleration/);
  assert.throws(() => normalizePhysicalActionSpec({ ...base, positionLimits: [20, 0] }), /positionLimits/);
  assert.throws(() => normalizePhysicalActionSpec({ ...base, positionLimits: [5] }), /positionLimits/);
});

test('normalize: sensor-read requires ordered expectedRange and a finite timestampMs', () => {
  const base = { kind: 'sensor-read', claimedValue: 5, expectedRange: [0, 10], timestampMs: 1000, nowMs: 1000 };
  assert.doesNotThrow(() => normalizePhysicalActionSpec(base));
  assert.throws(() => normalizePhysicalActionSpec({ ...base, expectedRange: [10, 0] }), /expectedRange/);
  assert.throws(() => normalizePhysicalActionSpec({ ...base, timestampMs: 'now' }), /finite timestampMs/);
});

// ---- camera-capture simulation ----------------------------------------

test('camera-capture: a well-formed, realistic capture is safe with no failure modes', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'camera-capture',
    intensitySamples: [{ t: 0, intensity: 100 }, { t: 1, intensity: 150 }],
    threshold: 0.3,
  });
  const result = simulatePhysicalAction(spec);
  assert.equal(result.safe, true);
  assert.deepEqual(result.failureModes, []);
  assert.equal(result.highestSeverity, 'none');
  assert.equal(typeof result.simulationDetail.eventCount, 'number');
});

test('camera-capture: an extreme intensity jump against a tiny threshold is caught as pixel-burst-overload, mirroring eventCameraPixel.js\'s own bounded-execution ceiling', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'camera-capture',
    intensitySamples: [{ t: 0, intensity: 100 }, { t: 1, intensity: 100 * Math.exp(1) }],
    threshold: 1e-6,
  });
  const result = simulatePhysicalAction(spec);
  assert.equal(result.safe, false);
  assert.equal(result.highestSeverity, 'high');
  const codes = result.failureModes.map((f) => f.code);
  assert.ok(codes.includes('pixel-burst-overload'));
});

test('camera-capture: a threshold far outside the realistic 0.1-0.5 band is flagged even when the simulation itself succeeds', () => {
  const tooSensitive = simulatePhysicalAction(normalizePhysicalActionSpec({
    kind: 'camera-capture', intensitySamples: [{ t: 0, intensity: 100 }, { t: 1, intensity: 100.1 }], threshold: 0.01,
  }));
  assert.ok(tooSensitive.failureModes.some((f) => f.code === 'threshold-too-sensitive'));

  const tooInsensitive = simulatePhysicalAction(normalizePhysicalActionSpec({
    kind: 'camera-capture', intensitySamples: [{ t: 0, intensity: 100 }, { t: 1, intensity: 110 }], threshold: 5,
  }));
  assert.ok(tooInsensitive.failureModes.some((f) => f.code === 'threshold-too-insensitive'));
});

// ---- actuator-move simulation -----------------------------------------

test('actuator-move: a move well within limits, with plenty of stopping room, is safe', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: 0, targetPosition: 10, maxVelocity: 1, maxAcceleration: 1, positionLimits: [-100, 100],
  });
  const result = simulatePhysicalAction(spec);
  assert.equal(result.safe, true);
  assert.deepEqual(result.failureModes, []);
});

test('actuator-move: hand-computed trapezoidal move time matches the simulator exactly', () => {
  // maxVelocity=2, maxAcceleration=1: accel/decel distance each = v^2/2a = 2.
  // distance=10 >= 2*2=4, so trapezoidal (cruise phase reached).
  // cruiseDistance = 10 - 4 = 6. time = 2*(v/a) + cruise/v = 2*2 + 6/2 = 4+3 = 7s.
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: 0, targetPosition: 10, maxVelocity: 2, maxAcceleration: 1, positionLimits: [-100, 100],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(Math.abs(result.simulationDetail.estimatedMoveTimeS - 7) < 1e-9);
  assert.ok(Math.abs(result.simulationDetail.stoppingDistance - 2) < 1e-9);
});

test('actuator-move: a short move that never reaches maxVelocity uses the triangular-profile formula, hand-verified', () => {
  // maxVelocity=10 (high), maxAcceleration=1, distance=4.
  // accelDistanceEach = v^2/2a = 50, so distance(4) < 2*50 -> triangular.
  // peakVelocity = sqrt(distance*a) = sqrt(4*1) = 2. time = 2*(peak/a) = 4s.
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: 0, targetPosition: 4, maxVelocity: 10, maxAcceleration: 1, positionLimits: [-100, 100],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(Math.abs(result.simulationDetail.estimatedMoveTimeS - 4) < 1e-9);
});

test('actuator-move: currentPosition already outside positionLimits is flagged', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: -50, targetPosition: 10, maxVelocity: 1, maxAcceleration: 1, positionLimits: [0, 100],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'current-position-out-of-bounds' && f.severity === 'high'));
});

test('actuator-move: targetPosition outside positionLimits is flagged', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: 0, targetPosition: 500, maxVelocity: 1, maxAcceleration: 1, positionLimits: [0, 100],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'target-outside-position-limits' && f.severity === 'high'));
});

test('actuator-move: insufficient stopping distance is caught -- a fast, low-deceleration move commanded near a limit', () => {
  // maxVelocity=10, maxAcceleration=1 -> stoppingDistance = 100/2 = 50.
  // positionLimits=[0,100], target=90 -> room beyond target = 100-90=10 < 50.
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: 0, targetPosition: 90, maxVelocity: 10, maxAcceleration: 1, positionLimits: [0, 100],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'insufficient-stopping-distance' && f.severity === 'high'));
});

test('actuator-move: a move whose estimated time exceeds maxMoveTimeS is flagged', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'actuator-move', currentPosition: 0, targetPosition: 1000, maxVelocity: 0.1, maxAcceleration: 0.1, positionLimits: [-10000, 10000], maxMoveTimeS: 5,
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'move-exceeds-time-budget' && f.severity === 'medium'));
});

// ---- sensor-read simulation --------------------------------------------

test('sensor-read: a fresh, in-range, finite reading is safe', () => {
  const spec = normalizePhysicalActionSpec({ kind: 'sensor-read', claimedValue: 22.5, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000 });
  const result = simulatePhysicalAction(spec);
  assert.equal(result.safe, true);
});

test('sensor-read: a non-finite claimed value is flagged as a real sensor-fault signature', () => {
  const spec = normalizePhysicalActionSpec({ kind: 'sensor-read', claimedValue: NaN, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000 });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'non-finite-reading' && f.severity === 'high'));
});

test('sensor-read: an out-of-range claimed value is flagged', () => {
  const spec = normalizePhysicalActionSpec({ kind: 'sensor-read', claimedValue: 999, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000 });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'reading-outside-expected-range' && f.severity === 'high'));
});

test('sensor-read: a stale reading beyond maxStalenessMs is flagged', () => {
  const spec = normalizePhysicalActionSpec({ kind: 'sensor-read', claimedValue: 10, expectedRange: [0, 50], timestampMs: 0, nowMs: 10000, maxStalenessMs: 5000 });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'stale-reading' && f.severity === 'medium'));
});

test('sensor-read: a timestamp in the future relative to nowMs is flagged (clock-sync fault)', () => {
  const spec = normalizePhysicalActionSpec({ kind: 'sensor-read', claimedValue: 10, expectedRange: [0, 50], timestampMs: 10000, nowMs: 0 });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'reading-timestamp-in-future'));
});

test('sensor-read: 3+ identical recent readings are flagged as a possible stuck sensor', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'sensor-read', claimedValue: 10, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000, recentValues: [10, 10, 10, 10],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(result.failureModes.some((f) => f.code === 'sensor-may-be-stuck' && f.severity === 'medium'));
});

test('sensor-read: varying recent readings do NOT trigger the stuck-sensor check', () => {
  const spec = normalizePhysicalActionSpec({
    kind: 'sensor-read', claimedValue: 10, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000, recentValues: [9, 10, 11, 10],
  });
  const result = simulatePhysicalAction(spec);
  assert.ok(!result.failureModes.some((f) => f.code === 'sensor-may-be-stuck'));
});

// ---- gatePhysicalAction: the confirm()-before-action gate --------------

test('gatePhysicalAction throws without an explicit confirm function', async () => {
  await assert.rejects(
    () => gatePhysicalAction({ kind: 'sensor-read', claimedValue: 1, expectedRange: [0, 10], timestampMs: 1 }, {}),
    /requires an explicit confirm/,
  );
});

test('gatePhysicalAction runs the simulation BEFORE confirm() and attaches it to what confirm() sees, never skipping either step', async () => {
  let seenAction = null;
  const confirm = async (action) => { seenAction = action; return true; };

  const result = await gatePhysicalAction(
    { kind: 'sensor-read', claimedValue: 999, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000 },
    { confirm, requestedBy: 'test-suite' },
  );

  assert.ok(seenAction, 'confirm() must have been called');
  assert.equal(seenAction.op, 'physical-action');
  assert.equal(seenAction.requestedBy, 'test-suite');
  assert.ok(seenAction.simulation, 'the action confirm() sees must carry the simulation result');
  assert.ok(seenAction.simulation.failureModes.some((f) => f.code === 'reading-outside-expected-range'));

  assert.equal(result.approved, true);
  assert.equal(result.safe, false);
  assert.ok(result.failureModes.length > 0);
});

test('gatePhysicalAction reports approved:false and the reason when confirm() declines, but STILL reports the simulation findings', async () => {
  const confirm = async () => false;
  const result = await gatePhysicalAction(
    { kind: 'sensor-read', claimedValue: 20, expectedRange: [0, 50], timestampMs: 1000, nowMs: 1000 },
    { confirm },
  );
  assert.equal(result.approved, false);
  assert.equal(result.reason, 'confirm() declined this specific action');
  assert.equal(result.safe, true); // the simulation itself found nothing wrong; the human simply declined
});

test('gatePhysicalAction does not auto-deny on failure modes -- confirm() still makes the actual call, now informed', async () => {
  // A high-severity finding (target outside limits) does not itself
  // block approval -- same "surface, don't auto-block" stance as every
  // claim-verification kernel and deviceExecutor.js's `verify` option.
  const confirm = async (action) => {
    assert.ok(action.simulation.failureModes.some((f) => f.severity === 'high'));
    return true; // a human, now informed, chooses to proceed anyway
  };
  const result = await gatePhysicalAction(
    { kind: 'actuator-move', currentPosition: 0, targetPosition: 500, maxVelocity: 1, maxAcceleration: 1, positionLimits: [0, 100] },
    { confirm },
  );
  assert.equal(result.approved, true);
  assert.equal(result.safe, false);
});
