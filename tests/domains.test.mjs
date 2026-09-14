// domains.test.mjs; every domain wrapper tested for BOTH a real
// violation found and a real clean case producing no false alarm — the
// same discipline financeRiskExample.test.mjs already proved for finance.

import test from 'node:test';
import assert from 'node:assert/strict';
import { verifyRiskMarkReconciliation, stressTestMargin, verifyRiskRankingConsistency, selectSurvivingCandidates } from '../src/domains/finance.js';
import { verifyDesignMargin, verifyDimensionalBalance } from '../src/domains/engineering.js';
import { verifyRequirementsConsistency } from '../src/domains/requirements.js';
import { stressTestResilience, verifySupplierPriorityConsistency } from '../src/domains/supplyChain.js';
import { verifyOperationalEnvelope } from '../src/domains/missionPlanning.js';
import { verifyContractConsistency, verifyComplianceClaimConsistency } from '../src/domains/legal.js';
import { verifyMotionMatchesModel, verifyObservedTrajectory, createStreamingTrajectoryVerifier } from '../src/domains/motion.js';

// ── finance ──────────────────────────────────────────────────────────

test('finance: conflicting risk marks are caught', () => {
  const findings = verifyRiskMarkReconciliation({
    quantity: 'portfolio VaR',
    markA: { value: 4.2, uncertainty: 0.3, source: 'internal' },
    markB: { value: 6.8, uncertainty: 0.4, source: 'counterparty' },
  });
  assert.equal(findings.length, 1);
});

test('finance: agreeing risk marks produce no false alarm', () => {
  const findings = verifyRiskMarkReconciliation({
    quantity: 'portfolio VaR',
    markA: { value: 5.0, uncertainty: 0.3, source: 'internal' },
    markB: { value: 5.1, uncertainty: 0.3, source: 'counterparty' },
  });
  assert.equal(findings.length, 0);
});

test('finance: a genuinely safe drawdown margin holds', () => {
  const result = stressTestMargin({
    params: [{ name: 'move', domain: [-0.1, 0.1] }, { name: 'slippage', domain: [-0.05, 0.05] }],
    objective: 'abs(move) * abs(slippage) + 0.003 - 0.02',
  });
  assert.equal(result.verdict, 'held');
});

test('finance: an impossible risk-ranking cycle is caught', () => {
  const cycles = verifyRiskRankingConsistency({
    metric: 'tail risk',
    comparisons: [
      { subject: 'strategy A', object: 'strategy B', comparator: 'greater', source: 's1' },
      { subject: 'strategy B', object: 'strategy C', comparator: 'greater', source: 's2' },
      { subject: 'strategy C', object: 'strategy A', comparator: 'greater', source: 's3' },
    ],
  });
  assert.equal(cycles.length, 1);
});

test('finance: a consistent risk ranking (no cycle) produces no false alarm', () => {
  const cycles = verifyRiskRankingConsistency({
    metric: 'tail risk',
    comparisons: [
      { subject: 'strategy A', object: 'strategy B', comparator: 'greater', source: 's1' },
      { subject: 'strategy B', object: 'strategy C', comparator: 'greater', source: 's2' },
    ],
  });
  assert.equal(cycles.length, 0);
});

test('finance: selectSurvivingCandidates independently sorts a mixed set of AI-generated candidates into survivors and falsified, each with its own concrete counterexample', () => {
  const candidates = [
    {
      id: 'strategy-1-conservative',
      note: 'claims drawdown stays under 2% for any move within +/-10%, slippage within +/-5%',
      params: [{ name: 'move', domain: [-0.10, 0.10] }, { name: 'slippage', domain: [-0.05, 0.05] }],
      objective: 'abs(move) * abs(slippage) + 0.003 - 0.02',
    },
    {
      id: 'strategy-2-overconfident',
      note: 'claims the same 0.5% drawdown bound the direct MCMC test already showed breaks',
      params: [{ name: 'move', domain: [-0.10, 0.10] }, { name: 'slippage', domain: [-0.05, 0.05] }],
      objective: 'abs(move) * abs(slippage) + 0.003 - 0.005',
    },
    {
      id: 'strategy-3-conservative',
      note: 'a second, independently-generated conservative candidate',
      params: [{ name: 'move', domain: [-0.08, 0.08] }, { name: 'slippage', domain: [-0.03, 0.03] }],
      objective: 'abs(move) * abs(slippage) + 0.002 - 0.015',
    },
  ];

  const results = selectSurvivingCandidates(candidates);
  assert.equal(results.length, 3);

  const byId = Object.fromEntries(results.map((r) => [r.id, r]));
  assert.equal(byId['strategy-1-conservative'].verdict, 'held');
  assert.equal(byId['strategy-2-overconfident'].verdict, 'violated');
  assert.ok(byId['strategy-2-overconfident'].result.bestMargin > 0, 'a falsified candidate must carry a concrete counterexample, not just a verdict');
  assert.equal(byId['strategy-3-conservative'].verdict, 'held');
});

