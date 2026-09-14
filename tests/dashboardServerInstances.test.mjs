// dashboardServerInstances.test.mjs; upgrade 13 — validates the
// dashboard's new live `/instances?objectType=` query route against a
// REAL temp instanceStoreRoot seeded with REAL saveInstances() data
// (not a mock), served over an actual HTTP socket.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { startDashboardServer } from '../src/lib/dashboardServer.js';
import { saveInstances } from '../src/ontology/instanceStore.js';

let handle;
let root;

test.before(async () => {
  root = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-dashboard-instances-test-'));
  saveInstances(root, 'RiskMark', [
    { objectType: 'RiskMark', quantity: 'VaR', value: 10, uncertainty: 0.3, source: 'internal-desk' },
    { objectType: 'RiskMark', quantity: 'VaR', value: 20, uncertainty: 0.3, source: 'counterparty' },
  ]);
  handle = await startDashboardServer({ port: 0, instanceStoreRoot: root });
});

test.after(async () => {
  await handle.close();
});

test('the overview page links to the live query view for an object type with stored instances', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`);
  const html = await res.text();
  assert.ok(html.includes('/instances?objectType=RiskMark'), 'expected a query link for RiskMark, which has stored instances');
});

test('the overview page does NOT link to a query view for an object type with no stored instances', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`);
  const html = await res.text();
  assert.ok(!html.includes('/instances?objectType=ContractClause'), 'ContractClause has no stored instances in this test\'s temp root');
});

test('GET /instances?objectType=RiskMark renders the real stored instances', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/instances?objectType=RiskMark`);
  assert.equal(res.status, 200);
  const html = await res.text();
  assert.ok(html.includes('internal-desk'));
  assert.ok(html.includes('counterparty'));
  assert.ok(html.includes('Stored instances (2)'));
});

test('GET /instances?objectType=RiskMark never renders a literal "undefined" for the omitted optional `assumes` property', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/instances?objectType=RiskMark`);
  const html = await res.text();
  assert.ok(!html.includes('>undefined<'), 'an omitted optional property must render as a placeholder, not the literal string "undefined"');
  assert.ok(html.includes('(none)'));
});

test('GET /instances?objectType=RiskMark renders the real, self-wired TensionWith relationship', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/instances?objectType=RiskMark`);
  const html = await res.text();
  assert.ok(html.includes('TensionWith'));
  assert.ok(html.includes('contradiction')); // the seeded values are >=5 sigma apart
});

test('GET /instances?objectType=ContractClause (a registered type with zero stored instances) renders cleanly, not an error', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/instances?objectType=ContractClause`);
  assert.equal(res.status, 200);
  const html = await res.text();
  assert.ok(html.includes('Stored instances (0)'));
  assert.ok(html.includes('none stored yet'));
});

test('GET /instances with no objectType param returns 400', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/instances`);
  assert.equal(res.status, 400);
});

test('GET /instances?objectType=NotARealType returns 404, not a 500 crash', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/instances?objectType=NotARealType`);
  assert.equal(res.status, 404);
});

test('an unknown route returns 404 with a link back to the overview', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/nonexistent-route`);
  assert.equal(res.status, 404);
  const html = await res.text();
  assert.ok(html.includes('back to overview'));
});
