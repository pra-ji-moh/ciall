// arinc429.js; upgrade 14 — a real ARINC 429 word codec, one concrete
// answer to "no real-time avionics bus integration" (named in this
// project's own PLTR-mechanism comparison and again when asked what
// would go wrong equipping this into an aircraft). ARINC 429 is the
// standard civil avionics data bus — used across Boeing, Airbus, and
// most transport-category aircraft — publicly documented, not an
// export-controlled military standard (unlike STANAG/Link-16, declined
// earlier in this project for exactly that reason). Legitimately
// buildable.
//
// SCOPE, disclosed plainly: this implements the WORD FORMAT (the
// 32-bit label/SDI/data/SSM/parity layout every ARINC 429 message
// uses) — not a physical-layer driver (this repo has no serial
// hardware access), not the full label dictionary (which label means
// what is aircraft/system-specific, assigned per ARINC 429's own
// attachment tables and the integrating OEM's ICD), and not BCD or
// discrete data-field sub-encodings (only the raw 19-bit data field
// and a BNR — Binary, the most common numeric sub-format — helper are
// implemented). Same scoping discipline as fixProtocol.js in upgrade
// 12: the wire format, validated against a hand-computed bit pattern,
// not the entire application-level standard.
//
// BIT LAYOUT (0-indexed from the LSB, matching how this file treats a
// word as a plain 32-bit unsigned integer — ARINC's own convention
// numbers these 1-32 from the LSB; bit N here is ARINC's "bit N+1"):
//   bits 0-7   (8 bits)  Label
//   bits 8-9   (2 bits)  SDI (Source/Destination Identifier)
//   bits 10-28 (19 bits) Data
//   bits 29-30 (2 bits)  SSM (Sign/Status Matrix)
//   bit 31     (1 bit)   Parity (odd parity over the other 31 bits)

const LABEL_BITS = 8;
const SDI_BITS = 2;
const DATA_BITS = 19;
const SSM_BITS = 2;
const DATA_MAX = (1 << DATA_BITS) - 1; // 524287

function popcount32(x) {
  let count = 0;
  let v = x >>> 0;
  // Bounded at 32 iterations by construction (a 32-bit value has at
  // most 32 set bits) -- no external bound needed.
  while (v !== 0) {
    count += v & 1;
    v >>>= 1;
  }
  return count;
}

/** The parity BIT value (0 or 1) that makes the total 1-bit count across `wordWithoutParity` PLUS this bit odd. */
function oddParityBit(wordWithoutParity) {
  return popcount32(wordWithoutParity) % 2 === 0 ? 1 : 0;
}

/**
 * Encodes `{label, sdi, data, ssm}` into a real 32-bit ARINC 429 word
 * (returned as an unsigned integer via `>>> 0`), with a correctly
 * computed odd-parity bit. Throws on any field outside its real bit
 * width rather than silently truncating it — a silently truncated
 * label or data field is exactly the kind of wrong-but-plausible value
 * this repo's discipline exists to catch before it ships, not after.
 */
export function encodeArinc429Word({ label, sdi, data, ssm }) {
  if (!Number.isInteger(label) || label < 0 || label > 0xFF) throw new Error(`arinc429: label must be an integer 0-255 (8 bits), got ${label}`);
  if (!Number.isInteger(sdi) || sdi < 0 || sdi > 0b11) throw new Error(`arinc429: sdi must be an integer 0-3 (2 bits), got ${sdi}`);
  if (!Number.isInteger(data) || data < 0 || data > DATA_MAX) throw new Error(`arinc429: data must be an integer 0-${DATA_MAX} (19 bits), got ${data}`);
  if (!Number.isInteger(ssm) || ssm < 0 || ssm > 0b11) throw new Error(`arinc429: ssm must be an integer 0-3 (2 bits), got ${ssm}`);

  const withoutParity = (label | (sdi << LABEL_BITS) | (data << (LABEL_BITS + SDI_BITS)) | (ssm << (LABEL_BITS + SDI_BITS + DATA_BITS))) >>> 0;
  const parity = oddParityBit(withoutParity);
  return (withoutParity | (parity << 31)) >>> 0;
}

