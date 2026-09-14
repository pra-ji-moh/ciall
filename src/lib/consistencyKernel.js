// consistencyKernel.js; deterministic contradiction detection with NO
// ground truth, no oracle, no network, no model in the deciding step.
//
// WHY THIS EXISTS, precisely.
//
// DeepMind's discovery stack (FunSearch, AlphaEvolve, AlphaProof) shares
// one requirement: an automated evaluator that scores a candidate against
// truth. AlphaEvolve says so outright; it can only attack problems that
// come with a built-in way to evaluate success. That constraint is real
// and it is not a failure of ambition: if you are SEARCHING for a correct
// answer, and nothing can tell you when you have one, the search has
// nothing to climb. A model grading its own output converges on
// confident-sounding output, not true output, which the uncertainty
// literature names bluntly as an internal evaluation cycle that conflates
// stability with truth.
//
// But notice the exact shape of the constraint. It binds on "is this
// claim TRUE". It does NOT bind on "are these claims COMPATIBLE".
// Consistency is checked against logic, which is free, always available,
// and requires no access to reality whatsoever. And the result is not a
// weaker consolation prize; it is decisive in the strongest sense:
//
//   If a set of commitments is inconsistent, at least one of them is
//   FALSE. That is a proof. It is obtained without an oracle, without an
//   experiment, and without anyone knowing which one is wrong.
//
// There is hard precedent that consistency alone yields real truth: the
// conformal bootstrap determined the 3D Ising critical exponents to record
// precision from crossing symmetry and unitarity alone, with no
// experimental input at all. Consistency is not a soft signal.
//
// WHAT THIS DOES AND DOES NOT CLAIM. It does not decide truth. It cannot
// tell you WHICH commitment to drop; that requires judgment it does not
// have, and it says so rather than guessing. It only proves that the set
// as stated cannot all hold. Silence from this module is NOT evidence of
// consistency either: absence of a detected contradiction is absence of
// proof, exactly the same asymmetry the rest of this codebase enforces
// between a found counterexample (decisive) and a failed search
// (suggestive only).
//
// THE HONEST SEAM. The kernel below is deterministic and sound: given the
// structured commitments, its conclusions follow by modus ponens,
// contraposition, and universal instantiation, and nothing else. But the
// EXTRACTION of structure from a person's prose is done by a model and is
// fallible. So a detected contradiction is computed CONDITIONAL ON A
// READING, and the reading must be shown to the user so they can reject
// it. A contradiction the user disowns because the extraction misread them
// is a bug in the reading, not a fact about their thinking. Every finding
// therefore carries its full derivation chain and the original sentences.

import { compareMeasurements } from './measurementTension.js';
import { analyzeEquation } from './dimensionalAnalysis.js';
import { findOrderCycles, COMPARATORS } from './orderConsistency.js';
import { hashKey } from './fnv1a.js';

export const MAX_COMMITMENTS = 40;
const MAX_ROUNDS = 64; // forward chaining is monotone; this only guards pathological input

// ── Structured commitment forms ─────────────────────────────────────
// Deliberately a small, closed set. A richer logic would extract worse:
// the bottleneck here is never expressive power, it is whether a model can
// map prose onto the form RELIABLY. Five forms that a model gets right
// beat twenty it gets wrong, because an unsound extraction produces
// confident false contradictions, which is the worst possible failure for
// a tool whose whole value is being trustworthy about disagreement.
//
//   assert    { atom, polarity }                  "growth is slowing"
//   implies   { fromAtom, fromPol, toAtom, toPol } "if pricing rises, churn rises"
//   universal { category, property, polarity }     "every marketplace needs liquidity"
//   instance  { entity, category }                 "we are a marketplace"
//   property  { entity, property, polarity }       "we do not need liquidity"

const norm = (s) => String(s || '').trim().toLowerCase().replace(/\s+/g, ' ').replace(/[.;,]+$/, '');

