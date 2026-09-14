// domainOfValidity.js; the fix for "contradiction found, discard branch".
//
// Origin: a proposed architecture searched alternate-physics configurations
// and threw away any branch where a contradiction appeared. That rule is
// wrong, and wrong in a way that deletes exactly the interesting branches.
// In effective field theory a contradiction at high energy usually means
// you left the domain of validity, not that the world is impossible. Every
// theory is "inconsistent" if you push it past its cutoff. The contradiction
// is not the theory's death, it is a measurement of where the theory stops.
//
// Ciall had the same bug. Every other instrument here is an attacker: MCMC,
// the debate partner, abductive counterfactuals all hunt for the break, and
// when they find one the node goes 'weak' and the branch effectively dies.
// But "your claim broke under condition X" and "your claim is false" are
// different findings, and the second is usually not what was shown. Most
// claims that break are not false; they were stated at the wrong scope.
//
// So this instrument runs AFTER a break and asks the question the attackers
// cannot: given that it failed there, where exactly does it still hold, and
// is that surviving region worth anything? It converts a binary refutation
// into a boundary.
//
// The honesty contract, and it is the whole instrument: narrowing a claim
// until it survives is trivial and worthless. Any claim can be saved by
// restricting it to a single case. So this must detect and refuse the
// vacuous save. A narrowed claim only counts if the surviving region is
// still non-trivial and still contains what the person actually cared
// about. A 'vacuous' verdict is a REAL result and is reported as plainly
// as a survival; it means the claim was rescued into meaninglessness.

export const MAX_BOUNDARIES = 4;

export function buildDomainOfValidityPrompt(claimText, coreClaim, breakEvidence) {
  return `A claim has just failed under pressure. Do NOT re-attack it; other instruments already did that, and the break is established. Your job is the opposite and more precise one: find where this claim STILL HOLDS.

CLAIM: "${claimText}"
${coreClaim && coreClaim !== claimText ? `(A branch of the broader claim: "${coreClaim}")` : ''}
${breakEvidence ? `\nHOW IT BROKE: ${breakEvidence}` : ''}

The reasoning behind this task: a claim that breaks under some condition is usually not false. It was usually stated at the wrong scope. A theory that fails at high energy is not wrong, it has a cutoff. Your job is to locate the cutoff.

STEP 1, THE BOUNDARY: state the sharpest condition separating where this claim holds from where it fails. Be specific and testable, not "in some cases". A real boundary looks like "holds while the input set is finite", "holds below the point where acquisition cost exceeds lifetime value", "holds for non-relativistic velocities". If the break is total and no boundary exists, say so; do not invent one.

STEP 2, THE RESTRICTED CLAIM: restate the claim scoped to the region where it survives. Change ONLY the scope. You may not weaken what the claim asserts inside its region, and you may not quietly swap the subject.

STEP 3, THE VACUITY TEST, and be ruthless here: is the surviving region still worth anything? Any claim can be rescued by narrowing it to a single case, and that rescue is worthless. Ask directly: does the restricted claim still cover the situations the author actually cared about, or has it been narrowed into a triviality? If the surviving region is trivial, empty, or excludes the motivating case, the honest verdict is "vacuous" and you must say so. A vacuous save is a real and useful finding; it tells the author the claim cannot be repaired by scoping and the core idea itself needs to change. Do not soften this to be encouraging.

STEP 4, WHAT THE BOUNDARY REVEALS: one line on what the location of the cutoff tells us that the original unrestricted claim concealed. This is often the most valuable output; a cutoff in an unexpected place is information about the structure of the problem.

Respond ONLY with JSON:
{
  "boundary": "the sharpest holds-here / fails-there condition, one or two sentences",
  "restricted": "the claim restated at its surviving scope, one or two sentences",
  "excluded": ["specific situations now excluded that the original claim covered, up to ${MAX_BOUNDARIES}"],
  "motivatingCaseSurvives": true or false (does the restricted claim still cover the case that made this claim worth making?),
  "insight": "what the location of the cutoff reveals, one line",
  "verdict": "narrowed" | "vacuous" | "total" | "unresolved"
}
"narrowed" = a real, non-trivial region survives AND it still contains the motivating case. "vacuous" = it can only be saved by narrowing it into a triviality, or the motivating case is excluded. "total" = the break is genuine and total, there is no surviving region at all. "unresolved" = genuinely cannot locate a boundary; do not force one.`;
}

// Pure normalization, no engine dependency, so it unit-tests cleanly.
export function normalizeDomainOfValidity(raw) {
  const validVerdicts = new Set(['narrowed', 'vacuous', 'total', 'unresolved']);
  const excluded = Array.isArray(raw?.excluded) ? raw.excluded.slice(0, MAX_BOUNDARIES) : [];
  const verdict = validVerdicts.has(raw?.verdict) ? raw.verdict : 'unresolved';
  const motivatingCaseSurvives = Boolean(raw?.motivatingCaseSurvives);
  return {
    boundary: String(raw?.boundary || '').slice(0, 500),
    restricted: String(raw?.restricted || '').slice(0, 500),
    excluded: excluded.map((e) => String(e || '').slice(0, 200)).filter(Boolean),
    motivatingCaseSurvives,
    insight: String(raw?.insight || '').slice(0, 300),
    // The model does not get the last word on whether a save was real.
    // If it claims a narrowing but also concedes the motivating case is
    // gone, that is a vacuous save by definition and we overrule it here.
    // This is the same discipline the kernel applies elsewhere: a
    // structural contradiction in the output beats the stated verdict.
    verdict: verdict === 'narrowed' && !motivatingCaseSurvives ? 'vacuous' : verdict,
  };
}

// A genuine save: a non-trivial region survives and it still contains the
// case that made the claim worth making. This is the only outcome that
// should ever soften a node's status, and even then only from refuted to
// weak, never to surviving; the unrestricted claim really did fail.
export function isGenuineNarrowing(normalized) {
  return normalized.verdict === 'narrowed'
    && normalized.motivatingCaseSurvives
    && Boolean(normalized.restricted);
}

export function summarizeDomainOfValidity(normalized) {
  if (!normalized) return '';
  switch (normalized.verdict) {
    case 'narrowed':
      return 'This is not false. It was stated too broadly, and it holds inside a real boundary.';
    case 'vacuous':
      return 'This can only be saved by narrowing it into a triviality. Scoping will not repair it; the idea itself has to change.';
    case 'total':
      return 'There is no surviving region. The break is genuine and total.';
    default:
      return 'No boundary could be located from what is here.';
  }
}
