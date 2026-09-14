// imageMetadata.test.mjs; REAL_JPEG_320x240 below is an actual, valid
// JPEG (generated once via `ffmpeg -f lavfi -i color=c=blue:s=320x240
// -frames:v 1`, a synthetic color source — no camera, no hardware
// involved in producing this fixture), embedded so this test suite never
// depends on ffmpeg being installed on whatever machine runs it.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readJpegDimensions } from '../src/lib/imageMetadata.js';

const REAL_JPEG_320x240 = Buffer.from(
  '/9j/4AAQSkZJRgABAgAAAQABAAD//gAQTGF2YzYyLjI4LjEwMgD/2wBDAAgEBAQEBAUFBQUFBQYGBgYGBgYGBgYGBgYHBwcICAgHBwcGBgcHCAgICAkJCQgICAgJCQoKCgwMCwsODg4RERT/xABNAAEBAAAAAAAAAAAAAAAAAAAABwEBAQEAAAAAAAAAAAAAAAAAAAUHEAEAAAAAAAAAAAAAAAAAAAAAEQEAAAAAAAAAAAAAAAAAAAAA/8AAEQgA8AFAAwEiAAIRAAMRAP/aAAwDAQACEQMRAD8AjgDf0oAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAB/9k=',
  'base64',
);

test('readJpegDimensions reads the real, correct dimensions from an actual JPEG', () => {
  const dims = readJpegDimensions(REAL_JPEG_320x240);
  assert.deepEqual(dims, { width: 320, height: 240 });
});

test('readJpegDimensions rejects a buffer that is not a JPEG at all', () => {
  assert.throws(() => readJpegDimensions(Buffer.from('not a jpeg')), /missing SOI marker/);
});

test('readJpegDimensions rejects a JPEG with SOI and a harmless segment but no SOF at all', () => {
  // SOI, then an APP0 marker whose 2-byte length field covers only itself (no payload, no SOF)
  const noSof = Buffer.from([0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x02]);
  assert.throws(() => readJpegDimensions(noSof), /no SOF marker found/);
});

test('readJpegDimensions requires an actual Buffer, not a plain array or string', () => {
  assert.throws(() => readJpegDimensions([0xFF, 0xD8]), /needs a Buffer/);
  assert.throws(() => readJpegDimensions('not a buffer'), /needs a Buffer/);
});

test('readJpegDimensions rejects a JPEG truncated right at the SOF marker (no room for dimension bytes)', () => {
  // SOI + a minimal SOF0 marker header with NO dimension bytes following
  const truncated = Buffer.from([0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x02]);
  assert.throws(() => readJpegDimensions(truncated), /truncated JPEG/);
});
