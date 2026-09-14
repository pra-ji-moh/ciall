// satKernel.js; a CDCL SAT solver, in plain deterministic JavaScript, on
// the user's own device.
//
// WHY THIS EXISTS, and why it is different from every other kernel here.
//
// numericCheck, mcmcSearch, dynamicsCheck and consequenceSearch all work
// the same way underneath: evaluate a relation at points, and report what
// the points show. That can FIND a counterexample, which is decisive, but
// it can never establish that no counterexample exists. Hence the
// asymmetry stamped across this codebase: a found violation is a proof, a
// passed check is weak evidence and must be reported as weak.
//
// A complete SAT solver breaks that asymmetry. When it returns UNSAT it
// has not failed to find a solution; it has PROVED there is none, by
// resolution, over the whole finite space the constraints describe. That
// is the first result in this product that can settle a negative claim,
// and it is why combinatorial existence questions ("is there a sequence
// of length N with property P?") belong here and nowhere else. Exhaustive
// enumeration dies around 2^45; this reaches instances enumeration cannot
// touch, because it prunes by inference rather than by walking.
//
// WHAT IT IS NOT. It is not a magic wand for hard combinatorics. Konev
// and Lisitsa needed serious solver time and a proof object measured in
// gigabytes to settle the Erdos discrepancy bound at length 1160. This
// solver will not do that. It is a real CDCL implementation with the
// standard machinery, and its honest range is instances of thousands of
// variables, not millions.
//
// THE SAFETY PROPERTY THAT MATTERS MOST. A wrong SAT answer would be
// caught instantly by anyone checking the witness; a wrong UNSAT would be
// a FALSE PROOF OF IMPOSSIBILITY, silently, with nothing to check. So:
//   - Every SAT result is verified against every clause before it is
//     returned. A model that does not satisfy the input cannot escape
//     this function; it throws instead.
//   - UNSAT has no such self-check available, so it is validated the only
//     honest way: the test suite cross-checks UNSAT verdicts against
//     brute-force enumeration on every instance small enough to enumerate,
//     plus structured instances (pigeonhole) whose answer is known.
//   - Every run is deterministic. Same input, same decisions, same
//     answer, bit for bit; there is no randomness anywhere in here.
//
// Literals are packed: lit = 2*v + (negated ? 1 : 0), so lit^1 negates.
// The public API speaks DIMACS-style signed integers (+3, -7) because
// that is what anything generating clauses will produce.

const UNDEF = -1;

export const SAT_BUDGET = { conflicts: 2_000_000, ms: 10_000 };

/**
 * @param {number} numVars  variables are 1..numVars
 * @param {number[][]} clauses  DIMACS signed literals, e.g. [[1,-2],[3]]
 * @param {{maxConflicts?:number, maxMs?:number, emitProof?:boolean}} [opts]
 *   `emitProof: true` additionally returns a DRAT proof (see below) on any
 *   UNSAT result -- zero behavior/performance change to the search itself
 *   when omitted (every line below only runs conditionally on the flag).
 * @returns {{sat:true, model:number[], conflicts:number, decisions:number}
 *          |{sat:false, conflicts:number, decisions:number, proof?:string[]}
 *          |{sat:null, reason:string, conflicts:number, decisions:number}}
 *
 * PROOF EMISSION, what it actually is. When `emitProof` is set and the
 * result is UNSAT, `proof` is a real DRAT (Deletion Resolution
 * Asymmetric Tautology) proof: one line per LEARNT clause, in the exact
 * order this solver derived them via conflict analysis, ending in the
 * empty clause. This is not this solver's own self-report dressed up —
 * every learnt clause a CDCL solver produces is, by construction,
 * RUP-derivable from the clauses already known (that is what conflict
 * analysis IS), so this log is a genuine, independently-checkable
 * certificate, not a summary. `src/lib/dratProof.js`'s `checkRupProof`
 * is a SEPARATE implementation (its own from-scratch unit-propagation
 * loop, sharing no code with this solver) that verifies exactly that
 * property — the same "structurally separate verifier" principle this
 * whole repo already applies to every claim it checks, here applied
 * recursively to this solver's own UNSAT claims, closing the one gap
 * this file's own header names: "UNSAT has no such self-check
 * available" (see satKernel.test.mjs's proof tests for that self-check,
 * now added).
 */
