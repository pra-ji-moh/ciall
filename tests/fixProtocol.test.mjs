// fixProtocol.test.mjs; upgrade 12 — validates the FIX wire-format
// codec against a HAND-COMPUTED literal byte sequence first (same
// discipline as msgpack.test.mjs/kernelProto.test.mjs/hpack.test.mjs
// in upgrade 8: never trust a round-trip alone, since a shared bug in
// encode and decode can pass a round-trip and still be non-compliant
// with the real wire format), then exercises real order/execution
// message round trips and tamper detection.

import test from 'node:test';
import assert from 'node:assert/strict';
import {
  encodeFixMessage, decodeFixMessage, buildLogon, buildNewOrderSingle,
  buildExecutionReport, buildReject, verifyExecutionAgainstOrder,
} from '../src/lib/fixProtocol.js';

const SOH = '\x01';

// ---- hand-computed literal byte sequence -------------------------------
//
// Message: BeginString=FIX.4.2, one body field 35=0 (Heartbeat).
// body = "35=0" + SOH -> 5 bytes ('3','5','=','0',SOH) -> BodyLength=5.
// withoutChecksum = "8=FIX.4.2" SOH "9=5" SOH "35=0" SOH
// CheckSum = (sum of ASCII/byte values of every character in
// withoutChecksum) mod 256, hand-summed below character by character:
//   8(56) =(61) F(70) I(73) X(88) .(46) 4(52) .(46) 2(50) SOH(1)
//   9(57) =(61) 5(53) SOH(1)
//   3(51) 5(53) =(61) 0(48) SOH(1)
// running total: 56,117,187,260,348,394,446,492,542,543,
//                600,661,714,715,766,819,880,928,929
// 929 mod 256 = 929 - 3*256 = 929 - 768 = 161
const EXPECTED_HEARTBEAT_MESSAGE = `8=FIX.4.2${SOH}9=5${SOH}35=0${SOH}10=161${SOH}`;

test('encodeFixMessage produces the exact hand-computed byte sequence for a minimal Heartbeat message', () => {
  const message = encodeFixMessage({ beginString: 'FIX.4.2', fields: [[35, '0']] });
  assert.equal(message, EXPECTED_HEARTBEAT_MESSAGE);
});

test('decodeFixMessage parses the same literal byte sequence and recomputes BodyLength=5 and CheckSum=161 independently, matching the claimed values', () => {
  const decoded = decodeFixMessage(EXPECTED_HEARTBEAT_MESSAGE);
  assert.equal(decoded.beginString, 'FIX.4.2');
  assert.equal(decoded.bodyLength, 5);
  assert.equal(decoded.checksum, 161);
  assert.equal(decoded.msgType, '0');
  assert.deepEqual(decoded.fields, [{ tag: 35, value: '0' }]);
});

// ---- reserved tags / validation -----------------------------------------

test('encodeFixMessage refuses tags 8, 9, and 10 in the caller-supplied fields (computed automatically, not overridable)', () => {
  assert.throws(() => encodeFixMessage({ fields: [[8, 'FIX.4.2']] }), /tag 8 is reserved/);
  assert.throws(() => encodeFixMessage({ fields: [[9, '5']] }), /tag 9 is reserved/);
  assert.throws(() => encodeFixMessage({ fields: [[10, '161']] }), /tag 10 is reserved/);
});

test('encodeFixMessage rejects non-ASCII field values', () => {
  assert.throws(() => encodeFixMessage({ fields: [[58, 'café']] }), /non-ASCII byte/);
});

test('encodeFixMessage rejects an empty fields array', () => {
  assert.throws(() => encodeFixMessage({ fields: [] }), /non-empty fields array/);
});

// ---- tamper detection: decode must recompute, never trust -------------

test('decodeFixMessage rejects a message whose CheckSum does not match a real recomputation', () => {
  const tampered = EXPECTED_HEARTBEAT_MESSAGE.replace('10=161', '10=999');
  assert.throws(() => decodeFixMessage(tampered), /CheckSum mismatch/);
});

