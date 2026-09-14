// vectorSpanKernel.js; the rank/span kernel this repo did not have.
//
// WHY THIS DIDN'T EXIST UNTIL NOW. Every other kernel here (consistency,
// mcmc, numeric-check, dynamics, combinatorial, domain-of-validity,
// order-consistency, boundary-check, chain-reachability, ...) checks a
// logical, numeric-sampled, or discrete/SAT claim. None of them touch
// exact LINEAR ALGEBRA. "Do these N vectors span R^k" / "is this
// matrix's rank at least K" are common, decisive, exactly-decidable
// engineering claims (steering-vocabulary span, actuation-matrix
// controllability, sensor-observability rank conditions) that had no
// home in this substrate — this file is that home, built the same way
// as everything else here: zero npm dependencies, deterministic, and
// every verdict cross-checked by a SECOND, independently-implemented
// algorithm before being trusted.
//
// TWO ALGORITHMS, SHARING NO CODE — the same "structurally separate
// verifier" discipline dratProof.js applies to satKernel.js, and
// chainKernel.js applies to its own SAT witness (an independent, non-SAT
// fixpoint replay):
//   1. Gaussian elimination with partial pivoting (computeRankByElimination)
//      — the standard numerical method, O(n*k^2).
//   2. Exhaustive k-by-k minor determinant checking via cofactor
//      expansion (computeRankByMinors) — a genuinely different
//      algorithmic approach (Laplace expansion over combinations of
//      rows, not row-reduction): rank >= k iff SOME k x k submatrix has
//      a nonzero determinant. Bounded (MAX_MINOR_CHECKS) and disclosed:
//      combinatorial in the number of vectors choose k, which is cheap
//      for realistic inputs (tens of vectors, dimension <= 6) and
//      explicitly refuses to silently sample/truncate for pathological
//      ones — see executeVectorSpan's 'inconclusive' path.
// If the two algorithms disagree, the verdict is WITHHELD
// (inconclusive), never silently trusting one over the other — same
// discipline combinatorialSearch.js applies when its own emitted proof
// fails independent re-verification.
//
// FLOATING POINT, DISCLOSED. Real-world vector components (e.g. from a
// physical steering-vocabulary table) are floats, not exact rationals.
// Both algorithms use a fixed epsilon (ZERO_TOLERANCE) to decide "is
// this effectively zero" — a genuine, disclosed numerical-analysis
// choice, not silently swept under a false claim of exactness. Same
// "float mode is not exact mode, and says so" discipline as
// numericCheck.js's own mode:'float'.

export const MAX_DIMENSION = 6; // matches real 6-DOF wrench/controllability claims; cofactor expansion is O(k!) per minor
export const MAX_VECTORS = 200;
export const MAX_MINOR_CHECKS = 200000; // bounded, same discipline as every other loop in this repo
const ZERO_TOLERANCE = 1e-9;

export function buildVectorSpanPrompt(node) {
  return `Design a VECTOR SPAN / RANK check. This instrument is exact linear algebra: given a set of labeled vectors in R^k, it decides whether they span R^k (equivalently, whether the matrix they form has rank >= k), or more generally whether a stated set of vectors has rank at least/exactly/at most some target -- via two independently-implemented algorithms that must agree.

CLAIM: ${node.text}
${node.reasoning ? `REASONING: ${node.reasoning}` : ''}

Extract every vector named in the claim with its real numeric components, the ambient dimension, and what is being claimed (spans the whole space, or a specific rank bound).

Respond ONLY with JSON:
{"kind":"vector_span","note":"what this tests","dimension":3,"vectors":[{"label":"A2","components":[0,0,2.810]},{"label":"A5","components":[0.060,0.375,2.930]}],"claim":"spans"}
or
{"kind":"vector_span","note":"...","dimension":6,"vectors":[...],"claim":"rank-at-least","targetRank":6}
{"kind":"none","reason":"this claim is not a vector-span/rank question, or the vectors are not fully specified with real numbers"}`;
}

