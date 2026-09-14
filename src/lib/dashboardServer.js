// dashboardServer.js; upgrade 12 — the missing analyst-facing tooling
// named against Palantir Foundry's Workshop (dashboard builder) and
// Code Workbook (notebook environment): a real, working, servable HTML
// dashboard showing what this substrate actually has registered and
// has actually done, over plain node:http — zero npm dependencies,
// consistent with this repo's standing rule.
//
// SCOPE, disclosed plainly: this is a read-only overview page plus one
// per-object-type live QUERY view (upgrade 13, `/instances?objectType=`
// — real stored instances and their self-wired relationships from
// `ontology/instanceStore.js`), not a dashboard BUILDER (an end user
// cannot compose their own layouts or widgets here), not a notebook
// environment, and not backed by any real enterprise data estate — see
// connectors/ for the honest state of that. Everything rendered is
// server-computed fresh on every request, never a cached snapshot.

import http from 'node:http';
import { listKernels } from './kernelRegistry.js';
import { listObjectTypes, getObjectType } from '../ontology/objectTypes.js';
import { listConnectorTypes } from './connectors/index.js';
import { readExecutionAudit, getExecutionAuditPath } from './deviceExecutor.js';
import { defaultInstanceStoreRoot, listStoredObjectTypes, loadInstances, loadRelationships } from '../ontology/instanceStore.js';

export function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c]));
}

function pageChrome(title, body) {
  return `<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>ciall-substrate — ${escapeHtml(title)}</title>
<style>
  body { font-family: -apple-system, Segoe UI, Arial, sans-serif; margin: 2rem; color: #1a1a1a; background: #fafafa; }
  h1 { font-size: 1.4rem; margin-bottom: 0.25rem; }
  .subtitle { color: #666; margin-top: 0; margin-bottom: 1.5rem; }
  section { margin-bottom: 2rem; }
  h2 { font-size: 1.05rem; border-bottom: 1px solid #ddd; padding-bottom: 0.3rem; }
  table { border-collapse: collapse; width: 100%; background: #fff; }
  th, td { text-align: left; padding: 0.4rem 0.6rem; border-bottom: 1px solid #eee; font-size: 0.9rem; }
  th { background: #f0f0f0; }
  code { background: #f0f0f0; padding: 0.1rem 0.3rem; border-radius: 3px; }
  .tag { display: inline-block; background: #e8eefc; color: #1a4dab; border-radius: 3px; padding: 0.05rem 0.4rem; font-size: 0.75rem; margin-right: 0.2rem; }
  .badge { display: inline-block; border-radius: 3px; padding: 0.05rem 0.4rem; font-size: 0.75rem; }
  .badge-ok { background: #e6f4ea; color: #1e7e34; }
  .badge-warn { background: #fff3e0; color: #b06000; }
  .empty { color: #999; font-style: italic; }
  footer { color: #999; font-size: 0.8rem; margin-top: 2rem; }
  a { color: #1a4dab; }
</style>
</head>
<body>
${body}
<footer>Generated fresh on every request. Not a cached snapshot.</footer>
</body>
</html>`;
}

function renderInstancesPage(objectTypeName, root) {
  const objectType = getObjectType(objectTypeName); // throws on unknown type; caller (route handler) converts to a 404
  const instances = loadInstances(root, objectTypeName);
  const relationships = loadRelationships(root, objectTypeName);

  const propNames = Object.keys(objectType.properties);
  const renderCell = (v) => (v === undefined ? '<span class="empty">(none)</span>' : escapeHtml(JSON.stringify(v)));
  const instanceRows = instances.map((inst) => `
    <tr>
      <td><code>${inst._id}</code></td>
      ${propNames.map((p) => `<td>${renderCell(inst[p])}</td>`).join('')}
      <td>${escapeHtml(new Date(inst._storedAt).toISOString())}</td>
    </tr>`).join('');

  const relRows = relationships.map((r) => `
    <tr><td><span class="tag">${escapeHtml(r.relationship)}</span></td><td><code>${escapeHtml(JSON.stringify(r))}</code></td></tr>`).join('');

  return pageChrome(`instances: ${objectTypeName}`, `
<h1>${escapeHtml(objectTypeName)} — live instance query</h1>
<p class="subtitle">${escapeHtml(objectType.description)} &middot; <a href="/">&larr; back to overview</a></p>

<section>
  <h2>Stored instances (${instances.length})</h2>
  <table><thead><tr><th>_id</th>${propNames.map((p) => `<th>${escapeHtml(p)}</th>`).join('')}<th>stored at</th></tr></thead>
  <tbody>${instanceRows || `<tr><td colspan="${propNames.length + 2}" class="empty">none stored yet</td></tr>`}</tbody></table>
</section>

<section>
  <h2>Computed relationships (${relationships.length})</h2>
  ${objectType.relationships.length === 0
    ? '<p class="empty">this object type declares no relationship kinds</p>'
    : `<table><thead><tr><th>kind</th><th>detail</th></tr></thead><tbody>${relRows || `<tr><td colspan="2" class="empty">none found among stored instances</td></tr>`}</tbody></table>`}
</section>`);
}

