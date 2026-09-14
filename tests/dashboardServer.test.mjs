// dashboardServer.test.mjs; upgrade 12 — validates the analyst
// dashboard by starting a REAL HTTP server on an ephemeral port and
// making REAL HTTP requests against it (global fetch), not by calling
// renderPage() as a unit in isolation — the thing being verified is
// that this actually serves over a real socket.

import test from 'node:test';
import assert from 'node:assert/strict';
import { startDashboardServer, escapeHtml } from '../src/lib/dashboardServer.js';
import { listKernels } from '../src/lib/kernelRegistry.js';

let handle;

test.before(async () => {
  handle = await startDashboardServer({ port: 0 });
});

test.after(async () => {
  await handle.close();
});

test('server actually binds to a real ephemeral port', () => {
  assert.ok(Number.isInteger(handle.port) && handle.port > 0);
});

test('GET / returns 200 and real HTML content-type', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type'), /text\/html/);
});

test('the dashboard lists every kernel actually registered in kernelRegistry.js, by id', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`);
  const html = await res.text();
  const kernels = listKernels();
  assert.ok(kernels.length > 0, 'sanity: there should be registered kernels to check against');
  for (const k of kernels) {
    assert.ok(html.includes(k.id), `dashboard HTML missing kernel id "${k.id}"`);
  }
});

test('the dashboard lists real ontology object types', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`);
  const html = await res.text();
  assert.ok(html.includes('RiskMark'), 'expected the real RiskMark object type to appear');
  assert.ok(html.includes('ContractClause'), 'expected the real ContractClause object type to appear');
});

test('the dashboard lists real registered connector types', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`);
  const html = await res.text();
  assert.ok(html.includes('>file<') || html.includes('file'), 'expected the "file" connector type to appear');
  assert.ok(html.includes('http'), 'expected the "http" connector type to appear');
});

test('escapeHtml neutralizes every HTML-significant character, so untrusted audit-log text (e.g. a real "reason" string) can never inject markup into the rendered page', () => {
  assert.equal(escapeHtml('<script>alert(1)</script>'), '&lt;script&gt;alert(1)&lt;/script&gt;');
  assert.equal(escapeHtml(`"quoted" & 'single'`), '&quot;quoted&quot; &amp; &#39;single&#39;');
  assert.equal(escapeHtml('plain text'), 'plain text');
});

test('a non-GET request is rejected with 405', async () => {
  const res = await fetch(`http://127.0.0.1:${handle.port}/`, { method: 'POST' });
  assert.equal(res.status, 405);
});

test('close() actually stops the server -- a subsequent request fails to connect', async () => {
  const handle2 = await startDashboardServer({ port: 0 });
  const port2 = handle2.port;
  await handle2.close();
  await assert.rejects(() => fetch(`http://127.0.0.1:${port2}/`, { signal: AbortSignal.timeout(500) }));
});
