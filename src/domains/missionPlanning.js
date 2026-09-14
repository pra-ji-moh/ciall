// domains/missionPlanning.js; the AI-Fight-Club-adjacent application
// from the original research: turning a scenario-level "this plan
// failed" into a claim-level, falsifiable envelope check. Zero new
// kernel code — wraps mcmcSearch.js unmodified.

import { normalizeMcmcSpec, executeMcmcSearch } from '../lib/mcmcSearch.js';

/** Stress-tests a stated operational envelope claim (e.g. "plan succeeds for any threat-timing/resource combination within X") by MCMC search over the parameter space. */
export function verifyOperationalEnvelope({ params, objective, note }) {
  const spec = normalizeMcmcSpec({ kind: 'mcmc_search', note, params, objective });
  return executeMcmcSearch(spec);
}
