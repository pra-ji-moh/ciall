# Ciall at PLTR level — working vision

Status: draft, written from conversation research on 2026-08-02. Not yet
reviewed against a separately-authored vision doc, because none was found
in this repo or elsewhere accessible — this IS that document, first draft.

## 1. The core mechanism, stated once

Ciall's differentiator is not randomness (one of five kernels is
stochastic, and it's seeded/reproducible). It's this:

> Adversarial search for the point of failure, structurally separated
> from the system that generated the claim.

Concretely, every instrument in `src/lib/` (consistencyKernel,
mcmcSearch, satKernel, dynamicsCheck, domainOfValidity,
combinatorialSearch, orderConsistency, dimensionalAnalysis, numericCheck)
follows the same shape: a model DESIGNS a spec against a claim,
`normalize()` validates and clamps it, and a deterministic (or
seeded-stochastic) executor RUNS it on-device — independent of the model
that proposed it. A human stays in the loop to judge novelty and decide
which commitment to drop when a contradiction is found; the kernel never
does that judgment call itself.

This is backed by real prior work, not just positioning: Knight & Leveson
showed independently-developed software variants still correlate in
failure, and current 2026 literature makes the same point about AI
verifiers built from the same foundation-model family — "the verifier has
the same failure modes as the thing it's verifying." A trained model
checking a trained model's output is not independent verification in the
sense that matters. A deterministic kernel, or a seeded search over a
mechanically-defined objective, genuinely is.

## 2. The PLTR angle: one substrate, many claims

Palantir's growth model is documented as land-and-expand: start on one
narrow, high-trust problem inside an institution, then expand
horizontally across other divisions/functions and vertically across more
users — using the SAME ontology substrate each time, not a new product
per use case. The ontology is a semantic layer mapping how an
organization's data relates to itself; it's the reason one platform
generalizes instead of requiring five bespoke tools.

Ciall's equivalent of the ontology is not a data-relationship graph — the
kernels don't need one. It's the **claim interface**: any domain-specific
problem that can be expressed as "here is a claim, design a spec that
would falsify it" can plug into the existing kernel substrate without a
new bespoke pipeline. That's what `src/lib/kernelRegistry.js` (added this
session) names explicitly: a registry entry is `{ buildPrompt, normalize,
run }`, the same three-step shape every kernel in this codebase already
uses. A new domain is a new registry entry, not a new call site threaded
through the UI by hand.

Claim domains identified so far as candidates for the same substrate,
inside a single organization (see prior conversation for the LMT-specific
version of this list):

- Engineering margins ("this spar holds under X g-load with Y% margin")
- Digital-twin / maintenance predictions
- Requirements consistency (order-consistency kernel against
  program-lifetime requirement accumulation)
- Mission/wargame diagnostics (claim-level explanation under a
  scenario-level simulation loss)
- Supply-chain resilience claims
- Financial model-risk validation (SR 26-2, the sharpest near-term
  regulatory wedge found — see prior research in this conversation)

None of these are built yet. The registry is the seam they'd attach to.

## 3. What shipped this session (Phase 1, code)

- `src/lib/kernelRegistry.js`: additive only. Wraps the seven existing
  kernels with common metadata (`id`, `domain` tags, `deterministic`,
  `needsModelExtraction`) and exposes `listKernels()`, `getKernel(id)`,
  `kernelsForDomain(tag)`. No existing call site in `ClientWorkspace.jsx`
  or elsewhere was modified or is required to use this registry — the
  working system is untouched. Verified: import resolves cleanly, all 332
  existing tests still pass.

This is intentionally the smallest possible first step: it names the
pattern without touching anything that currently works. Wiring
`ClientWorkspace.jsx` (or a future domain-specific UI) to dispatch through
the registry generically, rather than importing each kernel by name, is
the natural next code step — deferred here because it touches the live
UI's call sites and deserves its own reviewed change rather than being
folded into a first "start the codebase" commit.

## 4. Phase 2 — shipped

