// physicalActionGate.js; upgrade 11 — the pre-actuation simulation gate
// for physical systems (sensors, cameras, actuators), built to the same
// discipline deviceGate.js/deviceExecutor.js already established for
// filesystem actions.
//
// READ THIS BEFORE WIRING ANYTHING TO THIS FILE (same header discipline
// as deviceGate.js). This module has NO EXECUTION BACKEND. There is no
// camera driver, no GPIO, no serial port, no motor controller anywhere
// in this file or this repo. It cannot turn a camera on, move a motor,
// or poll a real sensor. What it does: given a PROPOSED physical action
// (already-structured parameters — this needs no model extraction,
// exactly like boundaryKernel.js), it runs a real, deterministic
// SIMULATION of that action against the physical limits the caller
// declared, enumerates every concrete failure mode the simulation finds
// — not a vague risk score, specific named modes with detail — and
// requires the same fresh, per-call confirm() that deviceExecutor.js
// requires for a filesystem write, with the failure-mode list attached
// to exactly what confirm() sees, before anything downstream could act.
// A future physical executor plugs in BELOW an approved decision here,
// exactly the same relationship deviceExecutor.js has to deviceGate.js
// today. Building that executor is out of scope for this file, same as
// deviceGate.js's own explicit "Phase 4 needs its own go-ahead" stance.
//
// WHY SIMULATION, NOT A HEURISTIC SCORE. "List all failures" (the
// actual request this was built against) only means something if the
// failure modes are mechanistically real, not a generic risk label. So:
//   - camera-capture reuses eventCameraPixel.js's REAL DVS pixel
//     simulator (simulatePixelEvents), the same one the event-camera
//     verification kernel uses — not a second, separate approximation.
//   - actuator-move runs a real closed-form trapezoidal motion profile
//     (accelerate at maxAcceleration to maxVelocity, cruise, decelerate)
//     and a real stopping-distance check against the declared travel
//     limits — the actual physics of "can this axis stop before it hits
//     the end of its travel if commanded at full speed," not a guess.
//   - sensor-read runs real range/staleness/finiteness checks plus a
//     stuck-sensor detector (zero variance across repeated readings is
//     a genuine, well-known real-sensor fault signature, not invented
//     for this file).

import { simulatePixelEvents } from './eventCameraPixel.js';

const MAX_DETAIL_LEN = 500;

function clampDetail(s) {
  return String(s).slice(0, MAX_DETAIL_LEN);
}

function failureMode(code, severity, detail) {
  return { code, severity, detail: clampDetail(detail) };
}

// --- normalize -------------------------------------------------------

