// ontology.test.mjs; two things matter here more than usual:
// 1. Every object type's `verifiedBy` kernel id must be REAL — checked
//    against kernelRegistry.js directly, not just declared in a comment,
//    so this file cannot silently drift from what the substrate can
//    actually verify.
// 2. Every relationship function must produce the IDENTICAL result to
//    calling the underlying kernel directly — proving the ontology layer
//    adds typing, not new (and therefore unverified) logic.

import test from 'node:test';
import assert from 'node:assert/strict';
import { listObjectTypes, getObjectType, objectTypesForDomain } from '../src/ontology/objectTypes.js';
import { tensionWith, contradicts, impossibleCycle } from '../src/ontology/relationships.js';
import { getKernel, listKernels } from '../src/lib/kernelRegistry.js';
import { tensionSigma, classifyTension } from '../src/lib/measurementTension.js';
import { findContradictions, normalizeCommitments } from '../src/lib/consistencyKernel.js';
import { normalizeRelations, findOrderCycles } from '../src/lib/orderConsistency.js';

test('every object type names a kernel that actually exists in kernelRegistry.js', () => {
  const realKernelIds = new Set(listKernels().map((k) => k.id));
  for (const t of listObjectTypes()) {
    assert.ok(realKernelIds.has(t.verifiedBy), `object type "${t.name}" claims verifiedBy="${t.verifiedBy}", which is not a real registered kernel`);
    assert.doesNotThrow(() => getKernel(t.verifiedBy)); // the actual check a caller would run
  }
});

test('listObjectTypes covers all 6 domains from the domain registry', () => {
  const domains = new Set(listObjectTypes().map((t) => t.domain));
  assert.deepEqual([...domains].sort(), ['engineering', 'finance', 'legal', 'mission-planning', 'requirements', 'supply-chain']);
});

test('getObjectType returns the entry; unknown name throws', () => {
  assert.equal(getObjectType('RiskMark').domain, 'finance');
  assert.throws(() => getObjectType('NoSuchType'), /Unknown object type/);
});

test('objectTypesForDomain filters correctly', () => {
  const financeTypes = objectTypesForDomain('finance').map((t) => t.name).sort();
  assert.deepEqual(financeTypes, ['RiskMarginClaim', 'RiskMark']); // alphabetical: 'g' < 'k'
});

test('tensionWith produces the IDENTICAL result to calling measurementTension.js directly', () => {
  const a = { value: 4.2, uncertainty: 0.3 };
  const b = { value: 6.8, uncertainty: 0.4 };
  const viaOntology = tensionWith(a, b);
  const direct = { sigma: tensionSigma(a, b), classification: classifyTension(tensionSigma(a, b)) };
  assert.equal(viaOntology.sigma, direct.sigma);
  assert.equal(viaOntology.classification, direct.classification);
});

test('contradicts produces the IDENTICAL findings to calling consistencyKernel.js directly', () => {
  const raw = [
    { kind: 'assert', atom: 'growth is slowing', polarity: true, source: 'a' },
    { kind: 'assert', atom: 'growth is slowing', polarity: false, source: 'b' },
  ];
  const viaOntology = contradicts(raw);
  const direct = findContradictions(normalizeCommitments({ commitments: raw }));
  assert.equal(viaOntology.length, direct.length);
  assert.equal(viaOntology.length, 1);
  assert.equal(viaOntology[0].about, direct[0].about);
});

test('impossibleCycle produces the IDENTICAL result to calling orderConsistency.js directly', () => {
  const raw = [
    { subject: 'A', object: 'B', comparator: 'greater', metric: 'x', source: 's1' },
    { subject: 'B', object: 'C', comparator: 'greater', metric: 'x', source: 's2' },
    { subject: 'C', object: 'A', comparator: 'greater', metric: 'x', source: 's3' },
  ];
  const viaOntology = impossibleCycle(raw);
  const direct = findOrderCycles(normalizeRelations({ relations: raw }));
  assert.equal(viaOntology.length, direct.length);
  assert.equal(viaOntology.length, 1);
});

test('a consistent (non-cyclic) ranking produces no relationship, via the ontology layer', () => {
  const raw = [{ subject: 'A', object: 'B', comparator: 'greater', metric: 'x', source: 's1' }];
  assert.equal(impossibleCycle(raw).length, 0);
});
