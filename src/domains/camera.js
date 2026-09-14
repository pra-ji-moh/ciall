// domains/camera.js; camera capture as a real, built-in Ciall capability
// — not a one-off test. This does NOT bypass anything: capturing a frame
// is just another commandExecutor.runCommand() call, subject to the
// exact same scope+confirm discipline as running any other external
// program in this repo. No new execution path, no privileged bypass.
//
// WHY THIS IS SEPARATE FROM motion.js: motion.js verifies data that
// already exists (observed samples, wherever they came from). This file
// is the part that PRODUCES that data, by invoking an external capture
// tool (ffmpeg — not bundled; the caller supplies its path, exactly like
// API keys are supplied by the caller rather than baked in) through the
// existing permission-gated executor. Building this file does not itself
// touch a camera — it defines HOW to, through the same confirm-gate
// everything else uses; the actual capture only happens when a caller
// with real, granted permission invokes it.

import { runCommand } from '../lib/commandExecutor.js';
import { readJpegDimensions } from '../lib/imageMetadata.js';
import { computeFrameDifference, triggerOnFrameChange } from '../lib/frameDifference.js';

export { computeFrameDifference, triggerOnFrameChange };

/**
 * Captures ONE still frame from a named DirectShow (Windows) video
 * device via ffmpeg, through the existing confirm-gated command
 * executor. Get the exact device name from
 * `ffmpeg -f dshow -list_devices true -i dummy`.
 *
 * `outputPath` MUST be inside the sandbox — this function does not
 * itself enforce that; commandExecutor does not scope a command's own
 * file arguments (see SECURITY.md's stated limit: a process-launch grant
 * controls which binary runs, not what it does with its own arguments).
 * The caller is responsible for choosing a sandboxed output path.
 */
export async function captureStillImage({ ffmpegPath, device, outputPath, confirm, requestedBy = 'unknown' }) {
  return runCommand(
    ffmpegPath,
    ['-f', 'dshow', '-i', `video=${device}`, '-frames:v', '1', '-y', outputPath],
    { confirm, requestedBy },
  );
}

/**
 * Verifies a claim about a captured image's actual resolution — the
 * "capture, then check a genuine claim about it" pattern, using pure
 * Buffer parsing (src/lib/imageMetadata.js), zero image-decoding
 * dependency, consistent with this substrate's zero-npm-dependency
 * design.
 */
export function verifyImageResolutionClaim({ jpegBuffer, claimedWidth, claimedHeight }) {
  const actual = readJpegDimensions(jpegBuffer);
  const matches = actual.width === claimedWidth && actual.height === claimedHeight;
  return {
    verdict: matches ? 'matched' : 'diverged',
    actual,
    claimed: { width: claimedWidth, height: claimedHeight },
    honesty: matches
      ? `Actual resolution ${actual.width}x${actual.height} matches the claimed ${claimedWidth}x${claimedHeight} exactly.`
      : `Actual resolution ${actual.width}x${actual.height} does NOT match the claimed ${claimedWidth}x${claimedHeight} — a concrete, checkable divergence.`,
  };
}
