// chainKernel.test.mjs; coverage for compositional exploit-chain
// reachability. Three required properties, per this kernel's own spec:
//   (a) a synthetic chain that should NOT compose is correctly
//       rejected -- proven via SAT/DRAT, not merely "no edge found"
//   (b) a synthetic chain that SHOULD compose is correctly proven via
//       SAT, independently cross-checked by a second, non-SAT algorithm
//   (c) severity escalation matches SEVERITY_RULE_TABLE exactly, never
//       a plausible-sounding guess -- including that it ignores the
//       individual findings' own severity labels entirely (no summing).

import test from 'node:test';
import assert from 'node:assert/strict';

import {
  normalizeChainInput,
  buildChainGraph,
  replayChainFixpoint,
  encodeChainReachability,
  classifyChainSeverity,
  proveChainReachability,
  SEVERITY_RULE_TABLE,
} from '../src/lib/chainKernel.js';
import { checkChainReachabilityProof } from '../src/lib/dratProof.js';
import { getKernel } from '../src/lib/kernelRegistry.js';

// ── normalizeChainInput ──────────────────────────────────────────────

test('normalizeChainInput rejects fewer than 2 findings', () => {
  assert.throws(() => normalizeChainInput({ findings: [{ id: 'a', kernelId: 'k', preconditions: [], postconditions: ['x'] }], targetFindingId: 'a' }), /at least 2 findings/);
});

test('normalizeChainInput rejects a duplicate finding id', () => {
  assert.throws(() => normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k', postconditions: ['x'] },
      { id: 'a', kernelId: 'k', preconditions: ['x'] },
    ],
    targetFindingId: 'a',
  }), /duplicate finding id/);
});

test('normalizeChainInput rejects an unknown targetFindingId', () => {
  assert.throws(() => normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k', postconditions: ['x'] },
      { id: 'b', kernelId: 'k', preconditions: ['x'] },
    ],
    targetFindingId: 'nope',
  }), /does not match any finding id/);
});

test('normalizeChainInput rejects a target finding with no preconditions (not a chain question)', () => {
  assert.throws(() => normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k', postconditions: ['x'] },
      { id: 'b', kernelId: 'k', postconditions: ['y'] },
    ],
    targetFindingId: 'b',
  }), /has no preconditions to chain into/);
});

test('normalizeChainInput accepts a well-formed spec and defaults initialAtoms to []', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'boundary-check', postconditions: ['x'] },
      { id: 'b', kernelId: 'consistency', preconditions: ['x'] },
    ],
    targetFindingId: 'b',
  });
  assert.deepEqual(spec.initialAtoms, []);
  assert.equal(spec.findings.length, 2);
  assert.equal(spec.findings[0].unauthenticated, false);
});

// ── buildChainGraph: diagnostic only ─────────────────────────────────

test('buildChainGraph finds a pairwise edge exactly where postcondition/precondition atoms overlap, and no edge otherwise', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k1', postconditions: ['x'] },
      { id: 'b', kernelId: 'k2', preconditions: ['x'], postconditions: ['y'] },
      { id: 'c', kernelId: 'k3', preconditions: ['unrelated-atom'] },
    ],
    targetFindingId: 'b',
  });
  const graph = buildChainGraph(spec);
  assert.deepEqual(graph.edges.map((e) => [e.from, e.to]), [['a', 'b']]);
});

// ── (a) a chain that should NOT compose ──────────────────────────────

function nonComposingSpec() {
  return normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'consistency', postconditions: ['atom-x'] },
      { id: 'b', kernelId: 'boundary-check', preconditions: ['atom-y'], postconditions: ['atom-z'] },
    ],
    initialAtoms: [],
    targetFindingId: 'b',
  });
}

test('replayChainFixpoint (independent, non-SAT) correctly reports the non-composing pair as unreachable', () => {
  const replay = replayChainFixpoint(nonComposingSpec());
  assert.equal(replay.reachable, false);
});

