// domains/motion.js; "motion detection" in the one sense that actually
// fits this substrate's core mechanism: does an observed/claimed
// trajectory match a stated reference model of its motion, or does it
// diverge? Zero new kernel code — a thin wrapper over
// dynamicsCheck.js's trajectory-equivalence checker, unmodified.
//
// WHAT THIS IS NOT, stated plainly rather than left implied: this is not
// camera/video motion detection. That's a different category of
// software (frame capture, image differencing, optical flow) with no
// relationship to claim verification, and this substrate has zero
// external dependencies by design (no image/video library exists here
// to build it on) — building it wouldn't be "using only the core
// mechanism," it would be a different mechanism entirely. What IS a
// genuine fit: detecting whether a system's ACTUAL motion (its real
// trajectory) diverges from a CLAIMED model of that motion — exactly
// what dynamicsCheck.js already computes, seeded and reproducible.

import { normalizeDynamicsSpec, executeDynamicsCheck } from '../lib/dynamicsCheck.js';
import { compileExpr } from '../lib/mathExpr.js';

/**
 * `spec`: the dynamics_equivalence shape dynamicsCheck.js expects minus
 * the `kind` field (added here) — stateA/stateB, derivA/derivB, initA/
 * initB, compareA/compareB, params, T, dt, tolerance. See
 * dynamicsCheck.js's own header for the full contract (seeded RK4,
 * step-halving audit before a divergence counts as decisive, "matched"
 * is always reported as weak evidence, never proof).
 */
export function verifyMotionMatchesModel(spec) {
  const normalized = normalizeDynamicsSpec({ kind: 'dynamics_equivalence', ...spec });
  return executeDynamicsCheck(normalized);
}

/**
 * Verifies EXTERNALLY-SUPPLIED observed samples (positions over time —
 * from a camera-tracking pipeline, a sensor log, anything at all) against
 * a claimed reference motion model. This does not capture or touch a
 * camera itself; it only checks whether already-observed data matches a
 * stated model, deterministically. If your tracking data came from a
 * camera pipeline running elsewhere, this is how you'd check it against
 * a claim about what that motion should look like.
 *
 * Unlike `verifyMotionMatchesModel` above (which integrates two systems
 * from their own equations of motion via dynamicsCheck.js), this is NEW
 * comparison logic — said plainly, not left implied — because no
 * existing kernel compares a model against externally-supplied discrete
 * data points. It's built on `compileExpr`, the same expression engine
 * every numeric instrument in this substrate already uses, and follows
 * the same honesty discipline: a divergence returns the exact sample
 * (timestamp, observed value, predicted value, residual) that broke
 * tolerance, never a vague downgrade; a match is reported as confirming
 * only the samples given, not the model in general.
 *
 * `modelExpr`: an expression in `t` (plus any named `params`) giving the
 * model's claimed value at time t.
 * `observedSamples`: [{ t, value }] — the actual observed data points.
 * `tolerance`: max allowed |observed - model(t)| before it counts as a
 * divergence.
 */
export function verifyObservedTrajectory({ modelExpr, observedSamples, params = {}, tolerance }) {
  if (!Array.isArray(observedSamples) || observedSamples.length === 0) {
    throw new Error('observedSamples must be a non-empty array of { t, value }');
  }
  if (!(tolerance > 0)) throw new Error('tolerance must be a positive number');

  const paramNames = Object.keys(params);
  const model = compileExpr(modelExpr, ['t', ...paramNames]);

  let worst = null;
  for (const sample of observedSamples) {
    const predicted = model({ t: sample.t, ...params });
    const residual = Math.abs(sample.value - predicted);
    if (!worst || residual > worst.residual) worst = { t: sample.t, observed: sample.value, predicted, residual };
  }

  const diverged = worst.residual > tolerance;
  return {
    verdict: diverged ? 'diverged' : 'matched',
    worst,
    sampleCount: observedSamples.length,
    honesty: diverged
      ? `Observed value ${worst.observed} at t=${worst.t} diverges from the claimed model's predicted ${worst.predicted.toFixed(6)} by ${worst.residual.toFixed(6)}, over the ${tolerance} tolerance — a concrete, checkable divergence.`
      : `All ${observedSamples.length} observed samples matched the claimed model within tolerance ${tolerance}. This confirms the model on the samples given; it does not prove the model holds between samples or outside this data.`,
  };
}

/**
 * Streaming/real-time variant of verifyObservedTrajectory. The batch
 * version above needs the whole dataset upfront; this is for data that
 * arrives incrementally — stdin, a file being tailed, a socket, a
 * polling loop, whatever real-time source a caller wires up. Ciall does
 * not open any of those sources itself (see bin/ciall.mjs's
 * `stream-motion` command for one concrete example, reading stdin); this
 * is the verification logic a real-time feed would call into, one
 * sample at a time, exactly as it arrives.
 *
 * Same math as the batch version (compileExpr, same residual/tolerance
 * check), restructured to score each sample the moment it's observed
 * instead of waiting for a complete array. `observe()` returns that
 * sample's own verdict immediately; `status()` returns the cumulative
 * picture across everything seen so far. Once diverged, stays diverged —
 * a later matching sample does not retroactively excuse an earlier
 * concrete divergence.
 */
export function createStreamingTrajectoryVerifier({ modelExpr, params = {}, tolerance }) {
  if (!(tolerance > 0)) throw new Error('tolerance must be a positive number');
  const paramNames = Object.keys(params);
  const model = compileExpr(modelExpr, ['t', ...paramNames]);

  let worst = null;
  let sampleCount = 0;
  let everDiverged = false;

  return {
    /** Feed one real-time sample; returns ITS OWN verdict immediately. */
    observe({ t, value }) {
      const predicted = model({ t, ...params });
      const residual = Math.abs(value - predicted);
      sampleCount++;
      const sampleDiverged = residual > tolerance;
      if (sampleDiverged) everDiverged = true;
      if (!worst || residual > worst.residual) worst = { t, observed: value, predicted, residual };
      return { verdict: sampleDiverged ? 'diverged' : 'held', t, observed: value, predicted, residual };
    },
    /** Cumulative status across every sample observed so far. */
    status() {
      return {
        verdict: everDiverged ? 'diverged' : 'matched',
        sampleCount,
        worst,
        honesty: everDiverged
          ? `A sample diverged from the claimed model at t=${worst.t} (residual ${worst.residual.toFixed(6)} over tolerance ${tolerance}) — decisive, and does not get un-found by later samples matching.`
          : `All ${sampleCount} samples observed so far matched within tolerance ${tolerance}. Running evidence only — a sample not yet seen could still diverge.`,
      };
    },
  };
}
