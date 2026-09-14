"""A worked example: a trade approval, run through the reasoning kernel.

Imports the kernel from kernel_py, which is the same code verify_reason.py
checks -- not a second copy that could drift from it.

Five unknowns, so thirty-two worlds:

    kyc    counterparty KYC is current
    sanc   counterparty appears on a sanctions list
    lim    trade is inside the position limit
    mkt    market is open
    col    collateral is posted

    approve = kyc and (not sanc) and lim and mkt and col

Nothing below tells the kernel what approve means beyond that formula. It
is not scored, ranked, or predicted. Every line of output is a count of
what survived.
"""

from kernel_py import *   # noqa: F401,F403

N = 32
KYC, SANC, LIM, MKT, COL = 4, 3, 2, 1, 0
BOOL = {0: 'false', 1: 'true'}

AND = Op2([0, 0, 0, 1], 2, 2, 2)
OR = Op2([0, 1, 1, 1], 2, 2, 2)
NOT = Op1([1, 0], 2, 2)


def mkq(n, answers, dom):
    q = Query()
    assert query_init(q, n, dom) == OK
    for w, a in enumerate(answers):
        assert query_set(q, w, a) == OK
    return q


def var(pos):
    return mkq(N, [(w >> pos) & 1 for w in range(N)], 2)


def full(n):
    s = State()
    assert state_init(s, n) == OK
    return s


def compose(op, a, b):
    out = Query()
    assert map2(op, a, b, out) == OK
    return out


def negate(a):
    out = Query()
    assert map1(NOT, a, out) == OK
    return out


kyc, sanc, lim, mkt, col = (var(KYC), var(SANC), var(LIM), var(MKT), var(COL))
approve = compose(AND,
                  compose(AND,
                          compose(AND, compose(AND, kyc, negate(sanc)), lim),
                          mkt),
                  col)


def show(label, q, s, indent='  '):
    r = Result()
    assert ask(q, s, r) == OK
    sv = ' n/a ' if r.S < 0 else f'{r.S:5.2f}'
    val = BOOL.get(r.value, '-') if r.verdict == DERIVED else '-'
    proof = 'checks' if witness_check(q, r.witness) else 'FAILS'
    print(f'{indent}{label:<26} {VNAME[r.verdict]:<13} S={sv}  '
          f'answer={val:<5}  witness {proof}')
    return r


def rule(title):
    print()
    print(title)
    print('-' * len(title))


# =====================================================================
rule('A. what is known before anything arrives')

s = full(N)
print(f'  {live_count(s)} worlds possible, nothing eliminated')
show('approve', approve, s)
for nm, q in (('kyc', kyc), ('sanctioned', sanc), ('within limit', lim),
              ('market open', mkt), ('collateral', col)):
    show(nm, q, s)

# =====================================================================
rule('B. context arrives, one piece at a time')

t = Settle()
assert settle_begin(t, s, approve) == OK
print('  target: approve')
print()
print(f'  {"context":<28}{"worlds gone":>12}{"productive":>12}'
      f'{"informative":>13}   answer set')
print('  ' + '-' * 76)


def feed(label, constraint, answer):
    before = t.rounds
    assert settle_step(t, s, approve, constraint, answer) == OK
    gone = t.removed[before]
    prod = 'yes' if gone else 'no'
    info = 'yes' if not t.settled else 'no'
    img = '{' + ', '.join(BOOL[i] for i in range(2)
                          if (t.image >> i) & 1) + '}'
    print(f'  {label:<28}{gone:>12}{prod:>12}{info:>13}   {img}')


feed('market is open', mkt, 1)
feed('counterparty IS sanctioned', sanc, 1)
feed('market is open (again)', mkt, 1)

print()
print(f'  rounds={t.rounds}  productive={t.productive}  '
      f'informative={t.informative}  settled={bool(t.settled)}  '
      f'live={t.live_now}')

# =====================================================================
rule('C. commit')

r = Result()
assert settle_commit(t, approve, s, r) == OK
print(f'  allowed={bool(r.allowed)}   verdict={VNAME[r.verdict]}   '
      f'answer={BOOL.get(r.value, "-")}')
print()
print('  and the four inputs it never learned:')
for nm, q in (('kyc', kyc), ('within limit', lim), ('collateral', col)):
    show(nm, q, s, indent='    ')
