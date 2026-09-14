# ciall-substrate

The kernel + MCMC + human-in-loop claim-verification engine, extracted
from [Ciall/Panton](../client-backend-fixed) as its own standalone,
domain-agnostic project. See [VISION-PLTR.md](VISION-PLTR.md) for why this
was split out and where it's headed.

This is deliberately separate from the consumer app it came from: the app
is one product built on the engine (idea stress-testing); this repo is the
engine itself, meant to be pointed at other claim domains (engineering
margins, model-risk validation, requirements consistency, and the rest of
the list in the vision doc) without dragging the app's UI, auth, billing,
or marketing surface along with it.

## What's here

- `src/lib/` — the pure kernel substrate. Every file is dependency-free
  (only relative imports between kernels, zero npm packages): deterministic
  or seeded-stochastic, reproducible, no network calls, no model in the
  deciding step.
  - `consistencyKernel.js` — deterministic contradiction detection across
    logical, numeric, dimensional, and ordering commitments
  - `mcmcSearch.js` — seeded Metropolis-Hastings counterexample search
    (the default), plus an opt-in parallel Sequential Monte Carlo mode
    (`{mode:'smc', N}` on the spec) — see "Parallel execution" below
  - `numericCheck.js` — sampled/exact inequality and identity checking
  - `dynamicsCheck.js` — dynamics-equivalence checking, fixed-step RK4
    (the default), plus an opt-in adaptive-step smooth integrator mode
    (`{integrator:'smooth'}` on the spec) whose vector field is a 2-layer
    MLP instead of an analytical equation of motion — see "Smooth
    integrator mode" below
  - `rk45.js` — generic adaptive-step Dormand-Prince RK45, the smooth
    integrator's stepping algorithm; knows nothing about MLPs or WASM
  - `mlpWasm.js` / `mlpVectorField.js` — the smooth integrator's vector
    field: a hardcoded, pre-compiled WASM module (2-layer MLP forward
    pass, tanh hidden layer) plus the glue that wires it up to rk45.js
  - `domainOfValidity.js` — domain-of-validity narrowing
  - `combinatorialSearch.js` / `satKernel.js` — SAT-based existence proofs
  - `orderConsistency.js` — ranking-cycle detection
  - `dimensionalAnalysis.js` — dimensional-balance checking
  - `measurementTension.js` — sigma-based measurement-tension scoring
  - `boundaryKernel.js` — device-action scope membership (path
    containment / allowlist), registered in the kernel registry like any
    other kernel. `deviceGate.js` consumes this rather than implementing
    its own copy of the check — see VISION-PLTR.md section 8 for why that
    distinction matters and the gap it closed.
  - `chainKernel.js` — compositional exploit-chain reachability: given a
    set of findings OTHER kernels already verified (each with stated
    preconditions/postconditions), decides whether a well-founded firing
    sequence reaches a stated target finding, via a bounded/layered
    SAT-planning encoding run through `satKernel.js` — never by LLM
    reasoning about the findings' prose. A SAT witness is cross-checked
    by a second, independent, non-SAT fixpoint replay; an UNSAT verdict
    is independently re-checked via `dratProof.js`'s
    `checkChainReachabilityProof`. Severity comes from an explicit,
    disclosed rule table (`SEVERITY_RULE_TABLE`), never summed or
    averaged from the individual findings. Produces no PoC, payload, or
    trigger sequence — only which findings chain, the reachability
    proof, and a table-driven severity; a human decides what (if
    anything) to do next.
  - `selfAudit.js` — the pattern-based source scanner (not registered in
    `kernelRegistry.js`; a standalone module, see `bin/ciall.mjs`'s
    `self-audit` command). Its rules are validated against a real,
    pinned 40-CVE benchmark in `bench/` (see `bench/FINDINGS.md`) — real
    recall (64.3% on the 14/40 entries it even claims to cover, 0 clean
    catches) and real, disclosed limitations, not an assumed number.
    `scanCodeText` is extracted specifically so other modules (below)
    can run the same red-flag rules against arbitrary text, not just
    repo files.
  - `iterativeVerify.js` — an iterative, multi-round verification loop
    over the model-designed kernels: round 1 proposes a spec and gets a
    real deterministic result (reusing `modelDesignSpec.js` unchanged);
    round 2+ has the model see that REAL result and decide to accept,
    revise (a new spec, possibly a different kernel — never a louder
    restatement of the same unverified claim), or propose a real action
    (which stops the loop untouched; nothing here ever calls
    `confirm()`). Every model response is scanned via `selfAudit.js`'s
    `scanCodeText` before use. `bin/ciall.mjs`'s `verify-iterative`
    command runs it live.
  - `shiftLeftScan.js` — wires `selfAudit.js`'s real findings straight
    into `chainKernel.js`'s SAT+DRAT-proven reachability, so a codebase
    can be checked for a *compositional* risk before any CVE is ever
    filed against it — zero model calls, zero non-determinism. A small,
    disclosed mapping table turns specific finding codes into chain
    atoms (one real, documented composition shipped: a prototype-
    pollution primitive chaining into `unreviewed-child-process`);
    everything is namespaced per file so unrelated files can never
    compose just by sharing an atom name. `bin/ciall.mjs`'s
    `shift-left-scan` command runs it.
  - `kernelRegistry.js` — the common `{buildPrompt, normalize, run}`
    interface every kernel above follows; the seam a new claim (or
    device-action) domain plugs into
  - `orchestrator.js` — Phase 2: runs a claim through every applicable
    kernel and returns one aggregated report, instead of a caller
    invoking each kernel by hand. Does not call a model itself; the
    spec-design step is injected by the caller. Runs every kernel's
    `run()` step in parallel by default (`{parallel:false}` restores the
    original sequential behavior exactly) — see "Parallel execution"
    below.
  - `deviceGate.js` — Phase 3: permission/audit scaffold for device
    actions. Deny-by-default, scoped + expiring grants only, full audit
    log. No execution itself — a persisted grant only says a target is
    in-bounds, never that an action may proceed.
  - `sandboxConfig.js` / `deviceExecutor.js` — Phase 4: the real
    executor, scoped to one persisted sandbox directory
    (`.ciall-sandbox.json`, defaults to a sibling `../ciall-sandbox`
    folder — never this project's own folder, never anything broader).
    `deviceExecutor.js` is the only file here that touches `node:fs`.
    Every `writeFile`/`readFile` call requires its own `confirm(action)`
    function and refuses to run without one — no grant, however
    long-lived, ever substitutes for a fresh per-action approval. Paths
    are resolved through `realpathSync` so a symlink can't be used to
    escape the sandbox. Every call (approved or refused) is appended to
    a persisted audit log (`.ciall-execution-audit.jsonl`).
  - `commandExecutor.js` — runs a command via `execFile` (never a shell),
    with the identical discipline: no active `process-launch` grant, no
    run; grant but no `confirm()`, no run.
  - `jsonExtract.js` / `modelClient.js` / `geminiClient.js` /
    `modelDesignSpec.js` — the model dependency this substrate previously
    deliberately avoided, added so consistency/mcmc/dynamics/etc. can run
    live, not just numeric-check. Two providers, identical interface
    (`modelClient.js` = Claude, `geminiClient.js` = Gemini, both take the
    API key as a parameter, neither reads `process.env` itself, both stay
    unit-testable with a mocked `fetch`); `bin/ciall.mjs` is the ONLY
    place in this repo that reads `GEMINI_API_KEY`/`ANTHROPIC_API_KEY`
    from the environment. Gemini is the default provider for `--live`
    (current preference); `--provider anthropic` switches to Claude. See
    VISION-PLTR.md sections 10-11 for the full account, including why
    each kernel's `buildPrompt()` needed its own adapter case.
- `bin/ciall.mjs` — **the actual runnable CLI.** Everything above this
  point required a `confirm` function that nothing supplied, so none of
  it could do anything on its own. This file supplies a real one: it
  prints the exact proposed action to the terminal and blocks on a
  literal `y`/`N` — press anything but `y` and nothing happens. Run it
  directly:
  ```
  node bin/ciall.mjs write notes/idea.txt "some content"
  node bin/ciall.mjs write notes/claim.txt "x^2 is never negative" --check "-1<=x^2" --var x=-10:10
  node bin/ciall.mjs read notes/idea.txt
  node bin/ciall.mjs run node -e "console.log('hi')"
  node bin/ciall.mjs audit
  ```
  `write ... --check` runs `numeric-check` deterministically, no model
  needed, spec built straight from `--check`/`--var` flags.
  `write ... --live [core|all|k1,k2] [--provider gemini|anthropic]` runs
  the CONTENT through real model-backed kernels (consistency, mcmc,
  numeric-check, dynamics by default; add `combinatorial`/
  `domain-of-validity` explicitly) via a real model call. Defaults to
  Gemini (`GEMINI_API_KEY`); `--provider anthropic` switches to Claude
  (`ANTHROPIC_API_KEY`). Either key is read from your environment only —
  never any other way. Either way, a violation doesn't auto-block the
  write; you still decide, now informed rather than blind.
  This is the first thing in this repo that touches the real, persisted
  sandbox (`../ciall-sandbox`) and the real audit log
  (`.ciall-execution-audit.jsonl`, gitignored — it's runtime state, not
  source) rather than a test double.
- `tests/registry.test.mjs`, `tests/orchestrator.test.mjs`,
  `tests/deviceGate.test.mjs`, `tests/deviceExecutor.test.mjs`,
  `tests/commandExecutor.test.mjs` — coverage for everything above;
  every test runs against a temp sandbox via env-var overrides, never
  the real persisted one
- `tests/sat.test.mjs`, `tests/mathExprLet.test.mjs` — carried over
  unchanged from the source app; test only files that live here

## What's NOT here (on purpose)

No React, no Vite, no auth, no billing, no marketing pages, no Anthropic
API client. This is the falsification engine only. A product built on top
of it (the original app, or a new domain-specific one) is a separate
consumer of this package, not folded into it.

## Parallel execution

Two layers, added incrementally, sharing ONE pre-warmed 4-worker pool
(`src/lib/kernelWorkerPool.js` + `src/lib/kernelWorker.js`) — never a
second, nested pool.

**Kernel-level (upgrade 1).** `orchestrator.js`'s `runPipeline()` runs
every applicable kernel's `run()` step in the pool by default, so
independent kernels for the same claim execute concurrently instead of
back-to-back; `{parallel:false}` restores the original strictly
sequential behavior. Kernel specs are handed to workers via
`postMessage`'s structured-clone (not a raw `SharedArrayBuffer`): specs
carry free-form math-expression strings and nested commitment objects,
not numeric vectors, so a zero-copy binary handoff would need a
bespoke encoder for no real benefit over the serialization Node already
gives every worker for free. A process that uses the parallel path and
wants to exit afterward (a script, a test) must call `shutdownPool()`
from `kernelWorkerPool.js` explicitly — see that file's SHUTDOWN note;
`worker.unref()` alone is not reliable once a worker has completed a
task (verified empirically on Node v24/Windows).

**Sampler-level (upgrade 2).** `mcmcSearch.js`'s Metropolis-Hastings
mode (the default) is untouched. An opt-in Sequential Monte Carlo mode
(`{mode:'smc', N}` on the spec) runs N particles across the SAME
4-worker pool: `src/lib/smcWasm.js` holds a hand-authored, pre-compiled
WebAssembly module (checked in as a base64 constant — no compiler runs
at any point after this repo is cloned) that does the one fixed,
claim-independent step of the generation loop — converting each
particle's margin into an importance weight via `exp(margin/temperature)`
— reading and writing directly through a `WebAssembly.Memory` created
with `shared:true`, so every worker's WASM instance touches the exact
same underlying bytes as every other worker and the main thread, with no
per-element messaging. Evaluating the claim's own (arbitrary,
caller-supplied) objective still happens in JS via `mathExpr.js`, same
as MH — a single fixed WASM module can't evaluate arbitrary claim
expressions without reimplementing that whole interpreter in hand-written
bytecode, so the split is: JS evaluates the claim, WASM does the fixed
numeric reweighting. Systematic resampling runs on the main thread once
per generation, single O(N) pass, using two buffers allocated once at
search start (never inside the generation loop). Every particle's
randomness derives from `combineSeed(masterSeed, particleIndex,
generation)` — reproducible bit-for-bit regardless of which worker
happens to process which slice, or in what order tasks complete.

