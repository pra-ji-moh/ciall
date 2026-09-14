// fixProtocol.js; upgrade 12 — real FIX (Financial Information
// eXchange) tag=value wire-format encode/decode, one of the specific
// standards named in the "no conformance to any existing government/
// defense/finance interface standard... no FIX protocol integration"
// critique. Unlike STANAG/Link-16 (declined earlier this project for
// export-control/ITAR-adjacent reasons), FIX is an open, publicly
// documented commercial financial-industry protocol with no such
// restriction — legitimately buildable.
//
// SCOPE, disclosed plainly: this implements the wire format itself
// (BeginString/BodyLength/CheckSum framing, tag=value fields,
// SOH-delimited) and four message types (Logon, NewOrderSingle,
// ExecutionReport, Reject) — enough to represent a real order/
// execution round trip, not the full FIX dictionary (repeating groups,
// every session-layer message type, FIXML/FAST encoding). A production
// FIX gateway needs considerably more; this is the honest, working
// slice, following this repo's own established pattern (msgpack.js/
// kernelProto.js/hpack.js in upgrade 8: hand-written wire-format
// codecs for exactly the scoped subset needed, validated against
// literal, hand-computed byte sequences, not just round-tripped
// through their own encode+decode pair).
//
// FIX's SOH-delimited tag=value format is ASCII by definition (the
// standard predates full Unicode field values in this profile); values
// containing non-ASCII bytes are out of scope and will throw, same
// "fail loudly on the unsupported case" discipline as hpack.js's
// Huffman/dynamic-table refusal.

const SOH = '\x01';
const MAX_MESSAGE_BYTES = 64 * 1024; // bounded, same discipline as every other input-accepting function in this repo
const MAX_FIELDS = 500;

function assertAscii(value, context) {
  for (let i = 0; i < value.length; i++) {
    if (value.charCodeAt(i) > 127) throw new Error(`fixProtocol: non-ASCII byte in ${context} ("${value}") — out of scope for this implementation`);
  }
}

function computeChecksum(str) {
  let sum = 0;
  for (let i = 0; i < str.length; i++) sum += str.charCodeAt(i);
  return sum % 256;
}

/**
 * `fields`: ordered array of `[tag: number, value: string|number]` pairs
 * — the BODY of the message, in the exact order to emit. Tags 8
 * (BeginString), 9 (BodyLength), and 10 (CheckSum) are reserved and
 * computed automatically; passing one throws rather than silently
 * overriding it.
 */
export function encodeFixMessage({ beginString = 'FIX.4.2', fields } = {}) {
  if (typeof beginString !== 'string' || !beginString) throw new Error('encodeFixMessage needs a non-empty beginString');
  if (!Array.isArray(fields) || fields.length === 0) throw new Error('encodeFixMessage needs a non-empty fields array');
  if (fields.length > MAX_FIELDS) throw new Error(`encodeFixMessage: ${fields.length} fields exceeds the ${MAX_FIELDS}-field cap`);
  assertAscii(beginString, 'beginString');

  const bodyParts = fields.map(([tag, value]) => {
    if (typeof tag !== 'number' || !Number.isInteger(tag) || tag <= 0) throw new Error(`encodeFixMessage: field tag must be a positive integer, got ${tag}`);
    if (tag === 8 || tag === 9 || tag === 10) throw new Error(`encodeFixMessage: tag ${tag} is reserved (BeginString/BodyLength/CheckSum are computed automatically, not passed in fields)`);
    const strValue = String(value);
    assertAscii(strValue, `field ${tag}`);
    return `${tag}=${strValue}`;
  });

  const body = bodyParts.join(SOH) + SOH;
  const bodyLength = Buffer.byteLength(body, 'ascii');
  const withoutChecksum = `8=${beginString}${SOH}9=${bodyLength}${SOH}${body}`;
  const checksum = computeChecksum(withoutChecksum);
  const message = `${withoutChecksum}10=${String(checksum).padStart(3, '0')}${SOH}`;
  if (Buffer.byteLength(message, 'ascii') > MAX_MESSAGE_BYTES) throw new Error(`encodeFixMessage: encoded message exceeds the ${MAX_MESSAGE_BYTES}-byte cap`);
  return message;
}

