// bench/harness.mjs; the pure grading logic for the CVE benchmark. No
// network, no filesystem walk of its own -- takes findings that were
// already produced (by auditSourceTree, or eventually a live model call
// for the other kernels) and turns them into a per-kernel, per-CVE
// verdict, then aggregates to precision/recall. Kept separate from
// bench/run.mjs (which does the I/O: load dataset.json, run
// auditSourceTree against the checked-in fixtures) so this file's
// scoring rules are unit-testable against synthetic data, independent
// of the real 20-CVE dataset ever having been fetched.
//
// THE CENTRAL HONESTY CONSTRAINT THIS FILE ENFORCES: only ONE of
// ciall-substrate's kernels can run directly, deterministically, and
// without a live model call against a raw source-code fixture --
// selfAudit.js's pattern scanner. Every kernel registered in
// kernelRegistry.js (consistency, mcmc, numeric-check, dynamics,
// combinatorial, domain-of-validity, order-consistency, boundary-check,
// neuromorphic-power, event-camera-pixel, decision-helper,
// chain-reachability) has `needsModelExtraction: true`, or (boundary-check,
// chain-reachability) consumes an already-structured spec/finding-set
// that nothing here can derive from a bare file without a model or a
// human doing that translation by hand. Hand-authoring a per-CVE spec
// to feed one of those kernels would not be testing the KERNEL's
// capability -- it would be testing whether the curator already knew
// the answer. So this benchmark marks all twelve of those kernels
// NOT_APPLICABLE for every entry, on purpose, rather than inventing a
// way to make them produce a number. See bench/FINDINGS.md for why this
// is the single most important finding of this whole exercise, not a
// footnote.

import { listKernels } from '../src/lib/kernelRegistry.js';

// ── which kernels can even attempt this benchmark ────────────────────

export const SELF_AUDIT_KERNEL_ID = 'self-audit';

export function structurallyNotApplicableKernelIds() {
  return listKernels().map((k) => k.id);
}

export const NOT_APPLICABLE_REASON =
  'requires a model (or a human) to translate raw source code into this kernel\'s structured claim/spec format before it can run at all -- this benchmark makes no live model calls, so this kernel was never actually invoked, and is excluded from precision/recall rather than scored as a miss';

// ── CWE -> selfAudit finding code, the ONE mapping this benchmark
// grades selfAudit against. Deliberately a flat table, checked in
// order, first match wins -- exactly the same "explicit table, no
// guessing" discipline chainKernel.js's SEVERITY_RULE_TABLE uses. A CWE
// not in this table means selfAudit never claimed a rule for it; those
// entries are reported as 'not-covered', never folded into a miss.
// `codes` is a LIST because selfAudit.js itself treats more than one
// finding code as functionally equivalent for a given exploit mechanism
// (its own header: "new Function( ... equivalent to eval for this
// repo's purposes") -- a real dataset entry caught this the hard way:
// GHSA-mm62-wxc8-cf7m's CWE-502 vulnerability is `new Function('return
// ' + str)`, not a literal eval call, and an earlier version of this table
// mapped CWE-502 to `eval-usage` ONLY, which silently scored a real
// selfAudit catch as a miss. Fixed by listing every code that would
// honestly count as "this class was caught," not by loosening what
// counts as a catch.
export const CWE_TO_SELFAUDIT_CODE = [
  { cwe: 'CWE-95', codes: ['eval-usage', 'function-constructor-usage'], note: 'Eval Injection' },
  { cwe: 'CWE-502', codes: ['eval-usage', 'function-constructor-usage'], note: 'Deserialization of Untrusted Data, when the concrete exploit is an eval or new-Function call (e.g. node-serialize-style)' },
  { cwe: 'CWE-1321', codes: ['prototype-mutation', 'unguarded-dynamic-key-assignment'], note: 'Prototype Pollution' },
  { cwe: 'CWE-915', codes: ['prototype-mutation', 'unguarded-dynamic-key-assignment'], note: 'Improperly Controlled Modification of Dynamically-Determined Object Attributes' },
  { cwe: 'CWE-1336', codes: ['prototype-mutation', 'unguarded-dynamic-key-assignment'], note: 'Template-engine prototype-pollution variants' },
  { cwe: 'CWE-798', codes: ['hardcoded-credential'], note: 'Use of Hard-coded Credentials' },
  { cwe: 'CWE-321', codes: ['hardcoded-credential'], note: 'Use of Hard-coded Cryptographic Key' },
  { cwe: 'CWE-327', codes: ['weak-crypto-algorithm'], note: 'Use of a Broken/Risky Cryptographic Algorithm' },
  { cwe: 'CWE-328', codes: ['weak-crypto-algorithm'], note: 'Use of Weak Hash' },
  { cwe: 'CWE-295', codes: ['tls-verification-disabled'], note: 'Improper Certificate Validation' },
  { cwe: 'CWE-78', codes: ['unreviewed-child-process'], note: 'OS Command Injection (proxy signal: this rule only detects the child_process IMPORT, not that untrusted input reaches it)' },
  { cwe: 'CWE-77', codes: ['unreviewed-child-process'], note: 'Command Injection (same proxy-signal caveat as CWE-78)' },
];