function renderPage(root) {
  const kernels = listKernels();
  const objectTypes = listObjectTypes();
  const connectorTypes = listConnectorTypes();
  const auditPath = getExecutionAuditPath();
  const auditEntries = readExecutionAudit().slice(-25).reverse();
  const storedTypes = listStoredObjectTypes(root);

  const kernelRows = kernels.map((k) => `
    <tr>
      <td><code>${escapeHtml(k.id)}</code></td>
      <td>${escapeHtml(k.label)}</td>
      <td>${(k.domain || []).map((d) => `<span class="tag">${escapeHtml(d)}</span>`).join(' ')}</td>
      <td>${k.deterministic ? '<span class="badge badge-ok">deterministic</span>' : '<span class="badge badge-warn">seeded-stochastic</span>'}</td>
    </tr>`).join('');

  const objectTypeRows = objectTypes.map((t) => `
    <tr>
      <td><code>${escapeHtml(t.name)}</code></td>
      <td>${escapeHtml(t.domain)}</td>
      <td>${escapeHtml(t.description)}</td>
      <td><code>${escapeHtml(t.verifiedBy)}</code></td>
      <td>${storedTypes.includes(t.name) ? `<a href="/instances?objectType=${encodeURIComponent(t.name)}">query &rarr;</a>` : '<span class="empty">no stored instances</span>'}</td>
    </tr>`).join('');

  const auditRows = auditEntries.map((e) => `
    <tr>
      <td>${escapeHtml(new Date(e.at).toISOString())}</td>
      <td>${escapeHtml(e.op || '')}</td>
      <td>${e.executed ? '<span class="badge badge-ok">executed</span>' : '<span class="badge badge-warn">not executed</span>'}</td>
      <td>${escapeHtml(e.target || e.executable || '')}</td>
      <td>${escapeHtml(e.reason || '')}</td>
    </tr>`).join('');

  return pageChrome('analyst dashboard', `
<h1>ciall-substrate — analyst dashboard</h1>
<p class="subtitle">Read-only, server-rendered live view. Not a dashboard builder; see this file's header for scope.</p>

<section>
  <h2>Registered kernels (${kernels.length})</h2>
  <table><thead><tr><th>id</th><th>label</th><th>domain</th><th>kind</th></tr></thead>
  <tbody>${kernelRows || '<tr><td colspan="4" class="empty">none registered</td></tr>'}</tbody></table>
</section>

<section>
  <h2>Ontology object types (${objectTypes.length}) — live query</h2>
  <table><thead><tr><th>name</th><th>domain</th><th>description</th><th>verified by</th><th>data</th></tr></thead>
  <tbody>${objectTypeRows || '<tr><td colspan="5" class="empty">none registered</td></tr>'}</tbody></table>
</section>

<section>
  <h2>Data connector types (${connectorTypes.length})</h2>
  <p>${connectorTypes.map((c) => `<code>${escapeHtml(c)}</code>`).join(', ') || '<span class="empty">none registered</span>'}</p>
</section>

<section>
  <h2>Recent execution audit (last ${auditEntries.length})</h2>
  <p style="font-size:0.8rem;color:#999;">Log file: <code>${escapeHtml(auditPath)}</code></p>
  <table><thead><tr><th>at</th><th>op</th><th>status</th><th>target</th><th>reason</th></tr></thead>
  <tbody>${auditRows || '<tr><td colspan="5" class="empty">no executions recorded yet</td></tr>'}</tbody></table>
</section>`);
}

/**
 * Starts a plain HTTP dashboard server. `port: 0` (the default) picks
 * an ephemeral free port, same convention as node:net's own API —
 * useful for tests, which never want to depend on a fixed port being
 * free. Returns `{server, port, close()}`; `close()` returns a Promise
 * resolving once the server has actually stopped, same "close() is
 * awaitable, never fire-and-forget" discipline as
 * rpc/kernelServiceServer.js's `close`.
 */
export function startDashboardServer({ port = 0, instanceStoreRoot } = {}) {
  const root = instanceStoreRoot || defaultInstanceStoreRoot();
  const server = http.createServer((req, res) => {
    if (req.method !== 'GET') {
      res.writeHead(405, { 'Content-Type': 'text/plain' });
      res.end('Method Not Allowed');
      return;
    }
    const url = new URL(req.url, 'http://localhost');
    let body;
    let status = 200;
    try {
      if (url.pathname === '/') {
        body = renderPage(root);
      } else if (url.pathname === '/instances') {
        const objectTypeName = url.searchParams.get('objectType');
        if (!objectTypeName) {
          status = 400;
          body = pageChrome('missing objectType', '<p>The <code>objectType</code> query parameter is required, e.g. <code>/instances?objectType=RiskMark</code>.</p>');
        } else {
          body = renderInstancesPage(objectTypeName, root);
        }
      } else {
        status = 404;
        body = pageChrome('not found', `<p>No route for <code>${escapeHtml(url.pathname)}</code>. <a href="/">&larr; back to overview</a></p>`);
      }
    } catch (e) {
      status = e.message && e.message.startsWith('Unknown object type') ? 404 : 500;
      body = pageChrome('error', `<p>${escapeHtml(e.message)}</p><p><a href="/">&larr; back to overview</a></p>`);
    }
    res.writeHead(status, { 'Content-Type': 'text/html; charset=utf-8' });
    res.end(body);
  });

  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, () => {
      const boundPort = server.address().port;
      resolve({
        server,
        port: boundPort,
        close: () => new Promise((res) => server.close(() => res())),
      });
    });
  });
}
