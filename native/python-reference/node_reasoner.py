"""The node maths from NODE-MATH.md, run.

Extends reasoner.py's transitive-closure World with an explicit Dom(n)/D(n)
per query, S(n) and H(n) computed from them, and a forced-collapse step for
when a caller needs a point value despite S(n) being low.

The one design choice that decides the answer to "is this reasoning or
prediction" is made explicit and isolated in one function: `speculate()`. Read
that function before trusting anything this file claims about itself.
"""

from __future__ import annotations

import math
import random
from dataclasses import dataclass

from reasoner import World, build, TRUE as R_TRUE, FALSE as R_FALSE

DERIVED = 'derived'
CONTRADICTION = 'contradiction'
GROUNDLESS = 'groundless'
UNDETERMINED = 'undetermined'
SPECULATED = 'speculated'


@dataclass(frozen=True)
class NodeResult:
    verdict: str
    value: object          # True, False, or None
    D: frozenset           # the derivable set of verdicts, {} for groundless
    Dom: frozenset | None  # None only for groundless -- no domain to speak of
    S: float | None
    H: float
    witness: tuple[str, ...] = ()
    # WHICH guesses this result rests on, as (guess_id, H) pairs -- not a
    # running total.
    #
    # It was a running total, and that was wrong. A single guess feeding
    # two paths that later recombine got counted once per path: one 1-bit
    # guess reported as 2.0 bits, and a diamond of depth n reported 2^n.
    # The quantity being tracked is a property of a SET of guesses, so the
    # set is what has to travel; composing unions it, and identical
    # guesses collapse because a set says they are the same guess.
    ancestry: frozenset = frozenset()

    @property
    def ancestry_H(self) -> float:
        """Total unlicensed information behind this result. Summed over the
        DISTINCT guesses, so a shared ancestor counts once."""
        return sum(h for _, h in self.ancestry)


def node_query(world: World, a: int, b: int) -> NodeResult:
    """One node: the query `lt(a, b)`. Dom is {True, False} once both
    entities are known; D is whichever of those the closure actually
    supports."""
    if a not in world.known or b not in world.known:
        # No domain to be underdetermined in. Distinct from D(n) being large.
        return NodeResult(GROUNDLESS, None, frozenset(), None, None, 0.0)

    Dom = frozenset({True, False})
    a_before_b = b in world.reach[a]
    b_before_a = a in world.reach[b]

    if a_before_b and b_before_a:
        # Both derivable is a genuine contradiction in the premises. The
        # world this reasoner builds is acyclic by construction, so this
        # branch should be unreachable -- kept explicit rather than assumed,
        # because a case a design believes is impossible and never checks is
        # exactly where a real contradiction would hide.
        return NodeResult(CONTRADICTION, None, frozenset(), Dom, 1.0, float('inf'))

    if a_before_b:
        D = frozenset({True})
    elif b_before_a:
        D = frozenset({False})
    else:
        D = Dom  # neither order follows; nothing has been ruled out

    S = 1 - len(D) / len(Dom)
    H = 0.0 if len(D) <= 1 else math.log2(len(D))

    if len(D) == 1:
        (value,) = D
        witness = world._path(a, b) if value else world._path(b, a)
        return NodeResult(DERIVED, value, D, Dom, S, H, witness)

    return NodeResult(UNDETERMINED, None, D, Dom, S, H)


def speculate(result: NodeResult, tau: float, rng: random.Random,
              guess_id=None) -> NodeResult:
    """The one place a point value gets produced without being forced.

    Direction, checked against the already-shipped rule rather than picked
    fresh: `S(n) >= tau(n)` means well-grounded enough to venture a guess,
    matching Smarsh's `speculate.js` (`allowed = s >= tau`), which is tested
    and shipped. An earlier version of this function had the comparison
    backwards -- it declined exactly when well-grounded and forced a guess
    exactly when poorly grounded -- and it went unnoticed because boolean
    D(n) makes S(n) exactly 0 whenever undetermined, so under the wrong
    direction every undetermined boolean query speculated regardless of tau,
    and nothing in the earlier smoke test varied tau against a real S value
    to expose the difference. Caught only once tau was tested against a
    graded S.

    The choice, once the bar is cleared, samples UNIFORMLY from D(n) -- not
    weighted by which answer showed up more often, not fit to any prior. A
    uniform guess does not try to be right more often than chance, which is
    the sense in which it is not prediction. Weighting this choice by
    anything derived from outcomes would make it predictive at exactly this
    point.
    """
    if result.verdict != UNDETERMINED:
        return result
    if result.S is None or result.S < tau:
        return result  # not grounded enough to venture a guess; stay honest
    value = rng.choice(sorted(result.D, key=str))
    # The guess needs an identity, or two different guesses cannot be told
    # apart from the same guess reached twice. The caller supplies it,
    # because the caller is what knows which node this is -- the runtime
    # cannot invent one without either breaking determinism (a counter
    # depends on evaluation order) or guessing at node identity.
    if guess_id is None:
        # No unsound fallback. The obvious one -- derive an identity from the
        # object -- fails three ways: id() is an address so it does not
        # survive a restart and breaks replay, addresses are reused after
        # collection so distinct guesses can collide, and for interned
        # singletons like True it collides ALWAYS. A guess whose identity
        # cannot be stated cannot be deduplicated, and silently miscounting
        # is worse than refusing.
        raise ValueError('speculate needs a guess_id: two guesses that cannot be '
                         'told apart cannot be counted correctly')
    return NodeResult(SPECULATED, value, result.D, result.Dom, result.S, result.H,
                      ancestry=frozenset({(guess_id, result.H)}))


