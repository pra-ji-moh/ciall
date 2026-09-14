// shiftLeftScan.test.mjs; proves selfAudit findings, from a REAL scan of
// REAL temp files (no hand-authored chainKernel specs -- that would
// defeat the point of a proactive, automatic scanner), get mapped into
// chain atoms and either PROVEN (via chainKernel's real SAT+DRAT
// machinery) or PROVEN NOT to compose, exactly as they should.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import {
  scanForProactiveChains,
  buildChainEligibleFindings,
  mapFindingToChainInput,
  computeCausalAncestry,
  SELF_AUDIT_TO_CHAIN_ATOMS,
} from '../src/lib/shiftLeftScan.js';
import { auditSourceTree } from '../src/lib/selfAudit.js';

function tempRepo(files) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-shiftleft-test-'));
  for (const [relPath, content] of Object.entries(files)) {
    const full = path.join(root, relPath);
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, content, 'utf8');
  }
  return root;
}

// ── the real, composing case ──────────────────────────────────────────

const COMPOSING_FILE = `
var childProcess = require('child_process');
function merge(target, source) {
  for (var key in source) {
    target[key] = source[key];
  }
  return target;
}
function run(cmd) {
  return childProcess.exec(cmd);
}
`;

test('a real scan of a file with BOTH an unguarded merge and child_process usage proves a genuine 2-finding chain, via real SAT/DRAT, not a hand-authored spec', () => {
  const root = tempRepo({ 'src/merge.js': COMPOSING_FILE });
  const result = scanForProactiveChains(root);

  assert.equal(result.chainEligibleCount, 2);
  assert.equal(result.rejected.length, 0);

  // The merge finding is itself directly reachable from the shared
  // entry atom (no composition needed) -- reported separately, not
  // conflated with a genuine chain.
  assert.equal(result.directlyReachable.length, 1);
  assert.match(result.directlyReachable[0].target, /unguarded-dynamic-key-assignment/);

  // The child_process finding genuinely NEEDS the merge finding to have
  // fired first -- a real, 2-distinct-finding composition.
  assert.equal(result.proven.length, 1);
  const chain = result.proven[0];
  assert.equal(chain.verdict, 'chain-verified');
  assert.equal(chain.chain.length, 2);
  assert.match(chain.target, /unreviewed-child-process/);
  assert.match(chain.targetFile, /merge\.js$/);
  // The severity rule table (chainKernel.js) should fire on this real composition.
  assert.ok(['critical', 'high', 'medium', 'unrated'].includes(chain.severity));
  // causalAncestry is the MINIMAL backward trace, exactly the 2 findings
  // that actually matter -- not whatever the (non-minimal) SAT witness happened to include.
  assert.equal(chain.causalAncestry.length, 2);
  assert.ok(chain.causalAncestry.some((id) => id.includes('unguarded-dynamic-key-assignment')));
  assert.ok(chain.causalAncestry.some((id) => id.includes('unreviewed-child-process')));
});

test('computeCausalAncestry excludes findings that are trivially active but causally irrelevant to the target', () => {
  const initialAtoms = ['unauthenticated-request'];
  const pollute = { id: 'pollute', preconditions: ['unauthenticated-request'], postconditions: ['prototype-polluted@f'] };
  const exec = { id: 'exec', preconditions: ['prototype-polluted@f'], postconditions: ['arbitrary-command-execution@f'] };
  const unrelatedFreebie = { id: 'freebie', preconditions: ['unauthenticated-request'], postconditions: ['credential-exposed@g'] }; // trivially active, but irrelevant to reaching `exec`
  const ancestry = computeCausalAncestry(exec, [pollute, exec, unrelatedFreebie], initialAtoms);
  assert.deepEqual(new Set(ancestry), new Set(['pollute', 'exec']));
  assert.ok(!ancestry.includes('freebie'));
});

// ── the same shapes, but nothing to chain into on their own ──────────

test('a lone hardcoded-credential finding is chain-eligible but never a valid TARGET (no precondition to chain into)', () => {
  const root = tempRepo({ 'src/secret.js': "const apiKey = 'sk_live_ABCDEFGHIJKLMNOPQRSTUVWX';\n" });
  const findings = buildChainEligibleFindings(auditSourceTree(root));
  assert.equal(findings.length, 1);
  assert.equal(findings[0].preconditions.length, 0);
});

