"""BABY -> RESEARCHER.

A program that starts unable to tell any two situations apart, and grows
by acquiring questions. Nothing here is scripted as "now it understands
arithmetic": every capacity has to EARN its place by refining the
partition, and a candidate that refines nothing is rejected as a
restatement however sophisticated it looks.

Runs on kernel_py, which is the code verify_reason.py checks.

THE WORLD IT GROWS UP IN
    Two piles and a label. 32 situations:
        a     items in the first pile   0..3
        b     items in the second pile  0..3
        name  the thing is called a cup, or a mug

CAPACITIES IT MIGHT ACQUIRE, each a question over those situations
    more      which pile is bigger, or neither      relational
    close     are they within one of each other      relational
    name      what it is called                      a label
    sum       how many altogether                    arithmetic
    exact_a   how many in the first pile             arithmetic
    exact_b   how many in the second                 arithmetic

WHAT GROWTH MEANS HERE
    A capacity is real iff it splits worlds the child could not previously
    tell apart. That is decidable before it is built.

    A question it cannot express is GROUNDLESS to it -- not unknown, not
    unlikely: there is no question there yet. A question it can express
    but the evidence does not settle is UNDETERMINED. The child says so
    and stops, at every stage. It never guesses to fill the silence.
"""

from kernel_py import *   # noqa: F401,F403

N_SIT = 32
TRUE_SITUATION = (3 << 3) | (1 << 1) | 1      # a=3, b=1, called a cup


def a_of(s):
    return (s >> 3) & 3


def b_of(s):
    return (s >> 1) & 3


def name_of(s):
    return s & 1


def sit_q(fn, dom):
    q = Query()
    assert query_init(q, N_SIT, dom) == OK
    for s in range(N_SIT):
        assert query_set(q, s, fn(s)) == OK
    return q


# ---- capacities -----------------------------------------------------
CAP = {
    'more':    sit_q(lambda s: 0 if a_of(s) < b_of(s)
                     else (1 if a_of(s) == b_of(s) else 2), 3),
    'close':   sit_q(lambda s: 1 if abs(a_of(s) - b_of(s)) <= 1 else 0, 2),
    'name':    sit_q(lambda s: name_of(s), 2),
    'sum':     sit_q(lambda s: a_of(s) + b_of(s), 7),
    'exact_a': sit_q(lambda s: a_of(s), 4),
    'exact_b': sit_q(lambda s: b_of(s), 4),
    # offered on purpose and expected to be rejected: it is "more" said
    # again, and saying a thing again is not learning it
    'at_least': sit_q(lambda s: 1 if a_of(s) >= b_of(s) else 0, 2),
}

# ---- the battery it is examined on at every stage -------------------
BATTERY = [
    ('is a bigger than b',   sit_q(lambda s: 1 if a_of(s) > b_of(s) else 0, 2)),
    ('how many altogether',  sit_q(lambda s: a_of(s) + b_of(s), 7)),
    ('is the total four',    sit_q(lambda s: 1 if a_of(s) + b_of(s) == 4
                                   else 0, 2)),
    ('is it called a cup',   sit_q(lambda s: name_of(s), 2)),
    ('is a at least two',    sit_q(lambda s: 1 if a_of(s) >= 2 else 0, 2)),
    ('is the total even',    sit_q(lambda s: 1 if (a_of(s) + b_of(s)) % 2 == 0
                                   else 0, 2)),
]

CURRICULUM = [
    ('newborn',    []),
    ('comparison', ['more']),
    ('nearness',   ['more', 'close']),
    ('naming',     ['more', 'close', 'name']),
    ('counting',   ['more', 'close', 'name', 'sum', 'exact_a', 'exact_b']),
]

# What a glance gives you. The arithmetic capacities extend what can be
# ASKED and what can be DERIVED from a glance; they do not hand over their
# own answers. Having the capacity to count is not the same as having
# counted, and collapsing those two is what made the first version of this
# program answer everything the moment it could phrase it.
IMMEDIATE = ('more', 'close', 'name')

