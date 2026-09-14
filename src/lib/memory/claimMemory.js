// claimMemory.js; the ciall-substrate-specific layer on top of
// memoryStore.js + entityGraph.js — where this memory system stops
// being "a GBrain-shaped note store" and starts being what makes it
// belong in THIS repo: recording verification history, and using this
// repo's OWN consistency kernel to check accumulated memory for
// contradictions, not just entityGraph.js's simpler direct pairwise
// check (which only catches two pages that assert opposite polarity
// about the literal SAME atom — a real contradiction can also be
// reachable through an `implies` chain neither page states directly).

import { putPage, appendTimeline, getPage, listPages } from './memoryStore.js';
import { findContradictions } from '../consistencyKernel.js';

/**
 * Records one verification event against a memory page: appends it to
 * the page's timeline (permanent) and rewrites compiledTruth to reflect
 * this as the CURRENT best understanding — exactly the "compiled truth
 * rewritten on new evidence, timeline never edited" split this whole
 * module is built around.
 *
 * `claimText` becomes the page's compiledTruth summary; `entities`
 * (optional) are explicit [[wikilink]]-independent entity refs for
 * entityGraph.js's shares-entity edges; `claims` (optional) are
 * consistencyKernel-shaped {atom, polarity} assertions this
 * verification makes, for findContradictingMemories() below.
 */
export function recordVerification(root, pageId, { claimText, entities = [], claims = [], kernelId, verdict, detail }) {
  if (!claimText) throw new Error('recordVerification needs claimText');
  if (!kernelId) throw new Error('recordVerification needs kernelId');
  const compiledTruth = `${claimText}\n\nAs of the most recent check, ${kernelId} reports: ${verdict}.${detail ? ` ${detail}` : ''}`;
  putPage(root, pageId, { compiledTruth, entities, claims });
  return appendTimeline(root, pageId, { kind: 'verification', kernelId, verdict, detail: detail || null });
}

/**
 * Collects every {atom, polarity} claim recorded across EVERY page in
 * the store into one consistencyKernel-shaped commitment array (each
 * claim becomes an `assert` commitment, id = "<pageId>#<index>", source
 * = the page id — so a reported contradiction's `sources` field tells
 * you exactly which two memory pages disagree), and runs the SAME
 * findContradictions() every 'consistency' kernel call already uses.
 * This is real reuse, not a re-implementation: an `implies` chain
 * planted via one page's `claims` can surface a contradiction between
 * two OTHER pages that never directly assert opposing polarities on
 * the same atom — exactly the kind of finding entityGraph.js's direct
 * pairwise check in buildGraph() cannot reach.
 */
export function findContradictingMemories(root) {
  const pages = listPages(root).map((id) => getPage(root, id)).filter(Boolean);
  const commitments = [];
  for (const page of pages) {
    (page.claims || []).forEach((claim, i) => {
      commitments.push({
        id: `${page.id}#${i}`,
        kind: 'assert',
        atom: claim.atom,
        polarity: claim.polarity,
        source: page.id,
      });
    });
  }
  if (commitments.length === 0) return [];
  return findContradictions(commitments);
}

export { putPage, appendTimeline, getPage, listPages } from './memoryStore.js';
export { buildGraph, edgesFor, extractEntities, allEntitiesFor } from './entityGraph.js';
