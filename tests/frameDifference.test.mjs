import test from 'node:test';
import assert from 'node:assert/strict';
import { computeFrameDifference, triggerOnFrameChange } from '../src/lib/frameDifference.js';

test('identical frames produce zero difference', () => {
  const a = Buffer.from([10, 20, 30, 40, 50]);
  const b = Buffer.from([10, 20, 30, 40, 50]);
  const diff = computeFrameDifference(a, b);
  assert.equal(diff.sumAbsDiff, 0);
  assert.equal(diff.meanAbsDiff, 0);
  assert.equal(diff.maxDiff, 0);
});

test('a single changed byte is correctly measured and localized', () => {
  const a = Buffer.from([10, 20, 30, 40, 50]);
  const b = Buffer.from([10, 20, 90, 40, 50]); // byte offset 2 changed by 60
  const diff = computeFrameDifference(a, b);
  assert.equal(diff.sumAbsDiff, 60);
  assert.equal(diff.meanAbsDiff, 12); // 60 / 5 bytes
  assert.equal(diff.maxDiff, 60);
  assert.equal(diff.maxDiffOffset, 2);
});

test('rejects buffers of different lengths -- different resolution/format is a real error, not silently compared', () => {
  assert.throws(() => computeFrameDifference(Buffer.from([1, 2, 3]), Buffer.from([1, 2])), /same length/);
});

test('rejects empty buffers and non-Buffer input', () => {
  assert.throws(() => computeFrameDifference(Buffer.alloc(0), Buffer.alloc(0)), /must not be empty/);
  assert.throws(() => computeFrameDifference([1, 2, 3], [1, 2, 3]), /raw pixel Buffers/);
});

test('triggerOnFrameChange does NOT fire below threshold', () => {
  const a = Buffer.from([10, 10, 10]);
  const b = Buffer.from([11, 10, 10]); // tiny change, meanAbsDiff = 1/3
  let called = false;
  const result = triggerOnFrameChange(a, b, 5, () => { called = true; });
  assert.equal(result.triggered, false);
  assert.equal(called, false);
});

test('triggerOnFrameChange DOES fire above threshold, and passes the real diff stats to onTrigger', () => {
  const a = Buffer.from([10, 10, 10]);
  const b = Buffer.from([200, 10, 10]); // large localized change
  let receivedDiff = null;
  const result = triggerOnFrameChange(a, b, 5, (diff) => { receivedDiff = diff; });
  assert.equal(result.triggered, true);
  assert.equal(receivedDiff.maxDiff, 190);
  assert.equal(receivedDiff.maxDiffOffset, 0);
});

test('triggerOnFrameChange rejects a negative threshold and a missing callback', () => {
  const a = Buffer.from([1]);
  const b = Buffer.from([2]);
  assert.throws(() => triggerOnFrameChange(a, b, -1, () => {}), /non-negative/);
  assert.throws(() => triggerOnFrameChange(a, b, 1, null), /onTrigger/);
});

// ── the actual "trigger the kernels on pixel change" wiring ────────────

test('end-to-end: a frame change triggers a real kernel verification, not just a callback stub', async () => {
  const { normalizeCheckSpec, executeCheck } = await import('../src/lib/numericCheck.js');

  const before = Buffer.from(new Array(100).fill(10));
  const after = Buffer.from(new Array(100).fill(10).map((v, i) => (i === 50 ? 250 : v))); // one large localized change

  let kernelVerdict = null;
  // 100 bytes, one changes by 240 -> meanAbsDiff = 2.4; threshold set below that
  const { triggered } = triggerOnFrameChange(before, after, 2, (diff) => {
    // vision decided "this is worth checking"; the KERNEL decides what the check finds
    const spec = normalizeCheckSpec({
      kind: 'inequality',
      lhs: 'diffAmount',
      rhs: '1000', // claim: the change never exceeds 1000 (sumAbsDiff units)
      vars: [{ name: 'diffAmount', domain: [diff.sumAbsDiff - 0.5, diff.sumAbsDiff + 0.5] }], // pinned to (near) the exact real measured value
    });
    kernelVerdict = executeCheck(spec).verdict;
  });

  assert.equal(triggered, true);
  assert.equal(kernelVerdict, 'held', 'the kernel, not the vision code, produced the final verdict');
});
