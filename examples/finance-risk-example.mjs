#!/usr/bin/env node
// examples/finance-risk-example.mjs
//
// The actual proof, not a claim: this file adds ZERO new kernel code. It
// imports the exact same consistencyKernel.js and mcmcSearch.js that were
// verified against physics claims (the Hubble tension test) and logic
// claims (growth-is-slowing contradictions), and points them at a
// finance/quant domain instead — the specific application named in the
// original research: "Measurement tension kernel -> reconciling two
// conflicting risk marks (internal VaR vs. counterparty value)" and
// "MCMC counterexample search -> stress-testing numeric-margin claims."
//
// If land-and-expand is real, this file proves it: no new registry
// entry was even needed for scenario 1 (consistency already handles
// 'measurement' commitments generically); scenario 2 reuses mcmc as-is.
// That's the actual test of "one substrate, many domains" — not whether
// it's described that way, but whether an unmodified kernel produces a
// correct, decisive finding on a claim from a domain it was never
// written for.

import { normalizeCommitments, findContradictions, summarizeConsistency } from '../src/lib/consistencyKernel.js';
import { normalizeMcmcSpec, executeMcmcSearch } from '../src/lib/mcmcSearch.js';

console.log('='.repeat(70));
console.log('SCENARIO 1: reconciling two conflicting risk marks');
console.log('(the exact application named in the original research doc --');
console.log(' "internal VaR vs. counterparty value... a computed sigma-level');
console.log(' tension, not a judgment call")');
console.log('='.repeat(70));

// Two desks quote the same portfolio's 1-day 99% VaR differently. This is
// the SAME 'measurement' commitment form the Hubble-tension test used --
// unmodified. The kernel does not know or care that this is finance.
const commitments = normalizeCommitments({
  commitments: [
    {
      kind: 'measurement',
      quantity: 'portfolio 1-day 99% VaR',
      value: 4.2,       // $M, internal risk model
      uncertainty: 0.3,  // $M
      assumes: ['internal historical-simulation model'],
      source: 'Internal risk desk mark, 2026-08-02',
    },
    {
      kind: 'measurement',
      quantity: 'portfolio 1-day 99% VaR',
      value: 6.8,       // $M, counterparty's independent mark
      uncertainty: 0.4,  // $M
      assumes: ['counterparty Monte Carlo model'],
      source: 'Counterparty ISDA CSA mark, 2026-08-02',
    },
  ],
});

const findings = findContradictions(commitments);
const summary = summarizeConsistency(findings, commitments.length);
console.log(`\nVerdict: ${summary.verdict}`);
console.log(summary.headline);
for (const f of findings) {
  if (f.numeric) {
    console.log(`\n  ${f.about}: ${f.numeric.sigma} sigma apart`);
    console.log(`  ${f.numeric.note}`);
  }
}
console.log('\n(This is the SAME computation, unmodified, that found the Hubble');
console.log(' tension in this codebase\'s own test suite. Same code, different domain.)');

console.log('\n' + '='.repeat(70));
console.log('SCENARIO 2: stress-testing a stated drawdown claim');
console.log('(the exact application named: "MCMC counterexample search ->');
console.log(' stress-testing numeric-margin claims... by searching for the');
console.log(' exact parameter combination that breaks it")');
console.log('='.repeat(70));

// Claim: "the hedge keeps portfolio drawdown under 2% for any underlying
// move within +/-10% and any hedge-ratio slippage within +/-5%." Modeled
// as a simplified linear-hedge P&L margin. Same mcmcSearch.js as the
// original tests -- zero modification.
const spec = normalizeMcmcSpec({
  kind: 'mcmc_search',
  note: 'drawdown margin: positive means the 2% claim is violated',
  params: [
    { name: 'move', domain: [-0.10, 0.10] },        // underlying price move
    { name: 'slippage', domain: [-0.05, 0.05] },      // hedge-ratio slippage
  ],
  // Simplified: unhedged exposure is 8% of notional; the hedge cancels
  // (1 - slippage) of the move. drawdown = |move| * (1 - (1 - slippage))
  // = |move| * slippage, plus a fixed 0.3% base cost. Margin = drawdown - 0.02.
  objective: 'abs(move) * abs(slippage) + 0.003 - 0.02',
});
const result = executeMcmcSearch(spec);

console.log(`\nVerdict: ${result.verdict}`);
if (result.verdict === 'violated') {
  console.log(`Counterexample found: move=${result.bestPoint.move.toFixed(4)}, slippage=${result.bestPoint.slippage.toFixed(4)}`);
  console.log(`Margin at that point: ${result.bestMargin.toFixed(5)} (positive = claim broken)`);
  console.log(result.honesty);
} else {
  console.log(result.honesty);
}
console.log(`\nEvaluations: ${result.evaluations}, seed: ${result.seed} (bit-for-bit reproducible)`);

console.log('\n' + '='.repeat(70));
console.log('What this proves: neither kernel was touched. The claim domain');
console.log('changed from physics/logic to quant risk; the code did not.');
console.log('That is the actual, verifiable version of "one substrate, many');
console.log('domains" -- not a slide, a working example anyone can re-run.');
console.log('='.repeat(70));
