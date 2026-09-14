// domains/requirements.js; catching contradictory requirements
// accumulated over a program's lifetime (the Nunn-McCurdy-adjacent
// application from the original research: engineering/design issues and
// schedule issues are the named root causes of cost-growth breaches).
// Zero new kernel code — wraps consistencyKernel.js unmodified.

import { normalizeCommitments, findContradictions, summarizeConsistency } from '../lib/consistencyKernel.js';

/**
 * `requirements`: an array of pre-structured commitment objects in
 * consistencyKernel's forms (assert/implies/universal/instance/property/
 * relation/equation/measurement) — e.g. "if weight increases, cost
 * increases" (implies) plus "cost must not increase" (assert) plus
 * "weight increased this quarter" (assert) is exactly the kind of
 * accumulated-contradiction set this catches.
 */
export function verifyRequirementsConsistency(requirements) {
  const commitments = normalizeCommitments({ commitments: requirements });
  const findings = findContradictions(commitments);
  return { findings, summary: summarizeConsistency(findings, commitments.length) };
}
