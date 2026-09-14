// hpack.js; upgrade 8 — a from-scratch HPACK (RFC 7541) header
// compression codec for the gRPC-over-HTTP/2 layer. No npm dependency;
// hand-implemented against the RFC's own algorithms.
//
// SCOPE, DISCLOSED (ground rule 4/5): this HPACK implementation covers
// every part of the spec that is REQUIRED for correctness, and
// deliberately omits two parts that are OPTIONAL per RFC 7541 itself:
//
//  1. Huffman coding (RFC 7541 Appendix B) is a pure compression
//     optimization — the spec explicitly permits encoding every string
//     literal with H=0 (raw octets) and remaining fully conformant.
//     This encoder always emits H=0. The decoder REJECTS an incoming
//     H=1 (Huffman-flagged) string with a clear error rather than
//     silently mishandling it — acceptable because the only peer this
//     decoder will ever see traffic from is this repo's own encoder
//     (kernelServiceClient.js/kernelServiceServer.js talking to each
//     other), which never sets H=1. A decoder facing arbitrary
//     internet traffic would need the full Huffman table; this one
//     doesn't, because there is no such traffic here.
//  2. The dynamic table (RFC 7541 §2.3.2) is likewise an optional
//     compression optimization — "Literal Header Field without
//     Indexing" is a fully spec-compliant representation for EVERY
//     header, every time, and this encoder always uses it (or the
//     plain "Indexed Header Field" form when a header matches the
//     STATIC table exactly). The decoder therefore never needs to grow
//     or evict dynamic-table entries and rejects a dynamic-table index
//     reference (>61) or an incremental-indexing literal with a clear
//     error, for the same reason as above.
//
// Net effect: every header block this module produces is wire-format
// HPACK any compliant decoder (including a real one) could parse
// correctly — these are documented, valid degenerate cases of the
// format, not a departure from it. It just never uses the compression
// techniques that would make near-repeated header sets smaller, which
// doesn't matter here: the header set is small, fixed, and sent once
// per RPC call, not per network byte at internet scale.

// RFC 7541 Appendix A, static table, index 1..61 (this array is
// 0-indexed; add 1 when treating it as an HPACK index).
export const STATIC_TABLE = [
  [':authority', ''],
  [':method', 'GET'],
  [':method', 'POST'],
  [':path', '/'],
  [':path', '/index.html'],
  [':scheme', 'http'],
  [':scheme', 'https'],
  [':status', '200'],
  [':status', '204'],
  [':status', '206'],
  [':status', '304'],
  [':status', '400'],
  [':status', '404'],
  [':status', '500'],
  ['accept-charset', ''],
  ['accept-encoding', 'gzip, deflate'],
  ['accept-language', ''],
  ['accept-ranges', ''],
  ['accept', ''],
  ['access-control-allow-origin', ''],
  ['age', ''],
  ['allow', ''],
  ['authorization', ''],
  ['cache-control', ''],
  ['content-disposition', ''],
  ['content-encoding', ''],
  ['content-language', ''],
  ['content-length', ''],
  ['content-location', ''],
  ['content-range', ''],
  ['content-type', ''],
  ['cookie', ''],
  ['date', ''],
  ['etag', ''],
  ['expect', ''],
  ['expires', ''],
  ['from', ''],
  ['host', ''],
  ['if-match', ''],
  ['if-modified-since', ''],
  ['if-none-match', ''],
  ['if-range', ''],
  ['if-unmodified-since', ''],
  ['last-modified', ''],
  ['link', ''],
  ['location', ''],
  ['max-forwards', ''],
  ['proxy-authenticate', ''],
  ['proxy-authorization', ''],
  ['range', ''],
  ['referer', ''],
  ['refresh', ''],
  ['retry-after', ''],
  ['server', ''],
  ['set-cookie', ''],
  ['strict-transport-security', ''],
  ['transfer-encoding', ''],
  ['user-agent', ''],
  ['vary', ''],
  ['via', ''],
  ['www-authenticate', ''],
];

function findExactStaticIndex(name, value) {
  for (let i = 0; i < STATIC_TABLE.length; i++) {
    if (STATIC_TABLE[i][0] === name && STATIC_TABLE[i][1] === value) return i + 1;
  }
  return null;
}

function findStaticNameIndex(name) {
  for (let i = 0; i < STATIC_TABLE.length; i++) {
    if (STATIC_TABLE[i][0] === name) return i + 1;
  }
  return null;
}

// ============================================================
// RFC 7541 §5.1 — integer representation, N-bit prefix.
// ============================================================

// Encodes `value` using an N-bit prefix. Returns the bytes for JUST the
// integer (the first byte's low N bits carry data; its high (8-N) bits
// are 0 — the caller ORs their own flag bits into them).
function encodeInt(value, N) {
  const max = (1 << N) - 1;
  if (value < max) return Buffer.from([value]);
  const bytes = [max];
  let remaining = value - max;
  while (remaining >= 128) {
    bytes.push((remaining % 128) + 128);
    remaining = Math.floor(remaining / 128);
  }
  bytes.push(remaining);
  return Buffer.from(bytes);
}

