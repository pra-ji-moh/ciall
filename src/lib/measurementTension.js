// measurementTension.js; deriving numerical incompatibility instead of
// being told about it.
//
// The gap this closes was found by running the Hubble tension through the
// consistency kernel as a positive control. It fired, but only because the
// commitment "H0 is a single quantity and 67.4 +/- 0.5 does not overlap
// 73.0 +/- 1.0" was supplied by hand. A physicist would state that without
// thinking, but requiring it means the tool can only confirm a tension the
// user already suspected. It could never surface one they had not noticed,
// which is the only thing that would make it an instrument of discovery
// rather than a formatter for conclusions already reached.
//
// Non-overlap is arithmetic. It should be computed, not asserted.
//
// THE MEASURE. Not "do the error bars touch", which is a weak and
// convention-dependent test, but the quantity physics actually uses:
//
//     tension (in sigma) = |v1 - v2| / sqrt(u1^2 + u2^2)
//
// This is the standard combination for two INDEPENDENT measurements, and
// it is the number quoted in the literature. Applied to Planck 2018
// (67.4 +/- 0.5) against SH0ES (73.0 +/- 1.0) it returns almost exactly
// 5.0, which is the figure the field quotes for the Hubble tension. That
// agreement with an external, independently published number is the real
// test of this module, and it is asserted in the test suite.
//
// THREE HONESTY CONSTRAINTS, all of which cost coverage and are worth it.
//
// 1. INDEPENDENCE IS ASSUMED AND IS OFTEN FALSE. The formula above adds
//    variances, which is only valid when the two measurements share no
//    systematic. Real measurements in the same field frequently share
//    calibrations, catalogues, or pipeline assumptions, and when they do
//    this OVERSTATES the tension. There is no way to detect that from the
//    numbers alone, so the module reports the assumption alongside every
//    result rather than hiding it, and never upgrades a shared-systematic
//    case silently.
//
// 2. A TENSION IS NOT AUTOMATICALLY A CONTRADICTION. Physics treats 3
//    sigma as evidence and 5 sigma as decisive, and this module keeps that
//    distinction instead of collapsing it into a boolean. Anything below
//    the decisive threshold is reported as a measured tension, in the same
//    spirit as the density check elsewhere in this codebase, which
//    measures rather than forcing a pass or fail.
//
// 3. MODEL-DEPENDENT VALUES ARE NOT DIRECT MEASUREMENTS. Planck's H0 is
//    not read off the sky; it is inferred under LCDM. So an apparent
//    conflict between it and a direct measurement may not mean either
//    instrument is faulty. It may mean the MODEL is wrong, which is
//    precisely what makes the Hubble tension interesting rather than
//    embarrassing. Any assumption attached to a measurement is therefore
//    carried into the finding as one of the commitments that could be the
//    false one, and is named in the output.

export const DECISIVE_SIGMA = 5;   // physics convention for a definitive discrepancy
export const EVIDENCE_SIGMA = 3;   // convention for "evidence for", not proof

// Returns the tension in sigma, or null when it genuinely cannot be
// computed. Returning null rather than a number is deliberate: a made-up
// tension is far worse than a missing one.
export function tensionSigma(a, b) {
  const v1 = Number(a?.value), v2 = Number(b?.value);
  const u1 = Number(a?.uncertainty), u2 = Number(b?.uncertainty);
  if (![v1, v2, u1, u2].every(Number.isFinite)) return null;
  if (u1 < 0 || u2 < 0) return null;
  const combined = Math.sqrt(u1 * u1 + u2 * u2);
  // Two values quoted with zero uncertainty are either identical or a
  // straightforward contradiction; sigma is undefined, so say so rather
  // than dividing by zero and reporting Infinity as if it were a measured
  // significance.
  if (combined === 0) return v1 === v2 ? 0 : null;
  return Math.abs(v1 - v2) / combined;
}

export function classifyTension(sigma) {
  if (sigma === null || !Number.isFinite(sigma)) return 'incomparable';
  if (sigma >= DECISIVE_SIGMA) return 'contradiction';
  if (sigma >= EVIDENCE_SIGMA) return 'tension';
  return 'compatible';
}

// Two measurements only conflict if they are of the SAME quantity. Matched
// by exact normalized string for the same reason atoms are elsewhere: a
// fuzzy match that wrongly unifies two different quantities would generate
// confident nonsense, which is the one failure this codebase cannot afford.
const normQuantity = (s) => String(s || '').trim().toLowerCase().replace(/\s+/g, ' ');

export function compareMeasurements(measurements) {
  const list = (measurements || []).filter((m) => m && normQuantity(m.quantity));
  const findings = [];
  for (let i = 0; i < list.length; i++) {
    for (let j = i + 1; j < list.length; j++) {
      const a = list[i], b = list[j];
      if (normQuantity(a.quantity) !== normQuantity(b.quantity)) continue;
      const sigma = tensionSigma(a, b);
      const kind = classifyTension(sigma);
      if (kind === 'compatible' || kind === 'incomparable') continue;
      const assumptions = [...new Set([...(a.assumes || []), ...(b.assumes || [])])].filter(Boolean);
      findings.push({
        quantity: normQuantity(a.quantity),
        sigma: Math.round(sigma * 100) / 100,
        kind, // 'contradiction' at >= 5 sigma, 'tension' at >= 3
        a: { value: Number(a.value), uncertainty: Number(a.uncertainty), source: a.source || '', assumes: a.assumes || [] },
        b: { value: Number(b.value), uncertainty: Number(b.uncertainty), source: b.source || '', assumes: b.assumes || [] },
        assumptions,
        note: describeFinding(kind, assumptions),
      });
    }
  }
  return findings.sort((x, y) => y.sigma - x.sigma);
}

function describeFinding(kind, assumptions) {
  const base = kind === 'contradiction'
    ? 'These two values of the same quantity are separated by more than five sigma, the threshold physics treats as decisive. They cannot both be right as stated.'
    : 'These two values of the same quantity are in real tension but below the five sigma threshold. This is evidence of a discrepancy, not proof of one.';
  const indep = ' The sigma assumes the two measurements are independent. If they share a systematic, a calibration, or a catalogue, this figure overstates the conflict.';
  const model = assumptions.length
    ? ` At least one of these values is model-dependent (${assumptions.join('; ')}). So the false commitment may not be either measurement: it may be the model. That possibility is the interesting one and must not be dropped.`
    : '';
  return base + indep + model;
}

export function summarizeMeasurementTensions(findings) {
  const list = findings || [];
  const decisive = list.filter((f) => f.kind === 'contradiction');
  if (list.length === 0) {
    return {
      verdict: 'no-tension-found',
      headline: '',
      detail: '',
    };
  }
  if (decisive.length > 0) {
    const top = decisive[0];
    return {
      verdict: 'numerical-contradiction',
      headline: `Two stated values of ${top.quantity} are ${top.sigma} sigma apart.`,
      detail: 'This was computed on your device from the numbers themselves, not argued. Which value or which assumption to give up is a judgement this tool does not have.',
    };
  }
  const top = list[0];
  return {
    verdict: 'numerical-tension',
    headline: `Two stated values of ${top.quantity} are in ${top.sigma} sigma tension.`,
    detail: 'Below the five sigma threshold physics treats as decisive, so this is a flag worth checking rather than a proof of conflict.',
  };
}