export function normalizeCommitments(raw) {
  const list = Array.isArray(raw?.commitments) ? raw.commitments.slice(0, MAX_COMMITMENTS) : [];
  const out = [];
  for (let i = 0; i < list.length; i++) {
    const c = list[i] || {};
    const id = `c${i + 1}`;
    const source = String(c.source || '').slice(0, 300);
    const pol = (v) => v !== false; // default true; only an explicit false is negative
    switch (c.kind) {
      case 'assert':
        if (norm(c.atom)) out.push({ id, kind: 'assert', atom: norm(c.atom), polarity: pol(c.polarity), source });
        break;
      case 'implies':
        if (norm(c.fromAtom) && norm(c.toAtom)) {
          out.push({
            id, kind: 'implies', source,
            fromAtom: norm(c.fromAtom), fromPol: pol(c.fromPol),
            toAtom: norm(c.toAtom), toPol: pol(c.toPol),
          });
        }
        break;
      case 'universal':
        if (norm(c.category) && norm(c.property)) {
          out.push({ id, kind: 'universal', category: norm(c.category), property: norm(c.property), polarity: pol(c.polarity), source });
        }
        break;
      case 'instance':
        if (norm(c.entity) && norm(c.category)) out.push({ id, kind: 'instance', entity: norm(c.entity), category: norm(c.category), source });
        break;
      case 'property':
        if (norm(c.entity) && norm(c.property)) {
          out.push({ id, kind: 'property', entity: norm(c.entity), property: norm(c.property), polarity: pol(c.polarity), source });
        }
        break;
      // A quoted numerical value with an uncertainty. Kept as numbers
      // rather than folded into an atom, so incompatibility can be
      // COMPUTED (measurementTension.js) instead of having to be asserted.
      // Before this, the kernel could only confirm a numerical conflict
      // the user had already spotted and stated.
      // A comparative claim. Only comparable within the same metric, and
      // a cycle through any strict comparison is a proven impossibility.
      case 'relation': {
        const subject = norm(c.subject), object = norm(c.object), metric = norm(c.metric);
        if (subject && object && metric && subject !== object && COMPARATORS.has(c.comparator)) {
          out.push({ id, kind: 'relation', subject, object, metric, comparator: c.comparator, source });
        }
        break;
      }
      // A stated equation, kept as text plus a symbol table so its
      // dimensions can be CHECKED rather than trusted. Fully decidable,
      // so it widens what the kernel proves without weakening proof.
      case 'equation': {
        if (String(c.lhs || '').trim() && String(c.rhs || '').trim()) {
          out.push({
            id, kind: 'equation', source,
            lhs: String(c.lhs).slice(0, 300),
            rhs: String(c.rhs).slice(0, 300),
            assignments: (c.assignments && typeof c.assignments === 'object') ? c.assignments : {},
          });
        }
        break;
      }
      case 'measurement': {
        const value = Number(c.value);
        const uncertainty = Number(c.uncertainty);
        if (norm(c.quantity) && Number.isFinite(value) && Number.isFinite(uncertainty)) {
          out.push({
            id, kind: 'measurement', source,
            quantity: norm(c.quantity), value, uncertainty,
            assumes: Array.isArray(c.assumes) ? c.assumes.map((a) => String(a).slice(0, 120)).filter(Boolean) : [],
          });
        }
        break;
      }
      default:
        break; // unknown form dropped rather than guessed at
    }
  }
  return out;
}

// ── The kernel ───────────────────────────────────────────────────────
// Forward chaining to a fixpoint over three sound rules. Monotone, so it
// terminates; every derived fact carries the chain that produced it, so
// every contradiction ships with a readable proof rather than a verdict.

const factKey = (kind, a, b) => (kind === 'atom' ? `atom:${a}` : `prop:${a}|${b}`);

// ── Sparse incremental recomputation (upgrade 6) ─────────────────────
//
// Transparent to callers: deriveClosure/findContradictions keep the
// exact same signature and return the exact same shape as before this
// upgrade. Internally, deriveClosure now warm-starts from the LAST full
// closure this module computed when the new commitments array looks
// like a small, position-aligned edit of the last one — same length,
// most positions carrying the exact same content. When it doesn't (no
// prior call, a different length, or nothing recognizable), it falls
// straight back to the exact same empty-seeded computation this
// function has always done.
//
// CORRECTNESS ARGUMENT, the part that actually matters here. Forward
// chaining over these three rules is a monotone operator (facts only
// ever get ADDED, never retracted, within one run — the file's own
// original comment already said so). For a monotone operator, starting
// the fixpoint iteration from ANY valid subset of its eventual result
// converges to the SAME least fixpoint as starting from empty
// (Knaster-Tarski) — this is not an approximation or a heuristic, it's
// the same guarantee that makes forward chaining terminate at all.
// "Valid subset" is the one thing that has to be gotten right: a warm-
// started fact is only safe to reuse if EVERY commitment in its
// derivation chain (`why`) is still present with IDENTICAL content —
// and since `why` arrays are built by spreading the parent's own `why`
// at each derivation step (see runClosureFixpoint below), checking a
// fact's own `why` array against the set of changed positions correctly
// captures its FULL transitive dependency, not just its immediate
// parent. Position alignment (same array index, not just "this content
// exists somewhere in the new array") is what keeps this safe without
// any id-remapping risk: an unchanged position keeps the exact same
// positional id (`c7` stays `c7`) between the two calls, so a reused
// `why` array's ids are never stale relative to the current call's own
// id assignment. If positions don't line up this cleanly, tryWarmStart
// simply declines (returns null) rather than guess — always safe, just
// not accelerated. Verified directly, not just argued: see
// tests/consistencyIncremental.test.mjs's property test, which compares
// this function's output against a forced-cold recomputation across
// hundreds of randomly generated commitment sets and edits.