Because SMC's own `run()` call drives that same 4-worker pool
internally, its normalized spec sets `selfParallel: true` so
`orchestrator.js` calls it directly (awaited) on the calling thread
instead of dispatching it into one of the pool's own workers first —
that would nest a 4-way fan-out inside a single worker rather than
running it alongside the pool's other three. This is also why `run()`
is documented as synchronous "with one exception" in
`kernelRegistry.js`: SMC's `run()` returns a Promise (it awaits real
worker_threads tasks); every caller in this repo already awaits
`run()`'s return value unconditionally, which is a no-op for every
other, synchronous kernel.

## Smooth integrator mode

`dynamicsCheck.js`'s default mode integrates both systems with seeded
fixed-step RK4 over analytically-specified derivatives (mathExpr
expressions). `{integrator:'smooth'}` on the spec swaps BOTH halves of
that: the vector field becomes a 2-layer MLP (`mlpA`/`mlpB` weights —
`w1`: inputDim×64, `b1`: 64, `w2`: 64×inputDim, `b2`: inputDim, tanh
hidden layer — set directly from the spec's own numbers, no training, no
model files) instead of an equation of motion, and the stepper becomes
adaptive RK45 (Dormand-Prince, `rk45.js`) instead of fixed-step RK4.
Everything else about the check — state variable naming, seeded initial
conditions, the observables being compared, the decisiveness/step-
halving audit — is identical between the two modes, sharing code
(`decisivenessSurvived` in `dynamicsCheck.js`) rather than being
reimplemented; only the vector-field evaluation and the stepping
algorithm actually differ.