export function normalizeVectorSpanSpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Vector-span spec is not an object');
  if (raw.kind === 'none') return { kind: 'none', reason: String(raw.reason || '').slice(0, 300) };
  if (raw.kind !== 'vector_span') throw new Error(`Unknown vector-span spec kind "${raw.kind}"`);

  const dimension = Number(raw.dimension);
  if (!Number.isInteger(dimension) || dimension < 1 || dimension > MAX_DIMENSION) {
    throw new Error(`dimension must be an integer in 1..${MAX_DIMENSION}`);
  }

  const rawVectors = Array.isArray(raw.vectors) ? raw.vectors : [];
  if (rawVectors.length === 0) throw new Error('at least one vector is required');
  if (rawVectors.length > MAX_VECTORS) throw new Error(`too many vectors (max ${MAX_VECTORS})`);

  const vectors = rawVectors.map((v, i) => {
    const where = `vectors[${i}]`;
    const label = String(v?.label || `v${i + 1}`).slice(0, 80);
    const components = Array.isArray(v?.components) ? v.components.map(Number) : null;
    if (!components || components.length !== dimension) throw new Error(`${where} ("${label}"): needs exactly ${dimension} numeric components`);
    if (!components.every(Number.isFinite)) throw new Error(`${where} ("${label}"): all components must be finite numbers`);
    return { label, components };
  });

  let claim = raw.claim;
  let targetRank;
  if (claim === 'spans') {
    targetRank = dimension;
  } else if (claim === 'rank-at-least' || claim === 'rank-at-most' || claim === 'rank-exactly') {
    targetRank = Number(raw.targetRank);
    if (!Number.isInteger(targetRank) || targetRank < 0 || targetRank > dimension) {
      throw new Error(`targetRank must be an integer in 0..${dimension}`);
    }
  } else {
    throw new Error(`Unknown claim "${claim}"; expected spans|rank-at-least|rank-at-most|rank-exactly`);
  }
  if (claim === 'spans') claim = 'rank-at-least'; // "spans R^k" IS "rank >= k" -- rank cannot exceed the ambient dimension

  return {
    kind: 'vector_span',
    note: String(raw.note || '').slice(0, 200),
    dimension, vectors, claim, targetRank,
  };
}

// ── Algorithm 1: Gaussian elimination with partial pivoting ──────────

export function computeRankByElimination(vectors, dimension) {
  const m = vectors.map((v) => v.components.slice());
  const rows = m.length;
  let rank = 0;
  for (let col = 0; col < dimension && rank < rows; col++) {
    let pivotRow = -1;
    let best = ZERO_TOLERANCE;
    for (let r = rank; r < rows; r++) {
      if (Math.abs(m[r][col]) > best) { best = Math.abs(m[r][col]); pivotRow = r; }
    }
    if (pivotRow === -1) continue; // this column contributes nothing new; move on
    [m[rank], m[pivotRow]] = [m[pivotRow], m[rank]];
    const pivot = m[rank][col];
    for (let r = 0; r < rows; r++) {
      if (r === rank) continue;
      const factor = m[r][col] / pivot;
      if (factor === 0) continue;
      for (let c = col; c < dimension; c++) m[r][c] -= factor * m[rank][c];
    }
    rank++;
  }
  return rank;
}

// ── Algorithm 2: exhaustive k-by-k minor determinants (cofactor expansion) ─

function* combinations(n, k) {
  if (k === 0) { yield []; return; }
  if (k > n) return;
  const idx = Array.from({ length: k }, (_, i) => i);
  yield idx.slice();
  while (true) {
    let i = k - 1;
    while (i >= 0 && idx[i] === n - k + i) i--;
    if (i < 0) return;
    idx[i]++;
    for (let j = i + 1; j < k; j++) idx[j] = idx[j - 1] + 1;
    yield idx.slice();
  }
}

// Cofactor (Laplace) expansion determinant -- deliberately NOT the same
// algorithm family as Gaussian elimination above, so a bug in one
// cannot also be a bug in the other. O(k!); fine for the small k this
// kernel is scoped to (MAX_DIMENSION=6). Exported for direct testing.
export function determinant(square) {
  const k = square.length;
  if (k === 1) return square[0][0];
  if (k === 2) return square[0][0] * square[1][1] - square[0][1] * square[1][0];
  let det = 0;
  let sign = 1;
  for (let col = 0; col < k; col++) {
    const val = square[0][col];
    if (val !== 0) {
      const minor = square.slice(1).map((row) => row.filter((_, c) => c !== col));
      det += sign * val * determinant(minor);
    }
    sign = -sign;
  }
  return det;
}