test('decodeFixMessage rejects a message whose claimed BodyLength does not match the actual body byte count', () => {
  const tampered = EXPECTED_HEARTBEAT_MESSAGE.replace('9=5', '9=99');
  assert.throws(() => decodeFixMessage(tampered), /does not end with the expected CheckSum field|BodyLength mismatch/);
});

test('decodeFixMessage rejects a message not starting with BeginString/BodyLength in order', () => {
  assert.throws(() => decodeFixMessage(`35=0${SOH}8=FIX.4.2${SOH}9=5${SOH}10=161${SOH}`), /first field must be tag 8/);
});

test('decodeFixMessage rejects a message not ending with CheckSum', () => {
  assert.throws(() => decodeFixMessage(`8=FIX.4.2${SOH}9=5${SOH}35=0${SOH}`), /last field must be tag 10/);
});

test('decodeFixMessage rejects a malformed field with no "="', () => {
  assert.throws(() => decodeFixMessage(`8=FIX.4.2${SOH}9=5${SOH}NOTAFIELD${SOH}10=161${SOH}`), /malformed field/);
});

test('decodeFixMessage rejects a non-3-digit CheckSum value', () => {
  assert.throws(() => decodeFixMessage(`8=FIX.4.2${SOH}9=5${SOH}35=0${SOH}10=61${SOH}`), /not a 3-digit decimal string/);
});

// ---- real message builders + round trip --------------------------------

test('buildLogon produces a message that decodes back to a Logon (msgType "A")', () => {
  const msg = buildLogon({ senderCompId: 'BUYER', targetCompId: 'SELLER', msgSeqNum: 1, sendingTime: '20260101-00:00:00.000' });
  const decoded = decodeFixMessage(msg);
  assert.equal(decoded.msgType, 'A');
});

test('buildNewOrderSingle round-trips a real limit buy order', () => {
  const msg = buildNewOrderSingle({
    senderCompId: 'BUYER', targetCompId: 'SELLER', msgSeqNum: 2, sendingTime: '20260101-00:00:01.000',
    clOrdId: 'ORD-1', symbol: 'AAPL', side: '1', orderQty: 100, ordType: '2', price: 150.25,
  });
  const decoded = decodeFixMessage(msg);
  assert.equal(decoded.msgType, 'D');
  const map = new Map(decoded.fields.map((f) => [f.tag, f.value]));
  assert.equal(map.get(55), 'AAPL');
  assert.equal(map.get(54), '1');
  assert.equal(map.get(38), '100');
  assert.equal(map.get(44), '150.25');
});

test('buildNewOrderSingle rejects an invalid side or ordType', () => {
  const base = { senderCompId: 'B', targetCompId: 'S', msgSeqNum: 1, sendingTime: 'x', clOrdId: 'c', symbol: 'AAPL', orderQty: 1 };
  assert.throws(() => buildNewOrderSingle({ ...base, side: '9', ordType: '1' }), /side must be/);
  assert.throws(() => buildNewOrderSingle({ ...base, side: '1', ordType: '9' }), /ordType must be/);
});

test('buildNewOrderSingle requires a price for a Limit order', () => {
  const base = { senderCompId: 'B', targetCompId: 'S', msgSeqNum: 1, sendingTime: 'x', clOrdId: 'c', symbol: 'AAPL', orderQty: 1, side: '1', ordType: '2' };
  assert.throws(() => buildNewOrderSingle(base), /requires a price/);
});

test('buildExecutionReport round-trips a real fill', () => {
  const msg = buildExecutionReport({
    senderCompId: 'SELLER', targetCompId: 'BUYER', msgSeqNum: 3, sendingTime: '20260101-00:00:02.000',
    clOrdId: 'ORD-1', orderId: 'EX-ORD-1', execId: 'EXEC-1', symbol: 'AAPL', side: '1',
    orderQty: 100, lastQty: 100, lastPx: 150.25, ordStatus: '2',
  });
  const decoded = decodeFixMessage(msg);
  assert.equal(decoded.msgType, '8');
});

test('buildReject round-trips', () => {
  const msg = buildReject({ senderCompId: 'S', targetCompId: 'B', msgSeqNum: 4, sendingTime: 'x', refSeqNum: 2, reason: 'unsupported message type' });
  const decoded = decodeFixMessage(msg);
  assert.equal(decoded.msgType, '3');
});

