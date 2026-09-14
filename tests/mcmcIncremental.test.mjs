// mcmcIncremental.test.mjs; upgrade 6 coverage — MH chain warm-start in
// mcmcSearch.js. executeMcmcSearch/normalizeMcmcSpec keep their exact
// pre-upgrade signature; every test here calls them exactly as any
// existing caller would. Each chain's evaluation count is DETERMINISTIC
// (1 initial + `total` proposal evaluations per chain, no early timeout
// at these tiny sizes), so warm vs cold behavior is checked by exact
// arithmetic rather than a fuzzy performance threshold: cold spends
// chains*(1+burnIn+samples) evaluations, a fully warm-started call spends
// chains*(1+samples) — burn-in skipped entirely.

import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import { shutdownPool } from '../src/lib/kernelWorkerPool.js';

// The SMC-mode test below drives kernelWorkerPool.js's worker pool (via
// whichever `?fresh=` mcmcSearch.js instance calls it -- the pool module
// itself is never versioned, so every instance shares the one pool);
// must be torn down explicitly for this file's process to exit. Same
// requirement as mcmcSmc.test.mjs.
after(() => shutdownPool());

let freshCounter = 0;
async function freshMcmc() {
  freshCounter++;
  return import(`../src/lib/mcmcSearch.js?fresh=${freshCounter}`);
}

function baseRawSpec(overrides = {}) {
  return {
    kind: 'mcmc_search',
    params: [{ name: 'x', domain: [0, 10] }],
    objective: 'x - 5',
    chains: 2,
    samples: 120,
    burnIn: 60,
    ...overrides,
  };
}

test('warm-start: an edit that keeps the structural fingerprint (bounds-only change) skips burn-in entirely', async () => {
  const mcmc = await freshMcmc();
  const specA = mcmc.normalizeMcmcSpec(baseRawSpec());
  const resultA = mcmc.executeMcmcSearch(specA);
  assert.equal(resultA.evaluations, specA.chains * (1 + specA.burnIn + specA.samples), 'first call on a fresh instance must be fully cold');

  // Same params/objective (same fingerprint), only the domain -- a
  // "bounds" edit -- changed.
  const specB = mcmc.normalizeMcmcSpec(baseRawSpec({ params: [{ name: 'x', domain: [0, 20] }] }));
  const resultB = mcmc.executeMcmcSearch(specB);
  assert.equal(resultB.evaluations, specB.chains * (1 + specB.samples), 'a bounds-only edit must warm-start (skip burn-in) on every chain');
  assert.ok(resultB.evaluations < resultA.evaluations, 'warm-started call must spend fewer evaluations than the cold call it followed');

  // The warm result must still be a well-formed, valid result -- warm-
  // starting is a performance path, not a shortcut that degrades output
  // shape or correctness.
  assert.ok(['violated', 'held', 'inconclusive'].includes(resultB.verdict));
  if (resultB.verdict !== 'inconclusive') {
    assert.ok(Number.isFinite(resultB.bestPoint.x));
    assert.ok(resultB.bestPoint.x >= 0 && resultB.bestPoint.x <= 20, 'resumed position must respect the NEW (possibly narrower/wider) domain');
  }
});

test('warm-start: a temperature/chains/samples/burnIn-only edit ("tolerance") also warm-starts', async () => {
  const mcmc = await freshMcmc();
  const specA = mcmc.normalizeMcmcSpec(baseRawSpec());
  mcmc.executeMcmcSearch(specA);

  const specB = mcmc.normalizeMcmcSpec(baseRawSpec({ temperature: 2, samples: 200 }));
  const resultB = mcmc.executeMcmcSearch(specB);
  assert.equal(resultB.evaluations, specB.chains * (1 + specB.samples), 'a temperature/samples-only edit must still warm-start');
});

