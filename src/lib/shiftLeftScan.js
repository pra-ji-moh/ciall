// shiftLeftScan.js; the proactive half of "advanced AI accelerates both
// attacks and defenses" -- wiring selfAudit.js's pattern findings
// directly into chainKernel.js's SAT+DRAT-proven compositional
// reachability, so a codebase can be checked for a compositional risk
// BEFORE any CVE is ever filed against it, on every commit, with zero
// model calls and zero non-determinism.
//
// WHY THIS DIDN'T EXIST UNTIL NOW. chainKernel.js's own header says
// plainly: "chainKernel does not re-scan code itself -- it consumes
// findings other kernels already verified." Nothing in this repo ever
// actually produced findings in the precondition/postcondition shape
// chainKernel needs, automatically, from a real scan -- a human would
// have had to hand-author them per file, which defeats the point of a
// PROACTIVE scanner (one that runs on every commit without a human
// pre-analyzing each one first). This file is that missing adapter:
// `auditSourceTree()`'s real findings, mapped through a small, disclosed
// table into chain atoms, fed straight into the same
// `proveChainReachability` this repo already ships.
//
// THE MAPPING TABLE IS A MODELING CHOICE, NOT A DERIVATION. Nothing
// here proves that a given selfAudit finding ACTUALLY has the stated
// precondition/postcondition in any specific file -- that would require
// real data-flow analysis selfAudit.js was never built to do (see its
// own header). What IS proven, rigorously, by the existing SAT+DRAT
// machinery once the atoms are asserted, is whether the STATED atoms
// compose into a reachable chain. Read a "chain-verified" result as
// "IF these findings' preconditions/postconditions are as asserted,
// here is a proven composition" -- the modeling assumption is the thing
// to check by hand, exactly as this repo's own SEVERITY_RULE_TABLE in
// chainKernel.js already asks a human to check its rule choices.
//
// SCOPE, DELIBERATELY NARROW. Only ONE illustrative, real, documented
// composition is shipped: `unguarded-dynamic-key-assignment`/
// `prototype-mutation` (a prototype-pollution primitive) chaining into
// `unreviewed-child-process` (a real, documented attack class: a
// polluted prototype altering options merged into a later
// exec/spawn-family call -- multiple real npm CVEs have this exact
// shape). Extending this table with more gadget chains (eval-usage as a
// pollution target, dynamic-require as a gadget, etc.) is real,
// legitimate future work, deliberately not attempted here: shipping a
// large, speculative gadget catalog untested against any real chain
// would be exactly the overclaiming this repo's whole discipline exists
// to refuse.
//
// PER-FILE SCOPING, ON PURPOSE. chainKernel.js's own header says
// `region` is diagnostic only and does NOT gate composition -- "if a
// producer wants region-locality to matter, encode it directly into the
// atom names." This file does exactly that: every derived atom is
// namespaced `atomName@relativeFilePath`, so two findings in UNRELATED
// files can never compose just because they happen to share an atom
// name. Real cross-file gadget chains exist (a value flowing from file A
// into file B) but proving one would need a real call-graph -- out of
// scope, disclosed, not attempted.

import { auditSourceTree } from './selfAudit.js';
import { normalizeChainInput, proveChainReachability } from './chainKernel.js';

// The ONE generic starting capability, shared across every file in a
// scan and deliberately NOT namespaced (see scopeAtom below) -- what an
// unauthenticated external attacker can typically reach. Every
// "entry-point" finding below (one with no OTHER finding's postcondition
// as its precondition) uses exactly this atom, on purpose: a single
// shared entry atom, rather than a different bespoke one per finding
// code, is what keeps "reachable directly from the attacker's starting
// position" (see DIRECTLY-REACHABLE vs PROVEN below) a meaningful,
// consistent question across the whole table.
export const DEFAULT_INITIAL_ATOMS = ['unauthenticated-request'];

