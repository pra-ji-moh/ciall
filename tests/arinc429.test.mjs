// arinc429.test.mjs; upgrade 14 — validates the ARINC 429 word codec
// against HAND-COMPUTED literal bit patterns first (same discipline as
// fixProtocol.test.mjs/msgpack.test.mjs/hpack.test.mjs: a round-trip
// alone can pass with a shared bug in encode and decode; a literal
// expected value, computed independently by hand, cannot).
//
// Hand computation for vector 1 (label=131, sdi=0, data=5, ssm=0):
//   wordWithoutParity = label | (sdi<<8) | (data<<10) | (ssm<<29)
//                      = 131 | 0 | (5<<10=5120) | 0 = 5251
//   5251 in binary has bits {0,1,7,10,12} set (1+2+128+1024+4096=5251)
//     -> popcount = 5 (ODD) -> parity bit must be 0 to keep the total
//        odd (5+0=5, still odd).
//   word = 5251 | (0<<31) = 5251 = 0x00001483
//
// Hand computation for vector 2 (label=131, sdi=0, data=4, ssm=0):
//   wordWithoutParity = 131 | 0 | (4<<10=4096) | 0 = 4227
//   4227 in binary has bits {0,1,7,12} set (1+2+128+4096=4227)
//     -> popcount = 4 (EVEN) -> parity bit must be 1 to make the total
//        odd (4+1=5).
//   word = 4227 | (1<<31) = 4227 + 2147483648 = 2147487875 = 0x80001083

import test from 'node:test';
import assert from 'node:assert/strict';
import {
  encodeArinc429Word, decodeArinc429Word, octalLabelToByte, encodeBnrData, decodeBnrData,
} from '../src/lib/arinc429.js';

test('encodeArinc429Word produces the exact hand-computed word for vector 1 (odd popcount -> parity=0)', () => {
  const word = encodeArinc429Word({ label: 131, sdi: 0, data: 5, ssm: 0 });
  assert.equal(word, 5251);
  assert.equal(word, 0x00001483);
});

test('encodeArinc429Word produces the exact hand-computed word for vector 2 (even popcount -> parity=1)', () => {
  const word = encodeArinc429Word({ label: 131, sdi: 0, data: 4, ssm: 0 });
  assert.equal(word, 2147487875);
  assert.equal(word, 0x80001083);
});

test('decodeArinc429Word recovers the exact original fields from both hand-computed literal words', () => {
  const d1 = decodeArinc429Word(5251);
  assert.deepEqual({ label: d1.label, sdi: d1.sdi, data: d1.data, ssm: d1.ssm, parityBit: d1.parityBit }, { label: 131, sdi: 0, data: 5, ssm: 0, parityBit: 0 });
  assert.equal(d1.parityValid, true);

  const d2 = decodeArinc429Word(2147487875);
  assert.deepEqual({ label: d2.label, sdi: d2.sdi, data: d2.data, ssm: d2.ssm, parityBit: d2.parityBit }, { label: 131, sdi: 0, data: 4, ssm: 0, parityBit: 1 });
  assert.equal(d2.parityValid, true);
});

test('decodeArinc429Word detects a corrupted parity bit -- recomputes rather than trusting the word\'s own claim', () => {
  // JS's `<<`/`^` operators treat operands as SIGNED 32-bit integers, so
  // `1 << 31` alone is -2147483648; `>>> 0` coerces the XOR result back
  // to the unsigned 32-bit value this module's words are always
  // represented as (this is a test-arithmetic detail, not a library bug
  // -- the library itself is careful about this exact pitfall via `>>> 0`
  // at every point that matters, see its own source).
  const corrupted = (5251 ^ (1 << 31)) >>> 0; // flip the parity bit of a known-good word
  const decoded = decodeArinc429Word(corrupted);
  assert.equal(decoded.parityValid, false);
  // Fields other than parity are still correctly extracted -- a
  // parity failure is reported as a finding, not silently hidden by
  // refusing to decode at all.
  assert.equal(decoded.label, 131);
});

test('decodeArinc429Word detects a corrupted data bit via parity', () => {
  const corrupted = 5251 ^ (1 << 10); // flip the lowest data bit
  const decoded = decodeArinc429Word(corrupted);
  assert.equal(decoded.parityValid, false);
});

