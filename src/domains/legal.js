// domains/legal.js; contract/clause consistency. Zero new kernel code —
// wraps consistencyKernel.js unmodified.

import { normalizeCommitments, findContradictions, summarizeConsistency } from '../lib/consistencyKernel.js';

/**
 * `clauses`: pre-structured commitment objects (assert/implies/universal/
 * instance/property/relation) — e.g. a termination clause that implies
 * one outcome while a liability clause asserts its negation.
 */
export function verifyContractConsistency(clauses) {
  const commitments = normalizeCommitments({ commitments: clauses });
  const findings = findContradictions(commitments);
  return { findings, summary: summarizeConsistency(findings, commitments.length) };
}

/**
 * The exact failure scenario Palantir's own blog names as still possible
 * even with ontology grounding: "your RAG system gives a compliance
 * officer a hallucinated sanction status on a counterparty... the cost
 * is not an engineering post-mortem — it's a regulatory conversation."
 * (blog.palantir.com/reducing-hallucinations-with-the-ontology-in-aip)
 *
 * Palantir's own described architecture is four layers of constraint —
 * what you see, what you can query, what you can do, who has permission.
 * None of those four independently VERIFY that a specific DERIVED claim
 * (a sanction status computed from ownership structure) is actually
 * consistent with the other facts already on record. They govern access
 * and provenance; they don't check the claim's logic.
 *
 * This function is that missing check, using the exact same
 * consistencyKernel.js every other domain here already uses — no new
 * logic. Given asserted facts about a counterparty (an ownership rule, an
 * instance fact, and a stated status), it proves whether the claimed
 * status can actually be true, deterministically, independent of
 * whatever system produced the claim.
 */
export function verifyComplianceClaimConsistency(facts) {
  const commitments = normalizeCommitments({ commitments: facts });
  const findings = findContradictions(commitments);
  return { findings, summary: summarizeConsistency(findings, commitments.length) };
}
