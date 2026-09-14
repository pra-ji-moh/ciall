// kernelProto.test.mjs; upgrade 8 — validates the hand-written
// protobuf wire-format encoder/decoder for KernelRequest/KernelResult
// against LITERAL byte sequences, including the two canonical varint
// examples from the official Protocol Buffers encoding guide
// (150 -> 0x96 0x01, 300 -> 0xAC 0x02), not just round-tripped through
// this module's own encode/decode pair.

import test from 'node:test';
import assert from 'node:assert/strict';
import { encodeKernelRequest, decodeKernelRequest, encodeKernelResult, decodeKernelResult } from '../src/lib/kernelProto.js';

function hex(buf) { return Buffer.from(buf).toString('hex'); }

test('KernelRequest wire bytes match hand-derived tag/length/value layout exactly', () => {
  const buf = encodeKernelRequest({ kernelName: 'mcmc', claim: Buffer.from([0xde, 0xad]) });
  // field 1 (kernel_name), wireType LEN=2 -> tag byte (1<<3)|2 = 0x0a
  // length 4, then "mcmc" = 6d 63 6d 63
  // field 2 (claim), wireType LEN=2 -> tag byte (2<<3)|2 = 0x12
  // length 2, then de ad
  assert.equal(hex(buf), '0a046d636d63' + '1202dead');
});

test('KernelResult wire bytes match hand-derived tag/varint layout exactly, including the two canonical protobuf.dev varint examples', () => {
  // wall_clock_ms=150 is the official spec's own varint example: 0x96 0x01
  const buf150 = encodeKernelResult({ output: Buffer.from([]), wallClockMs: 150 });
  // field 1 (output), LEN, tag 0x0a, length 0, no bytes
  // field 2 (wall_clock_ms), VARINT, tag (2<<3)|0 = 0x10, then varint(150) = 96 01
  assert.equal(hex(buf150), '0a00' + '10' + '9601');

  // wall_clock_ms=300 is the official spec's other canonical example: 0xAC 0x02
  const buf300 = encodeKernelResult({ output: Buffer.from([1, 2, 3]), wallClockMs: 300 });
  assert.equal(hex(buf300), '0a03010203' + '10' + 'ac02');
});

test('varint boundary values round-trip through wall_clock_ms exactly, including values beyond Number-unsafe 2^53', () => {
  for (const ms of [0, 1, 127, 128, 16383, 16384, 2097151, 2097152, Number.MAX_SAFE_INTEGER]) {
    const buf = encodeKernelResult({ output: Buffer.from('x'), wallClockMs: ms });
    const decoded = decodeKernelResult(buf);
    assert.equal(decoded.wallClockMs, ms, `wallClockMs=${ms}`);
  }
});

test('round-trip: KernelRequest and KernelResult with realistic payloads (msgpack-encoded claim/output bytes)', async () => {
  const { encode } = await import('../src/lib/msgpack.js');
  const claim = encode({ kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' });
  const reqBuf = encodeKernelRequest({ kernelName: 'mcmc', claim });
  const decodedReq = decodeKernelRequest(reqBuf);
  assert.equal(decodedReq.kernelName, 'mcmc');
  assert.deepEqual(new Uint8Array(decodedReq.claim), new Uint8Array(claim));

  const output = encode({ verdict: 'violated', bestPoint: { x: 9.99 } });
  const resBuf = encodeKernelResult({ output, wallClockMs: 42 });
  const decodedRes = decodeKernelResult(resBuf);
  assert.deepEqual(new Uint8Array(decodedRes.output), new Uint8Array(output));
  assert.equal(decodedRes.wallClockMs, 42);
});

test('field order in the wire bytes is not assumed: decoding still works with fields reversed', () => {
  // Hand-build a KernelRequest with field 2 (claim) BEFORE field 1
  // (kernel_name) -- valid protobuf wire format never guarantees field
  // order, so the decoder must not assume it.
  const claimField = Buffer.concat([Buffer.from([0x12, 0x02]), Buffer.from([0xbe, 0xef])]);
  const nameField = Buffer.concat([Buffer.from([0x0a, 0x03]), Buffer.from('abc', 'utf8')]);
  const reversed = Buffer.concat([claimField, nameField]);
  const decoded = decodeKernelRequest(reversed);
  assert.equal(decoded.kernelName, 'abc');
  assert.deepEqual(new Uint8Array(decoded.claim), new Uint8Array([0xbe, 0xef]));
});

test('decode fails loudly on a missing required field rather than returning undefined silently', () => {
  const onlyNameField = Buffer.concat([Buffer.from([0x0a, 0x03]), Buffer.from('abc', 'utf8')]);
  assert.throws(() => decodeKernelRequest(onlyNameField), /missing required field claim/);

  const onlyOutputField = Buffer.from([0x0a, 0x00]);
  assert.throws(() => decodeKernelResult(onlyOutputField), /missing required field wall_clock_ms/);
});

test('decode fails loudly on a truncated / malformed varint rather than looping forever or misreading', () => {
  // A varint byte with the continuation bit set but nothing after it.
  const truncated = Buffer.from([0x10, 0x80]);
  assert.throws(() => decodeKernelResult(Buffer.concat([Buffer.from([0x0a, 0x00]), truncated])), /unexpected end of buffer/);

  // A pathological run of continuation-set bytes, far beyond any valid
  // varint length -- must be rejected, not looped over indefinitely.
  const hostile = Buffer.concat([Buffer.from([0x0a, 0x00, 0x10]), Buffer.alloc(12, 0x80)]);
  assert.throws(() => decodeKernelResult(hostile), /too many continuation bytes/);
});

test('decode fails loudly on a length-delimited field whose declared length exceeds the buffer', () => {
  // tag 0x0a (field 1, LEN), then a VALID single-byte varint length of
  // 127 (0x7f, no continuation bit) -- but the buffer ends right there,
  // 127 bytes short. This must be a distinct error from a truncated
  // varint itself (covered by the previous test): the length field
  // decodes fine, it's the VALUE that doesn't fit.
  const badLen = Buffer.from([0x0a, 0x7f]);
  assert.throws(() => decodeKernelRequest(badLen), /exceeds buffer bounds/);
});