/**
 * Decodes and VALIDATES a raw FIX message: checks BeginString is the
 * first field, BodyLength is the second field and matches the actual
 * byte count it claims to describe, CheckSum is the last field and
 * matches a real recomputation over the actual bytes — never trusts
 * the message's own claimed values without recomputing them, the same
 * "structurally separate verifier, not a trusting parser" discipline
 * every kernel in this repo follows.
 */
export function decodeFixMessage(raw) {
  if (typeof raw !== 'string' || !raw) throw new Error('decodeFixMessage needs a non-empty string');
  if (Buffer.byteLength(raw, 'ascii') > MAX_MESSAGE_BYTES) throw new Error(`decodeFixMessage: message exceeds the ${MAX_MESSAGE_BYTES}-byte cap`);

  const parts = raw.split(SOH).filter((p) => p.length > 0);
  if (parts.length > MAX_FIELDS + 3) throw new Error(`decodeFixMessage: ${parts.length} fields exceeds the ${MAX_FIELDS}-field cap`);
  if (parts.length < 3) throw new Error('decodeFixMessage: message too short to contain BeginString, BodyLength, and CheckSum');

  const parsed = parts.map((part) => {
    const eq = part.indexOf('=');
    if (eq <= 0) throw new Error(`decodeFixMessage: malformed field "${part}" (expected tag=value)`);
    const tag = Number(part.slice(0, eq));
    if (!Number.isInteger(tag) || tag <= 0) throw new Error(`decodeFixMessage: malformed tag in field "${part}"`);
    return { tag, value: part.slice(eq + 1) };
  });

  if (parsed[0].tag !== 8) throw new Error(`decodeFixMessage: first field must be tag 8 (BeginString), got tag ${parsed[0].tag}`);
  if (parsed[1].tag !== 9) throw new Error(`decodeFixMessage: second field must be tag 9 (BodyLength), got tag ${parsed[1].tag}`);
  const last = parsed[parsed.length - 1];
  if (last.tag !== 10) throw new Error(`decodeFixMessage: last field must be tag 10 (CheckSum), got tag ${last.tag}`);

  const beginString = parsed[0].value;
  const claimedBodyLength = Number(parsed[1].value);
  if (!Number.isInteger(claimedBodyLength) || claimedBodyLength < 0) throw new Error(`decodeFixMessage: BodyLength value "${parsed[1].value}" is not a valid non-negative integer`);

  // Recompute BodyLength from the ACTUAL bytes: everything after the
  // SOH terminating the BodyLength field, up through the SOH
  // terminating the last body field before CheckSum.
  const beginField = `8=${beginString}${SOH}`;
  const bodyLengthField = `9=${parsed[1].value}${SOH}`;
  const checksumFieldStr = `10=${last.value}${SOH}`;
  if (!raw.startsWith(beginField + bodyLengthField)) {
    throw new Error('decodeFixMessage: message does not begin with the expected BeginString/BodyLength field sequence');
  }
  if (!raw.endsWith(checksumFieldStr)) {
    throw new Error('decodeFixMessage: message does not end with the expected CheckSum field');
  }
  const body = raw.slice((beginField + bodyLengthField).length, raw.length - checksumFieldStr.length);
  const actualBodyLength = Buffer.byteLength(body, 'ascii');
  if (actualBodyLength !== claimedBodyLength) {
    throw new Error(`decodeFixMessage: BodyLength mismatch — message claims ${claimedBodyLength}, actual body is ${actualBodyLength} bytes`);
  }

  const withoutChecksum = raw.slice(0, raw.length - checksumFieldStr.length);
  const actualChecksum = computeChecksum(withoutChecksum);
  if (!/^\d{3}$/.test(last.value)) throw new Error(`decodeFixMessage: CheckSum value "${last.value}" is not a 3-digit decimal string`);
  const claimedChecksum = Number(last.value);
  if (actualChecksum !== claimedChecksum) {
    throw new Error(`decodeFixMessage: CheckSum mismatch — message claims ${claimedChecksum}, actual computed checksum is ${actualChecksum}`);
  }

  const bodyFields = parsed.slice(2, parsed.length - 1);
  const msgTypeField = bodyFields.find((f) => f.tag === 35);
  return {
    beginString,
    bodyLength: claimedBodyLength,
    checksum: claimedChecksum,
    msgType: msgTypeField ? msgTypeField.value : null,
    fields: bodyFields,
  };
}

