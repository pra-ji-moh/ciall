// chainKernel.js; compositional exploit-chain reachability — the gap
// between a pattern-matching scanner and the "chain reasoning" published
// frontier-model security research describes (e.g. several individually
// low-severity browser bugs chained into a sandbox escape).
//
// WHAT THIS IS NOT. This does not scan code. It does not find findings.
// It is not itself allowed to decide, by "model reasoning" or by prose
// argument, that a chain exists. It consumes findings OTHER kernels in
// this registry already independently verified (boundary-check,
// domain-of-validity, consistency, selfAudit.js, or any future kernel
// that produces a finding in the normalized shape below), and answers
// exactly one narrow, structural question: given these findings' stated
// preconditions and postconditions, does a WELL-FOUNDED firing sequence
// exist that reaches a stated target finding's trigger condition? That
// question is graph reachability, not generation, and it is decided the
// same way every other decisive instrument in this repo decides an
// existence question: compiled to CNF and handed to the from-scratch
// CDCL solver in satKernel.js, never asserted on an LLM's say-so.
//
// NEVER PRODUCED HERE: exploit code, a payload, a trigger sequence a
// human could run, or any "here is how you would actually pull this
// off" narrative. The output is which findings chain, the reachability
// graph, the proof, and a severity pulled from an explicit table — never
// more. bin/ciall.mjs's confirm(action) gate governs every file/command
// action in this repo; this kernel does not touch the filesystem, run a
// command, or act on anything at all, so it needs no gate of its own —
// same reasoning as domainOfValidity.js and boundaryKernel.js, which are
// pure functions over already-produced text/specs.
//
// THE ENCODING, and why it is NOT a naive "A's postcondition satisfies
// B's precondition, therefore chain" graph walk. A plain implication
// encoding (atomTrue(x) <=> some finding that produces x is active) is
// UNSOUND here: two findings that mutually justify each other (i needs
// what j produces, j needs what i produces) would let a SAT solver set
// both "active" with no genuine base case at all — a circular proof
// that looks exactly like a real one. That failure mode is precisely
// the thing a compositional-exploit tool must never produce, so this
// uses a BOUNDED, LAYERED (SAT-planning / Kautz-Selman style) encoding
// instead: atoms and finding-activations are duplicated per round
// 0..K (K = the number of candidate findings, a safe upper bound —
// a genuine minimal derivation never needs to fire the same finding
// twice), and a finding firing at round t may only read atoms already
// established at round t-1, strictly earlier. Layer 0 is pinned to
// exactly the stated initial atoms. This makes every derivation
// well-founded BY CONSTRUCTION: a cycle among findings cannot inflate
// satisfiability, because "round t-1" and "round t" are different
// variables and nothing at round t can ever justify round t-1.
//
// THE SAME HONEST ASYMMETRY AS satKernel.js/combinatorialSearch.js.
// SAT (a chain composes) is verified two independent ways before this
// module reports it: satKernel.js's own model-vs-every-clause check
// (built into solveSat, a bad model cannot escape it), AND a second,
// completely separate, non-SAT algorithm in this file (replayChainFixpoint,
// a plain forward-chaining fixpoint with no SAT machinery at all) that
// must independently agree the target is reached. If the two disagree,
// that is an encoding bug, and the verdict downgrades to 'inconclusive'
// rather than shipping a confident answer built on a contradiction —
// exactly the discipline combinatorialSearch.js applies to its own
// solver's UNSAT claims. UNSAT (the chain does NOT compose) is verified
// via dratProof.js's independent RUP checker (see checkChainReachabilityProof
// there): the proof-checking ALGORITHM is not forked or duplicated —
// chain-reachability CNF is still just CNF — that function is a thin,
// documented wrapper naming the context, which is what "extending
// dratProof.js's coverage to chain-reachability proofs" concretely means
// here.
//
// SEVERITY IS A TABLE LOOKUP, NEVER ARITHMETIC. Summing or averaging
// individual findings' severities would contradict the entire premise of
// chain analysis: that composition is non-linear (an unauthenticated
// boundary bypass plus a logic contradiction is not "medium + medium",
// it can be a full authentication-bypass chain). SEVERITY_RULE_TABLE
// below is an explicit, disclosed, human-authored list of kernelId
// combinations this file's author judged escalate and to what level.
// A chain whose participating kernelIds match no rule gets 'unrated',
// not a guessed default — the same "silence is not evidence" discipline
// applied to scoring instead of to existence.

import { solveSat } from './satKernel.js';
import { checkChainReachabilityProof } from './dratProof.js';

