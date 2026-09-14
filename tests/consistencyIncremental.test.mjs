// consistencyIncremental.test.mjs; upgrade 6 coverage — sparse
// incremental recomputation in consistencyKernel.js. The kernel's public
// functions (deriveClosure, findContradictions) keep their exact
// pre-upgrade signature and behavior; every test here calls them exactly
// as any existing caller would, with no new flags. "Cold" comparisons
// use a fresh dynamic import (a cache-busted specifier gives ES modules
// their own module-level state) rather than any test-only reset hook,
// so the production module needed zero new exports for this file to work.

import test from 'node:test';
import assert from 'node:assert/strict';
import { deriveClosure, findContradictions } from '../src/lib/consistencyKernel.js';

let freshCounter = 0;
async function freshKernel() {
  freshCounter++;
  return import(`../src/lib/consistencyKernel.js?fresh=${freshCounter}`);
}

function canonicalFindings(findings) {
  return findings
    .map((f) => JSON.stringify(f, Object.keys(f).sort()))
    .sort();
}

// ── Deterministic random commitment generator (seeded, no Math.random) ──

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

const ATOMS = ['alpha', 'beta', 'gamma', 'delta', 'epsilon'];
const CATEGORIES = ['marketplace', 'platform', 'network'];
const PROPERTIES = ['liquidity', 'scale', 'moat'];
const ENTITIES = ['acme', 'globex', 'initech'];

function randomCommitment(rand, id) {
  const kind = ['assert', 'assert', 'property', 'implies', 'universal', 'instance'][Math.floor(rand() * 6)];
  const pick = (arr) => arr[Math.floor(rand() * arr.length)];
  const bool = () => rand() < 0.5;
  switch (kind) {
    case 'assert': return { id, kind: 'assert', atom: pick(ATOMS), polarity: bool(), source: `s-${id}` };
    case 'property': return { id, kind: 'property', entity: pick(ENTITIES), property: pick(PROPERTIES), polarity: bool(), source: `s-${id}` };
    case 'implies': return { id, kind: 'implies', fromAtom: pick(ATOMS), fromPol: bool(), toAtom: pick(ATOMS), toPol: bool(), source: `s-${id}` };
    case 'universal': return { id, kind: 'universal', category: pick(CATEGORIES), property: pick(PROPERTIES), polarity: bool(), source: `s-${id}` };
    case 'instance': return { id, kind: 'instance', entity: pick(ENTITIES), category: pick(CATEGORIES), source: `s-${id}` };
    default: throw new Error('unreachable');
  }
}

function randomCommitmentSet(rand, n) {
  return Array.from({ length: n }, (_, i) => randomCommitment(rand, `c${i + 1}`));
}

// Applies `edits` random in-place position changes (content only; length
// and ids unchanged), the "small edit" shape the warm-start path targets.
function applyRandomEdits(rand, commitments, edits) {
  const out = commitments.map((c) => ({ ...c }));
  for (let e = 0; e < edits; e++) {
    const i = Math.floor(rand() * out.length);
    out[i] = randomCommitment(rand, out[i].id);
  }
  return out;
}

test('correctness property: incremental output matches forced-cold recomputation across many random commitment sets and edits', async () => {
  const rand = mulberry32(0xC1A11);
  const TRIALS = 300;
  let mismatches = 0;

  for (let t = 0; t < TRIALS; t++) {
    const n = 2 + Math.floor(rand() * 12);
    const setA = randomCommitmentSet(rand, n);
    const edits = 1 + Math.floor(rand() * Math.min(3, n));
    const setB = applyRandomEdits(rand, setA, edits);

    // Warm path: same module instance, called on A then B.
    const warmKernel = await freshKernel();
    warmKernel.findContradictions(setA);
    const warmResult = warmKernel.findContradictions(setB);

    // Cold path: a fresh module instance, called on B directly.
    const coldKernel = await freshKernel();
    const coldResult = coldKernel.findContradictions(setB);

    const warmCanon = canonicalFindings(warmResult);
    const coldCanon = canonicalFindings(coldResult);
    if (JSON.stringify(warmCanon) !== JSON.stringify(coldCanon)) {
      mismatches++;
      if (mismatches <= 3) {
        console.error(`MISMATCH at trial ${t}: setA=${JSON.stringify(setA)} setB=${JSON.stringify(setB)}`);
        console.error('warm:', warmCanon);
        console.error('cold:', coldCanon);
      }
    }
  }
  assert.equal(mismatches, 0, `${mismatches}/${TRIALS} trials produced a mismatch between warm and cold recomputation`);
});