`src/lib/orchestrator.js`: `runPipeline(claim, { kernelIds | domain,
designSpec })` runs a claim through every applicable kernel back-to-back
and returns one aggregated report (`ran` / `skipped` / `failed` /
`anyViolated`), instead of a caller manually invoking each kernel and
reading each result in isolation. The model-extraction step
(`buildPrompt` -> a model turns prose into a spec) stays the caller's
responsibility, injected as `designSpec` — this substrate still has no
model client of its own (see README), so orchestration autonomy is scoped
to what happens after a spec exists: normalize -> run -> aggregate,
across every relevant kernel, without a human clicking through each one.
7 tests, all passing.

## 5. Phase 3 — shipped, deliberately inert

`src/lib/deviceGate.js`: the permission/audit scaffold for device
actions, with NO execution backend. It answers "is this action within a
granted scope" and logs every request, approved or denied — that's the
entire contract. There is no `fs.writeFile`, no `child_process`, no
browser automation anywhere in this file or this repo. `requestAction()`
returning `{ approved: true }` has nowhere to go; nothing downstream
turns that decision into a real filesystem or OS action.

Design points:
- Deny by default. An empty grant registry (the starting state, and the
  state after every test run) denies every action.
- Every grant is scoped (a specific directory / executable allowlist /
  hostname allowlist) and must carry a future `expiresAt` — no standing,
  unexpiring grants are expressible at all.
- `requestAction()` recomputes scope membership itself rather than
  trusting the caller — the same "generator vs. structurally-separate
  verifier" principle the kernels apply to claims, applied here to
  Ciall's own proposed actions. Path containment is checked as a real
  prefix match (`/project-evil` is NOT treated as inside `/project`),
  covered by a test.
- Every request, approved or denied, is appended to an in-memory audit
  log (`getAuditLog()`).
11 tests, including one that asserts the module exports nothing
execution-shaped by name (`write`/`exec`/`spawn`/`launch`/`fetch`/
`delete`/`remove`), so a future edit that quietly adds a real executor to
this file would fail its own test suite rather than sliding in unnoticed.

## 6. Phase 4 — shipped, with the three answers that unblocked it

The three blocking questions got concrete answers:

1. **Which directory** — a new, separate sandbox folder, not this
   project's own folder and not anything broader. `sandboxConfig.js`
   persists that decision to `.ciall-sandbox.json` and defaults to
   `../ciall-sandbox` (a sibling of this repo), created on first use.
2. **Consent flow** — ask every single action. `deviceExecutor.js`'s
   `writeFile()`/`readFile()` both REQUIRE a `confirm(action)` function
   as a parameter; there is no code path that writes or reads without
   confirm() being invoked fresh for that specific call and returning
   true. A grant from `deviceGate.js` only establishes that the target is
   in-bounds — it never substitutes for confirm().
3. **Persistence** — the sandbox boundary and the execution audit log
   both persist across sessions (`.ciall-sandbox.json`,
   `.ciall-execution-audit.jsonl`). Approval does NOT persist: every
   write still needs its own confirm(), covered by a test asserting
   confirm() is called once per write, never reused.

`deviceExecutor.js` is now the only file in this repo that imports
`node:fs`. Every path is resolved against the sandbox root via
`realpathSync` on the deepest existing ancestor, specifically so a
symlink pointing outside the sandbox is refused rather than followed —
covered by a test (skipped automatically on a Windows setup without
symlink permission, since that's a platform capability check, not a
behavior gap). 12 tests, all passing, all running against a temp sandbox
via `CIALL_SANDBOX_PATH`/`CIALL_AUDIT_LOG_PATH` overrides — no test run
touches the real persisted sandbox or audit log.

## 7. The runnable caller: `bin/ciall.mjs`, and `commandExecutor.js`

Section 6 ended with "nothing calls confirm with a real human yet" — that
gap is closed. `bin/ciall.mjs` is a real CLI: `write`, `read`, `run`,
`audit`. Its `confirm()` implementation prints the exact proposed action
as JSON and blocks on a literal terminal `y`/`N` — the same shape as a
permission prompt in an IDE or coding agent. Verified by hand, not just
by test: a declined write leaves no file on disk; an approved write
creates a real, readable file in the real persisted sandbox; a path
traversal attempt (`../../escape.txt`) is refused before confirm() is
even reached; every one of these lands in the real audit log.