/**
 * Decodes a 32-bit ARINC 429 word into its fields, and independently
 * RECOMPUTES the expected parity bit rather than trusting the word's
 * own claimed parity — same "recompute, never trust the message's own
 * claim" discipline as fixProtocol.js's CheckSum/BodyLength validation.
 * `parityValid: false` signals a corrupted or malformed word; the
 * caller decides what to do with that, same as every other kernel in
 * this repo surfacing a finding rather than silently discarding it.
 */
export function decodeArinc429Word(word) {
  if (!Number.isInteger(word) || word < 0 || word > 0xFFFFFFFF) throw new Error(`arinc429: word must be an unsigned 32-bit integer, got ${word}`);
  const w = word >>> 0;

  const label = w & 0xFF;
  const sdi = (w >>> LABEL_BITS) & 0b11;
  const data = (w >>> (LABEL_BITS + SDI_BITS)) & DATA_MAX;
  const ssm = (w >>> (LABEL_BITS + SDI_BITS + DATA_BITS)) & 0b11;
  const parityBit = (w >>> 31) & 1;
  const withoutParity = w & 0x7FFFFFFF;
  const expectedParity = oddParityBit(withoutParity);

  return { label, sdi, data, ssm, parityBit, parityValid: parityBit === expectedParity };
}

/**
 * Converts an ARINC 429 label written in its conventional OCTAL form
 * (labels are universally documented and spoken as octal, e.g. "203")
 * to the plain byte value this module's `label` field expects.
 */
export function octalLabelToByte(octalString) {
  if (typeof octalString !== 'string' || !/^[0-7]{1,3}$/.test(octalString)) throw new Error(`arinc429: label must be 1-3 octal digits, got "${octalString}"`);
  const value = parseInt(octalString, 8);
  if (value > 0xFF) throw new Error(`arinc429: octal label "${octalString}" (${value}) exceeds the 8-bit label field`);
  return value;
}

/**
 * BNR (Binary) data-field encoding: maps a real-valued `value` within
 * [-fullScaleRange, +fullScaleRange] to the 19-bit signed-magnitude-ish
 * fractional format ARINC 429's BNR convention uses — bit 28 (the
 * data field's own MSB) as sign (1 = negative, matching ARINC's
 * convention), the remaining 18 bits as a fraction of fullScaleRange.
 * Lossy by construction (18 bits of resolution) — `decodeBnrData`'s
 * round-trip test documents the real, bounded quantization error this
 * introduces, not a false claim of exactness.
 */
export function encodeBnrData(value, fullScaleRange) {
  if (!Number.isFinite(value)) throw new Error('arinc429: BNR value must be finite');
  if (!Number.isFinite(fullScaleRange) || fullScaleRange <= 0) throw new Error('arinc429: fullScaleRange must be a positive finite number');
  if (Math.abs(value) > fullScaleRange) throw new Error(`arinc429: value ${value} exceeds fullScaleRange +/-${fullScaleRange}`);

  const magnitudeBits = DATA_BITS - 1; // 18 bits of magnitude, 1 sign bit
  const maxMagnitude = (1 << magnitudeBits) - 1;
  const sign = value < 0 ? 1 : 0;
  const magnitude = Math.round((Math.abs(value) / fullScaleRange) * maxMagnitude);
  return (magnitude | (sign << magnitudeBits)) >>> 0;
}

/** Inverse of encodeBnrData; see its header for the real, bounded quantization error this introduces. */
export function decodeBnrData(data, fullScaleRange) {
  if (!Number.isInteger(data) || data < 0 || data > DATA_MAX) throw new Error(`arinc429: data must be an integer 0-${DATA_MAX}`);
  if (!Number.isFinite(fullScaleRange) || fullScaleRange <= 0) throw new Error('arinc429: fullScaleRange must be a positive finite number');

  const magnitudeBits = DATA_BITS - 1;
  const maxMagnitude = (1 << magnitudeBits) - 1;
  const sign = (data >>> magnitudeBits) & 1;
  const magnitude = data & maxMagnitude;
  const value = (magnitude / maxMagnitude) * fullScaleRange;
  return sign === 1 ? -value : value;
}
