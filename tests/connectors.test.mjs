// connectors.test.mjs; upgrade 12 — validates the data-integration
// layer against REAL local files (actual node:fs reads, no mocked I/O)
// and a REAL injected fetch (no live network calls, matching this
// repo's existing modelClient.test.mjs/geminiClient.test.mjs
// discipline for anything network-shaped).

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { createFileConnector } from '../src/lib/connectors/fileConnector.js';
import { createHttpConnector } from '../src/lib/connectors/httpConnector.js';
import { createConnector, listConnectorTypes, ingestToObjectType } from '../src/lib/connectors/index.js';
import { registerConnectorType } from '../src/lib/connectors/connector.js';

async function withTempFile(content, ext, fn) {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'ciall-connector-test-'));
  const filePath = path.join(dir, `data${ext}`);
  await fs.writeFile(filePath, content, 'utf8');
  try {
    await fn(filePath);
  } finally {
    await fs.rm(dir, { recursive: true, force: true });
  }
}

// ---- fileConnector ------------------------------------------------

test('fileConnector reads a real CSV file from disk and parses records', async () => {
  await withTempFile('quantity,value,uncertainty,source\nVaR,12.5,0.3,desk-a\nVaR,14.1,0.4,desk-b\n', '.csv', async (filePath) => {
    const connector = createFileConnector({ filePath });
    const records = await connector.fetch();
    assert.equal(records.length, 2);
    assert.equal(records[0].quantity, 'VaR');
    assert.equal(records[0].value, '12.5'); // CSV values are strings; mapper's job to coerce
    assert.equal(records[1].source, 'desk-b');
  });
});

test('fileConnector handles quoted fields with embedded commas and escaped quotes', async () => {
  await withTempFile('name,note\n"Acme, Inc.","said ""hello"""\n', '.csv', async (filePath) => {
    const connector = createFileConnector({ filePath });
    const records = await connector.fetch();
    assert.equal(records[0].name, 'Acme, Inc.');
    assert.equal(records[0].note, 'said "hello"');
  });
});

test('fileConnector reads a real JSON array file', async () => {
  await withTempFile(JSON.stringify([{ quantity: 'VaR', value: 12.5 }]), '.json', async (filePath) => {
    const connector = createFileConnector({ filePath });
    const records = await connector.fetch();
    assert.deepEqual(records, [{ quantity: 'VaR', value: 12.5 }]);
  });
});

test('fileConnector rejects a JSON file whose top level is not an array', async () => {
  await withTempFile(JSON.stringify({ not: 'an array' }), '.json', async (filePath) => {
    const connector = createFileConnector({ filePath });
    await assert.rejects(() => connector.fetch(), /must contain a JSON array/);
  });
});

test('fileConnector throws on a nonexistent file rather than returning empty', async () => {
  const connector = createFileConnector({ filePath: path.join(os.tmpdir(), 'definitely-does-not-exist-ciall.csv') });
  await assert.rejects(() => connector.fetch());
});

test('fileConnector requires a filePath', () => {
  assert.throws(() => createFileConnector({}), /needs a filePath/);
});

// ---- httpConnector --------------------------------------------------

test('httpConnector requires an explicit fetchImpl -- no implicit network access', () => {
  assert.throws(() => createHttpConnector({ url: 'https://example.test/api' }), /requires an explicit fetchImpl/);
});

test('httpConnector calls the injected fetchImpl and parses a plain JSON array response, never touching the real network', async () => {
  let calledWith = null;
  const fakeFetch = async (url, opts) => {
    calledWith = { url, opts };
    return { ok: true, status: 200, text: async () => JSON.stringify([{ id: 1 }, { id: 2 }]) };
  };
  const connector = createHttpConnector({ url: 'https://example.test/api', fetchImpl: fakeFetch, headers: { Authorization: 'Bearer x' } });
  const records = await connector.fetch();
  assert.deepEqual(records, [{ id: 1 }, { id: 2 }]);
  assert.equal(calledWith.url, 'https://example.test/api');
  assert.deepEqual(calledWith.opts.headers, { Authorization: 'Bearer x' });
});

test('httpConnector resolves a nested records path', async () => {
  const fakeFetch = async () => ({ ok: true, status: 200, text: async () => JSON.stringify({ data: { items: [{ id: 1 }] } }) });
  const connector = createHttpConnector({ url: 'https://example.test/api', fetchImpl: fakeFetch, recordsPath: ['data', 'items'] });
  const records = await connector.fetch();
  assert.deepEqual(records, [{ id: 1 }]);
});

test('httpConnector throws on a non-ok response status', async () => {
  const fakeFetch = async () => ({ ok: false, status: 503, text: async () => '' });
  const connector = createHttpConnector({ url: 'https://example.test/api', fetchImpl: fakeFetch });
  await assert.rejects(() => connector.fetch(), /status 503/);
});