test('round trip: every field survives encode -> decode unchanged, across many combinations', () => {
  const cases = [
    { label: 0, sdi: 0, data: 0, ssm: 0 },
    { label: 255, sdi: 3, data: 524287, ssm: 3 },
    { label: 27, sdi: 2, data: 300000, ssm: 1 },
    { label: 173, sdi: 1, data: 1, ssm: 2 },
  ];
  for (const c of cases) {
    const word = encodeArinc429Word(c);
    const decoded = decodeArinc429Word(word);
    assert.equal(decoded.label, c.label);
    assert.equal(decoded.sdi, c.sdi);
    assert.equal(decoded.data, c.data);
    assert.equal(decoded.ssm, c.ssm);
    assert.equal(decoded.parityValid, true, `parity should validate for ${JSON.stringify(c)}`);
  }
});

test('encodeArinc429Word rejects every field outside its real bit width, rather than silently truncating', () => {
  assert.throws(() => encodeArinc429Word({ label: 256, sdi: 0, data: 0, ssm: 0 }), /label must be an integer 0-255/);
  assert.throws(() => encodeArinc429Word({ label: -1, sdi: 0, data: 0, ssm: 0 }), /label must be an integer 0-255/);
  assert.throws(() => encodeArinc429Word({ label: 0, sdi: 4, data: 0, ssm: 0 }), /sdi must be an integer 0-3/);
  assert.throws(() => encodeArinc429Word({ label: 0, sdi: 0, data: 524288, ssm: 0 }), /data must be an integer 0-524287/);
  assert.throws(() => encodeArinc429Word({ label: 0, sdi: 0, data: 0, ssm: 4 }), /ssm must be an integer 0-3/);
});

test('decodeArinc429Word rejects a non-32-bit-unsigned input', () => {
  assert.throws(() => decodeArinc429Word(-1), /unsigned 32-bit integer/);
  assert.throws(() => decodeArinc429Word(0x100000000), /unsigned 32-bit integer/);
});

// ---- octal label convention ---------------------------------------------

test('octalLabelToByte converts a real, conventionally-written octal label correctly', () => {
  // 203 octal = 2*64 + 0*8 + 3 = 131 decimal -- the same label value
  // used throughout the hand-computed vectors above.
  assert.equal(octalLabelToByte('203'), 131);
  assert.equal(octalLabelToByte('0'), 0);
  assert.equal(octalLabelToByte('377'), 255); // max 8-bit octal label
});

test('octalLabelToByte rejects a non-octal or out-of-range string', () => {
  assert.throws(() => octalLabelToByte('89'), /1-3 octal digits/); // 8,9 are not octal digits
  assert.throws(() => octalLabelToByte('400'), /exceeds the 8-bit label field/); // 0o400 = 256
});

// ---- BNR data encoding ---------------------------------------------------

test('encodeBnrData/decodeBnrData round-trip within a small, real, bounded quantization error', () => {
  const fullScale = 400; // e.g. airspeed in knots
  for (const value of [0, 100, -100, 399.9, -399.9, 1]) {
    const data = encodeBnrData(value, fullScale);
    const decoded = decodeBnrData(data, fullScale);
    // 18 bits of magnitude resolution over a range of `fullScale` gives
    // a worst-case quantization step of fullScale / (2^18-1) -- assert
    // against exactly that real bound, not an arbitrary loose tolerance.
    const maxError = fullScale / ((1 << 18) - 1);
    assert.ok(Math.abs(decoded - value) <= maxError + 1e-9, `value ${value} decoded to ${decoded}, error exceeds the real quantization bound ${maxError}`);
  }
});

test('encodeBnrData correctly sets the sign bit for negative values', () => {
  const data = encodeBnrData(-50, 100);
  const magnitudeBits = 18;
  assert.equal((data >>> magnitudeBits) & 1, 1);
});

test('encodeBnrData rejects a value outside +/-fullScaleRange', () => {
  assert.throws(() => encodeBnrData(150, 100), /exceeds fullScaleRange/);
});

// ---- end-to-end: a real word carrying a real BNR-encoded value --------

test('end-to-end: a full ARINC 429 word carrying a BNR-encoded airspeed value round-trips correctly through encode/decode', () => {
  const airspeedKnots = 250.5;
  const fullScale = 400;
  const data = encodeBnrData(airspeedKnots, fullScale);
  const word = encodeArinc429Word({ label: octalLabelToByte('206'), sdi: 0, data, ssm: 0 }); // label 206 octal is a real, commonly-cited ARINC 429 computed-airspeed label
  const decoded = decodeArinc429Word(word);
  assert.equal(decoded.parityValid, true);
  assert.equal(decoded.label, octalLabelToByte('206'));
  const recoveredAirspeed = decodeBnrData(decoded.data, fullScale);
  const maxError = fullScale / ((1 << 18) - 1);
  assert.ok(Math.abs(recoveredAirspeed - airspeedKnots) <= maxError + 1e-9);
});
