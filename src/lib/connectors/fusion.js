// connectors/fusion.js; upgrade 15 — real multi-source data fusion
// with access-controlled filtering: the honest, generic engineering
// content behind "combine different databases and filter information
// for suitable output," built as a civilian data-integration
// capability, not branded or scoped for military coalition
// intelligence sharing (see this project's memory notes on why that
// framing was declined).
//
// Two real, separate steps, on purpose:
//   1. fuseRecords — combines records from multiple named sources that
//      refer to the SAME real-world entity (matched by a caller-
//      supplied key function), and — the part that actually matters
//      for trustworthiness — NEVER silently picks a winner when two
//      sources disagree on a field. A disagreement is recorded as a
//      real, visible conflict, the same "surface it, don't resolve it
//      for the human" principle every kernel in this repo already
//      follows for a claim's own logical/numeric conflicts.
//   2. filterForConsumer — gates the FUSED result through
//      accessControl.js's checkVisibility layer (upgrade 13), so a
//      consumer only ever sees fused records for an object type they
//      are actually permitted to see, tenant-isolated exactly the way
//      every other accessControl.js decision already is. Fusion never
//      bypasses that gate — it runs first, filtering runs after,
//      always, no path that returns fused data before the visibility
//      check.

import { checkVisibility } from '../accessControl.js';

const MAX_SOURCES = 20;
const MAX_RECORDS_PER_SOURCE = 5000;

/**
 * `sourceRecordSets`: Array<{sourceId: string, records: object[]}>.
 * `keyFn(record) -> string` identifies which records across different
 * sources refer to the same real-world entity (e.g. a stable business
 * key, NOT array position — position means nothing across independent
 * sources).
 *
 * Returns fused records: `{ key, fields: {...merged...}, sources:
 * string[], conflicts: [{field, valuesBySource: {sourceId: value}}] }`.
 * `fields` takes the FIRST value seen per field (source order as
 * given) for fields every source agrees on; any field where sources
 * DISAGREE is EXCLUDED from `fields` and reported in `conflicts`
 * instead — a caller must resolve a real conflict deliberately, never
 * by accident of iteration order.
 */
export function fuseRecords(sourceRecordSets, keyFn) {
  if (!Array.isArray(sourceRecordSets) || sourceRecordSets.length === 0) throw new Error('fuseRecords needs a non-empty array of {sourceId, records}');
  if (sourceRecordSets.length > MAX_SOURCES) throw new Error(`fuseRecords: ${sourceRecordSets.length} sources exceeds the ${MAX_SOURCES}-source cap`);
  if (typeof keyFn !== 'function') throw new Error('fuseRecords needs a keyFn(record) -> string');

  const byKey = new Map(); // key -> { sources: Map<sourceId, record> }

  for (const { sourceId, records } of sourceRecordSets) {
    if (typeof sourceId !== 'string' || !sourceId) throw new Error('every source needs a non-empty string sourceId');
    if (!Array.isArray(records)) throw new Error(`source "${sourceId}": records must be an array`);
    if (records.length > MAX_RECORDS_PER_SOURCE) throw new Error(`source "${sourceId}": ${records.length} records exceeds the ${MAX_RECORDS_PER_SOURCE}-record cap`);

    for (const record of records) {
      const key = String(keyFn(record));
      if (!byKey.has(key)) byKey.set(key, new Map());
      byKey.get(key).set(sourceId, record);
    }
  }

  const fused = [];
  for (const [key, sourcesForKey] of byKey.entries()) {
    const sourceIds = [...sourcesForKey.keys()];
    const allFieldNames = new Set();
    for (const record of sourcesForKey.values()) {
      for (const f of Object.keys(record)) allFieldNames.add(f);
    }

    const fields = {};
    const conflicts = [];
    for (const field of allFieldNames) {
      const valuesBySource = {};
      for (const [sourceId, record] of sourcesForKey.entries()) {
        if (field in record) valuesBySource[sourceId] = record[field];
      }
      const distinctValues = [...new Set(Object.values(valuesBySource).map((v) => JSON.stringify(v)))];
      if (distinctValues.length <= 1) {
        fields[field] = Object.values(valuesBySource)[0];
      } else {
        conflicts.push({ field, valuesBySource });
      }
    }

    fused.push({ key, fields, sources: sourceIds, conflicts });
  }

  return fused;
}

/**
 * Gates fused records through the SAME tenant-isolated visibility
 * layer every other accessControl.js decision uses. Never skipped,
 * never partially applied — a denied consumer gets an empty array and
 * the real reason, never a partial or best-effort result.
 */
export function filterForConsumer(fusedRecords, objectTypeName, { userId, resourceTenantId }) {
  const decision = checkVisibility({ userId, objectTypeName, resourceTenantId });
  if (!decision.allowed) {
    return { allowed: false, reason: decision.reason, records: [] };
  }
  return { allowed: true, reason: decision.reason, records: fusedRecords };
}