export function solveSat(numVars, clauses, opts = {}) {
  const maxConflicts = opts.maxConflicts ?? SAT_BUDGET.conflicts;
  const emitProof = Boolean(opts.emitProof);
  const proofLines = emitProof ? [] : null;
  const maxMs = opts.maxMs ?? SAT_BUDGET.ms;
  const startedAt = Date.now();

  const n = numVars;
  const nLits = 2 * (n + 1);
  const assigns = new Int8Array(n + 1).fill(UNDEF); // per var: -1 undef, 0 false, 1 true
  const level = new Int32Array(n + 1).fill(-1);
  const reason = new Int32Array(n + 1).fill(-1);    // clause index, or -1
  const activity = new Float64Array(n + 1);
  let varInc = 1.0;

  // Clause store. Original clauses first, learnt clauses appended.
  /** @type {number[][]} */
  const cs = [];
  const watches = Array.from({ length: nLits }, () => []);

  const packed = (dimacs) => (dimacs > 0 ? 2 * dimacs : 2 * -dimacs + 1);
  const litVar = (lit) => lit >> 1;
  const litSign = (lit) => lit & 1;               // 1 means negated
  const litValue = (lit) => {
    const a = assigns[litVar(lit)];
    if (a === UNDEF) return UNDEF;
    return litSign(lit) ? (a === 0 ? 1 : 0) : a;  // value of the LITERAL
  };
  const unpack = (lit) => (litSign(lit) ? -litVar(lit) : litVar(lit)); // inverse of packed(): back to a signed DIMACS integer, for proof output
  const logClause = (lits) => { if (proofLines) proofLines.push(`${lits.map(unpack).join(' ')} 0`); };
  const logEmpty = () => { if (proofLines) proofLines.push('0'); };

  const trail = [];
  const trailLim = [];
  let qhead = 0;
  let conflicts = 0;
  let decisions = 0;

  function attach(ci) {
    const c = cs[ci];
    watches[c[0]].push(ci);
    watches[c[1]].push(ci);
  }

  function enqueue(lit, from) {
    const v = litVar(lit);
    assigns[v] = litSign(lit) ? 0 : 1;
    level[v] = trailLim.length;
    reason[v] = from;
    trail.push(lit);
  }

  // Load originals. A clause containing a literal and its negation is a
  // tautology and is dropped; duplicates within a clause are removed.
  for (const raw of clauses) {
    const lits = [];
    const seen = new Set();
    let taut = false;
    for (const d of raw) {
      if (!Number.isInteger(d) || d === 0 || Math.abs(d) > n) {
        throw new Error(`Clause literal ${d} out of range for ${n} variables`);
      }
      const l = packed(d);
      if (seen.has(l ^ 1)) { taut = true; break; }
      if (!seen.has(l)) { seen.add(l); lits.push(l); }
    }
    if (taut) continue;
    if (lits.length === 0) { logEmpty(); return { sat: false, conflicts: 0, decisions: 0, ...(proofLines ? { proof: proofLines } : {}) }; } // empty clause
    if (lits.length === 1) {
      const val = litValue(lits[0]);
      if (val === 0) { logEmpty(); return { sat: false, conflicts: 0, decisions: 0, ...(proofLines ? { proof: proofLines } : {}) }; }
      if (val === UNDEF) enqueue(lits[0], -1);
      continue;
    }
    cs.push(lits);
    attach(cs.length - 1);
  }

  // Two-watched-literal unit propagation. Returns the conflicting clause
  // index, or -1.
  function propagate() {
    while (qhead < trail.length) {
      const p = trail[qhead++];          // p is now TRUE
      const falseLit = p ^ 1;            // clauses watching this are threatened
      const ws = watches[falseLit];
      let keep = 0;
      for (let i = 0; i < ws.length; i++) {
        const ci = ws[i];
        const c = cs[ci];
        // Ensure the threatened literal sits at position 1.
        if (c[0] === falseLit) { c[0] = c[1]; c[1] = falseLit; }
        // Already satisfied by the other watch: nothing to do.
        if (litValue(c[0]) === 1) { ws[keep++] = ci; continue; }
        // Find a new literal to watch.
        let found = -1;
        for (let k = 2; k < c.length; k++) {
          if (litValue(c[k]) !== 0) { found = k; break; }
        }
        if (found >= 0) {
          const t = c[1]; c[1] = c[found]; c[found] = t;
          watches[c[1]].push(ci);
          continue;                       // dropped from this watch list
        }
        ws[keep++] = ci;
        // Unit or conflicting.
        if (litValue(c[0]) === 0) {
          qhead = trail.length;
          // Carry the UNEXAMINED tail of the watch list forward BEFORE
          // truncating. Truncating first silently discarded every watch
          // after the conflicting clause, so those clauses stopped being
          // propagated and the solver returned models violating them.
          // Safe in place: keep <= i + 1 <= k, so writing ws[keep] never
          // clobbers an entry this loop has yet to read.
          for (let k = i + 1; k < ws.length; k++) ws[keep++] = ws[k];
          ws.length = keep;
          return ci;
        }
        enqueue(c[0], ci);
      }
      ws.length = keep;
    }
    return -1;
  }

  function bumpVar(v) {
    activity[v] += varInc;
    if (activity[v] > 1e100) {
      for (let i = 1; i <= n; i++) activity[i] *= 1e-100;
      varInc *= 1e-100;
    }
  }

  // First-UIP conflict analysis. Returns { learnt, backtrackLevel }.
  function analyze(confl) {
    const seen = new Uint8Array(n + 1);
    const learnt = [0]; // placeholder for the asserting literal
    let pathC = 0;
    let p = -1;
    let idx = trail.length - 1;
    let ci = confl;

    do {
      const c = cs[ci];
      for (let j = p === -1 ? 0 : 1; j < c.length; j++) {
        const q = c[j];
        const v = litVar(q);
        if (!seen[v] && level[v] > 0) {
          seen[v] = 1;
          bumpVar(v);
          if (level[v] >= trailLim.length) pathC++;
          else learnt.push(q);
        }
      }
      while (!seen[litVar(trail[idx])]) idx--;
      p = trail[idx];
      seen[litVar(p)] = 0;
      pathC--;
      ci = reason[litVar(p)];
      idx--;
    } while (pathC > 0);

    learnt[0] = p ^ 1; // the asserting literal

    let btLevel = 0;
    if (learnt.length > 1) {
      let maxI = 1;
      for (let i = 2; i < learnt.length; i++) {
        if (level[litVar(learnt[i])] > level[litVar(learnt[maxI])]) maxI = i;
      }
      const t = learnt[1]; learnt[1] = learnt[maxI]; learnt[maxI] = t;
      btLevel = level[litVar(learnt[1])];
    }
    return { learnt, btLevel };
  }

  function cancelUntil(lvl) {
    if (trailLim.length <= lvl) return;
    for (let i = trail.length - 1; i >= trailLim[lvl]; i--) {
      const v = litVar(trail[i]);
      assigns[v] = UNDEF;
      level[v] = -1;
      reason[v] = -1;
    }
    trail.length = trailLim[lvl];
    trailLim.length = lvl;
    qhead = trail.length;
  }

  // Deterministic decision: highest activity, ties broken by lowest index.
  // No randomness anywhere, so runs are reproducible bit for bit.
  function pickBranch() {
    let best = -1;
    let bestAct = -1;
    for (let v = 1; v <= n; v++) {
      if (assigns[v] !== UNDEF) continue;
      if (activity[v] > bestAct) { bestAct = activity[v]; best = v; }
    }
    return best;
  }

  // Luby restart sequence; standard, and keeps the search from sinking
  // into one region of a hard instance.
  function luby(y, x) {
    let size = 1, seq = 0;
    while (size < x + 1) { seq++; size = 2 * size + 1; }
    let xx = x;
    while (size - 1 !== xx) {
      size = (size - 1) >> 1;
      seq--;
      xx = xx % size;
    }
    return Math.pow(y, seq);
  }

  if (propagate() !== -1) { logEmpty(); return { sat: false, conflicts: 0, decisions: 0, ...(proofLines ? { proof: proofLines } : {}) }; }

  let restart = 0;
  let confBudget = 100 * luby(2, restart);
  let confSinceRestart = 0;

  for (;;) {
    const confl = propagate();
    if (confl !== -1) {
      conflicts++;
      confSinceRestart++;
      if (trailLim.length === 0) { logEmpty(); return { sat: false, conflicts, decisions, ...(proofLines ? { proof: proofLines } : {}) }; }
      const { learnt, btLevel } = analyze(confl);
      cancelUntil(btLevel);
      if (learnt.length === 1) {
        logClause(learnt);
        enqueue(learnt[0], -1);
      } else {
        logClause(learnt);
        cs.push(learnt);
        attach(cs.length - 1);
        enqueue(learnt[0], cs.length - 1);
      }
      varInc *= 1.05;

      if (conflicts >= maxConflicts || Date.now() - startedAt > maxMs) {
        return { sat: null, reason: `budget exhausted after ${conflicts} conflicts`, conflicts, decisions };
      }
      if (confSinceRestart >= confBudget) {
        restart++;
        confBudget = 100 * luby(2, restart);
        confSinceRestart = 0;
        cancelUntil(0);
      }
      continue;
    }

    const next = pickBranch();
    if (next === -1) {
      // Complete assignment. VERIFY before returning; a model that does
      // not satisfy the input must never escape this function.
      const model = [];
      for (let v = 1; v <= n; v++) model.push(assigns[v] === 1 ? v : -v);
      const bad = firstUnsatisfied(clauses, model);
      if (bad !== null) {
        throw new Error(`satKernel internal error: produced a model violating clause ${JSON.stringify(bad)}`);
      }
      return { sat: true, model, conflicts, decisions };
    }
    decisions++;
    trailLim.push(trail.length);
    enqueue(2 * next, -1); // try TRUE first, deterministically
  }
}

