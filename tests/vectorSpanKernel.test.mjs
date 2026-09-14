// vectorSpanKernel.test.mjs; coverage for the rank/span kernel, plus the
// DMCC_paper_complete steering-vocabulary data as a real regression case
// (transcribed directly from the paper's Section 5.2 table -- not a
// synthetic stand-in) so this exact real-world check stays reproducible
// through the actual, registered kernel rather than an ad hoc script.

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  normalizeVectorSpanSpec,
  computeRankByElimination,
  computeRankByMinors,
  determinant,
  executeVectorSpan,
  MAX_DIMENSION,
} from '../src/lib/vectorSpanKernel.js';
import { getKernel } from '../src/lib/kernelRegistry.js';

// ── determinant ────────────────────────────────────────────────────────

test('determinant: 2x2 and 3x3 hand-computed cases', () => {
  assert.equal(determinant([[1, 2], [3, 4]]), 1 * 4 - 2 * 3);
  assert.equal(determinant([[1, 0, 0], [0, 1, 0], [0, 0, 1]]), 1); // identity
  assert.equal(determinant([[1, 2, 3], [4, 5, 6], [7, 8, 9]]), 0); // rows linearly dependent
});

// ── computeRankByElimination / computeRankByMinors agree on known cases ─

test('a full-rank 3x3 (identity) has rank 3 under both algorithms', () => {
  const vectors = [
    { label: 'e1', components: [1, 0, 0] },
    { label: 'e2', components: [0, 1, 0] },
    { label: 'e3', components: [0, 0, 1] },
  ];
  assert.equal(computeRankByElimination(vectors, 3), 3);
  assert.equal(computeRankByMinors(vectors, 3, 3), 3);
});

test('two identical vectors (rank-deficient) is correctly rank 1 under both algorithms', () => {
  const vectors = [
    { label: 'a', components: [2, 4, 6] },
    { label: 'b', components: [1, 2, 3] }, // a scalar multiple of 'a' -- same direction
  ];
  assert.equal(computeRankByElimination(vectors, 3), 1);
  assert.equal(computeRankByMinors(vectors, 3, 1), 1);
});

test('three vectors in a plane (rank 2 in R^3) are correctly identified by both algorithms', () => {
  const vectors = [
    { label: 'a', components: [1, 0, 0] },
    { label: 'b', components: [0, 1, 0] },
    { label: 'c', components: [1, 1, 0] }, // = a + b, adds nothing new
  ];
  assert.equal(computeRankByElimination(vectors, 3), 2);
  assert.equal(computeRankByMinors(vectors, 3, 3), 2);
});

// ── the column-combination bug, caught and fixed before shipping ────
// An earlier version of computeRankByMinors only tried the FIRST k
// columns for each row combination, which is wrong for k < dimension.
// This is exactly the shape that would have been silently mis-ranked:
// the "signal" columns are NOT the first ones.

test('computeRankByMinors correctly finds rank 2 even when the nonzero minor lives in NON-leading columns', () => {
  const vectors = [
    { label: 'a', components: [0, 5, 0, 0] }, // signal only in column 2
    { label: 'b', components: [0, 0, 7, 0] }, // signal only in column 3
  ];
  // The first-2-columns submatrix for both rows is [[0,5],[0,0]] (det=0) --
  // a buggy "only try leading columns" implementation would wrongly
  // conclude rank < 2 here. The real answer is rank 2 (columns 2 and 3).
  assert.equal(computeRankByMinors(vectors, 4, 2), 2);
  assert.equal(computeRankByElimination(vectors, 4), 2);
});

// ── normalizeVectorSpanSpec ────────────────────────────────────────────

test('normalizeVectorSpanSpec: "spans" is sugar for rank-at-least === dimension', () => {
  const spec = normalizeVectorSpanSpec({
    kind: 'vector_span', dimension: 3,
    vectors: [{ label: 'a', components: [1, 0, 0] }],
    claim: 'spans',
  });
  assert.equal(spec.claim, 'rank-at-least');
  assert.equal(spec.targetRank, 3);
});

test('normalizeVectorSpanSpec: rejects a dimension mismatch in a vector\'s components', () => {
  assert.throws(() => normalizeVectorSpanSpec({
    kind: 'vector_span', dimension: 3,
    vectors: [{ label: 'a', components: [1, 0] }],
    claim: 'spans',
  }), /needs exactly 3 numeric components/);
});

test('normalizeVectorSpanSpec: rejects dimension above MAX_DIMENSION', () => {
  assert.throws(() => normalizeVectorSpanSpec({
    kind: 'vector_span', dimension: MAX_DIMENSION + 1,
    vectors: [{ label: 'a', components: Array(MAX_DIMENSION + 1).fill(1) }],
    claim: 'spans',
  }), /dimension must be an integer/);
});

test('normalizeVectorSpanSpec: kind:"none" passes through with a reason, like every other kernel\'s decline path', () => {
  assert.deepEqual(normalizeVectorSpanSpec({ kind: 'none', reason: 'not a span claim' }), { kind: 'none', reason: 'not a span claim' });
});

