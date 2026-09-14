// benchHarness.test.mjs; unit coverage for bench/harness.mjs's pure
// grading logic, against SYNTHETIC finding-code data -- independent of
// whether the real 20-CVE dataset has been ingested. This is what
// proves the scoring rules themselves (TP/FN/FP math, not-covered vs
// not-applicable bucketing, precision/recall aggregation) are correct,
// separate from bench/run.mjs actually walking real fixtures.

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  CWE_TO_SELFAUDIT_CODE,
  expectedSelfAuditCodes,
  gradeEntry,
  aggregate,
  gradeSelfAuditAcrossDataset,
  notApplicableRowsForDataset,
  structurallyNotApplicableKernelIds,
  SELF_AUDIT_KERNEL_ID,
  NOT_APPLICABLE_REASON,
  isAnnotatedAsLikelyFixed,
  gradeEntryAnnotationAware,
} from '../bench/harness.mjs';

// ── CWE -> code lookup ────────────────────────────────────────────────

test('expectedSelfAuditCodes maps a known CWE to its table entry', () => {
  assert.deepEqual(expectedSelfAuditCodes(['CWE-1321']), ['prototype-mutation', 'unguarded-dynamic-key-assignment']);
  assert.deepEqual(expectedSelfAuditCodes(['CWE-798']), ['hardcoded-credential']);
});

test('expectedSelfAuditCodes accepts EITHER eval-usage or function-constructor-usage for CWE-502/CWE-95 -- selfAudit itself treats new Function() as eval-equivalent', () => {
  assert.deepEqual(expectedSelfAuditCodes(['CWE-502']), ['eval-usage', 'function-constructor-usage']);
  assert.deepEqual(expectedSelfAuditCodes(['CWE-95']), ['eval-usage', 'function-constructor-usage']);
});

test('expectedSelfAuditCodes returns the FIRST matching CWE when an entry has several', () => {
  assert.deepEqual(expectedSelfAuditCodes(['CWE-79', 'CWE-1321']), ['prototype-mutation', 'unguarded-dynamic-key-assignment']);
});

test('expectedSelfAuditCodes returns null for a CWE class selfAudit has no rule for -- never guesses', () => {
  assert.equal(expectedSelfAuditCodes(['CWE-22']), null); // path traversal
  assert.equal(expectedSelfAuditCodes(['CWE-79']), null); // XSS
  assert.equal(expectedSelfAuditCodes(['CWE-400', 'CWE-1333']), null); // ReDoS
  assert.equal(expectedSelfAuditCodes([]), null);
  assert.equal(expectedSelfAuditCodes(undefined), null);
});

test('CWE_TO_SELFAUDIT_CODE is a flat, documented table -- every entry has a real cwe id, a non-empty codes array, and a note', () => {
  for (const e of CWE_TO_SELFAUDIT_CODE) {
    assert.match(e.cwe, /^CWE-\d+$/);
    assert.ok(Array.isArray(e.codes) && e.codes.length > 0);
    for (const c of e.codes) assert.equal(typeof c, 'string');
    assert.equal(typeof e.note, 'string');
    assert.ok(e.note.length > 0);
  }
});

// ── gradeEntry: the four-cell contingency table ──────────────────────

test('gradeEntry: not covered when no rule targets the CWE -- excluded, not scored as a miss', () => {
  const r = gradeEntry(null, ['some-other-code'], []);
  assert.equal(r.verdict, 'not-covered');
  assert.deepEqual([r.tp, r.fn, r.fp, r.tn], [0, 0, 0, 0]);
});

test('gradeEntry: true positive -- flags the vulnerable version, correctly silent on the fixed one', () => {
  const r = gradeEntry(['prototype-mutation'], ['prototype-mutation'], []);
  assert.equal(r.verdict, 'correct');
  assert.deepEqual([r.tp, r.fn, r.fp, r.tn], [1, 0, 0, 1]);
});

test('gradeEntry: a hit on ANY of several acceptable codes counts (e.g. function-constructor-usage for a CWE-502 entry)', () => {
  const r = gradeEntry(['eval-usage', 'function-constructor-usage'], ['function-constructor-usage'], []);
  assert.equal(r.verdict, 'correct');
  assert.deepEqual([r.tp, r.fn, r.fp, r.tn], [1, 0, 0, 1]);
});

test('gradeEntry: false negative -- misses the vulnerable version entirely', () => {
  const r = gradeEntry(['prototype-mutation'], [], []);
  assert.equal(r.verdict, 'missed');
  assert.deepEqual([r.tp, r.fn, r.fp, r.tn], [0, 1, 0, 1]);
});

test('gradeEntry: flags both pre AND post -- a real true positive that is ALSO noisy (the exact failure mode point 2 of the spec exists to catch)', () => {
  const r = gradeEntry(['unreviewed-child-process'], ['unreviewed-child-process'], ['unreviewed-child-process']);
  assert.equal(r.verdict, 'flags-both-versions');
  assert.deepEqual([r.tp, r.fn, r.fp, r.tn], [1, 0, 1, 0]);
});

