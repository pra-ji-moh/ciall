// hpack.test.mjs; upgrade 8 — validates the from-scratch HPACK codec.
// Literal byte checks for the "Indexed Header Field" form come straight
// from RFC 7541's own static table (index 3 = ":method: POST" -> byte
// 0x83, etc.) — not just round-tripped through this module's own
// encode/decode pair.

import test from 'node:test';
import assert from 'node:assert/strict';
import { encodeHeaders, decodeHeaders, STATIC_TABLE } from '../src/lib/rpc/hpack.js';

test('static table has exactly the 61 RFC 7541 entries, in order', () => {
  assert.equal(STATIC_TABLE.length, 61);
  assert.deepEqual(STATIC_TABLE[0], [':authority', '']);
  assert.deepEqual(STATIC_TABLE[1], [':method', 'GET']);
  assert.deepEqual(STATIC_TABLE[2], [':method', 'POST']);
  assert.deepEqual(STATIC_TABLE[6], [':scheme', 'https']);
  assert.deepEqual(STATIC_TABLE[7], [':status', '200']);
  assert.deepEqual(STATIC_TABLE[30], ['content-type', '']);
  assert.deepEqual(STATIC_TABLE[60], ['www-authenticate', '']);
});

test('Indexed Header Field: an exact static-table name+value match encodes as a single byte, 0x80|index', () => {
  assert.deepEqual([...encodeHeaders([[':method', 'POST']])], [0x83]); // index 3
  assert.deepEqual([...encodeHeaders([[':status', '200']])], [0x88]); // index 8
  assert.deepEqual([...encodeHeaders([[':scheme', 'https']])], [0x87]); // index 7
  assert.deepEqual([...encodeHeaders([['content-type', '']])], [0x9f]); // index 31, 0x80|31=0x9f
});

test('Literal Header Field without Indexing, indexed name: static name match, non-matching value', () => {
  const buf = encodeHeaders([[':path', '/ciall.KernelService/Run']]);
  // first byte: 4-bit prefix, index 4 (":path"), no continuation needed (4 < 15)
  assert.equal(buf[0], 0x04);
  const decoded = decodeHeaders(buf);
  assert.deepEqual(decoded, [[':path', '/ciall.KernelService/Run']]);
});

test('Literal Header Field without Indexing, new name: no static match at all', () => {
  const buf = encodeHeaders([['grpc-status', '0']]);
  assert.equal(buf[0], 0x00); // index 0 = new name follows
  const decoded = decodeHeaders(buf);
  assert.deepEqual(decoded, [['grpc-status', '0']]);
});

test('round-trip: a full realistic gRPC request header set, in order, multi-byte string lengths included', () => {
  const headers = [
    [':method', 'POST'],
    [':scheme', 'https'],
    [':path', '/ciall.KernelService/Run'],
    [':authority', 'localhost:50051'],
    ['content-type', 'application/grpc+proto'],
    ['te', 'trailers'],
    ['grpc-timeout', '5000m'],
  ];
  const buf = encodeHeaders(headers);
  assert.deepEqual(decodeHeaders(buf), headers);
});

test('round-trip: response headers and trailers (including an error grpc-status/grpc-message)', () => {
  const responseHeaders = [[':status', '200'], ['content-type', 'application/grpc+proto']];
  assert.deepEqual(decodeHeaders(encodeHeaders(responseHeaders)), responseHeaders);

  const okTrailers = [['grpc-status', '0']];
  assert.deepEqual(decodeHeaders(encodeHeaders(okTrailers)), okTrailers);

  const errorTrailers = [['grpc-status', '13'], ['grpc-message', 'kernel threw: bad spec']];
  assert.deepEqual(decodeHeaders(encodeHeaders(errorTrailers)), errorTrailers);
});

test('string length crossing the 7-bit prefix boundary (>=127 bytes) round-trips correctly', () => {
  const longValue = 'x'.repeat(300);
  const buf = encodeHeaders([['grpc-message', longValue]]);
  assert.deepEqual(decodeHeaders(buf), [['grpc-message', longValue]]);
});

test('integer encoding crossing the 7-bit-prefix static-index boundary is never hit in practice (only 61 static entries) but literal name-index encoding still round-trips at the 4-bit boundary', () => {
  // Every static name-index used by "indexed name" literals is <= 61,
  // which exceeds the 4-bit prefix max (15) for several real entries
  // (e.g. content-type is index 31) -- exercise that continuation path.
  const buf = encodeHeaders([['content-type', 'application/grpc+proto']]);
  assert.deepEqual(decodeHeaders(buf), [['content-type', 'application/grpc+proto']]);
});

test('decoding a Huffman-flagged string (H=1) is rejected with a clear, specific error', () => {
  // Hand-construct: Literal without Indexing, new name (0x00), then a
  // "string" with the H bit (top bit of the length byte) set.
  const malformed = Buffer.from([0x00, 0x81, 0x61]); // name = Huffman-flagged, length=1
  assert.throws(() => decodeHeaders(malformed), /Huffman/);
});

test('decoding an out-of-range (dynamic-table) index is rejected with a clear, specific error, not silently misread', () => {
  const buf = Buffer.from([0xff, 0x00]); // Indexed Header Field, 7-bit prefix all-1s (127) + continuation byte 0 -> index 62 (first dynamic-table slot)
  assert.throws(() => decodeHeaders(buf), /dynamic table/);
});

test('decoding "Literal Header Field with Incremental Indexing" is rejected with a clear, specific error', () => {
  const buf = Buffer.from([0x40, 0x00]); // 01xxxxxx pattern, 6-bit prefix, index 0 (new name) -- malformed on purpose, just needs the leading pattern
  assert.throws(() => decodeHeaders(buf), /Incremental Indexing/);
});

test('multiple header fields in one block decode in wire order, mixing all three representations', () => {
  const headers = [
    [':method', 'POST'], // indexed
    [':path', '/x'], // literal, indexed name
    ['x-custom', 'y'], // literal, new name
    [':status', '200'], // indexed again
  ];
  assert.deepEqual(decodeHeaders(encodeHeaders(headers)), headers);
});