// ---- verifyExecutionAgainstOrder: real claim-verification use case ----

test('verifyExecutionAgainstOrder holds for a matching fill within order quantity', () => {
  const orderMsg = buildNewOrderSingle({ senderCompId: 'B', targetCompId: 'S', msgSeqNum: 1, sendingTime: 'x', clOrdId: 'ORD-1', symbol: 'AAPL', side: '1', orderQty: 100, ordType: '1' });
  const execMsg = buildExecutionReport({ senderCompId: 'S', targetCompId: 'B', msgSeqNum: 2, sendingTime: 'x', clOrdId: 'ORD-1', orderId: 'X', execId: 'E1', symbol: 'AAPL', side: '1', orderQty: 100, lastQty: 60, lastPx: 150, ordStatus: '1' });
  const result = verifyExecutionAgainstOrder(decodeFixMessage(orderMsg).fields, decodeFixMessage(execMsg).fields);
  assert.equal(result.matches, true);
  assert.deepEqual(result.mismatches, []);
});

test('verifyExecutionAgainstOrder catches an execution reporting a symbol different from the order', () => {
  const orderMsg = buildNewOrderSingle({ senderCompId: 'B', targetCompId: 'S', msgSeqNum: 1, sendingTime: 'x', clOrdId: 'ORD-1', symbol: 'AAPL', side: '1', orderQty: 100, ordType: '1' });
  const execMsg = buildExecutionReport({ senderCompId: 'S', targetCompId: 'B', msgSeqNum: 2, sendingTime: 'x', clOrdId: 'ORD-1', orderId: 'X', execId: 'E1', symbol: 'MSFT', side: '1', orderQty: 100, lastQty: 10, lastPx: 150, ordStatus: '1' });
  const result = verifyExecutionAgainstOrder(decodeFixMessage(orderMsg).fields, decodeFixMessage(execMsg).fields);
  assert.equal(result.matches, false);
  assert.ok(result.mismatches.some((m) => m.includes('symbol mismatch')));
});

test('verifyExecutionAgainstOrder catches an execution reporting more quantity than the order requested', () => {
  const orderMsg = buildNewOrderSingle({ senderCompId: 'B', targetCompId: 'S', msgSeqNum: 1, sendingTime: 'x', clOrdId: 'ORD-1', symbol: 'AAPL', side: '1', orderQty: 100, ordType: '1' });
  const execMsg = buildExecutionReport({ senderCompId: 'S', targetCompId: 'B', msgSeqNum: 2, sendingTime: 'x', clOrdId: 'ORD-1', orderId: 'X', execId: 'E1', symbol: 'AAPL', side: '1', orderQty: 100, lastQty: 150, lastPx: 150, ordStatus: '2' });
  const result = verifyExecutionAgainstOrder(decodeFixMessage(orderMsg).fields, decodeFixMessage(execMsg).fields);
  assert.equal(result.matches, false);
  assert.ok(result.mismatches.some((m) => m.includes('exceeds order quantity')));
});

test('verifyExecutionAgainstOrder catches a mismatched ClOrdID (execution claims to fill a different order)', () => {
  const orderMsg = buildNewOrderSingle({ senderCompId: 'B', targetCompId: 'S', msgSeqNum: 1, sendingTime: 'x', clOrdId: 'ORD-1', symbol: 'AAPL', side: '1', orderQty: 100, ordType: '1' });
  const execMsg = buildExecutionReport({ senderCompId: 'S', targetCompId: 'B', msgSeqNum: 2, sendingTime: 'x', clOrdId: 'ORD-2', orderId: 'X', execId: 'E1', symbol: 'AAPL', side: '1', orderQty: 100, lastQty: 10, lastPx: 150, ordStatus: '1' });
  const result = verifyExecutionAgainstOrder(decodeFixMessage(orderMsg).fields, decodeFixMessage(execMsg).fields);
  assert.equal(result.matches, false);
  assert.ok(result.mismatches.some((m) => m.includes('ClOrdID mismatch')));
});