export const MAX_FINDINGS = 40;
export const MAX_ATOMS_PER_FINDING = 12;
export const MAX_TOTAL_ATOMS = 400;
const MAX_ATOM_LEN = 120;

function normAtom(a, where) {
  const s = String(a ?? '').trim();
  if (!s) throw new Error(`${where}: atom must be a non-empty string`);
  if (s.length > MAX_ATOM_LEN) throw new Error(`${where}: atom "${s.slice(0, 40)}..." exceeds ${MAX_ATOM_LEN} characters`);
  return s;
}

// ── Input normalization ─────────────────────────────────────────────
//
// Findings shape (the common form every producing kernel's output is
// translated into by its CALLER — this file does not know or care how
// boundary-check/domain-of-validity/consistency/selfAudit produced
// theirs, only that a caller has stated preconditions/postconditions
// for each):
//
//   id               unique string within this input set
//   kernelId         which kernel produced it, e.g. "boundary-check"
//   region           free-text location tag, carried through for
//                    display only — NOT used to gate composition; if a
//                    producer wants region-locality to matter, encode it
//                    directly into the atom names (e.g.
//                    "path-traversal-write@src/upload.js")
//   preconditions    string[], atoms that must already hold for this
//                    finding's weakness to be triggerable
//   postconditions   string[], atoms this finding's weakness grants
//                    once triggered
//   unauthenticated  boolean, true if THIS finding's own trigger point
//                    requires no authentication (feeds the severity
//                    rule table; informational only for the SAT proof)
//   severity         the producing kernel's own severity label,
//                    carried through for display only — NEVER summed
//                    or averaged, see this file's header
//   detail           free text, carried through for display only
export function normalizeChainInput(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Chain spec is not an object');
  const list = Array.isArray(raw.findings) ? raw.findings : [];
  if (list.length < 2) throw new Error('chainKernel needs at least 2 findings to compose a chain -- a single finding cannot chain with itself');
  if (list.length > MAX_FINDINGS) throw new Error(`Too many findings (max ${MAX_FINDINGS})`);

  const seenIds = new Set();
  const findings = list.map((f, i) => {
    const where = `findings[${i}]`;
    const id = String(f?.id ?? '').trim();
    if (!id) throw new Error(`${where}: needs a non-empty id`);
    if (seenIds.has(id)) throw new Error(`${where}: duplicate finding id "${id}"`);
    seenIds.add(id);
    const kernelId = String(f?.kernelId ?? '').trim();
    if (!kernelId) throw new Error(`${where} ("${id}"): needs a non-empty kernelId naming which kernel produced it`);
    const preconditions = Array.isArray(f?.preconditions) ? f.preconditions.map((a) => normAtom(a, `${where}.preconditions`)) : [];
    const postconditions = Array.isArray(f?.postconditions) ? f.postconditions.map((a) => normAtom(a, `${where}.postconditions`)) : [];
    if (preconditions.length > MAX_ATOMS_PER_FINDING) throw new Error(`${where} ("${id}"): too many preconditions (max ${MAX_ATOMS_PER_FINDING})`);
    if (postconditions.length > MAX_ATOMS_PER_FINDING) throw new Error(`${where} ("${id}"): too many postconditions (max ${MAX_ATOMS_PER_FINDING})`);
    return {
      id, kernelId,
      region: String(f?.region ?? '').slice(0, 200),
      preconditions, postconditions,
      unauthenticated: Boolean(f?.unauthenticated),
      severity: String(f?.severity ?? 'unrated').slice(0, 40),
      detail: String(f?.detail ?? '').slice(0, 500),
    };
  });

  const initialAtoms = Array.isArray(raw.initialAtoms)
    ? [...new Set(raw.initialAtoms.map((a) => normAtom(a, 'initialAtoms')))]
    : [];

  const targetFindingId = String(raw.targetFindingId ?? '').trim();
  if (!targetFindingId) throw new Error('chainKernel needs a targetFindingId naming which finding is the chain\'s goal');
  const target = findings.find((f) => f.id === targetFindingId);
  if (!target) throw new Error(`targetFindingId "${targetFindingId}" does not match any finding id`);
  if (target.preconditions.length === 0) {
    throw new Error(`target finding "${targetFindingId}" has no preconditions to chain into -- it is either always triggerable on its own (not a chain question) or malformed`);
  }

  const totalAtoms = new Set([
    ...initialAtoms,
    ...findings.flatMap((f) => [...f.preconditions, ...f.postconditions]),
  ]).size;
  if (totalAtoms > MAX_TOTAL_ATOMS) throw new Error(`Too many distinct atoms across findings + initialAtoms (max ${MAX_TOTAL_ATOMS})`);

  return { findings, initialAtoms, targetFindingId };
}

