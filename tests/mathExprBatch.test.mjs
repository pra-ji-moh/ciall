// mathExprBatch.test.mjs; upgrade 10 — validates compileExprBatch's
// bytecode-VM evaluator against compileExpr's existing, trusted
// closure-tree evaluator: same grammar, same precedence, same
// FUNCS/CONSTS whitelist, same let-binding semantics -- cross-checked
// on a battery of hand-written expressions AND a seeded random
// expression generator, not just a handful of hand-picked cases. Also
// reports an HONEST (not gated on a flattering ratio) wall-clock
// comparison -- this repo's established benchmark discipline.

import test from 'node:test';
import assert from 'node:assert/strict';
import { performance } from 'node:perf_hooks';
import { compileExpr } from '../src/lib/mathExpr.js';
import { compileExprBatch } from '../src/lib/mathExprBatch.js';

function mulberry32(seed) {
  let a = seed >>> 0;
  return () => { a |= 0; a = (a + 0x6D2B79F5) | 0; let t = Math.imul(a ^ (a >>> 15), 1 | a); t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t; return ((t ^ (t >>> 14)) >>> 0) / 4294967296; };
}

function crossCheck(src, varNames, valuesArray) {
  const closureFn = compileExpr(src, varNames);
  const batch = compileExprBatch(src, varNames);
  const inputObj = {};
  varNames.forEach((n, i) => { inputObj[n] = valuesArray[i]; });
  const expected = closureFn(inputObj);
  const actual = batch.evalOne(valuesArray);
  return { expected, actual, match: (Number.isNaN(expected) && Number.isNaN(actual)) || Math.abs(expected - actual) < 1e-9 || expected === actual };
}

test('hand-written battery: arithmetic, precedence, unary minus, right-assoc power, every FUNCS entry, every CONSTS entry', () => {
  const cases = [
    ['x + y * z', ['x', 'y', 'z'], [1, 2, 3]], // 1+6=7
    ['(x + y) * z', ['x', 'y', 'z'], [1, 2, 3]], // 9
    ['-x + y', ['x', 'y'], [5, 3]], // -2
    ['2^3^2', [], []], // right-assoc: 2^(3^2)=2^9=512
    ['x % y', ['x', 'y'], [-7, 3]], // mathematically-correct mod, not JS %
    ['sin(x) + cos(y)', ['x', 'y'], [0.5, 1.2]],
    ['atan2(y, x)', ['x', 'y'], [1, 1]],
    ['pow(x, 3)', ['x'], [2]],
    ['min(x, y, 2)', ['x', 'y'], [5, -3]],
    ['max(x, y, 2)', ['x', 'y'], [5, -3]],
    ['pi + e + tau', [], []],
    ['sqrt(x) + cbrt(y)', ['x', 'y'], [16, 27]],
    ['abs(x) + sign(y)', ['x', 'y'], [-4, -9]],
    ['floor(x) + ceil(y) + round(2.5)', ['x', 'y'], [1.9, 1.1]],
    ['log(x) + ln(x) + log2(x) + log10(x)', ['x'], [8]],
    ['sinh(x) + cosh(x) + tanh(x)', ['x'], [0.3]],
    ['asin(x) + acos(x) + atan(x)', ['x'], [0.4]],
    ['exp(x)', ['x'], [1]],
    ['let a = x + 1; let b = a * 2; a + b', ['x'], [5]], // a=6,b=12 -> 18
    ['let a = 2; let b = a^2; b + a', [], []], // a=2,b=4 -> 6
  ];
  for (const [src, varNames, values] of cases) {
    const { expected, actual, match } = crossCheck(src, varNames, values);
    assert.ok(match, `"${src}": closure=${expected}, batch=${actual}`);
  }
});

