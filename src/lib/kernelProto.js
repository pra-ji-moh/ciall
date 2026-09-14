// kernelProto.js; upgrade 8 — hand-written protobuf wire-format
// encoding for exactly the two messages Upgrade 8's KernelService needs,
// against the public Protocol Buffers encoding spec
// (protobuf.dev/programming-guides/encoding/). Not a general protobuf
// library — no .proto parser, no reflection, no other message shapes —
// just varint + length-delimited field encoding for:
//
//   message KernelRequest {
//     string kernel_name = 1;
//     bytes claim = 2;
//   }
//   message KernelResult {
//     bytes output = 1;
//     uint64 wall_clock_ms = 2;
//   }
//
// WIRE FORMAT RECAP (what this file implements). Each field is a
// varint TAG (fieldNumber << 3 | wireType) followed by its value.
// wireType 2 (LEN) is used for both fields here: a string/bytes value
// is itself length-prefixed (a varint byte length) then the raw bytes
// — same encoding for both, since a wire-format bytes payload doesn't
// carry its own "this is UTF-8 text" marker; kernel_name/claim/output
// are told apart by field NUMBER, not by wire type. wall_clock_ms is
// wireType 0 (VARINT) directly. A varint packs a value 7 bits at a
// time, LSB group first, continuation bit (0x80) set on every byte
// except the last.

const WIRE_VARINT = 0;
const WIRE_LEN = 2;

function tag(fieldNumber, wireType) {
  return (fieldNumber << 3) | wireType;
}

// Varints here are encoded/decoded via BigInt throughout: a JS Number
// can only safely represent integers up to 2^53, but wall_clock_ms is
// declared uint64 and a naively Number-based varint encoder would
// silently corrupt any value the protocol itself allows above that —
// exactly the kind of silent truncation this repo's honesty discipline
// exists to avoid. Every value that enters/leaves this module's public
// functions is a plain Number (milliseconds since epoch never
// approaches 2^53 in practice), but the wire encoding itself never
// assumes that.
function encodeVarint(n) {
  let v = BigInt(n);
  if (v < 0n) throw new Error('protobuf varint: negative values are not supported by this encoder (none of this schema\'s fields are signed)');
  const bytes = [];
  do {
    let b = Number(v & 0x7fn);
    v >>= 7n;
    if (v > 0n) b |= 0x80;
    bytes.push(b);
  } while (v > 0n);
  return Buffer.from(bytes);
}

function decodeVarint(buf, pos) {
  let result = 0n;
  let shift = 0n;
  let p = pos;
  for (;;) {
    if (p >= buf.length) throw new Error('protobuf varint: unexpected end of buffer');
    const b = buf[p++];
    result |= BigInt(b & 0x7f) << shift;
    if ((b & 0x80) === 0) break;
    shift += 7n;
    if (shift > 70n) throw new Error('protobuf varint: too many continuation bytes (malformed or hostile input)');
  }
  return { value: Number(result), nextPos: p };
}

function encodeLenField(fieldNumber, bytes) {
  return Buffer.concat([encodeVarint(tag(fieldNumber, WIRE_LEN)), encodeVarint(bytes.length), bytes]);
}

function encodeVarintField(fieldNumber, value) {
  return Buffer.concat([encodeVarint(tag(fieldNumber, WIRE_VARINT)), encodeVarint(value)]);
}

// Reads one field (tag + value) starting at `pos`; returns { fieldNumber, wireType, value, nextPos }.
// `value` is a Buffer for WIRE_LEN, a Number for WIRE_VARINT.
function decodeField(buf, pos) {
  const tagRead = decodeVarint(buf, pos);
  const fieldNumber = tagRead.value >> 3;
  const wireType = tagRead.value & 0x7;
  let p = tagRead.nextPos;
  if (wireType === WIRE_VARINT) {
    const v = decodeVarint(buf, p);
    return { fieldNumber, wireType, value: v.value, nextPos: v.nextPos };
  }
  if (wireType === WIRE_LEN) {
    const len = decodeVarint(buf, p);
    p = len.nextPos;
    if (p + len.value > buf.length) throw new Error('protobuf: length-delimited field exceeds buffer bounds');
    return { fieldNumber, wireType, value: buf.subarray(p, p + len.value), nextPos: p + len.value };
  }
  throw new Error(`protobuf: unsupported wire type ${wireType} (this module only implements VARINT and LEN, all this schema needs)`);
}

/** Encodes { kernelName: string, claim: Buffer } into a KernelRequest wire-format Buffer. */
export function encodeKernelRequest({ kernelName, claim }) {
  return Buffer.concat([
    encodeLenField(1, Buffer.from(kernelName, 'utf8')),
    encodeLenField(2, claim),
  ]);
}

/** Decodes a KernelRequest wire-format Buffer into { kernelName: string, claim: Buffer }. Field order in the wire bytes is not assumed. */
export function decodeKernelRequest(buf) {
  let pos = 0;
  let kernelName;
  let claim;
  while (pos < buf.length) {
    const f = decodeField(buf, pos);
    pos = f.nextPos;
    if (f.fieldNumber === 1) kernelName = f.value.toString('utf8');
    else if (f.fieldNumber === 2) claim = f.value;
  }
  if (kernelName === undefined) throw new Error('protobuf: KernelRequest missing required field kernel_name (1)');
  if (claim === undefined) throw new Error('protobuf: KernelRequest missing required field claim (2)');
  return { kernelName, claim };
}

/** Encodes { output: Buffer, wallClockMs: number } into a KernelResult wire-format Buffer. */
export function encodeKernelResult({ output, wallClockMs }) {
  return Buffer.concat([
    encodeLenField(1, output),
    encodeVarintField(2, wallClockMs),
  ]);
}

/** Decodes a KernelResult wire-format Buffer into { output: Buffer, wallClockMs: number }. */
export function decodeKernelResult(buf) {
  let pos = 0;
  let output;
  let wallClockMs;
  while (pos < buf.length) {
    const f = decodeField(buf, pos);
    pos = f.nextPos;
    if (f.fieldNumber === 1) output = f.value;
    else if (f.fieldNumber === 2) wallClockMs = f.value;
  }
  if (output === undefined) throw new Error('protobuf: KernelResult missing required field output (1)');
  if (wallClockMs === undefined) throw new Error('protobuf: KernelResult missing required field wall_clock_ms (2)');
  return { output, wallClockMs };
}
