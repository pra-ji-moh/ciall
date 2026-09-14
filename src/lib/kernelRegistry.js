// kernelRegistry.js; the substrate layer for Ciall's PLTR-level direction.
//
// WHY THIS EXISTS. Every instrument in this directory (consistencyKernel,
// mcmcSearch, satKernel, dynamicsCheck, domainOfValidity, combinatorialSearch,
// orderConsistency, dimensionalAnalysis, numericCheck) already follows the
// same shape: a model DESIGNS a spec against a claim, normalize() validates
// and clamps it, and a deterministic (or seeded-stochastic) executor RUNS it
// on-device, independent of the model that proposed it. That shape is the
// actual product, not any one kernel. This file names it once as a common
// interface, so a new domain (engineering margins, supply-chain resilience,
// requirements consistency, mission-planning diagnostics, ...) is a new
// registry ENTRY, not a new bespoke call site threaded through the UI.
//
// This is additive only. No existing call site (ClientWorkspace.jsx and
// friends) is modified or required to go through this registry; every
// import below re-exports functions that already exist, are already
// tested, and keep working exactly as they do today whether or not
// anything ever calls listKernels()/getKernel() at all. This is the first
// increment of the ontology-layer direction discussed, not a refactor of
// the working system.
//
// Each entry declares:
//   id                 stable string, used as the registry key
//   label              short human-readable name
//   domain             tags describing what kind of claim this applies to
//   deterministic      true if re-running the same spec always gives the
//                      same verdict with no randomness at all (a pure
//                      kernel); false if it is seeded-stochastic (MCMC) —
//                      reproducible bit-for-bit given the seed, but not
//                      "deterministic" in the stronger sense the logic
//                      kernels claim
//   needsModelExtraction  true if a model must first turn the claim's prose
//                      into a structured spec before the kernel can run;
//                      every entry here is true today, because every kernel
//                      in this codebase follows "model designs, device
//                      executes" — but the field exists so a future kernel
//                      that runs directly off structured input (no
//                      extraction step) is expressible without changing the
//                      shape.
//   buildPrompt(node)  returns the prompt asking a model to design a spec
//   normalize(raw)     validates/clamps the model's spec; throws on
//                       malformed input rather than guessing
//   run(spec)          executes the normalized spec; pure and synchronous
//                       for every kernel currently registered, with one
//                       documented exception: the mcmc kernel's run()
//                       returns a Promise when spec.mode === 'smc'
//                       (upgrade 2's parallel SMC sampler dispatches real
//                       worker_threads tasks and must await them). Every
//                       caller in this repo (orchestrator.js) awaits
//                       run()'s return value unconditionally, which is a
//                       no-op for every other, synchronous kernel — so
//                       this exception is transparent to callers that
//                       follow that convention.
//   summarize          optional: spec -> user-facing headline/detail

import { buildConsistencyPrompt, normalizeCommitments, findContradictions, summarizeConsistency } from './consistencyKernel.js';
import { buildMcmcPrompt, normalizeMcmcSpec, executeMcmcSearch } from './mcmcSearch.js';
import { buildNumericCheckPrompt, normalizeCheckSpec, executeCheck } from './numericCheck.js';
import { buildDynamicsPrompt, normalizeDynamicsSpec, executeDynamicsCheck } from './dynamicsCheck.js';
import { buildCombinatorialPrompt, normalizeCombinatorialSpec, executeCombinatorialSearch } from './combinatorialSearch.js';
import { buildDomainOfValidityPrompt, normalizeDomainOfValidity, summarizeDomainOfValidity } from './domainOfValidity.js';
import { normalizeRelations, findOrderCycles, summarizeOrder } from './orderConsistency.js';
import { normalizeBoundarySpec, checkBoundary } from './boundaryKernel.js';
import { normalizeEnergyClaimSpec, verifyEnergyClaim, buildEnergyClaimPrompt } from './neuromorphicPower.js';
import { normalizeEventCameraSpec, verifyEventLog, buildEventCameraPrompt } from './eventCameraPixel.js';
import { normalizeDecisionSpec, evaluateDecision, buildDecisionPrompt } from './decisionHelper.js';
import { normalizeChainInput, proveChainReachability } from './chainKernel.js';
import { buildVectorSpanPrompt, normalizeVectorSpanSpec, executeVectorSpan } from './vectorSpanKernel.js';

const REGISTRY = new Map();

function register(entry) {
  if (REGISTRY.has(entry.id)) throw new Error(`Duplicate kernel id "${entry.id}"`);
  REGISTRY.set(entry.id, Object.freeze(entry));
}