// ── executeVectorSpan: end to end ────────────────────────────────────

test('executeVectorSpan confirms a genuine spanning claim, with both algorithms agreeing', () => {
  const spec = normalizeVectorSpanSpec({
    kind: 'vector_span', dimension: 3,
    vectors: [
      { label: 'e1', components: [1, 0, 0] },
      { label: 'e2', components: [0, 1, 0] },
      { label: 'e3', components: [0, 0, 1] },
    ],
    claim: 'spans',
  });
  const result = executeVectorSpan(spec);
  assert.equal(result.verdict, 'claim-confirmed');
  assert.equal(result.rank, 3);
  assert.equal(result.rankByElimination, result.rankByMinors);
});

test('executeVectorSpan refutes a spanning claim that is actually rank-deficient', () => {
  const spec = normalizeVectorSpanSpec({
    kind: 'vector_span', dimension: 3,
    vectors: [
      { label: 'a', components: [1, 0, 0] },
      { label: 'b', components: [2, 0, 0] }, // same direction as a -- does not add rank
    ],
    claim: 'spans',
  });
  const result = executeVectorSpan(spec);
  assert.equal(result.verdict, 'claim-refuted');
  assert.equal(result.rank, 1);
});

test('executeVectorSpan: kind:"none" is inconclusive, not a fabricated verdict', () => {
  const result = executeVectorSpan({ kind: 'none', reason: 'no vectors named' });
  assert.equal(result.verdict, 'inconclusive');
});

test('registered in kernelRegistry.js as vector-span, needs model extraction, round-trips through the registry', () => {
  const kernel = getKernel('vector-span');
  assert.equal(kernel.needsModelExtraction, true);
  assert.equal(typeof kernel.buildPrompt, 'function');
  const spec = kernel.normalize({
    kind: 'vector_span', dimension: 3,
    vectors: [{ label: 'a', components: [1, 0, 0] }, { label: 'b', components: [0, 1, 0] }, { label: 'c', components: [0, 0, 1] }],
    claim: 'spans',
  });
  const result = kernel.run(spec);
  assert.equal(result.verdict, 'claim-confirmed');
});

// ── DMCC_paper_complete.docx, Section 5.2 / 6.1: real regression data ──
// Transcribed directly from the paper's steering-vocabulary table, not
// synthesized. This is the actual claim independently verified: the full
// 16-triad steering vocabulary spans R^3, and the paper's own 3-vector
// subset (A2, A5, A3) has det = -0.4325.

const DMCC_VECTORS = [
  { label: 'A1', components: [0.078, 0.0, 2.949] },
  { label: 'A2', components: [0.0, 0.0, 2.810] },
  { label: 'A3', components: [0.372, -0.240, 2.915] },
  { label: 'A4', components: [-0.276, -0.135, 2.863] },
  { label: 'A5', components: [0.060, 0.375, 2.930] },
  { label: 'A6', components: [-0.294, 0.240, 2.844] },
  { label: 'A7', components: [0.018, -0.375, 2.829] },
  { label: 'A8', components: [0.354, 0.135, 2.896] },
  { label: 'B1', components: [-0.078, 0.0, 2.949] },
  { label: 'B2', components: [0.0, 0.0, 2.810] },
  { label: 'B3', components: [0.237, 0.135, 2.915] },
  { label: 'B4', components: [-0.138, -0.375, 2.930] },
  { label: 'B5', components: [0.237, -0.135, 2.863] },
  { label: 'B6', components: [0.255, -0.510, 2.844] },
  { label: 'B7', components: [-0.393, 0.135, 2.829] },
  { label: 'B8', components: [-0.018, 0.375, 2.844] },
];

test('DMCC: the paper\'s own 3-vector subset (A2, A5, A3) has det = -0.4325, matching the paper exactly', () => {
  const A2 = DMCC_VECTORS.find((v) => v.label === 'A2').components;
  const A5 = DMCC_VECTORS.find((v) => v.label === 'A5').components;
  const A3 = DMCC_VECTORS.find((v) => v.label === 'A3').components;
  const det = determinant([A2, A5, A3]);
  assert.equal(Math.round(det * 10000) / 10000, -0.4325);
});

test('DMCC: the FULL 16-triad steering vocabulary is independently proven to span R^3, through the real registered kernel', () => {
  const kernel = getKernel('vector-span');
  const spec = kernel.normalize({
    kind: 'vector_span',
    note: 'DMCC steering vocabulary spans R^3 (Section 6.1)',
    dimension: 3,
    vectors: DMCC_VECTORS,
    claim: 'spans',
  });
  const result = kernel.run(spec);
  assert.equal(result.verdict, 'claim-confirmed');
  assert.equal(result.rank, 3);
  assert.equal(result.rankByElimination, 3);
  assert.equal(result.rankByMinors, 3);
  assert.equal(result.vectorCount, 16);
});
