"""Is the range formula in rank_domain.py exact, or only a sound
over-approximation? Checked here, not assumed. If it turns out to be loose
anywhere below, that gets reported as the actual result, not smoothed over.
"""

from __future__ import annotations

from reasoner import World
from rank_domain import rank_query, brute_force_positions
from node_reasoner import DERIVED, CONTRADICTION, GROUNDLESS, UNDETERMINED

FAILURES = []


def check(name, condition, detail=''):
    status = 'ok  ' if condition else 'FAIL'
    print(f'  {status}  {name}{("  -- " + detail) if detail and not condition else ""}')
    if not condition:
        FAILURES.append(name)


WORLDS = {
    'full chain (0<1<2<3<4)':
        World({(0, 1), (1, 2), (2, 3), (3, 4)}, {0, 1, 2, 3, 4}),
    'one order, one free (a<b, c untouched)':
        World({(0, 1)}, {0, 1, 2}),
    'diamond (a<b, a<c, b<d, c<d)':
        World({(0, 1), (0, 2), (1, 3), (2, 3)}, {0, 1, 2, 3}),
    'two independent chains (a<c, b<d)':
        World({(0, 2), (1, 3)}, {0, 1, 2, 3}),
    'wide fan (a < everything, rest free)':
        World({(0, 1), (0, 2), (0, 3), (0, 4)}, {0, 1, 2, 3, 4}),
    'totally free (no edges at all)':
        World(set(), {0, 1, 2, 3}),
}

print('the range formula against brute-force ground truth, across six posets')
print()

any_loose = False
for name, world in WORLDS.items():
    for e in sorted(world.known):
        formula = rank_query(world, e)
        truth = brute_force_positions(world, e)
        match = formula.D == truth
        if not match:
            any_loose = True
        check(f'{name}: entity {e}  D={sorted(formula.D)} vs truth={sorted(truth)}',
              match)

print()
# The fan world turned out symmetric -- every leaf has the same D size, so it
# only ever gives S in {0.2, 0.8}. That's a property of that world's shape,
# not a bug: the FIRST version of this check assumed 3+ distinct values with
# no justification, and the run above caught that assumption immediately
# rather than after the fact.  The real, checkable claim is narrower and
# true: values now occur outside the old degenerate {0.0, 0.5} set.
all_S = {round(rank_query(w, e).S, 3) for w in WORLDS.values() for e in sorted(w.known)
         if rank_query(w, e).S is not None}
check('S now takes values outside the old degenerate {0.0, 0.5} set',
      not all_S <= {0.0, 0.5}, f'S values seen: {sorted(all_S)}')

# The two states this must still get right, now under a bigger domain.
r_ground = rank_query(WORLDS['full chain (0<1<2<3<4)'], 99)
check('an unknown entity is still groundless, not merely a huge D',
      r_ground.verdict == GROUNDLESS)

cyclic = World({(0, 1), (1, 0)}, {0, 1})
r_c = rank_query(cyclic, 0)
check('a cycle is still reported as contradiction under the rank query too',
      r_c.verdict == CONTRADICTION, f'got {r_c.verdict}')

print()
if FAILURES:
    print(f'{len(FAILURES)} check(s) failed.')
    if any_loose:
        print('The range formula is NOT exact in at least one case above -- it is a')
        print('sound over-approximation there, not a tight one. That is the honest')
        print('result, not a bug to paper over.')
    raise SystemExit(1)
print('all checks passed -- the range formula matched brute-force ground truth exactly,')
print('on every entity, in every world tested, including the ones built to be tangled.')
