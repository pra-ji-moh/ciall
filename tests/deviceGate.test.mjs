// deviceGate.test.mjs; Phase 3 coverage. Every test here confirms the gate
// DENIES by default and only approves within an explicit, scoped,
// expiring grant — and that nothing in this file can actually touch the
// filesystem or OS (there is no such code to call).

import test from 'node:test';
import assert from 'node:assert/strict';

import { grant, revoke, requestAction, getAuditLog, listActiveGrants } from '../src/lib/deviceGate.js';

const future = () => Date.now() + 60_000;

test('an action with no grant at all is denied', () => {
  const decision = requestAction({ scope: 'filesystem-write', target: '/home/user/project/file.txt', requestedBy: 'test' });
  assert.equal(decision.approved, false);
  assert.match(decision.reason, /no active grant/);
});

test('grant() rejects a grant with no expiry', () => {
  assert.throws(() => grant({ id: 'g1', scope: 'filesystem-write', boundary: '/tmp/x', grantedBy: 'test' }), /future expiresAt/);
});

test('grant() rejects an unknown scope', () => {
  assert.throws(() => grant({ id: 'g2', scope: 'root-access', boundary: '/', expiresAt: future(), grantedBy: 'test' }), /Unknown grant scope/);
});

test('a target inside the granted directory is approved; a target outside it is not', () => {
  grant({ id: 'proj-write', scope: 'filesystem-write', boundary: '/home/user/project', expiresAt: future(), grantedBy: 'test' });

  const inside = requestAction({ scope: 'filesystem-write', target: '/home/user/project/sub/file.txt', requestedBy: 'test' });
  assert.equal(inside.approved, true);
  assert.equal(inside.grantId, 'proj-write');

  const outside = requestAction({ scope: 'filesystem-write', target: '/home/user/other-project/file.txt', requestedBy: 'test' });
  assert.equal(outside.approved, false);

  revoke('proj-write');
});

test('a sibling directory that merely shares a prefix is NOT treated as inside the boundary', () => {
  grant({ id: 'proj2', scope: 'filesystem-write', boundary: '/home/user/project', expiresAt: future(), grantedBy: 'test' });
  const decision = requestAction({ scope: 'filesystem-write', target: '/home/user/project-evil/file.txt', requestedBy: 'test' });
  assert.equal(decision.approved, false);
  revoke('proj2');
});

test('an expired grant no longer approves anything', async () => {
  grant({ id: 'short-lived', scope: 'filesystem-write', boundary: '/home/user/project', expiresAt: Date.now() + 20, grantedBy: 'test' });
  await new Promise((r) => setTimeout(r, 40));
  const decision = requestAction({ scope: 'filesystem-write', target: '/home/user/project/file.txt', requestedBy: 'test' });
  assert.equal(decision.approved, false);
  revoke('short-lived');
});

test('revoke() immediately removes an active grant', () => {
  grant({ id: 'to-revoke', scope: 'process-launch', boundary: ['node'], expiresAt: future(), grantedBy: 'test' });
  assert.equal(requestAction({ scope: 'process-launch', target: 'node', requestedBy: 'test' }).approved, true);
  revoke('to-revoke');
  assert.equal(requestAction({ scope: 'process-launch', target: 'node', requestedBy: 'test' }).approved, false);
});

test('process-launch and network grants only approve exact allowlisted targets', () => {
  grant({ id: 'proc', scope: 'process-launch', boundary: ['git', 'node'], expiresAt: future(), grantedBy: 'test' });
  assert.equal(requestAction({ scope: 'process-launch', target: 'git', requestedBy: 'test' }).approved, true);
  assert.equal(requestAction({ scope: 'process-launch', target: 'powershell', requestedBy: 'test' }).approved, false);
  revoke('proc');
});

test('every request, approved or denied, is recorded in the audit log', () => {
  const before = getAuditLog().length;
  grant({ id: 'audit-test', scope: 'filesystem-read', boundary: '/tmp', expiresAt: future(), grantedBy: 'test' });
  requestAction({ scope: 'filesystem-read', target: '/tmp/ok.txt', requestedBy: 'test' });
  requestAction({ scope: 'filesystem-read', target: '/etc/passwd', requestedBy: 'test' });
  const after = getAuditLog();
  assert.equal(after.length, before + 3); // grant-created + 2 action-requested
  revoke('audit-test');
});

test('listActiveGrants never includes an expired grant', async () => {
  grant({ id: 'expiring', scope: 'network', boundary: ['example.com'], expiresAt: Date.now() + 20, grantedBy: 'test' });
  assert.ok(listActiveGrants().some((g) => g.id === 'expiring'));
  await new Promise((r) => setTimeout(r, 40));
  assert.ok(!listActiveGrants().some((g) => g.id === 'expiring'));
});

test('this module exports no execution function of any kind', async () => {
  const mod = await import('../src/lib/deviceGate.js');
  const names = Object.keys(mod);
  for (const n of names) {
    assert.ok(!/write|exec|spawn|launch|fetch|delete|remove/i.test(n), `unexpected execution-shaped export: ${n}`);
  }
});