test('two chain-eligible findings whose atoms genuinely do not connect are proven NOT to compose', () => {
  const root = tempRepo({
    // hardcoded-credential -- no precondition, not a valid target at all
    'src/a.js': "const apiKey = 'sk_live_ABCDEFGHIJKLMNOPQRSTUVWX';\n",
    // unreviewed-child-process ALONE -- needs 'prototype-polluted@src/b.js',
    // which nothing in this scan produces (no pollution finding present)
    'src/b.js': "var childProcess = require('child_process');\nfunction run(cmd) { return childProcess.exec(cmd); }\n",
  });
  const result = scanForProactiveChains(root);
  assert.equal(result.chainEligibleCount, 2);
  assert.equal(result.proven.length, 0);
  assert.equal(result.directlyReachable.length, 0);
  assert.ok(result.rejected.length >= 1, 'the child_process target has an unsatisfiable precondition and must be proven-rejected, not silently dropped');
});

// ── per-file scoping: the SAME shapes in DIFFERENT files must not compose ─

test('the exact composing shapes, split across two DIFFERENT files, do NOT compose -- region-namespacing works', () => {
  const root = tempRepo({
    'src/pollute.js': `
      function merge(target, source) {
        for (var key in source) {
          target[key] = source[key];
        }
        return target;
      }
    `,
    'src/run.js': `
      var childProcess = require('child_process');
      function run(cmd) { return childProcess.exec(cmd); }
    `,
  });
  const result = scanForProactiveChains(root);
  assert.equal(result.chainEligibleCount, 2);
  assert.equal(result.proven.length, 0, 'findings in unrelated files must never compose just because they share an atom name');
  // pollute.js's merge is still individually reachable from the shared
  // entry atom (that alone is real and correct); run.js's child_process
  // needs prototype-polluted@src/run.js specifically, which nothing in
  // THAT file produces -- proven-rejected, not a cross-file chain.
  assert.equal(result.directlyReachable.length, 1);
  assert.match(result.directlyReachable[0].target, /pollute\.js/);
  assert.ok(result.rejected.length >= 1);
  assert.ok(result.rejected.some((r) => /run\.js/.test(r.target)));
});

// ── codes with no chain-atom mapping are excluded entirely ────────────

test('a finding whose code has no entry in SELF_AUDIT_TO_CHAIN_ATOMS is excluded from chain-eligible findings', () => {
  const root = tempRepo({ 'src/x.js': "export async function go(url) { return fetch(url); }\n" }); // unreviewed-fetch-call -- not in the table
  const findings = buildChainEligibleFindings(auditSourceTree(root));
  assert.deepEqual(findings, []);
});

test('mapFindingToChainInput returns null for an unmapped code, without throwing', () => {
  assert.equal(mapFindingToChainInput({ code: 'unreviewed-fetch-call', file: 'x.js' }, 0), null);
});

// ── fewer than 2 chain-eligible findings: an honest note, not a fake verdict ─

test('zero chain-eligible findings produces an honest note, no proven/rejected noise', () => {
  const root = tempRepo({ 'src/clean.js': 'export function add(a, b) { return a + b; }\n' });
  const result = scanForProactiveChains(root);
  assert.equal(result.chainEligibleCount, 0);
  assert.deepEqual(result.proven, []);
  assert.deepEqual(result.directlyReachable, []);
  assert.deepEqual(result.rejected, []);
  assert.match(result.note, /nothing to compose/);
});

test('exactly one chain-eligible finding also produces an honest note -- a chain needs at least two', () => {
  const root = tempRepo({ 'src/x.js': "const apiKey = 'sk_live_ABCDEFGHIJKLMNOPQRSTUVWX';\n" });
  const result = scanForProactiveChains(root);
  assert.equal(result.chainEligibleCount, 1);
  assert.match(result.note, /at least two/);
});

// ── the mapping table itself ───────────────────────────────────────────

test('every SELF_AUDIT_TO_CHAIN_ATOMS entry has real, non-empty postconditions', () => {
  for (const [code, atoms] of Object.entries(SELF_AUDIT_TO_CHAIN_ATOMS)) {
    assert.ok(Array.isArray(atoms.preconditions), `${code}: preconditions must be an array`);
    assert.ok(Array.isArray(atoms.postconditions) && atoms.postconditions.length > 0, `${code}: postconditions must be non-empty`);
  }
});

test('mapFindingToChainInput namespaces every atom by file, so the same code in two files never accidentally shares an atom', () => {
  const a = mapFindingToChainInput({ code: 'prototype-mutation', file: 'src/a.js', severity: 'high' }, 0);
  const b = mapFindingToChainInput({ code: 'prototype-mutation', file: 'src/b.js', severity: 'high' }, 1);
  assert.notEqual(a.postconditions[0], b.postconditions[0]);
  assert.ok(a.postconditions[0].endsWith('@src/a.js'));
  assert.ok(b.postconditions[0].endsWith('@src/b.js'));
});