const MAX_CACHE_ENTRIES = 1000;
// Per-assertion direct-fact cache, upgrade 6's literal design: every
// commitment's own single-assertion-determined seed fact (assert/
// property only — the only kinds whose fact depends on nothing but
// their own content), keyed by content hash, LRU-capped. Not load-
// bearing for tryWarmStart's correctness (that only reuses facts from
// `lastClosure`, verified above); this is the fine-grained per-
// assertion cache the task asked for, kept for its own sake and for any
// future caller that wants a single commitment's direct fact without
// running a whole closure.
const directFactCache = new Map();

function lruGet(map, key) {
  if (!map.has(key)) return undefined;
  const v = map.get(key);
  map.delete(key); map.set(key, v); // touch: move to most-recently-used position
  return v;
}
function lruSet(map, key, value, maxSize) {
  map.delete(key);
  map.set(key, value);
  if (map.size > maxSize) map.delete(map.keys().next().value); // evict least-recently-used
}

// Hash over content only — `id` is positional (assigned by array index
// in normalizeCommitments), not part of a commitment's identity, and
// must never affect its hash or two calls with the same content at
// different positions would wrongly be treated as different commitments.
// Keyed by OBJECT REFERENCE, not content: hashing is genuinely redundant
// work when a caller passes back the exact same, untouched commitment
// object it passed last time (the common shape of an edit -- every test
// and every real caller in this codebase builds an edited array via
// something like `commitments.map(c => i === changedIndex ? {...c, ...}
// : c)`, which only allocates a NEW object at the changed position and
// keeps the SAME reference everywhere else). Safe under the same
// implicit contract every other part of this cache already relies on:
// a commitment object, once passed to deriveClosure/findContradictions,
// is never mutated in place -- an edit is always a NEW object. Correct
// even for the very first call on a never-before-seen object (a WeakMap
// miss falls through to computing the hash normally) and correct across
// module instances (each fresh module gets its own empty WeakMap, so a
// true "full restart" still hashes everything, exactly as it should).
const hashMemo = new WeakMap();
function commitmentContentHash(c) {
  const cached = hashMemo.get(c);
  if (cached !== undefined) return cached;
  const { id, ...content } = c;
  const hash = hashKey(content);
  hashMemo.set(c, hash);
  return hash;
}

function directSeedFact(c) {
  if (c.kind === 'assert') return { key: factKey('atom', c.atom), polarity: c.polarity, label: c.atom };
  if (c.kind === 'property') return { key: factKey('prop', c.entity, c.property), polarity: c.polarity, label: `${c.entity} has ${c.property}` };
  return null; // implies/universal/instance/equation/relation/measurement: no single-assertion-determined fact
}

// The most recent FULL closure this module computed — a single slot,
// not a per-claim cache: a call that shares little with it naturally
// gets treated as "everything changed" by tryWarmStart below, which
// degrades gracefully to (in effect) a full recomputation rather than
// needing a separate cold-path decision.
let lastClosure = null; // { hashes: string[], ids: string[], facts: Map, contradictions: [] }