`commandExecutor.js` extends the same pattern to running commands:
`execFile` only (never a shell, so there's no shell string for an
argument to break out of), no default allowlist of executables at all
(unlike the filesystem sandbox, "which commands are OK" has no sensible
one-size default), and the identical two-independent-checks discipline —
in scope AND confirmed, neither one substitutes for the other.

One real bug surfaced by actually running this rather than just testing
it: the first version had a separate `grant-run` CLI command meant to
pre-authorize an executable, but `deviceGate`'s grants live in memory and
do not survive between separate CLI invocations — each `node bin/ciall.mjs
...` is its own process, so a grant created by one invocation was already
gone by the time the next one ran. Fixed by having `run` scope the
executable for that single invocation, in the same process, immediately
before asking for confirmation — scope and confirm stay two independent
checks, but now they can actually both fire in practice. Left in this
document because it's the kind of gap that unit tests (which stay within
one process) cannot catch, and manual end-to-end runs did.

## 8. The gap this vision itself pointed at, and closing it

Section 2 says it plainly: "a new domain is a new registry entry, not a
new call site threaded through the UI by hand." Phase 3 (`deviceGate.js`)
did not follow that. It had its own hand-written `pathWithinBoundary()`
and allowlist-`includes()` logic, duplicate of the *principle* every
kernel embodies but sharing none of the actual code or registry with
`kernelRegistry.js`. `orchestrator.js` — Phase 2, the thing that actually
runs a claim through the kernel substrate — was never called by
`bin/ciall.mjs` at all. Two systems, one philosophy, zero shared code:
exactly the bespoke-pipeline-per-domain pattern this document argues
against, built anyway.

Fixed: `boundaryKernel.js` now holds the scope-membership check
(`normalizeBoundarySpec` / `checkBoundary`, the same `{kind, target,
boundary} -> {verdict, ...}` shape as every other kernel), registered in
`kernelRegistry.js` as `boundary-check` (`needsModelExtraction: false` —
there's no prose to extract a spec from; scope membership is fully
determined by the action and the grant). `deviceGate.js` now imports
`getKernel('boundary-check')` and calls it instead of running its own
copy of the logic. Nine kernels total; `deviceGate.js` is a consumer of
the shared registry, not a second implementation next to it. Re-verified
by hand after the refactor: an approved write still lands, a path
traversal is still refused before `confirm()` is even reached, identical
behavior, now routed through one substrate instead of two.

`orchestrator.js` is still not called by `bin/ciall.mjs` — nothing
currently runs a proposed write's *content* through the claim-
verification kernels (consistency, MCMC, etc.) before asking for
confirmation. That's a real next step, not yet built: `deviceExecutor.writeFile`
could call `orchestrator.runPipeline` on the proposed content as part of
what gets shown to the human before they type `y`. Flagging it explicitly
here so it doesn't quietly become the same kind of gap again.

**Closed in the same session.** `deviceExecutor.writeFile` now takes an
optional `verify: { kernelIds, designSpec }` — the identical shape
`orchestrator.runPipeline` already takes — and runs it BEFORE `confirm()`
is called, attaching the report to `action.verification` so the human
sees the kernel's finding as part of what they're approving. It does not
auto-deny on a violation, deliberately: consistencyKernel.js's own stance
("which one to drop is your call, not the tool's") applies here too — the
kernel surfaces what it found, the human still decides, verified by a
test where confirm() explicitly declines *because* it saw a violation.

