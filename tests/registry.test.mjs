// registry.test.mjs; smoke coverage for the extracted substrate. Not a
// port of client-backend-fixed's full kernel.test.mjs (that file mixes in
// a large amount of app-specific machinery that does not live here) — this
// checks that each kernel still behaves correctly once decoupled from the
// app, plus the registry's own dispatch logic.

import test from 'node:test';
import assert from 'node:assert/strict';

import { listKernels, getKernel, kernelsForDomain } from '../src/lib/kernelRegistry.js';
import { normalizeCommitments, findContradictions, summarizeConsistency } from '../src/lib/consistencyKernel.js';
import { normalizeMcmcSpec, executeMcmcSearch } from '../src/lib/mcmcSearch.js';
import { normalizeCheckSpec, executeCheck } from '../src/lib/numericCheck.js';
import { normalizeRelations, findOrderCycles } from '../src/lib/orderConsistency.js';
import { analyzeEquation } from '../src/lib/dimensionalAnalysis.js';
import { tensionSigma, classifyTension } from '../src/lib/measurementTension.js';

// ── registry dispatch ───────────────────────────────────────────────────

test('listKernels returns all thirteen registered kernels', () => {
  const ids = listKernels().map((k) => k.id).sort();
  assert.deepEqual(ids, [
    'boundary-check', 'chain-reachability', 'combinatorial', 'consistency',
    'decision-helper', 'domain-of-validity', 'dynamics', 'event-camera-pixel',
    'mcmc', 'neuromorphic-power', 'numeric-check', 'order-consistency', 'vector-span',
  ]);
});

test('getKernel returns the entry; unknown id throws rather than returning undefined', () => {
  assert.equal(getKernel('mcmc').label, 'MCMC counterexample search');
  assert.throws(() => getKernel('nope'), /Unknown kernel id/);
});

test('kernelsForDomain filters by tag', () => {
  const numeric = kernelsForDomain('numeric').map((k) => k.id).sort();
  assert.deepEqual(numeric, ['decision-helper', 'dynamics', 'event-camera-pixel', 'mcmc', 'neuromorphic-power', 'numeric-check']);
  assert.deepEqual(kernelsForDomain('device-action').map((k) => k.id), ['boundary-check']);
  assert.deepEqual(kernelsForDomain('hardware').map((k) => k.id).sort(), ['event-camera-pixel', 'neuromorphic-power']);
  assert.deepEqual(kernelsForDomain('decision-support').map((k) => k.id), ['decision-helper']);
});

test('every registered kernel exposes normalize and run as functions', () => {
  for (const k of listKernels()) {
    assert.equal(typeof k.normalize, 'function', `${k.id} missing normalize`);
    assert.equal(typeof k.run, 'function', `${k.id} missing run`);
  }
});

// ── consistency kernel, decoupled ───────────────────────────────────────

test('consistency kernel still detects a direct contradiction after extraction', () => {
  const commitments = normalizeCommitments({
    commitments: [
      { kind: 'assert', atom: 'growth is slowing', polarity: true, source: 'a' },
      { kind: 'assert', atom: 'growth is slowing', polarity: false, source: 'b' },
    ],
  });
  const findings = findContradictions(commitments);
  assert.equal(findings.length, 1);
  const summary = summarizeConsistency(findings, commitments.length);
  assert.equal(summary.verdict, 'inconsistent');
});

test('consistency kernel stays silent on a genuinely consistent set', () => {
  const commitments = normalizeCommitments({
    commitments: [
      { kind: 'assert', atom: 'growth is slowing', polarity: true, source: 'a' },
      { kind: 'assert', atom: 'churn is rising', polarity: true, source: 'b' },
    ],
  });
  assert.equal(findContradictions(commitments).length, 0);
});

// ── mcmc, decoupled ──────────────────────────────────────────────────────

test('mcmc still finds a planted counterexample after extraction', () => {
  const spec = normalizeMcmcSpec({
    kind: 'mcmc_search',
    note: 'x exceeds 5 somewhere in [0, 10]',
    params: [{ name: 'x', domain: [0, 10] }],
    objective: 'x - 5',
  });
  const result = executeMcmcSearch(spec);
  assert.equal(result.verdict, 'violated');
  assert.ok(result.bestPoint.x > 5);
});

test('mcmc is bit-for-bit deterministic across two runs of the same spec', () => {
  const spec = normalizeMcmcSpec({
    kind: 'mcmc_search',
    params: [{ name: 'x', domain: [0, 10] }],
    objective: 'x - 5',
  });
  const a = executeMcmcSearch(spec);
  const b = executeMcmcSearch(spec);
  assert.deepEqual(a.bestPoint, b.bestPoint);
});

// ── numeric check, decoupled ─────────────────────────────────────────────

test('numeric check round-trips a simple held inequality (lhs <= rhs)', () => {
  const spec = normalizeCheckSpec({
    kind: 'inequality',
    lhs: '-1',
    rhs: 'x^2',
    vars: [{ name: 'x', domain: [-10, 10] }],
  });
  const result = executeCheck(spec);
  assert.equal(result.verdict, 'held');
});

// ── order consistency, decoupled ─────────────────────────────────────────

test('order consistency still catches an impossible ranking cycle', () => {
  const relations = normalizeRelations({
    relations: [
      { subject: 'a', object: 'b', comparator: 'greater', metric: 'speed', source: 's1' },
      { subject: 'b', object: 'c', comparator: 'greater', metric: 'speed', source: 's2' },
      { subject: 'c', object: 'a', comparator: 'greater', metric: 'speed', source: 's3' },
    ],
  });
  const cycles = findOrderCycles(relations);
  assert.equal(cycles.length, 1);
});

// ── dimensional analysis, decoupled ──────────────────────────────────────

test('dimensional analysis still flags a mismatched equation', () => {
  const result = analyzeEquation({ lhs: 'F', rhs: 'm', assignments: { F: 'force', m: 'mass' } });
  assert.equal(result.status, 'violation');
});

// ── measurement tension, decoupled ───────────────────────────────────────

test('measurement tension still reproduces the Hubble-tension figure', () => {
  const sigma = tensionSigma({ value: 67.4, uncertainty: 0.5 }, { value: 73.0, uncertainty: 1.0 });
  assert.ok(Math.abs(sigma - 5.0) < 0.1);
  assert.equal(classifyTension(sigma), 'contradiction');
});

// ── boundary-check, via the registry (not deviceGate directly) ──────────

test('boundary-check kernel: path-containment holds inside, violates outside and on a look-alike sibling', () => {
  const kernel = getKernel('boundary-check');
  const inside = kernel.run(kernel.normalize({ kind: 'path-containment', target: '/a/b/c.txt', boundary: '/a/b' }));
  assert.equal(inside.verdict, 'held');
  const outside = kernel.run(kernel.normalize({ kind: 'path-containment', target: '/a/other/c.txt', boundary: '/a/b' }));
  assert.equal(outside.verdict, 'violated');
  const lookalike = kernel.run(kernel.normalize({ kind: 'path-containment', target: '/a/b-evil/c.txt', boundary: '/a/b' }));
  assert.equal(lookalike.verdict, 'violated');
});

test('boundary-check kernel: allowlist holds only for an exact match', () => {
  const kernel = getKernel('boundary-check');
  const held = kernel.run(kernel.normalize({ kind: 'allowlist', target: 'git', boundary: ['git', 'node'] }));
  assert.equal(held.verdict, 'held');
  const violated = kernel.run(kernel.normalize({ kind: 'allowlist', target: 'powershell', boundary: ['git', 'node'] }));
  assert.equal(violated.verdict, 'violated');
});
