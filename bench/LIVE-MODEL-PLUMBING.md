# Live-model plumbing for the CVE benchmark — what was built, and why it wasn't run

`bench/liveKernelAttempt.mjs` lets the kernels this benchmark otherwise
marks `not-applicable` — `consistency`, `mcmc`, `numeric-check`,
`dynamics`, `combinatorial`, `domain-of-validity` — actually ATTEMPT a
CVE fixture via a live model call, instead of never being invoked. This
document is the honest account of what that plumbing is, why it was
never run against the real 40-CVE dataset in this session, and — more
importantly — why running it would likely not produce a meaningful
CVE-detection number even with a working API key.

## What exists and is tested

- `buildCodeClaimPrompt(code, candidateKernelIds)` — the one new prompt:
  given a source file, asks whether any candidate kernel's claim type
  (logical/numeric/combinatorial) genuinely applies, or whether to
  decline. Declining is presented as the expected, honest default, not a
  failure mode.
- `attemptKernelsAgainstCode(code, model, opts)` — dispatches through
  `runPipeline`, the exact same worker-pool path every other kernel call
  in this repo uses. Not a new execution path.
- `makeLiveCodeClaimModel(apiKey, {provider})` — the real, model-backed
  adapter, reusing `modelDesignSpec.js`'s `makeLiveDesignSpec` unchanged
  for the actual spec-design half.
- `tests/liveKernelAttempt.test.mjs` — 10 tests, all against injected
  FAKE models (no network), proving the mechanism genuinely dispatches
  to a real kernel run and returns a real verdict when a claim is
  accepted, and correctly declines/reports failures otherwise.

This is real, working code — not a stub, not a mockup.

## Why it was not run against the pinned dataset

Checked directly in this session's environment: neither `GEMINI_API_KEY`
nor `ANTHROPIC_API_KEY` is set (only an unrelated `ANTHROPIC_BASE_URL`).
Fabricating what a live run "would probably show" would be exactly the
kind of unverified claim this whole project exists to refuse to make —
see `bench/ingest.mjs`'s own header on the identical discipline applied
to dataset construction. If a key is available in your environment,
`makeLiveCodeClaimModel` + `attemptKernelsAgainstCode` are real,
callable, and ready to wire into `bench/run.mjs` as an opt-in path.

## Why this would likely not be a meaningful improvement even with a key

This is the actual, substantive finding of this exercise, and it is more
important than the missing API key.

Every kernel in this repo that genuinely works under "model designs,
device executes" has a **device-side grounding step**: something real
and independent of the model that checks the proposal against reality.
`numeric-check` evaluates an expression at real numbers. `mcmc` searches
a real parameter space and reports an actual counterexample if it finds
one. `combinatorial` hands a claim to a from-scratch CDCL solver that
either produces a verified witness or an independently-checked DRAT
proof. In every one of these cases, the KERNEL's answer is decisive
regardless of whether the model's framing was trustworthy, because the
kernel checks something real, not the model's own narrative.

For "is this source code file vulnerable to CWE-X," none of
`consistency`/`mcmc`/`dynamics`/`combinatorial`/`domain-of-validity` has
an equivalent grounding step, because none of them re-reads the source
code at all. Concretely: if a model is asked to state a claim about a
file's security property as `consistency`-style commitments (`assert`,
`implies`, `universal`, `property`), and then `findContradictions` finds
those commitments mutually consistent, that result says the model's
STORY doesn't contradict itself — it says nothing about whether the
story is actually TRUE of the code, because nothing in that pipeline
ever checks the commitments against the file again. A vulnerable file
described by an internally-consistent (but wrong) set of commitments
would score exactly the same as a genuinely safe one. That is
verification theater wearing this repo's own honest-verification
clothing, and shipping it as a real number would be a worse outcome than
not running it at all.

This is not a claim that these kernels are bad instruments — the whole
rest of this repo, and their real grounding for numeric/physics/logical
claims, says otherwise. It is a claim that **CVE-style source-code
vulnerability detection is not the kind of claim these six kernels are
built to decide**, live model or not, and that the honest fix is not
"add a model call," it's "don't force a mismatched instrument to answer
a question it structurally cannot ground."

## The two real exceptions, and what was built instead of forcing them here

`boundary-check` and `chain-reachability` are structurally different:
their `run()` steps are pure decision procedures over STRUCTURED input
(a target/boundary string pair; a findings graph with stated
preconditions/postconditions) rather than free-text commitments. A model
proposing that structured input from reading code is a real, bounded
extraction step — no different in kind from a human doing the same
translation, which is exactly the caveat `chainKernel.js`'s own
documentation already carries ("consumes findings other kernels already
verified"). Rather than wire a live model into that extraction for this
benchmark (adding the same non-determinism, cost, and API-key
requirement this whole document is being careful about), **a
deterministic, no-model version was built instead**: see
`src/lib/shiftLeftScan.js` and its own write-up. It answers a real,
proactive question — do this repo's OWN pattern-scan findings compose
into a proven chain — using exactly the same SAT+DRAT rigor
`chainKernel.js` already has, with zero model calls and zero
non-determinism.

## If you do wire this up

1. Set `GEMINI_API_KEY` or `ANTHROPIC_API_KEY`.
2. Build a small runner analogous to `bench/run.mjs` that calls
   `attemptKernelsAgainstCode` per fixture with `makeLiveCodeClaimModel`,
   and report the SAME honest categories this benchmark already uses:
   `attempted` vs `declined` (most files, most of the time — decline is
   the expected, correct answer per the grounding problem above, not a
   bug), and grade any `attempted` result with the same skepticism this
   whole document argues for, not at face value.
3. Expect non-determinism across runs (a live model, not a deterministic
   kernel) and real API cost — budget and disclose both if you report
   numbers from it.
