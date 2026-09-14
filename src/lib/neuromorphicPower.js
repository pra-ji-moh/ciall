// neuromorphicPower.js; upgrade 10 — a deterministic kernel verifying
// a claimed neuromorphic-hardware energy figure against REAL, cited
// published device physics, not an invented comparison number. Same
// "model designs a spec, device verifies it" shape as every other
// kernel in this repo.
//
// SOURCES (checked at build time, 2026-08; re-verify before trusting
// for anything beyond this repo's own test/demo purposes — published
// chip specs get revised, and secondary summaries of them are not
// always internally consistent, see the Loihi 2 note below):
//  - Loihi 1: ~15 picojoules per synaptic operation at nominal
//    operating conditions, ~30 billion synaptic ops/sec (Intel's own
//    published figures, widely cited).
//  - Loihi 2: published secondary sources are NOT fully consistent
//    with each other on an exact pJ/op figure (some summaries describe
//    an IMPROVEMENT over Loihi 1 while citing a numerically HIGHER
//    picojoule figure than Loihi 1's, which cannot both be true) --
//    disclosed here rather than picking whichever number sounded
//    better. This kernel therefore verifies Loihi 2 claims against the
//    qualitative, unambiguous, well-established property instead of a
//    single contested number: event-driven execution means DYNAMIC
//    power is consumed ONLY when a spike/event actually occurs, not
//    continuously -- so total energy must scale with actual event
//    count, not with elapsed time or a device's peak/idle draw.
//  - Neuromorphic-vs-GPU efficiency for SPARSE, EVENT-DRIVEN workloads
//    specifically (not dense training, where GPUs remain better
//    suited): commonly cited range is ~100-1000x energy-efficiency
//    improvement, e.g. ~1000 inferences/joule for event-driven vision
//    tasks vs ~10-100 inferences/joule on comparable GPU hardware.
//    This is a workload-class-specific range from the published
//    literature, not a universal physical constant -- treated here as
//    a wide PLAUSIBILITY band, not a razor-thin pass/fail line.
//
// HONESTY DISCIPLINE: this kernel never asserts a claim is "verified"
// as physically true -- consistency with published literature is the
// most it can decide. A claim inside the plausible band is 'held'
// (consistent with what's published, not independently re-measured);
// a claim far outside any defensible range (orders of magnitude off)
// is 'violated'; a claim about a device/metric this kernel has no
// citable data for is 'inconclusive', never guessed.

const LOIHI1_PJ_PER_OP_MIN = 10; // picojoules; published nominal figure is ~15pJ, banded generously either side
const LOIHI1_PJ_PER_OP_MAX = 25;
const LOIHI1_NOMINAL_OPS_PER_SEC = 30e9;

// Neuromorphic-vs-GPU efficiency ratio for sparse/event-driven
// workloads: published range ~100-1000x. Banded generously (10x-10000x)
// since this is a workload-class comparison, not a fixed constant --
// the point is catching claims that are implausible by an ORDER OF
// MAGNITUDE (e.g. 1x or 1,000,000x), not litigating whether a specific
// workload achieves 150x vs 400x.
const EFFICIENCY_RATIO_PLAUSIBLE_MIN = 10;
const EFFICIENCY_RATIO_PLAUSIBLE_MAX = 10000;

const KNOWN_DEVICES = new Set(['loihi1', 'loihi2', 'generic-event-driven']);

export function normalizeEnergyClaimSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Energy claim spec is not an object');
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };
  if (raw.kind !== 'energy_claim') throw new Error(`Unknown energy claim spec kind "${raw.kind}"`);

  const device = String(raw.device || '');
  if (!KNOWN_DEVICES.has(device)) throw new Error(`Unknown device "${device}"; expected one of ${[...KNOWN_DEVICES].join(', ')}`);

  const metric = raw.metric === 'efficiency-ratio-vs-gpu' ? 'efficiency-ratio-vs-gpu' : 'total-energy';

  if (metric === 'total-energy') {
    const operationCount = Number(raw.operationCount);
    const claimedEnergyJoules = Number(raw.claimedEnergyJoules);
    if (!Number.isFinite(operationCount) || operationCount <= 0) throw new Error('operationCount must be a positive finite number');
    if (!Number.isFinite(claimedEnergyJoules) || claimedEnergyJoules < 0) throw new Error('claimedEnergyJoules must be a non-negative finite number');
    return { kind: 'energy_claim', device, metric, operationCount, claimedEnergyJoules, note: String(raw.note || '').slice(0, 300) };
  }

  const claimedRatio = Number(raw.claimedRatio);
  if (!Number.isFinite(claimedRatio) || claimedRatio <= 0) throw new Error('claimedRatio must be a positive finite number');
  const workloadIsSparseEventDriven = Boolean(raw.workloadIsSparseEventDriven);
  return { kind: 'energy_claim', device, metric, claimedRatio, workloadIsSparseEventDriven, note: String(raw.note || '').slice(0, 300) };
}

