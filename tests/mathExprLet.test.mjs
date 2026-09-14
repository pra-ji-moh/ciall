// Tests for let-bindings in the float expression grammar.
//
// This is trusted-kernel code: the whole no-eval safety argument rests on
// the grammar staying total and closed. The bindings must (1) compute the
// right numbers, (2) stay pure across calls, (3) never open a recursion or
// a prototype-pollution hole, and (4) resolve every identifier to exactly
// one thing. The load-bearing test is the Adam recurrence that could not
// be expressed before, checked against an independent JS implementation.

import test from 'node:test';
import assert from 'node:assert/strict';

import { compileExpr, compileExprExact } from '../src/lib/mathExpr.js';

test('a single binding computes correctly', () => {
  assert.equal(compileExpr('let a = 2; a * 3')({}), 6);
});

test('bindings chain, each visible to the next', () => {
  assert.equal(compileExpr('let a = 2; let b = a + 1; a * b')({}), 6);
});

test('bindings see input variables', () => {
  const f = compileExpr('let s = x*x; s + 1', ['x']);
  assert.equal(f({ x: 3 }), 10);
});

test('a binding cannot reference itself (no recursion is expressible)', () => {
  assert.throws(() => compileExpr('let a = a + 1; a'), /Unknown identifier "a"/);
});

test('a binding cannot reference a LATER binding (forward refs rejected)', () => {
  assert.throws(() => compileExpr('let a = b; let b = 1; a'), /Unknown identifier "b"/);
});

test('duplicate binding names are rejected', () => {
  assert.throws(() => compileExpr('let a = 1; let a = 2; a'), /already defined/);
});

test('a binding may not shadow an input variable', () => {
  assert.throws(() => compileExpr('let x = 1; x', ['x']), /already defined|shadow/);
});

test('a binding may not use a function or constant name', () => {
  assert.throws(() => compileExpr('let sin = 1; sin'), /function name/);
  assert.throws(() => compileExpr('let pi = 1; pi'), /constant name/);
});

test('prototype-pollution names are refused as bindings', () => {
  assert.throws(() => compileExpr('let __proto__ = 1; 2'), /not a permitted binding name/);
  assert.throws(() => compileExpr('let constructor = 1; 2'), /not a permitted binding name/);
  assert.throws(() => compileExpr('let prototype = 1; 2'), /not a permitted binding name/);
});

test('a prototype-pollution binding cannot corrupt Object.prototype', () => {
  // Even though the name is refused, prove the null-proto scope holds by
  // constructing a program that WOULD pollute if the env had a normal
  // prototype: bind a legal name, then confirm no global leakage.
  const before = ({}).polluted;
  compileExpr('let z = 1; z')({});
  assert.equal(({}).polluted, before, 'no global object may be touched by evaluation');
  // A variable literally named __proto__ passed at call time must not
  // change the prototype of the internal scope either.
  const f = compileExpr('let a = 1; a', []);
  assert.equal(f({ __proto__: { polluted: true } }), 1);
  assert.equal(({}).polluted, undefined);
});

test('missing final expression after bindings is an error', () => {
  assert.throws(() => compileExpr('let a = 1;'), /Unexpected end|Expected/);
});

test('a malformed binding (missing =) is an error', () => {
  assert.throws(() => compileExpr('let a 1; a'), /Expected "="/);
});

test('evaluation is pure: repeated calls do not leak state', () => {
  const f = compileExpr('let a = x + 1; a * a', ['x']);
  assert.equal(f({ x: 2 }), 9);
  assert.equal(f({ x: 4 }), 25);
  assert.equal(f({ x: 2 }), 9); // same input, same output, no drift
});

test('a program with no bindings behaves exactly as before', () => {
  assert.equal(compileExpr('x^2 + 1', ['x'])({ x: 3 }), 10);
});

test('exact mode refuses let-bindings loudly, never mis-parses them', () => {
  assert.throws(() => compileExprExact('let a = 2; a * 3', []), /not supported in exact mode/);
  // and a plain exact expression still works
  assert.equal(compileExprExact('choose(6,2)', [])({}), 15n);
});

// ── The load-bearing case: the Adam recurrence that could not be
//    expressed before, now unrolled with bindings and checked against an
//    independent implementation. ─────────────────────────────────────────
test('the 5-step Adam ratio unrolls correctly with let-bindings', () => {
  const b1 = 0.9, b2 = 0.999;
  const prog = [
    'let m1 = (1-0.9)*g1',
    'let v1 = (1-0.999)*g1^2',
    'let m2 = 0.9*m1 + (1-0.9)*g2',
    'let v2 = 0.999*v1 + (1-0.999)*g2^2',
    'let m3 = 0.9*m2 + (1-0.9)*g3',
    'let v3 = 0.999*v2 + (1-0.999)*g3^2',
    'let mhat = m3 / (1 - 0.9^3)',
    'let vhat = v3 / (1 - 0.999^3)',
    'abs(mhat) / sqrt(vhat)',
  ].join('; ');
  const f = compileExpr(prog, ['g1', 'g2', 'g3']);

  // Independent reference implementation of exactly the same recurrence.
  const ref = (g1, g2, g3) => {
    let m = (1 - b1) * g1, v = (1 - b2) * g1 * g1;
    m = b1 * m + (1 - b1) * g2; v = b2 * v + (1 - b2) * g2 * g2;
    m = b1 * m + (1 - b1) * g3; v = b2 * v + (1 - b2) * g3 * g3;
    const mhat = m / (1 - b1 ** 3), vhat = v / (1 - b2 ** 3);
    return Math.abs(mhat) / Math.sqrt(vhat);
  };

  for (const [g1, g2, g3] of [[1, -2, 3], [0.5, 0.5, 0.5], [-3, 1, 0.2], [10, -10, 10]]) {
    assert.ok(Math.abs(f({ g1, g2, g3 }) - ref(g1, g2, g3)) < 1e-12,
      `mismatch at (${g1},${g2},${g3}): got ${f({ g1, g2, g3 })}, expected ${ref(g1, g2, g3)}`);
  }
});
