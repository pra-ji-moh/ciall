// modelDesignSpec.test.mjs; injects a fake callJSON so these tests never
// touch the network or modelClient.js at all — pure routing logic.

import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { makeLiveDesignSpec } from '../src/lib/modelDesignSpec.js';
import { getKernel } from '../src/lib/kernelRegistry.js';
import { shutdownPool } from '../src/lib/kernelWorkerPool.js';

// The orchestrator.runPipeline test below runs with parallel:true (the
// default since upgrade 1), which starts kernelWorkerPool.js's worker
// pool; it must be torn down explicitly for this file's process to exit.
after(() => shutdownPool());

function fakeCallJSON(returnValue) {
  const calls = [];
  const fn = async (apiKey, prompt, opts) => { calls.push({ apiKey, prompt, opts }); return { result: returnValue }; };
  fn.calls = calls;
  return fn;
}

test('mcmc/numeric-check/dynamics get a {text, reasoning} node built from the claim', async () => {
  const callJSON = fakeCallJSON({ kind: 'none', reason: 'stub' });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  const claim = { text: 'x never exceeds 5', reasoning: 'because it is bounded' };

  for (const id of ['mcmc', 'numeric-check', 'dynamics']) {
    const kernel = getKernel(id);
    const result = await designSpec(kernel, claim);
    assert.deepEqual(result, { kind: 'none', reason: 'stub' });
  }
  assert.equal(callJSON.calls.length, 3);
  for (const call of callJSON.calls) {
    assert.match(call.prompt, /x never exceeds 5/);
    assert.match(call.prompt, /because it is bounded/);
  }
});

test('combinatorial additionally threads sourceExcerpt into the prompt', async () => {
  const callJSON = fakeCallJSON({ kind: 'none', reason: 'stub' });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  const kernel = getKernel('combinatorial');
  await designSpec(kernel, { text: 'no such coloring exists', sourceExcerpt: 'EXCERPT_MARKER' });
  assert.match(callJSON.calls[0].prompt, /EXCERPT_MARKER/);
});

test('consistency is called with claimText/coreClaim/siblingClaims, not a node object', async () => {
  const callJSON = fakeCallJSON({ commitments: [] });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  const kernel = getKernel('consistency');
  const result = await designSpec(kernel, { text: 'growth is slowing', coreClaim: 'the business is declining', siblingClaims: ['churn is rising'] });
  assert.deepEqual(result, { commitments: [] });
  assert.match(callJSON.calls[0].prompt, /growth is slowing/);
  assert.match(callJSON.calls[0].prompt, /churn is rising/);
});

test('domain-of-validity declines (returns null, no call made) without breakEvidence', async () => {
  const callJSON = fakeCallJSON({ verdict: 'narrowed' });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  const kernel = getKernel('domain-of-validity');
  const result = await designSpec(kernel, { text: 'the claim' }); // no breakEvidence
  assert.equal(result, null);
  assert.equal(callJSON.calls.length, 0);
});

test('domain-of-validity calls the model when breakEvidence IS supplied', async () => {
  const callJSON = fakeCallJSON({ verdict: 'narrowed' });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  const kernel = getKernel('domain-of-validity');
  const result = await designSpec(kernel, { text: 'the claim', breakEvidence: 'it failed at x=11' });
  assert.deepEqual(result, { verdict: 'narrowed' });
  assert.match(callJSON.calls[0].prompt, /it failed at x=11/);
});

test('kernels with no buildPrompt (order-consistency, boundary-check) return null without calling the model', async () => {
  const callJSON = fakeCallJSON({ should: 'never be returned' });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  for (const id of ['order-consistency', 'boundary-check']) {
    const result = await designSpec(getKernel(id), { text: 'anything' });
    assert.equal(result, null);
  }
  assert.equal(callJSON.calls.length, 0);
});

// ── provider routing, WITHOUT injecting callJSON — proves the real
// dynamic import in modelDesignSpec.js picks the right module. Uses an
// empty apiKey so both clients throw their NoApiKeyError synchronously,
// BEFORE any network call, which is exactly what the guard in each
// client is for — this reaches that guard through the real import path.

test('with no callJSON injected, defaults to Gemini and reaches geminiClient\'s own key guard', async () => {
  const designSpec = makeLiveDesignSpec('');
  await assert.rejects(() => designSpec(getKernel('numeric-check'), { text: 'x >= 0' }), /GEMINI_API_KEY/);
});

test('provider: "anthropic" with no callJSON routes to modelClient and reaches ITS key guard instead', async () => {
  const designSpec = makeLiveDesignSpec('', { provider: 'anthropic' });
  await assert.rejects(() => designSpec(getKernel('numeric-check'), { text: 'x >= 0' }), /ANTHROPIC_API_KEY/);
});

test('this whole adapter can drive orchestrator.runPipeline end to end with zero real network calls', async () => {
  const { runPipeline } = await import('../src/lib/orchestrator.js');
  const callJSON = fakeCallJSON({ kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' });
  const designSpec = makeLiveDesignSpec('sk-ant-fake', { callJSON });
  const report = await runPipeline({ text: 'x never exceeds 5 in [0,10]' }, { kernelIds: ['mcmc'], designSpec });
  assert.equal(report.ran[0].verdict, 'violated'); // x CAN exceed 5 in [0,10]
});