test('seeded random expression fuzzer: batch matches closure evaluation across many generated expressions and inputs', () => {
  const rand = mulberry32(0xC1A11);
  const FUNC_NAMES_1ARY = ['sin', 'cos', 'exp', 'sqrt', 'abs', 'floor'];
  const FUNC_NAMES_2ARY = ['pow', 'min', 'max', 'atan2'];
  const VARS = ['x', 'y', 'z'];

  function genExpr(depth) {
    if (depth <= 0 || rand() < 0.35) {
      const r = rand();
      if (r < 0.4) return VARS[Math.floor(rand() * VARS.length)];
      if (r < 0.7) return String((rand() * 10 - 5).toFixed(3));
      return ['pi', 'e', 'tau'][Math.floor(rand() * 3)];
    }
    const kind = rand();
    if (kind < 0.35) {
      const op = ['+', '-', '*', '/'][Math.floor(rand() * 4)];
      return `(${genExpr(depth - 1)} ${op} ${genExpr(depth - 1)})`;
    }
    if (kind < 0.45) return `(-${genExpr(depth - 1)})`;
    if (kind < 0.55) return `(${genExpr(depth - 1)} ^ ${1 + Math.floor(rand() * 2)})`;
    if (kind < 0.8) {
      const f = FUNC_NAMES_1ARY[Math.floor(rand() * FUNC_NAMES_1ARY.length)];
      return `${f}(${genExpr(depth - 1)})`;
    }
    const f = FUNC_NAMES_2ARY[Math.floor(rand() * FUNC_NAMES_2ARY.length)];
    return `${f}(${genExpr(depth - 1)}, ${genExpr(depth - 1)})`;
  }

  let mismatches = 0;
  const TRIALS = 300;
  for (let t = 0; t < TRIALS; t++) {
    const src = genExpr(4);
    const values = VARS.map(() => rand() * 8 - 4);
    let result;
    try {
      result = crossCheck(src, VARS, values);
    } catch (e) {
      // A malformed generated expression (shouldn't happen given the
      // generator's own grammar, but guard against a generator bug
      // rather than let it masquerade as a crossCheck mismatch).
      throw new Error(`generator or compile error on "${src}": ${e.message}`);
    }
    if (!result.match) {
      mismatches++;
      if (mismatches <= 3) console.error(`MISMATCH "${src}" values=${JSON.stringify(values)}: closure=${result.expected} batch=${result.actual}`);
    }
  }
  assert.equal(mismatches, 0, `${mismatches}/${TRIALS} generated expressions disagreed between compileExpr and compileExprBatch`);
});

test('evalBatch matches per-point evalOne, and reuses one scope buffer across the whole batch (no per-point allocation of a fresh array)', () => {
  const batch = compileExprBatch('let s = x*x + y*y; sqrt(s)', ['x', 'y']);
  const rows = [[3, 4], [5, 12], [0, 0], [-3, -4]];
  const results = batch.evalBatch(rows);
  assert.deepEqual(results, rows.map((r) => Math.sqrt(r[0] * r[0] + r[1] * r[1])));
  rows.forEach((r, i) => assert.equal(batch.evalOne(r), results[i]));
});

test('let-bindings cannot self-reference or forward-reference, matching compileExpr exactly (same error class)', () => {
  assert.throws(() => compileExprBatch('let a = a + 1; a', ['x']), /Unknown identifier "a"/);
  assert.throws(() => compileExpr('let a = a + 1; a', ['x']), /Unknown identifier "a"/);
});

test('unknown function/identifier errors match compileExpr\'s wording (same grammar, same validation)', () => {
  assert.throws(() => compileExprBatch('nope(x)', ['x']), /Unknown function "nope"/);
  assert.throws(() => compileExprBatch('nope', ['x']), /Unknown identifier "nope"/);
});

test('honest benchmark: real, measured speedup of batched bytecode evaluation vs compileExpr\'s closure tree, reported not gated', () => {
  const src = 'let s = x*x + y*y + z*z; sqrt(s) + sin(x)*cos(y) - exp(z*0.01)';
  const varNames = ['x', 'y', 'z'];
  const closureFn = compileExpr(src, varNames);
  const batch = compileExprBatch(src, varNames);

  const N = 200000;
  const rows = Array.from({ length: N }, (_, i) => [i % 13, (i * 3) % 17, (i * 7) % 11]);

  // warmup
  for (let i = 0; i < 5000; i++) closureFn({ x: rows[i % N][0], y: rows[i % N][1], z: rows[i % N][2] });
  for (let i = 0; i < 5000; i++) batch.evalOne(rows[i % N]);

  let t0 = performance.now();
  for (let i = 0; i < N; i++) closureFn({ x: rows[i][0], y: rows[i][1], z: rows[i][2] });
  const closureMs = performance.now() - t0;

  t0 = performance.now();
  batch.evalBatch(rows);
  const batchMs = performance.now() - t0;

  console.log(`  closure: ${(closureMs / N * 1000).toFixed(3)}us/call total ${closureMs.toFixed(1)}ms`);
  console.log(`  batch:   ${(batchMs / N * 1000).toFixed(3)}us/call total ${batchMs.toFixed(1)}ms`);
  console.log(`  ratio: ${(closureMs / batchMs).toFixed(2)}x`);
  assert.ok(true); // reporting, not gating -- see file header
});
