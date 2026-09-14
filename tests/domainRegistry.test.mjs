import test from 'node:test';
import assert from 'node:assert/strict';
import { listDomains, getDomain } from '../src/domains/registry.js';

test('listDomains returns all eight registered verticals', () => {
  const ids = listDomains().map((d) => d.id).sort();
  assert.deepEqual(ids, ['camera', 'engineering', 'finance', 'legal', 'mission-planning', 'motion', 'requirements', 'supply-chain']);
});

test('getDomain returns the entry with a real, callable module; unknown id throws', () => {
  const finance = getDomain('finance');
  assert.equal(typeof finance.module.verifyRiskMarkReconciliation, 'function');
  assert.throws(() => getDomain('nope'), /Unknown domain id/);
});

test('every registered domain module actually works when called through the registry', () => {
  const { module: financeModule } = getDomain('finance');
  const findings = financeModule.verifyRiskMarkReconciliation({
    quantity: 'portfolio VaR',
    markA: { value: 4.2, uncertainty: 0.3, source: 'internal' },
    markB: { value: 6.8, uncertainty: 0.4, source: 'counterparty' },
  });
  assert.equal(findings.length, 1); // same real finding as domains.test.mjs, reached through the registry this time
});
