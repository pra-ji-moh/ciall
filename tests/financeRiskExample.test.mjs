// financeRiskExample.test.mjs; the assertions behind
// examples/finance-risk-example.mjs, so "the substrate generalizes to
// finance with zero new kernel code" stays a checked fact, not a claim
// that quietly stops being true after some future kernel change.

import test from 'node:test';
import assert from 'node:assert/strict';
import { normalizeCommitments, findContradictions } from '../src/lib/consistencyKernel.js';
import { normalizeMcmcSpec, executeMcmcSearch } from '../src/lib/mcmcSearch.js';

test('the unmodified consistency kernel finds a decisive tension between two conflicting risk marks', () => {
  const commitments = normalizeCommitments({
    commitments: [
      { kind: 'measurement', quantity: 'portfolio 1-day 99% VaR', value: 4.2, uncertainty: 0.3, assumes: ['internal historical-simulation model'], source: 'internal' },
      { kind: 'measurement', quantity: 'portfolio 1-day 99% VaR', value: 6.8, uncertainty: 0.4, assumes: ['counterparty Monte Carlo model'], source: 'counterparty' },
    ],
  });
  const findings = findContradictions(commitments);
  assert.equal(findings.length, 1);
  assert.ok(findings[0].numeric.sigma >= 5, 'a real 5+ sigma tension must be found, matching the physics decisive threshold');
  assert.ok(findings[0].numeric.assumptions.includes('internal historical-simulation model'));
});

test('two risk marks within the same tolerance produce NO false alarm', () => {
  const commitments = normalizeCommitments({
    commitments: [
      { kind: 'measurement', quantity: 'portfolio 1-day 99% VaR', value: 5.0, uncertainty: 0.3, source: 'internal' },
      { kind: 'measurement', quantity: 'portfolio 1-day 99% VaR', value: 5.1, uncertainty: 0.3, source: 'counterparty' },
    ],
  });
  assert.equal(findContradictions(commitments).length, 0);
});

test('the unmodified mcmc kernel correctly holds a genuinely safe drawdown claim', () => {
  const spec = normalizeMcmcSpec({
    kind: 'mcmc_search',
    params: [{ name: 'move', domain: [-0.10, 0.10] }, { name: 'slippage', domain: [-0.05, 0.05] }],
    objective: 'abs(move) * abs(slippage) + 0.003 - 0.02',
  });
  const result = executeMcmcSearch(spec);
  assert.equal(result.verdict, 'held');
});

test('the unmodified mcmc kernel finds a real counterexample when the claimed margin is actually too tight', () => {
  const spec = normalizeMcmcSpec({
    kind: 'mcmc_search',
    params: [{ name: 'move', domain: [-0.10, 0.10] }, { name: 'slippage', domain: [-0.05, 0.05] }],
    // Same shape, tighter claimed bound (0.5% instead of 2%) -- should break.
    objective: 'abs(move) * abs(slippage) + 0.003 - 0.005',
  });
  const result = executeMcmcSearch(spec);
  assert.equal(result.verdict, 'violated');
  assert.ok(result.bestMargin > 0);
});
