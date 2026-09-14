// connectors/connector.js; upgrade 12 — the missing data-integration
// layer named against Palantir Foundry: Foundry's core asset is
// ingesting an organization's real, live systems into a unified object
// model via ~100 built-in connectors. This is the honest, buildable
// slice of that: a real `DataSource` contract plus concrete
// implementations (fileConnector.js, httpConnector.js), and a real
// mapper (ingest.js) turning whatever a connector returns into
// validated instances of this repo's existing ontology object types
// (src/ontology/objectTypes.js) — not a new, parallel data model.
//
// WHAT THIS IS NOT. This is not 100 connectors, not a live production
// data estate, not years of enterprise integration work. It is a real,
// working, tested INTERFACE plus two genuinely useful concrete
// connectors (local file, HTTP poll) that any future connector plugs
// into the same way a new kernel plugs into kernelRegistry.js — a
// registry entry, not a bespoke pipeline. Scaling the CONNECTOR COUNT
// from here is an engineering-hours problem, not an architecture
// problem; this file is what makes that true.
//
// Every DataSource implements exactly one method:
//   async fetch() -> Array<Record<string, any>>
// returning whatever raw records it found, completely undefined in
// shape until a caller runs them through ingest.js's mapper against a
// specific object type. No DataSource here touches the ontology,
// kernels, or verification substrate directly — same separation of
// concerns as buildPrompt/normalize/run in every kernel.

const registry = new Map();

/**
 * Registers a DataSource factory under a stable string id, the same
 * pattern kernelRegistry.js uses for kernels. `factory(config)` must
 * return an object with an async `fetch()` method.
 */
export function registerConnectorType(id, factory) {
  if (typeof id !== 'string' || !id) throw new Error('registerConnectorType needs a non-empty string id');
  if (typeof factory !== 'function') throw new Error('registerConnectorType needs a factory function');
  if (registry.has(id)) throw new Error(`Duplicate connector type "${id}"`);
  registry.set(id, factory);
}

export function createConnector(id, config) {
  const factory = registry.get(id);
  if (!factory) throw new Error(`Unknown connector type "${id}". Known: ${[...registry.keys()].join(', ')}`);
  const connector = factory(config);
  if (typeof connector.fetch !== 'function') throw new Error(`connector type "${id}" did not produce an object with a fetch() method`);
  return connector;
}

export function listConnectorTypes() {
  return [...registry.keys()];
}