// Runs the round-based fixpoint (rules 1-3) to completion, MUTATING the
// given `facts`/`contradictions` in place. Exactly the rule-application
// logic this kernel has always used, factored out so it can start from
// either an empty state (full recomputation) or a warm-started partial
// state (incremental recomputation).
//
// `warmRuleFacts`, if given, is a Map of rule-derived facts carried over
// from a previous run that are still valid (see tryWarmStart) — merged
// in AFTER the direct seed step below, never before it and never in
// place of it. That ordering is load-bearing, not cosmetic: the direct
// seed step ALWAYS runs over every current commitment, into an
// initially empty facts map AND an initially empty contradictions
// array, in the exact same array order a cold run always has —
// bit-identical to cold's own seed phase, no exceptions, no shortcuts.
// Warm carry-over (both `warmRuleFacts` and `warmContradictions`) is
// merged in ONLY AFTER that fresh seed has fully run, never before and
// never mixed into it. Two real bugs lived in getting this ordering
// wrong, both caught by tests/consistencyIncremental.test.mjs's
// property test, not by inspection — re-run it if this function is
// touched again:
//  1. Seeding only the CHANGED commitments (skipping "unchanged" ones
//     as "already captured") loses shadowed backup justifications:
//     assertFact's "first writer wins, later same-polarity writers are
//     silently shadowed" means an UNCHANGED commitment can be the
//     silent backup for a fact whose CREDITED source did change, and
//     skipping its re-seed drops that backup entirely.
//  2. Pre-populating `contradictions` with warm carry-over BEFORE the
//     fresh seed runs corrupts assertFact's own dedup guard (`if
//     (!contradictions.some(c => c.key === sig))`): a stale entry
//     credited to last run's array-order winner blocks the fresh seed
//     from recording THIS run's correct winner for the same key. The
//     dedup guard has to see only what THIS run has itself discovered
//     so far, or it dedupes against the wrong thing.
function runClosureFixpoint(commitments, facts, contradictions, warmRuleFacts, warmContradictions) {
  function assertFact(key, polarity, why, label) {
    const existing = facts.get(key);
    if (existing) {
      if (existing.polarity !== polarity) {
        // Both polarities derivable: the set cannot all hold. Record it
        // once, with BOTH derivation chains, and keep going (one
        // contradiction should not hide another).
        const sig = `${key}`;
        if (!contradictions.some((c) => c.key === sig)) {
          contradictions.push({
            key: sig, label,
            positiveWhy: existing.polarity ? existing.why : why,
            negativeWhy: existing.polarity ? why : existing.why,
          });
        }
      }
      return false; // already known at this polarity; no new information
    }
    facts.set(key, { polarity, why, label });
    return true;
  }

  // Direct seed: EVERY commitment, always, same array order as cold,
  // into empty facts AND empty contradictions — see the note above.
  for (const c of commitments) {
    if (c.kind === 'assert') assertFact(factKey('atom', c.atom), c.polarity, [c.id], c.atom);
    if (c.kind === 'property') assertFact(factKey('prop', c.entity, c.property), c.polarity, [c.id], `${c.entity} has ${c.property}`);
  }

  const implications = commitments.filter((c) => c.kind === 'implies');
  const universals = commitments.filter((c) => c.kind === 'universal');
  const instances = commitments.filter((c) => c.kind === 'instance');

  function runRound() {
    let changed = false;

    // Rule 1, modus ponens: (A=p -> B=q), A=p  therefore  B=q
    // Rule 2, contraposition: (A=p -> B=q), B=!q  therefore  A=!p
    // Contraposition is included because it is exactly as sound as modus
    // ponens and catches the common real case where someone denies a
    // consequence while still holding its antecedent.
    for (const im of implications) {
      const fromKey = factKey('atom', im.fromAtom);
      const toKey = factKey('atom', im.toAtom);
      const from = facts.get(fromKey);
      const to = facts.get(toKey);
      if (from && from.polarity === im.fromPol) {
        if (assertFact(toKey, im.toPol, [...from.why, im.id], im.toAtom)) changed = true;
      }
      if (to && to.polarity !== im.toPol) {
        if (assertFact(fromKey, !im.fromPol, [...to.why, im.id], im.fromAtom)) changed = true;
      }
    }

    // Rule 3, universal instantiation: (all C have P=p), e is a C
    //                                   therefore  e has P=p
    for (const u of universals) {
      for (const inst of instances) {
        if (inst.category !== u.category) continue;
        const key = factKey('prop', inst.entity, u.property);
        if (assertFact(key, u.polarity, [u.id, inst.id], `${inst.entity} has ${u.property}`)) changed = true;
      }
    }

    return changed;
  }

  // Round 0 ALWAYS runs first, purely against the fresh seed above, with
  // ZERO warm-fact interference — a FOURTH bug, distinct from the
  // previous three (and only found by the multi-step stress variant of
  // the property test, not the single-edit one): merging a warm rule-
  // derived fact in BEFORE round 0 can let a self-referential or cyclic
  // implication chain fire in round 0 when cold would only let it fire
  // in round 1 or later — because in cold, that key genuinely doesn't
  // exist yet when an array-earlier rule checks for it in round 0, so
  // the rule that DOES establish it first (later in the SAME round, or
  // in the next) keeps credit even after the self-referential rule gets
  // its own turn. A warm fact sitting in the map before round 0 even
  // starts erases that "doesn't exist yet" state, letting the self-
  // referential rule fire immediately and claim credit cold never would
  // have given it — even on a call where NOTHING changed since the last
  // one, since the bug is about round-TIMING, not about which
  // commitments changed. Giving round 0 an unimpeded, cold-identical
  // first pass is what keeps array-order-priority intact for every kind
  // of chain, self-referential or not.
  let round = 0;
  let changed = runRound();
  round++;

  // Merge in warm rule-derived facts in the GAP after round 0, before
  // round 1 — not before round 0 (the bug above), and not skipped
  // entirely (which would mean no speedup at all for any claim with
  // real derivation depth, since round 0 alone rarely reaches
  // fixpoint). By this point every array-order-eligible commitment has
  // already had its fair first-round shot; anything STILL missing is
  // something that would otherwise take further rounds to (re)derive
  // fresh, which is exactly where reusing a cached value is safe. Fills
  // ONLY empty slots, no conflict detection of its own — same reasoning
  // as the seed-merge bug above: running assertFact's conflict check
  // during a merge can discover a conflict earlier than cold's own
  // discovery order ever would, corrupting the record of WHICH
  // commitment (of two, on an exact tie) findContradictions credits.
  if (warmRuleFacts) {
    for (const [key, fact] of warmRuleFacts) {
      if (!facts.has(key)) { facts.set(key, fact); changed = true; }
    }
  }

  while (changed && round < MAX_ROUNDS) {
    changed = runRound();
    round++;
  }

  // Warm-carried contradictions are merged in LAST, after the fresh
  // seed AND the round loop have both had their unimpeded, cold-
  // identical chance to discover everything themselves — for the exact
  // same reason warmRuleFacts is merged with no conflict-detection of
  // its own (see the note above): appending here can only ADD a
  // contradiction the fresh computation didn't happen to rediscover on
  // its own, never reorder or preempt one it did.
  if (warmContradictions) {
    for (const c of warmContradictions) {
      if (!contradictions.some((existing) => existing.key === c.key)) contradictions.push(c);
    }
  }
}

