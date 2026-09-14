# Plan for Software Aspects of Certification (PSAC) — DRAFT

**Status: draft, unsubmitted, not reviewed by any DER or certification
authority.** A PSAC is legitimately authored BY the applicant, early in a
real certification program, then submitted to the certification authority
(or a DER acting on its behalf) for review and concurrence. Everything in
this document is the applicant-side draft; nothing here has authority
concurrence, and nothing here should be represented as approved,
in-progress-with-a-DER, or certified.

Sections follow the general structure a real PSAC covers, in this
project's own words (paraphrased, not quoted from RTCA DO-178C, for the
same copyright reason `certification/COMPLIANCE-MATRIX.md` discloses).
Every `[PLACEHOLDER: ...]` below is left unfilled because filling it
honestly requires information that does not exist without a real customer
program: a specific aircraft or system, a system-level safety assessment,
an actual DER relationship, and a real schedule. Inventing values for these
would be exactly the kind of fabricated certification claim this project
will not produce.

---

## 1. Purpose and Scope

This document plans the software aspects of certifying `ciall-substrate`'s
deterministic verification kernels for use as a software component within
`[PLACEHOLDER: target airborne system / program name]`. It does not, by
itself, certify anything; it proposes the process a real certification
effort would follow.

## 2. System Overview

`[PLACEHOLDER: description of the aircraft/system this software would be
integrated into, and the system-level function it performs — this must
come from the actual system safety assessment of a real program, not be
invented here]`.

## 3. Software Overview

`ciall-substrate` is a deterministic, zero-runtime-dependency claim-
verification kernel library: a caller (a model, a human, or another
system) proposes a structured claim or a proposed physical action; a
kernel — pure logic (SAT/consistency checking), seeded-stochastic search
(MCMC counterexample search, reproducible bit-for-bit given a seed), or
numerical simulation (RK45 dynamics, DVS pixel event simulation,
trapezoidal-profile actuator motion) — evaluates it independently of
whatever proposed it, and returns a verdict with either supporting
evidence or an honest "inconclusive."

Candidate Design Assurance Level: `[PLACEHOLDER: DAL must be derived from
a real system-level functional hazard assessment of the actual system this
software is integrated into — it is not a property of this software in
isolation, and this project targets DAL A as a design goal, not because a
real hazard assessment has assigned it]`.

Relevant existing engineering evidence (see
`certification/COMPLIANCE-MATRIX.md` for the full honest breakdown):
- Extensive algorithmic verification: exhaustive cross-checks (SAT solver
  vs. brute force across 400 random instances), closed-form analytical
  validation (RK45 to <1e-8 relative error), bit-for-bit reproducibility
  of every seeded-stochastic kernel.
- A completed bounded-execution audit across every kernel (see
  `CERTIFICATION-GAPS.md`), closing the one real unbounded-iteration gap
  found.
- A statically-allocated, zero-heap, zero-recursion C proof-of-concept
  port of one representative kernel (`certifiable-c/`), demonstrating the
  underlying algorithm has no inherent garbage-collection dependency —
  explicitly disclosed as uncompiled in this environment.
- Real, measured (not asserted) wall-clock timing-variance instrumentation
  (`src/lib/timingVariance.js`), quantifying the deterministic-output-vs-
  deterministic-timing gap rather than hiding it.

## 4. Software Life Cycle

Proposed processes: requirements-based development, independent
verification of requirements/design/code/test artifacts, structural
coverage analysis including MC/DC for DAL A, formal configuration
management, and independent quality assurance. **Current state**: this
project has followed a verification-heavy but informally-structured
process (extensive automated testing, no formal independent review layer
yet — see `certification/COMPLIANCE-MATRIX.md` Table A-9). Establishing
genuine process independence (a reviewer who did not write the code under
review) is a precondition for DAL A regardless of anything else in this
plan.

## 5. Software Life Cycle Environment

- **Language**: current kernels are implemented in JavaScript (Node.js);
  one representative kernel has a C proof-of-concept port
  (`certifiable-c/`). A real DAL A program would need the FULL kernel set
  reimplemented in C, Ada, or a certified Rust subset — JavaScript's
  garbage collector disqualifies it regardless of code quality, disclosed
  plainly in `CERTIFICATION-GAPS.md`.
- **Compiler/toolchain**: `[PLACEHOLDER: no DO-330-qualified compiler or
  toolchain has been selected or engaged. This requires a commercial
  relationship with a qualified vendor and cannot be filled in without
  one.]`
- **Target hardware**: `[PLACEHOLDER: no target embedded hardware has ever
  run this code; only Node.js/V8 has been exercised.]`
- **Verification tools**: current testing uses Node's built-in,
  unqualified `node:test` runner. A real program needs a DO-330-qualified
  test/coverage tool for any evidence used in certification credit.

## 6. Software Life Cycle Data

Data items a real program would produce, and this project's current state
against each (see `certification/COMPLIANCE-MATRIX.md` for the detailed,
objective-by-objective version):

| Life cycle data item | Current state |
|---|---|
| Plan for Software Aspects of Certification | this document (draft, unsubmitted) |
| Software Development Plan | not yet written |
| Software Verification Plan | not yet written (existing test suite is real evidence toward one, not the plan itself) |
| Software Configuration Management Plan | not yet written; ad hoc git usage only, with known gaps (see Table A-8) |
| Software Quality Assurance Plan | not yet written |
| Software Requirements Data (high- and low-level) | does not exist in DO-178C form; informal equivalents exist (`kernelExport.js`'s `KERNEL_SPECS`) |
| Software Design Description | does not exist as a standalone document; `kernelRegistry.js`'s architecture comments are the closest informal equivalent |
| Source Code | exists (this repository); coding-standard conformance not formally enforced |
| Software Verification Cases and Procedures / Results | 422 automated tests exist and pass; not organized as formal DO-178C verification cases, and no coverage (including MC/DC) has been measured |
| Software Life Cycle Environment Configuration Index | not yet written |
| Software Configuration Index | not yet written |
| Problem Reports | not yet tracked formally |
| Software Accomplishment Summary | not yet written (this document and `CERTIFICATION-GAPS.md` are informal precursors) |

## 7. Schedule

`[PLACEHOLDER: a real schedule, including stage-of-involvement reviews
with the certification authority or DER, requires an actual program with
committed dates and resources. No schedule is asserted here.]`

## 8. Additional Considerations

- **Previously developed software**: none of this codebase has prior
  certification credit from any other program; it would need to be
  developed to DAL A from this baseline forward.
- **Tool qualification**: both the eventual compiler/toolchain AND any
  verification/coverage tool used for certification credit require formal
  DO-330 qualification. Neither currently exists for this project.
- **Alternative methods**: none proposed at this stage.

---

## Honest bottom line

This PSAC draft is real, useful engineering-planning work: it names
exactly what a certification program for this software would need to
produce, and honestly marks what already exists versus what doesn't. It
is not itself certification, not a submission, not reviewed by anyone
with the authority to grant certification credit, and every placeholder
in it stays a placeholder until a real program supplies the missing,
program-specific facts — never invented here.