export function normalizePhysicalActionSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Physical action spec is not an object');
  const kind = raw.kind;

  if (kind === 'camera-capture') {
    const MAX_SAMPLES = 5000;
    const samplesRaw = Array.isArray(raw.intensitySamples) ? raw.intensitySamples.slice(0, MAX_SAMPLES) : [];
    if (samplesRaw.length < 2) throw new Error('camera-capture needs intensitySamples: at least 2 samples (a reference plus at least one more)');
    const intensitySamples = samplesRaw.map((s) => ({ t: Number(s.t), intensity: Number(s.intensity) }));
    const threshold = Number(raw.threshold);
    if (!Number.isFinite(threshold) || threshold <= 0) throw new Error('camera-capture needs a positive finite threshold');
    return { kind, intensitySamples, threshold };
  }

  if (kind === 'actuator-move') {
    const currentPosition = Number(raw.currentPosition);
    const targetPosition = Number(raw.targetPosition);
    const maxVelocity = Number(raw.maxVelocity);
    const maxAcceleration = Number(raw.maxAcceleration);
    const limits = Array.isArray(raw.positionLimits) ? raw.positionLimits.map(Number) : null;
    if (!Number.isFinite(currentPosition)) throw new Error('actuator-move needs a finite currentPosition');
    if (!Number.isFinite(targetPosition)) throw new Error('actuator-move needs a finite targetPosition');
    if (!Number.isFinite(maxVelocity) || maxVelocity <= 0) throw new Error('actuator-move needs a positive finite maxVelocity');
    if (!Number.isFinite(maxAcceleration) || maxAcceleration <= 0) throw new Error('actuator-move needs a positive finite maxAcceleration');
    if (!limits || limits.length !== 2 || !Number.isFinite(limits[0]) || !Number.isFinite(limits[1]) || limits[0] >= limits[1]) {
      throw new Error('actuator-move needs positionLimits: [min, max] with min < max, both finite');
    }
    const maxMoveTimeS = Number.isFinite(Number(raw.maxMoveTimeS)) && Number(raw.maxMoveTimeS) > 0 ? Number(raw.maxMoveTimeS) : 300;
    return { kind, currentPosition, targetPosition, maxVelocity, maxAcceleration, positionLimits: [limits[0], limits[1]], maxMoveTimeS };
  }

  if (kind === 'sensor-read') {
    const claimedValue = Number(raw.claimedValue);
    const range = Array.isArray(raw.expectedRange) ? raw.expectedRange.map(Number) : null;
    if (!range || range.length !== 2 || !Number.isFinite(range[0]) || !Number.isFinite(range[1]) || range[0] >= range[1]) {
      throw new Error('sensor-read needs expectedRange: [min, max] with min < max, both finite');
    }
    const timestampMs = Number(raw.timestampMs);
    const nowMs = Number.isFinite(Number(raw.nowMs)) ? Number(raw.nowMs) : timestampMs;
    if (!Number.isFinite(timestampMs)) throw new Error('sensor-read needs a finite timestampMs');
    const maxStalenessMs = Number.isFinite(Number(raw.maxStalenessMs)) && Number(raw.maxStalenessMs) >= 0 ? Number(raw.maxStalenessMs) : 5000;
    const MAX_RECENT = 200;
    const recentValues = Array.isArray(raw.recentValues) ? raw.recentValues.slice(0, MAX_RECENT).map(Number) : [];
    return { kind, claimedValue, expectedRange: [range[0], range[1]], timestampMs, nowMs, maxStalenessMs, recentValues };
  }

  throw new Error(`Unknown physical action kind "${kind}" (expected camera-capture, actuator-move, or sensor-read)`);
}

// --- simulate ----------------------------------------------------------

function simulateCameraCapture(spec) {
  const failureModes = [];
  let simulationDetail;

  try {
    const events = simulatePixelEvents(spec.intensitySamples, { threshold: spec.threshold });
    simulationDetail = { eventCount: events.length };
  } catch (err) {
    // The real simulator's own bounded-execution ceiling (see
    // eventCameraPixel.js's MAX_BURST_PER_TRANSITION/MAX_EVENTS_TOTAL) —
    // reported here as a concrete failure mode of the PROPOSED capture
    // configuration, not swallowed into a generic "inconclusive".
    const code = /MAX_BURST_PER_TRANSITION/.test(err.message) ? 'pixel-burst-overload' : /MAX_EVENTS_TOTAL/.test(err.message) ? 'event-log-overflow' : 'simulation-error';
    failureModes.push(failureMode(code, 'high', err.message));
    simulationDetail = { eventCount: null, simulationError: err.message };
  }

  // Real DVS sensors' contrast threshold typically sits roughly
  // 0.1-0.5 in natural-log units (see eventCameraPixel.js's own header
  // citation) -- outside that band the proposed capture configuration
  // is a real risk even when the simulation itself doesn't throw.
  if (spec.threshold < 0.05) {
    failureModes.push(failureMode('threshold-too-sensitive', 'medium', `threshold=${spec.threshold} is far below the ~0.1-0.5 typical real-sensor range; expect noise-driven spurious events, not genuine transients`));
  } else if (spec.threshold > 1.0) {
    failureModes.push(failureMode('threshold-too-insensitive', 'low', `threshold=${spec.threshold} is far above the ~0.1-0.5 typical real-sensor range; expect missed transients`));
  }

  return { failureModes, simulationDetail };
}