// Position-aligned warm-start: null (decline) unless `commitments` is
// the exact same length as the last full closure's input. Where a
// position's content hash matches, that position is "unchanged"; every
// OTHER position (different content, this call or last) is "changed".
// A fact/contradiction from the last closure survives into the warm
// carry-over only if its ENTIRE why-chain is free of changed-position
// ids — see the correctness note on runClosureFixpoint above for why
// that's sound, and why these are merged in AFTER a full fresh seed
// rather than used to pre-populate the facts map directly.
function buildAtomGraph(commitments) {
  const atomGraph = new Map(); // atomKey -> Set<atomKey>
  const addEdge = (a, b) => {
    if (!atomGraph.has(a)) atomGraph.set(a, new Set());
    if (!atomGraph.has(b)) atomGraph.set(b, new Set());
    atomGraph.get(a).add(b);
    atomGraph.get(b).add(a);
  };
  for (const c of commitments) {
    if (c.kind === 'implies') addEdge(factKey('atom', c.fromAtom), factKey('atom', c.toAtom));
  }
  return atomGraph;
}

function tryWarmStart(commitments, hashes) {
  if (!lastClosure || lastClosure.hashes.length !== hashes.length) return null;

  // A position only counts as "unchanged" if BOTH its content hash AND
  // its id match the last run's — checking hash alone is not enough.
  // `why` chains carry the LAST run's ids; a position is only safe to
  // treat as still-justifying-what-it-justified-before if the SAME id
  // is still there. Ids here are caller-supplied and are NOT guaranteed
  // to be pure array-position labels (c1, c2, ...) the way
  // normalizeCommitments's own output always is — a direct caller of
  // deriveClosure/findContradictions (bypassing normalizeCommitments)
  // can use any ids it wants, and a removal followed by an addition can
  // leave the ARRAY the same length while every id past the removal
  // point has shifted. Comparing only `commitments[i].id` against a
  // changed-hash position (an earlier version of this function did
  // exactly that) records the WRONG id — the current, shifted one, not
  // the last run's now-stale one — and a fact still citing the last
  // run's stale id at that slot never gets excluded from carry-over.
  // Caught by tests/consistencyIncremental.test.mjs's add/remove
  // property test, not by inspection.
  const changedOldIds = new Set();
  const changedCommitments = []; // the CURRENT-array commitment objects at changed/new positions
  for (let i = 0; i < hashes.length; i++) {
    if (hashes[i] !== lastClosure.hashes[i] || commitments[i].id !== lastClosure.ids[i]) {
      changedOldIds.add(lastClosure.ids[i]);
      changedCommitments.push(commitments[i]);
    }
  }
  const touchesChanged = (why) => why.some((id) => changedOldIds.has(id));

  // A fourth bug, distinct from the previous three: a fact's OWN
  // why-chain not touching any changed id is NECESSARY but not
  // SUFFICIENT for it to be safe to reuse. `assertFact`'s credit goes to
  // whichever commitment reaches a key FIRST in array order — a changed
  // or brand-new `implies` commitment, positioned EARLIER in the array
  // than whatever originally got credited for a key, can outrank that
  // old credit in a fresh (cold) run even though the old credit's own
  // chain never mentions the new commitment at all. Concretely: an
  // unrelated edit turns some commitment into a duplicate of an
  // existing (unchanged, array-later) implication; cold credits the
  // array-earlier one; a naive warm-start still has the array-later
  // one's stale credit sitting in the cache, and nothing about ITS OWN
  // why-chain looks wrong, so the per-fact check alone lets it survive.
  //
  // Fixed with real contamination propagation, not another per-fact
  // spot check: build the graph `implies` commitments define over atom
  // keys (an implication genuinely links both atoms bidirectionally for
  // this purpose — contraposition means a change on either side can
  // affect what's derivable at the other), seed "contaminated" from
  // every key a changed/new commitment can directly touch, and
  // propagate through the graph. Universal/instance-derived facts don't
  // need graph propagation — their why-chain already names every
  // commitment their derivation actually depends on (category matching
  // is a static property of the commitments themselves, not of another
  // derived fact's value), so the plain touchesChanged check above is
  // already exact for them. Caught by tests/consistencyIncremental.
  // test.mjs's multi-step stress property test (a longer chain of mixed
  // edits than the single-edit tests catch), not by inspection.
  // The atom-implication graph only changes SHAPE when an `implies`
  // commitment is added, removed, or itself edited. Reusing the PREVIOUS
  // run's graph object whenever that's not the case avoids rebuilding an
  // unchanged structure on every single call -- otherwise this rebuild
  // is paid in full even when the edit has nothing to do with any
  // implication at all (e.g. an unrelated assert/property edit), eating
  // into the very savings sparse recomputation exists to capture. Safe
  // because the graph is never mutated after construction, only read.
  // Caught by the upgrade 6 performance benchmark, not by inspection: a
  // correct-but-naive full rebuild every call was leaving enough fixed
  // overhead on the table that a long implication chain's real
  // incremental savings fell short of the required 80% reduction.
  const impliesMayHaveChanged = changedCommitments.some((c) => c.kind === 'implies');
  const atomGraph = !impliesMayHaveChanged && lastClosure.atomGraph ? lastClosure.atomGraph : buildAtomGraph(commitments);

  const contaminated = new Set();
  const frontier = [];
  for (const c of changedCommitments) {
    if (c.kind === 'assert') frontier.push(factKey('atom', c.atom));
    else if (c.kind === 'property') frontier.push(factKey('prop', c.entity, c.property));
    else if (c.kind === 'implies') frontier.push(factKey('atom', c.fromAtom), factKey('atom', c.toAtom));
    // universal/instance: no graph participation, see note above.
  }
  while (frontier.length > 0) {
    const key = frontier.pop();
    if (contaminated.has(key)) continue;
    contaminated.add(key);
    for (const neighbor of atomGraph.get(key) || []) frontier.push(neighbor);
  }

  const ruleFacts = new Map();
  for (const [key, fact] of lastClosure.facts) {
    if (!touchesChanged(fact.why) && !contaminated.has(key)) ruleFacts.set(key, fact);
  }
  const contradictions = [];
  for (const c of lastClosure.contradictions) {
    if (!touchesChanged(c.positiveWhy) && !touchesChanged(c.negativeWhy) && !contaminated.has(c.key)) contradictions.push(c);
  }

  return { ruleFacts, contradictions, atomGraph };
}

