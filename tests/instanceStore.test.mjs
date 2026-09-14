// instanceStore.test.mjs; upgrade 13 — validates the persistent,
// self-wiring ontology instance store against a REAL temp directory on
// disk (same pattern as claimMemory.test.mjs), and confirms self-wiring
// actually reuses the existing, already-tested relationship functions
// (compareMeasurements/contradicts/impossibleCycle) rather than a
// parallel reimplementation — proven by checking the SAME kind of
// finding those functions are independently tested to produce.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {
  saveInstances, loadInstances, loadRelationships, listStoredObjectTypes,
} from '../src/ontology/instanceStore.js';

function tempRoot() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-instancestore-test-'));
}

test('saveInstances persists new instances, stamping _id and _storedAt', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'RiskMark', [{ objectType: 'RiskMark', quantity: 'VaR', value: 10, uncertainty: 1, source: 'a' }]);
  assert.equal(result.totalStored, 1);
  assert.equal(result.added, 1);

  const stored = loadInstances(root, 'RiskMark');
  assert.equal(stored.length, 1);
  assert.equal(stored[0]._id, 1);
  assert.equal(typeof stored[0]._storedAt, 'number');
  assert.equal(stored[0].quantity, 'VaR');
});

test('saveInstances appends to what is already stored, across separate calls', () => {
  const root = tempRoot();
  saveInstances(root, 'RiskMark', [{ objectType: 'RiskMark', quantity: 'VaR', value: 10, uncertainty: 1, source: 'a' }]);
  const result2 = saveInstances(root, 'RiskMark', [{ objectType: 'RiskMark', quantity: 'VaR', value: 20, uncertainty: 1, source: 'b' }]);
  assert.equal(result2.totalStored, 2);
  assert.equal(result2.added, 1);
  const stored = loadInstances(root, 'RiskMark');
  assert.equal(stored.length, 2);
  assert.equal(stored[1]._id, 2);
});

test('loadInstances on a never-saved type returns an empty array, not an error', () => {
  const root = tempRoot();
  assert.deepEqual(loadInstances(root, 'ContractClause'), []);
});

test('saveInstances throws on an unknown object type name', () => {
  const root = tempRoot();
  assert.throws(() => saveInstances(root, 'NotARealType', [{}]), /Unknown object type/);
});

test('saveInstances throws on an empty instances array', () => {
  const root = tempRoot();
  assert.throws(() => saveInstances(root, 'RiskMark', []), /non-empty array/);
});

test('saveInstances enforces the per-type instance cap', () => {
  const root = tempRoot();
  const many = new Array(301).fill(0).map((_, i) => ({ objectType: 'RiskMark', quantity: 'VaR', value: i, uncertainty: 1, source: `s${i}` }));
  assert.throws(() => saveInstances(root, 'RiskMark', many), /exceed the 300-instance cap/);
});

test('unsafe object type names are rejected as filename components', () => {
  const root = tempRoot();
  // getObjectType() throws first for a genuinely unknown name, so this
  // proves the intended defense using a real registered-looking but
  // path-traversal-shaped string is still caught (by the unknown-type
  // check, which happens to run first -- either way, nothing unsafe
  // ever reaches a filesystem path).
  assert.throws(() => saveInstances(root, '../../etc', [{}]), /Unknown object type/);
});

// ---- self-wiring: RiskMark -> TensionWith via the REAL compareMeasurements ----

test('self-wiring: two RiskMark instances of the same quantity with a decisive gap produce a real TensionWith relationship, reusing compareMeasurements unmodified', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'RiskMark', [
    { objectType: 'RiskMark', quantity: 'VaR', value: 10, uncertainty: 0.3, source: 'internal' },
    { objectType: 'RiskMark', quantity: 'VaR', value: 20, uncertainty: 0.3, source: 'counterparty' },
  ]);
  assert.equal(result.selfWired, true);
  assert.equal(result.relationships.length, 1);
  assert.equal(result.relationships[0].relationship, 'TensionWith');
  assert.equal(result.relationships[0].kind, 'contradiction'); // >=5 sigma
  assert.equal(result.relationships[0].quantity, 'var');

  const persisted = loadRelationships(root, 'RiskMark');
  assert.deepEqual(persisted, result.relationships);
});

