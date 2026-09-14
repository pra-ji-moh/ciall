// orchestratorParallel.test.mjs; upgrade 1 coverage — parallel kernel
// execution via kernelWorkerPool.js must produce a report identical to
// the sequential path, and must actually be faster for real concurrent
// workloads.

import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';

import { runPipeline } from '../src/lib/orchestrator.js';
import { shutdownPool } from '../src/lib/kernelWorkerPool.js';

// Same fake designSpec orchestrator.test.mjs uses: stands in for the
// model step this substrate deliberately does not own.
function fakeDesignSpec(kernel, claim) {
  if (kernel.id === 'mcmc') {
    return { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: claim.mcmcObjective || 'x - 5' };
  }
  if (kernel.id === 'consistency') {
    return { commitments: claim.commitments || [] };
  }
  return null;
}

after(() => shutdownPool());

// Every scenario runs through both paths and must produce the exact same
// report — this is the "bit-for-bit identical" correctness requirement,
// not just "same verdict."
const SCENARIOS = [
  {
    name: 'mcmc finds a violation',
    claim: { text: 'x never exceeds 5 in [0,10]', mcmcObjective: 'x - 5' },
    opts: { kernelIds: ['mcmc', 'numeric-check'], designSpec: fakeDesignSpec },
  },
  {
    name: 'mcmc holds',
    claim: { text: 'x never exceeds 50 in [0,10]', mcmcObjective: 'x - 50' },
    opts: { kernelIds: ['mcmc'], designSpec: fakeDesignSpec },
  },
  {
    name: 'consistency finds a contradiction',
    claim: {
      text: 'contradictory claim',
      commitments: [
        { kind: 'assert', atom: 'growth is slowing', polarity: true, source: 'a' },
        { kind: 'assert', atom: 'growth is slowing', polarity: false, source: 'b' },
      ],
    },
    opts: { kernelIds: ['consistency'], designSpec: fakeDesignSpec },
  },
  {
    name: 'a spec-design throw is recorded the same way in both paths',
    claim: { text: 'anything' },
    opts: { kernelIds: ['mcmc'], designSpec: () => { throw new Error('model call failed'); } },
  },
  {
    name: 'a malformed spec is recorded as failed the same way in both paths',
    claim: { text: 'anything' },
    opts: { kernelIds: ['mcmc'], designSpec: () => ({ kind: 'mcmc_search', params: [], objective: 'x' }) },
  },
  {
    name: 'default kernel set (mixed ran/skipped) matches across both paths',
    claim: { text: 'x never exceeds 5 in [0,10]', mcmcObjective: 'x - 5' },
    opts: { designSpec: fakeDesignSpec },
  },
];

for (const { name, claim, opts } of SCENARIOS) {
  test(`parallel matches sequential: ${name}`, async () => {
    const sequential = await runPipeline(claim, { ...opts, parallel: false });
    const parallel = await runPipeline(claim, { ...opts, parallel: true });
    assert.deepStrictEqual(parallel, sequential);
  });
}

test('parallel is the default (omitting the flag behaves like parallel: true)', async () => {
  const claim = { text: 'x never exceeds 5 in [0,10]', mcmcObjective: 'x - 5' };
  const withDefault = await runPipeline(claim, { kernelIds: ['mcmc'], designSpec: fakeDesignSpec });
  const explicitParallel = await runPipeline(claim, { kernelIds: ['mcmc'], designSpec: fakeDesignSpec, parallel: true });
  assert.deepStrictEqual(withDefault, explicitParallel);
});

// A real, controllable-cost workload: 'identity' mode always runs the
// full fixed SAMPLES=4000 evaluations (no early break on violations,
// unlike integer_search), each one a couple of trig calls through
// mathExpr's interpreter — enough real CPU work per kernel that a
// 4-wide worker pool should measurably beat running the same four
// invocations back to back on one thread.
test('parallel orchestrator run beats sequential wall-clock for real concurrent workloads', async () => {
  const heavySpec = {
    kind: 'identity',
    note: 'benchmark workload',
    lhs: 'sin(x)^2 + cos(x)^2',
    rhs: '1',
    vars: [{ name: 'x', domain: [-1000, 1000] }],
  };
  const claim = { text: 'benchmark' };
  const kernelIds = ['numeric-check', 'numeric-check', 'numeric-check', 'numeric-check'];
  const designSpec = () => heavySpec;

  const seqStart = performance.now();
  await runPipeline(claim, { kernelIds, designSpec, parallel: false });
  const sequentialMs = performance.now() - seqStart;

  // Warm dispatch first so pool spin-up cost doesn't get counted against
  // the parallel path's very first measurement.
  await runPipeline(claim, { kernelIds, designSpec, parallel: true });

  const parStart = performance.now();
  await runPipeline(claim, { kernelIds, designSpec, parallel: true });
  const parallelMs = performance.now() - parStart;

  assert.ok(
    parallelMs < sequentialMs,
    `expected parallel run (${parallelMs.toFixed(1)}ms) to beat sequential (${sequentialMs.toFixed(1)}ms)`,
  );
});