export function deriveClosure(commitments) {
  const hashes = commitments.map(commitmentContentHash);

  const warm = tryWarmStart(commitments, hashes);
  // Both start empty, always — runClosureFixpoint's own fresh seed step
  // fills them first; warm carry-over (if any) is merged in only after
  // that, inside runClosureFixpoint itself. See that function's header
  // for why pre-populating either one here would be wrong.
  const facts = new Map();
  const contradictions = [];

  runClosureFixpoint(commitments, facts, contradictions, warm ? warm.ruleFacts : null, warm ? warm.contradictions : null);

  for (let i = 0; i < commitments.length; i++) {
    lruSet(directFactCache, hashes[i], directSeedFact(commitments[i]), MAX_CACHE_ENTRIES);
  }
  const atomGraph = warm ? warm.atomGraph : buildAtomGraph(commitments);
  lastClosure = { hashes, ids: commitments.map((c) => c.id), facts: new Map(facts), contradictions: contradictions.slice(), atomGraph };

  return { facts, contradictions };
}

// The public call. Returns the proven contradictions, each with the exact
// commitments involved and the rule chain, so a user can check the reading
// and throw it out if the extraction misread them.
export function findContradictions(commitments) {
  const { contradictions } = deriveClosure(commitments);
  const byId = new Map(commitments.map((c) => [c.id, c]));

  // One inconsistent set of commitments is ONE contradiction, even though
  // forward chaining surfaces it at every atom along the loop. Reporting
  // the same conflict three times because it was detectable at three
  // points would overstate the finding, and overstating is the one thing
  // this module cannot afford: a user who is told "3 separate
  // contradictions" and finds it is really one stops believing the count,
  // and then stops believing the tool. Deduplicate on the set of
  // commitments actually involved, and keep the shortest derivation, which
  // is the one a human can follow most easily.
  const seen = new Map();
  for (const c of contradictions) {
    const involved = [...new Set([...c.positiveWhy, ...c.negativeWhy])].sort();
    const sig = involved.join('|');
    const chainLength = c.positiveWhy.length + c.negativeWhy.length;
    const existing = seen.get(sig);
    if (existing && existing.chainLength <= chainLength) continue;
    seen.set(sig, {
      chainLength,
      finding: {
        about: c.label,
        involved,
        sources: involved.map((id) => byId.get(id)?.source).filter(Boolean),
        positiveChain: c.positiveWhy,
        negativeChain: c.negativeWhy,
        // Directly stated on both sides, or reached through inference. The
        // second is the interesting kind: nobody knowingly asserts A and
        // not-A, but people hold commitments that ENTAIL both all the time.
        derived: c.positiveWhy.length > 1 || c.negativeWhy.length > 1,
      },
    });
  }
  const logical = [...seen.values()].map((v) => v.finding);

  // Numerical contradictions, DERIVED from the quoted values rather than
  // asserted. Only decisive discrepancies (>= 5 sigma) join the logical
  // contradictions; sub-threshold tensions are real information but are
  // not proof, and are returned separately by measurementTensions() so a
  // caller can report them without calling them contradictions.
  const numeric = compareMeasurements(commitments.filter((c) => c.kind === 'measurement'))
    .filter((f) => f.kind === 'contradiction')
    .map((f) => {
      const involved = commitments
        .filter((c) => c.kind === 'measurement' && (c.source === f.a.source || c.source === f.b.source))
        .map((c) => c.id);
      return {
        about: f.quantity,
        involved,
        sources: [f.a.source, f.b.source].filter(Boolean),
        positiveChain: involved,
        negativeChain: involved,
        derived: true, // computed from the numbers, never stated by anyone
        numeric: { sigma: f.sigma, assumptions: f.assumptions, note: f.note },
      };
    });

  // Dimensional violations. A dimensionally unbalanced equation is false
  // regardless of how plausible its surroundings are, and the check is
  // decidable, so these are genuine proofs and belong alongside the
  // logical contradictions. Equations that could not be analysed at all
  // are deliberately NOT reported here; an unanalysable equation is not a
  // passing one and must never be silently counted as either.
  const dimensional = commitments
    .filter((c) => c.kind === 'equation')
    .map((c) => ({ commitment: c, result: analyzeEquation({ lhs: c.lhs, rhs: c.rhs, assignments: c.assignments, source: c.source }) }))
    .filter(({ result }) => result.status === 'violation')
    .map(({ commitment, result }) => ({
      about: `${result.lhsText} = ${result.rhsText}`,
      involved: [commitment.id],
      sources: [commitment.source].filter(Boolean),
      positiveChain: [commitment.id],
      negativeChain: [commitment.id],
      derived: true, // computed from the exponents, not stated by anyone
      dimensional: { violations: result.violations, lhsDim: result.lhsDim, rhsDim: result.rhsDim },
    }));

  // Impossible rankings. A strict order cannot contain a cycle, so a loop
  // through any strict comparison proves at least one of the comparisons
  // is false. Decidable, and the class of error that survives peer review
  // most easily, because each individual comparison looks fine alone.
  const ordering = findOrderCycles(commitments.filter((c) => c.kind === 'relation')).map((f) => ({
    about: `ranking on ${f.metric}`,
    involved: f.involved,
    sources: f.sources,
    positiveChain: f.involved,
    negativeChain: f.involved,
    derived: true, // no single claim states the loop; it emerges from the set
    ordering: { metric: f.metric, readable: f.readable, entities: f.entities, cycle: f.cycle },
  }));

  return [...logical, ...numeric, ...dimensional, ...ordering];
}