test('gradeEntry: misses pre but somehow flags post -- rare, still tracked honestly rather than hidden', () => {
  const r = gradeEntry(['weak-crypto-algorithm'], [], ['weak-crypto-algorithm']);
  assert.equal(r.verdict, 'missed-pre-flagged-post');
  assert.deepEqual([r.tp, r.fn, r.fp, r.tn], [0, 1, 1, 0]);
});

// ── aggregate: precision/recall, and the exclusion accounting ───────

test('aggregate computes precision/recall exactly per the spec\'s formulas over a known synthetic set', () => {
  // 3 covered rows: 2 correct (TP), 1 missed (FN); 1 noisy row also
  // contributes a real FP on top.
  const rows = [
    { covered: true, tp: 1, fn: 0, fp: 0, tn: 1 },
    { covered: true, tp: 1, fn: 0, fp: 0, tn: 1 },
    { covered: true, tp: 0, fn: 1, fp: 0, tn: 1 },
    { covered: true, tp: 1, fn: 0, fp: 1, tn: 0 },
  ];
  const summary = aggregate(rows);
  assert.equal(summary.tp, 3);
  assert.equal(summary.fn, 1);
  assert.equal(summary.fp, 1);
  assert.equal(summary.covered, 4);
  assert.equal(summary.precision, 3 / 4); // TP/(TP+FP)
  assert.equal(summary.recall, 3 / 4);    // TP/(TP+FN)
});

test('aggregate excludes not-covered/not-applicable rows from precision and recall entirely -- they are not zeros, they are absent', () => {
  const rows = [
    { covered: false, tp: 0, fn: 0, fp: 0, tn: 0 },
    { covered: false, tp: 0, fn: 0, fp: 0, tn: 0 },
    { covered: true, tp: 1, fn: 0, fp: 0, tn: 1 },
  ];
  const summary = aggregate(rows);
  assert.equal(summary.notCovered, 2);
  assert.equal(summary.covered, 1);
  assert.equal(summary.precision, 1); // computed ONLY over the one covered row, not diluted by the two excluded ones
  assert.equal(summary.recall, 1);
});

test('aggregate returns null (not 0, not 1) precision/recall when there is no covered data at all -- an honest "no data", not a fabricated score', () => {
  const summary = aggregate([{ covered: false, tp: 0, fn: 0, fp: 0, tn: 0 }]);
  assert.equal(summary.precision, null);
  assert.equal(summary.recall, null);
});

// ── full dataset-shaped grading ──────────────────────────────────────

test('gradeSelfAuditAcrossDataset produces the exact {cve_id, kernel, verdict, ground_truth, correct} row shape the spec asks for', () => {
  const dataset = [
    { id: 'GHSA-aaaa', cwe: ['CWE-1321'] },
    { id: 'GHSA-bbbb', cwe: ['CWE-22'] }, // not covered by any selfAudit rule
  ];
  const findingsByEntry = {
    'GHSA-aaaa': { preCodes: ['prototype-mutation'], postCodes: [] },
    'GHSA-bbbb': { preCodes: [], postCodes: [] },
  };
  const { kernelId, rows, summary } = gradeSelfAuditAcrossDataset(dataset, findingsByEntry);
  assert.equal(kernelId, SELF_AUDIT_KERNEL_ID);
  assert.equal(rows.length, 2);

  const a = rows.find((r) => r.cve_id === 'GHSA-aaaa');
  assert.equal(a.kernel, 'self-audit');
  assert.equal(a.verdict, 'correct');
  assert.equal(a.correct, true);
  assert.deepEqual(a.ground_truth, { cwe: ['CWE-1321'], expectedSelfAuditCodes: ['prototype-mutation', 'unguarded-dynamic-key-assignment'] });

  const b = rows.find((r) => r.cve_id === 'GHSA-bbbb');
  assert.equal(b.verdict, 'not-covered');
  assert.equal(b.correct, null); // not-covered is neither correct nor incorrect

  // Only the covered entry (a) feeds precision/recall; b is excluded.
  assert.equal(summary.covered, 1);
  assert.equal(summary.notCovered, 1);
  assert.equal(summary.precision, 1);
  assert.equal(summary.recall, 1);
});

test('a dataset entry missing from findingsByEntry (e.g. a fixture that failed to load) defaults to no findings, not a crash', () => {
  const dataset = [{ id: 'GHSA-missing', cwe: ['CWE-798'] }];
  const { rows } = gradeSelfAuditAcrossDataset(dataset, {});
  assert.equal(rows[0].verdict, 'missed');
});

// ── not-applicable kernels ────────────────────────────────────────────