VERDICT_WORD = {DERIVED: 'derived', UNDETERMINED: 'cannot say',
                CONTRADICTION: 'contradiction', GROUNDLESS: 'no such question'}


def partition_for(names):
    p = Partition()
    assert refine([CAP[n] for n in names], N_SIT, p) == OK
    return p


def examine(names, verbose=False):
    """Give the child its capacities, let it perceive the true situation
    through them, then put the battery to it."""
    p = partition_for(names)
    st = State()
    assert state_init(st, p.n_cells) == OK

    # perception: a glance, through the immediate capacities it has
    for n in names:
        if n not in IMMEDIATE:
            continue
        pq = Query()
        assert project(p, CAP[n], pq) == OK
        assert observe(st, pq, CAP[n].ans[TRUE_SITUATION]) == OK

    rows = []
    derived = expressible = 0
    unheld = 0.0
    for label, target in BATTERY:
        tq = Query()
        if project(p, target, tq) != OK:
            # It cannot even form the question. Not unknown: absent.
            rows.append((label, GROUNDLESS, '-', 0.0))
            continue
        expressible += 1
        r = Result()
        assert ask(tq, st, r) == OK
        val = str(r.value) if r.verdict == DERIVED else '-'
        if r.verdict == DERIVED:
            derived += 1
        unheld += r.H
        rows.append((label, r.verdict, val, r.H))
    return p, st, rows, derived, expressible, unheld


print(__doc__.split('THE WORLD')[0].strip())
print()
print(f'the true situation: a=3, b=1, called a cup  '
      f'(situation {TRUE_SITUATION} of {N_SIT})')
print()
print(f'{"stage":<20}{"worlds":>8}{"can ask":>9}{"derived":>9}'
      f'{"bits unheld":>13}')
print('-' * 60)

history = []
for stage, names in CURRICULUM:
    p, st, rows, derived, expressible, unheld = examine(names)
    history.append((stage, names, p, rows, derived, expressible, unheld))
    print(f'{stage:<20}{p.n_cells:>8}{expressible:>9}/{len(BATTERY)}'
          f'{derived:>8}/{len(BATTERY)}{unheld:>13.2f}')

print()
print('It starts unable to tell any two situations apart, so it can form no')
print('question at all, and it says so rather than producing an answer.')

# =====================================================================
print()
print('EARNING A CAPACITY')
print('-' * 60)

p_cmp = partition_for(['more'])
print(f'  after comparison, it can tell {p_cmp.n_cells} kinds of situation apart')
for cand in ('at_least', 'close', 'exact_a'):
    d = distinguishes(p_cmp, CAP[cand])
    if d:
        after = partition_for(['more', cand])
        print(f'  {cand:<10} ACQUIRE  splits {p_cmp.n_cells} worlds into '
              f'{after.n_cells}')
    else:
        print(f'  {cand:<10} reject   splits nothing; it is what it already '
              f'knows, said again')

print()
print('  The rejection is not a judgement about sophistication. "at_least"')
print('  is a perfectly good question. It is simply already definable, so')
print('  acquiring it would add vocabulary and no capacity.')

# =====================================================================
print()
print('WHAT THE ORDER OF THE LADDER IS, AND IS NOT')
print('-' * 60)

base = partition_for([])
best = max(CAP.items(), key=lambda kv: partition_for([kv[0]]).n_cells)
print(f'  most refining single capacity: {best[0]} '
      f'({partition_for([best[0]]).n_cells} worlds in one step)')
print(f'  the one it actually takes first: more '
      f'({p_cmp.n_cells} worlds)')
print()
print('  So the ladder is NOT ordered by how much a step teaches. Greedy')
print('  would skip straight to exact counting. What orders it is what can')
print('  be FORMED yet: you cannot ask how many before you can tell apart.')
print('  That is a constructibility order, and it is not derivable from')
print('  information alone. Saying otherwise would be dressing up a')
print('  developmental fact as a theorem.')

