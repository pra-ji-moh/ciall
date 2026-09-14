// msgpack.js; upgrade 8 — a from-scratch MessagePack encoder/decoder,
// hand-implemented against the public MessagePack spec
// (github.com/msgpack/msgpack/blob/master/spec.md), no npm dependency.
// Implements exactly the subset the task calls for: nil, bool, int
// (encoded in the smallest spec-compliant fixed-width form, signed or
// unsigned, matching real msgpack encoders' behavior), float64, str,
// array, map. Deliberately does NOT implement bin8/16/32, float32, ext
// types, or timestamp — none of those are needed to carry a kernel
// name + a JSON-shaped claim/result payload, and every kernel spec in
// this repo is built from exactly {null, boolean, number, string,
// array, plain object}.
//
// VALIDATION DISCIPLINE (matching this repo's WASM-module precedent —
// smcWasm.js/mlpWasm.js were both checked against a reference before
// being trusted): every literal byte sequence asserted in
// tests/msgpack.test.mjs is taken directly from the public spec examples
// (nil=0xc0, false=0xc2, true=0xc3, fixstr/fixarray/fixmap prefix
// layout, etc.), not just round-tripped through this module's own
// encode+decode pair — a shared bug in both halves could pass a
// round-trip check while still being spec-non-compliant, so literal
// byte checks matter independently of round-trip checks.

const NIL = 0xc0;
const FALSE = 0xc2;
const TRUE = 0xc3;
const FLOAT64 = 0xcb;
const UINT8 = 0xcc;
const UINT16 = 0xcd;
const UINT32 = 0xce;
const UINT64 = 0xcf;
const INT8 = 0xd0;
const INT16 = 0xd1;
const INT32 = 0xd2;
const INT64 = 0xd3;
const STR8 = 0xd9;
const STR16 = 0xda;
const STR32 = 0xdb;
const ARRAY16 = 0xdc;
const ARRAY32 = 0xdd;
const MAP16 = 0xde;
const MAP32 = 0xdf;

// ============================================================
// ENCODE
// ============================================================

class ByteWriter {
  constructor() {
    this.chunks = [];
  }
  pushByte(b) {
    this.chunks.push(Buffer.from([b & 0xff]));
  }
  pushBytes(buf) {
    this.chunks.push(buf);
  }
  pushUInt8(n) { this.pushByte(n); }
  pushUInt16BE(n) { const b = Buffer.alloc(2); b.writeUInt16BE(n >>> 0); this.pushBytes(b); }
  pushUInt32BE(n) { const b = Buffer.alloc(4); b.writeUInt32BE(n >>> 0); this.pushBytes(b); }
  pushInt8(n) { const b = Buffer.alloc(1); b.writeInt8(n); this.pushBytes(b); }
  pushInt16BE(n) { const b = Buffer.alloc(2); b.writeInt16BE(n); this.pushBytes(b); }
  pushInt32BE(n) { const b = Buffer.alloc(4); b.writeInt32BE(n); this.pushBytes(b); }
  // 64-bit values go through BigInt so values beyond Number's 53-bit
  // safe-integer range (a caller-supplied uint64 wall-clock count could,
  // in principle, exceed it after ~285000 years of milliseconds — not a
  // real concern for this repo, but the encoder should not silently
  // corrupt a value it accepted) still round-trip exactly.
  pushUInt64BE(n) { const b = Buffer.alloc(8); b.writeBigUInt64BE(BigInt(n)); this.pushBytes(b); }
  pushInt64BE(n) { const b = Buffer.alloc(8); b.writeBigInt64BE(BigInt(n)); this.pushBytes(b); }
  pushDoubleBE(n) { const b = Buffer.alloc(8); b.writeDoubleBE(n); this.pushBytes(b); }
  toBuffer() { return Buffer.concat(this.chunks); }
}

function encodeInt(w, n) {
  if (Number.isInteger(n) && !Object.is(n, -0)) {
    if (n >= 0) {
      if (n <= 0x7f) { w.pushByte(n); return; } // positive fixint
      if (n <= 0xff) { w.pushByte(UINT8); w.pushUInt8(n); return; }
      if (n <= 0xffff) { w.pushByte(UINT16); w.pushUInt16BE(n); return; }
      if (n <= 0xffffffff) { w.pushByte(UINT32); w.pushUInt32BE(n); return; }
      w.pushByte(UINT64); w.pushUInt64BE(n); return;
    }
    if (n >= -32) { w.pushByte(0xe0 | (n + 32) & 0x1f); return; } // negative fixint, 5-bit two's complement
    if (n >= -128) { w.pushByte(INT8); w.pushInt8(n); return; }
    if (n >= -32768) { w.pushByte(INT16); w.pushInt16BE(n); return; }
    if (n >= -2147483648) { w.pushByte(INT32); w.pushInt32BE(n); return; }
    w.pushByte(INT64); w.pushInt64BE(n); return;
  }
  // Non-integer number (or -0, which MUST round-trip as a float to
  // preserve its sign — encoding it as integer 0 would silently lose
  // that): float64.
  w.pushByte(FLOAT64); w.pushDoubleBE(n);
}

function encodeStr(w, s) {
  const bytes = Buffer.from(s, 'utf8');
  const len = bytes.length;
  if (len <= 0x1f) { w.pushByte(0xa0 | len); }
  else if (len <= 0xff) { w.pushByte(STR8); w.pushUInt8(len); }
  else if (len <= 0xffff) { w.pushByte(STR16); w.pushUInt16BE(len); }
  else { w.pushByte(STR32); w.pushUInt32BE(len); }
  w.pushBytes(bytes);
}