// code -> {preconditions, postconditions}, using BARE atom names --
// mapFindingToChainInput namespaces the LINKING atoms (ones that also
// appear as some entry's postcondition, e.g. prototype-polluted) per
// file, so unrelated files can never compose just by sharing a name; the
// shared entry atom (unauthenticated-request) stays bare on purpose, so
// it is satisfiable from one common initial-atom set. A code with no
// entry here is not chain-eligible at all (most selfAudit codes aren't
// -- see this file's header on why the table stays small).
export const SELF_AUDIT_TO_CHAIN_ATOMS = {
  'unguarded-dynamic-key-assignment': {
    preconditions: ['unauthenticated-request'],
    postconditions: ['prototype-polluted'],
  },
  'prototype-mutation': {
    preconditions: ['unauthenticated-request'],
    postconditions: ['prototype-polluted'],
  },
  'unreviewed-child-process': {
    // A REAL, documented attack class: prototype pollution reaching an
    // options object later merged into an exec/spawn-family call can
    // inject flags/env that change what actually executes. Deliberately
    // requires prototype-polluted, NOT unauthenticated-request directly
    // -- selfAudit alone cannot tell whether an argument reaches exec
    // unsanitized (see selfAudit.js's own disclosed limitation on this
    // rule), so this table only asserts the ONE composition it can
    // actually justify: pollution enabling it, not direct injection.
    preconditions: ['prototype-polluted'],
    postconditions: ['arbitrary-command-execution'],
  },
  'hardcoded-credential': {
    preconditions: [],
    postconditions: ['credential-exposed'],
  },
  'tls-verification-disabled': {
    preconditions: ['unauthenticated-request'],
    postconditions: ['traffic-intercepted', 'credential-exposed'],
  },
};

// Every atom that appears as SOME code's postcondition is a "linking"
// atom -- a file-specific artifact one finding produces and another
// consumes, which must be namespaced per file so unrelated files can
// never compose just because they share an atom name (see this file's
// header). An atom that never appears as anyone's postcondition is a
// generic starting capability instead (e.g. attacker-controlled-object-key),
// satisfied directly from DEFAULT_INITIAL_ATOMS, and stays un-namespaced
// on purpose -- namespacing it would make it unreachable from a shared,
// generic initial-atom set.
const LINKING_ATOMS = new Set(Object.values(SELF_AUDIT_TO_CHAIN_ATOMS).flatMap((a) => a.postconditions));

function namespaced(atom, file) {
  return `${atom}@${file || '(unknown file)'}`;
}

function scopeAtom(atom, file) {
  return LINKING_ATOMS.has(atom) ? namespaced(atom, file) : atom;
}

/**
 * Converts one selfAudit finding into a chainKernel-shaped finding, or
 * null if this finding's code has no entry in SELF_AUDIT_TO_CHAIN_ATOMS
 * (the common case -- most codes never compose with anything).
 */
export function mapFindingToChainInput(finding, index) {
  const atoms = SELF_AUDIT_TO_CHAIN_ATOMS[finding.code];
  if (!atoms) return null;
  const file = finding.file || '';
  return {
    id: `${file}#${finding.code}#${index}`,
    kernelId: 'self-audit',
    region: file,
    preconditions: atoms.preconditions.map((a) => scopeAtom(a, file)),
    postconditions: atoms.postconditions.map((a) => scopeAtom(a, file)),
    // selfAudit is a pattern scan with no notion of authentication
    // context at all -- conservatively asserting every chain-eligible
    // finding as reachable without authentication is the FAIL-CLOSED
    // choice (never silently assumes a code path is safer than it
    // might be), disclosed here rather than left as an implicit default.
    unauthenticated: true,
    severity: finding.severity,
    detail: finding.detail,
  };
}

export function buildChainEligibleFindings(auditResult) {
  return auditResult.findings.map(mapFindingToChainInput).filter(Boolean);
}

// chainKernel.js's `chain`/`order` fields list every finding ACTIVE in
// A satisfying SAT assignment -- correct, but not necessarily MINIMAL
// (proveChainReachability finds a witness, not the smallest one). Found
// empirically running this against this repo's own test fixtures:
// several always-trivially-active findings (a bare hardcoded-credential
// with no precondition, or a finding whose only precondition is the
// shared entry atom) end up "free-riding" in the reported chain with no
// real causal bearing on the target, making a chain LOOK bigger and
// scarier than it is. This does a real backward trace over the
// diagnostic graph instead -- for display only; it never changes the
// underlying chain-verified/chain-rejected verdict, which is already
// correct regardless of which witness the solver happened to return.
export function computeCausalAncestry(target, allFindings, initialAtoms) {
  const initialAtomSet = new Set(initialAtoms);
  const byId = new Map(allFindings.map((f) => [f.id, f]));
  const producersOf = new Map(); // atom -> [findingId, ...]
  for (const f of allFindings) {
    for (const p of f.postconditions) {
      if (!producersOf.has(p)) producersOf.set(p, []);
      producersOf.get(p).push(f.id);
    }
  }

  const included = new Set();
  const stack = [target.id];
  while (stack.length > 0) {
    const id = stack.pop();
    if (included.has(id)) continue;
    included.add(id);
    const finding = byId.get(id);
    if (!finding) continue;
    for (const p of finding.preconditions) {
      if (initialAtomSet.has(p)) continue;
      for (const producerId of producersOf.get(p) || []) {
        if (producerId !== id) stack.push(producerId);
      }
    }
  }
  return allFindings.filter((f) => included.has(f.id)).map((f) => f.id);
}