// ── engineering ──────────────────────────────────────────────────────

test('engineering: a genuinely adequate structural margin holds', () => {
  // load never exceeds 1.0 (normalized), margin claims capacity is 1.5x -> always positive slack
  const result = verifyDesignMargin({
    params: [{ name: 'load', domain: [0, 1.0] }, { name: 'tolerance', domain: [-0.05, 0.05] }],
    objective: 'load * (1 + tolerance) - 1.5', // violated if this exceeds 0
  });
  assert.equal(result.verdict, 'held');
});

test('engineering: an inadequate margin is caught with a concrete counterexample', () => {
  const result = verifyDesignMargin({
    params: [{ name: 'load', domain: [0, 1.0] }, { name: 'tolerance', domain: [-0.05, 0.05] }],
    objective: 'load * (1 + tolerance) - 0.9', // claimed capacity too close to max load
  });
  assert.equal(result.verdict, 'violated');
  assert.ok(result.bestPoint.load !== undefined);
});

test('engineering: a dimensionally balanced equation passes', () => {
  const result = verifyDimensionalBalance({ lhs: 'F', rhs: 'm * a', assignments: { F: 'force', m: 'mass', a: 'acceleration' } });
  assert.equal(result.status, 'consistent');
});

test('engineering: a dimensionally UNBALANCED equation is caught regardless of how plausible it looks', () => {
  const result = verifyDimensionalBalance({ lhs: 'F', rhs: 'm', assignments: { F: 'force', m: 'mass' } });
  assert.equal(result.status, 'violation');
});

// ── requirements ─────────────────────────────────────────────────────

test('requirements: a program requirement set that entails a contradiction is caught', () => {
  const { findings, summary } = verifyRequirementsConsistency([
    { kind: 'implies', fromAtom: 'weight increases', fromPol: true, toAtom: 'cost increases', toPol: true, source: 'req-12: weight growth drives cost' },
    { kind: 'assert', atom: 'weight increases', polarity: true, source: 'design review: mass grew this quarter' },
    { kind: 'assert', atom: 'cost increases', polarity: false, source: 'req-3: unit cost shall not increase' },
  ]);
  assert.equal(findings.length, 1);
  assert.equal(summary.verdict, 'inconsistent');
});

test('requirements: a genuinely consistent requirement set produces no false alarm', () => {
  const { findings, summary } = verifyRequirementsConsistency([
    { kind: 'implies', fromAtom: 'weight increases', fromPol: true, toAtom: 'cost increases', toPol: true, source: 'req-12' },
    { kind: 'assert', atom: 'weight increases', polarity: false, source: 'design review: mass held flat' },
  ]);
  assert.equal(findings.length, 0);
  assert.equal(summary.verdict, 'no-contradiction-found');
});

// ── supply chain ─────────────────────────────────────────────────────

test('supply-chain: a genuinely resilient supply claim holds', () => {
  // single-supplier share never exceeds 30% (normalized 0-1); claim: resilient up to 50% loss
  const result = stressTestResilience({
    params: [{ name: 'supplierShare', domain: [0, 0.3] }],
    objective: 'supplierShare - 0.5', // violated if any supplier exceeds the 50% claimed threshold
  });
  assert.equal(result.verdict, 'held');
});

test('supply-chain: an over-concentrated supplier breaks the resilience claim, with a concrete counterexample', () => {
  const result = stressTestResilience({
    params: [{ name: 'supplierShare', domain: [0, 0.6] }], // a supplier CAN reach 60% share
    objective: 'supplierShare - 0.5', // claim: never exceeds 50%
  });
  assert.equal(result.verdict, 'violated');
  assert.ok(result.bestPoint.supplierShare > 0.5);
});

test('supply-chain: an impossible supplier-priority cycle is caught', () => {
  const cycles = verifySupplierPriorityConsistency({
    metric: 'lead time',
    comparisons: [
      { subject: 'supplier A', object: 'supplier B', comparator: 'less', source: 's1' },
      { subject: 'supplier B', object: 'supplier C', comparator: 'less', source: 's2' },
      { subject: 'supplier C', object: 'supplier A', comparator: 'less', source: 's3' },
    ],
  });
  assert.equal(cycles.length, 1);
});