/** Returns the first clause the model fails, or null if all hold. */
export function firstUnsatisfied(clauses, model) {
  const val = new Map();
  for (const l of model) val.set(Math.abs(l), l > 0);
  for (const c of clauses) {
    let ok = false;
    for (const d of c) {
      const v = val.get(Math.abs(d));
      if (v === undefined) continue;
      if ((d > 0) === v) { ok = true; break; }
    }
    if (!ok) return c;
  }
  return null;
}

export function verifyModel(clauses, model) {
  return firstUnsatisfied(clauses, model) === null;
}

// ── Cardinality constraints ─────────────────────────────────────────
// Combinatorial statements are rarely pure CNF; they say "at most k of
// these", "the partial sums stay within C". The sequential-counter
// encoding below turns at-most-k into clauses in O(n*k), which is what
// makes bounded-sum problems expressible at all.
//
// `nextVar` is a counter object { v } so callers can allocate fresh
// auxiliary variables without colliding with problem variables.

export function atMostK(lits, k, nextVar, out) {
  const n = lits.length;
  if (k >= n) return;
  if (k === 0) { for (const l of lits) out.push([-l]); return; }
  // s[i][j] means "at least j+1 of the first i+1 literals are true"
  const s = [];
  for (let i = 0; i < n; i++) {
    const row = [];
    for (let j = 0; j < k; j++) row.push(++nextVar.v);
    s.push(row);
  }
  out.push([-lits[0], s[0][0]]);
  for (let j = 1; j < k; j++) out.push([-s[0][j]]);
  for (let i = 1; i < n; i++) {
    out.push([-lits[i], s[i][0]]);
    out.push([-s[i - 1][0], s[i][0]]);
    for (let j = 1; j < k; j++) {
      out.push([-lits[i], -s[i - 1][j - 1], s[i][j]]);
      out.push([-s[i - 1][j], s[i][j]]);
    }
    out.push([-lits[i], -s[i - 1][k - 1]]);
  }
}

