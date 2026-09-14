// liveKernelAttempt.test.mjs; proves bench/liveKernelAttempt.mjs's
// plumbing works end to end -- claim extraction -> kernel dispatch via
// the real orchestrator/worker pool -> a real result -- using INJECTED
// FAKE models, same "fakeDesignSpec stands in for the model step"
// convention orchestrator.test.mjs already uses. No network call
// anywhere in this file; see bench/LIVE-MODEL-PLUMBING.md for why this
// was never run against a real model in this session.

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  buildCodeClaimPrompt,
  normalizeCodeClaimResponse,
  attemptKernelsAgainstCode,
  candidateKernelIdsForLiveAttempt,
} from '../bench/liveKernelAttempt.mjs';

test('candidateKernelIdsForLiveAttempt only lists kernels modelDesignSpec.js actually has an adapter for', () => {
  const ids = candidateKernelIdsForLiveAttempt();
  assert.deepEqual(ids.slice().sort(), ['combinatorial', 'consistency', 'domain-of-validity', 'dynamics', 'mcmc', 'numeric-check'].sort());
  assert.ok(!ids.includes('boundary-check')); // no buildPrompt adapter -- would be a silent decline, not a real attempt
  assert.ok(!ids.includes('chain-reachability'));
});

test('normalizeCodeClaimResponse: declined is the honest default shape', () => {
  assert.deepEqual(normalizeCodeClaimResponse({ applicable: false }, ['mcmc']), { applicable: false });
});

test('normalizeCodeClaimResponse: throws on a kernelId not in the offered candidate list -- never silently accepted', () => {
  assert.throws(() => normalizeCodeClaimResponse({ applicable: true, kernelId: 'not-offered', claimText: 'x' }, ['mcmc']), /unlisted kernel/);
});

test('normalizeCodeClaimResponse: throws on applicable:true with no claimText', () => {
  assert.throws(() => normalizeCodeClaimResponse({ applicable: true, kernelId: 'mcmc' }, ['mcmc']), /gave no claimText/);
});

test('buildCodeClaimPrompt embeds the source code and the real candidate kernel list, truncated to a bounded size', () => {
  const prompt = buildCodeClaimPrompt('function f() { return 1; }', ['mcmc', 'consistency']);
  assert.match(prompt, /function f\(\)/);
  assert.match(prompt, /mcmc, consistency/);
  const huge = buildCodeClaimPrompt('x'.repeat(20000), ['mcmc']);
  assert.ok(huge.length < 15000, 'must not embed unbounded file content into a prompt');
});

// ── end-to-end via fakes: the model declines ─────────────────────────

test('a model that declines produces attempted:false, and never calls a kernel at all', async () => {
  let designSpecCalls = 0;
  const model = {
    extractClaim: async () => ({ applicable: false }),
    designSpec: async () => { designSpecCalls++; return null; },
  };
  const result = await attemptKernelsAgainstCode('some code', model, { parallel: false });
  assert.equal(result.attempted, false);
  assert.match(result.reason, /declined/);
  assert.equal(designSpecCalls, 0);
});

// ── end-to-end via fakes: the model finds something, kernel actually runs ─

test('a model that finds an applicable claim dispatches through the REAL orchestrator/kernel run and returns a real verdict', async () => {
  const model = {
    extractClaim: async (code, candidateKernelIds) => {
      assert.ok(candidateKernelIds.includes('mcmc'));
      return { applicable: true, kernelId: 'mcmc', claimText: 'x never exceeds 5 in [0,10]' };
    },
    designSpec: async (kernel) => (kernel.id === 'mcmc'
      ? { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' }
      : null),
  };
  const result = await attemptKernelsAgainstCode('function clamp(x) { return x; } // never exceeds 5', model, { parallel: false });
  assert.equal(result.attempted, true);
  assert.equal(result.kernelId, 'mcmc');
  assert.equal(result.verdict, 'violated'); // mcmc genuinely found x > 5 in [0,10] -- a REAL kernel result, not a stub
  assert.equal(result.result.verdict, 'violated');
});

test('a malformed claim-extraction response (unknown kernel) is treated as not-attempted, never crashes the caller', async () => {
  const model = {
    extractClaim: async () => ({ applicable: true, kernelId: 'eval-usage', claimText: 'nonsense' }), // not a real candidate kernel
    designSpec: async () => null,
  };
  const result = await attemptKernelsAgainstCode('code', model, { parallel: false });
  assert.equal(result.attempted, false);
  assert.match(result.reason, /unlisted kernel/);
});

test('claim extraction throwing (a real network failure, in the live case) is caught and reported, not propagated', async () => {
  const model = {
    extractClaim: async () => { throw new Error('network unreachable'); },
    designSpec: async () => null,
  };
  const result = await attemptKernelsAgainstCode('code', model, { parallel: false });
  assert.equal(result.attempted, false);
  assert.match(result.reason, /network unreachable/);
});

test('a kernel that declines the designed claim (designSpec returns null) is still reported as attempted, with why', async () => {
  const model = {
    extractClaim: async () => ({ applicable: true, kernelId: 'domain-of-validity', claimText: 'some claim' }),
    designSpec: async () => null, // domain-of-validity always declines without breakEvidence -- the real, honest behavior
  };
  const result = await attemptKernelsAgainstCode('code', model, { parallel: false });
  assert.equal(result.attempted, true);
  assert.equal(result.kernelId, 'domain-of-validity');
  assert.match(result.declined, /designSpec returned nothing/);
});