// Equations the checker could not analyse, usually an undeclared symbol.
// Surfaced separately and never as a pass: refusing to guess is the point,
// but a silent refusal would let a user believe an equation was verified.
export function unanalyzableEquations(commitments) {
  return commitments
    .filter((c) => c.kind === 'equation')
    .map((c) => analyzeEquation({ lhs: c.lhs, rhs: c.rhs, assignments: c.assignments, source: c.source }))
    .filter((r) => r.status === 'unanalyzable');
}

// Sub-threshold numerical tensions: below five sigma, so evidence of a
// discrepancy rather than proof of one. Exposed separately precisely so
// they are never presented as contradictions.
export function measurementTensions(commitments) {
  return compareMeasurements(commitments.filter((c) => c.kind === 'measurement'))
    .filter((f) => f.kind === 'tension');
}

export function summarizeConsistency(contradictions, commitmentCount) {
  const n = contradictions.length;
  if (commitmentCount === 0) return { verdict: 'nothing-extracted', headline: '', detail: '' };
  if (n === 0) {
    return {
      verdict: 'no-contradiction-found',
      headline: 'No contradiction found among these commitments.',
      // The asymmetry, stated every time. A failed search is not a proof.
      detail: `${commitmentCount} commitments were checked against each other. Nothing here proves they are all true; it only means no inconsistency was found among them, which is a weaker result and is not evidence of correctness.`,
    };
  }
  return {
    verdict: 'inconsistent',
    headline: n === 1 ? 'These commitments cannot all be true.' : `These commitments cannot all be true (${n} separate contradictions).`,
    detail: 'At least one of the statements below is false. That follows by logic alone, with no appeal to evidence and no way for a confident argument to talk it away. Which one to drop is your call, not the tool\'s.',
  };
}

