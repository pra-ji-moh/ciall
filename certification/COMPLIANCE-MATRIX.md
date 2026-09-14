# DO-178C DAL A Compliance / Gap Matrix (draft)

## What this is, and what it is not

This is the kind of document a real DO-178C applicant writes for themselves,
early, before ever engaging a Designated Engineering Representative (DER) or
a certification authority — an honest self-assessment of where a codebase
stands against the standard's objectives, so the applicant (and anyone they
show it to) knows exactly what's done and what isn't. It is NOT a
certification artifact accepted by any authority, NOT reviewed by a DER, and
NOT a substitute for the real RTCA DO-178C document itself.

**Paraphrase disclosure:** DO-178C is a copyrighted RTCA/EUROCAE standard.
The objectives below are paraphrased from general knowledge of the
standard's structure, in my own words, for exactly the reason every other
document in this repo avoids reproducing external material verbatim — not
quoted, not a reliable substitute for the authoritative text. Before this
matrix is used for anything beyond internal orientation, a qualified DO-178C
engineer or DER must check every row against the real RTCA DO-178C: 2011
document (Annex A, Tables A-3 through A-10). Objective counts and exact
wording here may not match the standard precisely; the STATUS assessments
are honest given that caveat, not precise citations.

**Status legend:**
- **Satisfied** — real, verifiable evidence exists in this repo today.
- **Partial** — real evidence exists but falls short of what DAL A requires
  (commonly: not produced by a QUALIFIED tool, or not independently
  reviewed by a second party, which DAL A requires for most objectives).
- **Not Satisfied** — no meaningful evidence exists yet.
- **Requires External Party** — structurally cannot be satisfied by code or
  in-house engineering alone; needs a qualified toolchain vendor, a DER, or
  the certification authority itself.

---

## Table A-3 (paraphrased): Software Requirements Process

