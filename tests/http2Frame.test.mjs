// http2Frame.test.mjs; upgrade 8 — validates the from-scratch HTTP/2
// frame codec against the literal 9-byte frame header layout from
// RFC 7540 §4.1 (24-bit length, 8-bit type, 8-bit flags, 1 reserved +
// 31-bit stream id), plus round-trip and partial-buffer behavior.

import test from 'node:test';
import assert from 'node:assert/strict';
import {
  FRAME_TYPE, FLAG, CONNECTION_PREFACE,
  encodeFrame, tryReadFrame, encodeHeadersFrame, encodeDataFrame,
  encodeSettingsFrame, decodeSettingsPayload,
  encodeWindowUpdateFrame, decodeWindowUpdatePayload,
  encodeRstStreamFrame, decodeRstStreamPayload,
  encodeGoawayFrame, decodeGoawayPayload,
} from '../src/lib/rpc/http2Frame.js';

test('connection preface is the literal 24-byte RFC 7540 §3.5 magic string', () => {
  assert.equal(CONNECTION_PREFACE.toString('ascii'), 'PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n');
  assert.equal(CONNECTION_PREFACE.length, 24);
});

test('frame header layout matches RFC 7540 §4.1 byte-for-byte', () => {
  const payload = Buffer.from([0xaa, 0xbb, 0xcc]);
  const buf = encodeFrame({ type: FRAME_TYPE.DATA, flags: FLAG.END_STREAM, streamId: 5, payload });
  assert.equal(buf.length, 9 + 3);
  assert.equal(buf.readUIntBE(0, 3), 3); // 24-bit length
  assert.equal(buf.readUInt8(3), FRAME_TYPE.DATA); // type
  assert.equal(buf.readUInt8(4), FLAG.END_STREAM); // flags
  assert.equal(buf.readUInt32BE(5) & 0x7fffffff, 5); // stream id, R bit masked
  assert.equal(buf.readUInt32BE(5) >>> 31, 0); // R bit forced to 0
  assert.deepEqual([...buf.subarray(9)], [0xaa, 0xbb, 0xcc]);
});

test('a stream id with the high bit set has that bit stripped on encode (R bit is always 0)', () => {
  const buf = encodeFrame({ type: FRAME_TYPE.HEADERS, streamId: 0x80000001 });
  assert.equal(buf.readUInt32BE(5), 1); // top bit cleared, low 31 bits (1) preserved
});

test('tryReadFrame returns null on a partial buffer and the correct frame + byte count once complete', () => {
  const full = encodeFrame({ type: FRAME_TYPE.PING, streamId: 0, payload: Buffer.alloc(8) });
  assert.equal(tryReadFrame(full.subarray(0, 5)), null); // partial header
  assert.equal(tryReadFrame(full.subarray(0, 9)), null); // full header, no payload yet
  assert.equal(tryReadFrame(full.subarray(0, 12)), null); // partial payload
  const result = tryReadFrame(full);
  assert.equal(result.bytesConsumed, full.length);
  assert.equal(result.frame.type, FRAME_TYPE.PING);
  assert.equal(result.frame.payload.length, 8);
});

test('tryReadFrame parses only the FIRST frame when the buffer holds more than one, leaving the rest for the next call', () => {
  const a = encodeFrame({ type: FRAME_TYPE.PING, streamId: 0, payload: Buffer.alloc(8) });
  const b = encodeFrame({ type: FRAME_TYPE.WINDOW_UPDATE, streamId: 1, payload: Buffer.from([0, 0, 0, 10]) });
  const combined = Buffer.concat([a, b]);
  const first = tryReadFrame(combined);
  assert.equal(first.bytesConsumed, a.length);
  assert.equal(first.frame.type, FRAME_TYPE.PING);
  const second = tryReadFrame(combined.subarray(first.bytesConsumed));
  assert.equal(second.frame.type, FRAME_TYPE.WINDOW_UPDATE);
  assert.equal(second.frame.streamId, 1);
});

test('HEADERS/DATA frame flags: END_HEADERS defaults on, END_STREAM only when requested', () => {
  const headerBlock = Buffer.from([0x83]); // arbitrary small HPACK block
  const notLastHeaders = encodeHeadersFrame({ streamId: 1, headerBlock, endStream: false });
  const { frame: f1 } = tryReadFrame(notLastHeaders);
  assert.equal(f1.flags & 0x4, 0x4); // END_HEADERS set
  assert.equal(f1.flags & 0x1, 0); // END_STREAM not set

  const trailers = encodeHeadersFrame({ streamId: 1, headerBlock, endStream: true });
  const { frame: f2 } = tryReadFrame(trailers);
  assert.equal(f2.flags & 0x1, 0x1);

  const dataFrame = encodeDataFrame({ streamId: 1, data: Buffer.from('hi'), endStream: true });
  const { frame: f3 } = tryReadFrame(dataFrame);
  assert.equal(f3.flags & 0x1, 0x1);
  assert.equal(f3.payload.toString(), 'hi');
});

test('encodeHeadersFrame rejects a header block that would require CONTINUATION (not implemented)', () => {
  const huge = Buffer.alloc(20000);
  assert.throws(() => encodeHeadersFrame({ streamId: 1, headerBlock: huge, endStream: false }), /CONTINUATION/);
});

test('SETTINGS frame: empty (all-defaults) round-trips, and non-empty pairs round-trip in order', () => {
  const empty = encodeSettingsFrame([]);
  const { frame: f1 } = tryReadFrame(empty);
  assert.equal(f1.payload.length, 0);
  assert.deepEqual(decodeSettingsPayload(f1.payload), []);

  const pairs = [[3, 100], [4, 65535]]; // MAX_CONCURRENT_STREAMS=100, INITIAL_WINDOW_SIZE=65535
  const buf = encodeSettingsFrame(pairs);
  const { frame: f2 } = tryReadFrame(buf);
  assert.deepEqual(decodeSettingsPayload(f2.payload), pairs);

  const ack = encodeSettingsFrame([], { ack: true });
  const { frame: f3 } = tryReadFrame(ack);
  assert.equal(f3.flags & 0x1, 0x1);
  assert.equal(f3.payload.length, 0);
});

test('WINDOW_UPDATE round-trips its increment, R bit masked', () => {
  const buf = encodeWindowUpdateFrame({ streamId: 3, increment: 65535 });
  const { frame } = tryReadFrame(buf);
  assert.equal(decodeWindowUpdatePayload(frame.payload), 65535);
});

test('RST_STREAM round-trips its error code', () => {
  const buf = encodeRstStreamFrame({ streamId: 3, errorCode: 8 }); // CANCEL
  const { frame } = tryReadFrame(buf);
  assert.equal(decodeRstStreamPayload(frame.payload), 8);
});

test('GOAWAY round-trips lastStreamId, errorCode, and debug data', () => {
  const buf = encodeGoawayFrame({ lastStreamId: 7, errorCode: 0, debugData: Buffer.from('bye') });
  const { frame } = tryReadFrame(buf);
  const decoded = decodeGoawayPayload(frame.payload);
  assert.equal(decoded.lastStreamId, 7);
  assert.equal(decoded.errorCode, 0);
  assert.equal(decoded.debugData.toString(), 'bye');
});