function simulateActuatorMove(spec) {
  const failureModes = [];
  const { currentPosition, targetPosition, maxVelocity, maxAcceleration, positionLimits, maxMoveTimeS } = spec;
  const [lo, hi] = positionLimits;

  if (currentPosition < lo || currentPosition > hi) {
    failureModes.push(failureMode('current-position-out-of-bounds', 'high', `currentPosition=${currentPosition} is already outside declared positionLimits [${lo}, ${hi}] -- this move should not be commanded from an already-out-of-bounds state`));
  }
  if (targetPosition < lo || targetPosition > hi) {
    failureModes.push(failureMode('target-outside-position-limits', 'high', `targetPosition=${targetPosition} is outside declared positionLimits [${lo}, ${hi}]`));
  }

  // Real stopping-distance physics: distance needed to decelerate from
  // maxVelocity to 0 at maxAcceleration is v^2 / (2*a). If that distance
  // exceeds the room actually available between the target and whichever
  // limit the move is headed toward, an axis commanded at full speed
  // could overshoot past the physical travel limit before it can stop --
  // a real, mechanistically grounded safety check, not a heuristic.
  const stoppingDistance = (maxVelocity * maxVelocity) / (2 * maxAcceleration);
  const headingPositive = targetPosition >= currentPosition;
  const roomBeyondTarget = headingPositive ? (hi - targetPosition) : (targetPosition - lo);
  if (roomBeyondTarget < stoppingDistance && targetPosition >= lo && targetPosition <= hi) {
    failureModes.push(failureMode('insufficient-stopping-distance', 'high', `at maxVelocity=${maxVelocity} and maxAcceleration=${maxAcceleration}, stopping distance is ${stoppingDistance.toFixed(4)}, but only ${roomBeyondTarget.toFixed(4)} of travel remains beyond the target before the ${headingPositive ? 'upper' : 'lower'} limit -- an overshoot could exceed the physical travel limit`));
  }

  // Trapezoidal motion profile: distance covered while accelerating to
  // maxVelocity (and, symmetrically, decelerating) is v^2/(2a) each; if
  // the total move distance is less than that, maxVelocity is never
  // reached and this is a triangular profile instead -- both are
  // computed in closed form, no iteration.
  const distance = Math.abs(targetPosition - currentPosition);
  const accelDistanceEach = (maxVelocity * maxVelocity) / (2 * maxAcceleration);
  let moveTimeS;
  if (distance >= 2 * accelDistanceEach) {
    const cruiseDistance = distance - 2 * accelDistanceEach;
    moveTimeS = 2 * (maxVelocity / maxAcceleration) + cruiseDistance / maxVelocity;
  } else {
    const peakVelocity = Math.sqrt(distance * maxAcceleration);
    moveTimeS = 2 * (peakVelocity / maxAcceleration);
  }
  if (moveTimeS > maxMoveTimeS) {
    failureModes.push(failureMode('move-exceeds-time-budget', 'medium', `estimated move time ${moveTimeS.toFixed(3)}s exceeds maxMoveTimeS=${maxMoveTimeS}s -- a real actuator taking this long may indicate a stall, an obstruction, or parameters that don't match the physical hardware`));
  }

  return { failureModes, simulationDetail: { distance, estimatedMoveTimeS: moveTimeS, stoppingDistance } };
}

function simulateSensorRead(spec) {
  const failureModes = [];
  const { claimedValue, expectedRange, timestampMs, nowMs, maxStalenessMs, recentValues } = spec;
  const [lo, hi] = expectedRange;

  if (!Number.isFinite(claimedValue)) {
    failureModes.push(failureMode('non-finite-reading', 'high', `claimedValue is ${claimedValue} -- many real sensors report NaN/Infinity on a disconnect or ADC fault; this reading should not be acted on`));
  } else if (claimedValue < lo || claimedValue > hi) {
    failureModes.push(failureMode('reading-outside-expected-range', 'high', `claimedValue=${claimedValue} is outside expectedRange [${lo}, ${hi}] -- either a genuine extreme environmental condition or a sensor fault; either way this needs human judgment before anything acts on it`));
  }

  const stalenessMs = nowMs - timestampMs;
  if (stalenessMs > maxStalenessMs) {
    failureModes.push(failureMode('stale-reading', 'medium', `reading is ${stalenessMs}ms old, over the ${maxStalenessMs}ms staleness budget -- acting on it risks acting on conditions that have already changed`));
  } else if (stalenessMs < 0) {
    failureModes.push(failureMode('reading-timestamp-in-future', 'medium', `reading timestamp is ${-stalenessMs}ms in the future relative to nowMs -- a clock-sync fault between the sensor and this system`));
  }

  // Stuck-sensor detection: a real, well-known fault signature is a
  // sensor that freezes and repeats its last value. Only meaningful
  // with enough samples to distinguish "stuck" from "coincidentally
  // stable reading" -- 3 is the minimum for that distinction to mean
  // anything at all, and even then this is reported as a real risk
  // signal, not a certainty.
  if (recentValues.length >= 3) {
    const mean = recentValues.reduce((a, b) => a + b, 0) / recentValues.length;
    const variance = recentValues.reduce((a, b) => a + (b - mean) ** 2, 0) / recentValues.length;
    if (variance === 0) {
      failureModes.push(failureMode('sensor-may-be-stuck', 'medium', `the last ${recentValues.length} readings are all exactly ${recentValues[0]} -- zero variance across repeated readings is a real, known frozen/stuck-sensor fault signature`));
    }
  }

  return { failureModes, simulationDetail: { stalenessMs } };
}

