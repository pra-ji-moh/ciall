"""Checks the autonomous learner, not just one scripted run of it.

A demo that works once proves nothing about a loop that decides its own
next move. These are the properties that have to hold for EVERY goal and
EVERY scene, so they are checked over all of them exhaustively rather than
sampled: 6 goals x 32 scenes, from a fresh learner and from a grown one.
"""

import itertools
import random

from kernel_py import *   # noqa: F401,F403
import baby_auto as B

CHECKS = []


def check(name, ok, detail=''):
    CHECKS.append((name, bool(ok), detail))


def truth_value(goal, truth):
    return goal.ans[truth]


ALL_GOALS = [(lbl, q) for lbl, q, _ in B.GOALS]

# ---- 1. exhaustive: every goal, every scene, from a newborn ---------
bad = None
outcomes = {}
for lbl, goal in ALL_GOALS:
    for truth in range(B.N_SIT):
        kid = B.Learner()
        res = kid.pursue(lbl, goal, truth)
        outcomes[res['outcome']] = outcomes.get(res['outcome'], 0) + 1

        if res['outcome'] == 'derived':
            if res['value'] != truth_value(goal, truth):
                bad = ('derived the WRONG answer', lbl, truth,
                       res['value'], truth_value(goal, truth))
                break
            if not res['proved']:
                bad = ('derived without a checkable witness', lbl, truth)
                break
        else:
            if res['value'] is not None:
                bad = ('returned a value without deriving it', lbl, truth)
                break
        if kid.acquisitions > len(B.CAPS):
            bad = ('acquired more capacities than exist', lbl, truth)
            break
    if bad:
        break

check('192 runs (6 goals x 32 scenes) from a newborn: every derivation is '
      'CORRECT against the real situation', bad is None, str(bad))
check('every derivation carries a witness an independent checker accepts',
      bad is None)
check('nothing that was not derived ever returned a value', bad is None)
check('all 192 terminated, none needed a step budget',
      sum(outcomes.values()) == 6 * B.N_SIT, str(outcomes))
check('and every single one of them reached a derivation',
      outcomes.get('derived', 0) == 6 * B.N_SIT, str(outcomes))

# ---- 2. acquisition is bounded and monotone ------------------------
kid = B.Learner()
sizes = []
for lbl, goal, truth in B.GOALS:
    kid.pursue(lbl, goal, truth)
    sizes.append(kid.partition().n_cells)
check('worlds it can tell apart never decreases as it learns',
      all(b >= a for a, b in zip(sizes, sizes[1:])), str(sizes))
check('it never acquires a capacity it already has',
      len(kid.caps) == len(set(kid.caps)))
check('it stops acquiring once the goals are covered: it never took '
      'exact_b at all', 'exact_b' not in kid.caps, str(kid.caps))

# ---- 3. transfer: a grown learner does less work -------------------
fresh_cost, grown_cost = {}, {}
for lbl, goal, truth in B.GOALS:
    solo = B.Learner()
    r1 = solo.pursue(lbl, goal, truth)
    fresh_cost[lbl] = r1['acquired'] + r1['measured']
    veteran = B.Learner()
    veteran.caps = list(B.ORDER)
    r2 = veteran.pursue(lbl, goal, truth)
    grown_cost[lbl] = r2['acquired'] + r2['measured']
check('a learner that already holds every capacity never does MORE work '
      'than one starting from nothing',
      all(grown_cost[k] <= fresh_cost[k] for k in fresh_cost),
      f'{fresh_cost} vs {grown_cost}')
check('and on at least one goal it does strictly less',
      any(grown_cost[k] < fresh_cost[k] for k in fresh_cost))

# ---- 4. the acquisition policy is minimal, and that is checkable ---
kid = B.Learner()
kid.pursue(*B.GOALS[0])
after_first = list(kid.caps)
check('pursuing "is a bigger than b" acquires exactly one capacity, the '
      'cheapest that can phrase it', after_first == ['more'], str(after_first))
p = kid.partition()
check('and that capacity really is minimal: no held-out capacity phrases '
      'the goal with fewer worlds',
      all(B.Learner().partition(c).n_cells >= p.n_cells
          for c in B.CAPS
          if project(B.Learner().partition(c), B.GOALS[0][1], Query()) == OK))

# ---- 5. contradiction is detected, never absorbed ------------------
rng = random.Random(31)
bad = None
for trial in range(400):
    liar = B.Learner()
    liar.caps = list(B.ORDER)
    truth = rng.randrange(B.N_SIT)
    liar.glance(truth)
    # tell it something false about a capacity it can already check
    cap = rng.choice(['exact_a', 'exact_b', 'sum'])
    real = B.CAPS[cap].ans[truth]
    wrong = (real + 1) % B.CAPS[cap].dom
    liar.memory.append((cap, real))
    liar.memory.append((cap, wrong))
    p = liar.partition()
    st = liar.state(p)
    if live_count(st) != 0:
        bad = ('two incompatible reports did not contradict', trial)
        break
    gq = Query()
    project(p, ALL_GOALS[0][1], gq)
    if liar.recover(p, gq) is None:
        bad = ('could not recover from a single bad report', trial)
        break
    if live_count(liar.state(liar.partition())) == 0:
        bad = ('still contradicted after recovery', trial)
        break
