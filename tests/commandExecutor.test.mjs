// commandExecutor.test.mjs

import test from 'node:test';
import assert from 'node:assert/strict';
import { runCommand } from '../src/lib/commandExecutor.js';
import { grant, revoke } from '../src/lib/deviceGate.js';

const future = () => Date.now() + 60_000;

test('runCommand throws synchronously if confirm is missing', async () => {
  await assert.rejects(() => runCommand('node', ['-e', '1'], {}), /requires an explicit confirm/);
});

test('runCommand refuses an executable with no active process-launch grant', async () => {
  let confirmCalled = false;
  const result = await runCommand('node', ['-e', '1'], { confirm: async () => { confirmCalled = true; return true; } });
  assert.equal(result.executed, false);
  assert.match(result.reason, /no active grant/);
  assert.equal(confirmCalled, false, 'confirm must never be reached for an out-of-scope executable');
});

test('runCommand refuses when confirm() declines, even with an active grant', async () => {
  grant({ id: 'cmd-test-1', scope: 'process-launch', boundary: ['node'], expiresAt: future(), grantedBy: 'test' });
  const result = await runCommand('node', ['-e', '1'], { confirm: async () => false });
  assert.equal(result.executed, false);
  assert.match(result.reason, /declined/);
  revoke('cmd-test-1');
});

test('runCommand actually executes and captures stdout when confirmed and in-grant', async () => {
  grant({ id: 'cmd-test-2', scope: 'process-launch', boundary: ['node'], expiresAt: future(), grantedBy: 'test' });
  const result = await runCommand('node', ['-e', 'console.log("hello from child")'], { confirm: async () => true });
  assert.equal(result.executed, true);
  assert.equal(result.exitCode, 0);
  assert.match(result.stdout, /hello from child/);
  revoke('cmd-test-2');
});

test('runCommand reports a non-zero exit code without throwing', async () => {
  grant({ id: 'cmd-test-3', scope: 'process-launch', boundary: ['node'], expiresAt: future(), grantedBy: 'test' });
  const result = await runCommand('node', ['-e', 'process.exit(3)'], { confirm: async () => true });
  assert.equal(result.executed, true);
  assert.equal(result.exitCode, 3);
  revoke('cmd-test-3');
});

test('an executable not on the grant boundary is refused even while a DIFFERENT executable is granted', async () => {
  grant({ id: 'cmd-test-4', scope: 'process-launch', boundary: ['node'], expiresAt: future(), grantedBy: 'test' });
  const result = await runCommand('git', ['status'], { confirm: async () => true });
  assert.equal(result.executed, false);
  revoke('cmd-test-4');
});