/**
 * Runs the real, deterministic pre-actuation simulation for a
 * normalized physical action spec. Never throws on a bad physical
 * condition -- every finding becomes an entry in `failureModes`. Only
 * throws if `spec` itself was not produced by normalizePhysicalActionSpec
 * (a programming error, not a physical-world condition).
 */
export function simulatePhysicalAction(spec) {
  if (!spec || typeof spec !== 'object' || !spec.kind) throw new Error('simulatePhysicalAction needs an already-normalized spec');

  let result;
  if (spec.kind === 'camera-capture') result = simulateCameraCapture(spec);
  else if (spec.kind === 'actuator-move') result = simulateActuatorMove(spec);
  else if (spec.kind === 'sensor-read') result = simulateSensorRead(spec);
  else throw new Error(`simulatePhysicalAction: unknown kind "${spec.kind}"`);

  const highestSeverity = result.failureModes.some((f) => f.severity === 'high') ? 'high'
    : result.failureModes.some((f) => f.severity === 'medium') ? 'medium'
      : result.failureModes.length ? 'low' : 'none';

  return {
    kind: spec.kind,
    safe: result.failureModes.length === 0,
    highestSeverity,
    failureModes: result.failureModes,
    simulationDetail: result.simulationDetail,
    honesty: result.failureModes.length === 0
      ? 'The simulation found no failure mode among the ones this instrument checks for. This is NOT a guarantee the real action is safe -- only that it is safe with respect to the specific, named checks implemented here (see this module\'s header for exactly what those are and are not).'
      : `The simulation found ${result.failureModes.length} failure mode(s), listed above. This does NOT auto-block the action -- same stance as every claim-verification kernel in this repo: the finding is surfaced, a human decides.`,
  };
}

/**
 * Same shape as deviceExecutor.js's writeFile/readFile: requires a
 * fresh, explicit confirm(action) for THIS specific action, every
 * time, with the simulation's failure-mode list attached to what
 * confirm() sees as action.simulation. There is no path in this
 * function that skips the simulation or skips confirm() -- and, per
 * this file's header, no path anywhere in this repo that turns an
 * approval returned from here into a real physical action, because no
 * physical executor exists yet.
 */
export async function gatePhysicalAction(rawSpec, { confirm, requestedBy = 'unknown' } = {}) {
  if (typeof confirm !== 'function') {
    throw new Error('gatePhysicalAction requires an explicit confirm(action) function; no approval happens without a fresh per-action review of the simulation results');
  }
  const spec = normalizePhysicalActionSpec(rawSpec);
  const simulation = simulatePhysicalAction(spec);

  const action = { op: 'physical-action', kind: spec.kind, spec, requestedBy, simulation };
  const approved = await confirm(action);

  return {
    approved: Boolean(approved),
    kind: spec.kind,
    safe: simulation.safe,
    highestSeverity: simulation.highestSeverity,
    failureModes: simulation.failureModes,
    simulationDetail: simulation.simulationDetail,
    reason: approved ? 'confirmed' : 'confirm() declined this specific action',
  };
}