test('structurallyNotApplicableKernelIds lists every kernel actually registered in kernelRegistry.js', () => {
  const ids = structurallyNotApplicableKernelIds();
  assert.ok(ids.includes('boundary-check'));
  assert.ok(ids.includes('chain-reachability'));
  assert.ok(ids.includes('consistency'));
  assert.equal(ids.length, 13);
});

test('notApplicableRowsForDataset marks every entry not-applicable, uncovered, with correct=null and a disclosed reason', () => {
  const dataset = [{ id: 'GHSA-x', cwe: ['CWE-1321'] }, { id: 'GHSA-y', cwe: ['CWE-798'] }];
  const rows = notApplicableRowsForDataset(dataset, 'mcmc');
  assert.equal(rows.length, 2);
  for (const r of rows) {
    assert.equal(r.kernel, 'mcmc');
    assert.equal(r.verdict, 'not-applicable');
    assert.equal(r.correct, null);
    assert.equal(r.covered, false);
    assert.equal(r.reason, NOT_APPLICABLE_REASON);
  }
});

test('not-applicable rows aggregate to null precision/recall, never a fabricated score', () => {
  const dataset = [{ id: 'GHSA-x', cwe: ['CWE-1321'] }];
  const rows = notApplicableRowsForDataset(dataset, 'domain-of-validity');
  const summary = aggregate(rows);
  assert.equal(summary.precision, null);
  assert.equal(summary.recall, null);
  assert.equal(summary.notCovered, 1);
});

// ── secondary lens: annotation-aware precision ───────────────────────
// Never changes the primary/strict metric -- see harness.mjs's header
// on gradeEntryAnnotationAware for why these stay two separate numbers.

test('isAnnotatedAsLikelyFixed reads guardDetected for eval-usage/function-constructor-usage', () => {
  assert.equal(isAnnotatedAsLikelyFixed({ code: 'eval-usage', guardDetected: true }), true);
  assert.equal(isAnnotatedAsLikelyFixed({ code: 'function-constructor-usage', guardDetected: false }), false);
});

test('isAnnotatedAsLikelyFixed reads usesShellInterpolation (inverted) for unreviewed-child-process', () => {
  assert.equal(isAnnotatedAsLikelyFixed({ code: 'unreviewed-child-process', usesShellInterpolation: false }), true);
  assert.equal(isAnnotatedAsLikelyFixed({ code: 'unreviewed-child-process', usesShellInterpolation: true }), false);
});

test('isAnnotatedAsLikelyFixed is false for codes with no annotation defined at all (e.g. prototype-mutation) -- never guesses', () => {
  assert.equal(isAnnotatedAsLikelyFixed({ code: 'prototype-mutation' }), false);
  assert.equal(isAnnotatedAsLikelyFixed(null), false);
});

test('gradeEntryAnnotationAware: an annotated-as-fixed post finding does not count as an annotation-aware FP', () => {
  const r = gradeEntryAnnotationAware(
    ['function-constructor-usage'],
    ['function-constructor-usage'],
    [{ code: 'function-constructor-usage', guardDetected: true }]
  );
  assert.deepEqual(r, { tp: 1, fn: 0, fp: 0, tn: 1 });
});

test('gradeEntryAnnotationAware: an UNannotated post finding still counts as a real FP', () => {
  const r = gradeEntryAnnotationAware(
    ['unreviewed-child-process'],
    ['unreviewed-child-process'],
    [{ code: 'unreviewed-child-process', usesShellInterpolation: true }]
  );
  assert.deepEqual(r, { tp: 1, fn: 0, fp: 1, tn: 0 });
});

test('gradeEntryAnnotationAware: pre-patch tp/fn are identical to gradeEntry -- only post-patch noise counting changes', () => {
  const strict = gradeEntry(['prototype-mutation'], [], ['prototype-mutation']);
  const aware = gradeEntryAnnotationAware(['prototype-mutation'], [], [{ code: 'prototype-mutation' }]);
  assert.equal(strict.tp, aware.tp);
  assert.equal(strict.fn, aware.fn);
});

test('gradeSelfAuditAcrossDataset attaches an annotationAwareSummary that only ever differs from summary in fp/tn/precision, never tp/fn/recall', () => {
  const dataset = [{ id: 'GHSA-x', cwe: ['CWE-95'] }];
  const findingsByEntry = {
    'GHSA-x': {
      preCodes: ['function-constructor-usage'],
      postCodes: ['function-constructor-usage'],
      postFindings: [{ code: 'function-constructor-usage', guardDetected: true }],
    },
  };
  const { summary, annotationAwareSummary } = gradeSelfAuditAcrossDataset(dataset, findingsByEntry);
  assert.equal(summary.tp, annotationAwareSummary.tp);
  assert.equal(summary.fn, annotationAwareSummary.fn);
  assert.equal(summary.fp, 1); // strict: flags both, still a false positive
  assert.equal(annotationAwareSummary.fp, 0); // annotation-aware: recognized as guarded, not counted
  assert.equal(summary.precision, 0.5);
  assert.equal(annotationAwareSummary.precision, 1);
});
