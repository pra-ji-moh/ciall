"""SUPERSEDED. Kept for the record, not for use.

compose_and() here takes two RESULTS and combines them, the same shape
as the removed sm_compose_and() in the C core, and it has the same
defect: its inputs are the two answer sets, so it cannot tell
"a and b" from "a and not a". See NO-CHAIN.md for the proof that no
such function exists, and certifiable-c/smarsh_reason.h for what
replaced it -- composition over the QUESTIONS, which is exact.

The ancestry handling below (a set, unioned, computed before any
branch returns) was right and survives in sm_ancestry_union.

----------------------------------------------------------------
Composition: a node whose inputs are other nodes' outputs.

NODE-MATH.md flagged this as the unbuilt half of "the recursive rule":
groundless nullness already propagates through arithmetic in Smarsh without a
caller checking for it, but nothing carries forward the DIFFERENT fact that an
input was itself a forced guess (H > 0) rather than a real derivation. Without
that, a chain of node-level speculations could compound into an output that
looks fully derived by the time it reaches the top, with no record of the coin
flip two steps back.

Built here as `AND` over the boolean lt() queries, because it is the smallest
composition with a real, checkable semantics: three-valued logic, which is
well known enough to verify by hand rather than trust.

The one design point worth stating rather than leaving implicit: AND has an
absorbing element. `False AND x` is False whatever `x` is -- you do not need
to know anything about `x` to know that. This has to hold even when `x` is
GROUNDLESS, and getting that wrong was the first version of this file: it
treated any groundless input as poisoning the whole composition, the same way
null poisons `+`. That is right for `+`, which has no absorbing element, and
wrong for `AND`, which does. Caught by writing out the truth table by hand
before trusting the code.
"""

from __future__ import annotations

from node_reasoner import (
    NodeResult, DERIVED, CONTRADICTION, GROUNDLESS, UNDETERMINED, SPECULATED,
)


def _is_definitely_false(r: NodeResult) -> bool:
    if r.value is not None:
        return r.value is False
    return r.verdict != GROUNDLESS and r.D == frozenset({False})


def _values(r: NodeResult) -> frozenset:
    return frozenset({r.value}) if r.value is not None else r.D


def compose_and(r1: NodeResult, r2: NodeResult) -> NodeResult:
    """`r1 AND r2`, three-valued, with False absorbing through a groundless
    partner and ancestry_H carried forward from any speculated input."""

    # Union, not sum. A guess reached by two different paths is still one
    # guess, and adding would count it twice -- which it did, until a
    # diamond test caught it.
    #
    # Computed BEFORE any branch returns, because every outcome has to carry
    # it. A contradiction reached through a guess is not the same thing as a
    # contradiction derived from the premises alone: the first may only mean
    # the guess was wrong. Dropping ancestry on the contradiction and
    # groundless paths lost exactly that, until this was moved up here.
    ancestry = r1.ancestry | r2.ancestry

    if r1.verdict == CONTRADICTION or r2.verdict == CONTRADICTION:
        Dom = frozenset({True, False})
        return NodeResult(CONTRADICTION, None, frozenset(), Dom, 1.0, float('inf'),
                          ancestry=ancestry)

    # The absorbing case, checked first and unconditionally: a definite False
    # on either side settles the conjunction regardless of the other side's
    # state, including GROUNDLESS. You do not need grounds for a proposition
    # to know a conjunction with it is false, once the other half already is.
    if _is_definitely_false(r1) or _is_definitely_false(r2):
        Dom = frozenset({True, False})
        return NodeResult(DERIVED, False, frozenset({False}), Dom, 0.5, 0.0, ancestry=ancestry)

    # Past the absorbing case: if either side is groundless, the conjunction's
    # truth depends on a proposition that has no domain, so the conjunction
    # has none either. (Had the absorbing case not been checked first, this
    # branch would have wrongly swallowed a legitimate False-via-absorption.)
    if r1.verdict == GROUNDLESS or r2.verdict == GROUNDLESS:
        return NodeResult(GROUNDLESS, None, frozenset(), None, None, 0.0,
                          ancestry=ancestry)

    # Neither contradiction, neither groundless, neither definitely false:
    # ordinary three-valued AND over what each side still allows.
    D1, D2 = _values(r1), _values(r2)
    true_ok = True in D1 and True in D2
    false_ok = False in D1 or False in D2   # possible, even if not certain
    Dom = frozenset({True, False})
    D = frozenset(v for v, ok in ((True, true_ok), (False, false_ok)) if ok)

    if not D:
        # Should be unreachable -- kept explicit rather than assumed, same
        # discipline as the contradiction branch in node_reasoner.py.
        return NodeResult(CONTRADICTION, None, frozenset(), Dom, 1.0, float('inf'),
                          ancestry=ancestry)

    S = 1 - len(D) / len(Dom)
    H_own = 0.0 if len(D) <= 1 else 1.0

    if len(D) == 1:
        (value,) = D
        return NodeResult(DERIVED, value, D, Dom, S, H_own, ancestry=ancestry)
    return NodeResult(UNDETERMINED, None, D, Dom, S, H_own, ancestry=ancestry)