// Decodes an N-bit-prefix integer starting at buf[pos]. Returns { value, nextPos }.
function decodeInt(buf, pos, N) {
  if (pos >= buf.length) throw new Error('hpack: unexpected end of header block (integer)');
  const max = (1 << N) - 1;
  let value = buf[pos] & max;
  let p = pos + 1;
  if (value === max) {
    let m = 0;
    for (;;) {
      if (p >= buf.length) throw new Error('hpack: unexpected end of header block (integer continuation)');
      const b = buf[p++];
      value += (b & 0x7f) * 2 ** m;
      m += 7;
      if ((b & 0x80) === 0) break;
      if (m > 70) throw new Error('hpack: integer continuation too long (malformed or hostile input)');
    }
  }
  return { value, nextPos: p };
}

// ============================================================
// RFC 7541 §5.2 — string literal representation.
// ============================================================

function encodeString(str) {
  const bytes = Buffer.from(str, 'utf8');
  const lenBytes = encodeInt(bytes.length, 7); // H bit (top bit) left as 0: no Huffman, see file header
  return Buffer.concat([lenBytes, bytes]);
}

function decodeString(buf, pos) {
  if (pos >= buf.length) throw new Error('hpack: unexpected end of header block (string)');
  const huffman = (buf[pos] & 0x80) !== 0;
  if (huffman) throw new Error('hpack: Huffman-encoded string literal received, but this decoder does not implement Huffman (see hpack.js file header — only ever expected from this repo\'s own encoder, which never sets H=1)');
  const len = decodeInt(buf, pos, 7);
  const start = len.nextPos;
  const end = start + len.value;
  if (end > buf.length) throw new Error('hpack: string literal length exceeds header block bounds');
  return { value: buf.subarray(start, end).toString('utf8'), nextPos: end };
}

// ============================================================
// Header field representations (RFC 7541 §6). This encoder emits only
// two: "Indexed Header Field" (§6.1) and "Literal Header Field without
// Indexing" (§6.2.2, both the indexed-name and new-name forms) — see
// the file header for why the other representations are unnecessary
// here. The decoder recognizes exactly those two, plus a clear,
// specific error for anything else it might see (dynamic-table
// references, incremental indexing, never-indexed) rather than
// mishandling it silently.
// ============================================================

function encodeHeaderField([name, value]) {
  const exact = findExactStaticIndex(name, value);
  if (exact !== null) {
    // Indexed Header Field: 1xxxxxxx, 7-bit prefix index.
    const idxBytes = encodeInt(exact, 7);
    idxBytes[0] |= 0x80;
    return idxBytes;
  }
  const nameIdx = findStaticNameIndex(name);
  if (nameIdx !== null) {
    // Literal Header Field without Indexing, indexed name: 0000xxxx, 4-bit prefix.
    const idxBytes = encodeInt(nameIdx, 4); // top 4 bits already 0
    return Buffer.concat([idxBytes, encodeString(value)]);
  }
  // Literal Header Field without Indexing, new name: single 0x00 byte (index 0), then name, then value.
  return Buffer.concat([Buffer.from([0x00]), encodeString(name), encodeString(value)]);
}

/** Encodes an array of [name, value] pairs (in the given order) into an HPACK header block Buffer. */
export function encodeHeaders(pairs) {
  return Buffer.concat(pairs.map(encodeHeaderField));
}

/** Decodes an HPACK header block Buffer into an array of [name, value] pairs, in wire order. */
export function decodeHeaders(buf) {
  const out = [];
  let pos = 0;
  while (pos < buf.length) {
    const first = buf[pos];
    if ((first & 0x80) !== 0) {
      // Indexed Header Field (§6.1), 7-bit prefix.
      const { value: index, nextPos } = decodeInt(buf, pos, 7);
      pos = nextPos;
      if (index === 0) throw new Error('hpack: Indexed Header Field with index 0 is invalid');
      if (index > STATIC_TABLE.length) throw new Error(`hpack: index ${index} refers to the dynamic table, which this decoder does not implement (see file header)`);
      out.push([...STATIC_TABLE[index - 1]]);
      continue;
    }
    if ((first & 0xc0) === 0x40) {
      throw new Error('hpack: "Literal Header Field with Incremental Indexing" received, but this decoder does not implement the dynamic table (see file header)');
    }
    if ((first & 0xe0) === 0x20) {
      throw new Error('hpack: "Dynamic Table Size Update" received, but this decoder does not implement the dynamic table (see file header)');
    }
    // Literal Header Field without Indexing (0000xxxx) or Never Indexed
    // (0001xxxx) -- both use a 4-bit prefix and are handled identically
    // for decoding purposes; this encoder never emits Never Indexed,
    // but decoding it the same way is correct either way (the "never
    // index this" semantic only constrains re-encoders/proxies, not a
    // terminal decoder).
    const { value: nameIndex, nextPos: afterIndex } = decodeInt(buf, pos, 4);
    pos = afterIndex;
    let name;
    if (nameIndex === 0) {
      const nameResult = decodeString(buf, pos);
      name = nameResult.value;
      pos = nameResult.nextPos;
    } else {
      if (nameIndex > STATIC_TABLE.length) throw new Error(`hpack: literal header's name index ${nameIndex} refers to the dynamic table, which this decoder does not implement (see file header)`);
      name = STATIC_TABLE[nameIndex - 1][0];
    }
    const valueResult = decodeString(buf, pos);
    pos = valueResult.nextPos;
    out.push([name, valueResult.value]);
  }
  return out;
}