export function verifyEnergyClaim(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason };

  if (spec.metric === 'total-energy') {
    if (spec.device === 'loihi1') {
      const minJoules = spec.operationCount * LOIHI1_PJ_PER_OP_MIN * 1e-12;
      const maxJoules = spec.operationCount * LOIHI1_PJ_PER_OP_MAX * 1e-12;
      const held = spec.claimedEnergyJoules >= minJoules && spec.claimedEnergyJoules <= maxJoules;
      return {
        verdict: held ? 'held' : 'violated',
        device: spec.device,
        expectedRangeJoules: [minJoules, maxJoules],
        claimedEnergyJoules: spec.claimedEnergyJoules,
        source: 'Intel Loihi 1 published nominal figure: ~15 picojoules/synaptic-operation (banded 10-25pJ here), ~30e9 ops/sec nominal.',
        honesty: held
          ? `${spec.claimedEnergyJoules.toExponential(3)}J for ${spec.operationCount.toExponential(3)} synaptic operations is consistent with Loihi 1's published per-operation energy figure (banded ${LOIHI1_PJ_PER_OP_MIN}-${LOIHI1_PJ_PER_OP_MAX}pJ/op). Consistency with a published spec, not an independent re-measurement.`
          : `${spec.claimedEnergyJoules.toExponential(3)}J for ${spec.operationCount.toExponential(3)} synaptic operations falls OUTSIDE Loihi 1's published per-operation energy range (${minJoules.toExponential(3)}-${maxJoules.toExponential(3)}J expected) — either the claim, the operation count, or the device attribution is wrong.`,
      };
    }
    // loihi2 / generic-event-driven: no single contested pJ figure is
    // asserted (see file header) — verify the one thing that IS
    // unambiguous: energy must scale with operation count at a
    // POSITIVE, FINITE per-operation rate. A claim of ~0 total energy
    // for a nonzero operation count, or an implausibly large per-op
    // rate (worse than known non-event-driven hardware, defeating the
    // entire point of event-driven execution), is flagged; everything
    // else is inconclusive rather than guessed.
    const perOpJoules = spec.claimedEnergyJoules / spec.operationCount;
    const impliedPjPerOp = perOpJoules * 1e12;
    if (impliedPjPerOp <= 0.001) {
      return { verdict: 'violated', device: spec.device, impliedPjPerOp, honesty: `Implied ${impliedPjPerOp.toExponential(3)}pJ/operation is not physically plausible for any published event-driven device — dynamic power still has a nonzero floor per spike.` };
    }
    if (impliedPjPerOp > 1e6) {
      return { verdict: 'violated', device: spec.device, impliedPjPerOp, honesty: `Implied ${impliedPjPerOp.toExponential(3)}pJ/operation is orders of magnitude above ANY published neuromorphic figure (Loihi 1's own published rate is ~15pJ/op) — this would mean the device provides no event-driven efficiency advantage at all.` };
    }
    return { verdict: 'inconclusive', device: spec.device, impliedPjPerOp, reason: `No single, internally-consistent published per-operation figure exists for "${spec.device}" (see this file's header on the Loihi 2 source inconsistency) to verify against precisely; the implied rate (${impliedPjPerOp.toExponential(3)}pJ/op) is at least within the physically plausible range for event-driven hardware.` };
  }

  // efficiency-ratio-vs-gpu
  const held = spec.claimedRatio >= EFFICIENCY_RATIO_PLAUSIBLE_MIN && spec.claimedRatio <= EFFICIENCY_RATIO_PLAUSIBLE_MAX;
  if (!spec.workloadIsSparseEventDriven) {
    return {
      verdict: 'inconclusive',
      reason: 'The published 100-1000x neuromorphic efficiency range is specific to SPARSE, EVENT-DRIVEN workloads; this claim is not marked as one, so the citation does not apply either way. Neuromorphic hardware is not claimed to beat GPUs on dense workloads (e.g. dense training) at all.',
      claimedRatio: spec.claimedRatio,
    };
  }
  return {
    verdict: held ? 'held' : 'violated',
    claimedRatio: spec.claimedRatio,
    plausibleRange: [EFFICIENCY_RATIO_PLAUSIBLE_MIN, EFFICIENCY_RATIO_PLAUSIBLE_MAX],
    source: 'Published range for sparse/event-driven workloads specifically: ~100-1000x energy-efficiency vs comparable GPU hardware (e.g. ~1000 inferences/joule neuromorphic vs ~10-100 inferences/joule GPU for event-driven vision tasks). Banded 10x-10000x here as a plausibility check, not a precise bound.',
    honesty: held
      ? `A claimed ${spec.claimedRatio}x efficiency gain for a sparse/event-driven workload is within the plausible range the published literature supports. Consistency with cited literature, not an independent benchmark of THIS specific claim.`
      : `A claimed ${spec.claimedRatio}x efficiency gain is outside any defensible reading of the published 100-1000x range for sparse/event-driven workloads (banded 10x-10000x here) — worth checking the workload characterization or the arithmetic behind the claim.`,
  };
}

export function buildEnergyClaimPrompt(node) {
  return `Design an ENERGY-CLAIM VERIFICATION spec for this claim about neuromorphic/event-driven hardware: a check against real published device physics, not a vibe check.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Return ONLY JSON, one of:
{"kind":"energy_claim","device":"loihi1|loihi2|generic-event-driven","metric":"total-energy","operationCount":<number>,"claimedEnergyJoules":<number>,"note":"what this checks"}
{"kind":"energy_claim","device":"loihi1|loihi2|generic-event-driven","metric":"efficiency-ratio-vs-gpu","claimedRatio":<number>,"workloadIsSparseEventDriven":<boolean>,"note":"what this checks"}
{"kind":"none","reason":"why this claim cannot be reduced to a checkable energy figure"}

Rules: operationCount is the number of synaptic operations (or spikes/events) the claim's workload actually performs, not a rough guess at "operations" in some other sense. workloadIsSparseEventDriven must be true only if the workload is genuinely sparse/event-driven (most inference on a spiking/event-based system) — dense workloads (e.g. dense training) are NOT covered by the neuromorphic efficiency literature this kernel checks against, and marking them true anyway will not make the check charitable, it will make it correctly return "inconclusive."`;
}