The MLP's forward pass runs in `mlpWasm.js`, a hand-assembled,
pre-compiled WASM module (checked in as a base64 constant — no compiler
runs at any point after this repo is cloned), validated bit-for-bit
against a plain-JS reference implementation across several state
dimensions before being trusted. A single fixed WASM module can't
evaluate an ARBITRARY claim's derivatives (that's what the analytical
RK4 path is for), so the split is: the MLP is the vector field itself
(a function purely of state, no explicit time-dependence), evaluated
fully in WASM including the hidden layer's `tanh` (imported from the
host — WASM's numeric instruction set has no transcendental functions,
and a hand-rolled polynomial approximation would trade a well-tested
implementation for an untested one for no benefit).

Unlike upgrade 2's SMC module, this one never touches worker_threads —
dynamicsCheck.js calls into it directly on whatever thread is already
running — so it's instantiated with Node's synchronous
`new WebAssembly.Module()`/`new WebAssembly.Instance()` rather than the
async `WebAssembly.instantiate()` convenience wrapper, keeping the
smooth-mode `run()` path fully synchronous. No orchestrator.js changes
were needed for this upgrade at all.

Accuracy: on identical underlying dynamics (an MLP hand-constructed via
a tiny-epsilon tanh-linearization to reproduce a specific linear vector
field, compared against the equivalent RK4-mode spec using that same
linear field as literal expressions), smooth mode's computed deviation
matches RK4 mode's to within roughly 1e-10 relative error — comfortably
inside the 1e-8 bound `tests/dynamicsSmooth.test.mjs` enforces as a
regression test, across both "matched" and "diverged" verdicts and more
than one underlying system.