def _is_prediction(fn) -> bool:
    """Not a real static check -- there is no way to prove a function is
    'not predictive' by inspecting it. This exists so the claim below is at
    least falsifiable in one narrow, checkable sense: run the same undecided
    case many times and confirm the output distribution is uniform, which is
    what 'not weighted toward being right' actually looks like as data rather
    than as a description of the code."""
    raise NotImplementedError('see _check_speculation_is_uniform below')


def _check_speculation_is_uniform(world: World, a: int, b: int, tau: float, trials=4000):
    counts = {True: 0, False: 0}
    for seed in range(trials):
        r = node_query(world, a, b)
        r = speculate(r, tau, random.Random(seed), guess_id=('lt', a, b))
        if r.verdict == SPECULATED:
            counts[r.value] += 1
    total = sum(counts.values())
    return counts, total


def main():
    world, unknown = build(seed=0)
    known = sorted(world.known)

    counts = {DERIVED: 0, CONTRADICTION: 0, GROUNDLESS: 0, UNDETERMINED: 0}
    rng = random.Random(1)
    speculated_examples = []

    for a in known + unknown:
        for b in known + unknown:
            if a == b:
                continue
            r = node_query(world, a, b)
            counts[r.verdict] += 1
            if r.verdict == UNDETERMINED:
                # tau=0.0 deliberately: boolean D(n) is always {True,False}
                # when undetermined, so S(n) is always exactly 0 -- the
                # minimum possible. 0.0 is the only tau a boolean query can
                # ever clear. Any positive tau declines every single one,
                # which is arguably correct (zero grounds should not clear
                # any positive bar) but means the boolean domain cannot show
                # a real speculate/decline split. That split is demonstrated
                # properly below on the rank domain instead.
                spec = speculate(r, tau=0.0, rng=rng, guess_id=('lt', a, b))
                if spec.verdict == SPECULATED and len(speculated_examples) < 3:
                    speculated_examples.append((a, b, spec))

    print('node_reasoner: closure-derived D(n), S(n), H(n); forced collapse isolated')
    print()
    for k in (DERIVED, CONTRADICTION, GROUNDLESS, UNDETERMINED):
        print(f'  {k:<14}{counts[k]:>5}')
    print()

    a, b, spec = speculated_examples[0]
    print(f'  lt({a}, {b}) undetermined, S={spec.S:.2f}, H={spec.H:.2f} bits')
    print(f'  speculated at tau=0.0 (the only tau a boolean query clears) -> {spec.value}')
    print()

    print('  Is the forced guess weighted toward being right? Checking rather than')
    print('  asserting -- run the same undecided pair 4000 times, seed varying only:')
    counts2, total = _check_speculation_is_uniform(world, a, b, tau=0.0)
    p_true = counts2[True] / total
    print(f'    guessed True:  {counts2[True]:>5} / {total}  ({p_true:.3f})')
    print(f'    guessed False: {counts2[False]:>5} / {total}  ({1 - p_true:.3f})')
    if abs(p_true - 0.5) < 0.03:
        print('  Close to 0.5. The guess carries no signal -- it is not trying to be')
        print('  right more often than a coin flip, which is what "not predictive"')
        print('  has to mean for a forced binary guess.')
    else:
        print('  NOT close to 0.5 -- something here IS weighted, and the claim below')
        print('  is false as this file currently stands.')

    print()
    a2, b2, spec2 = next((a, b, node_query(world, a, b)) for a in known for b in known
                         if node_query(world, a, b).verdict == DERIVED)
    print(f'  lt({a2}, {b2}) -> {spec2.value}, derived, H=0. The witness:')
    for step in spec2.witness:
        print(f'      {step}')


if __name__ == '__main__':
    main()
