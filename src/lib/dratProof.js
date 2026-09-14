// dratProof.js; upgrade 16 — an INDEPENDENT DRAT/RUP proof checker for
// satKernel.js's UNSAT certificates. This file shares zero code with
// satKernel.js: a different, deliberately un-optimized unit-propagation
// algorithm (a plain fixpoint scan over every clause, not the solver's
// own two-watched-literal scheme), written from scratch against the
// published RUP definition, not by reading satKernel.js's
// implementation and mirroring it. The point is not speed — it is that
// a bug in satKernel.js's propagate()/analyze() cannot also be a bug
// here, because this is not the same code.
//
// WHAT RUP-CHECKING PROVES, precisely. A DRAT proof is a sequence of
// clause additions; the (simpler) RUP fragment this file implements —
// sufficient for any proof produced by ordinary CDCL conflict analysis,
// which is all satKernel.js ever emits — says: clause C is a valid
// addition if negating every literal of C, then running unit
// propagation over the clauses already known (original + every
// PREVIOUSLY VALIDATED proof line), derives a conflict. This is exactly
// what conflict-driven clause learning guarantees about the clauses it
// produces, which is WHY every learnt clause is RUP-valid by
// construction — this checker verifies that guarantee actually held,
// rather than trusting the solver's own bookkeeping.
//
// A full proof certifies UNSAT when its last line is the EMPTY clause
// and every line up to and including it passes its RUP check against
// the ORIGINAL clauses plus every earlier proof line — this file checks
// exactly that, front to back, and returns which specific line failed
// if the proof is not valid, never a bare true/false.
//
// SCOPE, disclosed plainly, same discipline as certifiable-c/'s own
// header: this is a real, from-scratch, independent implementation of
// the actual RUP-checking algorithm. It is NOT the community-standard
// `drat-trim` binary (a C tool; no C compiler is available in this
// environment, the same disclosed limitation as certifiable-c/) — this
// checker's validation is genuine and independent of satKernel.js, but
// it has not been cross-checked against the literal external reference
// implementation. `writeDratFile` below produces a real, standard DIMACS
// DRAT-format file specifically so a user WITH drat-trim installed can
// independently re-verify any proof this repo produces.

const MAX_PROPAGATION_ROUNDS = 100000; // bounded, same discipline as every other loop in this repo

/**
 * Parses one DRAT proof line ("3 -5 7 0" or "0" for the empty clause)
 * into an array of signed integers (without the trailing 0).
 */
function parseProofLine(line) {
  const nums = line.trim().split(/\s+/).map(Number);
  if (nums.length === 0 || nums[nums.length - 1] !== 0 || !nums.every(Number.isInteger)) {
    throw new Error(`dratProof: malformed proof line "${line}" (expected space-separated integers ending in 0)`);
  }
  return nums.slice(0, -1);
}

/**
 * Runs unit propagation over `db` (an array of clauses, each an array
 * of signed ints) starting from `assumedLits` (signed ints assumed
 * TRUE). Returns `{conflict: true}` if propagation derives a
 * falsified clause, or `{conflict: false}` if it reaches a fixpoint
 * with no conflict. A plain, unoptimized full-rescan fixpoint loop —
 * deliberately not the solver's own watched-literal scheme (see this
 * file's header for why that independence is the point).
 */
function unitPropagateConflict(db, numVars, assumedLits) {
  const assign = new Int8Array(numVars + 1); // 0 unassigned, 1 true, -1 false, indexed by |lit|
  const queue = [];

  for (const lit of assumedLits) {
    const v = Math.abs(lit);
    const want = lit > 0 ? 1 : -1;
    if (assign[v] !== 0 && assign[v] !== want) return { conflict: true }; // assumptions clash with each other directly
    if (assign[v] === 0) { assign[v] = want; queue.push(lit); }
  }

  let rounds = 0;
  let changed = true;
  while (changed) {
    changed = false;
    if (rounds++ > MAX_PROPAGATION_ROUNDS) throw new Error(`dratProof: unit propagation exceeded ${MAX_PROPAGATION_ROUNDS} rounds -- refusing to loop unboundedly`);

    for (const clause of db) {
      let unassignedLit = null;
      let unassignedCount = 0;
      let satisfied = false;
      for (const lit of clause) {
        const v = Math.abs(lit);
        const want = lit > 0 ? 1 : -1;
        if (assign[v] === want) { satisfied = true; break; }
        if (assign[v] === 0) { unassignedCount++; unassignedLit = lit; }
      }
      if (satisfied) continue;
      if (unassignedCount === 0) return { conflict: true }; // every literal false: clause falsified
      if (unassignedCount === 1) {
        const v = Math.abs(unassignedLit);
        const want = unassignedLit > 0 ? 1 : -1;
        assign[v] = want;
        queue.push(unassignedLit);
        changed = true;
      }
    }
  }
  return { conflict: false };
}

