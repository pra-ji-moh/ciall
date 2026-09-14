// domains/supplyChain.js; supplier resilience and dependency-ranking
// claims. Zero new kernel code — wraps mcmcSearch.js and
// orderConsistency.js unmodified.

import { normalizeMcmcSpec, executeMcmcSearch } from '../lib/mcmcSearch.js';
import { normalizeRelations, findOrderCycles } from '../lib/orderConsistency.js';

/** Stress-tests a stated resilience claim (e.g. "supply holds under any single-supplier failure up to X% of volume") across supplier-failure combinations. */
export function stressTestResilience({ params, objective, note }) {
  const spec = normalizeMcmcSpec({ kind: 'mcmc_search', note, params, objective });
  return executeMcmcSearch(spec);
}

/** Catches an impossible supplier-priority cycle (A preferred over B, B over C, C over A on the same criterion). */
export function verifySupplierPriorityConsistency({ metric, comparisons }) {
  const relations = normalizeRelations({
    relations: comparisons.map((c) => ({ subject: c.subject, object: c.object, comparator: c.comparator, metric, source: c.source })),
  });
  return findOrderCycles(relations);
}
