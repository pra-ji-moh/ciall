# DO-178C DAL A certification — an honest gap accounting

Same discipline as `POSITIONING.md` and `SECURITY.md`: this states what's
real, what's verified, and what's still missing, plainly. Nothing here
claims certification, certification-readiness, or a path to either that
skips the parts that actually require a certification authority. If
you're evaluating this repo for a safety-critical/DAL A context, read
this file first, not the source comments.

## The one-sentence answer

**No.** This is not DO-178C DAL A certified, is not on a code-only path
to becoming certified, and cannot become certified by adding more
JavaScript (or, on its own, more C) to it. Certification is a
regulatory and toolchain-qualification process, not a coding
achievement, and this repo has engaged with neither.

## Why "just write more careful code" doesn't close this gap

DAL A certification requires, at minimum, all of the following — none
of which exists for this project:

1. **A qualified toolchain.** The COMPILER needs DO-330 tool
   qualification evidence from its vendor. This is a commercial/legal
   relationship with a qualified vendor (or a very expensive in-house
   qualification effort), not a compiler flag. Node.js's V8 has never
   been qualified this way and — being a JIT-compiling, garbage-
   collected general-purpose JS engine — realistically never will be.
2. **A non-garbage-collected implementation language.** No garbage-
   collected language has ever achieved DAL A certification. This is
   not a JavaScript-specific limitation; it rules out this repo's
   current implementation language categorically, independent of how
   carefully the code within it is written.
3. **Formal WCET (worst-case execution time) proof**, via a real static
   timing analyzer (e.g. AbsInt aiT, Bound-T) characterized against the
   ACTUAL TARGET HARDWARE the code will run on. "Every loop has a
   bounded iteration count" (true here, see below) is necessary but a
   small fraction of what WCET proof requires — it also needs
   cycle-accurate modeling of cache behavior, pipeline stalls, and
   interrupt latency on specific silicon, which cannot be established
   in the abstract.
4. **MC/DC (Modified Condition/Decision Coverage) evidence**, gathered
   by a QUALIFIED coverage tool under an approved test procedure. This
   repo's test suite uses Node's built-in test runner with line/branch
   coverage at best — genuinely useful for catching bugs, not MC/DC,
   and not gathered by a qualified tool.
5. **A traceability matrix** from certified REQUIREMENTS documents
   (not source comments, however thorough) through design, code, and
   test artifacts.
6. **Actual engagement with a certification authority** (FAA/EASA) or a
   delegated engineering representative (DER). This repo has none.

## What IS real and verified here

