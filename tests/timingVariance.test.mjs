// timingVariance.test.mjs; upgrade 11 — validates the timing-variance
// instrumentation itself (statistics are computed correctly against
// synthetic, exactly-known samples via the injectable `now`), then
// uses it for real against an actual registered kernel to produce and
// report genuinely measured numbers -- not asserted against a fixed
// threshold, since real wall-clock variance on shared CI hardware is
// exactly the kind of thing this instrument exists to observe honestly
// rather than gate on.

import test from 'node:test';
import assert from 'node:assert/strict';
import { measureTimingVariance, measureTimingVarianceAsync } from '../src/lib/timingVariance.js';
import { getKernel } from '../src/lib/kernelRegistry.js';

function fakeClock(sequence) {
  let i = 0;
  return () => {
    if (i >= sequence.length) throw new Error('fakeClock: ran out of scripted timestamps');
    return sequence[i++];
  };
}

test('rejects a non-function fn', () => {
  assert.throws(() => measureTimingVariance(null), /fn must be a function/);
});

test('rejects runs < 2', () => {
  assert.throws(() => measureTimingVariance(() => {}, { runs: 1 }), /runs must be an integer/);
  assert.throws(() => measureTimingVariance(() => {}, { runs: 1.5 }), /runs must be an integer/);
});

test('rejects a negative warmupRuns', () => {
  assert.throws(() => measureTimingVariance(() => {}, { warmupRuns: -1 }), /warmupRuns must be a non-negative integer/);
});

test('computes exact statistics against a scripted clock -- known samples [1,2,3,4,5]ms, mean=3, and a hand-computed population stddev', () => {
  // Each call to fn() consumes two clock readings (t0, then after fn
  // runs) -- so for N measured runs the sequence needs 2*N timestamps,
  // laid out as consecutive [start,end] pairs giving deltas 1,2,3,4,5.
  // No warmup runs here (warmupRuns:0) so the clock isn't touched for
  // those.
  const now = fakeClock([0, 1, 1, 3, 3, 6, 6, 10, 10, 15]); // deltas: 1,2,3,4,5
  const stats = measureTimingVariance(() => {}, { runs: 5, warmupRuns: 0, now });
  assert.equal(stats.runs, 5);
  assert.equal(stats.warmupRuns, 0);
  assert.deepEqual(stats.samplesMs, [1, 2, 3, 4, 5]);
  assert.equal(stats.meanMs, 3);
  assert.equal(stats.minMs, 1);
  assert.equal(stats.maxMs, 5);
  // population variance of [1,2,3,4,5] around mean 3: (4+1+0+1+4)/5 = 2, stddev = sqrt(2)
  assert.ok(Math.abs(stats.stddevMs - Math.sqrt(2)) < 1e-9);
  assert.ok(Math.abs(stats.coefficientOfVariation - Math.sqrt(2) / 3) < 1e-9);
  assert.equal(stats.spreadRatio, 5); // max/min = 5/1
  assert.match(stats.honesty, /not a bound/);
  assert.match(stats.honesty, /CERTIFICATION-GAPS\.md/);
});

test('warmup runs execute fn but never touch the clock, and are excluded from the reported samples', () => {
  // Warmup calls fn() without timing it at all (see measureTimingVariance's
  // source: the warmup loop never calls `now`) -- so the scripted clock
  // only needs timestamps for the 3 MEASURED calls, each delta = 10. A
  // total-calls counter proves the warmup loop actually ran fn() the
  // requested number of times (5 = 2 warmup + 3 measured), just untimed.
  let totalCalls = 0;
  const now = fakeClock([0, 10, 10, 20, 20, 30]); // 3 measured calls, each delta = 10
  const stats = measureTimingVariance(() => { totalCalls++; }, { runs: 3, warmupRuns: 2, now });
  assert.equal(totalCalls, 5);
  assert.equal(stats.warmupRuns, 2);
  assert.deepEqual(stats.samplesMs, [10, 10, 10]);
  assert.equal(stats.stddevMs, 0);
  assert.equal(stats.coefficientOfVariation, 0);
  assert.equal(stats.spreadRatio, 1);
});

