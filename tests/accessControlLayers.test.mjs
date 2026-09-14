// accessControlLayers.test.mjs; upgrade 13 — validates the four
// explicitly-named permission layers (see/query/do/who's-allowed)
// added over accessControl.js's existing tenant-isolated
// checkPermission core. The property that matters most: the three
// layers are genuinely INDEPENDENT grants -- holding one never implies
// another, and tenant isolation holds across all three identically.

import test from 'node:test';
import assert from 'node:assert/strict';
import {
  createTenant, createUser, defineRole, defineLayeredRole, assignRole,
  checkVisibility, checkQuery, checkAction,
} from '../src/lib/accessControl.js';

let counter = 0;
const uid = (prefix) => `${prefix}-${++counter}-${Date.now()}`;

test('checkVisibility grants "see" for an object type the role names, within the user\'s own tenant', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['see:RiskMark']);
  assignRole(userId, roleName);

  const result = checkVisibility({ userId, objectTypeName: 'RiskMark', resourceTenantId: tenantId });
  assert.equal(result.allowed, true);
});

test('checkQuery grants "query" for a kernel the role names', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['query:mcmc']);
  assignRole(userId, roleName);

  const result = checkQuery({ userId, kernelId: 'mcmc', resourceTenantId: tenantId });
  assert.equal(result.allowed, true);
});

test('checkAction grants "do" for an action the role names', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['do:device-write']);
  assignRole(userId, roleName);

  const result = checkAction({ userId, actionName: 'device-write', resourceTenantId: tenantId });
  assert.equal(result.allowed, true);
});

test('the three layers are INDEPENDENT: a role granting "see" for a type grants NEITHER query NOR do for that same type/kernel/action name', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['see:RiskMark']); // ONLY see, nothing else
  assignRole(userId, roleName);

  assert.equal(checkVisibility({ userId, objectTypeName: 'RiskMark', resourceTenantId: tenantId }).allowed, true);
  assert.equal(checkQuery({ userId, kernelId: 'RiskMark', resourceTenantId: tenantId }).allowed, false);
  assert.equal(checkAction({ userId, actionName: 'RiskMark', resourceTenantId: tenantId }).allowed, false);
});

test('a real analyst-shaped scenario: a role that can SEE and QUERY RiskMark but cannot DO anything to it', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineLayeredRole(roleName, { see: ['RiskMark'], query: ['mcmc', 'consistency'] }); // no `do` at all
  assignRole(userId, roleName);

  assert.equal(checkVisibility({ userId, objectTypeName: 'RiskMark', resourceTenantId: tenantId }).allowed, true);
  assert.equal(checkQuery({ userId, kernelId: 'mcmc', resourceTenantId: tenantId }).allowed, true);
  assert.equal(checkQuery({ userId, kernelId: 'consistency', resourceTenantId: tenantId }).allowed, true);
  assert.equal(checkAction({ userId, actionName: 'device-write', resourceTenantId: tenantId }).allowed, false);
});

test('defineLayeredRole with all three layers grants exactly those, nothing more', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineLayeredRole(roleName, { see: ['ContractClause'], query: ['consistency'], do: ['device-write'] });
  assignRole(userId, roleName);

  assert.equal(checkVisibility({ userId, objectTypeName: 'ContractClause', resourceTenantId: tenantId }).allowed, true);
  assert.equal(checkQuery({ userId, kernelId: 'consistency', resourceTenantId: tenantId }).allowed, true);
  assert.equal(checkAction({ userId, actionName: 'device-write', resourceTenantId: tenantId }).allowed, true);
  // Not granted at all -- proves this isn't a blanket "role exists -> everything allowed" bug
  assert.equal(checkQuery({ userId, kernelId: 'mcmc', resourceTenantId: tenantId }).allowed, false);
});

test('TENANT ISOLATION holds identically across all three layers', () => {
  const tenantA = uid('tenant-a');
  const tenantB = uid('tenant-b');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantA);
  createTenant(tenantB);
  createUser(userId, { tenantId: tenantA });
  defineLayeredRole(roleName, { see: ['RiskMark'], query: ['mcmc'], do: ['device-write'] });
  assignRole(userId, roleName);

  assert.equal(checkVisibility({ userId, objectTypeName: 'RiskMark', resourceTenantId: tenantB }).allowed, false);
  assert.equal(checkQuery({ userId, kernelId: 'mcmc', resourceTenantId: tenantB }).allowed, false);
  assert.equal(checkAction({ userId, actionName: 'device-write', resourceTenantId: tenantB }).allowed, false);
  // Sanity: the SAME calls against the user's own tenant succeed, proving
  // the denial above is genuinely about tenant, not a broken grant.
  assert.equal(checkVisibility({ userId, objectTypeName: 'RiskMark', resourceTenantId: tenantA }).allowed, true);
  assert.equal(checkQuery({ userId, kernelId: 'mcmc', resourceTenantId: tenantA }).allowed, true);
  assert.equal(checkAction({ userId, actionName: 'device-write', resourceTenantId: tenantA }).allowed, true);
});
