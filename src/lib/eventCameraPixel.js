// eventCameraPixel.js; upgrade 10 — a real, mechanistically-accurate
// simulator for how a single event-based (DVS-style) camera pixel
// actually generates events, plus a deterministic kernel verifying a
// CLAIMED event log against what the real mechanism would produce from
// a given intensity signal.
//
// THE REAL MECHANISM (cited; verified against event-vision research
// literature, e.g. Gallego et al.'s survey and UZH-RPG's event-vision
// group): each pixel is bio-inspired and independent — it does NOT
// sample at a fixed frame rate. It continuously monitors the LOG of
// its own incident intensity. It remembers the log-intensity value at
// its own last event (or at power-on, for the very first one). The
// instant the log-intensity has drifted by more than a fixed contrast
// THRESHOLD from that remembered reference, in EITHER direction, it
// fires exactly one event — polarity ON (brighter) or OFF (dimmer) —
// and resets its own reference to (reference +/- threshold), NOT to
// the new instantaneous value. This distinction matters and is
// implemented faithfully here: a single large, fast brightness jump
// crossing several multiples of the threshold produces a BURST of
// several same-timestamp events (one per threshold-worth of change),
// not one event carrying a bigger "amount" — real DVS pixels have no
// concept of event magnitude, only polarity and timing, which is
// exactly what gives them their microsecond temporal resolution and
// their very high dynamic range (the literature's commonly cited
// figures: ~140dB dynamic range vs ~60dB for a conventional camera,
// temporal resolution on the order of microseconds).
//
// This module only simulates ONE pixel's mechanism (not multi-pixel
// sensor array timing/readout, which is a separate, much larger
// hardware-interconnect concern outside this repo's scope) — a
// disclosed, deliberate scope limit, not an oversight.

/**
 * Simulates one pixel's event output from a sequence of intensity
 * samples. `samples` must be sorted by `t` ascending and each
 * `intensity` must be a positive finite number (log-intensity is
 * undefined at/below zero, matching the real photodiode + logarithmic
 * preamplifier this models). Returns events IN ORDER: [{t, polarity}],
 * polarity is 'ON' (brightness increased past threshold) or 'OFF'
 * (decreased). No event is ever generated for the very first sample —
 * it establishes the pixel's initial reference, exactly as a real
 * pixel's very first reading does at power-on.
 */
// upgrade 11 bounded-iteration audit finding: the burst while-loops
// below are bounded by (log-intensity jump / threshold), which is
// FINITE for any finite input but NOT bounded by an independent
// constant -- an adversarial-but-valid input (an extreme intensity
// ratio against a tiny threshold) could burst millions of events from
// a single sample pair, unbounded memory/time. MAX_EVENTS_TOTAL caps
// the whole simulation; MAX_BURST_PER_TRANSITION caps a single
// sample-to-sample jump specifically, so the failure is attributable
// to the one pathological transition, not just "ran out of budget
// somewhere." Both throw a specific, catchable error rather than
// silently truncating the event log (which would be indistinguishable
// from "the real mechanism only produced this many events" -- exactly
// the kind of silent wrong-answer this repo's honesty discipline
// exists to prevent).
const MAX_EVENTS_TOTAL = 200000;
const MAX_BURST_PER_TRANSITION = 100000;

export function simulatePixelEvents(samples, { threshold }) {
  if (!Number.isFinite(threshold) || threshold <= 0) throw new Error('eventCameraPixel: threshold must be a positive finite number');
  if (!Array.isArray(samples) || samples.length === 0) return [];
  for (const s of samples) {
    if (!Number.isFinite(s.intensity) || s.intensity <= 0) throw new Error(`eventCameraPixel: intensity must be positive and finite, got ${s.intensity}`);
    if (!Number.isFinite(s.t)) throw new Error('eventCameraPixel: every sample needs a finite t');
  }
  for (let i = 1; i < samples.length; i++) {
    if (samples[i].t < samples[i - 1].t) throw new Error('eventCameraPixel: samples must be sorted by t ascending');
  }

  let refLog = Math.log(samples[0].intensity);
  const events = [];
  for (let i = 1; i < samples.length; i++) {
    const { t, intensity } = samples[i];
    const currentLog = Math.log(intensity);
    let burstCount = 0;
    // A burst: fire once per full threshold-worth of drift, resetting
    // the reference by exactly +/-threshold each time (not snapping to
    // currentLog) -- this is the actual per-pixel comparator behavior,
    // not a simplification of it.
    while (currentLog - refLog >= threshold) {
      events.push({ t, polarity: 'ON' }); refLog += threshold; burstCount++;
      if (burstCount > MAX_BURST_PER_TRANSITION) throw new Error(`eventCameraPixel: single transition at t=${t} would burst more than MAX_BURST_PER_TRANSITION=${MAX_BURST_PER_TRANSITION} events -- intensity ratio too extreme relative to threshold for this instrument's bounded-execution guarantee`);
      if (events.length > MAX_EVENTS_TOTAL) throw new Error(`eventCameraPixel: simulation exceeded MAX_EVENTS_TOTAL=${MAX_EVENTS_TOTAL} events`);
    }
    burstCount = 0;
    while (refLog - currentLog >= threshold) {
      events.push({ t, polarity: 'OFF' }); refLog -= threshold; burstCount++;
      if (burstCount > MAX_BURST_PER_TRANSITION) throw new Error(`eventCameraPixel: single transition at t=${t} would burst more than MAX_BURST_PER_TRANSITION=${MAX_BURST_PER_TRANSITION} events -- intensity ratio too extreme relative to threshold for this instrument's bounded-execution guarantee`);
      if (events.length > MAX_EVENTS_TOTAL) throw new Error(`eventCameraPixel: simulation exceeded MAX_EVENTS_TOTAL=${MAX_EVENTS_TOTAL} events`);
    }
  }
  return events;
}