test('(a) a synthetic 2-finding chain that should not compose is correctly rejected, proven via SAT/DRAT', () => {
  const result = proveChainReachability(nonComposingSpec());
  assert.equal(result.verdict, 'chain-rejected');
  assert.equal(result.proofIndependentlyVerified, true);
  assert.ok(Array.isArray(result.proof) && result.proof.length > 0);
  // The rejection itself is independently re-checkable by a caller,
  // not just asserted by this function's own say-so.
  const encoded = encodeChainReachability(nonComposingSpec());
  const check = checkChainReachabilityProof(encoded.numVars, encoded.clauses, result.proof, { targetFindingId: 'b' });
  assert.equal(check.valid, true);
});

test('a chain missing a required initial atom entirely (no producer anywhere) is also rejected', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k1', preconditions: ['never-provided'], postconditions: ['x'] },
      { id: 'b', kernelId: 'k2', preconditions: ['x'] },
    ],
    initialAtoms: [],
    targetFindingId: 'b',
  });
  const result = proveChainReachability(spec);
  assert.equal(result.verdict, 'chain-rejected');
});

// ── (b) a chain that SHOULD compose ──────────────────────────────────

function composingSpec() {
  return normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'boundary-check', region: 'src/upload.js', preconditions: ['unauthenticated-request'], postconditions: ['path-traversal-write'], unauthenticated: true, severity: 'medium' },
      { id: 'b', kernelId: 'consistency', region: 'src/upload.js', preconditions: ['path-traversal-write'], postconditions: ['arbitrary-file-write'], unauthenticated: true, severity: 'medium' },
    ],
    initialAtoms: ['unauthenticated-request'],
    targetFindingId: 'b',
  });
}

test('replayChainFixpoint independently confirms the composing pair is reachable, in the right order', () => {
  const replay = replayChainFixpoint(composingSpec());
  assert.equal(replay.reachable, true);
  assert.deepEqual(replay.order, ['a']);
});

test('(b) a synthetic 2-finding chain that should compose is correctly proven via SAT, cross-checked by the independent replay', () => {
  const result = proveChainReachability(composingSpec());
  assert.equal(result.verdict, 'chain-verified');
  assert.deepEqual(result.chain.slice().sort(), ['a', 'b']);
  assert.deepEqual(result.order, ['a', 'b']);
  assert.equal(result.severity, 'critical'); // boundary-check + consistency + unauthenticated
  assert.equal(result.severityRuleId, 'unauth-boundary-bypass-plus-consistency-contradiction');
});

test('a 3-finding chain (boundary bypass -> domain-of-validity narrowing -> consistency contradiction) composes end to end', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'boundary-bypass', kernelId: 'boundary-check', region: 'src/upload.js', preconditions: ['unauthenticated-request'], postconditions: ['path-outside-sandbox'], unauthenticated: true },
      { id: 'domain-narrowing', kernelId: 'domain-of-validity', region: 'src/upload.js', preconditions: ['path-outside-sandbox'], postconditions: ['narrowed-write-target-reachable'], unauthenticated: true },
      { id: 'consistency-contradiction', kernelId: 'consistency', region: 'src/upload.js', preconditions: ['narrowed-write-target-reachable'], postconditions: ['arbitrary-write-verified'], unauthenticated: true },
    ],
    initialAtoms: ['unauthenticated-request'],
    targetFindingId: 'consistency-contradiction',
  });
  const result = proveChainReachability(spec);
  assert.equal(result.verdict, 'chain-verified');
  assert.deepEqual(result.chain.slice().sort(), ['boundary-bypass', 'consistency-contradiction', 'domain-narrowing']);
  assert.deepEqual(result.order, ['boundary-bypass', 'domain-narrowing', 'consistency-contradiction']);
  assert.equal(result.severity, 'critical');
});

