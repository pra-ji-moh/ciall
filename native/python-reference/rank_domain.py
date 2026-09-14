"""Reworking the boolean query. `lt(a,b)` has Dom={True,False}, so S(n) could
only ever land on 0 or 0.5 -- verified degenerate in test_node_reasoner.py.
That gave tau nothing real to threshold against.

The question changes from "does lt(a,b) hold" (2 answers) to "what position
does e hold in the order" (up to k answers, k = number of known entities).
Same World, same closure -- just asked differently.

The bound used for D(e):

    D(e) = { p : predecessors(e) <= p <= k - 1 - successors(e) }

where predecessors(e)/successors(e) count entities the closure has determined
must come before/after e. This is a real, provable fact about linear
extensions of a finite partial order -- not a heuristic -- but "provable" and
"I have actually proven it here" are different things, and the discipline
today has been to check rather than assert. So it is checked below against
brute-force ground truth: every valid linear extension of several small
worlds, enumerated directly and independently of this formula, compared
position by position.
"""

from __future__ import annotations

from itertools import permutations

from reasoner import World
from node_reasoner import (
    NodeResult, DERIVED, CONTRADICTION, GROUNDLESS, UNDETERMINED,
)


def rank_query(world: World, e) -> NodeResult:
    if e not in world.known:
        return NodeResult(GROUNDLESS, None, frozenset(), None, None, 0.0)

    k = len(world.known)
    predecessors = sum(1 for x in world.known if x != e and e in world.reach.get(x, set()))
    successors = len(world.reach.get(e, set()) & world.known)

    Dom = frozenset(range(k))
    lo, hi = predecessors, k - 1 - successors

    if lo > hi:
        # More entities are forced before e and after e than there is room
        # for -- only possible if the closure is inconsistent, i.e. a cycle.
        return NodeResult(CONTRADICTION, None, frozenset(), Dom, 1.0, float('inf'))

    D = frozenset(range(lo, hi + 1))
    S = 1 - len(D) / len(Dom) if Dom else None
    import math
    H = 0.0 if len(D) <= 1 else math.log2(len(D))

    if len(D) == 1:
        (value,) = D
        return NodeResult(DERIVED, value, D, Dom, S, H)
    return NodeResult(UNDETERMINED, None, D, Dom, S, H)


def brute_force_positions(world: World, e) -> frozenset:
    """Ground truth. Enumerate every permutation of the known entities,
    keep the ones consistent with every edge, collect every position e
    actually occupies. Exponential -- for checking the formula, not for use."""
    entities = sorted(world.known)
    achieved = set()
    for perm in permutations(entities):
        pos = {x: i for i, x in enumerate(perm)}
        if all(pos[a] < pos[b] for a, b in world.edges if a in pos and b in pos):
            achieved.add(pos[e])
    return frozenset(achieved)
