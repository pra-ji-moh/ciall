// ontology/instanceStore.js; upgrade 13 — the missing piece between
// "connectors ingest raw records" (src/lib/connectors/) and "the
// ontology is a semantic layer over an organization's real data"
// (Palantir Foundry's framing): a real, persistent store of validated
// object-type instances, with SELF-WIRING relationship computation —
// every time new instances of a type this store knows how to relate
// land, it recomputes real relationships over the FULL accumulated set
// for that type, using the exact same kernels/relationship functions
// (`src/ontology/relationships.js`, `measurementTension.js`)
// everywhere else in this repo already uses, unmodified.
//
// Same storage discipline as `src/lib/memory/memoryStore.js`: one JSON
// file per object type on local disk, zero database, zero npm
// dependency. This is deliberately NOT Foundry's ontology at scale —
// no branching, no versioned schemas, no distributed storage, no
// access-controlled row-level views (see accessControl.js for the
// separate, additive governance layer that sits IN FRONT of reads from
// this store). It is a real, working, bounded slice of the same
// architectural idea: ingested data becomes typed, related, queryable
// objects, not an inert pile of records.
//
// SELF-WIRING, precisely: only object types this file has an explicit,
// reviewed relationship function for get automatic relationship
// computation (RiskMark -> TensionWith via the existing
// `compareMeasurements`, all-pairs/same-quantity-only, already bounded
// and already used elsewhere in this repo; Requirement/ContractClause
// -> Contradicts via the existing `contradicts()`; SupplierRanking ->
// ImpossibleCycle via the existing `impossibleCycle()`). Any other
// registered object type is stored and queryable, just without
// automatic relationship computation — silence, not a guess.
//
// BOUNDED, deliberately: MAX_INSTANCES_PER_TYPE caps the accumulated
// store per type. `compareMeasurements`'s all-pairs comparison is
// O(n^2); an unbounded store would make every future save progressively
// slower and eventually unbounded, exactly the class of gap the
// upgrade-11 bounded-execution audit exists to catch before it ships,
// not after.

import fs from 'node:fs';
import path from 'node:path';
import { getObjectType } from './objectTypes.js';
import { contradicts, impossibleCycle } from './relationships.js';
import { compareMeasurements } from '../lib/measurementTension.js';

const MAX_INSTANCES_PER_TYPE = 300;

const SELF_WIRING = {
  RiskMark: (all) => compareMeasurements(all).map((f) => ({ relationship: 'TensionWith', ...f })),
  Requirement: (all) => contradicts(all),
  ContractClause: (all) => contradicts(all),
  SupplierRanking: (all) => impossibleCycle(all),
};

export function defaultInstanceStoreRoot() {
  return path.join(process.cwd(), 'ciall-ontology-store');
}

function safeTypeName(objectTypeName) {
  if (!/^[A-Za-z0-9_-]+$/.test(objectTypeName)) throw new Error(`instanceStore: object type name "${objectTypeName}" contains characters outside [A-Za-z0-9_-] — must be a safe filename component`);
  return objectTypeName;
}

function instancesPath(root, objectTypeName) {
  return path.join(root, 'instances', `${safeTypeName(objectTypeName)}.json`);
}

function relationshipsPath(root, objectTypeName) {
  return path.join(root, 'relationships', `${safeTypeName(objectTypeName)}.json`);
}

function ensureDirs(root) {
  fs.mkdirSync(path.join(root, 'instances'), { recursive: true });
  fs.mkdirSync(path.join(root, 'relationships'), { recursive: true });
}

/** Reads every persisted instance of `objectTypeName`, or []. Throws on an unknown object type, same as every other caller of getObjectType. */
export function loadInstances(root, objectTypeName) {
  getObjectType(objectTypeName);
  const p = instancesPath(root, objectTypeName);
  if (!fs.existsSync(p)) return [];
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

/** Reads the last computed relationship set for `objectTypeName`, or []. Never throws on a type with no self-wiring function -- that's a real, valid state (stored, just not auto-related). */
export function loadRelationships(root, objectTypeName) {
  const p = relationshipsPath(root, objectTypeName);
  if (!fs.existsSync(p)) return [];
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

/**
 * Persists `newInstances` (already-validated instances, e.g. from
 * `connectors/ingest.js`'s `ingestToObjectType`) as new records of
 * `objectTypeName`, appended to whatever's already stored. If this
 * object type has a registered self-wiring function, relationships are
 * RECOMPUTED FROM SCRATCH over the full accumulated set (never
 * incrementally patched — recomputation from the same deterministic
 * function is simpler to reason about and to keep correct than trying
 * to incrementally update relationship state, and the underlying
 * kernels are already fast enough at this store's bounded scale).
 */
export function saveInstances(root, objectTypeName, newInstances) {
  getObjectType(objectTypeName);
  if (!Array.isArray(newInstances) || newInstances.length === 0) throw new Error('saveInstances needs a non-empty array of instances');
  ensureDirs(root);

  const existing = loadInstances(root, objectTypeName);
  const stamped = newInstances.map((inst, i) => ({ ...inst, _id: existing.length + i + 1, _storedAt: Date.now() }));
  const combined = existing.concat(stamped);
  if (combined.length > MAX_INSTANCES_PER_TYPE) {
    throw new Error(`instanceStore: storing ${combined.length} instances of "${objectTypeName}" would exceed the ${MAX_INSTANCES_PER_TYPE}-instance cap for this store`);
  }
  fs.writeFileSync(instancesPath(root, objectTypeName), JSON.stringify(combined, null, 2));

  let relationships = [];
  const wiringFn = SELF_WIRING[objectTypeName];
  if (wiringFn) {
    relationships = wiringFn(combined);
    fs.writeFileSync(relationshipsPath(root, objectTypeName), JSON.stringify(relationships, null, 2));
  }

  return { totalStored: combined.length, added: stamped.length, relationships, selfWired: Boolean(wiringFn) };
}

/** Lists object type names that currently have at least one stored instance. */
export function listStoredObjectTypes(root) {
  const dir = path.join(root, 'instances');
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir).filter((f) => f.endsWith('.json')).map((f) => f.slice(0, -'.json'.length)).sort();
}