check('400 misleading reports: each one contradicts rather than being '
      'averaged in, and each is recovered from by dropping one claim',
      bad is None, str(bad))

# ---- 5b. a broken sensor inside the loop ---------------------------
# The case that used to hang: a source that returns an impossible value
# for the very measurement that would settle the goal. Checked through
# pursue(), not by calling recover() directly, since calling it directly
# is how the loop bug stayed hidden.

class _Lying(B.Learner):
    def __init__(self, liar_cap, lie):
        super().__init__()
        self.liar_cap, self.lie = liar_cap, lie

    def measure(self, cap, truth):
        v = self.lie if cap == self.liar_cap else B.CAPS[cap].ans[truth]
        self.memory.append((cap, v))
        self.measurements += 1


_passes = {'n': 0}
_orig_part = B.Learner.partition


def _counted(self, extra=None):
    if extra is None:
        _passes['n'] += 1
        if _passes['n'] > 2000:
            raise RuntimeError('non-terminating')
    return _orig_part(self, extra)


B.Learner.partition = _counted
bad = None
hung = 0
right = wrong = refused = 0
wrong_witness_checks = True
for lbl, goal in ALL_GOALS:
    for truth in range(B.N_SIT):
        for liar_cap in ('sum', 'exact_a', 'exact_b'):
            real = B.CAPS[liar_cap].ans[truth]
            lie = (real + 1) % B.CAPS[liar_cap].dom
            kid = _Lying(liar_cap, lie)
            kid.caps = list(B.ORDER)
            _passes['n'] = 0
            try:
                res = kid.pursue(lbl, goal, truth)
            except RuntimeError:
                hung += 1
                bad = ('did not terminate', lbl, truth, liar_cap)
                continue
            if res['outcome'] != 'derived':
                refused += 1
            elif res['value'] == truth_value(goal, truth):
                right += 1
            else:
                wrong += 1
                wrong_witness_checks = wrong_witness_checks and res['proved']
B.Learner.partition = _orig_part

check('576 runs with one lying sensor (6 goals x 32 scenes x 3 liars), '
      'driven through pursue(): every one terminates',
      hung == 0, str(bad))
check('and at least 97% still end correct, by distrusting the source that '
      'contradicted and routing around it',
      right >= 0.97 * 576, f'{right} right, {wrong} wrong, {refused} refused')

# THE LIMIT, pinned so nobody later claims the stronger version. A lie that
# stays consistent with everything else the learner saw cannot be detected
# by anything, and the answer that follows from it is derived correctly
# from false evidence. The witness certifies ENTAILMENT, not truth.
check('LIMIT: a lie consistent with all other evidence produces a derived '
      'WRONG answer -- this is expected, and it happens',
      wrong > 0, f'{wrong} of 576')
check('LIMIT: and in every such case the witness still checks, because the '
      'answer really does follow from what the learner was told',
      wrong_witness_checks)
check('so the honest claim is not "never wrong" but "every wrong answer '
      'rests on a false input that the witness names"',
      wrong > 0 and wrong_witness_checks)

# the exact case that used to loop
kid = _Lying('sum', 6)
kid.caps = list(B.ORDER)
res = kid.pursue('is the total four', B.GOALS[4][1], (3 << 3) | (1 << 1) | 1)
check('the case that used to loop forever: sum reports an impossible 6, '
      'is distrusted, and the answer comes from counting the second pile',
      res['outcome'] == 'derived' and res['value'] == 1, str(res['outcome']))

# ---- 6. no scoring anywhere in the learner -------------------------
import io as _io
import os as _os
import re as _re
_src = _io.open(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                              'baby_auto.py'), encoding='utf-8').read()
_code = _re.sub(r'""".*?"""', '', _src, flags=_re.S)
_code = _re.sub(r'#.*', '', _code)
check('the learner never reads S at all: it decides on counts, not support',
      '.S' not in _code, [l.strip() for l in _code.split('\n') if '.S' in l])
check('and never reads an intensity', 'intensity' not in _code)
check('and never speculates: an autonomous loop that could guess would '
      'guess its way past every refusal', 'speculate' not in _code)

# =====================================================================
print('the autonomous learner, checked over every goal and every scene')
print()
failed = 0
for name, ok, detail in CHECKS:
    print(f'  {"ok  " if ok else "FAIL"}  {name}')
    if not ok:
        failed += 1
        if detail:
            print(f'          {detail}')
print()
if failed:
    print(f'{failed} of {len(CHECKS)} checks failed')
    raise SystemExit(1)
print(f'all {len(CHECKS)} checks pass')
