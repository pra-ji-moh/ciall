// entityGraph.js; deterministic, zero-LLM self-wiring for
// memoryStore.js's pages — see memoryStore.js's file header for how
// this relates to (and differs from) GBrain's own self-wiring graph.
//
// Two edge kinds, both computed by plain string/set comparison, no
// embeddings, no model call:
//   'shares-entity' — two pages both reference the same entity string
//     (either extracted from [[wikilink]] syntax in compiledTruth, or
//     passed explicitly via putPage's `entities` array).
//   'contradicts'   — two pages carry a `claims` entry for the SAME
//     atom with OPPOSITE polarity. This is the one that isn't just
//     "organize related text" — it's a real, if simple, verification
//     finding: two things you recorded as true can't both be true at
//     once. See claimMemory.js for the full version of this that
//     reuses consistencyKernel.js's actual forward-chaining logic
//     instead of this module's direct pairwise check (which only
//     catches a DIRECT clash on the same atom, not one reachable
//     through an implication chain).

const WIKILINK_PATTERN = /\[\[([^\]|]+)(?:\|[^\]]*)?\]\]/g;

/** Extracts [[Entity]] and [[Entity|display text]] references from a page's compiledTruth text. Returns a deduplicated, sorted array. */
export function extractEntities(text) {
  const found = new Set();
  for (const match of String(text || '').matchAll(WIKILINK_PATTERN)) {
    const entity = match[1].trim();
    if (entity) found.add(entity);
  }
  return [...found].sort();
}

/** The full entity set for a page: whatever was passed explicitly, plus whatever [[wikilinks]] its own compiledTruth text contains. */
export function allEntitiesFor(page) {
  return [...new Set([...(page.entities || []), ...extractEntities(page.compiledTruth)])].sort();
}

/**
 * Builds the self-wiring graph over `pages` (an array of page objects,
 * as returned by memoryStore.getPage/listPages+getPage). Returns
 * { edges: [{type, a, b, via}] } — `a`/`b` are page ids (a < b
 * lexicographically, so each unordered pair appears once), `via` is
 * the entity or atom the edge is about.
 */
export function buildGraph(pages) {
  const edges = [];

  // shares-entity: group pages by entity, connect every pair within a group.
  const byEntity = new Map();
  for (const page of pages) {
    for (const entity of allEntitiesFor(page)) {
      if (!byEntity.has(entity)) byEntity.set(entity, []);
      byEntity.get(entity).push(page.id);
    }
  }
  for (const [entity, ids] of byEntity) {
    const sorted = [...new Set(ids)].sort();
    for (let i = 0; i < sorted.length; i++) {
      for (let j = i + 1; j < sorted.length; j++) {
        edges.push({ type: 'shares-entity', a: sorted[i], b: sorted[j], via: entity });
      }
    }
  }

  // contradicts: group pages by (atom, polarity), connect a page
  // asserting polarity=true to every page asserting polarity=false for
  // the SAME atom.
  const trueByAtom = new Map();
  const falseByAtom = new Map();
  for (const page of pages) {
    for (const claim of page.claims || []) {
      const bucket = claim.polarity ? trueByAtom : falseByAtom;
      if (!bucket.has(claim.atom)) bucket.set(claim.atom, new Set());
      bucket.get(claim.atom).add(page.id);
    }
  }
  for (const [atom, trueIds] of trueByAtom) {
    const falseIds = falseByAtom.get(atom);
    if (!falseIds) continue;
    for (const t of trueIds) {
      for (const f of falseIds) {
        if (t === f) continue; // a single page asserting both polarities about its own atom is its own (real) problem, but not a cross-page edge
        const [a, b] = [t, f].sort();
        edges.push({ type: 'contradicts', a, b, via: atom });
      }
    }
  }

  // Dedup (a page pair can legitimately share more than one entity, but
  // the exact same {type,a,b,via} tuple should only ever appear once —
  // e.g. iterating trueByAtom/falseByAtom independently could double-add
  // if both true and false sets happened to contain the same id pair via
  // a different entry ordering).
  const seen = new Set();
  const deduped = [];
  for (const e of edges) {
    const key = `${e.type}|${e.a}|${e.b}|${e.via}`;
    if (seen.has(key)) continue;
    seen.add(key);
    deduped.push(e);
  }
  return { edges: deduped };
}

/** Every edge touching `id`, from a graph built by buildGraph(). */
export function edgesFor(graph, id) {
  return graph.edges.filter((e) => e.a === id || e.b === id).map((e) => ({
    type: e.type,
    other: e.a === id ? e.b : e.a,
    via: e.via,
  }));
}