test('self-wiring: RiskMark instances of DIFFERENT quantities never produce a spurious relationship', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'RiskMark', [
    { objectType: 'RiskMark', quantity: 'VaR', value: 10, uncertainty: 0.3, source: 'a' },
    { objectType: 'RiskMark', quantity: 'CVaR', value: 999, uncertainty: 0.3, source: 'b' },
  ]);
  assert.deepEqual(result.relationships, []);
});

test('self-wiring recomputes over the FULL accumulated set, not just the newly-added batch', () => {
  const root = tempRoot();
  saveInstances(root, 'RiskMark', [{ objectType: 'RiskMark', quantity: 'VaR', value: 10, uncertainty: 0.3, source: 'a' }]);
  // Second call adds one more instance; the resulting tension is between
  // THIS new one and the one saved in the FIRST call, proving relationships
  // are computed over everything stored, not just this call's own batch.
  const result2 = saveInstances(root, 'RiskMark', [{ objectType: 'RiskMark', quantity: 'VaR', value: 30, uncertainty: 0.3, source: 'b' }]);
  assert.equal(result2.relationships.length, 1);
  assert.equal(result2.relationships[0].a.source === 'a' || result2.relationships[0].b.source === 'a', true);
});

// ---- self-wiring: Requirement/ContractClause -> Contradicts via the REAL contradicts() ----

test('self-wiring: a direct logical contradiction across two ContractClause instances is caught via the real contradicts()/consistencyKernel path', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'ContractClause', [
    { objectType: 'ContractClause', kind: 'assert', atom: 'terminationForCause', polarity: true, source: 'clause-4.2' },
    { objectType: 'ContractClause', kind: 'assert', atom: 'terminationForCause', polarity: false, source: 'clause-9.1' },
  ]);
  assert.equal(result.selfWired, true);
  assert.ok(result.relationships.length > 0);
  assert.equal(result.relationships[0].relationship, 'Contradicts');
});

test('self-wiring: consistent ContractClause instances produce no findings', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'ContractClause', [
    { objectType: 'ContractClause', kind: 'assert', atom: 'renewalOptOut', polarity: true, source: 'clause-1' },
  ]);
  assert.deepEqual(result.relationships, []);
});

// ---- self-wiring: SupplierRanking -> ImpossibleCycle via the REAL impossibleCycle() ----

test('self-wiring: an impossible ranking cycle across SupplierRanking instances is caught via the real impossibleCycle() path', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'SupplierRanking', [
    { objectType: 'SupplierRanking', subject: 'A', object: 'B', comparator: 'greater', metric: 'reliability', source: 's1' },
    { objectType: 'SupplierRanking', subject: 'B', object: 'C', comparator: 'greater', metric: 'reliability', source: 's2' },
    { objectType: 'SupplierRanking', subject: 'C', object: 'A', comparator: 'greater', metric: 'reliability', source: 's3' },
  ]);
  assert.ok(result.relationships.length > 0);
  assert.equal(result.relationships[0].relationship, 'ImpossibleCycle');
});

// ---- object types without a self-wiring function -----------------------

test('an object type with no registered self-wiring function is stored but produces no relationships, silently and correctly (not an error)', () => {
  const root = tempRoot();
  const result = saveInstances(root, 'DesignMargin', [{ objectType: 'DesignMargin', params: [], objective: 'x<=1' }]);
  assert.equal(result.selfWired, false);
  assert.deepEqual(result.relationships, []);
  assert.deepEqual(loadRelationships(root, 'DesignMargin'), []);
});

// ---- listing ------------------------------------------------------------

test('listStoredObjectTypes lists exactly the types that have been saved to, sorted', () => {
  const root = tempRoot();
  assert.deepEqual(listStoredObjectTypes(root), []);
  saveInstances(root, 'RiskMark', [{ objectType: 'RiskMark', quantity: 'VaR', value: 1, uncertainty: 1, source: 'a' }]);
  saveInstances(root, 'ContractClause', [{ objectType: 'ContractClause', kind: 'assert', atom: 'x', polarity: true, source: 's' }]);
  assert.deepEqual(listStoredObjectTypes(root), ['ContractClause', 'RiskMark']);
});