test('supply-chain: a consistent supplier ranking produces no false alarm', () => {
  const cycles = verifySupplierPriorityConsistency({
    metric: 'lead time',
    comparisons: [{ subject: 'supplier A', object: 'supplier B', comparator: 'less', source: 's1' }],
  });
  assert.equal(cycles.length, 0);
});

// ── mission planning ─────────────────────────────────────────────────

test('mission-planning: a plan that genuinely covers its stated envelope holds', () => {
  // threatDelay maxes at 10, so max demand is 10/20 = 0.5; resourceSlack's
  // FLOOR is 0.6, safely above that demand everywhere in the domain.
  const result = verifyOperationalEnvelope({
    params: [{ name: 'threatDelay', domain: [0, 10] }, { name: 'resourceSlack', domain: [0.6, 1.0] }],
    objective: '(threatDelay / 20) - resourceSlack',
    note: 'plan claims resourceSlack always covers threatDelay/20 demand',
  });
  assert.equal(result.verdict, 'held');
});

test('mission-planning: a plan with a real gap in its envelope is caught with a concrete counterexample', () => {
  const result = verifyOperationalEnvelope({
    params: [{ name: 'threatDelay', domain: [0, 20] }, { name: 'resourceSlack', domain: [0.2, 1.0] }],
    objective: '(threatDelay / 20) - resourceSlack', // now threatDelay can reach 20, demanding slack=1.0 exactly, and slack can be as low as 0.2
    note: 'same claim, wider threatDelay range the plan did not actually cover',
  });
  assert.equal(result.verdict, 'violated');
});

// ── legal ────────────────────────────────────────────────────────────

test('legal: a contract with contradictory clauses is caught', () => {
  const { findings, summary } = verifyContractConsistency([
    { kind: 'implies', fromAtom: 'notice period elapses', fromPol: true, toAtom: 'contract terminates', toPol: true, source: 'clause 4.2: termination on notice' },
    { kind: 'assert', atom: 'notice period elapses', polarity: true, source: 'clause 4.1: 90-day notice given' },
    { kind: 'assert', atom: 'contract terminates', polarity: false, source: 'clause 9.3: agreement survives indefinitely' },
  ]);
  assert.equal(findings.length, 1);
  assert.equal(summary.verdict, 'inconsistent');
});

test('legal: a genuinely consistent contract produces no false alarm', () => {
  const { findings } = verifyContractConsistency([
    { kind: 'implies', fromAtom: 'notice period elapses', fromPol: true, toAtom: 'contract terminates', toPol: true, source: 'clause 4.2' },
    { kind: 'assert', atom: 'notice period elapses', polarity: false, source: 'clause 4.1: no notice given yet' },
  ]);
  assert.equal(findings.length, 0);
});

// ── the Palantir-named scenario: a hallucinated sanction status ────────

test('legal: catches the exact scenario Palantir\'s own blog names -- a hallucinated sanction status contradicting the ownership facts already on record', () => {
  const { findings, summary } = verifyComplianceClaimConsistency([
    { kind: 'universal', category: 'entity majority-owned by a sanctioned party', property: 'sanctioned', polarity: true, source: 'compliance rule 12(b): >50% ownership by a sanctioned entity confers sanctioned status' },
    { kind: 'instance', entity: 'counterparty X', category: 'entity majority-owned by a sanctioned party', source: 'ownership filing: counterparty X is 62% owned by Entity Y (sanctioned 2026-03-01)' },
    { kind: 'property', entity: 'counterparty X', property: 'sanctioned', polarity: false, source: 'RAG-retrieved compliance summary: counterparty X sanction status = clear' },
  ]);
  assert.equal(findings.length, 1);
  assert.equal(summary.verdict, 'inconsistent');
  // the derivation must actually chain through the ownership rule, not just assert a bare conflict
  assert.ok(findings[0].derived);
});

test('legal: a genuinely clear counterparty (no contradicting ownership fact) produces no false alarm', () => {
  const { findings } = verifyComplianceClaimConsistency([
    { kind: 'universal', category: 'entity majority-owned by a sanctioned party', property: 'sanctioned', polarity: true, source: 'compliance rule 12(b)' },
    { kind: 'property', entity: 'counterparty Z', property: 'sanctioned', polarity: false, source: 'compliance summary: counterparty Z sanction status = clear' },
    // no 'instance' commitment placing counterparty Z under the sanctioned-ownership category -- nothing to derive a conflict from
  ]);
  assert.equal(findings.length, 0);
});

// ── motion (trajectory verification, NOT video motion detection) ───────