const CWE_TO_CODES_MAP = new Map(CWE_TO_SELFAUDIT_CODE.map((e) => [e.cwe, e.codes]));

/** First CWE in the entry's list that this table maps, as its full list of acceptable codes, or null if none does. */
export function expectedSelfAuditCodes(cweList) {
  for (const cwe of cweList || []) {
    if (CWE_TO_CODES_MAP.has(cwe)) return CWE_TO_CODES_MAP.get(cwe);
  }
  return null;
}

// ── per-entry grading ─────────────────────────────────────────────────

/**
 * Grades one dataset entry's selfAudit result. `preCodes`/`postCodes`
 * are the finding `code` strings selfAudit actually raised against the
 * pre-patch and post-patch fixtures (auditSourceTree(...).findings.map(f=>f.code)).
 *
 * Returns the four raw counts (tp/fn/fp/tn are each 0 or 1, summed later
 * across the dataset -- see aggregate()) PLUS a human-readable verdict.
 * FP is computed independently of the pre-patch outcome, exactly per
 * this benchmark's own spec: "flags something in the post-patch (fixed)
 * version at the same... location" is a standalone false-positive
 * event, not something that only counts when paired with a miss.
 */
export function gradeEntry(expectedCodes, preCodes, postCodes) {
  if (!expectedCodes || expectedCodes.length === 0) {
    return { verdict: 'not-covered', tp: 0, fn: 0, fp: 0, tn: 0 };
  }
  const pre = preCodes.some((c) => expectedCodes.includes(c));
  const post = postCodes.some((c) => expectedCodes.includes(c));
  const tp = pre ? 1 : 0;
  const fn = pre ? 0 : 1;
  const fp = post ? 1 : 0;
  const tn = post ? 0 : 1;

  let verdict;
  if (pre && !post) verdict = 'correct'; // flagged the vulnerable version, correctly silent on the fixed one
  else if (pre && post) verdict = 'flags-both-versions'; // right rule, but noisy -- doesn't actually distinguish fixed from vulnerable
  else if (!pre && post) verdict = 'missed-pre-flagged-post'; // rare/pathological: silent on the real bug, noisy on the fix
  else verdict = 'missed'; // silent on both

  return { verdict, tp, fn, fp, tn };
}

// ── secondary, clearly-labeled metric: annotation-aware precision ────
//
// selfAudit's eval-usage/function-constructor-usage findings carry a
// `guardDetected` field and unreviewed-child-process carries
// `usesShellInterpolation` (see selfAudit.js's header on both -- proximity
// heuristics, not real taint tracking, each validated against one real
// dataset entry: serialize-to-js's real fix adds a `sanitize()` call
// right before an unchanged `new Function()`; growl's real fix switches
// `exec()` to `spawn()`). A finding on the POST-PATCH fixture that
// carries one of these "looks fixed" annotations is still a false
// positive under gradeEntry()'s STRICT, primary definition above -- that
// definition never changes, for comparability across runs of this
// benchmark. This is a SEPARATE, additional lens: "if a human used these
// annotations to deprioritize, how many of the strict FPs would they
// have correctly set aside?" Never used to replace or inflate the
// primary number; reported alongside it, explicitly labeled.
export function isAnnotatedAsLikelyFixed(finding) {
  if (!finding) return false;
  if (finding.code === 'eval-usage' || finding.code === 'function-constructor-usage') return finding.guardDetected === true;
  if (finding.code === 'unreviewed-child-process') return finding.usesShellInterpolation === false;
  return false;
}

/**
 * Same contingency-table shape as gradeEntry, but `postFindings` is the
 * FULL finding objects (not just codes) for the post-patch fixture, and
 * `fp`/`tn` are recomputed treating an annotated-as-likely-fixed post
 * finding as NOT a false positive. Everything else (tp/fn, based on
 * preCodes) is identical to gradeEntry -- this lens only ever changes
 * how POST-patch noise is counted, never pre-patch detection.
 */
