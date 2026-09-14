// orchestrator.test.mjs; Phase 2 coverage.

import test, { after } from 'node:test';
import assert from 'node:assert/strict';

import { runPipeline, summarizePipeline } from '../src/lib/orchestrator.js';
import { shutdownPool } from '../src/lib/kernelWorkerPool.js';

// runPipeline defaults to parallel:true since upgrade 1, which starts
// kernelWorkerPool.js's worker pool; it must be torn down explicitly for
// this file's process to exit (see kernelWorkerPool.js's SHUTDOWN note).
after(() => shutdownPool());

// A fake designSpec standing in for the model step this substrate
// deliberately does not own (see orchestrator.js header).
function fakeDesignSpec(kernel, claim) {
  if (kernel.id === 'mcmc') {
    return { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: claim.mcmcObjective || 'x - 5' };
  }
  if (kernel.id === 'consistency') {
    return { commitments: claim.commitments || [] };
  }
  return null; // every other kernel declines this claim in these tests
}

test('runPipeline runs only the requested kernels and aggregates verdicts', async () => {
  const claim = { text: 'x never exceeds 5 in [0,10]', mcmcObjective: 'x - 5' };
  const report = await runPipeline(claim, { kernelIds: ['mcmc', 'numeric-check'], designSpec: fakeDesignSpec });
  assert.equal(report.ran.length, 1); // mcmc ran
  assert.equal(report.skipped.length, 1); // numeric-check declined (fakeDesignSpec returns null)
  assert.equal(report.ran[0].kernelId, 'mcmc');
  assert.equal(report.ran[0].verdict, 'violated'); // x can reach above 5 in [0,10]
  assert.equal(report.anyViolated, true);
});

test('runPipeline reports held when no kernel finds a violation', async () => {
  const claim = { text: 'x never exceeds 50 in [0,10]', mcmcObjective: 'x - 50' };
  const report = await runPipeline(claim, { kernelIds: ['mcmc'], designSpec: fakeDesignSpec });
  assert.equal(report.ran[0].verdict, 'held');
  assert.equal(report.anyViolated, false);
});

test('runPipeline treats a non-empty consistency finding as violated', async () => {
  const claim = {
    text: 'contradictory claim',
    commitments: [
      { kind: 'assert', atom: 'growth is slowing', polarity: true, source: 'a' },
      { kind: 'assert', atom: 'growth is slowing', polarity: false, source: 'b' },
    ],
  };
  const report = await runPipeline(claim, { kernelIds: ['consistency'], designSpec: fakeDesignSpec });
  assert.equal(report.ran[0].verdict, 'violated');
  assert.equal(report.anyViolated, true);
});

test('a kernel whose spec design throws is recorded as skipped, not silently dropped', async () => {
  const claim = { text: 'anything' };
  const throwingDesign = () => { throw new Error('model call failed'); };
  const report = await runPipeline(claim, { kernelIds: ['mcmc'], designSpec: throwingDesign });
  assert.equal(report.ran.length, 0);
  assert.equal(report.skipped.length, 1);
  assert.match(report.skipped[0].reason, /model call failed/);
});

test('a malformed spec is recorded as failed, not thrown out of runPipeline', async () => {
  const claim = { text: 'anything' };
  const badSpec = () => ({ kind: 'mcmc_search', params: [], objective: 'x' }); // no params: normalize should throw
  const report = await runPipeline(claim, { kernelIds: ['mcmc'], designSpec: badSpec });
  assert.equal(report.ran.length, 0);
  assert.equal(report.failed.length, 1);
});

test('runPipeline defaults to every registered kernel when neither kernelIds nor domain is given', async () => {
  const claim = { text: 'x never exceeds 5 in [0,10]', mcmcObjective: 'x - 5' };
  const report = await runPipeline(claim, { designSpec: fakeDesignSpec });
  const total = report.ran.length + report.skipped.length + report.failed.length;
  assert.equal(total, 13); // all thirteen registered kernels were considered
});

test('summarizePipeline never claims something is "safe" or "verified"', async () => {
  const claim = { text: 'x never exceeds 50 in [0,10]', mcmcObjective: 'x - 50' };
  const report = await runPipeline(claim, { kernelIds: ['mcmc'], designSpec: fakeDesignSpec });
  const summary = summarizePipeline(report);
  assert.ok(!/\bsafe\b/i.test(summary));
  assert.ok(!/\bverified\b/i.test(summary));
});
