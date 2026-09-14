"""Reasoning does the deciding. Prediction is confined to where derivation is
legitimately silent, and is marked as a guess when it speaks.

The 2D task this replaces was a prediction problem wearing a reasoning costume.
Its rule was a smooth surface, so a held-out region was still interpolable, and
the baseline scored 99.5% accuracy in a region it had never seen. It was right
to. There were grounds there: inductive ones. A test a predictive model can ace
cannot detect the thing being built.

So the task is compositional instead. The answer to a query is not a point on a
surface to be interpolated; it is a consequence to be derived, or not.

  Facts     a set of `lt(a, b)` edges over known entities.
  Rule      transitivity. lt(X,Y) and lt(Y,Z) gives lt(X,Z).
  Query     does lt(a, b) hold?

Four outcomes, and the reasoner knows which it is in every case, because it
knows what it derived and from what:

  TRUE          a path a -> b exists. Answer yes, and the path is the grounds.
  FALSE         a path b -> a exists, so lt(a,b) contradicts the premises.
  UNDETERMINED  both entities are known, and neither order is derivable. The
                premises genuinely do not decide it. This is aleatoric, and
                abstaining here is WRONG: the correct answer is "the premises
                permit both", which is itself derived and grounded.
  GROUNDLESS    an entity that appears in no fact. Nothing to reason from.
                The only case where abstention is correct.

The distinction between the last two is the whole point, and a deductive engine
gets it exactly right for a structural reason rather than a statistical one: it
knows whether it had premises. A softmax has no such knowledge and cannot
acquire it, because "I was not given anything about this" is not a fact about
the input distribution, it is a fact about the derivation.

Zero learned parameters. Everything here is a closure computation.
"""

from __future__ import annotations

import random
from dataclasses import dataclass

TRUE = 'true'
FALSE = 'false'
UNDETERMINED = 'undetermined'
GROUNDLESS = 'groundless'


@dataclass(frozen=True)
class Answer:
    verdict: str
    # The derivation. Empty for UNDETERMINED (nothing follows) and for
    # GROUNDLESS (nothing to derive from), and those two emptinesses are
    # distinguished by the verdict, not by the absence.
    grounds: tuple[str, ...]


class World:
    """A set of facts and the rule that closes them."""

    def __init__(self, edges: set[tuple[int, int]], known: set[int]):
        self.edges = edges
        self.known = known
        self.reach = self._closure()

    def _closure(self) -> dict[int, set[int]]:
        """Transitive closure by repeated relaxation.

        This is the entire 'model'. It has no parameters and does not learn.
        Two runs on the same facts produce the same closure, necessarily.
        """
        reach: dict[int, set[int]] = {e: set() for e in self.known}
        for a, b in self.edges:
            reach[a].add(b)
        changed = True
        while changed:
            changed = False
            for a in self.known:
                extra = set()
                for b in reach[a]:
                    extra |= reach[b]
                if not extra <= reach[a]:
                    reach[a] |= extra
                    changed = True
        return reach

    def _path(self, a: int, b: int) -> tuple[str, ...]:
        """A witness for the answer. A verdict without one is an assertion."""
        prev: dict[int, int] = {a: a}
        frontier = [a]
        while frontier:
            nxt = []
            for u in frontier:
                for v in self.edges_from(u):
                    if v not in prev:
                        prev[v] = u
                        if v == b:
                            steps, cur = [], b
                            while cur != a:
                                steps.append(f'lt({prev[cur]}, {cur})')
                                cur = prev[cur]
                            return tuple(reversed(steps))
                        nxt.append(v)
            frontier = nxt
        return ()

    def edges_from(self, u: int) -> set[int]:
        return {b for a, b in self.edges if a == u}

    def ask(self, a: int, b: int) -> Answer:
        # Grounds first. Not "is the answer uncertain" but "is there anything
        # here to reason from at all". This check is why the engine can tell
        # the two kinds of not-knowing apart, and it costs one set lookup.
        if a not in self.known or b not in self.known:
            return Answer(GROUNDLESS, ())

        if b in self.reach[a]:
            return Answer(TRUE, self._path(a, b))
        if a in self.reach[b]:
            return Answer(FALSE, self._path(b, a))
        return Answer(UNDETERMINED, ())


def build(n_entities=14, n_edges=18, seed=0) -> tuple[World, list[int]]:
    """A random DAG over known entities, plus entities that appear in no fact.

    The unknown ones are the point: they are not rare inputs or outliers, they
    are entities the premises never mention. No amount of data about the others
    says anything about them.
    """
    rng = random.Random(seed)
    known = set(range(n_entities))
    order = list(known)
    rng.shuffle(order)
    rank = {e: i for i, e in enumerate(order)}

    edges = set()
    while len(edges) < n_edges:
        a, b = rng.sample(order, 2)
        if rank[a] < rank[b]:          # keep it acyclic, so FALSE is meaningful
            edges.add((a, b))

    unknown = list(range(n_entities, n_entities + 6))
    return World(edges, known), unknown


def queries(world: World, unknown: list[int], n=400, seed=1):
    rng = random.Random(seed)
    pool = sorted(world.known)
    out = []
    for _ in range(n):
        if rng.random() < 0.25:
            a = rng.choice(pool)
            b = rng.choice(unknown)
            if rng.random() < 0.5:
                a, b = b, a
        else:
            a, b = rng.sample(pool, 2)
        out.append((a, b))
    return out


def main():
    world, unknown = build(seed=0)
    qs = queries(world, unknown, n=600, seed=1)

    counts = {TRUE: 0, FALSE: 0, UNDETERMINED: 0, GROUNDLESS: 0}
    with_grounds = 0
    for a, b in qs:
        ans = world.ask(a, b)
        counts[ans.verdict] += 1
        if ans.grounds:
            with_grounds += 1

    print('reasoner: transitive closure, 0 learned parameters')
    print(f'  facts: {len(world.edges)} edges over {len(world.known)} entities')
    print(f'  {len(unknown)} entities appear in no fact at all')
    print()
    for k in (TRUE, FALSE, UNDETERMINED, GROUNDLESS):
        print(f'  {k:<14}{counts[k]:>5}')
    print()
    print(f'  answers carrying an explicit derivation: {with_grounds}')
    print()

    # The two kinds of not-knowing, side by side.
    ua, ub = next((a, b) for a, b in qs if world.ask(a, b).verdict == UNDETERMINED)
    ga, gb = next((a, b) for a, b in qs if world.ask(a, b).verdict == GROUNDLESS)
    print(f'  lt({ua}, {ub}) -> undetermined : both entities known, neither order follows.')
    print('                                   The premises permit both. Answering')
    print('                                   "undetermined" IS the grounded answer.')
    print(f'  lt({ga}, {gb}) -> groundless   : an entity no fact mentions. Nothing to')
    print('                                   derive from. Abstention is correct.')
    print()
    print('  A confidence number cannot express that difference. A derivation can,')
    print('  because it knows what it had, not merely what it produced.')

    # A worked derivation, because a verdict without one is an assertion.
    for a, b in qs:
        ans = world.ask(a, b)
        if ans.verdict == TRUE and len(ans.grounds) >= 3:
            print()
            print(f'  lt({a}, {b}) -> true, and here is why:')
            for step in ans.grounds:
                print(f'      {step}')
            break


if __name__ == '__main__':
    main()
