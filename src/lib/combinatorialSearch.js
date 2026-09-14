// combinatorialSearch.js; the instrument that can PROVE A NEGATIVE.
//
// Every other kernel in this product evaluates a relation at points. That
// finds counterexamples, which is decisive, but it can never show that no
// counterexample exists; hence the asymmetry stamped everywhere else here
// (found violation = proof, passed check = weak).
//
// This one is different in kind. It compiles a finite combinatorial
// question to CNF and hands it to the complete CDCL solver in
// satKernel.js. UNSAT from a complete solver is not "we looked and found
// nothing"; it is a resolution proof that nothing exists in the stated
// finite space. So a claim of the form "no object of size N has property
// P" becomes settleable rather than merely testable, and a claim of the
// form "such an object exists" gets an explicit witness.
//
// SCOPE, stated plainly because overreach here would be expensive. The
// finite space must be small enough to encode: thousands of variables,
// not millions. Exhaustive enumeration of +/-1 sequences dies around
// length 42; this reaches length 400 on the same problem in seconds. It
// does NOT reach the published frontier of hard combinatorics (Konev and
// Lisitsa needed industrial solvers and a proof object measured in
// gigabytes to settle the Erdos discrepancy bound at 1160). Between
// those two lines is the instrument's real range, and the result says
// which side of it a run landed on.
//
// THE THING THAT MUST NEVER HAPPEN. A wrong UNSAT is a false proof of
// impossibility with nothing to check against. Protections, in order:
//   - satKernel verifies every SAT model against every clause before
//     returning it, so a false SAT cannot escape.
//   - Its UNSAT verdicts are cross-checked against brute-force
//     enumeration on hundreds of generated instances in tests/sat.test.mjs,
//     plus pigeonhole instances whose answer is known independently.
//   - A budget-exhausted run is reported as UNDECIDED and never as
//     either verdict. "We ran out of time" and "no solution exists" are
//     different sentences and this instrument keeps them different.

import { solveSat, atMostK, atLeastK, boundedRunningSum } from './satKernel.js';
import { checkRupProof } from './dratProof.js';

const MAX_VARS = 6000;
const MAX_CONSTRAINTS = 40000;
const MAX_CLAUSES = 3_000_000;

/**
 * Spec:
 * {
 *   kind: 'combinatorial_search', note,
 *   n: 40,                        // boolean variables, 1..n
 *   meaning: 'variable i true means f(i) = +1',
 *   claim: 'exists' | 'not-exists',
 *   constraints: [
 *     { type: 'clause', lits: [1, -2, 3] },
 *     { type: 'atMostK', lits: [...], k: 2 },
 *     { type: 'atLeastK', lits: [...], k: 1 },
 *     { type: 'boundedRunningSum', lits: [...], C: 2 }
 *   ]
 * }
 */
export function normalizeCombinatorialSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Combinatorial spec is not an object');
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };
  if (raw.kind !== 'combinatorial_search') throw new Error(`Unknown combinatorial spec kind "${raw.kind}"`);

  const n = Number(raw.n);
  if (!Number.isInteger(n) || n < 1 || n > MAX_VARS) throw new Error(`n must be an integer in 1..${MAX_VARS}`);
  const claim = raw.claim === 'not-exists' ? 'not-exists' : 'exists';

  const list = Array.isArray(raw.constraints) ? raw.constraints : [];
  if (list.length === 0) throw new Error('At least one constraint is required');
  if (list.length > MAX_CONSTRAINTS) throw new Error(`Too many constraints (max ${MAX_CONSTRAINTS})`);

  const checkLits = (lits, where) => {
    if (!Array.isArray(lits) || lits.length === 0) throw new Error(`${where}: lits must be a non-empty array`);
    for (const l of lits) {
      if (!Number.isInteger(l) || l === 0 || Math.abs(l) > n) throw new Error(`${where}: literal ${l} out of range for n=${n}`);
    }
    return lits.slice();
  };

  const constraints = list.map((c, i) => {
    const where = `constraint ${i}`;
    switch (c?.type) {
      case 'clause':
        return { type: 'clause', lits: checkLits(c.lits, where) };
      case 'atMostK':
      case 'atLeastK': {
        const lits = checkLits(c.lits, where);
        const k = Number(c.k);
        if (!Number.isInteger(k) || k < 0 || k > lits.length) throw new Error(`${where}: k out of range`);
        return { type: c.type, lits, k };
      }
      case 'boundedRunningSum': {
        const lits = checkLits(c.lits, where);
        const C = Number(c.C);
        if (!Number.isInteger(C) || C < 0 || C > 64) throw new Error(`${where}: C must be an integer 0..64`);
        return { type: 'boundedRunningSum', lits, C };
      }
      default:
        throw new Error(`${where}: unknown constraint type "${c?.type}"`);
    }
  });

  return {
    kind: 'combinatorial_search',
    note: String(raw.note || '').slice(0, 200),
    meaning: String(raw.meaning || '').slice(0, 200),
    n, claim, constraints,
  };
}