- **Bounded-execution audit (2026-08-05).** Every kernel's loops were
  checked directly, not assumed:
  - `consistencyKernel.js`: forward-chaining fixpoint bounded by
    `MAX_ROUNDS=64`; contamination-propagation BFS bounded by the
    finite atom graph (terminates for any finite input via visited-
    tracking, though not independently constant-bounded — see the
    caveat below).
  - `mcmcSearch.js` (MH and SMC): bounded by `MAX_TOTAL_EVALS`/
    `TIME_BUDGET_MS` (MH) and `SMC_MAX_GENERATIONS`/`SMC_TIME_BUDGET_MS`
    (SMC), checked every iteration.
  - `rk45.js` (the smooth integrator's adaptive stepper): bounded by
    `maxSteps=100000`, checked every step; on exceeding it, throws —
    caught by `dynamicsCheck.js`'s caller and converted to an honest
    `inconclusive`/`blewUp` result, never left to crash or hang.
  - `satKernel.js`: the main CDCL solve loop is bounded by
    `maxConflicts`/`maxMs`, checked on every conflict, and degrades
    gracefully to `{sat: null, reason: 'budget exhausted'}` — verdict
    `undecided`, never fabricated as a proof.
  - `numericCheck.js`: range-scan loops check `TIME_BUDGET_MS` every
    iteration regardless of how large the requested range is, and
    degrade to a `partial` result rather than running unboundedly.
  - `orderConsistency.js`: BFS bounded by the finite relation graph via
    visited-tracking.
  - `eventCameraPixel.js` (upgrade 10): **a real gap was found and
    fixed during this specific audit** — the burst-event loop (firing
    one event per full threshold-worth of log-intensity drift) had no
    independent cap; an adversarially extreme but validly-finite
    intensity ratio against a tiny threshold could burst an unbounded
    number of events. Fixed with `MAX_BURST_PER_TRANSITION` and
    `MAX_EVENTS_TOTAL` constants, checked every iteration, throwing a
    specific error caught by `verifyEventLog` and converted to an
    honest `inconclusive` — same pattern as `rk45.js`/`satKernel.js`.
    See `tests/eventCameraPixel.test.mjs`'s two "bounded-execution
    audit" tests.
  - **Caveat, disclosed rather than glossed over:** several bounds
    above (`consistencyKernel.js`'s BFS, `orderConsistency.js`'s BFS)
    are "terminates for any finite input," not "bounded by a fixed
    constant independent of input size." That distinction is exactly
    what separates ordinary defensive engineering from WCET-provable
    code: a real DAL A effort would need every CALLER's input size
    independently, statically bounded too, which this repo's own
    direct-caller-bypasses-normalizeCommitments design explicitly does
    NOT do (see `consistencyKernel.js`'s own comments on this).
- **Real, measured structural coverage — not MC/DC, but not "unmeasured"
  anymore either (2026-08-05, `npm run test:coverage`, Node's built-in
  `--experimental-test-coverage`, zero new dependencies).** Across the
  full 487-test suite: **65.06% line, 76.21% branch, 57.16% function**
  coverage over `src/`. This is honestly reported, not rounded up: several
  files sit well below the average (`mcmcSearch.js` 57.19% line/46.43%
  branch — its SMC particle-swarm and worker-dispatch paths are the least
  exercised; `numericCheck.js` 63.71% line; `jsonExtract.js` 47.46% line;
  `domainOfValidity.js` 67.62% line). This is real evidence toward Table
  A-6's coverage objectives in `certification/COMPLIANCE-MATRIX.md` —
  genuinely better than "not measured" — but still explicitly NOT MC/DC
  (Node's test runner reports line/branch/function coverage, not
  Modified Condition/Decision Coverage), and NOT gathered by a DO-330
  qualified tool. Both gaps remain open and are not claimed as closed.
- **Deterministic OUTPUT, not deterministic TIMING.** Every seeded-
  stochastic kernel in this repo (`mcmc`, `dynamics`) is proven
  bit-for-bit reproducible in its OUTPUT, extensively, throughout this
  repo's test suite. That is a genuinely different and much weaker
  property than deterministic TIMING. V8's JIT compilation and garbage
  collector mean the same call can take measurably different wall-clock
  time run to run even when it returns the identical answer every time
  — see `tests/timingVariance.test.mjs` (upgrade 11) for real, measured
  numbers, not a hand-waved acknowledgment.
- **A statically-allocated, no-heap C port exists for one kernel**
  (`certifiable-c/boundary_kernel.c`, the device-action scope check),
  demonstrating the ALGORITHM has no inherent dependency on garbage
  collection — that was always a property of the implementation
  language, not the logic. Read `certifiable-c/boundary_kernel.h`
  before trusting it: **it was written but NOT compiled or run** (no C
  toolchain was available in the environment that wrote it). Its
  algorithm was cross-validated against the real, tested JS reference
  via a JS simulation of the same bounded-buffer logic (0 mismatches
  across 10 cases, including the trickiest edge case — a "look-alike
  sibling" path). That validates the LOGIC. It does not validate the C
  syntax. Compile and run `certifiable-c/test_boundary_kernel.c`
  yourself before relying on it for anything.

- **A real avionics data bus word format, and a real externally-enforced
  execution deadline (2026-08-05, upgrade 14) — asked directly what
  would go wrong equipping this into an aircraft "tomorrow."** The
  honest answer named two concrete, buildable gaps among the many
  non-buildable ones (no DO-160 environmental qualification, no real
  actuator/sensor hardware, no fail-safe architecture at the system
  level — none of those changed). The two that WERE buildable:
  - `src/lib/arinc429.js`: a real ARINC 429 word codec (label/SDI/data/
    SSM/parity, the actual 32-bit format civil transport aircraft data
    buses use) — a real avionics wire format now exists where there was
    none, validated against hand-computed bit patterns (see the test
    file's own worked arithmetic), independently RECOMPUTING parity on
    decode rather than trusting a word's own claim, the same discipline
    as this repo's other wire-format codecs (`fixProtocol.js`,
    `msgpack.js`). This is the word FORMAT only — no physical-layer
    driver exists or could exist in this environment (no serial
    hardware access), and label MEANINGS are aircraft/ICD-specific and
    out of scope.
  - `src/lib/failSafeWrapper.js`: runs a kernel in a dedicated worker
    thread with a deadline enforced by a SEPARATE thread, not the
    kernel's own cooperation — forcibly terminates and returns a defined
    fail-safe verdict on overrun, unlike every kernel's own internal
    `TIME_BUDGET_MS` (which depends on the kernel's own code reaching
    its own clock check; a GC pause or an infinite-loop bug could blow
    through that silently). Verified against a REAL worker spawn and a
    REAL mcmc search, not a fake sleep — empirically, worker-spawn
    latency alone exceeded a 5ms deadline every time this was tested.
    That number is itself the honest disclosure: worker spawn/terminate
    latency is OS-scheduler-dependent and NOT proven bounded anywhere in
    this file. This is a real safety PATTERN (externally-enforced
    deadline, defined fail-safe output), not a certified fail-safe
    architecture, and makes no claim to be real-time-safe.
  - Neither of these, together or separately, changes the answer to
    "should this fly tomorrow." They are real, working, tested pieces of
    what a genuine avionics integration effort would need to build on —
    the same relationship `certifiable-c/` has to real DAL A
    certification: honest progress on a named sub-problem, not a claim
    the larger problem is solved.

## If you actually need this certified

Porting the remaining 9 kernels to a certifiable-subset language,
compiling and testing all of them for real, running static WCET
analysis against real target hardware, gathering MC/DC coverage with a
qualified tool, building the requirements traceability matrix, and
engaging a DER — is a multi-year, specialist undertaking, not a follow-
up task. This document exists so that fact is on the record, not
discovered later.