// ── Diagnostic graph (display only; NOT what decides the verdict) ────
//
// A plain pairwise "does A's postcondition set intersect B's
// precondition set" edge list, for a human to look at alongside the
// proof. This is deliberately NOT how reachability is decided (see this
// file's header on why a naive graph walk is unsound for cyclic finding
// sets) -- it is cheap, useful context, always computed, and reported
// even when a chain is rejected so a reviewer can see what edges the
// SAT encoding considered.
export function buildChainGraph({ findings }) {
  const nodes = findings.map((f) => ({ id: f.id, kernelId: f.kernelId, region: f.region, severity: f.severity, unauthenticated: f.unauthenticated }));
  const edges = [];
  for (const a of findings) {
    for (const b of findings) {
      if (a.id === b.id) continue;
      const atoms = a.postconditions.filter((x) => b.preconditions.includes(x));
      if (atoms.length > 0) edges.push({ from: a.id, to: b.id, atoms });
    }
  }
  return { nodes, edges };
}

// ── Independent, non-SAT verifier ─────────────────────────────────────
//
// Plain forward-chaining fixpoint (no CNF, no solver, shares no code
// with encodeChainReachability/solveSat) over the SAME findings/atoms.
// Deliberately the simplest possible correct algorithm for this
// question, so a bug in the SAT encoding cannot also be a bug here.
// Bounded the same way consistencyKernel.js's own forward-chaining is
// bounded: reachability over a monotone (no-retraction) atom set
// converges in at most one round per remaining finding.
export function replayChainFixpoint(spec) {
  const atoms = new Set(spec.initialAtoms);
  const remaining = new Map(spec.findings.filter((f) => f.id !== spec.targetFindingId).map((f) => [f.id, f]));
  const order = [];
  const maxRounds = spec.findings.length + 2;
  let rounds = 0;
  let changed = true;
  while (changed && remaining.size > 0) {
    if (rounds++ > maxRounds) throw new Error(`replayChainFixpoint: exceeded ${maxRounds} rounds -- refusing to loop unboundedly`);
    changed = false;
    for (const [id, f] of remaining) {
      if (f.preconditions.every((p) => atoms.has(p))) {
        for (const q of f.postconditions) atoms.add(q);
        order.push(id);
        remaining.delete(id);
        changed = true;
      }
    }
  }
  const target = spec.findings.find((f) => f.id === spec.targetFindingId);
  const reachable = target.preconditions.every((p) => atoms.has(p));
  return { reachable, order, finalAtoms: [...atoms] };
}

// ── The SAT encoding ───────────────────────────────────────────────────
//
// See this file's header for why this is layered/bounded rather than a
// flat implication graph. Returns the raw CNF plus the variable maps
// needed to read a witness back out.
export function encodeChainReachability(spec) {
  const atoms = [...new Set([
    ...spec.initialAtoms,
    ...spec.findings.flatMap((f) => [...f.preconditions, ...f.postconditions]),
  ])];
  const K = spec.findings.length; // safe upper bound: a minimal well-founded derivation never fires a finding twice

  let nextVar = 0;
  const alloc = () => ++nextVar;
  const atomVar = new Map();   // `${atom}|${t}` -> varId, t = 0..K
  const activeVar = new Map(); // `${findingId}|${t}` -> varId, t = 1..K

  for (const atom of atoms) for (let t = 0; t <= K; t++) atomVar.set(`${atom}|${t}`, alloc());
  for (const f of spec.findings) for (let t = 1; t <= K; t++) activeVar.set(`${f.id}|${t}`, alloc());

  const clauses = [];
  const initialSet = new Set(spec.initialAtoms);

  // Layer 0 is pinned exactly to the stated initial atoms -- no
  // variable at round 0 is free, which is what makes round 0 a genuine
  // base case rather than something a solver could set arbitrarily.
  for (const atom of atoms) {
    const v = atomVar.get(`${atom}|0`);
    clauses.push(initialSet.has(atom) ? [v] : [-v]);
  }

  const producers = new Map(atoms.map((atom) => [atom, spec.findings.filter((f) => f.postconditions.includes(atom)).map((f) => f.id)]));

  for (let t = 1; t <= K; t++) {
    for (const f of spec.findings) {
      const av = activeVar.get(`${f.id}|${t}`);
      // Firing at round t may only read round t-1 -- strictly earlier,
      // never its own or a later round's output. This is the actual
      // mechanism that rules out circular self-support.
      for (const p of f.preconditions) clauses.push([-av, atomVar.get(`${p}|${t - 1}`)]);
      // Firing at round t writes round t's atoms (its effects).
      for (const q of f.postconditions) clauses.push([-av, atomVar.get(`${q}|${t}`)]);
    }
    for (const atom of atoms) {
      const vPrev = atomVar.get(`${atom}|${t - 1}`);
      const vCur = atomVar.get(`${atom}|${t}`);
      clauses.push([-vPrev, vCur]); // monotonic: once true, stays true
      const producedBy = producers.get(atom).map((id) => activeVar.get(`${id}|${t}`));
      clauses.push([-vCur, vPrev, ...producedBy]); // true at t only if already true, or justified by something firing AT t
    }
  }

  // The existence question itself: force the target finding's
  // preconditions to be satisfiable by the final round.
  clauses.push([activeVar.get(`${spec.targetFindingId}|${K}`)]);

  return { numVars: nextVar, clauses, atomVar, activeVar, atoms, K };
}

