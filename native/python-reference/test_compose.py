"""compose_and, checked against a hand-computed truth table rather than
trusted from the derivation. This is the file that caught the absorbing-
element bug: an earlier version poisoned any composition with a groundless
input unconditionally, the way `+` poisons on null -- wrong for AND, which
has an absorbing element that should win regardless.
"""

from __future__ import annotations

from node_reasoner import NodeResult, DERIVED, CONTRADICTION, GROUNDLESS, UNDETERMINED, SPECULATED
from compose import compose_and

FAILURES = []


def check(name, condition, detail=''):
    status = 'ok  ' if condition else 'FAIL'
    print(f'  {status}  {name}{("  -- " + detail) if detail and not condition else ""}')
    if not condition:
        FAILURES.append(name)


DOM = frozenset({True, False})


def derived(value):
    return NodeResult(DERIVED, value, frozenset({value}), DOM, 0.5, 0.0)


def undetermined():
    return NodeResult(UNDETERMINED, None, DOM, DOM, 0.0, 1.0)


def groundless():
    return NodeResult(GROUNDLESS, None, frozenset(), None, None, 0.0)


def contradiction():
    return NodeResult(CONTRADICTION, None, frozenset(), DOM, 1.0, float('inf'))


_next_guess = [0]


def speculated(value, H=1.0, ident=None):
    """Each call is a DISTINCT guess unless an explicit ident says otherwise.
    The counter exists because the first version derived identity from the
    value, and True is interned -- so two separate guesses collided and the
    diamond fix silently swallowed one of them."""
    if ident is None:
        _next_guess[0] += 1
        ident = ('guess', _next_guess[0])
    return NodeResult(SPECULATED, value, DOM, DOM, 0.0, H,
                      ancestry=frozenset({(ident, H)}))


# ---------------------------------------------------------------------------
# 1. The ordinary three-valued truth table, hand-computed
# ---------------------------------------------------------------------------

print('compose_and against a hand-computed three-valued truth table')

cases = [
    # (r1, r2, expected verdict, expected value)
    (derived(True),  derived(True),  DERIVED, True),
    (derived(True),  derived(False), DERIVED, False),
    (derived(False), derived(True),  DERIVED, False),
    (derived(False), derived(False), DERIVED, False),
    (derived(True),  undetermined(), UNDETERMINED, None),
    (undetermined(), derived(True),  UNDETERMINED, None),
    (derived(False), undetermined(), DERIVED, False),   # absorption
    (undetermined(), derived(False), DERIVED, False),   # absorption
    (undetermined(), undetermined(), UNDETERMINED, None),
]
for i, (r1, r2, want_verdict, want_value) in enumerate(cases):
    got = compose_and(r1, r2)
    check(f'case {i}: {r1.verdict}({r1.value}) AND {r2.verdict}({r2.value}) -> {want_verdict}({want_value})',
          got.verdict == want_verdict and got.value == want_value,
          f'got {got.verdict}({got.value})')

# ---------------------------------------------------------------------------
# 2. The absorbing element through a GROUNDLESS partner -- the bug this file
#    was built to catch, checked explicitly rather than folded into the table
# ---------------------------------------------------------------------------

print()
print('the absorbing case: False wins even when the other side is groundless')

r = compose_and(groundless(), derived(False))
check('groundless AND definitely-False -> DERIVED False, not groundless',
      r.verdict == DERIVED and r.value is False, f'got {r.verdict}({r.value})')

r = compose_and(derived(False), groundless())
check('definitely-False AND groundless -> DERIVED False (order should not matter)',
      r.verdict == DERIVED and r.value is False, f'got {r.verdict}({r.value})')

r = compose_and(groundless(), derived(True))
check('groundless AND definitely-True -> groundless (True does not absorb)',
      r.verdict == GROUNDLESS, f'got {r.verdict}')

r = compose_and(groundless(), undetermined())
check('groundless AND undetermined -> groundless (nothing forces False)',
      r.verdict == GROUNDLESS, f'got {r.verdict}')

