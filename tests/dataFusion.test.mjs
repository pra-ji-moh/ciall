import test from 'node:test';
import assert from 'node:assert/strict';
import { fuseMeasurements } from '../src/lib/dataFusion.js';

test('fuses two compatible measurements via inverse-variance weighting, matching a hand-computed result', () => {
  // value=10,unc=1 and value=12,unc=1 -> sigma = |10-12|/sqrt(1+1) = 1.414 (compatible)
  // fused value = (10*1 + 12*1) / (1+1) = 11; fused uncertainty = sqrt(1/(1+1)) = 0.70710678...
  const result = fuseMeasurements([
    { value: 10, uncertainty: 1, source: 'sensor A' },
    { value: 12, uncertainty: 1, source: 'sensor B' },
  ]);
  assert.equal(result.refusedReason, null);
  assert.equal(result.fused.value, 11);
  assert.ok(Math.abs(result.fused.uncertainty - Math.sqrt(0.5)) < 1e-9);
});

test('fused uncertainty is always <= the smallest input uncertainty -- more independent info sharpens, never widens', () => {
  const result = fuseMeasurements([
    { value: 10, uncertainty: 2 },
    { value: 10.5, uncertainty: 3 },
  ]);
  assert.ok(result.fused.uncertainty <= 2);
});

test('refuses to fuse measurements in DECISIVE tension -- the exact Hubble tension figures already verified elsewhere in this repo', () => {
  const result = fuseMeasurements([
    { value: 67.4, uncertainty: 0.5, source: 'Planck 2018' },
    { value: 73.0, uncertainty: 1.0, source: 'SH0ES' },
  ]);
  assert.equal(result.fused, null);
  assert.match(result.refusedReason, /decisive tension/);
  assert.match(result.refusedReason, /Planck 2018/);
});

test('sub-decisive tension does not block fusion but is carried as a caveat, not hidden', () => {
  // value=10,unc=0.5 and value=13,unc=0.5 -> sigma = 3/sqrt(0.5^2+0.5^2) = 4.24
  // (3-5 sigma band: "tension", evidence but not decisive)
  const result = fuseMeasurements([
    { value: 10, uncertainty: 0.5 },
    { value: 13, uncertainty: 0.5 },
  ]);
  assert.equal(result.refusedReason, null, 'sub-decisive tension must not block fusion');
  assert.ok(result.fused, 'a fused value must still be produced');
  assert.equal(result.caveats.length, 1, 'the tension must be surfaced as a caveat, not silently smoothed over');
});

test('rejects fewer than 2 measurements, and any non-finite value/uncertainty', () => {
  assert.throws(() => fuseMeasurements([{ value: 1, uncertainty: 1 }]), /at least 2/);
  assert.throws(() => fuseMeasurements([{ value: 1, uncertainty: 1 }, { value: NaN, uncertainty: 1 }]), /finite value/);
  assert.throws(() => fuseMeasurements([{ value: 1, uncertainty: 0 }, { value: 2, uncertainty: 1 }]), /positive finite uncertainty/);
});

test('checkedPairs reports every pair checked, for 3+ measurements (3 measurements = 3 pairs)', () => {
  const result = fuseMeasurements([
    { value: 10, uncertainty: 1 },
    { value: 10.5, uncertainty: 1 },
    { value: 9.8, uncertainty: 1 },
  ]);
  assert.equal(result.checkedPairs.length, 3); // (0,1) (0,2) (1,2)
});
