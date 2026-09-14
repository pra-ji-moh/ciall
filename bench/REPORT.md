# ciall-substrate CVE benchmark report

Dataset: 40 CVE/GHSA entries, pinned at `bench/dataset.json` (recorded 2026-08-08T05:11:10.718Z).
Dataset integrity: **VERIFIED against pinned baseline**.

## Per-kernel breakdown (no blended score -- see bench/FINDINGS.md for why)

| kernel | status | covered | TP | FN | FP | precision | recall |
|---|---|---|---|---|---|---|---|
| self-audit | ran (pattern scan, no model) | 14/40 | 9 | 5 | 9 | 50.0% | 64.3% |
| consistency | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| mcmc | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| numeric-check | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| dynamics | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| combinatorial | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| domain-of-validity | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| order-consistency | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| boundary-check | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| neuromorphic-power | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| event-camera-pixel | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| decision-helper | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| chain-reachability | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |
| vector-span | not-applicable (needs model-based spec extraction) | 0/40 | 0 | 0 | 0 | n/a | n/a |

"covered" = how many dataset entries even have a rule this kernel claims could apply (selfAudit's CWE table for self-audit; zero, structurally, for every other kernel). precision/recall are computed ONLY over covered entries -- an excluded entry is not a zero.

## Secondary lens: annotation-aware precision (self-audit only)

selfAudit's `guardDetected`/`usesShellInterpolation` fields (see selfAudit.js's header) let a human deprioritize a post-patch finding that LOOKS fixed. This does NOT change the strict primary metric above -- it answers a different, additional question: "if a human used these annotations, how many of the strict FPs would they have correctly set aside?"

Strict FP: 9. Annotation-aware FP: 7 (precision 50.0% -> 56.3% over the same 14 covered entries).

## CWE classes with zero selfAudit coverage in this dataset

- CWE-79: 7 dataset entries, no selfAudit rule targets it
- CWE-400: 5 dataset entries, no selfAudit rule targets it
- CWE-20: 4 dataset entries, no selfAudit rule targets it
- CWE-471: 4 dataset entries, no selfAudit rule targets it
- CWE-1333: 4 dataset entries, no selfAudit rule targets it
- CWE-88: 1 dataset entry, no selfAudit rule targets it
- CWE-64: 1 dataset entry, no selfAudit rule targets it
- CWE-269: 1 dataset entry, no selfAudit rule targets it
- CWE-74: 1 dataset entry, no selfAudit rule targets it
- CWE-94: 1 dataset entry, no selfAudit rule targets it
- CWE-1050: 1 dataset entry, no selfAudit rule targets it
- CWE-172: 1 dataset entry, no selfAudit rule targets it
- CWE-792: 1 dataset entry, no selfAudit rule targets it

See `bench/FINDINGS.md` for the full write-up (which kernels are structurally not-applicable and why, per-rule noise, and what a real recall number here does and does not mean).