| # | Objective (paraphrased) | DAL A independence | Status | Evidence / Notes |
|---|---|---|---|---|
| 1 | High-level software requirements comply with system requirements | required | **Not Satisfied** | No system-level requirements or system safety assessment exist. This repo was built bottom-up as a general-purpose kernel library, not top-down from a certified aircraft/system architecture — the real starting point DO-178C assumes. |
| 2 | High-level requirements are accurate, unambiguous, and consistent | required | **Partial** | Each kernel's `buildPrompt`/`normalize`/`run` shape and `kernelExport.js`'s `KERNEL_SPECS` (algorithm, inputSchema, outputSchema, abstentionConditions) function as informal requirements. Real, but never independently reviewed against a formal requirements document by a second party. |
| 3 | High-level requirements are compatible with the target computer | n/a until target is known | **Not Satisfied** | No target embedded hardware has ever run this code. Only Node.js/V8 has been exercised. |
| 4 | High-level requirements are verifiable | required | **Partial** | Every kernel's behavior IS independently checkable (exhaustive cross-checks, known analytical solutions, published reference figures — see `CERTIFICATION-GAPS.md`), but this was verified against the CODE's actual behavior, not against a separately-authored requirements document, which is what this objective actually asks for. |
| 5 | High-level requirements conform to a defined standards document | required | **Not Satisfied** | No adopted requirements-authoring standard exists for this project. |
| 6 | High-level requirements are traceable to system requirements | required | **Not Satisfied** | No system requirements exist to trace to (see #1). |
| 7 | Algorithms are accurate | required | **Satisfied** | Genuinely strong evidence here: SAT solver cross-checked against brute force (400 random instances), RK45 checked to <1e-8 relative error against closed-form linear systems, MCMC reproducibility checked bit-for-bit, published-figure cross-checks for the neuromorphic-power kernel. This is the one area of real algorithmic rigor already in place. |

## Table A-4 (paraphrased): Software Design Process

| # | Objective (paraphrased) | DAL A independence | Status | Evidence / Notes |
|---|---|---|---|---|
| 1 | Low-level requirements comply with high-level requirements | required | **Not Satisfied** | No formal low-level requirements documents exist separate from the source code itself. |
| 2 | Low-level requirements are accurate and consistent | required | **Not Satisfied** | Same gap as above. |
| 3 | Software architecture is compatible with high-level requirements and is consistent | required | **Partial** | `kernelRegistry.js`'s documented common interface (every kernel: `buildPrompt`/`normalize`/`run`, deterministic-vs-seeded-stochastic declared explicitly) is a real, consistent architecture — genuinely helps here — but was never independently reviewed as a DESIGN ARTIFACT separate from the implementation. |
| 4 | Software architecture is compatible with the target computer | n/a until target is known | **Not Satisfied** | See Table A-3 #3. |
| 5 | Software partitioning integrity is confirmed (if partitioning is used) | required | **Not Applicable (currently)** | This system does not currently claim or implement partitioned execution in the DO-178C sense; would need to be addressed if a real target architecture uses it. |

## Table A-5 (paraphrased): Software Coding & Integration Process

| # | Objective (paraphrased) | DAL A independence | Status | Evidence / Notes |
|---|---|---|---|---|
| 1 | Source code complies with low-level requirements and software architecture | required | **Not Satisfied** | No separate low-level requirements to comply with (see Table A-4). |
| 2 | Source code is verifiable | required | **Partial** | The JavaScript kernels are verifiable and extensively verified (422 tests). The C port in `certifiable-c/` has NOT been compiled or run in this environment — its logic was cross-validated via a JS simulation, but the actual C source itself carries zero direct verification evidence yet. |
| 3 | Source code conforms to a defined coding standard | required | **Not Satisfied** | No adopted, enforced coding standard (a MISRA-C-equivalent discipline was followed BY HAND in `certifiable-c/` — every loop bound is a compile-time constant, zero malloc/recursion — but this is a self-imposed discipline, not a document a QA process checks conformance against.) |
| 4 | Source code is traceable to low-level requirements | required | **Not Satisfied** | Same root cause as #1. |
| 5 | Output of the software integration process is complete and correct | required | **Partial** | The 422-test suite genuinely exercises cross-kernel integration (`orchestrator.js`'s `runPipeline`, the remote-kernel routing, the event-stream router) — real evidence, but produced by an unqualified test runner (Node's built-in `node:test`), not a DO-330-qualified verification tool. |

## Table A-6 (paraphrased): Testing of Outputs of Integration Process

| # | Objective (paraphrased) | DAL A independence | Status | Evidence / Notes |
|---|---|---|---|---|
| 1 | Executable object code complies with high-level requirements (normal range) | required | **Not Satisfied** | No qualified "executable object code" exists — this runs interpreted/JIT-compiled under V8, and the C port has never been compiled. Neither has been run through a real requirements-based test campaign against a target. |
| 2 | Executable object code is robust to abnormal/out-of-range inputs (robustness testing) | required | **Partial** | Real, if informal: every kernel's `normalize()` rejects malformed input loudly (extensively tested — malformed specs, adversarial inputs, budget-exhaustion paths). This is genuine robustness behavior, not yet organized as a formal robustness test campaign against documented requirements. |
| 3 | Test coverage of low-level requirements is achieved | required | **Not Satisfied** | No low-level requirements exist to measure coverage against. |
| 4 | Test coverage of software structure — statement coverage | required (all DALs) | **Partial** | Now genuinely measured (2026-08-05, `npm run test:coverage`, Node's built-in `--experimental-test-coverage`): **65.06% line coverage** across `src/`, file-by-file, honestly reported including the weak spots (`jsonExtract.js` 47.46%, `mcmcSearch.js` 57.19%). Real progress over "not measured," but the tool itself is not DO-330 qualified, so this is not admissible certification evidence yet — only genuine engineering signal. |
| 5 | Test coverage of software structure — decision coverage | required (DAL A/B) | **Partial** | Node's coverage tool reports **76.21% branch coverage** across `src/`, a reasonable proxy for decision coverage though not identical to the DO-178C definition. Same qualified-tool caveat as row 4 applies. |
| 6 | Test coverage of software structure — Modified Condition/Decision Coverage (MC/DC) | required (DAL A only) | **Requires External Party** | Not measured, and cannot be meaningfully measured without a DO-330-qualified coverage tool — an unqualified tool's MC/DC number would not be admissible evidence regardless of what it reports. This is explicitly named in `CERTIFICATION-GAPS.md` as one of the hard, external-party-dependent gaps. |
| 7 | Test coverage of data coupling and control coupling between code components | required | **Not Satisfied** | Not measured. |

## Table A-7 (paraphrased): Verification of Verification Process Results

| # | Objective (paraphrased) | DAL A independence | Status | Evidence / Notes |
|---|---|---|---|---|
| 1 | Test cases are correct and their results are correctly evaluated | required | **Partial** | Every test in this suite asserts a specific, independently-derivable expected result (hand-computed physics in `physicalActionGate.test.mjs`, known analytical solutions in `dynamicsSmooth.test.mjs`, published figures in `neuromorphicPower.test.mjs`) rather than a snapshot or a loose sanity check — genuinely good practice, but never independently re-reviewed by a second engineer as this objective requires. |
| 2 | Test coverage analysis is correct and complete | required | **Not Satisfied** | No coverage analysis has been performed at all yet (see Table A-6 #4-7). |
| 3 | Traceability between test cases, requirements, and code exists and is reviewed | required | **Not Satisfied** | No formal requirements exist to trace tests against (root cause shared with Table A-3/A-4). |

## Table A-8 (paraphrased): Software Configuration Management Process

| # | Objective (paraphrased) | Status | Evidence / Notes |
|---|---|---|---|
| 1 | Configuration items are identified | **Partial** | This is a real git repository with identifiable files, but as of this delivery a substantial amount of work across several upgrades remains **uncommitted in the working tree** (verified directly via `git status` during this delivery, not glossed over). Real CM discipline requires regular baselining; this has not been consistently practiced. |
| 2 | Baselines and traceability of configuration items are established | **Not Satisfied** | No formal baseline/release process exists — no tags, no versioned releases beyond the informal "Upgrade N" naming used in conversation and in the project memory notes, which is not itself part of this repo's own artifacts. |
| 3 | Problem reporting, change control, change review, and archival/retrieval are defined | **Not Satisfied** | No formal problem-reporting or change-control process exists; issues found during development (e.g. the bounded-execution gap found this upgrade) were fixed directly rather than tracked through any formal PR/CR process. |

## Table A-9 (paraphrased): Software Quality Assurance Process

| # | Objective (paraphrased) | Status | Evidence / Notes |
|---|---|---|---|
| 1 | An independent SQA function reviews process compliance and transition criteria | **Not Satisfied** | No independent SQA function exists. All engineering, testing, and self-review of this codebase has been performed within the same development process (a single AI-assisted engineering session), which does not satisfy DAL A's independence requirements — this is structurally the same gap as "no qualified toolchain, no DER": it requires a genuinely separate party, not more code. |

## Table A-10 (paraphrased): Certification Liaison Process

| # | Objective (paraphrased) | Status | Evidence / Notes |
|---|---|---|---|
| 1 | Communication and coordination occurs with the certification authority throughout the program | **Requires External Party** | Zero engagement with any certification authority or DER has occurred. This cannot be satisfied by engineering work of any kind. |
| 2 | Compliance with the standard is substantiated (Software Accomplishment Summary, etc.) | **Not Satisfied** | No such substantiation exists; this matrix and `CERTIFICATION-GAPS.md` are informal precursors to what a real Software Accomplishment Summary would eventually need to contain. |
| 3 | Open problem reports and deviations from the plan are disclosed to the authority | **Requires External Party** | No authority relationship exists to disclose to. |

---

## Summary

Of the objectives represented above, roughly a handful are genuinely
**Satisfied** or close to it (algorithm accuracy, informal architecture
consistency, robustness-adjacent input validation), a larger number are
**Partial** (real evidence exists but falls short of DAL A's independence
or qualified-tool requirements — this now includes statement and branch
coverage, genuinely measured at 65.06%/76.21% as of 2026-08-05, up from
"not measured"), and the majority — everything gated on system-level
requirements, a qualified toolchain, MC/DC, formal CM/QA processes, and
any authority engagement — are **Not Satisfied** or **Requires External
Party**. This is consistent with, and should be read alongside,
`CERTIFICATION-GAPS.md`'s plain statement: DAL A certification is not
achieved and is not achievable through code changes alone. Measured
coverage moving several rows from "not measured" to "measured, not yet
qualified-tool-admissible" is real progress of exactly the kind this
matrix exists to track honestly — it is not, and is not presented as,
progress toward the objective itself being satisfied.
