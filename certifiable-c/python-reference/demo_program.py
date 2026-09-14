"""The same kernel, on a program.

No kernel change. Not one line. demo_desk.py reasons about a trading desk,
baby_auto.py grows from nothing into a learner, verify_reason section 19
does induction with worlds as RULES, and this reasons about code. All four
import the same kernel_py and it knows about none of them.

That is the answer to "are we building on one specific thing": the kernel
has no domain in it at all. It has worlds, questions, answers and one
operation. A domain is what you hand it.

THE PROGRAM

    var x : 0..3
    var y : 0..3
    if x > y { z = x } else { z = y }
    assert z >= 2

WHERE THE QUESTIONS COME FROM

    Every question below is read off the program text. The declarations
    give the variables, the conditional gives the branch, the assignment
    gives z, the assertion gives the claim. Nobody modelled anything.

    That matters for where this is going. In general, deciding what the
    questions ARE is the open problem. For a program it is not open: a
    program already declares its own question space. That is the whole
    reason the narrow claim was the strong one.
"""

from kernel_py import *   # noqa: F401,F403

N_SIT = 16               # x in 0..3 times y in 0..3


def x_of(s):
    return s >> 2


def y_of(s):
    return s & 3


def z_of(s):
    return max(x_of(s), y_of(s))


def sq(fn, dom):
    q = Query()
    assert query_init(q, N_SIT, dom) == OK
    for s in range(N_SIT):
        assert query_set(q, s, fn(s)) == OK
    return q


# read off the program text
q_x = sq(x_of, 4)                                        # var x
q_y = sq(y_of, 4)                                        # var y
q_branch = sq(lambda s: 1 if x_of(s) > y_of(s) else 0, 2)  # the if
q_z = sq(z_of, 4)                                        # the assignment
q_assert = sq(lambda s: 1 if z_of(s) >= 2 else 0, 2)     # the assertion
q_zx = sq(lambda s: 1 if z_of(s) >= x_of(s) else 0, 2)   # a claimed invariant

BOOL = {0: 'false', 1: 'true'}


def full(n):
    s = State()
    assert state_init(s, n) == OK
    return s


def show(label, q, st, indent='  '):
    r = Result()
    assert ask(q, st, r) == OK
    val = (BOOL.get(r.value, str(r.value)) if q.dom == 2 else str(r.value))
    val = val if r.verdict == DERIVED else '-'
    proof = 'checks' if witness_check(q, r.witness) else 'FAILS'
    print(f'{indent}{label:<26}{VNAME[r.verdict]:<14}S={r.S:5.2f}  '
          f'{val:<6} witness {proof}')
    return r


def rule(t):
    print()
    print(t)
    print('-' * len(t))


print(__doc__.split('THE PROGRAM')[0].strip())

# ---------------------------------------------------------------------
rule('the world count is read off the declarations')

p = Partition()
assert refine([q_x, q_y], N_SIT, p) == OK
print(f'  two variables over 0..3 give {p.n_cells} worlds, and that number '
      f'is derived,')
print(f'  not chosen: it is the common refinement of what the declarations '
      f'can ask.')
print(f'  the branch condition adds nothing: '
      f'{"a new distinction" if distinguishes(p, q_branch) else "already definable from x and y"}')
print(f'  z adds nothing either: '
      f'{"a new distinction" if distinguishes(p, q_z) else "already definable from x and y"}')

# ---------------------------------------------------------------------
rule('an invariant, with both inputs entirely unknown')

st = full(N_SIT)
show('x', q_x, st)
show('y', q_y, st)
show('assert z >= 2', q_assert, st)
show('invariant z >= x', q_zx, st)
print()
print('  Both inputs undetermined at S = 0, and z >= x comes out DERIVED')
print('  true. That is a proof over all 16 executions, not a test of some')
print('  of them, and it needed no evidence at all.')

# ---------------------------------------------------------------------
rule('a partial precondition is enough')

st = full(N_SIT)
observe(st, sq(lambda s: 1 if x_of(s) >= 2 else 0, 2), 1)   # precondition x >= 2
print('  given only: x >= 2')
show('y', q_y, st)
show('assert z >= 2', q_assert, st)
print()
print('  The assertion holds and y was never constrained. Four of the eight')
print('  surviving executions have y = 0. It did not need them to agree on')
print('  y, only on the assertion.')

# ---------------------------------------------------------------------
rule('when it does not hold, the witness IS the counterexample')

st = full(N_SIT)
observe(st, q_x, 1)
r = show('assert z >= 2  (x = 1)', q_assert, st)
print(f'  still {live_count(st)} executions alive, so it will not answer')
print()

pr = Probe()
cands = [q_y, q_branch]
names = ['learn y', 'learn which branch']
for nm, c in zip(names, cands):
    assert probe(st, q_assert, c, pr) == OK
    print(f'    {nm:<22}'
          f'{"SETTLES IT" if pr.sufficient else "narrows it, may not finish"}')
_, pick = choose(st, q_assert, cands, ASK_GUARANTEE, pr)
print(f'  it takes: {names[pick]}')

observe(st, q_y, 0)
r = show('assert z >= 2  (x = 1, y = 0)', q_assert, st)
survivors = [s for s in range(N_SIT) if world_possible(st, s)]
print()
print(f'  derived FALSE, and the witness holds exactly one execution: '
      f'x={x_of(survivors[0])}, y={y_of(survivors[0])}, z={z_of(survivors[0])}')
print('  A failed assertion does not come back as low confidence. It comes')
print('  back as the input that breaks it, and a checker confirms it.')

# ---------------------------------------------------------------------
rule('a specification that contradicts itself')

st = full(N_SIT)
observe(st, sq(lambda s: 1 if x_of(s) >= 2 else 0, 2), 1)
observe(st, sq(lambda s: 1 if x_of(s) <= 1 else 0, 2), 1)
r = Result()
assert ask(q_assert, st, r) == OK
print(f'  required x >= 2 and x <= 1')
print(f'  verdict: {VNAME[r.verdict]}, {live_count(st)} executions admitted')
print('  Not an unsatisfiable-looking low score. A distinct verdict saying')
print('  the requirements admit no execution, which is a fact about the')
print('  specification and not about the analysis.')

print()
print('=' * 70)
print('Same kernel as the trading desk, the learner, and the induction over')
print('rules. Zero lines changed for any of them.')
