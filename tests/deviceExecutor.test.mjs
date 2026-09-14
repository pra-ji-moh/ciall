// deviceExecutor.test.mjs; Phase 4 coverage. Every write/read here targets
// an isolated temp sandbox (CIALL_SANDBOX_PATH / CIALL_AUDIT_LOG_PATH),
// never this project's real persisted sandbox or audit log.

import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { shutdownPool } from '../src/lib/kernelWorkerPool.js';

// writeFile's verify option runs claims through orchestrator.runPipeline
// (parallel:true by default since upgrade 1), which starts
// kernelWorkerPool.js's worker pool; it must be torn down explicitly for
// this file's process to exit (see kernelWorkerPool.js's SHUTDOWN note).
after(() => shutdownPool());

const tmpSandbox = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-sandbox-test-'));
const tmpAudit = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-audit-test-')), 'audit.jsonl');
process.env.CIALL_SANDBOX_PATH = tmpSandbox;
process.env.CIALL_AUDIT_LOG_PATH = tmpAudit;

// Imported AFTER the env vars are set, since sandboxConfig/deviceExecutor
// read them at call time but the module-level boundary-grant cache means
// this file must commit to one sandbox path for its whole run.
const { writeFile, readFile, getExecutionAuditPath, readExecutionAudit } = await import('../src/lib/deviceExecutor.js');
const { getSandboxPath } = await import('../src/lib/sandboxConfig.js');

test('getSandboxPath honors the CIALL_SANDBOX_PATH override', () => {
  assert.equal(getSandboxPath(), tmpSandbox);
});

test('writeFile throws synchronously if confirm is missing', async () => {
  await assert.rejects(() => writeFile('a.txt', 'hi', {}), /requires an explicit confirm/);
});

test('writeFile refuses a path that resolves outside the sandbox', async () => {
  let confirmCalled = false;
  await assert.rejects(
    () => writeFile('../outside.txt', 'hi', { confirm: async () => { confirmCalled = true; return true; } }),
    /resolves outside the sandbox/,
  );
  assert.equal(confirmCalled, false, 'confirm must never be reached for an out-of-sandbox path');
});

test('writeFile rejects non-string content before touching confirm() or disk', async () => {
  let confirmCalled = false;
  await assert.rejects(
    () => writeFile('bad-type.txt', 12345, { confirm: async () => { confirmCalled = true; return true; } }),
    /content must be a string/,
  );
  assert.equal(confirmCalled, false);
});

test('writeFile rejects content over the byte cap before touching confirm() or disk', async () => {
  const { MAX_CONTENT_BYTES } = await import('../src/lib/deviceExecutor.js');
  const oversized = 'x'.repeat(MAX_CONTENT_BYTES + 1);
  let confirmCalled = false;
  const target = path.join(tmpSandbox, 'oversized.txt');
  await assert.rejects(
    () => writeFile('oversized.txt', oversized, { confirm: async () => { confirmCalled = true; return true; } }),
    /over the .*-byte cap/,
  );
  assert.equal(confirmCalled, false, 'an oversized write is out of scope regardless of what a human would say');
  assert.equal(fs.existsSync(target), false);
});

test('writeFile accepts content right at the byte cap', async () => {
  const { MAX_CONTENT_BYTES } = await import('../src/lib/deviceExecutor.js');
  const exact = 'x'.repeat(MAX_CONTENT_BYTES);
  const result = await writeFile('at-cap.txt', exact, { confirm: async () => true });
  assert.equal(result.executed, true);
});

test('writeFile refuses when confirm() declines, and touches nothing on disk', async () => {
  const target = path.join(tmpSandbox, 'declined.txt');
  const result = await writeFile('declined.txt', 'should not land', { confirm: async () => false, requestedBy: 'test' });
  assert.equal(result.executed, false);
  assert.equal(fs.existsSync(target), false);
});

test('writeFile succeeds when confirm() approves, and the file is really there', async () => {
  const result = await writeFile('real.txt', 'hello sandbox', { confirm: async () => true, requestedBy: 'test' });
  assert.equal(result.executed, true);
  assert.equal(fs.readFileSync(result.target, 'utf8'), 'hello sandbox');
});

test('writeFile calls confirm() fresh on every call; nothing persists a standing approval', async () => {
  let calls = 0;
  const confirm = async () => { calls++; return true; };
  await writeFile('one.txt', '1', { confirm });
  await writeFile('two.txt', '2', { confirm });
  assert.equal(calls, 2, 'confirm() must be invoked once per write, not reused across writes');
});

test('readFile refuses without confirm, and returns not-executed when confirm declines', async () => {
  await assert.rejects(() => readFile('real.txt', {}), /requires an explicit confirm/);
  const declined = await readFile('real.txt', { confirm: async () => false });
  assert.equal(declined.executed, false);
});

test('readFile returns the real content when confirm() approves', async () => {
  const result = await readFile('real.txt', { confirm: async () => true, requestedBy: 'test' });
  assert.equal(result.executed, true);
  assert.equal(result.content, 'hello sandbox');
});

