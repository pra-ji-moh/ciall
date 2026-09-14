// fusion.test.mjs; upgrade 15 — validates multi-source data fusion and
// its access-controlled filtering. The property that matters most:
// when two sources DISAGREE on a field, fusion never silently picks a
// winner — it's reported as a real conflict. And filterForConsumer
// genuinely gates through the REAL accessControl.js tenant-isolated
// decision, not a bypassable check.

import test from 'node:test';
import assert from 'node:assert/strict';
import { fuseRecords, filterForConsumer } from '../src/lib/connectors/fusion.js';
import { createTenant, createUser, defineRole, assignRole } from '../src/lib/accessControl.js';

let counter = 0;
const uid = (prefix) => `${prefix}-${++counter}-${Date.now()}`;

test('fuseRecords rejects empty/malformed input', () => {
  assert.throws(() => fuseRecords([], (r) => r.id), /non-empty array/);
  assert.throws(() => fuseRecords([{ sourceId: 'a', records: [] }], null), /keyFn/);
  assert.throws(() => fuseRecords([{ sourceId: '', records: [] }], (r) => r.id), /non-empty string sourceId/);
});

test('fuseRecords merges two sources reporting the SAME entity with agreeing fields into one fused record', () => {
  const sourceA = { sourceId: 'db-a', records: [{ id: 'acme', reliability: 'high', country: 'US' }] };
  const sourceB = { sourceId: 'db-b', records: [{ id: 'acme', reliability: 'high', region: 'NA' }] };
  const fused = fuseRecords([sourceA, sourceB], (r) => r.id);

  assert.equal(fused.length, 1);
  assert.equal(fused[0].key, 'acme');
  assert.deepEqual(fused[0].sources.sort(), ['db-a', 'db-b']);
  assert.equal(fused[0].fields.reliability, 'high'); // agreed, merged
  assert.equal(fused[0].fields.country, 'US'); // only in A, carried through
  assert.equal(fused[0].fields.region, 'NA'); // only in B, carried through
  assert.deepEqual(fused[0].conflicts, []);
});

test('fuseRecords NEVER silently resolves a conflicting field -- it is excluded from `fields` and reported in `conflicts` instead', () => {
  const sourceA = { sourceId: 'db-a', records: [{ id: 'acme', riskRating: 'low' }] };
  const sourceB = { sourceId: 'db-b', records: [{ id: 'acme', riskRating: 'high' }] };
  const fused = fuseRecords([sourceA, sourceB], (r) => r.id);

  assert.equal(fused[0].fields.riskRating, undefined, 'a disagreed field must NOT appear in fields, silently picking a winner');
  assert.equal(fused[0].conflicts.length, 1);
  assert.equal(fused[0].conflicts[0].field, 'riskRating');
  assert.deepEqual(fused[0].conflicts[0].valuesBySource, { 'db-a': 'low', 'db-b': 'high' });
});

test('fuseRecords keeps entities that appear in only ONE source, with a single-element sources array', () => {
  const sourceA = { sourceId: 'db-a', records: [{ id: 'acme' }, { id: 'globex' }] };
  const sourceB = { sourceId: 'db-b', records: [{ id: 'acme' }] };
  const fused = fuseRecords([sourceA, sourceB], (r) => r.id);

  const globex = fused.find((f) => f.key === 'globex');
  assert.deepEqual(globex.sources, ['db-a']);
});

test('fuseRecords handles three or more sources, merging agreement and flagging conflicts across all of them', () => {
  const a = { sourceId: 'db-a', records: [{ id: 'x', status: 'active' }] };
  const b = { sourceId: 'db-b', records: [{ id: 'x', status: 'active' }] };
  const c = { sourceId: 'db-c', records: [{ id: 'x', status: 'inactive' }] };
  const fused = fuseRecords([a, b, c], (r) => r.id);
  assert.equal(fused[0].conflicts[0].field, 'status');
  assert.deepEqual(fused[0].conflicts[0].valuesBySource, { 'db-a': 'active', 'db-b': 'active', 'db-c': 'inactive' });
});