export function buildConsistencyPrompt(claimText, coreClaim, siblingClaims = []) {
  const others = siblingClaims.filter(Boolean).slice(0, 12);
  return `Extract the logical COMMITMENTS in the material below into structured form. Do not judge whether anything is true. Do not argue. Extraction only. A separate deterministic checker will do the reasoning, and it can only be as good as this extraction.

MAIN CLAIM: "${claimText}"
${coreClaim && coreClaim !== claimText ? `BROADER CLAIM: "${coreClaim}"` : ''}
${others.length ? `\nOTHER THINGS THE SAME PERSON HAS COMMITTED TO IN THIS SESSION:\n${others.map((s, i) => `${i + 1}. ${s}`).join('\n')}` : ''}

Use ONLY these five forms:

1. assert     {"kind":"assert","atom":"<short canonical proposition>","polarity":true|false,"source":"<the sentence it came from>"}
   For a plain statement. "Growth is slowing" becomes atom "growth is slowing", polarity true.
   "Growth is not slowing" becomes the SAME atom with polarity false.

2. implies    {"kind":"implies","fromAtom":"...","fromPol":true|false,"toAtom":"...","toPol":true|false,"source":"..."}
   For conditional commitments. "If we raise prices, churn goes up."

3. universal  {"kind":"universal","category":"<class>","property":"<property>","polarity":true|false,"source":"..."}
   For statements about every member of a class. "Every marketplace needs liquidity."

4. instance   {"kind":"instance","entity":"<the thing>","category":"<class>","source":"..."}
   For membership. "We are a marketplace."

5. property   {"kind":"property","entity":"<the thing>","property":"<property>","polarity":true|false,"source":"..."}
   For a property of one specific thing. "We do not need liquidity."

6. measurement {"kind":"measurement","quantity":"<what is measured>","value":<number>,"uncertainty":<number>,"assumes":["<model the value depends on>"],"source":"..."}
   For ANY quoted numeric value carrying an error bar, tolerance, confidence interval, or stated range. "H0 = 67.4 +/- 0.5", "conversion was 4.2% +/- 0.3", "the coefficient is 1.8, standard error 0.2".
   Extract EVERY such value, even when nothing looks like it conflicts. The checker computes the comparison itself and will find discrepancies nobody noticed, which is the entire reason this form exists. Do not pre-filter for what seems interesting.
   If a range is given instead of an error bar ("between 60 and 70"), use the midpoint as the value and half the width as the uncertainty.
   If NO uncertainty is stated, OMIT the commitment rather than inventing one. A fabricated error bar produces a fabricated significance, which is worse than silence.
   "assumes" matters: record any model or premise the number depends on ("assumes LCDM", "assumes churn stays flat"). A value derived under a model is not a direct measurement, and when two such values conflict the false thing may be the MODEL rather than either measurement. Leave it empty for a direct measurement.
   Use the SAME quantity string for the same quantity everywhere, exactly as with atoms.

7. equation    {"kind":"equation","lhs":"<left side>","rhs":"<right side>","assignments":{"<symbol>":"<known quantity name OR {M,L,T} exponent object>"},"source":"..."}
   For ANY stated equation or formula. The checker verifies dimensional balance, which is decidable, so extract every one you see even if it looks obviously right.
   Write expressions in plain ASCII arithmetic: + - * / ^ and the functions sqrt, abs, sin, cos, tan, exp, log, ln. Write implicit multiplication explicitly: "m*c^2", never "mc^2".
   Every symbol must be declared in "assignments" unless it is one of the built-in names: length, distance, radius, time, mass, area, volume, velocity, speed, acceleration, momentum, force, energy, work, power, pressure, density, frequency, angular_momentum, action, spin, torque, temperature, curvature, ricci_scalar, torsion, hubble_parameter, cosmological_constant, energy_density, spin_density, and the constants c, G, hbar, h, k_B.
   Declare either by name ("v":"velocity") or by exponents ("kappa":{"M":-1,"L":1,"T":2}). Use {} for a dimensionless symbol.
   If you cannot determine a symbol's dimensions, OMIT the whole equation rather than guessing. A wrong declaration produces a confident false result, which is the worst outcome available.

8. relation    {"kind":"relation","subject":"<thing>","object":"<other thing>","comparator":"greater"|"less"|"equal"|"atleast"|"atmost","metric":"<what is being compared>","source":"..."}
   For ANY comparative claim: faster, cheaper, more accurate, outperforms, beats, exceeds, is at least as good as. Extract every one, including comparisons between things mentioned in different places, because the checker looks for loops that no single statement contains.
   "metric" is critical and must be SPECIFIC: "accuracy on ImageNet", not "performance". Comparisons are only checked against each other within an identical metric string, so a vague or inconsistent metric silently disables the check, while two genuinely different metrics wrongly given the same string would produce a false contradiction.
   Use "greater" for strictly better or larger on that metric, "atleast" for "at least as good as" or "no worse than".

THE SINGLE MOST IMPORTANT RULE: atom, category, property and entity strings are matched by EXACT TEXT. Two commitments only interact if their strings are identical. So you must reuse the SAME wording for the same concept everywhere, and phrase every atom POSITIVELY, putting the negation in the polarity field. Write "needs liquidity" with polarity false, never "does not need liquidity" with polarity true. If you paraphrase the same idea two different ways, the checker sees two unrelated ideas and finds nothing.

Do NOT invent commitments to manufacture a contradiction. Extract only what was genuinely stated or unmistakably presupposed. An honest empty extraction is far better than a fabricated one, because a false contradiction destroys the user's trust in every true one.

Respond ONLY with JSON:
{"commitments":[ ... up to ${MAX_COMMITMENTS} objects in the forms above ... ]}`;
}