export function gradeEntryAnnotationAware(expectedCodes, preCodes, postFindings) {
  if (!expectedCodes || expectedCodes.length === 0) {
    return { tp: 0, fn: 0, fp: 0, tn: 0 };
  }
  const pre = preCodes.some((c) => expectedCodes.includes(c));
  const relevantPostFindings = (postFindings || []).filter((f) => expectedCodes.includes(f.code));
  const postCountsAsFp = relevantPostFindings.some((f) => !isAnnotatedAsLikelyFixed(f));
  return {
    tp: pre ? 1 : 0,
    fn: pre ? 0 : 1,
    fp: postCountsAsFp ? 1 : 0,
    tn: postCountsAsFp ? 0 : 1,
  };
}

// ── aggregation ────────────────────────────────────────────────────────

/**
 * rows: array of {kernel, covered: bool, tp, fn, fp, tn}. Only rows
 * with covered===true contribute to precision/recall -- a kernel that
 * is structurally not-applicable, or a CWE class selfAudit has no rule
 * for, must never silently look like a perfect (0 FP, 0 FN) or a
 * failing score; it is excluded from the fraction entirely, and its
 * exclusion count is reported alongside the score so nobody mistakes
 * "excluded" for "flawless."
 */
export function aggregate(rows) {
  const totals = { tp: 0, fn: 0, fp: 0, tn: 0, covered: 0, notCovered: 0 };
  for (const r of rows) {
    if (!r.covered) { totals.notCovered++; continue; }
    totals.covered++;
    totals.tp += r.tp; totals.fn += r.fn; totals.fp += r.fp; totals.tn += r.tn;
  }
  const precision = (totals.tp + totals.fp) > 0 ? totals.tp / (totals.tp + totals.fp) : null;
  const recall = (totals.tp + totals.fn) > 0 ? totals.tp / (totals.tp + totals.fn) : null;
  return { ...totals, precision, recall };
}

/**
 * Runs the full grading pass for one kernel across the whole dataset.
 * `findingsByEntry` maps dataset entry id -> {preCodes, postCodes,
 * postFindings} (already computed by bench/run.mjs via auditSourceTree;
 * this function does no I/O; `postFindings` is optional -- omitting it
 * just means the annotation-aware secondary metric sees no findings).
 * Returns {kernelId, rows, summary, annotationAwareSummary} where `rows`
 * is the literal {cve_id, kernel, verdict, ground_truth, correct} shape
 * the task asks for (plus the raw annotation-aware counts per row), and
 * `summary`/`annotationAwareSummary` are aggregate() over the covered
 * subset under the strict and annotation-aware lenses respectively --
 * see gradeEntryAnnotationAware's header for why these are two DIFFERENT
 * numbers, not one replacing the other.
 */
export function gradeSelfAuditAcrossDataset(dataset, findingsByEntry) {
  const rows = dataset.map((entry) => {
    const expectedCodes = expectedSelfAuditCodes(entry.cwe);
    const f = findingsByEntry[entry.id] || { preCodes: [], postCodes: [], postFindings: [] };
    const graded = gradeEntry(expectedCodes, f.preCodes, f.postCodes);
    const gradedAnnotationAware = gradeEntryAnnotationAware(expectedCodes, f.preCodes, f.postFindings);
    return {
      cve_id: entry.id,
      kernel: SELF_AUDIT_KERNEL_ID,
      verdict: graded.verdict,
      ground_truth: { cwe: entry.cwe, expectedSelfAuditCodes: expectedCodes || null },
      correct: graded.verdict === 'not-covered' ? null : graded.verdict === 'correct',
      covered: Boolean(expectedCodes),
      tp: graded.tp, fn: graded.fn, fp: graded.fp, tn: graded.tn,
      annotationAware: { ...gradedAnnotationAware, covered: Boolean(expectedCodes) },
    };
  });
  return {
    kernelId: SELF_AUDIT_KERNEL_ID,
    rows,
    summary: aggregate(rows),
    annotationAwareSummary: aggregate(rows.map((r) => ({ covered: r.covered, ...r.annotationAware }))),
  };
}

/** Every not-applicable kernel gets the identical row shape, for a uniform report -- never silently omitted. */
export function notApplicableRowsForDataset(dataset, kernelId) {
  return dataset.map((entry) => ({
    cve_id: entry.id,
    kernel: kernelId,
    verdict: 'not-applicable',
    ground_truth: { cwe: entry.cwe, expectedSelfAuditCodes: null },
    correct: null,
    covered: false,
    reason: NOT_APPLICABLE_REASON,
    tp: 0, fn: 0, fp: 0, tn: 0,
  }));
}