// ── Severity rule table ─────────────────────────────────────────────
//
// Every rule is checked in order; the FIRST match wins. `match(ctx)`
// sees only the kernelIds actually present in the proven chain and
// whether any participating finding is unauthenticated -- nothing else,
// so a rule can never depend on prose the model wrote. Extend this
// table deliberately, with a name and a one-line reason, exactly like
// every other reviewed whitelist in this repo (see selfAudit.js's
// EVAL_WHITELIST etc. for the same "a new case needs an explicit,
// on-purpose entry" discipline).
export const SEVERITY_RULE_TABLE = [
  {
    id: 'unauth-boundary-bypass-plus-consistency-contradiction',
    description: 'A device/path boundary-check bypass chains with a logical consistency contradiction, and at least one finding in the chain sits on an unauthenticated code path.',
    match: (ctx) => ctx.kernelIds.has('boundary-check') && ctx.kernelIds.has('consistency') && ctx.anyUnauthenticated,
    severity: 'critical',
  },
  {
    id: 'unauth-boundary-bypass-plus-domain-narrowing',
    description: 'A boundary-check bypass chains with a domain-of-validity narrowing (the narrowed-but-still-real surviving region becomes reachable) on an unauthenticated code path.',
    match: (ctx) => ctx.kernelIds.has('boundary-check') && ctx.kernelIds.has('domain-of-validity') && ctx.anyUnauthenticated,
    severity: 'critical',
  },
  {
    id: 'self-audit-credential-or-tls-plus-boundary-bypass',
    description: 'A selfAudit.js finding (hardcoded-credential, tls-verification-disabled, weak-crypto-algorithm, or similar) chains with a boundary-check bypass.',
    match: (ctx) => ctx.kernelIds.has('self-audit') && ctx.kernelIds.has('boundary-check'),
    severity: 'critical',
  },
  {
    id: 'boundary-bypass-plus-consistency-authenticated-only',
    description: 'Boundary-check bypass chains with a consistency contradiction, but every participating finding requires authentication.',
    match: (ctx) => ctx.kernelIds.has('boundary-check') && ctx.kernelIds.has('consistency') && !ctx.anyUnauthenticated,
    severity: 'high',
  },
  {
    id: 'boundary-bypass-alone-in-chain',
    description: 'A boundary-check bypass participates in a proven chain with no consistency, domain-of-validity, or self-audit finding alongside it.',
    match: (ctx) => ctx.kernelIds.has('boundary-check') && ctx.kernelIds.size >= 2 && !ctx.kernelIds.has('consistency') && !ctx.kernelIds.has('domain-of-validity') && !ctx.kernelIds.has('self-audit'),
    severity: 'high',
  },
  {
    id: 'domain-narrowing-plus-consistency-only',
    description: 'A domain-of-validity narrowing chains with a consistency contradiction, with no boundary-check or self-audit finding involved.',
    match: (ctx) => ctx.kernelIds.has('domain-of-validity') && ctx.kernelIds.has('consistency') && !ctx.kernelIds.has('boundary-check') && !ctx.kernelIds.has('self-audit'),
    severity: 'medium',
  },
];

