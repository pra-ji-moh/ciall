// sandboxConfig.test.mjs; the regression test for the bug found by
// actually copying this repo into a fresh location and running it: a
// raw filesystem copy carries .ciall-sandbox.json along, and without a
// check, a copied install would silently reuse the ORIGINAL machine's
// sandbox path instead of establishing its own. Every test here uses
// CIALL_SANDBOX_CONFIG_PATH to point at a temp file, never the real
// project config.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const tmpConfigDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-sandboxconfig-test-'));
const tmpConfigPath = path.join(tmpConfigDir, '.ciall-sandbox.json');
delete process.env.CIALL_SANDBOX_PATH; // make sure the config-file path is what's actually exercised
process.env.CIALL_SANDBOX_CONFIG_PATH = tmpConfigPath;

const { getSandboxPath, getConfigPath } = await import('../src/lib/sandboxConfig.js');

test('getConfigPath honors the CIALL_SANDBOX_CONFIG_PATH override', () => {
  assert.equal(getConfigPath(), tmpConfigPath);
});

test('first call persists a config with both sandboxPath and installRoot', () => {
  const sandboxPath = getSandboxPath();
  assert.ok(fs.existsSync(tmpConfigPath));
  const cfg = JSON.parse(fs.readFileSync(tmpConfigPath, 'utf8'));
  assert.equal(cfg.sandboxPath, sandboxPath);
  assert.ok(cfg.installRoot, 'installRoot must be recorded so a future copy can be detected as stale');
});

test('a second call with the SAME installRoot reuses the persisted path (does not recompute)', () => {
  const first = getSandboxPath();
  const second = getSandboxPath();
  assert.equal(first, second);
});

test('a config copied in from a DIFFERENT install root is treated as stale, not trusted', () => {
  // Simulates exactly what happened during the real strange-environment
  // test: a config file that legitimately belongs to a different
  // install location (as if `cp -r` had carried it here).
  fs.writeFileSync(tmpConfigPath, JSON.stringify({ sandboxPath: 'C:\\some\\other\\machine\\ciall-sandbox', installRoot: 'C:\\some\\other\\machine\\ciall-substrate', createdAt: 1 }));
  const resolved = getSandboxPath();
  assert.notEqual(resolved, 'C:\\some\\other\\machine\\ciall-sandbox', 'a stale config from a different install must NOT be trusted');
  // and it must have overwritten the stale file with a fresh, correct one
  const cfg = JSON.parse(fs.readFileSync(tmpConfigPath, 'utf8'));
  assert.equal(cfg.sandboxPath, resolved);
});

test('a pre-existing config with no installRoot at all (from before this field existed) is also treated as stale', () => {
  fs.writeFileSync(tmpConfigPath, JSON.stringify({ sandboxPath: 'C:\\legacy\\path', createdAt: 1 })); // old shape, no installRoot
  const resolved = getSandboxPath();
  assert.notEqual(resolved, 'C:\\legacy\\path');
});

test('CIALL_SANDBOX_PATH still overrides everything, config file included', () => {
  process.env.CIALL_SANDBOX_PATH = path.join(tmpConfigDir, 'explicit-override');
  try {
    assert.equal(getSandboxPath(), path.resolve(path.join(tmpConfigDir, 'explicit-override')));
  } finally {
    delete process.env.CIALL_SANDBOX_PATH;
  }
});
