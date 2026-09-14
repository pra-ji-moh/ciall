// adaptiveMcmc.js; NEW logic (stated plainly) addressing one specific,
// real source of bias in adversarial search: the human/model-chosen
// search domain itself. mcmcSearch.js's kernel is completely unmodified
// — every test depending on its bit-for-bit determinism keeps passing
// unchanged; this wraps it, it does not touch it.
//
// THE BIAS THIS ADDRESSES. A human or a model designs the params/domain
// for an MCMC search. If that domain happens to exclude the real
// counterexample — deliberately, or just by an unlucky guess at
// reasonable bounds — the kernel will correctly report "held" WITHIN
// that domain, and that verdict gets trusted, even though a genuine
// violation sits just past the boundary someone chose. That is not a
// flaw in the kernel; the kernel is honest about only searching what it
// was told to search. It is a flaw in trusting a human-chosen boundary
// uncritically, without ever asking whether the boundary itself might be
// hiding the answer.
//
// THE SIGNAL, and it's a real one, not invented: if the best point found
// sits within a small margin of a domain's edge, the search was pulled
// toward that boundary — meaning the worst region may well continue past
// it. Automatically re-running with an EXPANDED domain when this happens
// removes the original boundary as a silent, untested source of false
// confidence. Every expansion performed is recorded and returned,
// visible to the caller, never hidden — this is disclosure, not a
// silent retry loop.

import { normalizeMcmcSpec, executeMcmcSearch } from './mcmcSearch.js';

const DEFAULT_MAX_EXPANSIONS = 3;
const DEFAULT_EDGE_MARGIN_FRACTION = 0.03; // within 3% of the domain width counts as "hugging the edge"
const DEFAULT_EXPANSION_FACTOR = 3; // domain width triples per expansion

function hugsEdge(bestPoint, params, marginFraction) {
  return params.some((p) => {
    const v = bestPoint[p.name];
    const width = p.domain[1] - p.domain[0];
    const margin = width * marginFraction;
    return v - p.domain[0] <= margin || p.domain[1] - v <= margin;
  });
}

function expandDomain(params, factor) {
  return params.map((p) => {
    const [lo, hi] = p.domain;
    const width = hi - lo;
    const grow = (width * (factor - 1)) / 2;
    return { ...p, domain: [lo - grow, hi + grow] };
  });
}

/**
 * Runs mcmcSearch, and if the best point found hugs a domain edge,
 * automatically re-runs with an expanded domain — up to `maxExpansions`
 * times — rather than trusting the original boundary uncritically.
 *
 * Returns the underlying executeMcmcSearch result, PLUS:
 *   expansions: [{ attempt, paramsBeforeExpansion, bestPoint, reason }]
 *   finalParams: the params actually used for the returned result
 * An empty `expansions` array means the first search already didn't hug
 * an edge — no bias-correction was needed, and none was silently applied.
 */
export function runAdaptiveMcmcSearch(rawSpec, opts = {}) {
  const maxExpansions = opts.maxExpansions ?? DEFAULT_MAX_EXPANSIONS;
  const marginFraction = opts.edgeMarginFraction ?? DEFAULT_EDGE_MARGIN_FRACTION;
  const expansionFactor = opts.expansionFactor ?? DEFAULT_EXPANSION_FACTOR;

  let currentParams = rawSpec.params;
  const expansions = [];
  let result = null;

  for (let attempt = 0; attempt <= maxExpansions; attempt++) {
    const spec = normalizeMcmcSpec({ ...rawSpec, params: currentParams });
    result = executeMcmcSearch(spec);

    if (!result.bestPoint) break; // inconclusive/none: nothing to check an edge against
    if (!hugsEdge(result.bestPoint, currentParams, marginFraction)) break; // genuinely interior; no bias correction needed
    if (attempt === maxExpansions) break; // out of retries; report what we have, honestly, expansions list shows exactly how far this went

    expansions.push({ attempt, paramsBeforeExpansion: currentParams, bestPoint: result.bestPoint, reason: 'best point hugs a domain edge; expanding search space rather than trusting the original boundary' });
    currentParams = expandDomain(currentParams, expansionFactor);
  }

  return { ...result, expansions, finalParams: currentParams };
}
