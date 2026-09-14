// http2Frame.js; upgrade 8 — HTTP/2 frame format (RFC 7540 §4), hand-
// implemented against the spec. No npm dependency (Node's own built-in
// `http2` module deliberately not used — the task calls for implementing
// the framing itself, not wrapping an existing implementation of it).
//
// SCOPE, DISCLOSED: this repo's HEADERS payloads are always small (a
// handful of fixed pseudo-headers + a couple of gRPC headers, HPACK-
// encoded with no Huffman/dynamic-table compression — see hpack.js),
// well under DEFAULT_MAX_FRAME_SIZE. CONTINUATION frames (for a header
// block that doesn't fit in one HEADERS frame) are therefore not
// implemented; encodeHeadersFrame() asserts the block fits in one frame
// rather than silently truncating or producing a malformed one if that
// assumption is ever violated.

export const DEFAULT_MAX_FRAME_SIZE = 16384;

export const FRAME_TYPE = {
  DATA: 0x0,
  HEADERS: 0x1,
  PRIORITY: 0x2,
  RST_STREAM: 0x3,
  SETTINGS: 0x4,
  PUSH_PROMISE: 0x5,
  PING: 0x6,
  GOAWAY: 0x7,
  WINDOW_UPDATE: 0x8,
  CONTINUATION: 0x9,
};

export const FLAG = {
  END_STREAM: 0x1,
  ACK: 0x1, // SETTINGS/PING; distinct namespace from END_STREAM but same bit value, never used on the same frame type
  END_HEADERS: 0x4,
  PADDED: 0x8,
  PRIORITY: 0x20,
};

export const CONNECTION_PREFACE = Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n', 'ascii');

// ── generic frame header (9 bytes) ──────────────────────────────────

export function encodeFrame({ type, flags = 0, streamId = 0, payload = Buffer.alloc(0) }) {
  if (payload.length > 0xffffff) throw new Error(`http2: frame payload ${payload.length} bytes exceeds the 24-bit length field`);
  const header = Buffer.alloc(9);
  header.writeUIntBE(payload.length, 0, 3);
  header.writeUInt8(type, 3);
  header.writeUInt8(flags, 4);
  header.writeUInt32BE(streamId & 0x7fffffff, 5); // top R bit forced to 0 per spec
  return Buffer.concat([header, payload]);
}

/** Reads exactly one frame (header + payload) from the start of `buf`. Returns null if `buf` doesn't yet contain a complete frame (caller should wait for more bytes). */
export function tryReadFrame(buf) {
  if (buf.length < 9) return null;
  const length = buf.readUIntBE(0, 3);
  const type = buf.readUInt8(3);
  const flags = buf.readUInt8(4);
  const streamId = buf.readUInt32BE(5) & 0x7fffffff;
  const total = 9 + length;
  if (buf.length < total) return null;
  return { frame: { type, flags, streamId, payload: buf.subarray(9, total) }, bytesConsumed: total };
}

// ── per-type payload helpers ─────────────────────────────────────────

export function encodeHeadersFrame({ streamId, headerBlock, endStream, endHeaders = true }) {
  if (headerBlock.length > DEFAULT_MAX_FRAME_SIZE) throw new Error(`http2: HEADERS block ${headerBlock.length} bytes exceeds DEFAULT_MAX_FRAME_SIZE (${DEFAULT_MAX_FRAME_SIZE}) — CONTINUATION frames are not implemented (see file header)`);
  let flags = 0;
  if (endStream) flags |= FLAG.END_STREAM;
  if (endHeaders) flags |= FLAG.END_HEADERS;
  return encodeFrame({ type: FRAME_TYPE.HEADERS, flags, streamId, payload: headerBlock });
}

export function encodeDataFrame({ streamId, data, endStream }) {
  return encodeFrame({ type: FRAME_TYPE.DATA, flags: endStream ? FLAG.END_STREAM : 0, streamId, payload: data });
}

/** Encodes a SETTINGS frame from an array of [identifier, value] pairs (empty array is valid — accepts all peer defaults). */
export function encodeSettingsFrame(pairs = [], { ack = false } = {}) {
  if (ack) return encodeFrame({ type: FRAME_TYPE.SETTINGS, flags: FLAG.ACK, streamId: 0 });
  const payload = Buffer.alloc(pairs.length * 6);
  pairs.forEach(([id, value], i) => {
    payload.writeUInt16BE(id, i * 6);
    payload.writeUInt32BE(value >>> 0, i * 6 + 2);
  });
  return encodeFrame({ type: FRAME_TYPE.SETTINGS, streamId: 0, payload });
}

export function decodeSettingsPayload(payload) {
  if (payload.length % 6 !== 0) throw new Error(`http2: malformed SETTINGS payload (${payload.length} bytes, not a multiple of 6)`);
  const pairs = [];
  for (let i = 0; i < payload.length; i += 6) {
    pairs.push([payload.readUInt16BE(i), payload.readUInt32BE(i + 2)]);
  }
  return pairs;
}

export function encodeWindowUpdateFrame({ streamId = 0, increment }) {
  if (!(increment > 0 && increment <= 0x7fffffff)) throw new Error(`http2: WINDOW_UPDATE increment ${increment} out of range`);
  const payload = Buffer.alloc(4);
  payload.writeUInt32BE(increment & 0x7fffffff, 0);
  return encodeFrame({ type: FRAME_TYPE.WINDOW_UPDATE, streamId, payload });
}

export function decodeWindowUpdatePayload(payload) {
  return payload.readUInt32BE(0) & 0x7fffffff;
}

export function encodeRstStreamFrame({ streamId, errorCode }) {
  const payload = Buffer.alloc(4);
  payload.writeUInt32BE(errorCode >>> 0, 0);
  return encodeFrame({ type: FRAME_TYPE.RST_STREAM, streamId, payload });
}

export function decodeRstStreamPayload(payload) {
  return payload.readUInt32BE(0);
}

export function encodeGoawayFrame({ lastStreamId, errorCode, debugData = Buffer.alloc(0) }) {
  const payload = Buffer.alloc(8 + debugData.length);
  payload.writeUInt32BE(lastStreamId & 0x7fffffff, 0);
  payload.writeUInt32BE(errorCode >>> 0, 4);
  debugData.copy(payload, 8);
  return encodeFrame({ type: FRAME_TYPE.GOAWAY, streamId: 0, payload });
}

export function decodeGoawayPayload(payload) {
  return {
    lastStreamId: payload.readUInt32BE(0) & 0x7fffffff,
    errorCode: payload.readUInt32BE(4),
    debugData: payload.subarray(8),
  };
}