function binom(n, k) {
  if (k < 0 || k > n) return 0;
  let result = 1;
  for (let i = 0; i < k; i++) result = (result * (n - i)) / (i + 1);
  return Math.round(result);
}

/**
 * Returns the largest k (0..maxK) for which SOME k x k submatrix has a
 * nonzero determinant -- trying EVERY combination of k of the vectors
 * (rows) AND EVERY combination of k coordinate axes (columns), not just
 * the first k columns: the correct general definition of matrix rank
 * via minors requires both, and an earlier version of this function
 * that only tried the first k columns was wrong for any k strictly
 * less than `dimension` (it happened to be correct only in the special
 * case k===dimension, where there is exactly one column combination) --
 * caught and fixed before shipping, not discovered by a caller.
 *
 * Throws {budgetExceeded:true} via a thrown Error if the combinatorial
 * search (rowCombos * colCombos, summed over every k tried) would
 * exceed MAX_MINOR_CHECKS, exactly like combinatorialSearch.js's own
 * encoding-size guard -- never silently truncated.
 */
export function computeRankByMinors(vectors, dimension, maxK) {
  const n = vectors.length;
  let checks = 0;
  for (let k = Math.min(maxK, dimension, n); k >= 1; k--) {
    const combosAtK = binom(n, k) * binom(dimension, k);
    if (checks + combosAtK > MAX_MINOR_CHECKS) {
      const err = new Error(`vectorSpanKernel: minor-checking budget (${MAX_MINOR_CHECKS}) exceeded at k=${k} (${combosAtK} row x column combinations) -- refusing to silently truncate`);
      err.budgetExceeded = true;
      throw err;
    }
    for (const rowIdx of combinations(n, k)) {
      for (const colIdx of combinations(dimension, k)) {
        checks++;
        const square = rowIdx.map((r) => colIdx.map((c) => vectors[r].components[c]));
        if (Math.abs(determinant(square)) > ZERO_TOLERANCE) return k;
      }
    }
  }
  return 0;
}

// ── Top-level entry point ─────────────────────────────────────────────

export function executeVectorSpan(spec) {
  if (spec.kind === 'none') return { verdict: 'inconclusive', reason: spec.reason };

  const rankByElimination = computeRankByElimination(spec.vectors, spec.dimension);

  let rankByMinors;
  try {
    rankByMinors = computeRankByMinors(spec.vectors, spec.dimension, spec.dimension);
  } catch (e) {
    if (e.budgetExceeded) {
      return {
        verdict: 'undecided',
        rankByElimination,
        honesty: `The independent minor-checking cross-verification hit its combinatorial budget: ${e.message}. This is NOT evidence either way -- the elimination-based rank (${rankByElimination}) was computed, but could not be independently cross-checked, so no verdict is reported.`,
      };
    }
    throw e;
  }

  if (rankByElimination !== rankByMinors) {
    return {
      verdict: 'inconclusive',
      rankByElimination, rankByMinors,
      reason: `the two independent rank algorithms DISAGREE (elimination=${rankByElimination}, minors=${rankByMinors}) -- this indicates numerical ill-conditioning near the ${ZERO_TOLERANCE} zero-tolerance threshold or a real bug; the verdict is withheld rather than trusting either one`,
    };
  }

  const rank = rankByElimination;
  let claimHolds;
  if (spec.claim === 'rank-at-least') claimHolds = rank >= spec.targetRank;
  else if (spec.claim === 'rank-at-most') claimHolds = rank <= spec.targetRank;
  else claimHolds = rank === spec.targetRank; // rank-exactly

  return {
    verdict: claimHolds ? 'claim-confirmed' : 'claim-refuted',
    rank,
    rankByElimination, rankByMinors,
    dimension: spec.dimension,
    vectorCount: spec.vectors.length,
    honesty: `Independently verified by two algorithms sharing no code (Gaussian elimination with partial pivoting; exhaustive k-by-k minor determinants via cofactor expansion) -- both agree the rank is exactly ${rank}, out of a maximum possible ${spec.dimension} (ambient dimension). ${claimHolds ? 'The claim is confirmed.' : 'The claim is refuted by this computation.'} Floating-point, zero-tolerance=${ZERO_TOLERANCE} -- see this file's header for why that is disclosed, not hidden.`,
  };
}