test('exact repeat: calling with a BYTE-IDENTICAL spec twice never warm-starts and stays bit-for-bit deterministic', async () => {
  const mcmc = await freshMcmc();
  const spec = mcmc.normalizeMcmcSpec(baseRawSpec());
  const a = mcmc.executeMcmcSearch(spec);
  const b = mcmc.executeMcmcSearch(spec);
  assert.equal(a.evaluations, spec.chains * (1 + spec.burnIn + spec.samples));
  assert.equal(b.evaluations, spec.chains * (1 + spec.burnIn + spec.samples), 'a literal repeat call must run fully cold, not warm-start off itself');
  assert.deepEqual(a.bestPoint, b.bestPoint);
  assert.equal(a.bestMargin, b.bestMargin);
});

test('exact repeat is unaffected by a purely cosmetic field (note) differing between calls', async () => {
  const mcmc = await freshMcmc();
  const specA = mcmc.normalizeMcmcSpec(baseRawSpec({ note: 'first phrasing of the same search' }));
  const a = mcmc.executeMcmcSearch(specA);
  const specB = mcmc.normalizeMcmcSpec(baseRawSpec({ note: 'a differently-worded description, same search' }));
  const b = mcmc.executeMcmcSearch(specB);
  assert.equal(b.evaluations, specB.chains * (1 + specB.burnIn + specB.samples), 'a note-only difference must not be mistaken for a computational edit');
  assert.deepEqual(a.bestPoint, b.bestPoint);
});

test('fingerprint change (different objective): full restart, no warm-start shortcut taken', async () => {
  const mcmc = await freshMcmc();
  const specA = mcmc.normalizeMcmcSpec(baseRawSpec());
  mcmc.executeMcmcSearch(specA);

  const specB = mcmc.normalizeMcmcSpec(baseRawSpec({ objective: '5 - x' })); // genuinely different search
  const resultB = mcmc.executeMcmcSearch(specB);
  assert.equal(resultB.evaluations, specB.chains * (1 + specB.burnIn + specB.samples), 'a structurally different search must never warm-start');
});

test('fingerprint change (different param set): full restart', async () => {
  const mcmc = await freshMcmc();
  const specA = mcmc.normalizeMcmcSpec(baseRawSpec());
  mcmc.executeMcmcSearch(specA);

  const specB = mcmc.normalizeMcmcSpec(baseRawSpec({ params: [{ name: 'y', domain: [0, 10] }], objective: 'y - 5' }));
  const resultB = mcmc.executeMcmcSearch(specB);
  assert.equal(resultB.evaluations, specB.chains * (1 + specB.burnIn + specB.samples));
});

test('cache isolation: a fresh module instance never sees another instance\'s warm state', async () => {
  const mcmcOne = await freshMcmc();
  const specA = mcmcOne.normalizeMcmcSpec(baseRawSpec());
  mcmcOne.executeMcmcSearch(specA);
  const specB = mcmcOne.normalizeMcmcSpec(baseRawSpec({ temperature: 3 }));
  const warmResult = mcmcOne.executeMcmcSearch(specB);
  assert.equal(warmResult.evaluations, specB.chains * (1 + specB.samples), 'sanity: this WOULD warm-start on the shared instance');

  // The exact same "edit" spec, as the FIRST call on a brand-new
  // instance, must be fully cold -- each worker thread's cache is its
  // own, per the task spec's per-worker-instance requirement.
  const mcmcTwo = await freshMcmc();
  const coldOnFreshInstance = mcmcTwo.executeMcmcSearch(specB);
  assert.equal(coldOnFreshInstance.evaluations, specB.chains * (1 + specB.burnIn + specB.samples), 'a fresh instance must never inherit another instance\'s cache');
});

test('SMC mode is unaffected: still fully re-initializes every call (no MH-style warm-start applied to it)', async () => {
  const mcmc = await freshMcmc();
  const spec = mcmc.normalizeMcmcSpec({ kind: 'mcmc_search', mode: 'smc', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5', N: 8 });
  const a = await mcmc.executeMcmcSearch(spec);
  const b = await mcmc.executeMcmcSearch(spec);
  assert.deepEqual(a.bestPoint, b.bestPoint, 'SMC must stay bit-for-bit reproducible across repeat calls, exactly as before upgrade 6');
});