function eventsEqual(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (a[i].t !== b[i].t || a[i].polarity !== b[i].polarity) return false;
  }
  return true;
}

function firstMismatch(claimed, real) {
  const n = Math.max(claimed.length, real.length);
  for (let i = 0; i < n; i++) {
    const c = claimed[i];
    const r = real[i];
    if (!c) return { index: i, claimed: null, real: r, reason: `claimed log ends at ${claimed.length} events, but the real mechanism produces at least ${i + 1}` };
    if (!r) return { index: i, claimed: c, real: null, reason: `claimed log has ${claimed.length} events, but the real mechanism produces only ${i}` };
    if (c.t !== r.t || c.polarity !== r.polarity) return { index: i, claimed: c, real: r, reason: `event ${i}: claimed {t:${c.t},polarity:${c.polarity}} but the real mechanism produces {t:${r.t},polarity:${r.polarity}}` };
  }
  return null;
}

export function normalizeEventCameraSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Event camera spec is not an object');
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };
  if (raw.kind !== 'event_camera_check') throw new Error(`Unknown event camera spec kind "${raw.kind}"`);

  const MAX_SAMPLES = 5000;
  const samplesRaw = Array.isArray(raw.intensitySamples) ? raw.intensitySamples.slice(0, MAX_SAMPLES) : [];
  if (samplesRaw.length < 2) throw new Error('intensitySamples needs at least 2 samples (a reference plus at least one more)');
  const intensitySamples = samplesRaw.map((s) => ({ t: Number(s.t), intensity: Number(s.intensity) }));

  const threshold = Number(raw.threshold);
  if (!Number.isFinite(threshold) || threshold <= 0) throw new Error('threshold must be a positive finite number');

  const claimedEventsRaw = Array.isArray(raw.claimedEvents) ? raw.claimedEvents.slice(0, MAX_SAMPLES * 50) : [];
  const claimedEvents = claimedEventsRaw.map((e) => ({ t: Number(e.t), polarity: e.polarity === 'ON' ? 'ON' : e.polarity === 'OFF' ? 'OFF' : (() => { throw new Error(`claimed event polarity must be "ON" or "OFF", got "${e.polarity}"`); })() }));

  return { kind: 'event_camera_check', intensitySamples, threshold, claimedEvents, note: String(raw.note || '').slice(0, 300) };
}

export function verifyEventLog(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason };

  let realEvents;
  try {
    realEvents = simulatePixelEvents(spec.intensitySamples, { threshold: spec.threshold });
  } catch (err) {
    // Same discipline as dynamicsCheck.js catching RK45's maxSteps
    // exception, and satKernel returning 'undecided' on budget
    // exhaustion: hitting this instrument's bounded-execution ceiling
    // is an honest "cannot decide," never a crash and never silently
    // reinterpreted as a verdict either way.
    return { verdict: 'inconclusive', reason: `simulation hit its bounded-execution limit: ${err.message}` };
  }
  const held = eventsEqual(spec.claimedEvents, realEvents);
  if (held) {
    return {
      verdict: 'held',
      eventCount: realEvents.length,
      honesty: `The claimed ${realEvents.length}-event log matches EXACTLY what the real per-pixel threshold-crossing mechanism produces from the given intensity signal and threshold — a deterministic re-simulation, not a statistical comparison.`,
    };
  }
  const mismatch = firstMismatch(spec.claimedEvents, realEvents);
  return {
    verdict: 'violated',
    claimedEventCount: spec.claimedEvents.length,
    realEventCount: realEvents.length,
    firstMismatch: mismatch,
    honesty: `The claimed event log does NOT match the real mechanism's output: ${mismatch.reason}. Re-simulated deterministically from the same intensity signal and threshold the claim itself specified.`,
  };
}

export function buildEventCameraPrompt(node) {
  return `Design an EVENT-CAMERA PIXEL VERIFICATION spec for this claim: check a claimed sequence of per-pixel DVS-style events against the real threshold-crossing mechanism, given an intensity signal over time.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Return ONLY JSON, one of:
{"kind":"event_camera_check","intensitySamples":[{"t":<number>,"intensity":<positive number>}, ...],"threshold":<positive number, contrast threshold in natural-log units>,"claimedEvents":[{"t":<number>,"polarity":"ON"|"OFF"}, ...],"note":"what this checks"}
{"kind":"none","reason":"why this claim cannot be reduced to a checkable per-pixel event log"}

Rules: intensitySamples must be sorted by t ascending, at least 2 samples, every intensity strictly positive (log-intensity is undefined otherwise). threshold is the CONTRAST threshold in natural-log units (a typical real sensor's threshold is roughly 0.1-0.5 in these units, corresponding to roughly 10-70% relative intensity change — do not invent a threshold wildly outside that if the claim doesn't specify one explicitly). claimedEvents is whatever event log the claim asserts the pixel produced; this instrument re-simulates the real mechanism from intensitySamples+threshold and checks it against exactly that.`;
}