print()
print('  One fact decided it. Four of the five inputs are still entirely')
print('  unknown, S = 0 on each, and the answer is derived anyway. Nothing')
print('  had to be assumed about them, because the operator table holds one')
print('  value across that whole row.')

# =====================================================================
rule('D. the same desk, with nothing disqualifying')

s2 = full(N)
t2 = Settle()
assert settle_begin(t2, s2, approve) == OK
for label, q, a in (('kyc is current', kyc, 1),
                    ('not sanctioned', sanc, 0),
                    ('market is open', mkt, 1)):
    assert settle_step(t2, s2, approve, q, a) == OK
    print(f'  applied: {label}')
print()
show('approve', approve, s2)
r2 = Result()
assert settle_commit(t2, approve, s2, r2) == OK
print(f'  commit allowed={bool(r2.allowed)}   value returned='
      f'{r2.value}  (0 is "no value", not "false")')
print()
print('  Three facts in, and it will not answer. Limit and collateral are')
print('  unknown, so the worlds disagree, so there is nothing to pass on.')

# =====================================================================
rule('E. asked to guess anyway')

for tau in (0.50, 0.25, 0.00):
    probe = full(N)
    for q, a in ((kyc, 1), (sanc, 0), (mkt, 1)):
        assert observe(probe, q, a) == OK
    anc = Ancestry()
    rr = Result()
    cp = checkpoint(probe)
    assert speculate(probe, approve, tau, 20260905, INTENSITY_SUPPORT,
                     3, anc, rr) == OK
    if rr.allowed:
        print(f'  tau={tau:.2f}  GUESSED {BOOL[rr.value]:<5} '
              f'debt={anc.bits():.1f} bit   witness still says '
              f'{VNAME[rr.witness.verdict]}')
        assert retract(probe, cp, anc, 3) == OK
        back = Result()
        assert ask(approve, probe, back) == OK
        print(f'           retracted -> {VNAME[back.verdict]}, '
              f'debt={anc.bits():.1f}')
    else:
        print(f'  tau={tau:.2f}  refused   verdict={VNAME[rr.verdict]}  '
              f'S={rr.S:.2f} < tau, worlds untouched ({live_count(probe)})')

print()
print('  S is 0 here because nothing about approve has been ruled out. So')
print('  any bar above zero refuses. At tau=0 it will guess, and the guess')
print('  is uniform, carries a recorded debt, and its own witness declines')
print('  to back it.')

# =====================================================================
rule('F. structure alone, where a score cannot follow')

s3 = full(N)
show('within limit', lim, s3)
show('collateral', col, s3)
print('  both operands: undetermined, S = 0.00, answer set {false, true}')
print()
show('limit or not limit', compose(OR, lim, negate(lim)), s3)
show('limit and not limit', compose(AND, lim, negate(lim)), s3)
show('limit or collateral', compose(OR, lim, col), s3)
print()
print('  Three composites. Identical operand answer sets and identical')
print('  operand supports. Derived true, derived false, and undetermined.')
print('  No function of the operands returns three values for one input,')
print('  so nothing that combines per-claim confidences reproduces this')
print('  row. The kernel gets it by composing the questions and asking')
print('  once.')

# =====================================================================
rule('G. context that conflicts')

s4 = full(N)
t4 = Settle()
assert settle_begin(t4, s4, approve) == OK
assert settle_step(t4, s4, approve, sanc, 1) == OK
print('  applied: counterparty IS sanctioned')
assert settle_step(t4, s4, approve, sanc, 0) == OK
print('  applied: counterparty is NOT sanctioned')
r4 = Result()
assert settle_commit(t4, approve, s4, r4) == OK
print()
print(f'  verdict={VNAME[r4.verdict]}  live={t4.live_now}  '
      f'contradicted={bool(t4.contradicted)}  commit allowed='
      f'{bool(r4.allowed)}')
print()
print('  Not low confidence, and not a tie to be broken. A fourth verdict:')
print('  the constraints admit no world at all. The incoming claim was')
print('  checked against what was already held instead of overwriting it.')

print()
print('=' * 78)
print('Every answer above came from counting the worlds that survived.')
print('No score was combined with another score at any point.')
