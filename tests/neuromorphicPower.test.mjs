// neuromorphicPower.test.mjs; upgrade 10 — verifies the energy-claim
// kernel's arithmetic against the actual cited published figures in
// its own source (Loihi 1: ~15pJ/synaptic-op nominal, banded
// 10-25pJ; sparse/event-driven neuromorphic-vs-GPU efficiency:
// ~100-1000x published, banded 10x-10000x here), and its honesty
// discipline (inconclusive rather than guessed where the literature
// itself doesn't give one clean number).

import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizeEnergyClaimSpec, verifyEnergyClaim } from '../src/lib/neuromorphicPower.js';
import { getKernel, listKernels } from '../src/lib/kernelRegistry.js';

test('registered as the 9th kernel, exposes normalize/run like every other kernel', () => {
  const ids = listKernels().map((k) => k.id);
  assert.ok(ids.includes('neuromorphic-power'));
  const kernel = getKernel('neuromorphic-power');
  assert.equal(typeof kernel.normalize, 'function');
  assert.equal(typeof kernel.run, 'function');
  assert.equal(kernel.deterministic, true);
});

test('loihi1 total-energy: a claim within the published 10-25pJ/op band holds', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'total-energy', operationCount: 1e9, claimedEnergyJoules: 15e9 * 1e-12 }); // 15pJ/op * 1e9 ops
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'held');
});

test('loihi1 total-energy: a claim far below the published minimum (violates known per-op floor) is violated', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'total-energy', operationCount: 1e9, claimedEnergyJoules: 1e9 * 1e-12 }); // 1pJ/op, below the 10pJ published floor
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'violated');
});

test('loihi1 total-energy: a claim far above the published maximum is violated', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'total-energy', operationCount: 1e9, claimedEnergyJoules: 1000e9 * 1e-12 }); // 1000pJ/op, way above published
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'violated');
});

test('loihi1 total-energy: the expected range in the result is computed directly from operationCount * the cited per-op band', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'total-energy', operationCount: 1e6, claimedEnergyJoules: 15e6 * 1e-12 });
  const result = verifyEnergyClaim(spec);
  assert.equal(result.expectedRangeJoules[0], 1e6 * 10 * 1e-12);
  assert.equal(result.expectedRangeJoules[1], 1e6 * 25 * 1e-12);
});

test('loihi2/generic-event-driven: an implausibly tiny implied per-op energy is violated (dynamic power cannot be ~zero for a nonzero spike count)', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi2', metric: 'total-energy', operationCount: 1e12, claimedEnergyJoules: 1e-15 }); // implies ~0.000001pJ/op
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'violated');
});

test('loihi2/generic-event-driven: an implausibly huge implied per-op energy (worse than any published figure, defeats the point of event-driven hardware) is violated', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi2', metric: 'total-energy', operationCount: 1, claimedEnergyJoules: 1 }); // 1 joule for ONE operation -- absurd
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'violated');
});

test('loihi2/generic-event-driven: a plausible-but-not-precisely-citable claim is honestly inconclusive, never guessed', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi2', metric: 'total-energy', operationCount: 1e9, claimedEnergyJoules: 15e9 * 1e-12 }); // 15pJ/op, plausible, but no single citable Loihi 2 figure per this kernel's own disclosed source note
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'inconclusive');
  assert.match(result.reason, /no single, internally-consistent published/i);
});

test('efficiency-ratio-vs-gpu: a claim within the published 100-1000x range (banded 10x-10000x) for a sparse/event-driven workload holds', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'generic-event-driven', metric: 'efficiency-ratio-vs-gpu', claimedRatio: 300, workloadIsSparseEventDriven: true });
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'held');
});

test('efficiency-ratio-vs-gpu: an implausible ratio (orders of magnitude outside the band) is violated', () => {
  const tooLow = verifyEnergyClaim(normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'efficiency-ratio-vs-gpu', claimedRatio: 2, workloadIsSparseEventDriven: true }));
  assert.equal(tooLow.verdict, 'violated');
  const tooHigh = verifyEnergyClaim(normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'efficiency-ratio-vs-gpu', claimedRatio: 5_000_000, workloadIsSparseEventDriven: true }));
  assert.equal(tooHigh.verdict, 'violated');
});

test('efficiency-ratio-vs-gpu: a claim NOT marked sparse/event-driven is honestly inconclusive -- the cited range does not apply to dense workloads', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'efficiency-ratio-vs-gpu', claimedRatio: 300, workloadIsSparseEventDriven: false });
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'inconclusive');
  assert.match(result.reason, /sparse, event-driven workloads/i);
});

test('kind:"none" is a clean escape hatch, never forced into a verdict', () => {
  const spec = normalizeEnergyClaimSpec({ kind: 'none', reason: 'not reducible to a checkable energy figure' });
  const result = verifyEnergyClaim(spec);
  assert.equal(result.verdict, 'inconclusive');
  assert.equal(result.reason, 'not reducible to a checkable energy figure');
});

test('normalize rejects an unknown device and an unknown kind loudly', () => {
  assert.throws(() => normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'made-up-chip', metric: 'total-energy', operationCount: 1, claimedEnergyJoules: 1 }), /Unknown device/);
  assert.throws(() => normalizeEnergyClaimSpec({ kind: 'not-a-real-kind' }), /Unknown energy claim spec kind/);
  assert.throws(() => normalizeEnergyClaimSpec(null), /is not an object/);
});

test('normalize rejects non-finite/negative numeric fields loudly rather than silently coercing them', () => {
  assert.throws(() => normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'total-energy', operationCount: -5, claimedEnergyJoules: 1 }), /operationCount must be a positive/);
  assert.throws(() => normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'total-energy', operationCount: 1, claimedEnergyJoules: -1 }), /claimedEnergyJoules must be a non-negative/);
  assert.throws(() => normalizeEnergyClaimSpec({ kind: 'energy_claim', device: 'loihi1', metric: 'efficiency-ratio-vs-gpu', claimedRatio: NaN }), /claimedRatio must be a positive/);
});
