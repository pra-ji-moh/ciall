// satKernel validation. The stakes are asymmetric: a wrong SAT verdict
// is caught the moment anyone checks the witness, but a wrong UNSAT is a
// FALSE PROOF OF IMPOSSIBILITY with nothing to check against. So the
// central test here is exhaustive cross-validation: for every small
// instance, the solver's verdict must agree with brute force over all
// 2^n assignments, both ways, on hundreds of generated instances.

import test from 'node:test';
import assert from 'node:assert/strict';
import { solveSat, verifyModel, firstUnsatisfied, atMostK, atLeastK, boundedSignedSum, boundedRunningSum } from '../src/lib/satKernel.js';
import { normalizeCombinatorialSpec, executeCombinatorialSearch, compileToCnf, buildCombinatorialPrompt } from '../src/lib/combinatorialSearch.js';

// Brute force over 2^n; the reference the solver is judged against.
function bruteForce(n, clauses) {
  for (let mask = 0; mask < (1 << n); mask++) {
    const model = [];
    for (let v = 1; v <= n; v++) model.push((mask >> (v - 1)) & 1 ? v : -v);
    if (verifyModel(clauses, model)) return model;
  }
  return null;
}

// Deterministic PRNG so a failure is reproducible from the seed alone.
function mulberry32(seed) {
  let a = seed >>> 0;
  return () => {
    a |= 0; a = (a + 0x6D2B79F5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

test('trivial instances behave', () => {
  assert.equal(solveSat(1, [[1]]).sat, true);
  assert.equal(solveSat(1, [[1], [-1]]).sat, false);
  assert.equal(solveSat(2, []).sat, true);
  assert.equal(solveSat(1, [[]]).sat, false); // empty clause is unsatisfiable
});

test('a SAT model is always verified before it is returned', () => {
  const clauses = [[1, 2, -3], [-1, 3], [-2, 3], [1, -2]];
  const r = solveSat(3, clauses);
  assert.equal(r.sat, true);
  assert.equal(firstUnsatisfied(clauses, r.model), null);
});

test('EXHAUSTIVE CROSS-CHECK: 400 random instances, solver vs brute force', () => {
  const rand = mulberry32(12345);
  let sat = 0, unsat = 0;
  for (let iter = 0; iter < 400; iter++) {
    const n = 3 + Math.floor(rand() * 6);          // 3..8 vars
    const m = 2 + Math.floor(rand() * (n * 4));    // enough to straddle the threshold
    const clauses = [];
    for (let i = 0; i < m; i++) {
      const width = 1 + Math.floor(rand() * 3);
      const c = [];
      for (let k = 0; k < width; k++) {
        const v = 1 + Math.floor(rand() * n);
        c.push(rand() < 0.5 ? v : -v);
      }
      clauses.push(c);
    }
    const mine = solveSat(n, clauses);
    const truth = bruteForce(n, clauses);
    assert.notEqual(mine.sat, null, 'no budget exhaustion on tiny instances');
    assert.equal(
      mine.sat, truth !== null,
      `disagreement at iter ${iter}: solver said ${mine.sat}, brute force said ${truth !== null}\n${JSON.stringify(clauses)}`,
    );
    if (mine.sat) { assert.equal(firstUnsatisfied(clauses, mine.model), null); sat++; } else unsat++;
  }
  // The generator must actually produce both verdicts or the test proves
  // nothing about UNSAT, which is the dangerous direction.
  assert.ok(sat > 40, `only ${sat} SAT instances`);
  assert.ok(unsat > 40, `only ${unsat} UNSAT instances`);
});

test('pigeonhole PHP(n+1, n) is UNSAT, and PHP(n, n) is SAT', () => {
  // n+1 pigeons into n holes: unsatisfiable, and famously hard for
  // resolution, so it exercises conflict analysis rather than luck.
  const php = (pigeons, holes) => {
    const v = (p, h) => p * holes + h + 1;
    const clauses = [];
    for (let p = 0; p < pigeons; p++) {
      const row = [];
      for (let h = 0; h < holes; h++) row.push(v(p, h));
      clauses.push(row);                       // every pigeon gets a hole
    }
    for (let h = 0; h < holes; h++) {
      for (let p1 = 0; p1 < pigeons; p1++) {
        for (let p2 = p1 + 1; p2 < pigeons; p2++) {
          clauses.push([-v(p1, h), -v(p2, h)]); // no hole takes two
        }
      }
    }
    return { numVars: pigeons * holes, clauses };
  };
  for (const n of [3, 4, 5]) {
    const bad = php(n + 1, n);
    assert.equal(solveSat(bad.numVars, bad.clauses).sat, false, `PHP(${n + 1},${n}) must be UNSAT`);
    const good = php(n, n);
    assert.equal(solveSat(good.numVars, good.clauses).sat, true, `PHP(${n},${n}) must be SAT`);
  }
});

test('atMostK encodes exactly the right models', () => {
  for (let n = 2; n <= 5; n++) {
    for (let k = 0; k <= n; k++) {
      const lits = Array.from({ length: n }, (_, i) => i + 1);
      const nextVar = { v: n };
      const clauses = [];
      atMostK(lits, k, nextVar, clauses);
      // Every assignment of the ORIGINAL vars with <= k true must extend
      // to a model, and every one with > k true must not.
      for (let mask = 0; mask < (1 << n); mask++) {
        const trueCount = [...Array(n).keys()].filter((i) => (mask >> i) & 1).length;
        const forced = [...Array(n).keys()].map((i) => ((mask >> i) & 1 ? i + 1 : -(i + 1)));
        const r = solveSat(nextVar.v, [...clauses, ...forced.map((l) => [l])]);
        assert.equal(r.sat, trueCount <= k, `n=${n} k=${k} mask=${mask} count=${trueCount}`);
      }
    }
  }
});

test('atLeastK and boundedSignedSum agree with direct counting', () => {
  const n = 5;
  for (let k = 0; k <= n; k++) {
    const lits = Array.from({ length: n }, (_, i) => i + 1);
    const nextVar = { v: n };
    const clauses = [];
    atLeastK(lits, k, nextVar, clauses);
    for (let mask = 0; mask < (1 << n); mask++) {
      const trueCount = [...Array(n).keys()].filter((i) => (mask >> i) & 1).length;
      const forced = [...Array(n).keys()].map((i) => [((mask >> i) & 1 ? i + 1 : -(i + 1))]);
      assert.equal(solveSat(nextVar.v, [...clauses, ...forced]).sat, trueCount >= k);
    }
  }
  // signed sum: true = +1, false = -1, |sum| <= C
  for (const C of [0, 1, 2, 3]) {
    const lits = [1, 2, 3, 4];
    const nextVar = { v: 4 };
    const clauses = [];
    boundedSignedSum(lits, C, nextVar, clauses);
    for (let mask = 0; mask < 16; mask++) {
      const t = [...Array(4).keys()].filter((i) => (mask >> i) & 1).length;
      const sum = 2 * t - 4;
      const forced = [...Array(4).keys()].map((i) => [((mask >> i) & 1 ? i + 1 : -(i + 1))]);
      assert.equal(solveSat(nextVar.v, [...clauses, ...forced]).sat, Math.abs(sum) <= C, `C=${C} sum=${sum}`);
    }
  }
});

test('the solver is deterministic; identical input, identical counters', () => {
  const clauses = [[1, 2], [-1, 3], [-2, -3], [1, -3], [2, 3]];
  const a = solveSat(3, clauses);
  const b = solveSat(3, clauses);
  assert.deepEqual(a, b);
});

// ── The instrument's reason for existing ────────────────────────────
// Erdos discrepancy, finite version. Exhaustive enumeration dies around
// N=45; SAT settles the same questions by inference. Ground truth: the
// longest +/-1 sequence with discrepancy <= 1 has length 11.

function discrepancyInstance(N, C) {
  // Variable i (1..N) true means f(i) = +1.
  const nextVar = { v: N };
  const clauses = [];
  for (let d = 1; d <= N; d++) {
    const hap = [];
    for (let k = 1; k * d <= N; k++) {
      hap.push(k * d);
      if (hap.length > C) boundedSignedSum(hap.slice(), C, nextVar, clauses);
    }
  }
  return { numVars: nextVar.v, clauses };
}

test('ERDOS DISCREPANCY: SAT reproduces the known threshold of 11', () => {
  for (const N of [9, 10, 11]) {
    const { numVars, clauses } = discrepancyInstance(N, 1);
    const r = solveSat(numVars, clauses);
    assert.equal(r.sat, true, `N=${N} should admit a discrepancy-1 sequence`);
  }
  const { numVars, clauses } = discrepancyInstance(12, 1);
  const r = solveSat(numVars, clauses);
  // This is the load-bearing assertion: a PROOF that no sequence of
  // length 12 has discrepancy 1, not a failure to find one.
  assert.equal(r.sat, false, 'N=12 must be proved impossible');
});

test('a discrepancy witness really has the claimed discrepancy', () => {
  const N = 11;
  const { numVars, clauses } = discrepancyInstance(N, 1);
  const r = solveSat(numVars, clauses);
  assert.equal(r.sat, true);
  const f = [];
  for (let i = 1; i <= N; i++) f.push(r.model.includes(i) ? 1 : -1);
  let worst = 0;
  for (let d = 1; d <= N; d++) {
    let sum = 0;
    for (let k = 1; k * d <= N; k++) { sum += f[k * d - 1]; worst = Math.max(worst, Math.abs(sum)); }
  }
  assert.equal(worst, 1, `witness has discrepancy ${worst}, expected 1`);
});

test('boundedRunningSum matches direct prefix-sum checking, exhaustively', () => {
  for (let m = 1; m <= 8; m++) {
    for (const C of [1, 2, 3]) {
      const lits = Array.from({ length: m }, (_, i) => i + 1);
      const nextVar = { v: m };
      const clauses = [];
      boundedRunningSum(lits, C, nextVar, clauses);
      for (let mask = 0; mask < (1 << m); mask++) {
        let sum = 0, ok = true;
        for (let i = 0; i < m; i++) { sum += ((mask >> i) & 1) ? 1 : -1; if (Math.abs(sum) > C) { ok = false; break; } }
        const forced = [...Array(m).keys()].map((i) => [((mask >> i) & 1 ? i + 1 : -(i + 1))]);
        assert.equal(solveSat(nextVar.v, [...clauses, ...forced]).sat, ok, `m=${m} C=${C} mask=${mask}`);
      }
    }
  }
});

test('the efficient encoding reproduces the discrepancy threshold of 11', () => {
  const inst = (N, C) => {
    const nextVar = { v: N };
    const clauses = [];
    for (let d = 1; d <= N; d++) {
      const hap = [];
      for (let k = 1; k * d <= N; k++) hap.push(k * d);
      if (hap.length > C) boundedRunningSum(hap, C, nextVar, clauses);
    }
    return { numVars: nextVar.v, clauses };
  };
  const a = inst(11, 1);
  assert.equal(solveSat(a.numVars, a.clauses).sat, true);
  const b = inst(12, 1);
  assert.equal(solveSat(b.numVars, b.clauses).sat, false, 'N=12 proved impossible by the efficient encoding too');
});

// ── The instrument wrapper ──────────────────────────────────────────

test('a non-existence claim is CONFIRMED by proof, not by failed search', () => {
  // "No +/-1 sequence of length 12 has discrepancy 1" - true.
  const constraints = [];
  for (let d = 1; d <= 12; d++) {
    const hap = [];
    for (let k = 1; k * d <= 12; k++) hap.push(k * d);
    if (hap.length > 1) constraints.push({ type: 'boundedRunningSum', lits: hap, C: 1 });
  }
  const spec = normalizeCombinatorialSpec({
    kind: 'combinatorial_search', n: 12, claim: 'not-exists',
    meaning: 'variable i true means f(i)=+1', constraints,
  });
  const out = executeCombinatorialSearch(spec);
  assert.equal(out.verdict, 'claim-confirmed');
  assert.equal(out.satisfiable, false);
  assert.ok(/PROVED IMPOSSIBLE/.test(out.honesty));
  assert.ok(/not a failed search/.test(out.honesty));
});

test('upgrade 16: the SAME non-existence claim, with emitProof:true, carries a real DRAT proof that is independently re-verified before the verdict is trusted', () => {
  const constraints = [];
  for (let d = 1; d <= 12; d++) {
    const hap = [];
    for (let k = 1; k * d <= 12; k++) hap.push(k * d);
    if (hap.length > 1) constraints.push({ type: 'boundedRunningSum', lits: hap, C: 1 });
  }
  const spec = normalizeCombinatorialSpec({
    kind: 'combinatorial_search', n: 12, claim: 'not-exists',
    meaning: 'variable i true means f(i)=+1', constraints,
  });
  const out = executeCombinatorialSearch(spec, { emitProof: true });
  assert.equal(out.verdict, 'claim-confirmed');
  assert.equal(out.proofIndependentlyVerified, true);
  assert.ok(Array.isArray(out.proof) && out.proof.length > 0);
  assert.equal(out.proof[out.proof.length - 1], '0');
  assert.match(out.honesty, /INDEPENDENTLY VERIFIED/);
});

test('upgrade 16: WITHOUT emitProof (the default), no proof field appears -- byte-for-byte the same result shape as before this upgrade', () => {
  const constraints = [];
  for (let d = 1; d <= 12; d++) {
    const hap = [];
    for (let k = 1; k * d <= 12; k++) hap.push(k * d);
    if (hap.length > 1) constraints.push({ type: 'boundedRunningSum', lits: hap, C: 1 });
  }
  const spec = normalizeCombinatorialSpec({
    kind: 'combinatorial_search', n: 12, claim: 'not-exists',
    meaning: 'variable i true means f(i)=+1', constraints,
  });
  const out = executeCombinatorialSearch(spec);
  assert.equal('proof' in out, false);
  assert.equal('proofIndependentlyVerified' in out, false);
});

test('the same claim at length 11 is REFUTED with an explicit witness', () => {
  const constraints = [];
  for (let d = 1; d <= 11; d++) {
    const hap = [];
    for (let k = 1; k * d <= 11; k++) hap.push(k * d);
    if (hap.length > 1) constraints.push({ type: 'boundedRunningSum', lits: hap, C: 1 });
  }
  const spec = normalizeCombinatorialSpec({
    kind: 'combinatorial_search', n: 11, claim: 'not-exists',
    meaning: 'variable i true means f(i)=+1', constraints,
  });
  const out = executeCombinatorialSearch(spec);
  assert.equal(out.verdict, 'claim-refuted');
  assert.equal(out.witness.length, 11, 'witness covers the problem variables only, no encoding scaffolding');
  // The witness must genuinely have discrepancy 1.
  const f = [];
  for (let i = 1; i <= 11; i++) f.push(out.witness.includes(i) ? 1 : -1);
  let worst = 0;
  for (let d = 1; d <= 11; d++) {
    let s = 0;
    for (let k = 1; k * d <= 11; k++) { s += f[k * d - 1]; worst = Math.max(worst, Math.abs(s)); }
  }
  assert.equal(worst, 1);
});

test('budget exhaustion is UNDECIDED, never a verdict', () => {
  // A hard instance with a punishing budget: must refuse to conclude.
  const constraints = [];
  for (let d = 1; d <= 900; d++) {
    const hap = [];
    for (let k = 1; k * d <= 900; k++) hap.push(k * d);
    if (hap.length > 2) constraints.push({ type: 'boundedRunningSum', lits: hap, C: 2 });
  }
  const spec = normalizeCombinatorialSpec({
    kind: 'combinatorial_search', n: 900, claim: 'exists',
    meaning: 'variable i true means f(i)=+1', constraints,
  });
  const out = executeCombinatorialSearch(spec, { maxMs: 700, maxConflicts: 400 });
  assert.equal(out.verdict, 'undecided');
  assert.ok(/NOT evidence either way/.test(out.honesty));
  assert.ok(out.satisfiable === undefined, 'undecided must not carry a satisfiability claim');
});

test('malformed combinatorial specs fail loudly, never silently shrink', () => {
  assert.throws(() => normalizeCombinatorialSpec({ kind: 'combinatorial_search', n: 5, constraints: [{ type: 'clause', lits: [9] }] }), /out of range/);
  assert.throws(() => normalizeCombinatorialSpec({ kind: 'combinatorial_search', n: 5, constraints: [] }), /At least one constraint/);
  assert.throws(() => normalizeCombinatorialSpec({ kind: 'combinatorial_search', n: 5, constraints: [{ type: 'nonsense', lits: [1] }] }), /unknown constraint type/);
  assert.equal(normalizeCombinatorialSpec({ kind: 'none', reason: 'not discrete' }).kind, 'none');
});