test('fuseRecords enforces the per-source record cap and source-count cap', () => {
  const many = new Array(5001).fill(0).map((_, i) => ({ id: `r${i}` }));
  assert.throws(() => fuseRecords([{ sourceId: 'a', records: many }], (r) => r.id), /exceeds the 5000-record cap/);

  const manySources = new Array(21).fill(0).map((_, i) => ({ sourceId: `s${i}`, records: [] }));
  assert.throws(() => fuseRecords(manySources, (r) => r.id), /exceeds the 20-source cap/);
});

// ---- filterForConsumer: real access-controlled gating -------------------

test('filterForConsumer returns fused records when the consumer has real "see" permission for the object type, within their own tenant', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantId);
  createUser(userId, { tenantId });
  defineRole(roleName, ['see:SupplierRanking']);
  assignRole(userId, roleName);

  const fused = fuseRecords([{ sourceId: 'a', records: [{ id: 'x', trust: 'high' }] }], (r) => r.id);
  const result = filterForConsumer(fused, 'SupplierRanking', { userId, resourceTenantId: tenantId });
  assert.equal(result.allowed, true);
  assert.deepEqual(result.records, fused);
});

test('filterForConsumer returns an EMPTY array (never partial data) when the consumer lacks "see" permission', () => {
  const tenantId = uid('tenant');
  const userId = uid('user');
  createTenant(tenantId);
  createUser(userId, { tenantId }); // no role assigned at all

  const fused = fuseRecords([{ sourceId: 'a', records: [{ id: 'x', secret: 'yes' }] }], (r) => r.id);
  const result = filterForConsumer(fused, 'SupplierRanking', { userId, resourceTenantId: tenantId });
  assert.equal(result.allowed, false);
  assert.deepEqual(result.records, []);
  assert.match(result.reason, /holds no role granting action/);
});

test('filterForConsumer respects TENANT ISOLATION: a "see" grant in one tenant never leaks fused data scoped to another tenant', () => {
  const tenantA = uid('tenant-a');
  const tenantB = uid('tenant-b');
  const userId = uid('user');
  const roleName = uid('role');
  createTenant(tenantA);
  createTenant(tenantB);
  createUser(userId, { tenantId: tenantA });
  defineRole(roleName, ['see:SupplierRanking']);
  assignRole(userId, roleName);

  const fused = fuseRecords([{ sourceId: 'a', records: [{ id: 'x' }] }], (r) => r.id);
  const resultOwnTenant = filterForConsumer(fused, 'SupplierRanking', { userId, resourceTenantId: tenantA });
  const resultOtherTenant = filterForConsumer(fused, 'SupplierRanking', { userId, resourceTenantId: tenantB });

  assert.equal(resultOwnTenant.allowed, true);
  assert.equal(resultOtherTenant.allowed, false);
  assert.deepEqual(resultOtherTenant.records, []);
});

// ---- end-to-end: two "databases" fused, filtered per-consumer -----------

test('end-to-end: two sources fused, then two different consumers see different results based on their real permissions', () => {
  const tenantId = uid('tenant');
  const analystId = uid('analyst');
  const outsiderId = uid('outsider');
  const roleName = uid('analyst-role');
  createTenant(tenantId);
  createUser(analystId, { tenantId });
  createUser(outsiderId, { tenantId });
  defineRole(roleName, ['see:SupplierRanking']);
  assignRole(analystId, roleName); // outsider gets no role at all

  const internalDb = { sourceId: 'internal-crm', records: [{ id: 'acme', tier: 'gold', lastAudit: '2026-01-01' }] };
  const externalDb = { sourceId: 'external-rating-agency', records: [{ id: 'acme', tier: 'gold', creditScore: 750 }] };
  const fused = fuseRecords([internalDb, externalDb], (r) => r.id);

  const analystView = filterForConsumer(fused, 'SupplierRanking', { userId: analystId, resourceTenantId: tenantId });
  const outsiderView = filterForConsumer(fused, 'SupplierRanking', { userId: outsiderId, resourceTenantId: tenantId });

  assert.equal(analystView.allowed, true);
  assert.equal(analystView.records[0].fields.creditScore, 750);
  assert.equal(analystView.records[0].fields.lastAudit, '2026-01-01');
  assert.equal(outsiderView.allowed, false);
  assert.deepEqual(outsiderView.records, []);
});
