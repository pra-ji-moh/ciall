// msgpack.test.mjs; upgrade 8 — validates the from-scratch MessagePack
// encoder/decoder against LITERAL byte sequences taken directly from
// the public MessagePack spec (not just round-tripped through this
// module's own encode+decode pair, which could share a bug and still
// "pass"), plus round-trip property coverage for nested structures and
// format-boundary transitions (fixstr->str8->str16->str32, etc.).

import test from 'node:test';
import assert from 'node:assert/strict';
import { encode, decode } from '../src/lib/msgpack.js';

function hex(buf) { return Buffer.from(buf).toString('hex'); }

test('literal spec byte sequences: nil, bool, small ints', () => {
  assert.equal(hex(encode(null)), 'c0');
  assert.equal(hex(encode(undefined)), 'c0');
  assert.equal(hex(encode(false)), 'c2');
  assert.equal(hex(encode(true)), 'c3');
  assert.equal(hex(encode(0)), '00');
  assert.equal(hex(encode(127)), '7f');
  assert.equal(hex(encode(-1)), 'ff');
  assert.equal(hex(encode(-32)), 'e0');
});

test('literal spec byte sequences: fixed-width int encodings, smallest applicable form chosen', () => {
  assert.equal(hex(encode(128)), 'cc80');
  assert.equal(hex(encode(255)), 'ccff');
  assert.equal(hex(encode(256)), 'cd0100');
  assert.equal(hex(encode(65535)), 'cdffff');
  assert.equal(hex(encode(65536)), 'ce00010000');
  assert.equal(hex(encode(4294967295)), 'ceffffffff');
  assert.equal(hex(encode(4294967296)), 'cf0000000100000000');
  assert.equal(hex(encode(-33)), 'd0df');
  assert.equal(hex(encode(-128)), 'd080');
  assert.equal(hex(encode(-129)), 'd1ff7f');
  assert.equal(hex(encode(-32768)), 'd18000');
  assert.equal(hex(encode(-32769)), 'd2ffff7fff');
  assert.equal(hex(encode(-2147483648)), 'd280000000');
  assert.equal(hex(encode(-2147483649)), 'd3ffffffff7fffffff');
});

test('literal spec byte sequences: float64 (non-integer numbers, and -0)', () => {
  const b = Buffer.alloc(9);
  b[0] = 0xcb;
  b.writeDoubleBE(1.5, 1);
  assert.equal(hex(encode(1.5)), hex(b));
  // -0 must round-trip as a float carrying its sign, not collapse to
  // integer 0 -- Object.is distinguishes them, JS's == does not.
  const negZeroBytes = encode(-0);
  assert.equal(negZeroBytes[0], 0xcb);
  assert.ok(Object.is(decode(negZeroBytes), -0));
});

test('literal spec byte sequences: fixstr/str8, fixarray, fixmap', () => {
  assert.equal(hex(encode('')), 'a0');
  assert.equal(hex(encode('a')), 'a161');
  assert.equal(hex(encode('hello')), 'a568656c6c6f');
  assert.equal(hex(encode([])), '90');
  assert.equal(hex(encode([1, 2, 3])), '93010203');
  assert.equal(hex(encode({})), '80');
  assert.equal(hex(encode({ a: 1 })), '81a16101');
});

test('format-boundary transitions round-trip correctly: str8/16/32, array16/32, map16', () => {
  for (const len of [31, 32, 255, 256, 65535, 65536, 70000]) {
    const s = 'x'.repeat(len);
    assert.equal(decode(encode(s)), s, `string length ${len}`);
  }
  for (const len of [15, 16, 17, 65536]) {
    const arr = Array.from({ length: len }, (_, i) => i % 256);
    assert.deepEqual(decode(encode(arr)), arr, `array length ${len}`);
  }
  for (const len of [15, 16, 17]) {
    const obj = {};
    for (let i = 0; i < len; i++) obj[`k${i}`] = i;
    assert.deepEqual(decode(encode(obj)), obj, `map length ${len}`);
  }
});

test('round-trip: deeply nested mixed structure (the shape a KernelRequest/KernelResult payload actually is)', () => {
  const value = {
    kind: 'mcmc_search',
    note: 'a claim description',
    params: [
      { name: 'x', domain: [0, 10], integer: false },
      { name: 'n', domain: [1, 5000], integer: true },
    ],
    objective: 'x - 5',
    temperature: 1.5,
    chains: 3,
    nested: { a: [1, 2, { b: null, c: true, d: false, e: -17, f: 3.14159 }] },
    unicode: 'héllo wörld 日本語 🎉',
    emptyArr: [],
    emptyObj: {},
    negative: -0,
    bigish: 9007199254740991, // Number.MAX_SAFE_INTEGER
  };
  const decoded = decode(encode(value));
  // node:assert/strict's deepEqual uses Object.is semantics for
  // primitives, so it already distinguishes -0 from 0 -- no separate
  // sign check needed, this alone would fail if -0 got flattened.
  assert.deepEqual(decoded, value);
});

test('decode rejects a truncated buffer rather than silently returning a partial value', () => {
  const full = encode({ a: 1, b: 'hello world' });
  assert.throws(() => decode(full.subarray(0, full.length - 1)), /unexpected end of buffer/);
});

test('decode rejects unconsumed trailing bytes after a complete value', () => {
  const withExtra = Buffer.concat([encode(42), Buffer.from([0xff])]);
  assert.throws(() => decode(withExtra), /unconsumed trailing byte/);
});

test('decode rejects an unrecognized leading byte rather than misinterpreting it', () => {
  // 0xc1 is a genuinely reserved/unused byte in the MessagePack spec.
  assert.throws(() => decode(Buffer.from([0xc1])), /unsupported\/unrecognized leading byte/);
});

test('encode rejects an unsupported value type rather than silently coercing it', () => {
  assert.throws(() => encode(() => {}), /cannot encode value of type "function"/);
  assert.throws(() => encode(Symbol('x')), /cannot encode value of type "symbol"/);
});

test('a map with a non-string key fails decode loudly (this subset only supports string-keyed maps)', () => {
  // Hand-construct a fixmap with an integer key (0x01) instead of a string.
  const malformed = Buffer.from([0x81, 0x01, 0x02]);
  assert.throws(() => decode(malformed), /map key must be a string/);
});