/** Compiles the spec to CNF. Auxiliary variables start above n. */
export function compileToCnf(spec) {
  const nextVar = { v: spec.n };
  const clauses = [];
  for (const c of spec.constraints) {
    switch (c.type) {
      case 'clause': clauses.push(c.lits.slice()); break;
      case 'atMostK': atMostK(c.lits, c.k, nextVar, clauses); break;
      case 'atLeastK': atLeastK(c.lits, c.k, nextVar, clauses); break;
      case 'boundedRunningSum': boundedRunningSum(c.lits, c.C, nextVar, clauses); break;
      default: break;
    }
    if (clauses.length > MAX_CLAUSES) throw new Error(`Encoding exceeded ${MAX_CLAUSES} clauses; the stated instance is too large for this instrument`);
  }
  return { numVars: nextVar.v, clauses };
}

export function executeCombinatorialSearch(spec, opts = {}) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason };

  let cnf;
  try {
    cnf = compileToCnf(spec);
  } catch (err) {
    return { verdict: 'inconclusive', reason: err.message };
  }

  const wantProof = Boolean(opts.emitProof);
  const r = solveSat(cnf.numVars, cnf.clauses, { maxMs: opts.maxMs ?? 8000, maxConflicts: opts.maxConflicts ?? 1_500_000, emitProof: wantProof });
  const size = { variables: cnf.numVars, clauses: cnf.clauses.length };

  // Budget exhaustion is NOT a verdict. Reporting it as one would turn
  // "we ran out of time" into "no solution exists", which is the single
  // most damaging thing this instrument could say.
  if (r.sat === null) {
    return {
      verdict: 'undecided', size, conflicts: r.conflicts,
      honesty: `The solver hit its budget after ${r.conflicts} conflicts without settling the question. This is NOT evidence either way: nothing was found and nothing was ruled out. The instance (${size.variables} variables, ${size.clauses} clauses) is beyond what this instrument decides in the time allowed.`,
    };
  }

  if (r.sat === true) {
    // Only the problem variables mean anything; auxiliaries are encoding
    // scaffolding and would be noise in a witness a human checks.
    const witness = r.model.filter((l) => Math.abs(l) <= spec.n);
    const settled = spec.claim === 'exists';
    return {
      verdict: settled ? 'claim-confirmed' : 'claim-refuted',
      satisfiable: true,
      witness,
      size, conflicts: r.conflicts,
      honesty: settled
        ? `An object meeting every stated constraint EXISTS; the witness above is explicit and was verified against all ${size.clauses} clauses before being returned. Check it by hand against the original statement.`
        : `The claim that no such object exists is FALSE, and here is the counterexample. It was verified against all ${size.clauses} clauses. A single explicit witness settles a non-existence claim outright.`,
    };
  }

  const settled = spec.claim === 'not-exists';

  // Optional, real proof-carrying result: when requested, the DRAT
  // proof solveSat just emitted is independently re-checked RIGHT HERE
  // (via dratProof.js's from-scratch, separate RUP checker) BEFORE this
  // function commits to a verdict at all -- this is not a warning
  // bolted onto an already-decided answer. If the independent check
  // ever fails (it never should, since every learnt clause a CDCL
  // solver emits is RUP-valid by construction -- but "should never
  // happen" is exactly the complacency this repo's own discipline
  // exists to refuse), the verdict itself downgrades to 'inconclusive'
  // rather than shipping a claim-confirmed/claim-refuted verdict next
  // to a buried warning a caller could fail to check.
  if (wantProof && r.proof) {
    const check = checkRupProof(cnf.numVars, cnf.clauses, r.proof);
    if (!check.valid) {
      return {
        verdict: 'inconclusive',
        size, conflicts: r.conflicts,
        reason: `the solver reported UNSAT, but its own emitted proof FAILED independent verification (${check.reason}) -- this indicates a real bug, so the UNSAT verdict is withheld rather than trusted`,
      };
    }
    return {
      verdict: settled ? 'claim-confirmed' : 'claim-refuted',
      satisfiable: false,
      size, conflicts: r.conflicts,
      proof: r.proof,
      proofIndependentlyVerified: true,
      honesty: `PROVED IMPOSSIBLE, AND INDEPENDENTLY VERIFIED. A complete search established by resolution that no assignment satisfies the stated constraints; this is a proof of non-existence over the finite space described, not a failed search. An independent RUP checker (dratProof.js, sharing no code with the solver that produced this) replayed all ${r.proof.length} proof line(s) from scratch and confirmed the empty clause is genuinely derivable -- this is not the solver's own self-report, it is a second, separate verification. ${settled ? 'The claim of non-existence is confirmed.' : 'The claim that such an object exists is therefore refuted.'}`,
    };
  }

  return {
    verdict: settled ? 'claim-confirmed' : 'claim-refuted',
    satisfiable: false,
    size, conflicts: r.conflicts,
    honesty: `PROVED IMPOSSIBLE. A complete search established by resolution that no assignment satisfies the stated constraints; this is a proof of non-existence over the finite space described, not a failed search. It is exactly as strong as the encoding is faithful, so the constraints above are the thing to check, not the verdict. ${settled ? 'The claim of non-existence is confirmed.' : 'The claim that such an object exists is therefore refuted.'}`,
  };
}