Since this substrate still owns no model client, only a kernel that needs
no model extraction can be driven end-to-end without one — today that's
`numeric-check`. `bin/ciall.mjs write <path> <content> --check "lhs<=rhs"
--var x=lo:hi` builds a real `inequality` spec directly from CLI flags
(no model call anywhere) and runs it through `numeric-check` before
asking for `y`. Verified by hand: `--check "-1<=x^2" --var x=-10:10`
shows `verdict: held` and writes; `--check "x^2<=-1" --var x=-10:10`
shows `verdict: violated`, still asks, and a `y` still writes it (the
human's call, exactly as designed) while an `n` leaves nothing on disk.

## 10. The model dependency, added — closing the last named gap

Section 9 said a model-driven kernel reaching the live path was "future
work... this substrate takes on a model dependency it has deliberately
avoided so far." That tradeoff got made, explicitly, in this session,
because the alternative was leaving `--check`'s deterministic-only path
as the permanent ceiling — fine for numeric claims, useless for the
consistency/contradiction-finding half of what this substrate is
actually for.

Three new files, each with one job:

- `jsonExtract.js` — copied unmodified from `client-backend-fixed`, pure
  and dependency-free, so it belongs directly in this substrate.
- `modelClient.js` — a real Anthropic Messages API client, deliberately
  smaller than the app's `anthropicClient.js` (no proxy mode, no web
  search, no Opus tier, no prompt caching — this is one-off kernel-spec
  calls, not the app's resent-system-prompt tree generation). The API key
  is a PARAMETER, never read from `process.env` inside this file — kept
  that way specifically so it stays unit-testable (mocked `fetch`, any
  string works as a key) and so there is exactly ONE place in this entire
  repo that ever reads a real key from the environment: `bin/ciall.mjs`.
  Never logged, never in the request body, never in the audit log —
  covered by a test that asserts the literal key string appears in
  neither the request body nor thrown error text.
- `modelDesignSpec.js` — the adapter. Every kernel's `buildPrompt()` has
  its own call signature (a pre-existing wrinkle inherited from how each
  kernel got wired into the app's UI originally: `mcmc`/`numeric-check`/
  `dynamics` take a `{text, reasoning}` node, `consistency` takes three
  separate string arguments, `domain-of-validity` additionally needs
  `breakEvidence` from a prior instrument and DECLINES — returns `null`,
  no call made — when none is given, exactly like `orchestrator.js`
  already treats a decline). `makeLiveDesignSpec(apiKey, {callJSON})`
  returns a `designSpec(kernel, claim)` matching `orchestrator.runPipeline`'s
  exact shape, so it plugs into everything already built without changing
  `orchestrator.js` or any kernel at all.

`bin/ciall.mjs write <path> <content> --live [core|all|k1,k2]` is the
result: `--live` alone runs consistency, mcmc, numeric-check, and dynamics
against the real content via a real Claude call; `--live all` includes
combinatorial and domain-of-validity too (domain-of-validity still
declines without break evidence, correctly, since none exists on a fresh
write); an explicit list runs exactly those. The API key is read from
`ANTHROPIC_API_KEY` in the environment ONLY — verified by hand: running
`--live` with the variable unset fails immediately with a clear message
and **makes zero network calls** (confirmed: no fetch happens before the
key check), no crash, no file written, no partial state. `--check` and
`--live` together are explicitly rejected rather than silently picking
one.

Every test for this (`modelClient.test.mjs`, `modelDesignSpec.test.mjs`)
mocks the network — `globalThis.fetch` is monkey-patched, never real —
so the test suite makes zero live API calls and needs no key to run.
`bin/ciall.mjs` is the only code path in this entire repo that can make a
real model call, and it only does so when a human explicitly passes
`--live` with a key they set themselves.

What's still true, unchanged: every write, checked or not, live or not,
still requires its own fresh `confirm()` — a kernel finding, live or
deterministic, never auto-blocks or auto-approves anything. The human's
final call was never the part that was missing.

## 11. Second provider: Gemini, made the default

`geminiClient.js` mirrors `modelClient.js`'s exact interface
(`{text, usage}` / `{result, usage, tokens, cost}`) — same pricing-math
shape, same "API key is a parameter, never read from `process.env`
inside this file" boundary, same reason (unit-testable with a mocked
`fetch`, and exactly one place in the whole repo — `bin/ciall.mjs` — ever
reads a real key from the environment). `modelDesignSpec.js`'s
`makeLiveDesignSpec` takes a `provider` option (`'gemini'` default,
`'anthropic'` also available) and dynamically imports the matching
client; neither `orchestrator.js` nor any kernel needed to change, since
both clients return the identical shape.

