// decisionHelper.test.mjs; upgrade 15 — validates the probabilistic
// decision-support engine: reproducibility (seeded sampling must be
// bit-for-bit identical across runs, same discipline as mcmc), a
// branch with a known-bad parameter region is correctly ranked below
// a genuinely safe one, and every fault point reported is independently
// re-checked against the real compiled objective (not just trusted).

import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizeDecisionSpec, evaluateBranch, evaluateDecision } from '../src/lib/decisionHelper.js';
import { compileExpr } from '../src/lib/mathExpr.js';

test('normalize rejects fewer than 2 branches', () => {
  assert.throws(() => normalizeDecisionSpec({ branches: [{ id: 'a', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' }] }), /at least 2 branches/);
});

test('normalize rejects a duplicate branch id', () => {
  const raw = { branches: [
    { id: 'a', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' },
    { id: 'a', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' },
  ] };
  assert.throws(() => normalizeDecisionSpec(raw), /duplicate branch id/);
});

test('normalize rejects a branch with an invalid param domain', () => {
  const raw = { branches: [
    { id: 'a', params: [{ name: 'x', domain: [5, 1] }], objective: 'x' },
    { id: 'b', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' },
  ] };
  assert.throws(() => normalizeDecisionSpec(raw), /finite domain \[lo, hi\] with lo < hi/);
});

test('normalize fails loudly on a malformed objective expression BEFORE any sampling', () => {
  const raw = { branches: [
    { id: 'a', params: [{ name: 'x', domain: [0, 1] }], objective: 'x +' },
    { id: 'b', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' },
  ] };
  assert.throws(() => normalizeDecisionSpec(raw));
});

test('evaluateBranch is bit-for-bit reproducible across two calls with the same seed', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'safe', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - (-5)' },
      { id: 'risky', params: [{ name: 'x', domain: [-10, 10] }], objective: 'x' },
    ],
    samples: 500,
  });
  const r1 = evaluateBranch(spec.branches[1], { samples: spec.samples, seed: spec.seed });
  const r2 = evaluateBranch(spec.branches[1], { samples: spec.samples, seed: spec.seed });
  assert.deepEqual(r1, r2);
});

test('a branch whose objective always holds across its entire domain has zero estimated failure probability', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'always-safe', params: [{ name: 'x', domain: [0, 10] }], objective: 'x + 1000' }, // always positive
      { id: 'other', params: [{ name: 'x', domain: [0, 10] }], objective: 'x' },
    ],
    samples: 1000,
  });
  const result = evaluateBranch(spec.branches[0], { samples: spec.samples, seed: spec.seed });
  assert.equal(result.failureProbability, 0);
  assert.equal(result.successProbability, 1);
});

test('a branch whose objective always violates across its entire domain has failure probability 1', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'always-fails', params: [{ name: 'x', domain: [0, 10] }], objective: '0 - x - 1000' }, // always negative
      { id: 'other', params: [{ name: 'x', domain: [0, 10] }], objective: 'x' },
    ],
    samples: 1000,
  });
  const result = evaluateBranch(spec.branches[0], { samples: spec.samples, seed: spec.seed });
  assert.equal(result.failureProbability, 1);
  assert.ok(result.faultPoint);
});

test('the reported fault point is REAL: independently re-evaluating the compiled objective at that exact point reproduces the same margin', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'a', params: [{ name: 'x', domain: [-50, 50] }, { name: 'y', domain: [-50, 50] }], objective: '100 - x*x - y*y' }, // fails outside a circle of radius 10
      { id: 'b', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' },
    ],
    samples: 3000,
  });
  const result = evaluateBranch(spec.branches[0], { samples: spec.samples, seed: spec.seed });
  assert.ok(result.faultPoint, 'expected at least one failing sample across a [-50,50]x[-50,50] domain for a radius-10 safe region');
  const recompiled = compileExpr(spec.branches[0].objective, ['x', 'y']);
  const recomputedMargin = recompiled(result.faultPoint.point);
  assert.ok(Math.abs(recomputedMargin - result.faultPoint.margin) < 1e-9);
});

test('evaluateDecision ranks a genuinely safer branch above a genuinely riskier one', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'wide-margin', label: 'Wide safety margin', params: [{ name: 'load', domain: [0, 100] }], objective: '1000 - load' }, // never fails
      { id: 'tight-margin', label: 'Tight, risky margin', params: [{ name: 'load', domain: [0, 100] }], objective: '50 - load' }, // fails for load > 50, half the domain
    ],
    samples: 5000,
  });
  const result = evaluateDecision(spec);
  assert.equal(result.ranking[0], 'wide-margin');
  assert.equal(result.recommendation.branchId, 'wide-margin');
  const tight = result.branches.find((b) => b.branchId === 'tight-margin');
  assert.ok(tight.failureProbability > 0.3 && tight.failureProbability < 0.7, `expected roughly 50% failure rate for a half-domain violation, got ${tight.failureProbability}`);
});

test('evaluateDecision never returns an instruction to act -- recommendation is a finding with a reason, and an explicit honesty disclosure is always present', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'a', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' },
      { id: 'b', params: [{ name: 'x', domain: [0, 1] }], objective: 'x + 1' },
    ],
  });
  const result = evaluateDecision(spec);
  assert.match(result.recommendation.reason, /highest estimated success probability/);
  assert.match(result.honesty, /not a proof, not a guarantee, and not a decision/);
});

test('evaluateDecision is bit-for-bit reproducible end to end across two full runs of the same spec', () => {
  const spec = normalizeDecisionSpec({
    branches: [
      { id: 'a', params: [{ name: 'x', domain: [0, 10] }, { name: 'y', domain: [-5, 5] }], objective: 'x - y' },
      { id: 'b', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 3' },
      { id: 'c', params: [{ name: 'x', domain: [-10, 10] }], objective: 'x' },
    ],
    samples: 800,
  });
  assert.deepEqual(evaluateDecision(spec), evaluateDecision(spec));
});

test('samples is capped at MAX_SAMPLES and branches at MAX_BRANCHES rather than accepting unbounded input', () => {
  const manyBranches = new Array(25).fill(0).map((_, i) => ({ id: `b${i}`, params: [{ name: 'x', domain: [0, 1] }], objective: 'x' }));
  const spec = normalizeDecisionSpec({ branches: manyBranches, samples: 999999 });
  assert.equal(spec.branches.length, 20);
  assert.equal(spec.samples, 20000);
});