test('httpConnector throws when the resolved recordsPath is missing or not an array', async () => {
  const fakeFetch = async () => ({ ok: true, status: 200, text: async () => JSON.stringify({ data: {} }) });
  const connector = createHttpConnector({ url: 'https://example.test/api', fetchImpl: fakeFetch, recordsPath: ['data', 'items'] });
  await assert.rejects(() => connector.fetch(), /recordsPath key "items" not found/);
});

// ---- registry ---------------------------------------------------------

test('the built-in file/http connector types are registered', () => {
  const types = listConnectorTypes();
  assert.ok(types.includes('file'));
  assert.ok(types.includes('http'));
});

test('createConnector dispatches by id and throws on an unknown id', () => {
  const fakeFetch = async () => ({ ok: true, status: 200, text: async () => '[]' });
  const c = createConnector('http', { url: 'https://example.test', fetchImpl: fakeFetch });
  assert.equal(typeof c.fetch, 'function');
  assert.throws(() => createConnector('carrier-pigeon', {}), /Unknown connector type/);
});

test('registerConnectorType rejects a duplicate id', () => {
  assert.throws(() => registerConnectorType('file', () => ({ fetch: async () => [] })), /Duplicate connector type/);
});

test('registerConnectorType rejects a factory that does not produce a fetch() method', () => {
  registerConnectorType('broken-test-connector', () => ({}));
  assert.throws(() => createConnector('broken-test-connector', {}), /did not produce an object with a fetch/);
});

// ---- ingestToObjectType: real ontology validation ----------------------

test('ingestToObjectType maps and validates raw CSV records into real RiskMark ontology instances', async () => {
  await withTempFile('quantity,value,uncertainty,source\nVaR,12.5,0.3,desk-a\n', '.csv', async (filePath) => {
    const connector = createFileConnector({ filePath });
    const raw = await connector.fetch();
    const instances = ingestToObjectType(raw, 'RiskMark', (r) => ({
      quantity: r.quantity, value: Number(r.value), uncertainty: Number(r.uncertainty), source: r.source,
    }));
    assert.equal(instances.length, 1);
    assert.equal(instances[0].objectType, 'RiskMark');
    assert.equal(instances[0].quantity, 'VaR');
    assert.equal(instances[0].value, 12.5);
  });
});

test('ingestToObjectType throws a specific error on the first record that fails the object type\'s declared schema', () => {
  const raw = [{ quantity: 'VaR', value: 'not-a-number', uncertainty: 0.1, source: 'x' }];
  assert.throws(
    () => ingestToObjectType(raw, 'RiskMark', (r) => r),
    /record 0 property "value" does not match declared type "number"/,
  );
});

test('ingestToObjectType respects optional properties (assumes: "string[]?")', () => {
  const raw = [{ quantity: 'VaR', value: 1, uncertainty: 0.1, source: 'x' }]; // no `assumes` at all
  const instances = ingestToObjectType(raw, 'RiskMark', (r) => r);
  assert.equal(instances.length, 1);
});

test('ingestToObjectType throws on an unknown object type name', () => {
  assert.throws(() => ingestToObjectType([], 'NotARealType', (r) => r), /Unknown object type/);
});

test('ingestToObjectType surfaces a mapper exception with the failing record index', () => {
  const raw = [{}];
  assert.throws(
    () => ingestToObjectType(raw, 'RiskMark', () => { throw new Error('boom'); }),
    /mapper threw on record 0: boom/,
  );
});

test('ingestToObjectType rejects more than MAX_RECORDS in one call', () => {
  const raw = new Array(10001).fill({});
  assert.throws(() => ingestToObjectType(raw, 'RiskMark', (r) => r), /exceeds the 10000-record cap/);
});

// ---- end-to-end: file -> ingest -> real consistencyKernel verification --

test('end-to-end: a CSV of two conflicting risk marks, ingested through the real connector layer, is caught by the UNMODIFIED measurementTension/ontology relationship path', async () => {
  const { tensionWith } = await import('../src/ontology/relationships.js');
  await withTempFile(
    'quantity,value,uncertainty,source\nVaR,12.5,0.3,internal-desk\nVaR,20.0,0.3,counterparty\n',
    '.csv',
    async (filePath) => {
      const connector = createFileConnector({ filePath });
      const raw = await connector.fetch();
      const marks = ingestToObjectType(raw, 'RiskMark', (r) => ({
        quantity: r.quantity, value: Number(r.value), uncertainty: Number(r.uncertainty), source: r.source,
      }));
      const result = tensionWith(
        { quantity: marks[0].quantity, value: marks[0].value, uncertainty: marks[0].uncertainty, source: marks[0].source },
        { quantity: marks[1].quantity, value: marks[1].value, uncertainty: marks[1].uncertainty, source: marks[1].source },
      );
      assert.ok(result.sigma > 5, `expected a decisive tension, got sigma=${result.sigma}`);
    },
  );
});
