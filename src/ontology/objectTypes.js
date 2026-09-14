// ontology/objectTypes.js; the actual missing piece of Palantir's
// technical core — not the company (data, deployment, customers, which
// no code produces), but the ARCHITECTURE: a formal, typed layer of
// object definitions that domain functions and kernels operate on,
// instead of every domain function taking whatever ad hoc shape its
// author happened to write.
//
// This formalizes what already exists; it does not invent new
// verification logic. Every object type here maps directly onto a
// commitment/spec shape a kernel in src/lib/ already accepts. The
// `verifiedBy` field names the real kernel id from kernelRegistry.js —
// checked by a test, not just documented — so this file cannot silently
// drift out of sync with what the substrate can actually verify.

const OBJECT_TYPES = new Map();

function defineObjectType(def) {
  if (OBJECT_TYPES.has(def.name)) throw new Error(`Duplicate object type "${def.name}"`);
  if (!def.verifiedBy) throw new Error(`Object type "${def.name}" must name the kernel that verifies it`);
  OBJECT_TYPES.set(def.name, Object.freeze(def));
}

defineObjectType({
  name: 'RiskMark',
  domain: 'finance',
  description: 'A single quoted value of a risk quantity (e.g. VaR), with uncertainty and source.',
  properties: {
    quantity: 'string', value: 'number', uncertainty: 'number', source: 'string', assumes: 'string[]?',
  },
  relationships: ['TensionWith'],
  verifiedBy: 'consistency',
});

defineObjectType({
  name: 'RiskMarginClaim',
  domain: 'finance',
  description: 'A stated numeric margin claim over a risk quantity (e.g. drawdown, VaR limit), parameterized for search.',
  properties: { params: 'object[]', objective: 'string' },
  relationships: [],
  verifiedBy: 'mcmc',
});

defineObjectType({
  name: 'DesignMargin',
  domain: 'engineering',
  description: 'A stated engineering safety-margin claim over load/tolerance parameters.',
  properties: { params: 'object[]', objective: 'string' },
  relationships: [],
  verifiedBy: 'mcmc',
});

defineObjectType({
  name: 'EngineeringEquation',
  domain: 'engineering',
  description: 'A stated formula whose dimensional balance can be checked.',
  properties: { lhs: 'string', rhs: 'string', assignments: 'object' },
  relationships: [],
  verifiedBy: 'consistency', // dimensional checking is exposed through consistencyKernel's 'equation' commitment form
});

defineObjectType({
  name: 'Requirement',
  domain: 'requirements',
  description: 'A single program requirement, expressed as a logical commitment (assert/implies/universal/instance/property).',
  properties: { kind: 'string', source: 'string' },
  relationships: ['Contradicts'],
  verifiedBy: 'consistency',
});

defineObjectType({
  name: 'SupplierResilienceClaim',
  domain: 'supply-chain',
  description: 'A stated resilience claim over supplier-concentration parameters.',
  properties: { params: 'object[]', objective: 'string' },
  relationships: [],
  verifiedBy: 'mcmc',
});

defineObjectType({
  name: 'SupplierRanking',
  domain: 'supply-chain',
  description: 'A comparative ranking of suppliers on a stated metric.',
  properties: { subject: 'string', object: 'string', comparator: 'string', metric: 'string', source: 'string' },
  relationships: ['ImpossibleCycle'],
  verifiedBy: 'order-consistency',
});

defineObjectType({
  name: 'OperationalEnvelope',
  domain: 'mission-planning',
  description: 'A stated operational-envelope claim (plan succeeds within a parameterized threat/resource space).',
  properties: { params: 'object[]', objective: 'string' },
  relationships: [],
  verifiedBy: 'mcmc',
});

defineObjectType({
  name: 'ContractClause',
  domain: 'legal',
  description: 'A single contract clause, expressed as a logical commitment.',
  properties: { kind: 'string', source: 'string' },
  relationships: ['Contradicts'],
  verifiedBy: 'consistency',
});

export function listObjectTypes() {
  return [...OBJECT_TYPES.values()];
}

export function getObjectType(name) {
  const entry = OBJECT_TYPES.get(name);
  if (!entry) throw new Error(`Unknown object type "${name}"`);
  return entry;
}

export function objectTypesForDomain(domain) {
  return listObjectTypes().filter((t) => t.domain === domain);
}
