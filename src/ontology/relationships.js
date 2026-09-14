// ontology/relationships.js; computed RELATIONSHIPS between object
// instances — the other half of an ontology (objects alone are just a
// schema; relationships are what make it a graph). Every function here
// is a thin wrapper over an existing kernel, unmodified — same rule as
// domains/: this formalizes typing, it does not add verification logic.

import { tensionSigma, classifyTension } from '../lib/measurementTension.js';
import { findContradictions, normalizeCommitments } from '../lib/consistencyKernel.js';
import { normalizeRelations, findOrderCycles } from '../lib/orderConsistency.js';

/** TensionWith: the computed relationship between two RiskMark instances. */
export function tensionWith(riskMarkA, riskMarkB) {
  const sigma = tensionSigma(riskMarkA, riskMarkB);
  return { relationship: 'TensionWith', sigma, classification: classifyTension(sigma) };
}

/** Contradicts: the computed relationship across a set of Requirement or ContractClause instances. */
export function contradicts(commitmentInstances) {
  const commitments = normalizeCommitments({ commitments: commitmentInstances });
  const findings = findContradictions(commitments);
  return findings.map((f) => ({ relationship: 'Contradicts', ...f }));
}

/** ImpossibleCycle: the computed relationship across a set of SupplierRanking instances. */
export function impossibleCycle(rankingInstances) {
  const relations = normalizeRelations({ relations: rankingInstances });
  const cycles = findOrderCycles(relations);
  return cycles.map((c) => ({ relationship: 'ImpossibleCycle', ...c }));
}