register({
  id: 'consistency',
  label: 'Consistency kernel',
  domain: ['logic', 'measurement', 'dimensional'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildConsistencyPrompt,
  normalize: (raw) => normalizeCommitments(raw),
  run: (commitments) => findContradictions(commitments),
  summarize: summarizeConsistency,
});

register({
  id: 'mcmc',
  label: 'MCMC counterexample search',
  domain: ['numeric', 'counterexample-search'],
  deterministic: false, // seeded-stochastic: reproducible, not deterministic
  needsModelExtraction: true,
  buildPrompt: buildMcmcPrompt,
  normalize: normalizeMcmcSpec,
  run: executeMcmcSearch,
});

register({
  id: 'numeric-check',
  label: 'Numeric check',
  domain: ['numeric', 'exact'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildNumericCheckPrompt,
  normalize: normalizeCheckSpec,
  run: executeCheck,
});

register({
  id: 'dynamics',
  label: 'Dynamics equivalence check',
  domain: ['dynamics', 'numeric'],
  deterministic: false, // seeded-stochastic, same caveat as mcmc
  needsModelExtraction: true,
  buildPrompt: buildDynamicsPrompt,
  normalize: normalizeDynamicsSpec,
  run: executeDynamicsCheck,
});

register({
  id: 'combinatorial',
  label: 'Combinatorial / SAT existence search',
  domain: ['combinatorial', 'existence-proof'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildCombinatorialPrompt,
  normalize: normalizeCombinatorialSpec,
  run: executeCombinatorialSearch,
});

register({
  id: 'domain-of-validity',
  label: 'Domain-of-validity narrowing',
  domain: ['scope', 'logic'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildDomainOfValidityPrompt,
  normalize: normalizeDomainOfValidity,
  run: (normalized) => normalized, // narrowing IS the normalized result; no separate execution step
  summarize: summarizeDomainOfValidity,
});

register({
  id: 'order-consistency',
  label: 'Order-consistency (ranking cycle) check',
  domain: ['logic', 'ordering'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: null, // consumed via consistencyKernel's 'relation' commitment form, not a standalone prompt today
  normalize: normalizeRelations,
  run: findOrderCycles,
  summarize: summarizeOrder,
});

register({
  id: 'boundary-check',
  label: 'Device-action boundary check',
  domain: ['device-action', 'scope'],
  deterministic: true,
  needsModelExtraction: false, // scope membership is fully determined by the action + grant; nothing to extract from prose
  buildPrompt: null,
  normalize: normalizeBoundarySpec,
  run: checkBoundary,
});

register({
  id: 'neuromorphic-power',
  label: 'Neuromorphic energy-claim verification',
  domain: ['hardware', 'numeric'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildEnergyClaimPrompt,
  normalize: normalizeEnergyClaimSpec,
  run: verifyEnergyClaim,
});

register({
  id: 'event-camera-pixel',
  label: 'Event-camera pixel mechanism verification',
  domain: ['hardware', 'vision', 'numeric'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildEventCameraPrompt,
  normalize: normalizeEventCameraSpec,
  run: verifyEventLog,
});

register({
  id: 'decision-helper',
  label: 'Probabilistic decision support (fault-point analysis)',
  domain: ['decision-support', 'probabilistic', 'numeric'],
  deterministic: false, // seeded-stochastic Monte Carlo, same caveat as mcmc/dynamics
  needsModelExtraction: true,
  buildPrompt: buildDecisionPrompt,
  normalize: normalizeDecisionSpec,
  run: evaluateDecision,
});

register({
  id: 'chain-reachability',
  label: 'Compositional exploit-chain reachability',
  domain: ['security', 'existence-proof'],
  deterministic: true,
  needsModelExtraction: false, // consumes findings other kernels already verified; nothing to extract from prose
  buildPrompt: null,
  normalize: normalizeChainInput,
  run: proveChainReachability,
});

register({
  id: 'vector-span',
  label: 'Vector span / matrix rank',
  domain: ['linear-algebra', 'existence-proof', 'engineering'],
  deterministic: true,
  needsModelExtraction: true,
  buildPrompt: buildVectorSpanPrompt,
  normalize: normalizeVectorSpanSpec,
  run: executeVectorSpan,
});

export function listKernels() {
  return [...REGISTRY.values()];
}

export function getKernel(id) {
  const entry = REGISTRY.get(id);
  if (!entry) throw new Error(`Unknown kernel id "${id}"`);
  return entry;
}

export function kernelsForDomain(tag) {
  return listKernels().filter((k) => k.domain.includes(tag));
}
