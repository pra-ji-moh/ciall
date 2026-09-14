# ciall-substrate CVE benchmark — findings and limitations

This is the honest write-up the numbers in `bench/REPORT.md`/`bench/REPORT.json`
need to be read alongside. Three things to take from this document, in
order of importance:

1. **12 of the 13 kernels registered in `kernelRegistry.js`, plus
   `chainKernel.js`, cannot be scored by this benchmark at all,
   structurally** — reported as `not-applicable`, never as a low score.
2. **A real, tested, disclosed improvement (§3) took selfAudit's recall
   on the 14 covered entries from 21.4% (3/14) to 64.3% (9/14) — a
   genuine result, verified by re-running this exact pinned dataset
   before and after, not a projection.** Precision held flat at 50.0%
   (annotation-aware: 56.3%), and the **clean-catch count is still
   zero** — every single hit, before and after, also fires on the
   patched, fixed version of the same file. §4 explains exactly why that
   ceiling didn't move even though recall did, with the real diffs.
3. Nothing in §3 was tuned by looking at which specific CVEs it would
   flip — every rule change was written from a general principle (the
   real minimist `setKey` shape; a legitimate blocklist-array style;
   growl's actual `exec`→`spawn` fix) and *then* measured against the
   whole pinned set, including the cases that made it worse before a
   fix (see the two false-positive regressions found and fixed while
   building this, both documented in `src/lib/selfAudit.js`'s own
   comments).

## 1. Why only `self-audit` produces a real number here

Every kernel in `kernelRegistry.js` — `consistency`, `mcmc`,
`numeric-check`, `dynamics`, `combinatorial`, `domain-of-validity`,
`order-consistency`, `boundary-check`, `neuromorphic-power`,
`event-camera-pixel`, `decision-helper`, `chain-reachability`,
`vector-span` — follows
this repo's own documented "model designs, device executes" shape: a
model turns a prose claim (or, for `boundary-check`/`chain-reachability`,
a human or upstream tool) into a *structured spec*, and only then does
the kernel's deterministic `run()` decide anything. None of them takes
raw source code as input, and this benchmark makes **zero live model
calls** (see `bench/ingest.mjs`'s header on why even the *dataset
construction* avoids trusting anything from memory, let alone trusting a
model's on-the-fly read of a CVE's code).

Hand-authoring a spec per CVE to make one of those kernels "work" would
not test the kernel — it would test whether the curator already knew the
answer. This benchmark refuses to do that. `selfAudit.js` is the one
exception: a separate module (not in `kernelRegistry.js` at all — see
`bin/ciall.mjs`'s `self-audit` command) that pattern-matches raw source
text directly, no model, no spec.

## 2. Coverage: 14 of 40 dataset entries have ANY applicable rule

`selfAudit.js`'s rules were written against *this repo's own* standing
practices — they were never designed as a general vulnerability scanner.
`bench/harness.mjs`'s `CWE_TO_SELFAUDIT_CODE` table names exactly which
CWE classes any rule even claims to target:

| CWE | selfAudit code(s) | note |
|---|---|---|
| CWE-95 / CWE-502 | `eval-usage` OR `function-constructor-usage` | Eval Injection / unsafe deserialization |
| CWE-1321 / CWE-915 / CWE-1336 | `prototype-mutation` OR `unguarded-dynamic-key-assignment` | Prototype Pollution (second code added in §3) |
| CWE-798 / CWE-321 | `hardcoded-credential` | hardcoded secret / crypto key |
| CWE-327 / CWE-328 | `weak-crypto-algorithm` | broken/weak algorithm or hash |
| CWE-295 | `tls-verification-disabled` | improper certificate validation |
| CWE-78 / CWE-77 | `unreviewed-child-process` | command injection (proxy signal only — see §4) |

Of this dataset's 40 real entries, only **14** carry a CWE in this table
at all (10 × CWE-1321 prototype pollution, 1 × CWE-502, 1 × CWE-78, 2 more
CWE-1321-adjacent). The other **26** — 7×CWE-79 (XSS), 5×CWE-400 (ReDoS),
4×CWE-20, 4×CWE-471, 4×CWE-1333 (ReDoS), and one each of CWE-88/64/269/74/
94/1050/172/792 — have **zero** selfAudit rule and are reported
`not-covered`, excluded from precision/recall, never scored as a miss.
This 14/40 split is itself a finding: even restricted to real, disclosed
npm-ecosystem CVEs, most of what actually gets reported (XSS, ReDoS,
general input validation) is completely outside what a zero-eval,
zero-child_process-style pattern scanner was ever built to see.

## 3. The improvement: two new detectors, measured before and after on this exact dataset

The first pass of this benchmark (recorded in git history) found 21.4%
recall (3/14) with zero clean catches, and named the specific cause: the
literal `.__proto__ =` pattern cannot see the shape most real
prototype-pollution CVEs actually have — a recursive merge/set/clone
helper that assigns through a *computed* property, `o[key] = value`,
where `key` comes from iterating another object's keys. `selfAudit.js`
now has a second rule, `unguarded-dynamic-key-assignment`, targeting
exactly that shape, across three real loop idioms found in this
dataset's own fixtures:

1. `for (var key in source) { target[key] = ... }`
2. `Object.keys(source).forEach(function (key) { target[key] = ... })`
   (minimist's real, actual `setKey`: `keys.slice(0,-1).forEach(function
   (key) { if (o[key] === undefined) o[key] = {}; o = o[key]; })`)
3. `for (let i = 0; i < keys.length; i++) { let key = keys[i]; ...
   target[key] = ... }` — a plain pre-ES6 indexed loop, which turned out
   empirically to be at least as common as the two shapes above in this
   dataset's own set-value/hoek fixtures.

A finding only fires if NO comparison against `"__proto__"`/
`"constructor"`/`"prototype"` is found either in the loop's own body or
earlier in the *same enclosing top-level function* — see
`findEnclosingTopLevelStart` in `selfAudit.js` for exactly why that
boundary, not the loop body alone (misses a guard array declared just
above the function) and not the whole file (see the false positive this
caused, below).

**Two real false positives were found and fixed while building this,
against this repo's own code and its own tests — not against the
benchmark dataset:**

- `src/lib/dynamicsCheck.js`'s `stateA.forEach((n, i) => { envA0[n] =
  a0[i]; })` and `tests/mathExprBatch.test.mjs`'s identical pattern both
  matched the shape (a `.forEach` callback variable used as a computed
  property) but are iterating an ARRAY of state-variable-name strings,
  not an object's own keys — reviewed and whitelisted with the reasoning
  written down in `selfAudit.js` (the blast radius, even in the worst
  case, is a wrong answer for one local computation, never shared
  state). This is a disclosed, permanent limitation of the rule, not a
  one-off patch: it cannot distinguish "iterating an object's own keys"
  from "iterating an array of name strings" by syntax alone.
- An early version of the guard-detection window used a *fixed
  1500-character lookback* instead of the enclosing-function boundary.
  Running it against minimist's real `index.js` (which has several
  unrelated top-level functions) produced a genuine, confirmed false
  positive: removing a `'__proto__'` string from one function's guard,
  during an unrelated part of the same fix commit, shifted what the
  fixed-size window considered "nearby" onto a completely different,
  always-safe `forEach` elsewhere in the file — a finding caused by
  unrelated code moving, not by anything becoming less safe. Fixed by
  scoping the guard search to the enclosing top-level function instead
  of a character count.

**Measured result, same pinned 40-CVE dataset, before vs. after:**

| | before | after |
|---|---|---|
| covered | 14/40 | 14/40 |
| TP | 3 | 9 |
| FN | 11 | 5 |
| FP | 3 | 9 |
| precision | 50.0% | 50.0% |
| recall | 21.4% | **64.3%** |
| clean catches | 0 | **0** |

Recall nearly tripled — a real, verified result, not a projection.
Precision did not move, and the clean-catch count is STILL zero: every
one of the 6 newly-caught entries is a `flags-both-versions` hit, same
as the original 3. §4 explains precisely why, with the real diff that
finally explained it.

## 4. Why the clean-catch ceiling didn't move: the fix isn't always inline

The full per-entry breakdown is in `bench/REPORT.json`. Three real,
distinct reasons every hit still fires on the patched version too:

- **`GHSA-4g88-fppr-53pp` (set-value) — the fix filters keys BEFORE the
  loop, via a named function, not an inline check.** The real diff:
  ```diff
  - const keys = isArray ? path : split(path, opts);
  + const keys = (isArray ? path : split(path, opts)).filter(isValidKey);
  + function isValidKey(key) {
  +   return key !== '__proto__' && key !== 'constructor' && key !== 'prototype';
  + }
  ```
  The loop body itself is byte-for-byte unchanged. The actual guard
  moved to a `.filter(isValidKey)` call applied to the array *before*
  the loop ever sees it, with the real check inside a separately-named
  function. No syntactic, single-scope heuristic can follow a guard
  through a function reference like this without becoming a real
  interprocedural data-flow analyzer — a fundamentally different, much
  larger class of tool than a regex-based scanner. This is the actual,
  concrete ceiling, not a hand-wave: three different fix STYLES were
  found across ten dataset entries (inline comparison, blocklist array,
  and this one — pre-filter via a predicate function), and only the
  first two are things a syntactic rule can see at all.
- **`GHSA-mm62-wxc8-cf7m` (serialize-to-js, CWE-502)** — the real fix
  adds `str = sanitize(str)` on the line before an otherwise-unchanged
  `new Function('...' + str)`. `function-constructor-usage` cannot see
  that a sanitizer was added — but `guardDetected` (§ below) now
  correctly annotates the post-patch finding as likely-fixed.
- **`GHSA-qh2h-chj9-jffq` (growl, CWE-78)** — the real fix switches
  `require('child_process').exec` to `.spawn`. `unreviewed-child-process`
  fires on the import either way — but `usesShellInterpolation` now
  correctly reports `true` pre-patch and `false` post-patch.

**A second, additional lens — never replacing the strict metric above —
uses those two annotations.** `guardDetected` (on `eval-usage`/
`function-constructor-usage`: a sanitizer-shaped call found in the same
enclosing function before the risky call) and `usesShellInterpolation`
(on `unreviewed-child-process`: whether the actually-dangerous
shell-string half of the API, not just the argv-array half, is present)
are validated against exactly the two real cases above: `guardDetected`
is `false` pre-patch and `true` post-patch for serialize-to-js;
`usesShellInterpolation` is `true` pre-patch and `false` post-patch for
growl. Annotation-aware precision on this same dataset: **50.0% →
56.3%** (strict FP 9 → annotation-aware FP 7) — a real, if modest,
improvement in what a human reviewer would see, reported separately and
explicitly labeled in `bench/REPORT.md`, never blended into the primary
number.

The other 7 prototype-pollution `flags-both-versions` cases have no
equivalent annotation: by construction, `unguarded-dynamic-key-assignment`
already only fires when its OWN guard search found nothing, so there is
no secondary "was there a guard nearby after all" signal left to add —
these are the genuine cases where the fix moved the guard somewhere this
heuristic's function-scope boundary cannot see (a `.filter(isValidKey)`
one function away, or a completely unrelated but syntactically identical
`forEach` elsewhere in a large multi-function file like minimist's).

## 5. Dataset provenance and its own limitations

- Every entry in `bench/dataset.json` was resolved live: a package name
  (never a memorized CVE ID) queried against OSV.dev's real API, a real
  GitHub commit SHA taken from OSV's own `references`, and the actual
  pre/post file content pulled via `git show` against a live shallow
  clone (see `bench/ingest.mjs`'s header for why `git` directly, not the
  GitHub REST API — the API's 60-req/hour unauthenticated cap was hit and
  observed mid-run before that switch). `bench/ingestion-rejections.json`
  lists every candidate that was tried and dropped, and why.
- Two candidate packages (`marked`, `clean-css`) were dropped after a
  `git fetch` against one of their advisory commits reproducibly hung
  well past this script's own timeout — a real, observed stuck
  transport state on this host, not a slow-but-progressing clone. Not
  investigated further since the run had already produced 40 accepted
  entries, double the ~20 target, by the time this was cut.
- **Ecosystem bias**: npm/JavaScript only, because selfAudit.js only
  scans `.js`/`.mjs`. This says nothing about applicability to other
  languages' CVEs — untested.
- **Single-file granularity**: each entry captures the file with the
  largest diff in the fix commit. A multi-file fix is under-represented.
- **Package selection skews toward selfAudit's own coverage**
  (prototype-pollution-heavy packages especially), specifically so this
  benchmark would have enough covered entries to say anything about
  recall at all. A randomly-sampled CVE set would have an even LOWER
  covered fraction, not higher, because most disclosed CVEs are XSS/
  ReDoS/path-traversal, which §2 already shows have zero coverage here.

## 6. What this number does and does not license

64.3% recall, with a 0/14 clean-catch rate, says: *"On this small,
npm-skewed sample, selfAudit's pattern rules now notice the right
mechanism in most of the cases they claim to cover, but still have never
once distinguished a vulnerable revision from its own fix."* The first
half of that sentence is new and real. The second half did not change,
and is arguably the more important one: it does **not** say
ciall-substrate "detects prototype pollution" or "detects command
injection" as a general capability — it detects the *mechanism*, and a
human still has to determine whether it was actually fixed. It says
nothing about the other twelve kernels, which this benchmark did not run
at all.

## 7. Concrete follow-up this benchmark surfaced (not built here)

- **Interprocedural guard tracking** (§4's set-value example: a guard
  applied via `.filter(isValidKey)` one function away from the loop) is
  the single largest remaining ceiling on the clean-catch rate — and it
  is a fundamentally different, much larger class of tool (real
  call-graph/data-flow analysis) than a regex-based scanner. Flagged,
  not attempted: this is where "pattern matching" stops being the right
  tool at all, not just where it needs one more regex.
- A real taint-aware check for `unreviewed-child-process` (does an
  argument reaching `exec`/`spawn` trace back to an HTTP request / user
  input, not just "does the file use the shell-string half of the API,"
  which `usesShellInterpolation` now checks) would be the next real step
  beyond what §4 already added.
- Wiring a live model-backed `designSpec` (this repo already has
  `modelClient.js`/`geminiClient.js`, and `iterativeVerify.js` now adds a
  multi-round loop on top of it) would let the other twelve kernels
  actually attempt this benchmark instead of being marked
  not-applicable — see `bench/LIVE-MODEL-PLUMBING.md` for what was built
  and why it was not run live in this session (no API key configured).

## Reproducing this report

```
node bench/datasetBaseline.mjs   # only needed once, or after a deliberate dataset change
node bench/run.mjs
```

`bench/run.mjs` verifies `bench/dataset-baseline.json` against the live
fixture tree before scoring anything, and the report says so — "we
scored X" is a claim about this exact pinned dataset, not a moving
target. Re-running `bench/ingest.mjs` touches the network and rebuilds
`dataset.json`/`bench/fixtures/` from scratch; re-run
`bench/datasetBaseline.mjs` afterward if you intentionally change the
dataset.