function encodeArrayHeader(w, len) {
  if (len <= 0x0f) { w.pushByte(0x90 | len); }
  else if (len <= 0xffff) { w.pushByte(ARRAY16); w.pushUInt16BE(len); }
  else { w.pushByte(ARRAY32); w.pushUInt32BE(len); }
}

function encodeMapHeader(w, len) {
  if (len <= 0x0f) { w.pushByte(0x80 | len); }
  else if (len <= 0xffff) { w.pushByte(MAP16); w.pushUInt16BE(len); }
  else { w.pushByte(MAP32); w.pushUInt32BE(len); }
}

function encodeValue(w, value) {
  if (value === null || value === undefined) { w.pushByte(NIL); return; }
  if (value === false) { w.pushByte(FALSE); return; }
  if (value === true) { w.pushByte(TRUE); return; }
  const t = typeof value;
  if (t === 'number') { encodeInt(w, value); return; }
  if (t === 'string') { encodeStr(w, value); return; }
  if (Array.isArray(value)) {
    encodeArrayHeader(w, value.length);
    for (const item of value) encodeValue(w, item);
    return;
  }
  if (t === 'object') {
    const keys = Object.keys(value);
    encodeMapHeader(w, keys.length);
    for (const key of keys) { encodeStr(w, key); encodeValue(w, value[key]); }
    return;
  }
  throw new Error(`msgpack: cannot encode value of type "${t}" (supported: nil, bool, number, string, array, plain object)`);
}

/** Encodes a JS value ({null, boolean, number, string, array, plain object}, arbitrarily nested) into a MessagePack Buffer. */
export function encode(value) {
  const w = new ByteWriter();
  encodeValue(w, value);
  return w.toBuffer();
}

// ============================================================
// DECODE
// ============================================================

class ByteReader {
  constructor(buf) {
    this.buf = buf;
    this.pos = 0;
  }
  readByte() {
    if (this.pos >= this.buf.length) throw new Error('msgpack: unexpected end of buffer');
    return this.buf[this.pos++];
  }
  readBytes(n) {
    if (this.pos + n > this.buf.length) throw new Error('msgpack: unexpected end of buffer');
    const out = this.buf.subarray(this.pos, this.pos + n);
    this.pos += n;
    return out;
  }
  readUInt8() { return this.readBytes(1).readUInt8(0); }
  readUInt16BE() { return this.readBytes(2).readUInt16BE(0); }
  readUInt32BE() { return this.readBytes(4).readUInt32BE(0); }
  readUInt64BE() { return Number(this.readBytes(8).readBigUInt64BE(0)); }
  readInt8() { return this.readBytes(1).readInt8(0); }
  readInt16BE() { return this.readBytes(2).readInt16BE(0); }
  readInt32BE() { return this.readBytes(4).readInt32BE(0); }
  readInt64BE() { return Number(this.readBytes(8).readBigInt64BE(0)); }
  readDoubleBE() { return this.readBytes(8).readDoubleBE(0); }
}

function decodeValue(r) {
  const b = r.readByte();

  if (b === NIL) return null;
  if (b === FALSE) return false;
  if (b === TRUE) return true;
  if (b <= 0x7f) return b; // positive fixint
  if (b >= 0xe0) return b - 256; // negative fixint (0xe0..0xff -> -32..-1)

  if (b === UINT8) return r.readUInt8();
  if (b === UINT16) return r.readUInt16BE();
  if (b === UINT32) return r.readUInt32BE();
  if (b === UINT64) return r.readUInt64BE();
  if (b === INT8) return r.readInt8();
  if (b === INT16) return r.readInt16BE();
  if (b === INT32) return r.readInt32BE();
  if (b === INT64) return r.readInt64BE();
  if (b === FLOAT64) return r.readDoubleBE();

  if ((b & 0xe0) === 0xa0) return r.readBytes(b & 0x1f).toString('utf8'); // fixstr
  if (b === STR8) return r.readBytes(r.readUInt8()).toString('utf8');
  if (b === STR16) return r.readBytes(r.readUInt16BE()).toString('utf8');
  if (b === STR32) return r.readBytes(r.readUInt32BE()).toString('utf8');

  if ((b & 0xf0) === 0x90) return decodeArray(r, b & 0x0f); // fixarray
  if (b === ARRAY16) return decodeArray(r, r.readUInt16BE());
  if (b === ARRAY32) return decodeArray(r, r.readUInt32BE());

  if ((b & 0xf0) === 0x80) return decodeMap(r, b & 0x0f); // fixmap
  if (b === MAP16) return decodeMap(r, r.readUInt16BE());
  if (b === MAP32) return decodeMap(r, r.readUInt32BE());

  throw new Error(`msgpack: unsupported/unrecognized leading byte 0x${b.toString(16).padStart(2, '0')} (this decoder implements only nil/bool/int/float64/str/array/map)`);
}

function decodeArray(r, len) {
  const out = new Array(len);
  for (let i = 0; i < len; i++) out[i] = decodeValue(r);
  return out;
}

function decodeMap(r, len) {
  const out = {};
  for (let i = 0; i < len; i++) {
    const key = decodeValue(r);
    if (typeof key !== 'string') throw new Error(`msgpack: map key must be a string (got ${typeof key}) — this decoder only supports string-keyed maps, matching every plain-object encode path above`);
    out[key] = decodeValue(r);
  }
  return out;
}

/** Decodes a single MessagePack-encoded value from the start of `buf`. Throws if the buffer has unconsumed trailing bytes (catches truncated/over-long frames early rather than silently ignoring them). */
export function decode(buf) {
  const r = new ByteReader(buf);
  const value = decodeValue(r);
  if (r.pos !== buf.length) throw new Error(`msgpack: ${buf.length - r.pos} unconsumed trailing byte(s) after a complete value`);
  return value;
}
