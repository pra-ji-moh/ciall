// dratProof.test.mjs; upgrade 16 — validates the independent RUP proof
// checker against HAND-TRACED cases first (worked by hand in these
// comments, not just "the code agrees with itself"), before any
// integration with satKernel.js's own proof output. This is the
// checker's own correctness evidence, deliberately kept separate from
// trusting the solver that will later feed it real proofs.

import test from 'node:test';
import assert from 'node:assert/strict';
import { checkRupProof, writeDratFile } from '../src/lib/dratProof.js';
import { solveSat } from '../src/lib/satKernel.js';

// ---- hand-traced case 1: the simplest possible UNSAT instance -----------
//
// clauses = [[1],[-1]] (x1 must be true AND x1 must be false).
// A single-line proof "0" (the empty clause, no literals to negate) is
// checked by propagating db AS-IS with zero new assumptions: clause [1]
// is already unit (1 literal) and forces x1=true; clause [-1] is then
// immediately falsified (its only literal is now false). Conflict found
// -> the empty clause is RUP-valid -> UNSAT certified in one line.

test('hand-traced: the trivial 2-clause contradiction is certified UNSAT by a single-line proof', () => {
  const result = checkRupProof(1, [[1], [-1]], ['0']);
  assert.equal(result.valid, true);
  assert.equal(result.linesChecked, 1);
});

// ---- hand-traced case 2: a real 2-variable, 4-clause UNSAT instance ------
//
// clauses = (x1∨x2)∧(x1∨¬x2)∧(¬x1∨x2)∧(¬x1∨¬x2) -- unsatisfiable for
// either value of x1 (worked by hand in this file's own commit history/
// design notes): x1=T forces x2=T (clause 3) AND x2=F (clause 4);
// x1=F forces x2=T (clause 1) AND x2=F (clause 2). Either way, both
// values of x2 are forced simultaneously -- contradiction.
//
// A real 3-line DRAT proof, hand-verified by tracing unit propagation
// literal by literal (see the design notes accompanying this test):
//   "1 0"  -- assume x1=F; clause1 forces x2=T; clause2 then falsified
//             (needs x2=F) -> conflict -> (x1) is RUP-valid.
//   "-1 0" -- assume x1=T; clause3 forces x2=T; clause4 then falsified
//             (needs x2=F) -> conflict -> (¬x1) is RUP-valid.
//   "0"    -- with both (x1) and (¬x1) now in the clause database,
//             propagating with NO assumptions forces x1=T (from the
//             unit clause (x1)) then immediately falsifies (¬x1) ->
//             conflict -> the empty clause is RUP-valid.
const XOR_CLAUSES = [[1, 2], [1, -2], [-1, 2], [-1, -2]];

test('hand-traced: a real 2-variable UNSAT instance is certified by a real 3-line resolution proof', () => {
  const result = checkRupProof(2, XOR_CLAUSES, ['1 0', '-1 0', '0']);
  assert.equal(result.valid, true);
  assert.equal(result.linesChecked, 3);
});

test('a proof that asserts the empty clause immediately, skipping the actual derivation steps, is correctly REJECTED, not rubber-stamped', () => {
  // No clause in XOR_CLAUSES is unit to begin with, so propagating with
  // zero assumptions and zero prior derivations reaches a fixpoint with
  // no conflict at all -- the empty clause is genuinely NOT
  // RUP-derivable from the original clauses alone in one step.
  const result = checkRupProof(2, XOR_CLAUSES, ['0']);
  assert.equal(result.valid, false);
  assert.equal(result.failedAtLine, 0);
});

test('a proof missing its final empty-clause line is correctly REJECTED, even though every line it DOES have is individually RUP-valid', () => {
  const result = checkRupProof(2, XOR_CLAUSES, ['1 0', '-1 0']);
  assert.equal(result.valid, false);
  assert.match(result.reason, /not the empty clause/);
});

test('a genuinely arbitrary, unrelated first line is REJECTED with the specific failing line number', () => {
  // Asserting a completely unrelated variable is forced true, with zero
  // prior derivations to justify it, cannot be RUP-valid: nothing in
  // XOR_CLAUSES forces anything until some real unit fact exists first.
  // (An earlier version of this test tried flipping a literal's SIGN
  // instead, e.g. claiming (x2) rather than an unrelated variable --
  // that turned out to be independently RUP-derivable too, for real,
  // structural reasons specific to this tightly-constrained 2-variable
  // instance, discovered by actually running it rather than assuming.
  // Asserting an unrelated variable avoids that ambiguity entirely.)
  const result = checkRupProof(3, [...XOR_CLAUSES, [3]], ['-3 0', '0']);
  assert.equal(result.valid, false);
  assert.equal(result.failedAtLine, 0);
});