test('readFile reports not-executed (not a throw) for a file that does not exist', async () => {
  const result = await readFile('nope.txt', { confirm: async () => true });
  assert.equal(result.executed, false);
  assert.match(result.reason, /does not exist/);
});

test('every call, approved or refused, is durably logged and survives reading back from disk', async () => {
  const entries = readExecutionAudit();
  assert.ok(entries.length >= 6, `expected multiple audit entries, got ${entries.length}`);
  assert.ok(entries.some((e) => e.op === 'write' && e.executed === true));
  assert.ok(entries.some((e) => e.op === 'write' && e.executed === false));
  assert.ok(fs.existsSync(getExecutionAuditPath()));
});

test('a symlink pointing outside the sandbox is refused, not followed', { skip: process.platform === 'win32' && !canSymlink() }, async () => {
  const outsideDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-outside-'));
  const linkPath = path.join(tmpSandbox, 'escape-link');
  fs.symlinkSync(outsideDir, linkPath, 'dir');
  await assert.rejects(
    () => writeFile('escape-link/pwned.txt', 'nope', { confirm: async () => true }),
    /resolves outside the sandbox/,
  );
});

// ── the verify option: writeFile actually running through the kernel
// substrate before confirm(), not just scope-checking ─────────────────

test('writeFile runs verify BEFORE confirm(), and confirm() sees the report', async () => {
  let seenAction = null;
  const confirm = async (action) => { seenAction = action; return true; };
  const designSpec = (kernel, claim) =>
    kernel.id === 'numeric-check'
      ? { kind: 'inequality', lhs: '-1', rhs: 'x^2', vars: [{ name: 'x', domain: [-10, 10] }] } // always holds
      : null;

  const result = await writeFile('verified-ok.txt', 'content', {
    confirm, requestedBy: 'test', verify: { kernelIds: ['numeric-check'], designSpec },
  });

  assert.equal(result.executed, true);
  assert.equal(result.verification.anyViolated, false);
  assert.deepEqual(seenAction.verification.ran, [{ kernelId: 'numeric-check', verdict: 'held' }]);
});

test('a kernel violation is surfaced but does NOT auto-block the write; confirm() still decides', async () => {
  const designSpec = (kernel) =>
    kernel.id === 'numeric-check'
      ? { kind: 'inequality', lhs: 'x^2', rhs: '-1', vars: [{ name: 'x', domain: [-10, 10] }] } // never holds
      : null;

  let seenViolation = null;
  const confirmSeesIt = async (action) => { seenViolation = action.verification.anyViolated; return true; };
  const result = await writeFile('verified-violated.txt', 'content', {
    confirm: confirmSeesIt, requestedBy: 'test', verify: { kernelIds: ['numeric-check'], designSpec },
  });

  assert.equal(seenViolation, true, 'confirm() must see the violation in the action it is asked to approve');
  assert.equal(result.executed, true, 'a kernel violation does not auto-deny; the human still decides, same as consistencyKernel\'s own stance');
  assert.equal(result.verification.anyViolated, true);
});

test('declining after seeing a violation leaves no file on disk', async () => {
  const designSpec = (kernel) =>
    kernel.id === 'numeric-check'
      ? { kind: 'inequality', lhs: 'x^2', rhs: '-1', vars: [{ name: 'x', domain: [-10, 10] }] }
      : null;
  const target = path.join(tmpSandbox, 'declined-after-violation.txt');
  const result = await writeFile('declined-after-violation.txt', 'content', {
    confirm: async (action) => !action.verification.anyViolated, // a real human declining because the kernel flagged it
    verify: { kernelIds: ['numeric-check'], designSpec },
  });
  assert.equal(result.executed, false);
  assert.equal(fs.existsSync(target), false);
});

test('writeFile with no verify option behaves exactly as before (verification is null)', async () => {
  const result = await writeFile('unverified.txt', 'content', { confirm: async () => true });
  assert.equal(result.verification, null);
});

// ── security regression: file CONTENT must never reach the persisted
// audit log, whether written or read back. Only metadata (path, byte
// length, verdicts) belongs there — the audit log is meant to be safe to
// hand to someone auditing what Ciall did, not a second copy of
// everything it touched.

test('the audit log never contains written or read file content, only metadata', async () => {
  const secretMarker = 'SECRET_MARKER_7f3a9c2b_do_not_leak';
  await writeFile('secret-check.txt', `some content with ${secretMarker} inside it`, { confirm: async () => true });
  await readFile('secret-check.txt', { confirm: async () => true });

  const rawLog = fs.readFileSync(getExecutionAuditPath(), 'utf8');
  assert.ok(!rawLog.includes(secretMarker), 'file content leaked into the persisted audit log');

  const entries = readExecutionAudit();
  for (const entry of entries) {
    assert.ok(!JSON.stringify(entry).includes(secretMarker), `entry leaked content: ${JSON.stringify(entry)}`);
  }
});

function canSymlink() {
  try {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-symlink-check-'));
    fs.symlinkSync(dir, path.join(dir, 'self-link'), 'dir');
    return true;
  } catch {
    return false;
  }
}