/**
 * Runs selfAudit against `dir`, maps chain-eligible findings, and
 * attempts to PROVE (via chainKernel's existing SAT+DRAT machinery,
 * zero model calls) a reachability chain into every chain-eligible
 * finding as a candidate target, initial atoms defaulting to
 * `['unauthenticated-request']` (override via opts.initialAtoms).
 *
 * Returns real per-target verdicts, never a blended score, split into
 * FOUR buckets, never conflated:
 *   `proven`           chain-verified AND the witness genuinely uses 2+
 *                       DISTINCT findings -- an actual composition, the
 *                       whole point of this file.
 *   `directlyReachable` chain-verified, but the target's own
 *                       precondition was satisfied straight from
 *                       initialAtoms with no other finding needed --
 *                       real and worth knowing, but NOT a chain (it is
 *                       just that finding's own reachability, which
 *                       selfAudit's `unauthenticated:true` already
 *                       implied on its own); reported separately so it
 *                       is never mistaken for a genuine composition.
 *   `rejected`          chain-rejected, proven NOT to compose (or not
 *                       even directly reachable), independently
 *                       verified via dratProof.js.
 *   `undecided`         budget exhausted or an encoding disagreement --
 *                       never silently dropped.
 */
export function scanForProactiveChains(dir, opts = {}) {
  const auditResult = auditSourceTree(dir);
  const chainFindings = buildChainEligibleFindings(auditResult);
  const initialAtoms = opts.initialAtoms || DEFAULT_INITIAL_ATOMS;

  const base = {
    filesScanned: auditResult.filesScanned,
    findingsScanned: auditResult.findings.length,
    chainEligibleCount: chainFindings.length,
  };

  if (chainFindings.length < 2) {
    return {
      ...base,
      proven: [], directlyReachable: [], rejected: [], undecided: [],
      note: chainFindings.length === 0
        ? 'no selfAudit findings in this scan map to a chain atom -- nothing to compose'
        : 'only one chain-eligible finding in this scan -- a chain needs at least two',
    };
  }

  const proven = [];
  const directlyReachable = [];
  const rejected = [];
  const undecided = [];

  const initialAtomSet = new Set(initialAtoms);

  for (const target of chainFindings) {
    if (target.preconditions.length === 0) continue; // nothing to chain INTO -- normalizeChainInput would reject this target anyway
    const others = chainFindings.filter((f) => f.id !== target.id);

    let spec;
    try {
      spec = normalizeChainInput({ findings: [...others, target], initialAtoms, targetFindingId: target.id });
    } catch {
      continue; // not a well-formed chain question for this target -- not an error, just nothing to prove
    }

    const result = proveChainReachability(spec, opts.chainOpts);
    if (result.verdict === 'chain-verified') {
      const entry = { target: target.id, targetFile: target.region, ...result };
      // A genuine composition requires the TARGET's own preconditions to
      // need something beyond the bare initial atoms -- NOT "the SAT
      // witness happened to include 2+ active findings," which is
      // unreliable: proveChainReachability finds A satisfying
      // assignment, not a MINIMAL one, so an unrelated finding can end
      // up "along for the ride" in the witness with no causal bearing
      // on the target at all. Checking the target's own precondition
      // set against initialAtoms directly is a static, deterministic
      // fact about the target, independent of which particular witness
      // the solver happened to return.
      const isDirectlyReachable = target.preconditions.every((p) => initialAtomSet.has(p));
      if (isDirectlyReachable) {
        directlyReachable.push(entry);
      } else {
        entry.causalAncestry = computeCausalAncestry(target, [...others, target], initialAtoms);
        proven.push(entry);
      }
    } else if (result.verdict === 'chain-rejected') {
      rejected.push({ target: target.id, targetFile: target.region, verdict: result.verdict });
    } else {
      undecided.push({ target: target.id, targetFile: target.region, verdict: result.verdict });
    }
  }

  return { ...base, proven, directlyReachable, rejected, undecided };
}