# =====================================================================
print()
print('ARITHMETIC DOES NOT EXTEND STRUCTURE, IT SUBSUMES IT')
print('-' * 60)

p_struct = partition_for(['more', 'close'])
p_exact = partition_for(['exact_a', 'exact_b'])
print(f'  structure alone      {p_struct.n_cells:>3} worlds')
print(f'  exact counts alone   {p_exact.n_cells:>3} worlds')
print(f'  can structure be recovered from counts?  '
      f'{"yes" if project(p_exact, CAP["more"], Query()) == OK else "no"}')
print(f'  can counts be recovered from structure?  '
      f'{"yes" if project(p_struct, CAP["exact_a"], Query()) == OK else "no"}')
print()
print('  Once it can say "three", "more" was always derivable and stops')
print('  being a separate thing it knows. The earlier stage is not thrown')
print('  away, it becomes shorthand. That is checked, not asserted: the')
print('  projection succeeds one way and refuses the other.')

# =====================================================================
print()
print('THE EXAMINATION, STAGE BY STAGE')
print('-' * 60)
for stage, names, p, rows, derived, expressible, unheld in history:
    print()
    print(f'  {stage}  ({p.n_cells} worlds, knows: '
          f'{", ".join(names) if names else "nothing"})')
    for label, verdict, val, h in rows:
        extra = f'  = {val}' if verdict == DERIVED else ''
        bits = f'   {h:.2f} bits unheld' if h > 0 else ''
        print(f'    {label:<24}{VERDICT_WORD[verdict]:<18}{extra}{bits}')

# =====================================================================
print()
print('THE RESEARCHER: AN ANSWER THAT CARRIES ITS OWN PROOF')
print('-' * 60)

p, st, _, _, _, _ = examine(CURRICULUM[-1][1])
claim = BATTERY[2][1]                      # is the total four
cq = Query()
assert project(p, claim, cq) == OK

r = Result()
assert ask(cq, st, r) == OK
print(f'  claim: the total is four')
print(f'  from a glance alone: {VERDICT_WORD[r.verdict]}, '
      f'{live_count(st)} worlds still standing, {r.H:.2f} bits it does '
      f'not hold')
print()
print('  It can phrase the question now and it still will not answer it.')
print('  So it works out what to measure. Not by ranking the measurements')
print('  as interesting: by counting what each one would leave standing.')
print()

MEASURES = [('count the first pile', 'exact_a'),
            ('count the second pile', 'exact_b'),
            ('count them together', 'sum')]
projected = []
for label, key in MEASURES:
    mq = Query()
    assert project(p, CAP[key], mq) == OK
    projected.append(mq)
    pb = Probe()
    assert probe(st, cq, mq, pb) == OK
    verdict = ('SETTLES IT whichever way it comes back' if pb.sufficient
               else ('cannot help on its own' if pb.irrelevant
                     else 'narrows it, but may not finish'))
    print(f'    {label:<24}{verdict}')

pr = Probe()
_, pick = choose(st, cq, projected, ASK_GUARANTEE, pr)
print()
print(f'  it chooses: {MEASURES[pick][0]}')

key = MEASURES[pick][1]
assert observe(st, projected[pick], CAP[key].ans[TRUE_SITUATION]) == OK
r = Result()
assert ask(cq, st, r) == OK
print(f'  measures, and the total is four: {VERDICT_WORD[r.verdict]}, '
      f'value {r.value}, S = {r.S:.3f}')
print(f'  worlds still standing: {live_count(st)} of {p.n_cells}')
print(f'  a checker sharing none of the engine re-derives it from the '
      f'witness alone: {"yes" if witness_check(cq, r.witness) else "NO"}')

print()
print('  At the first stage it could not form this question. In the middle')
print('  it could phrase it and refused to answer. At the end it worked out')
print('  which single measurement would settle it, took that one, answered,')
print('  and handed over a proof a stranger can check.')
print()
print('  Nothing in between was a confidence rising. Every step was worlds')
print('  being ruled out, and the answer arrived when the survivors agreed.')
