"""tau against a real, graded S -- not just proof that S can vary.

Everything before this file showed S taking more than two values. It never
showed the speculate/decline SPLIT actually landing correctly at a chosen tau.
That's a different claim, and this is the direct test of it: construct a
5-entity world where one entity sits just above a chosen tau and another sits
just below it, then confirm the system speculates for the first and honestly
declines for the second -- not once, but across many seeds, since the
decision itself must not depend on the RNG at all (only the VALUE chosen,
once the decision to speculate has already been made without it).

This also exercises the direction fix directly: under the bug this file would
have found the split backwards.
"""

from __future__ import annotations

import random

from reasoner import World
from rank_domain import rank_query, brute_force_positions
from node_reasoner import speculate, UNDETERMINED, SPECULATED

FAILURES = []


def check(name, condition, detail=''):
    status = 'ok  ' if condition else 'FAIL'
    print(f'  {status}  {name}{("  -- " + detail) if detail and not condition else ""}')
    if not condition:
        FAILURES.append(name)


# Five entities. Built so that entity 3 has exactly 2 known predecessors and
# 1 known successor (|D|=2 of 5, S=0.6), and entity 2 has exactly 1 known
# predecessor and 1 known successor (|D|=3 of 5, S=0.4) -- straddling tau=0.5
# by the same margin on each side.
edges = {(0, 3), (1, 3), (3, 4), (0, 2), (2, 4)}
known = {0, 1, 2, 3, 4}
world = World(edges, known)

TAU = 0.5

r_above = rank_query(world, 3)   # intended S = 0.6
r_below = rank_query(world, 2)   # intended S = 0.4

print('the boundary world, checked before the boundary claim is tested on it')
check('entity 3 sits just ABOVE tau: S=0.6', r_above.S == 0.6, f'got {r_above.S}')
check('entity 2 sits just BELOW tau: S=0.4', r_below.S == 0.4, f'got {r_below.S}')
check('both are undetermined going in, not already derived',
      r_above.verdict == UNDETERMINED and r_below.verdict == UNDETERMINED,
      f'{r_above.verdict}, {r_below.verdict}')
# Not trusting the formula here either, same discipline as test_rank_domain.py.
check('entity 3\'s D matches brute-force ground truth',
      r_above.D == brute_force_positions(world, 3))
check('entity 2\'s D matches brute-force ground truth',
      r_below.D == brute_force_positions(world, 2))

print()
print(f'tau={TAU}: does the split land correctly, across 500 seeds each?')

above_verdicts = set()
below_verdicts = set()
for seed in range(500):
    above_verdicts.add(speculate(r_above, TAU, random.Random(seed), guess_id=('rank', 3)).verdict)
    below_verdicts.add(speculate(r_below, TAU, random.Random(seed), guess_id=('rank', 2)).verdict)

check('S=0.6 >= tau=0.5: ALWAYS speculates, never declines, across every seed',
      above_verdicts == {SPECULATED}, f'saw {above_verdicts}')
check('S=0.4 < tau=0.5: ALWAYS declines, never speculates, across every seed',
      below_verdicts == {UNDETERMINED}, f'saw {below_verdicts}')

# The decision itself must not be a coin flip. Only the VALUE, once speculation
# is already decided, may vary with the seed.
above_results = [speculate(r_above, TAU, random.Random(s), guess_id=('rank', 3)) for s in range(200)]
check('the decision to speculate does not depend on the seed (only the value does)',
      all(r.verdict == SPECULATED for r in above_results))
check('but the value chosen does vary across seeds (it is a real guess, not fixed)',
      len({r.value for r in above_results}) > 1,
      f'always chose {({r.value for r in above_results})}')

print()
print('the exact boundary: S == tau')

# Construct a case where S lands exactly on tau, to settle >= vs > explicitly
# rather than leave it implied by two points on either side.
edges_edge = {(0, 1), (1, 2)}   # entity 1: 1 predecessor, 1 successor, k=4 -> S=0.5
world_edge = World(edges_edge, {0, 1, 2, 3})
r_edge = rank_query(world_edge, 1)
check('constructed case actually lands at S=0.5 exactly',
      r_edge.S == 0.5, f'got {r_edge.S}')
edge_verdicts = {speculate(r_edge, 0.5, random.Random(seed), guess_id=('rank', 1)).verdict for seed in range(300)}
check('S == tau counts as clearing the bar (>=), so it speculates',
      edge_verdicts == {SPECULATED}, f'saw {edge_verdicts}')

print()
if FAILURES:
    print(f'{len(FAILURES)} check(s) failed: {", ".join(FAILURES)}')
    raise SystemExit(1)
print('all checks passed -- the speculate/decline split lands correctly at a real')
print('tau against a graded S, on both sides and at the exact boundary, and the')
print('decision itself is seed-independent even though the guessed value is not.')