test('a finding that is individually triggerable without any chain still requires the stated preconditions -- an unrelated bystander finding is not swept into the chain', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'boundary-check', postconditions: ['x'], unauthenticated: true },
      { id: 'b', kernelId: 'consistency', preconditions: ['x'], unauthenticated: true },
      { id: 'bystander', kernelId: 'mcmc', preconditions: ['totally-unrelated'], postconditions: ['also-unrelated'] },
    ],
    initialAtoms: [],
    targetFindingId: 'b',
  });
  const result = proveChainReachability(spec);
  assert.equal(result.verdict, 'chain-verified');
  assert.deepEqual(result.chain.slice().sort(), ['a', 'b']);
  assert.ok(!result.chain.includes('bystander'));
});

// ── circular self-support must NOT be exploitable ────────────────────
//
// If i's precondition is only ever produced by i itself (or by a cycle
// with no real base case), the layered encoding must NOT let the
// solver "bootstrap" that atom into existence -- see this file's header
// on why a flat implication graph would be unsound here.

test('two findings that only justify EACH OTHER (a mutual cycle with no base case) do not compose -- no free lunch from circular support', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k1', preconditions: ['atom-b'], postconditions: ['atom-a'] },
      { id: 'b', kernelId: 'k2', preconditions: ['atom-a'], postconditions: ['atom-b'] },
    ],
    initialAtoms: [],
    targetFindingId: 'b',
  });
  const result = proveChainReachability(spec);
  assert.equal(result.verdict, 'chain-rejected');
  assert.equal(result.proofIndependentlyVerified, true);
});

test('a finding whose only producer is itself does not compose either (self-loop, degenerate cycle)', () => {
  const spec = normalizeChainInput({
    findings: [
      { id: 'a', kernelId: 'k1', preconditions: ['seed'], postconditions: ['x'] },
      { id: 'b', kernelId: 'k2', preconditions: ['atom-self'], postconditions: ['atom-self'] },
    ],
    initialAtoms: ['seed'],
    targetFindingId: 'b',
  });
  const result = proveChainReachability(spec);
  assert.equal(result.verdict, 'chain-rejected');
});

// ── undecided (budget exhaustion) must never be promoted ─────────────

test('a budget of zero conflicts on a genuinely composing chain reports undecided, never a false rejection or false confirmation', () => {
  const result = proveChainReachability(composingSpec(), { maxConflicts: 0, maxMs: 100000 });
  // Either the solver settles it with zero conflicts needed (pure unit
  // propagation, no decisions) -- fine, or it reports undecided; it
  // must never claim chain-rejected when the true answer is composable.
  assert.notEqual(result.verdict, 'chain-rejected');
});

// ── (c) severity table exactness ─────────────────────────────────────

const F = (id, kernelId, unauthenticated = false, severity = 'unrated') => ({ id, kernelId, unauthenticated, severity });

test('(c) boundary-check + consistency + unauthenticated escalates to critical, by the named rule', () => {
  const r = classifyChainSeverity([F('a', 'boundary-check', true), F('b', 'consistency', true)]);
  assert.equal(r.severity, 'critical');
  assert.equal(r.ruleId, 'unauth-boundary-bypass-plus-consistency-contradiction');
});

test('(c) boundary-check + domain-of-validity + unauthenticated escalates to critical, by the named rule', () => {
  const r = classifyChainSeverity([F('a', 'boundary-check', true), F('b', 'domain-of-validity', true)]);
  assert.equal(r.severity, 'critical');
  assert.equal(r.ruleId, 'unauth-boundary-bypass-plus-domain-narrowing');
});

test('(c) self-audit + boundary-check escalates to critical regardless of the authentication flag', () => {
  const r = classifyChainSeverity([F('a', 'self-audit', false), F('b', 'boundary-check', false)]);
  assert.equal(r.severity, 'critical');
  assert.equal(r.ruleId, 'self-audit-credential-or-tls-plus-boundary-bypass');
});

test('(c) the SAME kernelId combination, but with every finding authenticated, is high, not critical -- authentication genuinely changes the outcome', () => {
  const r = classifyChainSeverity([F('a', 'boundary-check', false), F('b', 'consistency', false)]);
  assert.equal(r.severity, 'high');
  assert.equal(r.ruleId, 'boundary-bypass-plus-consistency-authenticated-only');
});