## Running the tests

```
npm test
```

Slow but deterministic: `tests/mcmcSmc.test.mjs`'s benchmark test (SMC
vs MH detection rate at an equal wall-clock budget) takes roughly a
minute by design — both samplers run their full ~4.5s time budget
across a fixed battery of claims. Every trial parameter is fixed (no
`Math.random` anywhere in this repo), so the result is fully
reproducible, not a statistical average that could flip between runs.

## Camera capture — a built-in capability, not a one-off test

`src/domains/camera.js`: `captureStillImage()` invokes ffmpeg through the
exact same confirm-gated `commandExecutor.runCommand()` every other
command in this repo already uses — no new execution path, no bypass.
`verifyImageResolutionClaim()` checks a claim about the captured image's
actual resolution via `src/lib/imageMetadata.js`, a dependency-free JPEG
parser (walks marker segments to the SOF marker; no image-decoding
library, consistent with this substrate's zero-dependency design).

Verified with a real JPEG (generated once via ffmpeg's synthetic
`lavfi` color source — no camera, no hardware involved in producing the
test fixture, since it's embedded in the test suite so tests never
depend on ffmpeg being installed elsewhere): the parser correctly read
320x240 from real binary data, and correctly caught a false resolution
claim against the same file.

The actual camera-hardware invocation path (`-f dshow -i
video="Integrated Webcam"`) is built and tested for its argv
construction and confirm-gate behavior — but running it for real against
live hardware is gated by Claude Code's own harness-level permission
classifier, independent of anything in this repo. That's not a gap in
the software; the capability is real and complete, waiting on the
runtime permission a human (not Claude) grants.

### Frame differencing as a trigger, not a vision system

`src/lib/frameDifference.js`'s `computeFrameDifference()` /
`triggerOnFrameChange()` compute the absolute pixel difference between
two raw frames and fire a callback when it crosses a threshold — pure
arithmetic, no ML, no object/face/person detection, no external
dependency. The boundary is deliberate: this measures HOW MUCH changed,
never WHAT changed. It does not do scene understanding.

The intended shape: vision decides *when* something is worth checking;
the existing kernels (unmodified) decide what a check of it actually
finds. Live-verified end to end: a synthetic frame change triggered at a
measured mean difference of 4.0 (threshold 1.0), handed off to
`numericCheck.js`'s kernel, which independently verified a claim about
the exact measured difference — vision never touches the verdict, only
the kernel does.

## MCMC speed, measured (not assumed)

`mcmcSearch.js` was already fast before touching anything: a real
2-parameter, 4-chain, 17,604-evaluation search completed in 69ms on this
machine (~255,000 evaluations/sec), well under the kernel's own 4500ms
budget. No changes were made to the kernel's numeric behavior — it's
bit-for-bit deterministic and every existing test (including many
domains' exact verdicts) depends on that; the honest finding was that
there was no real bottleneck to fix, not that one was found and ignored.

## Domain-bias correction: adaptive search-space expansion

`src/lib/adaptiveMcmc.js`'s `runAdaptiveMcmcSearch()` addresses a real,
specific bias: the human/model-chosen search DOMAIN itself. If a domain
happens to exclude the real counterexample, the kernel correctly reports
"held" within it — and that verdict gets trusted, even though a genuine
violation sits just past the boundary someone picked. The signal: a best
point sitting within ~3% of a domain's edge means the search was pulled
toward that boundary, so the function automatically re-runs with an
expanded domain (capped, and every expansion recorded and returned, never
hidden) instead of trusting the original bound uncritically.

`mcmcSearch.js` itself is completely unmodified — this wraps it. Live-
verified: a deliberately narrow domain `[0,4]` for the claim `x-5<=0`
(genuinely violated only past x=5, outside that domain) was automatically
expanded three times and found the real violation at x≈56; a well-chosen
domain `[0,10]` for a claim genuinely violated at an interior point
performed zero expansions, proving it doesn't expand when there's no
bias to correct.

## Data fusion

`src/lib/dataFusion.js`'s `fuseMeasurements()` combines multiple
independent measurements of the same quantity via inverse-variance
weighting (the standard statistical method) — new logic, stated plainly,
built on `measurementTension.js` unmodified as a gate: measurements in
DECISIVE tension are refused, not silently averaged, so fusion can never
launder a real contradiction into a falsely-confident number. Wired into
finance as `fuseRiskMarks()`. Live-verified: three compatible VaR marks
fused into a sharper estimate (uncertainty below every individual input);
the exact Hubble-tension figures used elsewhere in this repo correctly
refused rather than averaged.

**Heavy-tail fusion mode (upgrade 5).** `fuseMeasurements(measurements,
{distribution:'student-t', nu})` opts into Student-t weighted fusion —
robust to outliers, for when the uncertainty distribution is unknown or
heavy-tailed — solved by iteratively reweighted least squares (IRLS),
buffers pre-allocated once at module load, Kahan-compensated summation
throughout. The default Gaussian path (`fuseMeasurements(measurements)`,
no second argument — every existing caller including `fuseRiskMarks()`)
is untouched. The decisive-tension gate above runs unconditionally
before either mode, in one shared code path — there is no way for
either mode to fuse measurements in decisive tension, by construction,
not by convention.

Note on the weight formula actually implemented: `w_i =
(nu+1)/(nu+D_i)`, `D_i=(x_i-mu)²/sigma_i²` — the standard EM/IRLS
responsibility weight for t-distributed errors (Lange, Little & Taylor
1989). This is a deliberate departure from a plausible-looking
alternative (`w_i=(1+D_i/nu)^(-(nu+1)/2)`, the Student-t *density*
rather than the weight an IRLS loop actually needs): checked directly,
that form's fixed point sits a persistent ~6.4e-4 relative distance from
the Gaussian result regardless of how large nu grows, confirmed out to
nu=1e7 with zero improvement — not a convergence-rate issue, a wrong
quantity. The formula actually shipped here converges correctly (error
→0 as nu→∞) and still gives genuine outlier-downweighting at low nu.

## Domains

7 verticals registered (`node bin/ciall.mjs domains`), all zero new
kernel code except `motion.js`'s `verifyObservedTrajectory` — new,
small, deterministic comparison logic (built on the existing
`mathExpr.js` expression engine, not a new kernel) for checking
externally-supplied observed data against a claimed model. Explicitly
NOT camera/video capture: this substrate has zero external dependencies
by design and doesn't touch hardware. If tracking data came from a
camera pipeline running elsewhere, this is how you'd verify it against a
claim — Ciall never opens a camera itself.

## Cross-domain proof

`examples/finance-risk-example.mjs` (`node examples/finance-risk-example.mjs`)
runs a finance/quant claim through `consistencyKernel.js` and
`mcmcSearch.js` completely unmodified — the same code already verified
against physics (Hubble tension) and logic claims, now pointed at
reconciling two conflicting VaR marks and stress-testing a drawdown
claim. Zero new kernel code. Covered by
`tests/financeRiskExample.test.mjs`. See VISION-PLTR.md section 12 for
what this does and doesn't prove.

## Hardening

See [SECURITY.md](SECURITY.md) for the actual threat model — what's
enforced, what isn't, and why. Short version: retry+timeout on every
model call (`retry.js`), a size cap on writes (`MAX_CONTENT_BYTES` in
`deviceExecutor.js`), and a set of regression tests
(`tests/keyHandling.test.mjs`) that mechanically enforce the API-key
handling rules rather than just documenting them.

## Origin

Extracted from `client-backend-fixed` on 2026-08-02. The kernel files
themselves are unmodified copies (byte-for-byte at extraction time) of the
originals still in use by the live app — this is a fork of the substrate,
not a move; the original app keeps working exactly as before.
