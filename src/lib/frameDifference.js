// frameDifference.js; pixel-level frame differencing as a TRIGGER for the
// existing verification kernels — not a vision system that identifies or
// tracks anything. NEW logic, stated plainly: pure arithmetic over raw
// pixel buffers, no ML, no object/face/person detection of any kind, no
// external dependency.
//
// THE BOUNDARY THAT MATTERS. This computes ONE thing: how much two
// frames differ, numerically, byte by byte. It does not know or care
// what changed, whether it was a person, an object, or lighting — it has
// no concept of "what" at all, only "how much." That's deliberate: this
// substrate verifies CLAIMS via kernels; it does not do scene
// understanding. The intended use is exactly what was asked for —
// "trigger the kernels on pixel change" — meaning frame differencing
// decides WHEN something is worth checking, and the existing kernels
// (unmodified) decide what the check actually finds. Vision detects
// change; kernels verify claims. Those stay two different jobs.
//
// Buffers must be raw, same-length, same-format pixel data (e.g. from
// ffmpeg's `-f rawvideo -pix_fmt gray` output) — not JPEG files. This
// file does not decode any image format; that's out of scope, same as
// imageMetadata.js only reads a JPEG's header, never its pixels.

export function computeFrameDifference(bufferA, bufferB) {
  if (!Buffer.isBuffer(bufferA) || !Buffer.isBuffer(bufferB)) {
    throw new Error('computeFrameDifference needs two raw pixel Buffers');
  }
  if (bufferA.length !== bufferB.length) {
    throw new Error(`frame buffers must be the same length (got ${bufferA.length} and ${bufferB.length}) — same resolution/format required`);
  }
  if (bufferA.length === 0) throw new Error('frame buffers must not be empty');

  let sumAbsDiff = 0;
  let maxDiff = 0;
  let maxDiffOffset = -1;
  for (let i = 0; i < bufferA.length; i++) {
    const diff = Math.abs(bufferA[i] - bufferB[i]);
    sumAbsDiff += diff;
    if (diff > maxDiff) { maxDiff = diff; maxDiffOffset = i; }
  }

  return {
    byteCount: bufferA.length,
    sumAbsDiff,
    meanAbsDiff: sumAbsDiff / bufferA.length,
    maxDiff,
    maxDiffOffset, // byte offset of the single largest change; NOT a spatial coordinate or an object location
  };
}

/**
 * Computes the difference and calls `onTrigger(diff)` only if
 * meanAbsDiff exceeds `threshold`. Vision's entire job ends here — at
 * "something changed by this much." Everything downstream (what it
 * means, whether it matters, whether a claim now needs checking) is
 * `onTrigger`'s job, never this function's; it does not interpret the
 * change, it only measures it and hands off.
 */
export function triggerOnFrameChange(bufferA, bufferB, threshold, onTrigger) {
  if (!(threshold >= 0)) throw new Error('threshold must be a non-negative number');
  if (typeof onTrigger !== 'function') throw new Error('triggerOnFrameChange needs an onTrigger(diff) callback');

  const diff = computeFrameDifference(bufferA, bufferB);
  const triggered = diff.meanAbsDiff > threshold;
  if (triggered) onTrigger(diff);
  return { triggered, diff };
}