r = compose_and(groundless(), groundless())
check('groundless AND groundless -> groundless',
      r.verdict == GROUNDLESS, f'got {r.verdict}')

# ---------------------------------------------------------------------------
# 3. Contradiction propagates unconditionally
# ---------------------------------------------------------------------------

print()
print('contradiction propagates through composition')

r = compose_and(contradiction(), derived(True))
check('contradiction AND anything -> contradiction',
      r.verdict == CONTRADICTION, f'got {r.verdict}')

# ---------------------------------------------------------------------------
# 4. Ancestry: a chain of guesses does not become invisible
# ---------------------------------------------------------------------------

print()
print('ancestry_H: ' 'this rests on a guess' ' survives composition')

s1 = speculated(True, H=1.0)   # a coin flip upstream
d2 = derived(True)             # a real derivation
r = compose_and(s1, d2)
check('composing a speculated True with a derived True still resolves to True',
      r.verdict == DERIVED and r.value is True, f'got {r.verdict}({r.value})')
check('but ancestry_H carries the upstream guess forward -- this is NOT fully derived',
      r.ancestry_H == 1.0, f'got {r.ancestry_H}')
check('the node\'s OWN H is still 0 -- the composition itself was forced, only its input was a guess',
      r.H == 0.0, f'got {r.H}')

# A second guess composed on top: ancestry should ACCUMULATE, not reset.
s3 = speculated(True, H=1.0)
r2 = compose_and(r, s3)
check('a second speculated input adds to ancestry rather than replacing it',
      r2.ancestry_H == 2.0, f'got {r2.ancestry_H} (expected 1.0 from r plus 1.0 from s3)')

# Two derivations, no guesses anywhere: ancestry stays exactly zero.
r3 = compose_and(derived(True), derived(True))
check('two genuine derivations compose with ancestry_H = 0 exactly',
      r3.ancestry_H == 0.0, f'got {r3.ancestry_H}')

# The absorbing case still has to carry ancestry, even though the RESULT was
# forced -- a False that only exists because of a definite False elsewhere
# still owes its ancestry to whatever guesses fed the OTHER side, if any.
r4 = compose_and(speculated(False, H=1.0), undetermined())
check('absorption through a speculated False still carries that guess in ancestry',
      r4.verdict == DERIVED and r4.value is False and r4.ancestry_H == 1.0,
      f'got {r4.verdict}({r4.value}), ancestry={r4.ancestry_H}')


# ---------------------------------------------------------------------------
# shared ancestry: a guess reached twice is still one guess
# ---------------------------------------------------------------------------

print()
print("the diamond: one guess feeding two paths that recombine")

g = speculated(True, 1.0, ident="g1")
left = compose_and(g, derived(True))
right = compose_and(g, derived(True))
joined = compose_and(left, right)
check("one shared guess counted once, not twice",
      joined.ancestry_H == 1.0, f"got {joined.ancestry_H}")

deep = g
for _ in range(5):
    deep = compose_and(deep, deep)
check("a depth-5 diamond does not explode to 2^5 bits",
      deep.ancestry_H == 1.0, f"got {deep.ancestry_H}")

g2 = speculated(True, 1.0, ident="g2")
both = compose_and(compose_and(g, derived(True)), compose_and(g2, derived(True)))
check("two genuinely distinct guesses still add to 2.0",
      both.ancestry_H == 2.0, f"got {both.ancestry_H}")

print()
print("ancestry survives the branches that used to drop it")

contra = NodeResult(CONTRADICTION, None, frozenset(), DOM, 1.0, float("inf"))
check("a contradiction reached through a guess remembers the guess",
      compose_and(g, contra).ancestry_H == 1.0)
check("a groundless result reached through a guess remembers the guess",
      compose_and(g, groundless()).ancestry_H == 1.0)

# The summary comes last. It used to sit above the diamond section, so
# those checks ran after "all checks passed" and could not fail the file.
print()
if FAILURES:
    print(f'{len(FAILURES)} check(s) failed: {", ".join(FAILURES)}')
    raise SystemExit(1)
print('all checks passed')