test('correctness property: a THIRD call (double warm-start) still matches cold recomputation', async () => {
  const rand = mulberry32(0xBEEF);
  const TRIALS = 100;
  let mismatches = 0;

  for (let t = 0; t < TRIALS; t++) {
    const n = 3 + Math.floor(rand() * 10);
    const setA = randomCommitmentSet(rand, n);
    const setB = applyRandomEdits(rand, setA, 1 + Math.floor(rand() * 2));
    const setC = applyRandomEdits(rand, setB, 1 + Math.floor(rand() * 2));

    const warmKernel = await freshKernel();
    warmKernel.findContradictions(setA);
    warmKernel.findContradictions(setB);
    const warmResult = warmKernel.findContradictions(setC);

    const coldKernel = await freshKernel();
    const coldResult = coldKernel.findContradictions(setC);

    if (JSON.stringify(canonicalFindings(warmResult)) !== JSON.stringify(canonicalFindings(coldResult))) mismatches++;
  }
  assert.equal(mismatches, 0, `${mismatches}/${TRIALS} trials produced a mismatch after two consecutive warm-starts`);
});

test('correctness property: adding and removing commitments (length changes, declines warm-start) still matches cold', async () => {
  const rand = mulberry32(0x5EED);
  const TRIALS = 100;
  let mismatches = 0;

  for (let t = 0; t < TRIALS; t++) {
    const n = 3 + Math.floor(rand() * 8);
    const setA = randomCommitmentSet(rand, n);
    // setB: different length -- some removed, some added.
    const keep = setA.filter(() => rand() < 0.7);
    const extra = randomCommitmentSet(rand, 1 + Math.floor(rand() * 3)).map((c, i) => ({ ...c, id: `x${i + 1}` }));
    const setB = [...keep, ...extra];

    const warmKernel = await freshKernel();
    warmKernel.findContradictions(setA);
    const warmResult = warmKernel.findContradictions(setB);

    const coldKernel = await freshKernel();
    const coldResult = coldKernel.findContradictions(setB);

    if (JSON.stringify(canonicalFindings(warmResult)) !== JSON.stringify(canonicalFindings(coldResult))) mismatches++;
  }
  assert.equal(mismatches, 0, `${mismatches}/${TRIALS} trials produced a mismatch across a length-changing edit`);
});

test('regression: existing direct-contradiction case still works exactly as before, called via the module-level import (not a fresh one)', () => {
  const commitments = [
    { id: 'c1', kind: 'assert', atom: 'growth is slowing', polarity: true, source: 'a' },
    { id: 'c2', kind: 'assert', atom: 'growth is slowing', polarity: false, source: 'b' },
  ];
  const findings = findContradictions(commitments);
  assert.equal(findings.length, 1);
});

test('correctness property: calling findContradictions TWICE on the identical, unchanged state is idempotent', async () => {
  // Regression coverage for a bug the edit-transition property tests above
  // could not see: they only ever compare a warm call against a cold call
  // made on a DIFFERENT (post-edit) state. This test instead calls warm
  // twice in a row on the exact same state with nothing changed in
  // between, which is what originally exposed a round-timing bug -- a
  // warm-carried fact merged into the facts map before the fixpoint's
  // first round could let a self-referential or cyclic rule fire a round
  // earlier than a true cold run ever would, so the SECOND call (now
  // itself warm-starting from the first call's result) produced a
  // different "why" chain than the first call did, even though no
  // commitment had changed at all.
  const rand = mulberry32(0x1DE4);
  const TRIALS = 200;
  let mismatches = 0;

  for (let t = 0; t < TRIALS; t++) {
    const n = 2 + Math.floor(rand() * 15);
    const setA = randomCommitmentSet(rand, n);
    const kernel = await freshKernel();
    kernel.findContradictions(setA);

    let cur = setA;
    const numEdits = 1 + Math.floor(rand() * 3);
    for (let e = 0; e < numEdits; e++) {
      cur = applyRandomEdits(rand, cur, 1 + Math.floor(rand() * Math.min(3, cur.length)));
      kernel.findContradictions(cur);
    }

    const first = kernel.findContradictions(cur);
    const second = kernel.findContradictions(cur);
    if (JSON.stringify(canonicalFindings(first)) !== JSON.stringify(canonicalFindings(second))) {
      mismatches++;
      if (mismatches <= 3) {
        console.error(`IDEMPOTENCE MISMATCH at trial ${t}: cur=${JSON.stringify(cur)}`);
        console.error('first:', canonicalFindings(first));
        console.error('second:', canonicalFindings(second));
      }
    }
  }
  assert.equal(mismatches, 0, `${mismatches}/${TRIALS} trials produced a different result on a repeated call with nothing changed`);
});

test('deriveClosure return shape is unchanged: {facts: Map, contradictions: Array}', () => {
  const commitments = [{ id: 'c1', kind: 'assert', atom: 'x', polarity: true, source: 's' }];
  const result = deriveClosure(commitments);
  assert.ok(result.facts instanceof Map);
  assert.ok(Array.isArray(result.contradictions));
});
