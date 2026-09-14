# Security & threat model

Written for a serious reviewer, not marketing. Honest about limits.

## What this is

A Node library (verification kernels + orchestration) plus one CLI
(`bin/ciall.mjs`). No server, no network listener, no deployment, no
multi-tenant surface. Everything runs as whatever OS user invokes it,
with that user's own filesystem/process permissions — this is not a
sandbox in the OS-security sense (no seccomp, no container, no VM). The
sandboxing here is **application-level scope control**, not isolation
from the host.

## The core security property, and how it's enforced

Every device-touching action (file write, file read, process launch)
requires two independent checks, neither substituting for the other:

1. **Scope** — `deviceGate.requestAction()` checks the target against
   active grants via `boundaryKernel.js`. Deny by default: an empty grant
   registry (the starting state) approves nothing.
2. **Confirmation** — `deviceExecutor.js`/`commandExecutor.js` require a
   `confirm(action)` function and call it fresh, every single time, with
   the exact action being proposed. No grant, however long-lived, skips
   this. `bin/ciall.mjs`'s `terminalConfirm` prints the full action as
   JSON and requires a literal `y`.

Enforced by tests, not just documented: `deviceGate.test.mjs`,
`deviceExecutor.test.mjs`, `commandExecutor.test.mjs` all assert deny-by-
default and per-call confirmation, including a test that confirm() is
invoked fresh on every write (`confirm() calls fresh on every call`) and
one that a kernel violation surfaced to a human does not auto-block or
auto-approve anything.

## Filesystem: what's actually enforced

- **One directory only.** `sandboxConfig.js` persists a single boundary
  (`../ciall-sandbox` by default). `deviceGate.grant()` for
  `filesystem-*` scopes is checked against this boundary via
  `boundaryKernel.js`'s `path-containment` check — real prefix matching
  (`/project-evil` is NOT inside `/project`), covered by a test.
- **Symlink escape is blocked**, not just string-checked. `deviceExecutor.
  resolveInSandbox` resolves the deepest EXISTING ancestor via
  `fs.realpathSync` before comparing, so a symlink inside the sandbox
  pointing outside it is refused. Covered by a test (skipped automatically
  on a platform without symlink permission — a capability gap, not a
  correctness gap).
- **Size-capped.** `writeFile` rejects content over `MAX_CONTENT_BYTES`
  (10MB) before confirm() is even asked — an oversized write is out of
  scope regardless of what a human would approve. Non-string content is
  rejected with a clear error rather than a confusing runtime crash.
- **What this does NOT do**: no filesystem quota beyond the single write
  cap (repeated writes can still fill disk over many calls — there's no
  cumulative session-level budget); no encryption at rest; no OS-level
  ACL enforcement beyond whatever the invoking user's OS permissions
  already are.

## Process execution: the honest limitation

`commandExecutor.js` uses `execFile`, never a shell — arguments reach the
OS as an argv array, so there is no shell string for an argument to break
out of (no `; rm -rf`, no backtick injection; those only matter against a
shell parser, and there isn't one here).

**What it does NOT do**: restrict what an approved executable can
actually do once it runs. A `process-launch` grant controls WHICH BINARY
may run, not its semantics — granting `node` means `node -e "<anything>"`
is executable, because `node` itself is a general-purpose interpreter.
This is not a gap that can be closed by better allowlisting; it's
inherent to allowing arbitrary process launch at all. **The actual
security boundary here is the human reading the full command and
arguments in `confirm()` before approving — not the allowlist.** The
allowlist's job is narrower: it stops execution of anything the user
hasn't explicitly scoped in the current process, at all, before a human
is even asked.

## Credentials: the property most worth verifying independently

`ANTHROPIC_API_KEY` / `GEMINI_API_KEY` are read from the environment in
exactly one place in this entire repository: `bin/ciall.mjs`. Never a CLI
argument (would land in shell history and the process list), never
logged, never written to the audit log, never read by `modelClient.js` or
`geminiClient.js` internally (both take the key as a plain parameter,
which is also what keeps them unit-testable without a real key).

This is not just documented — `tests/keyHandling.test.mjs` asserts it
mechanically: `deviceGate.js`/`deviceExecutor.js`/`commandExecutor.js`
never reference `apiKey` in any form; `modelClient.js`/`geminiClient.js`
never read `process.env` themselves; no console call in either client
file includes anything key-shaped; and `bin/ciall.mjs` is the ONLY file
in `src/` or `bin/` that references either key name against
`process.env`. A future change that violates any of these fails its own
test, not just a code-review glance.

## Model calls: what leaves this machine

`--live` sends the claim's TEXT (and any `reasoning`/`coreClaim`/
`siblingClaims`/`sourceExcerpt` supplied) to Anthropic or Google's API —
this is the one code path where data leaves the machine at all. Every
other operation (kernels, `--check`, file/command execution) is fully
local. `--live` is opt-in per write, never default, and the exact model
provider is visible in the command you typed.

## Reliability, since availability is part of a real security posture

`modelClient.js`/`geminiClient.js` route through `retry.js`: bounded
backoff on 5xx/dropped connections, a 30-second per-attempt timeout via
`AbortController` (an earlier version had neither — a stalled connection
could hang indefinitely). 429 is deliberately NOT retried at this layer —
retrying a rate limit multiplies attempts and lengthens the lockout,
which is the opposite of the correct response.

Every kernel (`mcmcSearch.js`, `numericCheck.js`, `dynamicsCheck.js`,
`satKernel.js`) already carries its own wall-clock and evaluation budget
(4-10 seconds, capped evaluation counts) — inherited from
`client-backend-fixed`'s original design, not added for this document. A
runaway model-designed spec cannot hang the process; it times out and
reports `partial: true` honestly rather than pretending to have finished.

## What a real audit should still check independently

- This document describes what the code does as of this commit. Verify
  against the current source, not this file, before relying on it —
  same rule as anywhere else memory or documentation could drift from
  code.
- Zero external npm dependencies today (`package.json` has no
  `dependencies` field at all — every kernel, `retry.js`, `jsonExtract.js`,
  and both model clients use only Node built-ins). That's a real,
  verifiable security property (no supply-chain surface from third-party
  packages), not a claim — check it stays true as this evolves; the day a
  `dependencies` entry gets added is the day a dependency audit becomes
  necessary.
- No fuzzing pass has been run against the kernels' input parsers
  (`mathExpr.js`'s expression compiler in particular) beyond the existing
  unit tests. A dedicated fuzz/property-testing pass would be the natural
  next hardening step beyond what's covered here.
- The disk-fill limitation noted above (per-write cap exists, no
  cumulative session cap) is a known, undated gap, not an oversight this
  document is hiding.