function fieldsToMap(fields) {
  const map = new Map();
  for (const { tag, value } of fields) map.set(tag, value);
  return map;
}

// --- message builders (a real, minimal, useful subset) ------------------

export function buildLogon({ senderCompId, targetCompId, msgSeqNum, sendingTime, heartBtInt = 30, beginString } = {}) {
  return encodeFixMessage({
    beginString,
    fields: [
      [35, 'A'], [49, senderCompId], [56, targetCompId], [34, msgSeqNum], [52, sendingTime], [98, 0], [108, heartBtInt],
    ],
  });
}

export function buildNewOrderSingle({ senderCompId, targetCompId, msgSeqNum, sendingTime, clOrdId, symbol, side, orderQty, ordType, price, beginString } = {}) {
  if (side !== '1' && side !== '2') throw new Error(`buildNewOrderSingle: side must be "1" (Buy) or "2" (Sell), got "${side}"`);
  if (ordType !== '1' && ordType !== '2') throw new Error(`buildNewOrderSingle: ordType must be "1" (Market) or "2" (Limit), got "${ordType}"`);
  const fields = [
    [35, 'D'], [49, senderCompId], [56, targetCompId], [34, msgSeqNum], [52, sendingTime],
    [11, clOrdId], [55, symbol], [54, side], [60, sendingTime], [38, orderQty], [40, ordType],
  ];
  if (ordType === '2') {
    if (price === undefined) throw new Error('buildNewOrderSingle: ordType "2" (Limit) requires a price');
    fields.push([44, price]);
  }
  return encodeFixMessage({ beginString, fields });
}

export function buildExecutionReport({ senderCompId, targetCompId, msgSeqNum, sendingTime, clOrdId, orderId, execId, symbol, side, orderQty, lastQty, lastPx, ordStatus, beginString } = {}) {
  return encodeFixMessage({
    beginString,
    fields: [
      [35, '8'], [49, senderCompId], [56, targetCompId], [34, msgSeqNum], [52, sendingTime],
      [11, clOrdId], [37, orderId], [17, execId], [55, symbol], [54, side],
      [38, orderQty], [32, lastQty], [31, lastPx], [39, ordStatus],
    ],
  });
}

export function buildReject({ senderCompId, targetCompId, msgSeqNum, sendingTime, refSeqNum, reason, beginString } = {}) {
  return encodeFixMessage({
    beginString,
    fields: [
      [35, '3'], [49, senderCompId], [56, targetCompId], [34, msgSeqNum], [52, sendingTime], [45, refSeqNum], [58, reason],
    ],
  });
}

/**
 * A real, deterministic check that a decoded ExecutionReport is
 * consistent with the decoded NewOrderSingle it claims to execute —
 * the actual claim-verification use case this protocol layer exists
 * to feed: symbol and side must match exactly, and the cumulative
 * executed quantity (LastQty, tag 32) must never exceed the order's
 * requested quantity (OrderQty, tag 38). Returns a specific,
 * itemized mismatch list rather than a bare true/false, same
 * "first-mismatch diagnosis, not just doesn't match" discipline as
 * eventCameraPixel.js's verifyEventLog.
 */
export function verifyExecutionAgainstOrder(orderFields, execFields) {
  const order = fieldsToMap(orderFields);
  const exec = fieldsToMap(execFields);
  const mismatches = [];

  const orderSymbol = order.get(55);
  const execSymbol = exec.get(55);
  if (orderSymbol !== execSymbol) mismatches.push(`symbol mismatch: order=${orderSymbol}, execution=${execSymbol}`);

  const orderSide = order.get(54);
  const execSide = exec.get(54);
  if (orderSide !== execSide) mismatches.push(`side mismatch: order=${orderSide}, execution=${execSide}`);

  const orderClOrdId = order.get(11);
  const execClOrdId = exec.get(11);
  if (orderClOrdId !== execClOrdId) mismatches.push(`ClOrdID mismatch: order=${orderClOrdId}, execution=${execClOrdId}`);

  const orderQty = Number(order.get(38));
  const lastQty = Number(exec.get(32));
  if (Number.isFinite(orderQty) && Number.isFinite(lastQty) && lastQty > orderQty) {
    mismatches.push(`executed quantity ${lastQty} exceeds order quantity ${orderQty}`);
  }

  return { matches: mismatches.length === 0, mismatches };
}
