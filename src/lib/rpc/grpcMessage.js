// grpcMessage.js; upgrade 8 — gRPC's own message framing, which sits
// INSIDE HTTP/2 DATA frame payloads (distinct from HTTP/2's own frame
// header): a 1-byte compressed-flag + 4-byte big-endian message length,
// then the raw protobuf-encoded message bytes. This repo never sends
// compressed messages (payloads are small; compression would be pure
// overhead), so the flag is always 0 on encode and rejected if set to
// anything else on decode.

export function wrapGrpcMessage(messageBytes) {
  const header = Buffer.alloc(5);
  header.writeUInt8(0, 0); // compressed-flag: always uncompressed
  header.writeUInt32BE(messageBytes.length, 1);
  return Buffer.concat([header, messageBytes]);
}

/** Unwraps exactly one gRPC-framed message from `buf` (which must contain exactly one — the DATA payload for a single unary request/response, this repo's only shape). */
export function unwrapGrpcMessage(buf) {
  if (buf.length < 5) throw new Error('grpc message framing: buffer shorter than the 5-byte length-prefix header');
  const compressed = buf.readUInt8(0);
  if (compressed !== 0) throw new Error(`grpc message framing: compressed-flag ${compressed} received, but compression is never used by this implementation`);
  const len = buf.readUInt32BE(1);
  if (5 + len !== buf.length) throw new Error(`grpc message framing: declared length ${len} does not match the remaining buffer (${buf.length - 5} bytes) — malformed frame or more than one message packed in, which this implementation (unary RPC only) does not expect`);
  return buf.subarray(5, 5 + len);
}