test('(c) domain-of-validity + consistency alone (no boundary-check/self-audit) is medium', () => {
  const r = classifyChainSeverity([F('a', 'domain-of-validity', true), F('b', 'consistency', true)]);
  assert.equal(r.severity, 'medium');
  assert.equal(r.ruleId, 'domain-narrowing-plus-consistency-only');
});

test('(c) a kernelId combination matching no rule is left unrated, never guessed', () => {
  const r = classifyChainSeverity([F('a', 'mcmc', true), F('b', 'dynamics', true)]);
  assert.equal(r.severity, 'unrated');
  assert.equal(r.ruleId, null);
  assert.match(r.honesty, /mcmc\+?/); // names the actual unmatched combination
});

test('(c) severity is NEVER computed from the individual findings\' own severity labels -- summing/averaging would violate this kernel\'s whole premise', () => {
  const allLow = classifyChainSeverity([F('a', 'boundary-check', true, 'low'), F('b', 'consistency', true, 'low')]);
  const allCritical = classifyChainSeverity([F('a', 'boundary-check', true, 'critical'), F('b', 'consistency', true, 'critical')]);
  assert.equal(allLow.severity, allCritical.severity);
  assert.equal(allLow.severity, 'critical'); // driven entirely by kernelId+unauthenticated, not by the input severity fields
});

test('(c) every SEVERITY_RULE_TABLE entry has a stable id, a description, and a valid severity level', () => {
  const validLevels = new Set(['critical', 'high', 'medium', 'low']);
  const ids = new Set();
  for (const rule of SEVERITY_RULE_TABLE) {
    assert.equal(typeof rule.id, 'string');
    assert.ok(rule.id.length > 0);
    assert.ok(!ids.has(rule.id), `duplicate rule id ${rule.id}`);
    ids.add(rule.id);
    assert.equal(typeof rule.description, 'string');
    assert.ok(rule.description.length > 0);
    assert.ok(validLevels.has(rule.severity), `${rule.id} has an invalid severity "${rule.severity}"`);
    assert.equal(typeof rule.match, 'function');
  }
});

test('(c) severity end-to-end through proveChainReachability matches classifyChainSeverity exactly for the same chain', () => {
  const spec = composingSpec();
  const proved = proveChainReachability(spec);
  const direct = classifyChainSeverity(spec.findings);
  assert.equal(proved.severity, direct.severity);
  assert.equal(proved.severityRuleId, direct.ruleId);
});

// ── checkChainReachabilityProof wrapper (dratProof.js extension) ─────

test('checkChainReachabilityProof delegates to the same RUP algorithm and echoes context on both outcomes', () => {
  const valid = checkChainReachabilityProof(1, [[1], [-1]], ['0'], { targetFindingId: 'x' });
  assert.equal(valid.valid, true);
  assert.deepEqual(valid.context, { targetFindingId: 'x' });
  assert.match(valid.meaning, /UNREACHABLE/);

  const invalid = checkChainReachabilityProof(2, [[1, 2], [1, -2], [-1, 2], [-1, -2]], ['0'], { targetFindingId: 'y' });
  assert.equal(invalid.valid, false);
  assert.deepEqual(invalid.context, { targetFindingId: 'y' });
  assert.match(invalid.meaning, /FAILED independent verification/);
});

// ── registered in kernelRegistry.js like the other kernels ───────────

test('chain-reachability is registered, needs no model extraction, and round-trips normalize -> run through the registry', () => {
  const kernel = getKernel('chain-reachability');
  assert.equal(kernel.needsModelExtraction, false);
  assert.equal(kernel.buildPrompt, null);
  const spec = kernel.normalize({
    findings: [
      { id: 'a', kernelId: 'boundary-check', postconditions: ['x'] },
      { id: 'b', kernelId: 'consistency', preconditions: ['x'] },
    ],
    targetFindingId: 'b',
  });
  const result = kernel.run(spec);
  assert.equal(result.verdict, 'chain-verified');
});
