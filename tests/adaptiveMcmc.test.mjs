// adaptiveMcmc.test.mjs; every scenario here was run by hand first to
// confirm the real numbers before being pinned down as assertions.

import test from 'node:test';
import assert from 'node:assert/strict';
import { runAdaptiveMcmcSearch } from '../src/lib/adaptiveMcmc.js';

test('a narrow human-chosen domain that would hide the real violation gets automatically expanded until it is found', () => {
  // x-5 is monotonically increasing; within [0,4] it never exceeds 0
  // (a human choosing this domain would get a false "held"). The real
  // violation is at x>5, outside the original domain entirely.
  const result = runAdaptiveMcmcSearch({
    kind: 'mcmc_search',
    params: [{ name: 'x', domain: [0, 4] }],
    objective: 'x - 5',
  });
  assert.equal(result.verdict, 'violated');
  assert.ok(result.bestPoint.x > 5, 'the found counterexample must be past the ORIGINAL domain the human chose');
  assert.ok(result.expansions.length > 0, 'expansion must have actually happened to find this');
  assert.deepEqual(result.expansions[0].paramsBeforeExpansion[0].domain, [0, 4], 'the original domain must be recorded, not silently discarded');
});

test('a well-chosen domain with the counterexample genuinely interior performs ZERO expansions', () => {
  const result = runAdaptiveMcmcSearch({
    kind: 'mcmc_search',
    params: [{ name: 'x', domain: [0, 10] }],
    objective: 'sin(x) - 0.999', // violated near x=pi/2, well inside [0,10]
  });
  assert.equal(result.verdict, 'violated');
  assert.equal(result.expansions.length, 0);
  assert.deepEqual(result.finalParams[0].domain, [0, 10], 'no bias correction was needed, so none was applied -- the original domain stands unchanged');
});

test('a genuinely safe claim reports "held" honestly, not forced into a false verdict by expansion', () => {
  const result = runAdaptiveMcmcSearch({
    kind: 'mcmc_search',
    params: [{ name: 'x', domain: [-1, 1] }],
    objective: '-x*x - 5', // maximum is at the interior point x=0; genuinely holds everywhere
  });
  assert.equal(result.verdict, 'held');
  assert.equal(result.expansions.length, 0);
});

test('maxExpansions is a real cap -- a pathological monotonic-forever objective stops expanding and reports honestly, not infinitely', () => {
  const result = runAdaptiveMcmcSearch(
    { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 1] }], objective: 'x' }, // no upper bound will ever satisfy this; always hugs the top edge
    { maxExpansions: 2 },
  );
  assert.equal(result.expansions.length, 2, 'must stop at exactly the configured cap, not run forever');
});

test('expansions list carries the exact reason and the point that triggered each expansion, not just a count', () => {
  const result = runAdaptiveMcmcSearch({
    kind: 'mcmc_search',
    params: [{ name: 'x', domain: [0, 4] }],
    objective: 'x - 5',
  });
  for (const e of result.expansions) {
    assert.match(e.reason, /hugs a domain edge/);
    assert.ok(Number.isFinite(e.bestPoint.x));
  }
});