export function buildCombinatorialPrompt(node, sourceExcerpt = '') {
  return `Design a COMBINATORIAL EXISTENCE CHECK. This instrument is different from the others: it compiles a finite question to logic and runs a complete solver, so it can PROVE that no object exists, not merely fail to find one. Use it when the claim is about the existence or non-existence of a finite discrete object: a sequence, a colouring, a partition, a set avoiding some pattern.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}
${sourceExcerpt ? `\nSOURCE MATERIAL:\n${sourceExcerpt}` : ''}

Model the object with boolean variables 1..n and say what a variable being true MEANS. Then state the constraints. Available forms:
- {"type":"clause","lits":[1,-2,3]}                    at least one of these literals holds
- {"type":"atMostK","lits":[...],"k":2}                at most k of them are true
- {"type":"atLeastK","lits":[...],"k":1}               at least k are true
- {"type":"boundedRunningSum","lits":[...],"C":2}      reading true as +1 and false as -1, EVERY prefix sum stays within [-C, C]

Set "claim" to "exists" if the source asserts such an object exists, or "not-exists" if it asserts none does. The instrument reports whether the claim is confirmed or refuted, with a witness when one exists.

Keep the instance small enough to decide: n up to a few thousand, and prefer the smallest n that genuinely tests the claim. If the honest instance is far larger than that, say so with "none" instead of encoding a version so shrunken it tests nothing.

Respond ONLY with JSON, one of:
{"kind":"combinatorial_search","note":"what is being decided","n":12,"meaning":"variable i true means f(i)=+1","claim":"not-exists","constraints":[{"type":"boundedRunningSum","lits":[1,2,3,4,5,6,7,8,9,10,11,12],"C":1},{"type":"boundedRunningSum","lits":[2,4,6,8,10,12],"C":1},{"type":"boundedRunningSum","lits":[3,6,9,12],"C":1}]}
{"kind":"none","reason":"this claim is not a finite discrete existence question, or the honest instance is far too large to encode"}`;
}