`bin/ciall.mjs write ... --live` now defaults to Gemini (`GEMINI_API_KEY`)
rather than Claude, per current preference — cost is a real reason to
default to the cheaper/available key rather than always reaching for
Anthropic. `--live core --provider anthropic` switches to Claude when
wanted. Verified by hand: with neither key set, `--live` (gemini) and
`--live core --provider anthropic` both fail immediately with the
correct provider-specific message, zero network calls either way,
nothing written. `--check` (the deterministic, model-free path) is
completely unaffected by any of this.

## 9. Where this actually stands

Concretely, as of this session: `bin/ciall.mjs` can write files, read
files, and run commands — for real, on this machine — but ONLY inside
`../ciall-sandbox`, ONLY when a human is at the terminal to type `y`, and
ONLY one action at a time; there is no autonomous loop, no scheduled
trigger, and no connection back to the actual Ciall/Panton app
(`client-backend-fixed`) at all. "Control over the whole device" is still
nowhere near accurate — this is a small, sandboxed, human-gated CLI, and
that gap between the two is intentional, not a placeholder waiting to be
removed.

## 12. The "revolutionary like PLTR" ask, and what's actually true

Asked directly to make this "revolutionary like PLTR." Being straight
about it: that's not a coding output. Palantir is real data integration
across an organization's actual systems, live deployed infrastructure,
real customers, and years of land-and-expand execution at real
institutions. None of that can be produced by writing more code in a
session, and claiming otherwise would be dishonest.

What IS honestly true, and provable rather than asserted:
`examples/finance-risk-example.mjs` runs a finance/quant claim —
reconciling two conflicting VaR marks, stress-testing a drawdown claim —
through `consistencyKernel.js` and `mcmcSearch.js` completely
UNMODIFIED. Same files already verified against the Hubble tension and
logic contradictions. Zero new kernel code. It correctly found a
decisive 5.2-sigma tension between the two risk marks (the exact
"internal VaR vs. counterparty value" application named in the original
research), correctly held on a genuinely safe drawdown claim, and
correctly found a real counterexample when the claimed margin was made
too tight — covered by `tests/financeRiskExample.test.mjs`, not just a
demo script that could silently stop being true.

That's the actual, checkable version of "the substrate generalizes
across domains" — not a slide claiming it, a re-runnable example proving
it on a domain (finance) this codebase was never written for. It is one
piece of evidence for the PLTR-shaped architectural pattern holding, not
a demonstration of the company-scale things — data, deployment, scale,
customers — that "revolutionary like PLTR" actually requires.

## 13. A documented gap, closed with the existing mechanism only

Researched, with sources, what Palantir/AIP, JPMorgan's LLM Suite, and
Citadel actually say about their own AI limitations in 2026, rather than
guessing:

- **Palantir**: its own stated approach to hallucination is ontology
  grounding and transparent reasoning chains ("shows its work") — NOT
  independent falsification of a specific claim. An LLM plugged into the
  ontology can take a sanctioned action against real systems of record;
  that's provenance and governance, not a structurally-separate check
  that the action's underlying claim is actually true.
- **JPMorgan**: publicly acknowledges LLM hallucination risk, and its own
  mitigation is confinement — AI value stays "almost entirely back-office
  and risk management focused, rather than customer-facing," because a
  hallucination in a fiduciary context is legal liability, not a
  beneficial feature. The limitation isn't the model's raw capability;
  it's the absence of a way to make its output SAFE to use somewhere
  higher-stakes.
- **Citadel**: its own quant chief describes the bottleneck shifting as
  AI makes generating candidate trades/strategies cheap — "AI generates
  the option set... selection under tail risk becomes the bottleneck."
  Generation stopped being scarce; independent verification under stress
  is now the scarce thing.

One problem, stated three ways by three different institutions:
AI-generated candidates are abundant; a NON-correlated way to verify them
under tail-risk stress, before trusting them, is not.

`selectSurvivingCandidates()` in `src/domains/finance.js` answers this
with the exact mechanism this whole codebase already is — zero new
kernel logic. Given a set of candidates (however generated, by whatever
model, correlated with each other or not), it runs EACH one through the
unmodified MCMC kernel independently, searching for a tail-risk
counterexample. Live-verified: three candidate strategies in, two
correctly held, one correctly falsified with an exact, reproducible
counterexample point — not a downgrade, not a vibe, a specific breaking
input. That's Palantir's missing independent-falsification step, JPMorgan's
missing safe-to-trust-at-higher-stakes gate, and Citadel's named
selection-under-tail-risk bottleneck, answered by the same kernel that
already found the Hubble tension and a contract clause contradiction
earlier in this document.

