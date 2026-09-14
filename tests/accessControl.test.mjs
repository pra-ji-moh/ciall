// accessControl.test.mjs; upgrade 12 — validates the RBAC/
// multi-tenancy layer, with particular focus on the property that
// actually matters for a governance claim: tenant isolation holds
// EVEN WHEN a user has a role that would otherwise grant the action,
// and even when the user is asking for that exact action against a
// different tenant's resource.

import test from 'node:test';
import assert from 'node:assert/strict';
import {
  createTenant, createUser, defineRole, assignRole, revokeRole,
  checkPermission, getAuditLog, listTenants, listUsers, getUserRoles,
} from '../src/lib/accessControl.js';

// Unique per-test ids (module-level singleton registries, same pattern
// as deviceGate.js's own module-level grants Map) so tests never
// collide with each other's tenants/users/roles.
let counter = 0;
const uid = (prefix) => `${prefix}-${++counter}-${Date.now()}`;

test('createTenant/createUser/defineRole/assignRole round-trip', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId, { name: 'Acme Corp' });
  createUser(userId, { tenantId, displayName: 'Alice' });
  defineRole(roleName, ['kernel:run', 'device:write']);
  assignRole(userId, roleName);

  assert.deepEqual(getUserRoles(userId), [roleName]);
  assert.ok(listTenants().some((t) => t.id === tenantId));
  assert.ok(listUsers(tenantId).some((u) => u.id === userId));
});

test('checkPermission grants an action the user\'s role covers, within their own tenant', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['kernel:run']);
  assignRole(userId, roleName);

  const result = checkPermission({ userId, action: 'kernel:run', resourceTenantId: tenantId });
  assert.equal(result.allowed, true);
});

test('checkPermission denies an action no held role grants', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['kernel:run']);
  assignRole(userId, roleName);

  const result = checkPermission({ userId, action: 'device:write', resourceTenantId: tenantId });
  assert.equal(result.allowed, false);
  assert.match(result.reason, /holds no role granting action/);
});

test('TENANT ISOLATION: a user with a role that grants the action is still denied against a DIFFERENT tenant\'s resource', () => {
  const tenantA = uid('tenant-a');
  const tenantB = uid('tenant-b');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantA);
  createTenant(tenantB);
  createUser(userId, { tenantId: tenantA });
  defineRole(roleName, ['kernel:run', 'device:write', 'connector:read']); // broad role, every permission granted
  assignRole(userId, roleName);

  // Same user, same broad role, same action -- only the resource's tenant differs.
  const withinTenant = checkPermission({ userId, action: 'kernel:run', resourceTenantId: tenantA });
  const crossTenant = checkPermission({ userId, action: 'kernel:run', resourceTenantId: tenantB });

  assert.equal(withinTenant.allowed, true);
  assert.equal(crossTenant.allowed, false);
  assert.match(crossTenant.reason, /cross-tenant access is never granted/);
});

test('TENANT ISOLATION holds across every permission the role grants, not just one sampled action', () => {
  const tenantA = uid('tenant-a');
  const tenantB = uid('tenant-b');
  const userId = uid('user');
  const roleName = uid('role');
  const permissions = ['kernel:run', 'device:write', 'device:read', 'connector:read', 'connector:write'];
  createTenant(tenantA);
  createTenant(tenantB);
  createUser(userId, { tenantId: tenantA });
  defineRole(roleName, permissions);
  assignRole(userId, roleName);

  for (const action of permissions) {
    const result = checkPermission({ userId, action, resourceTenantId: tenantB });
    assert.equal(result.allowed, false, `expected "${action}" to be denied cross-tenant, was allowed`);
  }
});

test('revokeRole actually removes the permission grant', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['kernel:run']);
  assignRole(userId, roleName);
  assert.equal(checkPermission({ userId, action: 'kernel:run', resourceTenantId: tenantId }).allowed, true);

  const revoked = revokeRole(userId, roleName);
  assert.equal(revoked, true);
  assert.equal(checkPermission({ userId, action: 'kernel:run', resourceTenantId: tenantId }).allowed, false);
});

test('revokeRole on a role the user never held returns false, does not throw', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['kernel:run']);
  assert.equal(revokeRole(userId, roleName), false);
});

test('checkPermission denies an unknown user rather than throwing', () => {
  const tenantId = uid('tenant');
  createTenant(tenantId);
  const result = checkPermission({ userId: 'never-created', action: 'kernel:run', resourceTenantId: tenantId });
  assert.equal(result.allowed, false);
  assert.match(result.reason, /unknown user/);
});

test('checkPermission denies an unknown resource tenant rather than throwing', () => {
  const userId = uid('user');
  const tenantId = uid('tenant');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  const result = checkPermission({ userId, action: 'kernel:run', resourceTenantId: 'never-created-tenant' });
  assert.equal(result.allowed, false);
  assert.match(result.reason, /unknown resource tenant/);
});

test('createTenant/createUser/defineRole reject duplicates', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, []);
  assert.throws(() => createTenant(tenantId), /Duplicate tenant/);
  assert.throws(() => createUser(userId, { tenantId }), /Duplicate user/);
  assert.throws(() => defineRole(roleName, []), /Duplicate role/);
});

test('createUser rejects an unknown tenantId', () => {
  assert.throws(() => createUser(uid('user'), { tenantId: 'nonexistent' }), /Unknown tenant/);
});

test('assignRole rejects an unknown user or unknown role', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, []);
  assert.throws(() => assignRole('nonexistent-user', roleName), /Unknown user/);
  assert.throws(() => assignRole(userId, 'nonexistent-role'), /Unknown role/);
});

test('every permission check, allowed or denied, is recorded in the audit log', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['kernel:run']);
  assignRole(userId, roleName);

  const before = getAuditLog().length;
  checkPermission({ userId, action: 'kernel:run', resourceTenantId: tenantId }); // allowed
  checkPermission({ userId, action: 'device:write', resourceTenantId: tenantId }); // denied
  const after = getAuditLog();
  assert.equal(after.length, before + 2);
  assert.equal(after[after.length - 2].allowed, true);
  assert.equal(after[after.length - 1].allowed, false);
});

test('getAuditLog returns a copy -- mutating it does not affect the real log', () => {
  const log = getAuditLog();
  const originalLength = log.length;
  log.push({ fake: 'entry' });
  assert.equal(getAuditLog().length, originalLength);
});
