"""The reasoning foundation, verified before anything gets built on it.

Assert-based, no test framework, matching the convention `dataset.py` already
set. Every check here exists because it is a place the maths could be wrong
without printing anything wrong -- the S/H bug in NODE-MATH.md was exactly
that: a formula that looked fine until someone computed it.

Four things get checked hard, because they are the four places "looks right"
and "is right" can quietly diverge in this kind of system:

  1. S and H against hand-computed values, as a regression test for the
     direction bug already found once.
  2. The contradiction branch, FORCED rather than trusted. It printed 0 in
     the last run because the acyclic world never reaches it. Unreached code
     is not verified code, so this builds a world where it must fire.
  3. The uniformity claim, as an assertion with a stated tolerance, not a
     number eyeballed in a printout.
  4. Determinism: the same seed must produce the same guess, every time,
     because a system that cannot replay from a seed is not producing
     evidence, only a plausible-looking run.

If this file passes, the base layer is verified to the degree these checks
reach. It does not mean the design is right -- only that the arithmetic
matches what the design claims about itself.
"""

from __future__ import annotations

import math
import random

from reasoner import World, build
from node_reasoner import (
    node_query, speculate,
    DERIVED, CONTRADICTION, GROUNDLESS, UNDETERMINED, SPECULATED,
)

FAILURES = []


def check(name, condition, detail=''):
    status = 'ok  ' if condition else 'FAIL'
    print(f'  {status}  {name}{("  -- " + detail) if detail and not condition else ""}')
    if not condition:
        FAILURES.append(name)


# ---------------------------------------------------------------------------
# 1. S and H, against numbers computed by hand, not by the code under test
# ---------------------------------------------------------------------------

print('S(n) and H(n), regression-checked against hand-computed values')

# A single-fact world: 0 < 1. Domain for any query is always {True, False}.
w = World(edges={(0, 1)}, known={0, 1})

r = node_query(w, 0, 1)  # derived: True
check('derived: |D|=1, |Dom|=2 gives S=0.5, not 1',
      r.S == 0.5, f'got S={r.S}')
check('derived: H=0 (the emission was forced, not guessed)',
      r.H == 0.0, f'got H={r.H}')
check('derived: verdict and value are correct',
      r.verdict == DERIVED and r.value is True)

w2 = World(edges=set(), known={5, 6})  # no facts at all between 5 and 6
r2 = node_query(w2, 5, 6)  # undetermined: nothing rules either order out
check('undetermined: |D|=2=|Dom|, S=0 exactly (nothing ruled out)',
      r2.S == 0.0, f'got S={r2.S}')
check('undetermined: H=1.0 bit (one of two, forced to guess)',
      r2.H == 1.0, f'got H={r2.H}')
check('undetermined: verdict is undetermined, no value yet',
      r2.verdict == UNDETERMINED and r2.value is None)

# The gap worth naming rather than hiding: with a boolean domain, S can only
# land on 0.0 or 0.5. There is no intermediate value, so tau has no room to
# discriminate within a single query -- it only matters once Dom(n) is larger
# than two elements. That is a real limitation of this instantiation, not of
# the formula, and it should not be quietly true only in a printout.
possible_S_values = {node_query(w, 0, 1).S, node_query(w2, 5, 6).S}
check('S is degenerate on a boolean domain: only {0.0, 0.5} occur',
      possible_S_values == {0.5, 0.0},
      f'got {possible_S_values} -- if this ever differs, the note above is stale')

r3 = node_query(w, 2, 3)  # neither entity known
check('groundless: no domain, S is None, not 0',
      r3.verdict == GROUNDLESS and r3.S is None and r3.Dom is None,
      f'verdict={r3.verdict} S={r3.S} Dom={r3.Dom}')

# ---------------------------------------------------------------------------
# 2. Contradiction, forced rather than trusted
# ---------------------------------------------------------------------------

print()
print('the contradiction branch, forced by a cyclic world rather than assumed unreachable')

cyclic = World(edges={(0, 1), (1, 0)}, known={0, 1})
rc = node_query(cyclic, 0, 1)
check('a genuine cycle is reported as contradiction, not silently resolved',
      rc.verdict == CONTRADICTION, f'got verdict={rc.verdict}')
check('contradiction carries no value to act on',
      rc.value is None)
check('contradiction is reported at maximum support -- the premises are certain and wrong',
      rc.S == 1.0, f'got S={rc.S}')

# ---------------------------------------------------------------------------
# 3. The uniformity claim, pinned with a tolerance rather than eyeballed
# ---------------------------------------------------------------------------

print()
print('speculation is uniform: pinned with a tolerance, not read off a printout')

world3, _ = build(seed=0)
known3 = sorted(world3.known)
a, b = next((a, b) for a in known3 for b in known3
            if a != b and node_query(world3, a, b).verdict == UNDETERMINED)

# tau=0.0, not 0.5: boolean D(n) is always {True, False} when undetermined,
# so S(n) is always exactly 0 -- the minimum. Under the corrected direction
# (S >= tau to speculate), 0.0 is the only tau this domain can ever clear.
# The real speculate-vs-decline split, at a real tau, is tested on the graded
# rank domain in test_rank_domain_tau_boundary.py -- this file only checks
# that the guess is unweighted once speculation happens at all.
trials = 6000
outcomes = {True: 0, False: 0}
for seed in range(trials):
    r = node_query(world3, a, b)
    r = speculate(r, tau=0.0, rng=random.Random(seed), guess_id=('lt', a, b))
    outcomes[r.value] += 1

p_true = outcomes[True] / trials
check(f'guessed True close to 0.5 over {trials} trials (tolerance 0.03)',
      abs(p_true - 0.5) < 0.03, f'got {p_true:.4f}')

# ---------------------------------------------------------------------------
# 4. Determinism: a seed must reproduce, or nothing here is evidence
# ---------------------------------------------------------------------------

print()
print('determinism: same seed reproduces exactly')

r_a = speculate(node_query(world3, a, b), tau=0.0, rng=random.Random(42), guess_id=('lt', a, b))
r_b = speculate(node_query(world3, a, b), tau=0.0, rng=random.Random(42), guess_id=('lt', a, b))
check('two runs with the same seed agree on the guess',
      r_a.value == r_b.value, f'{r_a.value} vs {r_b.value}')

seeds_seen = {speculate(node_query(world3, a, b), tau=0.0, rng=random.Random(s), guess_id=('lt', a, b)).value
              for s in range(20)}
check('different seeds are not all forced to the same guess',
      len(seeds_seen) == 2, f'only saw {seeds_seen}')

# ---------------------------------------------------------------------------

print()
if FAILURES:
    print(f'{len(FAILURES)} check(s) failed: {", ".join(FAILURES)}')
    raise SystemExit(1)
print('all checks passed')