// ---- malformed input -----------------------------------------------------

test('checkRupProof throws on a malformed proof line (no trailing 0)', () => {
  assert.throws(() => checkRupProof(1, [[1], [-1]], ['1 2']), /malformed proof line/);
});

test('checkRupProof rejects invalid arguments', () => {
  assert.throws(() => checkRupProof(0, [[1]], ['0']), /positive integer numVars/);
  assert.throws(() => checkRupProof(1, null, ['0']), /array of originalClauses/);
  assert.throws(() => checkRupProof(1, [[1]], []), /non-empty proofLines/);
});

// ---- writeDratFile ---------------------------------------------------------

test('writeDratFile formats proof lines as a real, standard newline-terminated DRAT file', () => {
  const file = writeDratFile(['1 0', '-1 0', '0']);
  assert.equal(file, '1 0\n-1 0\n0\n');
});

// ---- real integration: a real UNSAT proof FROM THE ACTUAL SOLVER, --------
// ---- independently re-checked by this completely separate checker --------

test('integration: solveSat(emitProof:true) on a real pigeonhole instance produces a proof this INDEPENDENT checker certifies as valid UNSAT', () => {
  // Pigeonhole PHP(3,2): 3 pigeons, 2 holes -- pigeon i in hole j is
  // var (i-1)*2+j. Every pigeon in some hole; no hole holds 2 pigeons.
  const numHoles = 2, numPigeons = 3;
  const v = (p, h) => (p - 1) * numHoles + h;
  const clauses = [];
  for (let p = 1; p <= numPigeons; p++) clauses.push(Array.from({ length: numHoles }, (_, h) => v(p, h + 1)));
  for (let h = 1; h <= numHoles; h++) {
    for (let p1 = 1; p1 <= numPigeons; p1++) {
      for (let p2 = p1 + 1; p2 <= numPigeons; p2++) clauses.push([-v(p1, h), -v(p2, h)]);
    }
  }
  const numVars = numPigeons * numHoles;

  const result = solveSat(numVars, clauses, { emitProof: true });
  assert.equal(result.sat, false);
  assert.ok(Array.isArray(result.proof) && result.proof.length > 0, 'solver must emit a non-empty proof when emitProof is set and the result is UNSAT');
  assert.equal(result.proof[result.proof.length - 1], '0', 'a certifying proof must end in the empty clause');

  const check = checkRupProof(numVars, clauses, result.proof);
  assert.equal(check.valid, true, `independent checker rejected the solver's own proof: ${check.reason || ''}`);
});

test('integration: a TRUNCATED real solver proof (missing its final empty-clause line) is correctly rejected by the independent checker', () => {
  // Pigeonhole PHP(3,2): 3 pigeons, 2 holes -- confirmed by direct
  // inspection to produce a real 2-line proof (["-1 0","0"]), not a
  // degenerate single-line one, so truncating actually removes real
  // content rather than this test silently no-op-ing.
  const numHoles = 2, numPigeons = 3;
  const v = (p, h) => (p - 1) * numHoles + h;
  const clauses = [];
  for (let p = 1; p <= numPigeons; p++) clauses.push(Array.from({ length: numHoles }, (_, h) => v(p, h + 1)));
  for (let h = 1; h <= numHoles; h++) {
    for (let p1 = 1; p1 <= numPigeons; p1++) {
      for (let p2 = p1 + 1; p2 <= numPigeons; p2++) clauses.push([-v(p1, h), -v(p2, h)]);
    }
  }
  const numVars = numPigeons * numHoles;
  const result = solveSat(numVars, clauses, { emitProof: true });
  assert.equal(result.sat, false);
  assert.ok(result.proof.length > 1, 'sanity: this test needs a real multi-line proof to truncate meaningfully');
  const truncated = result.proof.slice(0, -1);
  const check = checkRupProof(numVars, clauses, truncated);
  assert.equal(check.valid, false);
  assert.match(check.reason, /not the empty clause/);
});

test('integration: emitProof defaults to off -- no proof field, and behavior is otherwise byte-for-byte identical to before this upgrade', () => {
  const clauses = [[1], [-1]];
  const result = solveSat(1, clauses);
  assert.equal(result.sat, false);
  assert.equal('proof' in result, false);
});

test('integration: a real SATISFIABLE instance with emitProof:true carries no proof field at all (there is nothing to certify -- SAT results are already self-checking via the model)', () => {
  const result = solveSat(1, [[1]], { emitProof: true });
  assert.equal(result.sat, true);
  assert.equal('proof' in result, false);
});