export function atLeastK(lits, k, nextVar, out) {
  // at least k of L  <=>  at most (n-k) of ~L
  atMostK(lits.map((l) => -l), lits.length - k, nextVar, out);
}

// |sum of +/-1 values| <= C, where each literal true means +1 and false
// means -1. With m literals and t of them true, the sum is 2t - m, so
// the bound becomes (m-C)/2 <= t <= (m+C)/2.
export function boundedSignedSum(lits, C, nextVar, out) {
  const m = lits.length;
  const hi = Math.floor((m + C) / 2);
  const lo = Math.ceil((m - C) / 2);
  if (hi < m) atMostK(lits, hi, nextVar, out);
  if (lo > 0) atLeastK(lits, lo, nextVar, out);
}

// Constrains EVERY prefix sum of a +/-1 sequence to stay within [-C, C],
// in one pass. The naive route (call boundedSignedSum once per prefix)
// rebuilds a counter chain for each prefix and is quadratic: it produced
// 1.69M variables at sequence length 160 and ran the heap out at 200.
//
// This encodes the running sum as a small automaton instead. State
// variables q[i][v] mean "the partial sum after i terms equals v", for v
// in [-C, C]; the sequence literal then drives the transition. Size is
// O(len * C), independent of how many prefixes there are, because every
// prefix is a state in the same chain rather than its own constraint.
export function boundedRunningSum(lits, C, nextVar, out) {
  const width = 2 * C + 1;
  const idx = (v) => v + C;                       // state value -> 0..2C
  const m = lits.length;
  if (m === 0) return;

  // q[i] holds the state variables after i terms, i = 0..m
  const q = [];
  for (let i = 0; i <= m; i++) {
    const row = new Array(width);
    for (let j = 0; j < width; j++) row[j] = ++nextVar.v;
    q.push(row);
  }

  // Start at sum 0, and in exactly one state.
  out.push([q[0][idx(0)]]);
  for (let j = 0; j < width; j++) if (j !== idx(0)) out.push([-q[0][j]]);

  for (let i = 0; i < m; i++) {
    const lit = lits[i];
    // At least one successor state stays reachable.
    out.push(q[i + 1].slice());
    for (let v = -C; v <= C; v++) {
      const from = q[i][idx(v)];
      // literal TRUE means +1
      if (v + 1 > C) out.push([-from, -lit]);              // would overflow the bound
      else out.push([-from, -lit, q[i + 1][idx(v + 1)]]);
      // literal FALSE means -1
      if (v - 1 < -C) out.push([-from, lit]);              // would underflow
      else out.push([-from, lit, q[i + 1][idx(v - 1)]]);
    }
    // States are mutually exclusive, so a reached state pins the sum.
    for (let a = 0; a < width; a++) {
      for (let b = a + 1; b < width; b++) out.push([-q[i + 1][a], -q[i + 1][b]]);
    }
  }
}