export function classifyChainSeverity(chainFindings) {
  const kernelIds = new Set(chainFindings.map((f) => f.kernelId));
  const anyUnauthenticated = chainFindings.some((f) => f.unauthenticated);
  const ctx = { kernelIds, anyUnauthenticated };
  for (const rule of SEVERITY_RULE_TABLE) {
    if (rule.match(ctx)) return { severity: rule.severity, ruleId: rule.id, honesty: `Matched rule "${rule.id}": ${rule.description}` };
  }
  return {
    severity: 'unrated',
    ruleId: null,
    honesty: `No rule in SEVERITY_RULE_TABLE matches this chain's kernel combination (${[...kernelIds].sort().join('+')}${anyUnauthenticated ? ', unauthenticated' : ''}) -- severity is deliberately left unrated rather than guessed. Extend SEVERITY_RULE_TABLE with an explicit, named rule before this chain can be scored.`,
  };
}

// ── Top-level entry point ─────────────────────────────────────────────
//
// spec is already-normalized (normalizeChainInput). Registered in
// kernelRegistry.js as this kernel's `run`.
export function proveChainReachability(spec, opts = {}) {
  const graph = buildChainGraph(spec);
  const replay = replayChainFixpoint(spec);
  const encoded = encodeChainReachability(spec);

  const r = solveSat(encoded.numVars, encoded.clauses, {
    maxMs: opts.maxMs ?? 8000,
    maxConflicts: opts.maxConflicts ?? 1_500_000,
    emitProof: true,
  });

  if (r.sat === null) {
    return {
      verdict: 'undecided',
      graph, conflicts: r.conflicts,
      honesty: `The SAT solver hit its budget after ${r.conflicts} conflicts without settling whether this chain composes. This is NOT evidence either way: nothing was found and nothing was ruled out.`,
    };
  }

  if (r.sat === true) {
    // Independent, non-SAT cross-check -- see this file's header. Both
    // must agree before a chain-verified verdict is ever returned.
    if (!replay.reachable) {
      return {
        verdict: 'inconclusive',
        graph, replay,
        satWitnessFound: true,
        reason: 'the SAT encoding reports the chain composes, but the independent fixpoint replay (a structurally separate algorithm) disagrees -- this indicates an encoding bug, so the chain-verified verdict is withheld rather than trusted',
      };
    }

    const isActive = (findingId) => {
      for (let t = 1; t <= encoded.K; t++) {
        const v = encoded.activeVar.get(`${findingId}|${t}`);
        if (r.model[v - 1] === v) return true;
      }
      return false;
    };
    const chain = spec.findings.filter((f) => isActive(f.id));
    const severity = classifyChainSeverity(chain);
    const order = replay.order.filter((id) => chain.some((c) => c.id === id)).concat([spec.targetFindingId]);

    return {
      verdict: 'chain-verified',
      graph,
      chain: chain.map((f) => f.id),
      order,
      severity: severity.severity,
      severityRuleId: severity.ruleId,
      severityHonesty: severity.honesty,
      size: { variables: encoded.numVars, clauses: encoded.clauses.length },
      conflicts: r.conflicts,
      honesty: 'PROVEN COMPOSABLE, two independent ways. A satisfying assignment was found for the bounded, well-founded reachability encoding (a finding firing at round t may only read atoms established at the STRICTLY earlier round t-1, so no finding can bootstrap off its own or a later finding\'s effect -- see this file\'s header), and a second, completely separate, non-SAT fixpoint replay independently confirms the same target is reached. No PoC, payload, or trigger sequence is included or was constructed; a human security researcher decides what, if anything, to do with this.',
    };
  }

  // r.sat === false: UNSAT. Independently re-checked via dratProof.js
  // BEFORE this function commits to a rejection verdict at all -- same
  // discipline as combinatorialSearch.js.
  const check = checkChainReachabilityProof(encoded.numVars, encoded.clauses, r.proof, { targetFindingId: spec.targetFindingId });
  if (!check.valid) {
    return {
      verdict: 'inconclusive',
      graph, conflicts: r.conflicts,
      reason: `the solver reported this chain does NOT compose, but its own emitted proof FAILED independent verification (${check.reason}) -- the rejection is withheld rather than trusted`,
    };
  }
  return {
    verdict: 'chain-rejected',
    graph, replay,
    size: { variables: encoded.numVars, clauses: encoded.clauses.length },
    conflicts: r.conflicts,
    proof: r.proof,
    proofIndependentlyVerified: true,
    honesty: 'PROVEN NOT TO COMPOSE, AND INDEPENDENTLY VERIFIED. A complete search over the bounded reachability encoding established by resolution that no well-founded firing sequence reaches the target finding\'s preconditions from the stated findings and initial atoms -- a genuine proof of non-composition, not a failed search -- and dratProof.js\'s from-scratch RUP checker (sharing no code with the solver that produced this) independently replayed the proof and confirmed it.',
  };
}
