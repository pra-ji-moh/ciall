# Position

One sentence, and everything here serves it:

> **The thing being checked does not get to write the check.**

Three projects have been built toward this without it being written down, which
is why they looked like three projects. They are one position implemented at
three layers.

## The three layers

| Layer | What must not happen | The mechanism | State |
|---|---|---|---|
| **Execution** (Smarsh) | a program reports success while hiding what it tried | capabilities declared not inherited; refusals recorded in a hash-chained, signed record the program cannot edit | **runs** |
| **Claim** (Ciall) | a claim is accepted because it sounded checkable | formulate into arithmetic plus invariants, then try to falsify it | partial |
| **Model** (the backprop work) | a confident answer with no grounding behind it | a first-class null state, distinct from "grounded but uncertain" | unbuilt |

The same object appears at every layer. At the execution layer it is a recorded
refusal. At the claim layer it is `dataFusion` refusing to fuse measurements in
decisive tension rather than averaging a real discrepancy into a false
consensus. At the model layer it is the null state that cross-entropy has no way
to express, because the loss function assumes there is always a right-shaped
answer to output.

`speculate` in Smarsh is that null state, at the execution layer, running today.

## What actually exists

Verified rather than remembered, as of 2026-09-04.

**Smarsh** (`C:\Users\USER\smarsh`, public, MIT, zero dependencies)
- ~17k lines across 29 modules, 965 tests over 41 files, 95% line coverage
- Two engines, a tree-walker that is the specification and a closure compiler
  4x faster, held differentially identical across 3,065 programs
- Capabilities declared and never inherited; a callback runs with its own
  authority, not its caller's
- Both halves of the decentralized label model, confidentiality and integrity
- Hash-chained, Ed25519-signed run manifests, anchored by exclusion so a field
  added later is covered by default
- A prover that distinguishes proved, refuted, and cannot decide, and never
  proves by silence
- `speculate`: grounding as a degree, a bar set by stakes and formalizability,
  refusal below it, and the numbers recorded rather than returned

**Ciall** (`C:\Users\USER\ciall..suvstrate`, private, **not under version control**)
- 67 test files, nine domains, dozens of kernels, zero dependencies
- SAT with DRAT certificates, seeded MCMC counterexample search, contradiction
  detection, RK4/RK45 dynamics, dimensional analysis, data fusion
- No users, no deployment, and the domains are hypotheses rather than
  extractions from anyone's real problem

**The model layer**
- A diagnosis, not an implementation. Cross-entropy conflates epistemic and
  aleatoric uncertainty; there is no separate channel for "I have no grounds."

## The shared wall

All three are blocked on the same thing, and it is worth stating in one place
because effort spent on it pays three times.

> **Who writes the formulation.**

Smarsh's contracts, invariants, `stakes` and `formalizability` are hand-written
by someone who already knows the domain. Ciall's nine domain mappings are
hand-written the same way. In the foundational-reasoning work the same wall
appeared as evidence functions specified per domain by a domain expert: fine for
two domains as proof of an engine, and it does not scale to anything called
foundational.

So the honest division is:

- **The checking half is built.** Nothing gets asserted without grounds, and the
  record cannot be forged.
- **The formulating half is not.** Producing the structure to be checked, from a
  messy real claim, is unsolved here and was not found in any prior art searched.

Smarsh mechanises the checking. Reasoning is mostly the formulating. That gap is
the whole distance between what exists and what "native reasoning" would mean.

## What is deliberately not claimed

- Smarsh does not reason. It checks. There is no learning, no representation, no
  hypothesis generation, and the prover is linear arithmetic over rationals: a
  single product of two unknowns goes undecidable.
- No third-party audit, no users, pre-1.0. The security claims are the product,
  and unaudited they are assertions. Every claim in SECURITY.md now cites the
  test that tries to break it, and a build failure protects the citation, which
  makes them checkable rather than audited. Different thing.
- `ffi` is a complete escape. Grant it and the capability system stops applying.
  It is a door, named in the record, not a leak.
- Ciall's sector positioning is researched, not sold. No adoption is claimed.

## The filter

When deciding whether something belongs in any of the three:

> **Does this make a groundless claim harder to make?**

That question would have kept quantum logic out of the front of the Smarsh
README months ago, and it is a better test than whether something is interesting.
Breadth is fine as inventory and fatal as the pitch.

## Order of work

The layers differ enormously in tractability, so the order picks itself.

1. **Smarsh** is the beachhead. It exists, it runs, and it needs nobody's
   permission or compute.
2. **Ciall** is the application that makes the thesis worth something, one
   domain at a time. `dataFusion` first: small, and its refusal semantics are
   already the shape Smarsh enforces.
3. **The model layer** is the long bet, and it becomes dramatically more
   credible pointing at two working systems built on the same principle than at
   a preprint.

Things that start general do not scale. Unix, Rails, Erlang and Rust were all
extracted from one demanding application after the second and third case showed
which parts actually repeated. CORBA, SOAP and the semantic web were designed
general first and abstracted over imagined cases. Smarsh is the first kind: it
was built for Ciall, which is why its breadth is requirements rather than
collecting. Ciall is currently at risk of being the second kind, with nine
domains and no users. One domain with a real claim outranks a tenth domain.