test('spreadRatio is 1 (not NaN/Infinity) when every sample is exactly 0ms', () => {
  const now = fakeClock([0, 0, 0, 0, 0, 0]);
  const stats = measureTimingVariance(() => {}, { runs: 3, warmupRuns: 0, now });
  assert.deepEqual(stats.samplesMs, [0, 0, 0]);
  assert.equal(stats.spreadRatio, 1);
  assert.equal(stats.coefficientOfVariation, 0);
});

test('measureTimingVarianceAsync awaits fn and rejects the same invalid inputs', async () => {
  await assert.rejects(() => measureTimingVarianceAsync(null), /fn must be a function/);
  await assert.rejects(() => measureTimingVarianceAsync(() => {}, { runs: 1 }), /runs must be an integer/);

  let calls = 0;
  const now = fakeClock([0, 5, 5, 12]); // 0 warmup, 2 measured: deltas 5, 7
  const stats = await measureTimingVarianceAsync(async () => { calls++; await Promise.resolve(); }, { runs: 2, warmupRuns: 0, now });
  assert.equal(calls, 2);
  assert.deepEqual(stats.samplesMs, [5, 7]);
});

test('real measurement: boundary-check kernel (a pure, fast, deterministic kernel) reports plausible real-world statistics', () => {
  const kernel = getKernel('boundary-check');
  const spec = kernel.normalize({ kind: 'path-containment', target: '/sandbox/project/file.txt', boundary: '/sandbox/project' });

  const stats = measureTimingVariance(() => kernel.run(spec), { runs: 40, warmupRuns: 10 });

  // No fixed pass/fail threshold on the variance itself -- that would be
  // exactly the kind of fabricated determinism guarantee this
  // instrument exists to avoid manufacturing. What IS asserted is that
  // the instrument produces internally-consistent, sane statistics: a
  // non-negative mean, min <= mean <= max, and every field genuinely
  // populated from a real run of a real kernel.
  assert.equal(stats.runs, 40);
  assert.ok(stats.meanMs >= 0);
  assert.ok(stats.minMs <= stats.meanMs);
  assert.ok(stats.meanMs <= stats.maxMs);
  assert.ok(stats.stddevMs >= 0);
  assert.equal(stats.samplesMs.length, 40);
  assert.match(stats.honesty, /deterministic TIMING/);

  console.log(`[timingVariance real measurement] boundary-check: mean=${stats.meanMs.toFixed(4)}ms min=${stats.minMs.toFixed(4)}ms max=${stats.maxMs.toFixed(4)}ms cv=${(stats.coefficientOfVariation * 100).toFixed(2)}%`);
});

test('real measurement: consistency kernel (heavier, allocation-bearing) also reports internally-consistent statistics', async () => {
  const kernel = getKernel('consistency');
  const commitments = [
    { id: 'c1', kind: 'assert', atom: 'atomA', polarity: true, source: 'test' },
    { id: 'c2', kind: 'implies', fromAtom: 'atomA', fromPol: true, toAtom: 'atomB', toPol: true, source: 'test' },
  ];
  const spec = kernel.normalize({ commitments });

  const stats = await measureTimingVarianceAsync(async () => { await kernel.run(spec); }, { runs: 25, warmupRuns: 8 });

  assert.equal(stats.runs, 25);
  assert.ok(stats.minMs <= stats.maxMs);
  assert.ok(stats.coefficientOfVariation >= 0);
  console.log(`[timingVariance real measurement] consistency: mean=${stats.meanMs.toFixed(4)}ms min=${stats.minMs.toFixed(4)}ms max=${stats.maxMs.toFixed(4)}ms cv=${(stats.coefficientOfVariation * 100).toFixed(2)}%`);
});
