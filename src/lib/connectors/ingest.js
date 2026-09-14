// connectors/ingest.js; upgrade 12 — the step that actually connects a
// DataSource's raw records to this repo's existing ontology
// (src/ontology/objectTypes.js): validates a mapped record against its
// declared object type's `properties` schema and produces a real,
// checked instance, rather than letting arbitrary connector output
// flow into the verification substrate unchecked.
//
// This does NOT invent a new data model. `getObjectType(name)` is the
// same function `src/domains/*.js` already uses; an ingested instance
// is validated against the EXACT properties schema already declared
// there, so a connector cannot silently produce objects the rest of
// the substrate doesn't recognize.

import { getObjectType } from '../../ontology/objectTypes.js';

const MAX_RECORDS = 10000; // bounded, same discipline as every other input-accepting function in this repo

function typeMatches(value, typeSpec) {
  const optional = typeSpec.endsWith('?');
  const base = optional ? typeSpec.slice(0, -1) : typeSpec;
  if (value === undefined || value === null) return optional;

  if (base === 'string') return typeof value === 'string';
  if (base === 'number') return typeof value === 'number' && Number.isFinite(value);
  if (base === 'string[]') return Array.isArray(value) && value.every((v) => typeof v === 'string');
  if (base === 'object[]') return Array.isArray(value) && value.every((v) => v !== null && typeof v === 'object');
  if (base === 'object') return value !== null && typeof value === 'object';
  throw new Error(`ingest: unrecognized property type spec "${typeSpec}" in an object type definition`);
}

/**
 * Validates and maps `rawRecords` (whatever a DataSource's fetch()
 * returned) into instances of `objectTypeName`, via `mapper(raw) ->
 * {property: value, ...}` supplied by the caller (every connector's
 * raw shape is different — a CSV row's string keys, an HTTP API's JSON
 * shape — so there is no way to auto-map without one). Throws with a
 * specific per-record reason on the FIRST invalid record rather than
 * silently dropping or coercing it; ingestion into a verification
 * substrate failing loudly on bad data is the same principle as every
 * kernel's normalize() refusing malformed input rather than guessing.
 */
export function ingestToObjectType(rawRecords, objectTypeName, mapper) {
  if (!Array.isArray(rawRecords)) throw new Error('ingestToObjectType: rawRecords must be an array');
  if (rawRecords.length > MAX_RECORDS) throw new Error(`ingestToObjectType: ${rawRecords.length} records exceeds the ${MAX_RECORDS}-record cap per ingest call`);
  if (typeof mapper !== 'function') throw new Error('ingestToObjectType needs a mapper(raw) function');

  const objectType = getObjectType(objectTypeName); // throws on unknown type name, same as every other caller of this function

  const instances = [];
  for (let i = 0; i < rawRecords.length; i++) {
    let mapped;
    try {
      mapped = mapper(rawRecords[i]);
    } catch (e) {
      throw new Error(`ingestToObjectType: mapper threw on record ${i}: ${e.message}`);
    }
    if (!mapped || typeof mapped !== 'object') throw new Error(`ingestToObjectType: mapper for record ${i} did not return an object`);

    for (const [propName, typeSpec] of Object.entries(objectType.properties)) {
      if (!typeMatches(mapped[propName], typeSpec)) {
        throw new Error(`ingestToObjectType: record ${i} property "${propName}" does not match declared type "${typeSpec}" for object type "${objectTypeName}" (got ${JSON.stringify(mapped[propName])})`);
      }
    }
    instances.push({ objectType: objectTypeName, ...mapped });
  }
  return instances;
}
