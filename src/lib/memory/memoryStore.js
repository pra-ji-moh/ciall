// memoryStore.js; a persistent, long-term memory layer for
// ciall-substrate — GBrain-inspired, deliberately not the same.
//
// WHAT'S BORROWED FROM GBRAIN (github.com/garrytan/gbrain, the project
// referenced when this was requested): the "compiled truth above the
// line, append-only timeline below the line" page model — a page's
// compiledTruth is the CURRENT best understanding, rewritten whenever
// new evidence arrives; its timeline is a permanent, never-edited log
// of every event that shaped that understanding. And the "self-wiring
// knowledge graph, zero LLM calls" principle: relationships between
// pages are extracted deterministically from structure the caller
// already provides, never inferred by a model call.
//
// WHAT'S DELIBERATELY DIFFERENT, why this is "similar, not the same":
//  - GBrain runs on Postgres + pgvector + OpenAI embeddings for hybrid
//    semantic/keyword search, hosted (Supabase). This substrate has a
//    hard "zero npm dependencies" rule and no database — storage here
//    is one JSON file per page on local disk, and search/relatedness
//    is deterministic (entity-string matching, no vectors, no API
//    calls), consistent with every kernel in this repo already being
//    callable with zero AI/network involvement.
//  - GBrain's self-wiring extracts generic typed edges (attended,
//    works_at, founded, ...) for a personal-knowledge/CRM-shaped
//    domain. This memory layer is scoped to what ciall-substrate
//    actually is — a claim-verification engine — so its self-wiring is
//    claim-shaped: shared-entity edges (two pages talk about the same
//    thing) AND, uniquely to this domain, CONTRADICTION edges, found
//    by literally reusing consistencyKernel.js's own
//    findContradictions() across every page's recorded polarized
//    claims (see claimMemory.js). GBrain organizes text; this also
//    VERIFIES it, because verification is what this substrate already
//    does.
//
// Each page is one file: <dir>/pages/<id>.json, shape:
//   { id, createdAt, compiledTruth, entities: string[],
//     claims: [{atom, polarity}], timeline: [{at, kind, detail, verdict?}] }
// `claims` (optional) is the structured, consistencyKernel-shaped
// subset of what a page asserts — see claimMemory.js's
// findContradictingMemories() for why this is a separate, structured
// field rather than trying to parse polarity out of prose.

import fs from 'node:fs';
import path from 'node:path';

export function defaultMemoryRoot() {
  return path.join(process.cwd(), 'ciall-memory');
}

function pagesDir(root) {
  return path.join(root, 'pages');
}

function pagePath(root, id) {
  if (!/^[A-Za-z0-9_.-]+$/.test(id)) throw new Error(`memory: page id "${id}" contains characters outside [A-Za-z0-9_.-] — must be a safe filename component`);
  return path.join(pagesDir(root), `${id}.json`);
}

function ensureDir(root) {
  fs.mkdirSync(pagesDir(root), { recursive: true });
}

/** Reads one page, or null if it doesn't exist. */
export function getPage(root, id) {
  const p = pagePath(root, id);
  if (!fs.existsSync(p)) return null;
  return JSON.parse(fs.readFileSync(p, 'utf8'));
}

/** Lists every page id currently in the store, sorted. */
export function listPages(root) {
  const dir = pagesDir(root);
  if (!fs.existsSync(dir)) return [];
  return fs.readdirSync(dir)
    .filter((f) => f.endsWith('.json'))
    .map((f) => f.slice(0, -'.json'.length))
    .sort();
}

/**
 * Creates a page if it doesn't exist, or updates its compiledTruth/
 * entities/claims (the "above the line" fields) if it does — the
 * timeline is NEVER touched here; see appendTimeline(). `entities`
 * and `claims` default to [] (a page's own text can still self-wire
 * via extractEntities() in entityGraph.js even if the caller never
 * passes structured entities explicitly).
 */
export function putPage(root, id, { compiledTruth, entities = [], claims = [] }) {
  if (typeof compiledTruth !== 'string' || !compiledTruth) throw new Error('memory: compiledTruth must be a non-empty string');
  ensureDir(root);
  const existing = getPage(root, id);
  const page = {
    id,
    createdAt: existing ? existing.createdAt : nowIso(),
    compiledTruth,
    entities: [...entities],
    claims: claims.map((c) => ({ atom: String(c.atom), polarity: Boolean(c.polarity) })),
    timeline: existing ? existing.timeline : [],
  };
  fs.writeFileSync(pagePath(root, id), JSON.stringify(page, null, 2));
  return page;
}

/**
 * Appends one entry to a page's timeline — permanent, never edited or
 * removed by anything in this module. Creates the page (with an empty
 * compiledTruth placeholder) if it doesn't exist yet, so a caller can
 * start recording events before ever calling putPage() directly.
 */
export function appendTimeline(root, id, entry) {
  ensureDir(root);
  const existing = getPage(root, id);
  const page = existing || { id, createdAt: nowIso(), compiledTruth: '(no compiled truth recorded yet)', entities: [], claims: [], timeline: [] };
  page.timeline = [...page.timeline, { at: nowIso(), ...entry }];
  fs.writeFileSync(pagePath(root, id), JSON.stringify(page, null, 2));
  return page;
}

export function deletePage(root, id) {
  const p = pagePath(root, id);
  if (fs.existsSync(p)) fs.unlinkSync(p);
}

// Timestamps are stamped by the CALLER's clock, not Date.now() called
// deep inside this module unconditionally — every public function here
// accepts the natural system clock by default via this helper, but
// nothing about the module's own logic depends on wall-clock time being
// real (tests pass explicit timeline entries with their own `at`
// whenever ordering matters), keeping this store deterministic to test
// against.
function nowIso() {
  return new Date().toISOString();
}