test('motion: an undamped oscillator matches its own undamped reference model (c=0)', () => {
  const result = verifyMotionMatchesModel({
    stateA: ['x', 'v'], derivA: ['v', '-x'],
    stateB: ['x2', 'v2'], derivB: ['v2', '-x2 - c*v2'],
    initA: [{ range: [1, 1] }, { range: [0, 0] }],
    initB: ['x', 'v'],
    compareA: ['x'], compareB: ['x2'],
    params: { c: 0 },
    T: 10, dt: 0.01, tolerance: 0.05,
  });
  assert.equal(result.verdict, 'matched');
});

test('motion: a genuinely damped trajectory is caught diverging from the undamped reference model', () => {
  const result = verifyMotionMatchesModel({
    stateA: ['x', 'v'], derivA: ['v', '-x'],
    stateB: ['x2', 'v2'], derivB: ['v2', '-x2 - c*v2'],
    initA: [{ range: [1, 1] }, { range: [0, 0] }],
    initB: ['x', 'v'],
    compareA: ['x'], compareB: ['x2'],
    params: { c: 0.5 }, // real damping -- trajectory genuinely diverges from the undamped claim
    T: 10, dt: 0.01, tolerance: 0.05,
  });
  assert.equal(result.verdict, 'diverged');
});

test('motion: externally-supplied observed samples that genuinely match a claimed model produce no false alarm', () => {
  // model: position = 5*t (constant velocity 5); observed samples close to that line
  const result = verifyObservedTrajectory({
    modelExpr: '5 * t',
    observedSamples: [{ t: 0, value: 0.01 }, { t: 1, value: 4.98 }, { t: 2, value: 10.03 }, { t: 3, value: 14.97 }],
    tolerance: 0.1,
  });
  assert.equal(result.verdict, 'matched');
});

test('motion: an observed sample that genuinely breaks the claimed model is caught with the exact breaking point', () => {
  const result = verifyObservedTrajectory({
    modelExpr: '5 * t',
    observedSamples: [{ t: 0, value: 0.01 }, { t: 1, value: 4.98 }, { t: 2, value: 25 }, { t: 3, value: 14.97 }], // t=2 is way off the claimed line
    tolerance: 0.1,
  });
  assert.equal(result.verdict, 'diverged');
  assert.equal(result.worst.t, 2);
  assert.ok(result.worst.residual > 0.1);
});

test('motion: verifyObservedTrajectory rejects empty samples and non-positive tolerance', () => {
  assert.throws(() => verifyObservedTrajectory({ modelExpr: 't', observedSamples: [], tolerance: 1 }), /non-empty array/);
  assert.throws(() => verifyObservedTrajectory({ modelExpr: 't', observedSamples: [{ t: 0, value: 0 }], tolerance: 0 }), /positive number/);
});

// ── streaming / real-time variant ───────────────────────────────────────

test('streaming verifier: each observe() returns its own verdict immediately, matching the batch result sample-by-sample', () => {
  const stream = createStreamingTrajectoryVerifier({ modelExpr: '5 * t', tolerance: 0.1 });
  const r1 = stream.observe({ t: 0, value: 0.01 });
  const r2 = stream.observe({ t: 1, value: 4.98 });
  assert.equal(r1.verdict, 'held');
  assert.equal(r2.verdict, 'held');
  assert.equal(stream.status().verdict, 'matched');
  assert.equal(stream.status().sampleCount, 2);
});

test('streaming verifier: a real-time divergence is caught the moment it arrives, not after the fact', () => {
  const stream = createStreamingTrajectoryVerifier({ modelExpr: '5 * t', tolerance: 0.1 });
  stream.observe({ t: 0, value: 0.01 });
  const divergingResult = stream.observe({ t: 1, value: 25 }); // way off
  assert.equal(divergingResult.verdict, 'diverged');
  assert.equal(stream.status().verdict, 'diverged');
});

test('streaming verifier: once diverged, a LATER matching sample does not erase the earlier divergence', () => {
  const stream = createStreamingTrajectoryVerifier({ modelExpr: '5 * t', tolerance: 0.1 });
  stream.observe({ t: 0, value: 25 }); // diverges immediately
  const laterGood = stream.observe({ t: 1, value: 5.0 }); // this one is fine on its own
  assert.equal(laterGood.verdict, 'held', 'each sample is still scored on its own merits');
  assert.equal(stream.status().verdict, 'diverged', 'but cumulative status stays diverged -- an earlier concrete finding is not un-found');
});

test('streaming verifier rejects non-positive tolerance up front', () => {
  assert.throws(() => createStreamingTrajectoryVerifier({ modelExpr: 't', tolerance: 0 }), /positive number/);
});
