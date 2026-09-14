// orderConsistency.js; proving that a set of comparative claims cannot
// all hold, by finding a cycle in a strict order.
//
// Comparative claims are everywhere and are almost never checked as a SET.
// "Method A beats B on this benchmark" appears in one paper, "B beats C"
// in another, "C beats A" in a third, and every individual statement
// survives peer review because no single reviewer is holding all three
// papers in their head at once. The set is nonetheless impossible: a
// strict order cannot contain a cycle. That is a proof, it needs no
// oracle, and nobody checks it.
//
// The same failure appears in ordinary reasoning constantly. "We are
// cheaper than X", "X undercuts Y", "Y is cheaper than us" is the identical
// structure wearing different clothes.
//
// THE RULES, and they are the whole module:
//   A strict order is irreflexive and transitive. So if a > b and b >= c
//   and c >= a, then a > a, which is false. Any cycle containing AT LEAST
//   ONE strict edge is therefore a proven contradiction.
//   A cycle of only non-strict edges (a >= b >= c >= a) is NOT a
//   contradiction. It proves the three are equal, which is a real and
//   sometimes interesting conclusion, but it is not an inconsistency and
//   must never be reported as one.
//
// THE CONSTRAINT THAT MATTERS MOST: comparisons are only comparable
// WITHIN THE SAME METRIC. "A is faster than B" and "B is cheaper than A"
// are not in tension; they are two different orders. Metrics are matched
// by exact normalized string, for the same reason atoms are elsewhere in
// this codebase. A fuzzy match that silently unified "accuracy on
// ImageNet" with "accuracy on CIFAR" would generate confident nonsense,
// which is the one failure mode this codebase cannot afford.

const norm = (s) => String(s || '').trim().toLowerCase().replace(/\s+/g, ' ').replace(/[.;,]+$/, '');

// Comparators are normalized into edges meaning "at least as large as",
// each carrying whether it is strict. Reducing five surface forms to one
// internal relation keeps the cycle logic small enough to be obviously
// correct, which matters more here than expressive power.
export const COMPARATORS = new Set(['greater', 'less', 'equal', 'atleast', 'atmost']);

export function normalizeRelations(raw) {
  const list = Array.isArray(raw?.relations) ? raw.relations.slice(0, 60) : [];
  const out = [];
  for (let i = 0; i < list.length; i++) {
    const r = list[i] || {};
    const subject = norm(r.subject), object = norm(r.object), metric = norm(r.metric);
    if (!subject || !object || !metric) continue;
    if (subject === object) continue; // "A beats A" is malformed input, not a finding
    if (!COMPARATORS.has(r.comparator)) continue;
    out.push({
      id: `r${i + 1}`, subject, object, metric,
      comparator: r.comparator,
      source: String(r.source || '').slice(0, 300),
    });
  }
  return out;
}

// Each relation becomes one or two directed edges u -> v meaning
// "u is at least as large as v on this metric".
function edgesFor(rel) {
  switch (rel.comparator) {
    case 'greater': return [{ from: rel.subject, to: rel.object, strict: true, rel }];
    case 'less': return [{ from: rel.object, to: rel.subject, strict: true, rel }];
    case 'atleast': return [{ from: rel.subject, to: rel.object, strict: false, rel }];
    case 'atmost': return [{ from: rel.object, to: rel.subject, strict: false, rel }];
    case 'equal': return [
      { from: rel.subject, to: rel.object, strict: false, rel },
      { from: rel.object, to: rel.subject, strict: false, rel },
    ];
    default: return [];
  }
}

// Breadth-first path search, returning the edges traversed so the finding
// can show the actual chain rather than merely asserting one exists. A
// contradiction the reader cannot follow is not much use.
function findPath(edges, start, goal) {
  if (start === goal) return [];
  const adjacency = new Map();
  for (const e of edges) {
    if (!adjacency.has(e.from)) adjacency.set(e.from, []);
    adjacency.get(e.from).push(e);
  }
  const queue = [[start, []]];
  const seen = new Set([start]);
  while (queue.length) {
    const [node, path] = queue.shift();
    for (const e of adjacency.get(node) || []) {
      if (e.to === goal) return [...path, e];
      if (seen.has(e.to)) continue;
      seen.add(e.to);
      queue.push([e.to, [...path, e]]);
    }
  }
  return null;
}

// The instrument. For every STRICT edge u -> v, ask whether v can reach u
// again. If it can, the order contains a cycle through a strict edge, and
// the set is impossible. Only strict edges are used as the seed, which is
// exactly what keeps a harmless all-equal cycle from being reported as a
// contradiction.
export function findOrderCycles(relations) {
  const byMetric = new Map();
  for (const rel of relations) {
    if (!byMetric.has(rel.metric)) byMetric.set(rel.metric, []);
    byMetric.get(rel.metric).push(rel);
  }

  const findings = [];
  const seenSignatures = new Set();

  for (const [metric, rels] of byMetric) {
    const edges = rels.flatMap(edgesFor);
    for (const edge of edges) {
      if (!edge.strict) continue;
      const back = findPath(edges, edge.to, edge.from);
      if (!back) continue;

      const chain = [edge, ...back];
      const involved = [...new Set(chain.map((e) => e.rel.id))].sort();
      // One cycle can be discovered from each of its strict edges; report
      // it once. Inflating the count would cost exactly the trust this
      // module exists to earn.
      const signature = `${metric}|${involved.join(',')}`;
      if (seenSignatures.has(signature)) continue;
      seenSignatures.add(signature);

      findings.push({
        metric,
        involved,
        cycle: chain.map((e) => ({
          from: e.from, to: e.to, strict: e.strict, source: e.rel.source, comparator: e.rel.comparator,
        })),
        entities: [...new Set(chain.flatMap((e) => [e.from, e.to]))],
        sources: [...new Set(chain.map((e) => e.rel.source).filter(Boolean))],
        // The readable form: "a > b >= c >= a".
        readable: chain.map((e, i) => (i === 0 ? `${e.from} ${e.strict ? '>' : '>='} ${e.to}` : `${e.strict ? '>' : '>='} ${e.to}`)).join(' '),
      });
    }
  }

  // Shortest cycles first: a three-term loop is far easier to act on than
  // an eight-term one, and is usually the one actually worth fixing.
  return findings.sort((a, b) => a.cycle.length - b.cycle.length);
}

export function summarizeOrder(findings, relationCount) {
  const list = findings || [];
  if (relationCount === 0) return { verdict: 'nothing-extracted', headline: '', detail: '' };
  if (list.length === 0) {
    return {
      verdict: 'no-cycle-found',
      headline: 'No impossible ranking was found among these comparisons.',
      // Same asymmetry as everywhere: a failed search is not a proof.
      detail: `${relationCount} comparative claims were checked against each other within each metric. This does not establish that the rankings are correct; it only means no cycle was found, which is a much weaker result.`,
    };
  }
  const shortest = list[0];
  return {
    verdict: 'impossible-ranking',
    headline: list.length === 1
      ? 'These comparisons describe a ranking that cannot exist.'
      : `${list.length} of these comparison sets describe rankings that cannot exist.`,
    detail: `On "${shortest.metric}" the claims form a loop: ${shortest.readable}. A strict ordering cannot contain a cycle, so at least one of these comparisons is false. Which one is not something this can determine, and if any comparison was read as being about a different metric than intended, the loop is not real.`,
  };
}
