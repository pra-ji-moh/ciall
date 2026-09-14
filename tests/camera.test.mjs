// camera.test.mjs; captureStillImage tests NEVER invoke a real camera —
// confirm() always declines (or the executable is out of scope), so no
// process actually spawns. What's verified is that this file builds the
// correct ffmpeg invocation and goes through the exact same
// scope+confirm discipline as every other commandExecutor caller, not
// that ffmpeg or a camera actually works.

import test from 'node:test';
import assert from 'node:assert/strict';
import { captureStillImage, verifyImageResolutionClaim } from '../src/domains/camera.js';
import { grant, revoke } from '../src/lib/deviceGate.js';

const future = () => Date.now() + 60_000;

const REAL_JPEG_320x240 = Buffer.from(
  '/9j/4AAQSkZJRgABAgAAAQABAAD//gAQTGF2YzYyLjI4LjEwMgD/2wBDAAgEBAQEBAUFBQUFBQYGBgYGBgYGBgYGBgYHBwcICAgHBwcGBgcHCAgICAkJCQgICAgJCQoKCgwMCwsODg4RERT/xABNAAEBAAAAAAAAAAAAAAAAAAAABwEBAQEAAAAAAAAAAAAAAAAAAAUHEAEAAAAAAAAAAAAAAAAAAAAAEQEAAAAAAAAAAAAAAAAAAAAA/8AAEQgA8AFAAwEiAAIRAAMRAP/aAAwDAQACEQMRAD8AjgDf0oAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB/9k=',
  'base64',
);

test('captureStillImage requires confirm() -- throws before touching anything (same contract as every other commandExecutor caller)', async () => {
  await assert.rejects(
    () => captureStillImage({ ffmpegPath: 'ffmpeg', device: 'Integrated Webcam', outputPath: 'out.jpg' }),
    /requires an explicit confirm/,
  );
});

test('captureStillImage is refused with no active grant, and confirm() is never reached', async () => {
  let confirmCalled = false;
  const result = await captureStillImage({
    ffmpegPath: 'ffmpeg-not-granted', device: 'Integrated Webcam', outputPath: 'out.jpg',
    confirm: async () => { confirmCalled = true; return true; },
  });
  assert.equal(result.executed, false);
  assert.match(result.reason, /no active grant/);
  assert.equal(confirmCalled, false, 'no camera invocation is attempted for an out-of-scope executable');
});

test('captureStillImage builds the correct ffmpeg argv, and a decline means nothing spawns', async () => {
  grant({ id: 'camera-test', scope: 'process-launch', boundary: ['ffmpeg-camera-test'], expiresAt: future(), grantedBy: 'test' });
  let seenAction = null;
  const result = await captureStillImage({
    ffmpegPath: 'ffmpeg-camera-test', device: 'Integrated Webcam', outputPath: 'C:\\sandbox\\out.jpg',
    confirm: async (action) => { seenAction = action; return false; }, // decline -- nothing actually spawns
  });
  assert.equal(result.executed, false);
  assert.match(result.reason, /declined/);
  assert.deepEqual(seenAction.args, ['-f', 'dshow', '-i', 'video=Integrated Webcam', '-frames:v', '1', '-y', 'C:\\sandbox\\out.jpg']);
  assert.equal(seenAction.executable, 'ffmpeg-camera-test');
  revoke('camera-test');
});

test('verifyImageResolutionClaim correctly matches a real captured resolution', () => {
  const result = verifyImageResolutionClaim({ jpegBuffer: REAL_JPEG_320x240, claimedWidth: 320, claimedHeight: 240 });
  assert.equal(result.verdict, 'matched');
});

test('verifyImageResolutionClaim correctly catches a false claim about the real image', () => {
  const result = verifyImageResolutionClaim({ jpegBuffer: REAL_JPEG_320x240, claimedWidth: 1920, claimedHeight: 1080 });
  assert.equal(result.verdict, 'diverged');
  assert.deepEqual(result.actual, { width: 320, height: 240 });
});
