// iterativeVerify.test.mjs; coverage for the round-based verification
// loop. Every test injects a FAKE, deterministic {designSpec, reflect}
// pair (same "fakeDesignSpec stands in for the model step" convention
// orchestrator.test.mjs already uses) -- no live model call anywhere in
// this file. `parallel:false` throughout: this file tests the LOOP's
// control flow, not orchestrator.js's worker-pool dispatch, which
// already has its own dedicated tests.

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  runIterativeVerification,
  normalizeReflection,
  scanForRedFlags,
  buildReflectionPrompt,
  DEFAULT_MAX_ROUNDS,
  HARD_MAX_ROUNDS,
} from '../src/lib/iterativeVerify.js';

// ── a case that resolves cleanly in round 1 ──────────────────────────

test('round 1 finds a real result and round 2 accepts it -- confirmed, backed by an actual kernel run', async () => {
  const designSpec = (kernel) => (kernel.id === 'mcmc' ? { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' } : null);
  let reflectCalls = 0;
  const reflect = async ({ history }) => {
    reflectCalls++;
    assert.equal(history.length, 1); // sees exactly round 1's outcome
    assert.equal(history[0].kernelId, 'mcmc');
    assert.equal(history[0].verdict, 'violated'); // x can exceed 5 in [0,10]
    return { decision: 'accept', rationale: 'mcmc already found a real violation over the full stated domain' };
  };

  const result = await runIterativeVerification(
    { text: 'x never exceeds 5 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], parallel: false }
  );

  assert.equal(result.verdict, 'confirmed');
  assert.equal(reflectCalls, 1);
  assert.equal(result.rounds.length, 2);
  assert.equal(result.rounds[0].action, 'kernel-check');
  assert.equal(result.rounds[1].action, 'accept');
  assert.equal(result.finalKernelId, 'mcmc');
  assert.equal(result.finalResult.verdict, 'violated');
});

// ── self-correction: round 1 too narrow, round 2 uses a DIFFERENT kernel ─

test('round 1 (domain-of-validity) is inconclusive; round 2 self-corrects with mcmc -- a genuinely new deterministic check, not louder confidence in the same one', async () => {
  const designSpec = (kernel, claim) => {
    if (kernel.id === 'domain-of-validity') {
      return { boundary: '', restricted: '', excluded: [], motivatingCaseSurvives: false, insight: '', verdict: 'unresolved' };
    }
    if (kernel.id === 'mcmc') {
      assert.match(claim.reasoning || '', /domain-of-validity found no boundary/); // the revise rationale actually reached the next designSpec call
      return { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' };
    }
    return null;
  };

  const reflect = async ({ history }) => {
    if (history.length === 1) {
      assert.equal(history[0].kernelId, 'domain-of-validity');
      return { decision: 'revise', kernelId: 'mcmc', rationale: 'domain-of-validity found no boundary at all -- need a real numeric search over the full domain instead' };
    }
    assert.equal(history.length, 2);
    assert.equal(history[1].kernelId, 'mcmc');
    assert.equal(history[1].verdict, 'violated');
    return { decision: 'accept', rationale: 'mcmc settled it with a real counterexample' };
  };

  const result = await runIterativeVerification(
    { text: 'x never exceeds 5 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['domain-of-validity'], parallel: false }
  );

  assert.equal(result.verdict, 'confirmed');
  assert.equal(result.rounds.length, 3);
  assert.equal(result.rounds[0].kernelId, 'domain-of-validity');
  assert.equal(result.rounds[1].kernelId, 'mcmc');
  assert.notEqual(result.rounds[0].kernelId, result.rounds[1].kernelId); // a genuinely different check, not a repeat
  assert.equal(result.rounds[2].action, 'accept');
  assert.equal(result.finalKernelId, 'mcmc');
});

// ── hard stop: max rounds reached ────────────────────────────────────

test('the model revising forever without ever accepting hits the round cap and reports unconfirmed, never a silent escalation to confirmed', async () => {
  const designSpec = () => ({ kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 50' }); // always "held" -- never settles either way on its own
  const reflect = async () => ({ decision: 'revise', kernelId: 'mcmc', rationale: 'not satisfied yet, try again' });

  const result = await runIterativeVerification(
    { text: 'x never exceeds 50 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], maxRounds: 3, parallel: false }
  );

  assert.equal(result.verdict, 'unconfirmed-max-rounds');
  assert.equal(result.rounds.length, 3); // round 1 + two revise rounds, then the cap
  assert.ok(result.rounds.every((r) => r.action === 'kernel-check'));
});

test('maxRounds is clamped to HARD_MAX_ROUNDS regardless of what a caller passes', async () => {
  const designSpec = () => ({ kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 50' });
  const reflect = async () => ({ decision: 'revise', kernelId: 'mcmc', rationale: 'keep going' });
  const result = await runIterativeVerification(
    { text: 'anything' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], maxRounds: 999999, parallel: false }
  );
  assert.equal(result.verdict, 'unconfirmed-max-rounds');
  assert.equal(result.rounds.length, HARD_MAX_ROUNDS);
});

// ── act: stops the loop, never executes anything ─────────────────────

test('a round proposing a real action stops the loop immediately -- no execution, no confirm() call, just a reported proposal', async () => {
  const designSpec = (kernel) => (kernel.id === 'mcmc' ? { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' } : null);
  const reflect = async () => ({
    decision: 'act',
    actionProposal: { kind: 'write-file', detail: 'record the confirmed violation in notes/finding.md' },
    rationale: 'the violation is confirmed, time to record it',
  });

  const result = await runIterativeVerification(
    { text: 'x never exceeds 5 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], parallel: false }
  );

  assert.equal(result.verdict, 'awaiting-human-action');
  assert.deepEqual(result.proposedAction, { kind: 'write-file', detail: 'record the confirmed violation in notes/finding.md' });
  assert.equal(result.rounds.length, 2); // the kernel-check, then the act decision -- nothing after
});

// ── red flags: a model response tripping selfAudit's rules terminates ─

test('a reflection response containing a red-flag pattern (e.g. eval() in its own rationale) terminates the loop for human review, never continues', async () => {
  const designSpec = (kernel) => (kernel.id === 'mcmc' ? { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' } : null);
  const reflect = async () => ({
    decision: 'accept',
    rationale: 'looks fine -- similar to how eval(userSuppliedExpression) would resolve this dynamically',
  });

  const result = await runIterativeVerification(
    { text: 'x never exceeds 5 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], parallel: false }
  );

  assert.equal(result.verdict, 'terminated-red-flag');
  assert.notEqual(result.verdict, 'confirmed');
  const flaggedRound = result.rounds.find((r) => r.redFlags);
  assert.ok(flaggedRound);
  assert.ok(flaggedRound.redFlags.some((f) => f.code === 'eval-usage'));
});

test('a claim whose enriched reasoning (from a prior revise rationale) trips a red flag is also caught, before the next kernel run', async () => {
  const designSpec = (kernel) => (kernel.id === 'mcmc' ? { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' } : null);
  let round = 0;
  const reflect = async () => {
    round++;
    if (round === 1) {
      return { decision: 'revise', kernelId: 'mcmc', rationale: 'try again, maybe using new Function("return 1") as a shortcut' };
    }
    return { decision: 'accept', rationale: 'fine' };
  };

  const result = await runIterativeVerification(
    { text: 'x never exceeds 5 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], parallel: false }
  );

  assert.equal(result.verdict, 'terminated-red-flag');
});

// ── accept with nothing backing it is overridden, not trusted ────────

test('the model saying "accept" when no round has ever produced a real result is overridden -- never confirmed on opinion alone', async () => {
  const designSpec = () => null; // every kernel declines this claim, every round
  const reflect = async () => ({ decision: 'accept', rationale: 'I am confident this is fine' });

  const result = await runIterativeVerification(
    { text: 'a claim no kernel here can evaluate' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], maxRounds: 2, parallel: false }
  );

  assert.notEqual(result.verdict, 'confirmed');
  assert.equal(result.verdict, 'unconfirmed-max-rounds');
  assert.ok(result.rounds.some((r) => r.action === 'accept-overridden'));
});

// ── malformed / invalid revisions consume a round rather than looping forever ─

test('a revise naming an unknown kernel id is recorded and consumes a round, not retried for free', async () => {
  const designSpec = (kernel) => (kernel.id === 'mcmc' ? { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' } : null);
  const reflect = async ({ history }) => (history.length === 1
    ? { decision: 'revise', kernelId: 'not-a-real-kernel', rationale: 'try something else' }
    : { decision: 'accept', rationale: 'giving up gracefully' });

  const result = await runIterativeVerification(
    { text: 'x never exceeds 5 in [0,10]' },
    { model: { designSpec, reflect }, candidateKernelIds: ['mcmc'], maxRounds: 3, parallel: false }
  );

  assert.ok(result.rounds.some((r) => r.action === 'revise-invalid-kernel'));
});

// ── normalizeReflection ────────────────────────────────────────────────

test('normalizeReflection throws on an unknown decision, and requires kernelId for revise', () => {
  assert.throws(() => normalizeReflection({ decision: 'maybe' }), /unknown reflection decision/);
  assert.throws(() => normalizeReflection({ decision: 'revise' }), /needs a non-empty kernelId/);
  assert.deepEqual(normalizeReflection({ decision: 'accept', rationale: 'ok' }), { decision: 'accept', rationale: 'ok' });
  assert.deepEqual(normalizeReflection({ decision: 'revise', kernelId: 'mcmc', rationale: 'r' }), { decision: 'revise', kernelId: 'mcmc', rationale: 'r' });
});

// ── scanForRedFlags / buildReflectionPrompt ──────────────────────────

test('scanForRedFlags finds a real selfAudit finding in arbitrary text, with no whitelist exceptions', () => {
  const findings = scanForRedFlags('const x = eval("1+1");');
  assert.ok(findings.some((f) => f.code === 'eval-usage'));
  assert.ok(findings.every((f) => f.file === undefined)); // no file field -- this is not a repo scan
});

test('scanForRedFlags is silent on ordinary, benign text', () => {
  assert.deepEqual(scanForRedFlags({ decision: 'accept', rationale: 'the mcmc search found a clean counterexample' }), []);
});

test('buildReflectionPrompt includes the claim text and every round summarized, and lists real kernel ids from the registry', () => {
  const prompt = buildReflectionPrompt({ text: 'CLAIM TEXT HERE' }, [{ round: 1, kernelId: 'mcmc', verdict: 'held', result: { verdict: 'held' } }]);
  assert.match(prompt, /CLAIM TEXT HERE/);
  assert.match(prompt, /kernel="mcmc"/);
  assert.match(prompt, /consistency/); // a real registered kernel id, not invented
});

// ── setup validation ───────────────────────────────────────────────────

test('runIterativeVerification throws on a missing model, rather than silently doing nothing', async () => {
  await assert.rejects(() => runIterativeVerification({ text: 'x' }, {}), /needs opts\.model/);
});

test('runIterativeVerification throws on a claim with no text', async () => {
  await assert.rejects(() => runIterativeVerification({}, { model: { designSpec: () => null, reflect: async () => ({ decision: 'accept' }) } }), /non-empty text/);
});

test('DEFAULT_MAX_ROUNDS is a small, sane bound', () => {
  assert.ok(DEFAULT_MAX_ROUNDS >= 1 && DEFAULT_MAX_ROUNDS <= HARD_MAX_ROUNDS);
});