Sources: [Inside Palantir AIP — Towards AI](https://towardsai.com/p/machine-learning/inside-palantir-aip-how-the-worlds-most-controversial-ai-platform-actually-works),
[JPMorgan Introduces Its Own Financial AI LLM Suite — Dataconomy](https://dataconomy.com/2024/07/31/jpmorgan-financial-ai-llm-suite/),
[Citadel's Quant Chief: AI's New Market Paradox — HedgeCo](https://hedgeco.net/news/05/2026/citadels-quant-chief-ais-new-market-paradox-faster-information-more-crowded-trades.html),
[Hedge Funds May Face the AI Crowding Risk — HedgeCo](https://hedgeco.net/news/06/2026/hedge-funds-may-face-the-ai-crowding-risk.html).

## 14. Deeper on Palantir specifically: the ontology's own admitted gap

Went deeper on Palantir alone, past the general "ontology grounds,
doesn't falsify" framing, into their own technical documentation and
customer-facing reviews.

- **Palantir's own docs acknowledge ontology maintenance rot**:
  "duplicated object types, redundant properties, and copy-pasted
  workflows are a maintenance burden and a context-management problem,
  for both humans and AI agents that need to reason about the Ontology."
  Not a critic's claim — their own words.
- **Implementation reality**: production-ready teams go through multiple
  certification tracks (Foundry Aware, Application Developer, Data
  Engineer, AIP Builder); "rapid," in Palantir's own terms, means months.
  Heavy consulting dependency follows from this.
- **Lock-in**: no OWL export, no portable ontology format; once business
  logic is encoded in Foundry, migrating away is expensive.
- **The sharpest one, from Palantir's own blog post on reducing
  hallucinations**: their architecture is four layers of constraint —
  what you see, what you can query, what you can do, who has permission.
  Their own words on the remaining gap: *"When your RAG system gives a
  compliance officer a hallucinated sanction status on a counterparty...
  the cost is not an engineering post-mortem — it's a regulatory
  conversation."* Stated plainly, in their own material: even with all
  four layers, a hallucinated DERIVED fact can still reach a governed,
  permitted action. The four layers constrain who can act and what
  they're allowed to touch; none of them independently verify that a
  specific claim is logically consistent with the other facts already on
  record.

`verifyComplianceClaimConsistency()` in `src/domains/legal.js` is built
against that exact named scenario, not a paraphrase of it. Given a
compliance rule ("majority ownership by a sanctioned entity confers
sanctioned status"), an ownership fact (counterparty X is 62% owned by a
sanctioned entity), and a claimed status (X is clear) — the SAME
consistencyKernel.js already used for the Hubble tension and a contract
termination clause proves the claimed status cannot be true, with the
full derivation chain and every source cited. Live-verified: three
commitments in, one proven contradiction out, the exact scenario
Palantir names as still-possible under their own architecture, caught
mechanically. A genuinely clear counterparty (no ownership link into the
sanctioned category) correctly produces no finding at all — not a
rubber stamp.

Sources: [Palantir Foundry Ontology: How It Works, What Problems It
Solves, and Where It Falls Short — Medium](https://medium.com/@cloudpankaj/palantir-foundry-ontology-how-it-works-what-problems-it-solves-and-where-it-falls-short-d8b4a1ae4900),
[Palantir AIP Reviews & Ratings — Gartner Peer Insights](https://www.gartner.com/reviews/product/palantir-aip),
[Reducing Hallucinations with the Ontology in Palantir AIP — Palantir Blog](https://blog.palantir.com/reducing-hallucinations-with-the-ontology-in-aip-288552477383),
[Palantir AIP vs. Cerebro: Cost, Lock-In, and Who Actually Owns Your AI](https://cyberhillpartners.com/cerebro-vs-palantir-aip/).
