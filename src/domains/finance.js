// domains/finance.js; the finance-risk example promoted from a one-off
// script into a reusable, registered vertical. Zero new kernel code —
// every function here is a thin, domain-shaped wrapper over
// consistencyKernel.js / mcmcSearch.js / orderConsistency.js, unmodified.

import { normalizeCommitments, findContradictions } from '../lib/consistencyKernel.js';
import { normalizeMcmcSpec, executeMcmcSearch } from '../lib/mcmcSearch.js';
import { normalizeRelations, findOrderCycles } from '../lib/orderConsistency.js';
import { fuseMeasurements } from '../lib/dataFusion.js';

/** Reconciles two marks of the same risk quantity (e.g. internal VaR vs. counterparty VaR). */
export function verifyRiskMarkReconciliation({ quantity, markA, markB }) {
  const commitments = normalizeCommitments({
    commitments: [
      { kind: 'measurement', quantity, value: markA.value, uncertainty: markA.uncertainty, assumes: markA.assumes || [], source: markA.source },
      { kind: 'measurement', quantity, value: markB.value, uncertainty: markB.uncertainty, assumes: markB.assumes || [], source: markB.source },
    ],
  });
  return findContradictions(commitments);
}

/** Stress-tests a numeric margin claim (e.g. drawdown, VaR limit) by MCMC counterexample search. */
export function stressTestMargin({ params, objective, note }) {
  const spec = normalizeMcmcSpec({ kind: 'mcmc_search', note, params, objective });
  return executeMcmcSearch(spec);
}

/** Catches an impossible risk-ranking cycle across desks/strategies (e.g. A<B<C<A on the same metric). */
export function verifyRiskRankingConsistency({ metric, comparisons }) {
  const relations = normalizeRelations({
    relations: comparisons.map((c) => ({ subject: c.subject, object: c.object, comparator: c.comparator, metric, source: c.source })),
  });
  return findOrderCycles(relations);
}

/**
 * "Selection under tail risk" gate.
 *
 * The problem this answers, found by research (2026): Citadel's own
 * quant chief describes the bottleneck shifting once AI makes generating
 * candidate trades/strategies cheap — "AI generates the option set...
 * selection under tail risk becomes the bottleneck." Palantir's AIP
 * addresses hallucination through ontology grounding and transparent
 * reasoning chains, NOT independent falsification of a specific claim.
 * JPMorgan's own mitigation for LLM hallucination risk in a fiduciary
 * context is to keep AI confined to back-office use rather than
 * high-stakes output, because an unverified hallucination there is legal
 * liability, not a beneficial feature.
 *
 * This function is the mechanical answer common to all three: every
 * candidate — however it was generated, by whatever model, correlated or
 * not with every other candidate — gets run through the SAME unmodified
 * MCMC kernel, independently, searching for a tail-risk counterexample.
 * A candidate that survives did so against a search structurally
 * separate from whatever proposed it; a candidate that fails comes back
 * with the concrete point that breaks it, not a vague downgrade.
 *
 * `candidates`: [{ id, note, params, objective }] — each already in the
 * exact shape mcmcSearch.js expects; this function adds no new spec
 * logic, it only runs the set and reports per-candidate.
 */
export function selectSurvivingCandidates(candidates) {
  return candidates.map((c) => {
    const spec = normalizeMcmcSpec({ kind: 'mcmc_search', note: c.note, params: c.params, objective: c.objective });
    const result = executeMcmcSearch(spec);
    return { id: c.id, verdict: result.verdict, result };
  });
}

/**
 * Data fusion for risk marks: combines multiple independent marks of the
 * same risk quantity (e.g. internal VaR, counterparty VaR, a third
 * vendor's model) into one fused estimate via inverse-variance
 * weighting — REFUSING to fuse if any pair is in decisive tension
 * (exactly the reconciliation check `verifyRiskMarkReconciliation` above
 * already does, reused here as the gate before fusing rather than after).
 * See src/lib/dataFusion.js for the full contract.
 */
export function fuseRiskMarks(marks) {
  return fuseMeasurements(marks);
}