/**
 * Checks a full DRAT/RUP proof against `originalClauses` from scratch.
 * Returns `{valid:true, linesChecked}` or `{valid:false, failedAtLine,
 * reason}` — never throws on an invalid proof (a malformed/incomplete
 * proof is a real, reportable finding, not an exceptional program
 * state); it DOES throw on malformed INPUT (a proof line that isn't
 * parseable at all), same as every normalize() in this repo.
 */
export function checkRupProof(numVars, originalClauses, proofLines) {
  if (!Number.isInteger(numVars) || numVars < 1) throw new Error('checkRupProof needs a positive integer numVars');
  if (!Array.isArray(originalClauses)) throw new Error('checkRupProof needs an array of originalClauses');
  if (!Array.isArray(proofLines) || proofLines.length === 0) throw new Error('checkRupProof needs a non-empty proofLines array');

  const db = originalClauses.map((c) => c.slice());

  for (let i = 0; i < proofLines.length; i++) {
    const clause = parseProofLine(proofLines[i]);
    const assumptions = clause.map((lit) => -lit); // RUP check: assume the NEGATION of every literal in the candidate clause
    const { conflict } = unitPropagateConflict(db, numVars, assumptions);
    if (!conflict) {
      return { valid: false, failedAtLine: i, reason: `line ${i} ("${proofLines[i]}") is not RUP-derivable from the original clauses plus every earlier proof line -- unit propagation reached a fixpoint with no conflict` };
    }
    db.push(clause);
    if (clause.length === 0 && i === proofLines.length - 1) {
      return { valid: true, linesChecked: proofLines.length };
    }
  }

  const last = parseProofLine(proofLines[proofLines.length - 1]);
  if (last.length !== 0) {
    return { valid: false, failedAtLine: proofLines.length - 1, reason: 'every line RUP-checked successfully, but the LAST line is not the empty clause -- this proof does not certify UNSAT (it may be a valid, incomplete partial proof)' };
  }
  return { valid: true, linesChecked: proofLines.length };
}

/**
 * Formats `proofLines` (from solveSat's `emitProof` option) as a real,
 * standard DIMACS DRAT proof file — plain text, one clause per line,
 * space-separated signed integers ending in 0, exactly the format the
 * external `drat-trim` reference tool consumes. No deletion lines are
 * emitted (satKernel.js never deletes learnt clauses, and DRAT proofs
 * without deletions are still fully valid, just not minimized).
 */
export function writeDratFile(proofLines) {
  if (!Array.isArray(proofLines)) throw new Error('writeDratFile needs an array of proof lines');
  return proofLines.join('\n') + '\n';
}

/**
 * chainKernel.js's "extended to cover chain-reachability proofs, not
 * just the base solver's unsat proofs" piece, concretely. This is a
 * THIN, DOCUMENTED WRAPPER around checkRupProof above -- the checking
 * ALGORITHM is not forked, duplicated, or specialized in any way,
 * because a chain-reachability CNF (encodeChainReachability's layered,
 * bounded encoding) is still just CNF and a RUP proof over it is still
 * just a RUP proof; nothing about "the clauses came from a chain
 * question instead of a combinatorial one" changes what makes a proof
 * line valid. The only things this wrapper adds are call-site
 * self-documentation and a `context` echoed back on both outcomes (e.g.
 * `{targetFindingId}`) so a caller reporting a rejected chain doesn't
 * have to re-thread that context past a bare {valid, reason} result.
 */
export function checkChainReachabilityProof(numVars, clauses, proofLines, context = {}) {
  const result = checkRupProof(numVars, clauses, proofLines);
  if (result.valid) {
    return { ...result, context, meaning: 'UNREACHABLE, independently verified: no well-founded assignment of the chain-reachability encoding satisfies the constraints with the target finding\'s preconditions forced satisfiable.' };
  }
  return { ...result, context, meaning: 'the solver\'s UNSAT claim for this chain-reachability question FAILED independent verification -- withhold the "chain-rejected" verdict.' };
}
