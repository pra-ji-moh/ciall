// domains/engineering.js; structural/design-margin claims. Zero new
// kernel code — wraps mcmcSearch.js and dimensionalAnalysis.js unmodified.

import { normalizeMcmcSpec, executeMcmcSearch } from '../lib/mcmcSearch.js';
import { analyzeEquation } from '../lib/dimensionalAnalysis.js';

/** Stress-tests a stated design margin (e.g. "spar holds under X g-load with Y% margin") across tolerance + load combinations. */
export function verifyDesignMargin({ params, objective, note }) {
  const spec = normalizeMcmcSpec({ kind: 'mcmc_search', note, params, objective });
  return executeMcmcSearch(spec);
}

/** Checks a stated engineering equation for dimensional balance — a mismatched equation is false regardless of context. */
export function verifyDimensionalBalance({ lhs, rhs, assignments }) {
  return analyzeEquation({ lhs, rhs, assignments });
}
