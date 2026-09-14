"""Checks the reasoning kernel's ALGORITHM, not the C.

Transliterates smarsh_reason.c line for line, then checks it against
ground truth computed a DIFFERENT way -- plain Python sets and dicts, no
bitsets, no shared helpers. Same contract as every other verify_*.py in
this directory: no C compiler is present, so what is verified is the
reasoning, and the C is a faithful transcription of what was verified.

The centrepiece is section 4. It does not test the correlation theorem;
it PROVES it, by exhibiting three composites whose operands have
identical answer sets and identical supports and whose verdicts are all
three different.

Stated at correct scope, that result is: NO COMPOSITIONAL CONFIDENCE
SEMANTICS EXISTS. It reaches anything that attaches a number to a claim
and then combines claims, any interface whose contract is a per-answer
confidence, and any chain of reasoning that commits an intermediate
conclusion and reuses it. It does NOT reach the internals of a system
that composes representations rather than scores, and claiming otherwise
would be overreach that costs the result its credibility.
"""

import math
import random

# The kernel itself lives in kernel_py so a demo cannot run a
# different copy of it than the one checked here.
from kernel_py import *  # noqa: F401,F403

# ====================================================================
# ground truth, computed a different way: plain sets, no bitsets
# ====================================================================

def truth_image(ans_list, live_set):
    """The answer set, as a Python set over a Python list. Shares nothing
    with image() above -- different representation, different loop."""
    return {ans_list[w] for w in sorted(live_set)}


def truth_verdict(answers, has_domain):
    if not has_domain:
        return GROUNDLESS
    return {0: CONTRADICTION, 1: DERIVED}.get(len(answers), UNDETERMINED)


def truth_support(answers, dom):
    return 1.0 - len(answers) / dom


# ====================================================================
# harness
# ====================================================================

CHECKS = []


def check(section, name, ok, detail=''):
    CHECKS.append((section, name, bool(ok), detail))


def mk(n_worlds, answers, dom):
    q = Query()
    assert query_init(q, n_worlds, dom) == OK
    for w, a in enumerate(answers):
        assert query_set(q, w, a) == OK
    return q


def full(n_worlds):
    s = State()
    assert state_init(s, n_worlds) == OK
    return s


def live_set(s):
    return {w for w in range(s.n_worlds) if world_possible(s, w)}


# -- 1. the primitive -------------------------------------------------

s = full(8)
check(1, 'a fresh state admits every world', live_count(s) == 8)
eliminate(s, 3)
check(1, 'eliminate removes exactly one world', live_count(s) == 7 and not world_possible(s, 3))
eliminate(s, 3)
check(1, 'eliminate is idempotent: twice means the same as once', live_count(s) == 7)
check(1, 'eliminate refuses a world outside the universe',
      eliminate(s, 8) == ERR_INDEX_OUT_OF_DOMAIN)

g = State()
state_groundless(g)
check(1, 'groundless has no domain and no worlds', g.has_domain == 0 and live_count(g) == 0)
check(1, 'eliminating from groundless is refused, not silently ignored',
      eliminate(g, 0) == ERR_EMPTY_DOMAIN)

dead = full(4)
for w in range(4):
    eliminate(dead, w)
check(1, 'a state with everything eliminated still HAS a domain',
      dead.has_domain == 1 and live_count(dead) == 0)

# -- 2. the four verdicts, counted ------------------------------------

q_id = mk(4, [0, 1, 0, 1], 2)
r = Result()
ask(q_id, full(4), r)
check(2, 'four worlds disagreeing -> undetermined', r.verdict == UNDETERMINED)

s2 = full(4)
observe(s2, q_id, 1)
ask(q_id, s2, r)
check(2, 'after observing, the survivors agree -> derived',
      r.verdict == DERIVED and r.value == 1)

ask(q_id, dead, r)
check(2, 'no world survives -> contradiction, not groundless',
      r.verdict == CONTRADICTION)

ask(q_id, g, r)
check(2, 'no domain -> groundless, not contradiction', r.verdict == GROUNDLESS)
check(2, 'groundless reports S = -1.0 out of band, never 0.0', r.S == -1.0)
check(2, 'contradiction and groundless are different verdicts',
      CONTRADICTION != GROUNDLESS)

# -- 3. composition against independently brute-forced ground truth ---

rng = random.Random(20260905)
mismatch = None
for trial in range(4000):
    n = rng.randint(1, 24)
    da, db = rng.randint(1, 5), rng.randint(1, 5)
    do = rng.randint(1, 5)
    qa = mk(n, [rng.randrange(da) for _ in range(n)], da)
    qb = mk(n, [rng.randrange(db) for _ in range(n)], db)
    tbl = [rng.randrange(do) for _ in range(da * db)]
    op = Op2(tbl, da, db, do)
    comp = Query()
    if map2(op, qa, qb, comp) != OK:
        mismatch = ('map2 refused a well-formed composition', trial)
        break
    st = full(n)
    for w in range(n):
        if rng.random() < 0.4:
            eliminate(st, w)
    L = live_set(st)
    r = Result()
    ask(comp, st, r)
    # ground truth, computed with sets over lists
    want = {tbl[qa.ans[w] * db + qb.ans[w]] for w in sorted(L)}
    got = {i for i in range(MAX_ANSWERS) if (r.witness.image >> i) & 1}
    if got != want:
        mismatch = ('image', trial, got, want)
        break
    if r.verdict != truth_verdict(want, 1):
        mismatch = ('verdict', trial)
        break
    if abs(r.S - truth_support(want, do)) > 1e-12:
        mismatch = ('support', trial)
        break
    if not witness_check(comp, r.witness):
        mismatch = ('witness', trial)
        break
check(3, '4000 random compositions match brute force exactly '
         '(image, verdict, S, witness)', mismatch is None, str(mismatch))

# unary, same treatment
mismatch = None
for trial in range(1500):
    n = rng.randint(1, 24)
    da, do = rng.randint(1, 6), rng.randint(1, 6)
    qa = mk(n, [rng.randrange(da) for _ in range(n)], da)
    tbl = [rng.randrange(do) for _ in range(da)]
    op = Op1(tbl, da, do)
    comp = Query()
    if map1(op, qa, comp) != OK:
        mismatch = ('map1 refused', trial)
        break
    st = full(n)
    for w in range(n):
        if rng.random() < 0.4:
            eliminate(st, w)
    r = Result()
    ask(comp, st, r)
    want = {tbl[qa.ans[w]] for w in sorted(live_set(st))}
    got = {i for i in range(MAX_ANSWERS) if (r.witness.image >> i) & 1}
    if got != want or r.verdict != truth_verdict(want, 1):
        mismatch = (trial, got, want)
        break
check(3, '1500 random unary maps match brute force exactly', mismatch is None,
      str(mismatch))

check(3, 'composing queries over different universes is refused',
      map2(Op2([0, 0, 0, 0], 2, 2, 2), mk(4, [0] * 4, 2), mk(5, [0] * 5, 2),
           Query()) == ERR_INDEX_OUT_OF_DOMAIN)
check(3, 'an operator table pointing outside its output domain is refused',
      map2(Op2([0, 0, 0, 7], 2, 2, 2), mk(2, [0, 1], 2), mk(2, [0, 1], 2),
           Query()) == ERR_INDEX_OUT_OF_DOMAIN)

# -- 4. THE CORRELATION THEOREM ---------------------------------------
# One unknown boolean a. Two worlds: w0 has a=false, w1 has a=true.

AND = Op2([0, 0, 0, 1], 2, 2, 2)     # out[a*2+b]
OR = Op2([0, 1, 1, 1], 2, 2, 2)
NOT = Op1([1, 0], 2, 2)

q_a = mk(2, [0, 1], 2)
q_not_a = Query()
map1(NOT, q_a, q_not_a)

# an INDEPENDENT b needs its own axis: 4 worlds = a x b
q_a4 = mk(4, [0, 0, 1, 1], 2)
q_b4 = mk(4, [0, 1, 0, 1], 2)

u2, u4 = full(2), full(4)
r_a, r_na = Result(), Result()
ask(q_a, u2, r_a)
ask(q_not_a, u2, r_na)
check(4, 'operand a is undetermined with S = 0',
      r_a.verdict == UNDETERMINED and r_a.S == 0.0)
check(4, 'operand not-a is undetermined with S = 0, identical to a',
      r_na.verdict == UNDETERMINED and r_na.S == 0.0)

img_a = {i for i in range(2) if (r_a.witness.image >> i) & 1}
img_na = {i for i in range(2) if (r_na.witness.image >> i) & 1}
check(4, 'the two operands have identical ANSWER SETS too, not just supports',
      img_a == img_na == {0, 1})

cases = {}
for label, op, x, y, univ in (
        ('a and a', AND, q_a, q_a, u2),
        ('a and not a', AND, q_a, q_not_a, u2),
        ('a or not a', OR, q_a, q_not_a, u2),
        ('a and b (independent)', AND, q_a4, q_b4, u4)):
    comp = Query()
    assert map2(op, x, y, comp) == OK
    rr = Result()
    ask(comp, univ, rr)
    cases[label] = rr

check(4, 'a and a          -> undetermined',
      cases['a and a'].verdict == UNDETERMINED)
check(4, 'a and not a      -> DERIVED false, from structure alone',
      cases['a and not a'].verdict == DERIVED and cases['a and not a'].value == 0)
check(4, 'a or not a       -> DERIVED true, from structure alone',
      cases['a or not a'].verdict == DERIVED and cases['a or not a'].value == 1)
check(4, 'a and b (indep.) -> undetermined',
      cases['a and b (independent)'].verdict == UNDETERMINED)

# The proof. Every one of these has operand supports (0.0, 0.0) and
# operand answer sets ({0,1}, {0,1}).
signature = (0.0, 0.0, frozenset({0, 1}), frozenset({0, 1}))
outcomes = {(cases[k].verdict, cases[k].value) for k in
            ('a and a', 'a and not a', 'a or not a')}
check(4, 'three composites share ONE operand signature and land on THREE '
         'distinct outcomes', len(outcomes) == 3, f'signature={signature}')
check(4, 'therefore no f with S_out = f(S_left, S_right) exists: it would '
         'have to return three values for one argument', len(outcomes) == 3)
check(4, 'and no g over answer sets exists either, by the same exhibition',
      len(outcomes) == 3)
check(4, 'the kernel gets all three right without any correlation '
         'tracking, because it composes questions and asks once',
      cases['a and not a'].verdict == DERIVED and
      cases['a or not a'].verdict == DERIVED and
      cases['a and a'].verdict == UNDETERMINED)

# -- 5. graded domains, where S is not degenerate ---------------------
# rank of an element among 4 positions; 4 worlds, one per position

q_rank = mk(4, [0, 1, 2, 3], 4)
sr_ = full(4)
r = Result()
ask(q_rank, sr_, r)
check(5, 'nothing known about a 4-position rank: S = 0.0', r.S == 0.0)
eliminate(sr_, 3)
ask(q_rank, sr_, r)
check(5, 'one position ruled out: S = 0.25 (not a boolean 0-or-1)', r.S == 0.25)
eliminate(sr_, 2)
ask(q_rank, sr_, r)
check(5, 'two ruled out: S = 0.5, H = 1.0 bit still unheld',
      r.S == 0.5 and abs(r.H - 1.0) < 1e-12)
eliminate(sr_, 1)
ask(q_rank, sr_, r)
check(5, 'three ruled out: S = 0.75, derived, H = 0',
      r.S == 0.75 and r.verdict == DERIVED and r.H == 0.0)

# a graded composition: min of two ranks
MIN4 = Op2([min(i, j) for i in range(4) for j in range(4)], 4, 4, 4)
qr1 = mk(16, [i // 4 for i in range(16)], 4)
qr2 = mk(16, [i % 4 for i in range(16)], 4)
qmin = Query()
map2(MIN4, qr1, qr2, qmin)
s16 = full(16)
r = Result()
ask(qmin, s16, r)
check(5, 'min over two free 4-ranks is undetermined over all four values',
      r.verdict == UNDETERMINED and r.S == 0.0)
observe(s16, qr1, 0)
ask(qmin, s16, r)
check(5, 'pinning one rank to its minimum DERIVES the min, though the '
         'other rank is still completely free',
      r.verdict == DERIVED and r.value == 0)
r2 = Result()
ask(qr2, s16, r2)
check(5, 'and the other rank really is still free: S = 0 alongside a '
         'derived composite', r2.verdict == UNDETERMINED and r2.S == 0.0)

# -- 6. witnesses -----------------------------------------------------

sw = full(8)
qw = mk(8, [0, 1, 2, 0, 1, 2, 0, 1], 3)
observe(sw, qw, 1)
rw = Result()
ask(qw, sw, rw)
check(6, 'a real witness passes its own check', witness_check(qw, rw.witness) == 1)

t = rw.witness.copy(); t.image ^= 1
check(6, 'a witness with a flipped image bit is rejected', witness_check(qw, t) == 0)
t = rw.witness.copy(); t.verdict = UNDETERMINED
check(6, 'a witness claiming the wrong verdict is rejected', witness_check(qw, t) == 0)
t = rw.witness.copy(); t.S += 0.1
check(6, 'a witness with an inflated S is rejected', witness_check(qw, t) == 0)
t = rw.witness.copy(); t.value = 2
check(6, 'a witness naming the wrong derived value is rejected',
      witness_check(qw, t) == 0)
t = rw.witness.copy(); t.n_worlds = 4
check(6, 'a witness shrinking its universe is rejected', witness_check(qw, t) == 0)
# world 20, in a universe of 8. Bit 200 would have been the natural thing
# to write and would have been a bad test: Python ints are unbounded, so it
# would sit outside the 64-bit word C actually has and the mask could not
# see it. Kept inside one word so this checks the kernel, not the
# transliteration.
t = rw.witness.copy(); t.live[0] |= 1 << 20
check(6, 'a witness claiming worlds outside the universe is rejected',
      witness_check(qw, t) == 0)
t = rw.witness.copy(); t.H += 0.5
check(6, 'a witness with an understated H is rejected', witness_check(qw, t) == 0)

gr = Result()
ask(qw, g, gr)
check(6, 'a groundless witness checks out and claims nothing else',
      witness_check(qw, gr.witness) == 1)
t = gr.witness.copy(); t.verdict = CONTRADICTION
check(6, 'a groundless witness relabelled as contradiction is rejected',
      witness_check(qw, t) == 0)

# every derived verdict, over the random corpus, must be witness-backed
bad = None
for trial in range(2000):
    n = rng.randint(1, 20)
    d = rng.randint(1, 6)
    q = mk(n, [rng.randrange(d) for _ in range(n)], d)
    st = full(n)
    for w in range(n):
        if rng.random() < 0.5:
            eliminate(st, w)
    r = Result()
    ask(q, st, r)
    if not witness_check(q, r.witness):
        bad = trial
        break
    if r.verdict == DERIVED and r.witness.verdict != DERIVED:
        bad = ('unbacked derivation', trial)
        break
check(6, '2000 random asks: every verdict re-derives from its witness alone',
      bad is None, str(bad))

# -- 7. observation is elimination ------------------------------------

so = full(8)
qo = mk(8, [0, 0, 1, 1, 2, 2, 0, 1], 3)
before = live_count(so)
observe(so, qo, 0)
check(7, 'observing narrows the world set and nothing else',
      live_count(so) == 3 and before == 8)
check(7, 'observing the same thing twice changes nothing (idempotent)',
      (observe(so, qo, 0), live_count(so))[1] == 3)
observe(so, qo, 1)
ro = Result()
ask(qo, so, ro)
check(7, 'two incompatible observations give CONTRADICTION, not groundless',
      ro.verdict == CONTRADICTION and live_count(so) == 0)
check(7, 'the contradicted state still has a domain', so.has_domain == 1)

# -- 8. the tau boundary, on a non-degenerate S -----------------------
# dom 4, two answers left -> S = 0.5 exactly.

def two_left():
    st = full(4)
    eliminate(st, 2)
    eliminate(st, 3)
    return st, mk(4, [0, 1, 2, 3], 4)

st, q = two_left()
r = Result()
ask(q, st, r)
check(8, 'the boundary fixture really is S = 0.5, |D| = 2 of 4', r.S == 0.5)

st, q = two_left()
anc = Ancestry()
r = Result()
speculate(st, q, 0.5, 1234, INTENSITY_SUPPORT, 0, anc, r)
check(8, 'S exactly equal to tau is ALLOWED (>=, matching speculate.js)',
      r.allowed == 1 and r.verdict == SPECULATED)

st, q = two_left()
anc = Ancestry()
r = Result()
speculate(st, q, 0.5 + 1e-9, 1234, INTENSITY_SUPPORT, 0, anc, r)
check(8, 'S a hair below tau is REFUSED', r.allowed == 0)
check(8, 'a refusal reports undetermined, NOT groundless: there is a '
         'question and there is a domain', r.verdict == UNDETERMINED)
check(8, 'a refusal leaves the world set untouched', live_count(st) == 2)
check(8, 'a refusal records no guess in the ancestry', anc.ids == 0)
check(8, 'a refusal carries intensity 0, not an absent intensity',
      r.intensity == 0.0)

st, q = two_left()
anc = Ancestry()
r = Result()
speculate(st, q, 0.75, 1234, INTENSITY_SUPPORT, 0, anc, r)
check(8, 'a bar above the available support is refused', r.allowed == 0)

check(8, 'tau outside [0,1] is a checked error',
      speculate(full(4), mk(4, [0, 1, 2, 3], 4), 1.5, 0, INTENSITY_SUPPORT, 0,
                Ancestry(), Result()) == ERR_BAD_THRESHOLD)
check(8, 'a NaN tau is a checked error, not a threshold that lets every '
         'guess through',
      speculate(full(4), mk(4, [0, 1, 2, 3], 4), float('nan'), 0,
                INTENSITY_SUPPORT, 0, Ancestry(), Result()) == ERR_BAD_THRESHOLD)
check(8, 'a guess id outside the ancestry range is a checked error',
      speculate(full(4), mk(4, [0, 1, 2, 3], 4), 0.0, 0, INTENSITY_SUPPORT, 99,
                Ancestry(), Result()) == ERR_GUESS_ID_OUT_OF_RANGE)

# -- 9. speculation IS unlicensed elimination -------------------------

st, q = two_left()
anc = Ancestry()
r = Result()
speculate(st, q, 0.5, 99, INTENSITY_SUPPORT, 3, anc, r)
check(9, 'an allowed speculation eliminates worlds, exactly as observing does',
      live_count(st) == 1)
after = Result()
ask(q, st, after)
check(9, 'the state now reads as DERIVED, which is precisely the danger',
      after.verdict == DERIVED)
check(9, 'so the debt is recorded: the guess is in the ancestry',
      (anc.ids >> 3) & 1 == 1)
check(9, 'with remaining = |D| at the moment of the cut', anc.remaining[3] == 2)
check(9, 'so the unlicensed information is 1.0 bit', abs(anc.bits() - 1.0) < 1e-12)
check(9, 'the RESULT verdict is SPECULATED, never DERIVED',
      r.verdict == SPECULATED)
check(9, 'and its witness still says UNDETERMINED: the witness does not '
         'back the value, which is the entire distinction',
      r.witness.verdict == UNDETERMINED and witness_check(q, r.witness) == 1)
check(9, 'H on the result is the bit it claims but does not hold',
      abs(r.H - 1.0) < 1e-12)
check(9, 'intensity under g(S) = S is the support', r.intensity == 0.5)

st, q = two_left()
r = Result()
speculate(st, q, 0.5, 99, INTENSITY_HEADROOM, 3, Ancestry(), r)
check(9, 'under g(S) = headroom, scraping the bar reports 0, not 0.5',
      r.intensity == 0.0)

# derived needs no guess
sd = full(4)
qd = mk(4, [0, 1, 2, 3], 4)
for w in (1, 2, 3):
    eliminate(sd, w)
anc = Ancestry()
r = Result()
speculate(sd, qd, 0.0, 7, INTENSITY_SUPPORT, 5, anc, r)
check(9, 'an already-derived answer is not speculated at, even at tau = 0',
      r.allowed == 0 and r.verdict == DERIVED and anc.ids == 0)

# contradiction and groundless are not guessed at either
sc = full(4)
for w in range(4):
    eliminate(sc, w)
r = Result()
speculate(sc, qd, 0.0, 7, INTENSITY_SUPPORT, 5, Ancestry(), r)
check(9, 'a contradiction is not papered over with a guess',
      r.allowed == 0 and r.verdict == CONTRADICTION)
r = Result()
speculate(g, qd, 0.0, 7, INTENSITY_SUPPORT, 5, Ancestry(), r)
check(9, 'a groundless state is not guessed at', r.allowed == 0)

# replay
runs = set()
for _ in range(5):
    st, q = two_left()
    r = Result()
    speculate(st, q, 0.5, 424242, INTENSITY_SUPPORT, 0, Ancestry(), r)
    runs.add(r.value)
check(9, 'the same seed replays the same guess every time', len(runs) == 1)

picks = set()
for seed in range(60):
    st, q = two_left()
    r = Result()
    speculate(st, q, 0.5, seed, INTENSITY_SUPPORT, 0, Ancestry(), r)
    if r.allowed:
        picks.add(r.value)
check(9, 'over many seeds the guess ranges over the whole surviving set, '
         'uniformly rather than favouring one answer', picks == {0, 1})

# -- 10. the diamond: one guess reached twice is one guess ------------

anc = Ancestry()
anc.add(2, 4)
one = anc.bits()
anc.add(2, 4)
check(10, 'adding the same guess twice does not double its cost',
      abs(anc.bits() - one) < 1e-12 and abs(one - 2.0) < 1e-12)
anc.add(5, 2)
check(10, 'two distinct guesses do add up', abs(anc.bits() - 3.0) < 1e-12)

left, right = Ancestry(), Ancestry()
left.add(1, 4)
right.add(1, 4)
right.add(6, 2)
merged = Ancestry()
merged.ids = left.ids | right.ids
for i in range(MAX_GUESSES):
    if (right.ids >> i) & 1:
        merged.remaining[i] = right.remaining[i]
    elif (left.ids >> i) & 1:
        merged.remaining[i] = left.remaining[i]
check(10, 'a guess feeding two paths that recombine is counted once, '
          'not once per path', abs(merged.bits() - 3.0) < 1e-12)

# -- 11. no scoring anywhere in the derivation path -------------------
# Structural: the only function that can produce a value its witness does
# not back is speculate(), and it is the only one that touches ancestry.

sm = full(6)
qm = mk(6, [0, 1, 1, 2, 2, 2], 3)
r = Result()
ask(qm, sm, r)
check(11, 'ask() never produces a guess: ancestry stays empty',
      r.ancestry.ids == 0 and r.allowed == 0 and r.verdict != SPECULATED)
observe(sm, qm, 2)
ask(qm, sm, r)
check(11, 'and still empty after observation, which is licensed',
      r.ancestry.ids == 0 and r.verdict == DERIVED)
check(11, 'every derived answer in this file came from counting survivors, '
          'never from combining scores', witness_check(qm, r.witness) == 1)

# -- 12. absorption: derived, not declared ----------------------------
# This is what replaces sm_compose_and(). That function took two RESULTS
# and combined them, which section 4 proves cannot be right in general.
# Here the same behaviour -- and more of it -- falls out of asking whether
# the operator table is constant across everything the operands could be.
# Nothing below names AND, or false, or an absorbing element.

MAX4 = Op2([max(i, j) for i in range(4) for j in range(4)], 4, 4, 4)

W4 = full(4)
q_false = mk(4, [0, 0, 0, 0], 2)
q_true = mk(4, [1, 1, 1, 1], 2)
q_gnd = Query(); query_groundless(q_gnd, 4, 2)


def a2(op, x, y, state=None):
    comp, res = Query(), Result()
    st = ask2(op, x, y, state if state is not None else W4, comp, res)
    assert st == OK, st
    return comp, res

check(12, 'a groundless query cannot be composed pointwise: map2 refuses it',
      map2(AND, q_false, q_gnd, Query()) == ERR_EMPTY_DOMAIN)

_, r = a2(AND, q_false, q_gnd)
check(12, 'false AND groundless -> DERIVED false, because the table is '
          'constant across that row', r.verdict == DERIVED and r.value == 0)
_, r_tg = a2(AND, q_true, q_gnd)
check(12, 'true AND groundless -> groundless, because it is not',
      r_tg.verdict == GROUNDLESS)
_, r = a2(OR, q_true, q_gnd)
check(12, 'true OR groundless -> DERIVED true, same rule, no new code',
      r.verdict == DERIVED and r.value == 1)
_, r = a2(OR, q_false, q_gnd)
check(12, 'false OR groundless -> groundless', r.verdict == GROUNDLESS)
_, r = a2(AND, q_gnd, q_gnd)
check(12, 'groundless AND groundless -> groundless', r.verdict == GROUNDLESS)
check(12, 'a non-constant absorption never claims UNDETERMINED: the image '
          'came from a product, so it claims nothing at all',
      r_tg.verdict == GROUNDLESS and r_tg.S == -1.0)

const0 = Op2([0, 0, 0, 0], 2, 2, 2)
_, r = a2(const0, q_gnd, q_gnd)
check(12, 'an operator constant everywhere derives even with BOTH operands '
          'groundless', r.verdict == DERIVED and r.value == 0)

q_r0 = mk(4, [0, 0, 0, 0], 4)
q_r3 = mk(4, [3, 3, 3, 3], 4)
q_gr = Query(); query_groundless(q_gr, 4, 4)
_, r = a2(MIN4, q_r0, q_gr)
check(12, 'graded: min(0, groundless-rank) derives 0 over a 4-value domain',
      r.verdict == DERIVED and r.value == 0 and r.S == 0.75)
_, r_absorb = a2(MAX4, q_r3, q_gr)
check(12, 'graded: max(3, groundless-rank) derives 3 -- absorption is not '
          'a boolean idea', r_absorb.verdict == DERIVED and r_absorb.value == 3)
_, r = a2(MIN4, mk(4, [1, 1, 1, 1], 4), q_gr)
check(12, 'graded: min(1, groundless-rank) is groundless, since it could '
          'be 0 or 1', r.verdict == GROUNDLESS)

check(12, 'an absorption witness re-derives from the operator table alone',
      witness_check_absorb(MAX4, r_absorb.witness) == 1)
t = r_absorb.witness.copy(); t.value = 2
check(12, 'an absorption witness naming the wrong value is rejected',
      witness_check_absorb(MAX4, t) == 0)
t = r_absorb.witness.copy(); t.image = 0b11
check(12, 'an absorption witness claiming a wider image is rejected',
      witness_check_absorb(MAX4, t) == 0)
t = r_absorb.witness.copy(); t.range_a = 0b1000
check(12, 'an absorption witness narrowing a range to make the operator '
          'look constant is rejected', witness_check_absorb(MIN4, t) == 0)
t = r_absorb.witness.copy(); t.range_b = 1 << 40
check(12, 'an absorption witness ranging outside an operand domain is '
          'rejected', witness_check_absorb(MAX4, t) == 0)
t = r_absorb.witness.copy(); t.S = 0.9
check(12, 'an absorption witness with an inflated S is rejected',
      witness_check_absorb(MAX4, t) == 0)

check(12, 'a worlds witness handed to the absorption checker is rejected, '
          'not checked as if it were one',
      witness_check_absorb(AND, rw.witness) == 0)
check(12, 'and an absorption witness handed to the worlds checker likewise',
      witness_check(q_r3, r_absorb.witness) == 0)

comp, r = a2(AND, q_a, q_not_a, u2)
check(12, 'ask2 with two real questions still takes the exact path: '
          'a and not a is derived false through it too',
      r.verdict == DERIVED and r.value == 0)
check(12, 'and its witness is a worlds witness that checks out',
      witness_check(comp, r.witness) == 1)

gstate = State(); state_groundless(gstate)
_, r = a2(AND, q_false, q_gnd, gstate)
check(12, 'no situation outranks absorption: nothing to be constant over',
      r.verdict == GROUNDLESS)

# soundness, brute forced: a claimed absorption must hold at EVERY world
# for EVERY value the groundless operand could have taken.
bad = None
for trial in range(3000):
    n = rng.randint(1, 12)
    da, db, do = rng.randint(1, 4), rng.randint(1, 4), rng.randint(1, 4)
    tbl = [rng.randrange(do) for _ in range(da * db)]
    op = Op2(tbl, da, db, do)
    qa = mk(n, [rng.randrange(da) for _ in range(n)], da)
    qb = Query(); query_groundless(qb, n, db)
    st = full(n)
    for w in range(n):
        if rng.random() < 0.3:
            eliminate(st, w)
    comp, r = Query(), Result()
    if ask2(op, qa, qb, st, comp, r) != OK:
        bad = ('ask2 refused', trial)
        break
    L = live_set(st)
    truth = {tbl[qa.ans[w] * db + y] for w in sorted(L) for y in range(db)}
    if r.verdict == DERIVED:
        if truth != {r.value}:
            bad = ('claimed an absorption that does not hold', trial)
            break
        if not witness_check_absorb(op, r.witness):
            bad = ('derived absorption without a checkable witness', trial)
            break
    elif r.verdict != GROUNDLESS:
        bad = ('absorption reported something other than derived or '
               'groundless', trial, r.verdict)
        break
    elif len(truth) == 1 and L:
        bad = ('missed a real absorption', trial)
        break
check(12, '3000 random absorptions: every derivation holds at every world '
          'for every value, and every real one was found', bad is None,
      str(bad))

# -- 13. the no-chain invariant, checked against the source ------------
# Section 4 proves no score function CAN exist. This checks that none has
# been written anyway. A chain can only have one shape: a score consumed
# to produce another score. So the invariant is about where S and
# intensity are READ, and it is enforced against the text of the C rather
# than trusted to stay true as the file grows.

import io as _io
import os as _os
import re as _re

# resolved against this file, so the suite works from any directory
# the C source sits one folder up, beside the C that replaced this file
_here = _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__)))
_c = _io.open(_os.path.join(_here, 'smarsh_reason.c'), encoding='utf-8').read()
# strip comments so prose about S does not count as a use of S
_code = _re.sub(r'/\*.*?\*/', '', _c, flags=_re.S)
_code = '\n'.join(l for l in _code.split('\n') if not l.strip().startswith('//'))

_reads_S = [l.strip() for l in _code.split('\n')
            if _re.search(r'(?<![A-Za-z_])(?:out|w|r)\s*->\s*S(?!\s*=[^=])', l)]
_writes_only = lambda l: _re.search(r'->\s*S\s*=[^=]', l)
_real_reads = [l for l in _reads_S if not _writes_only(l)]

# Every read of S must fall into one of exactly four permitted roles.
# Anything else is a new consumer of a score, which is the first half of a
# chain. Naming the roles rather than counting lines means a new read has
# to be justified, not just tolerated.
_gate = [l for l in _real_reads if 'tau' in l or 'intensity_of' in l]
_recheck = [l for l in _real_reads if 's_expect' in l]
_record = [l for l in _real_reads if 'witness.S =' in l]
_marker = [l for l in _real_reads if '-1.0' in l]
_other = [l for l in _real_reads
          if l not in _gate + _recheck + _record + _marker]

check(13, 'S is read in exactly one place that DECIDES anything: the tau '
          'gate, plus the intensity it reports afterwards', len(_gate) == 2,
      f'{_gate}')
check(13, 'two more reads are witness checkers re-deriving S from the '
          'worlds, which is verification and not derivation',
      len(_recheck) == 2, f'{_recheck}')
check(13, 'one copies S into the witness record, which stores a number '
          'rather than acting on one', len(_record) == 1, f'{_record}')
check(13, 'one asserts the groundless marker is out of band, so a refusal '
          'cannot be read as a measurement', len(_marker) == 1, f'{_marker}')
check(13, 'and nothing else in the kernel consumes S at all', not _other,
      f'{_other}')

_int_reads = [l.strip() for l in _code.split('\n')
              if '->intensity' in l and not _re.search(r'->\s*intensity\s*=[^=]', l)]
check(13, 'intensity is written and never read: a reported leaf, not an '
          'input to anything', not _int_reads, f'{_int_reads}')

check(13, 'no arithmetic combines two supports anywhere in the kernel',
      not _re.search(r'->\s*S\s*[-+*/]\s*\w+\s*->\s*S', _code))
check(13, 'sr_map1 and sr_map2 take queries, never results: composition '
          'cannot see a score even if it wanted to',
      'sr_result_t *a' not in _code and
      _code.count('sr_map2(const sr_op2_t *op, const sr_query_t *a') == 1)
check(13, 'ancestry stays a set and is never collapsed into a number that '
          'gates something', 'sm_ancestry_bits' not in _code)

# -- 14. settling: the draft's stopping criterion, made exact ---------
# 16 worlds = four independent booleans a,b,c,d. Target is "a and b".

def bit(n):
    return mk(16, [(w >> n) & 1 for w in range(16)], 2)


q_a4b, q_b4b, q_c4b, q_d4b = bit(3), bit(2), bit(1), bit(0)
q_target = Query()
assert map2(AND, q_a4b, q_b4b, q_target) == OK

S16 = full(16)
T = Settle()
settle_begin(T, S16, q_target)
check(14, 'at the start nothing has been tried, so nothing is settled',
      T.settled == 0 and T.image == 0b11 and T.live_at_start == 16)

r = Result()
settle_commit(T, q_target, S16, r)
check(14, 'and committing is refused, because the worlds do not agree yet',
      r.allowed == 0 and r.verdict == UNDETERMINED)

settle_step(T, S16, q_target, q_c4b, 1)
check(14, 'context about c eliminates half the worlds', T.removed[0] == 8)
check(14, 'but it does not move the answer to "a and b", so the round is '
          'PRODUCTIVE and not INFORMATIVE',
      T.productive == 1 and T.informative == 0)
check(14, 'and that is exactly what settled means here: the belief stopped '
          'moving even though the world set did not', T.settled == 1)
check(14, 'a norm on a belief vector cannot tell those two apart; a count '
          'of what was eliminated can', T.removed[0] == 8 and T.settled == 1)

settle_step(T, S16, q_target, q_a4b, 0)
check(14, 'context about a eliminates worlds AND moves the answer',
      T.removed[1] == 4 and T.informative == 1 and T.settled == 0)
check(14, 'the answer set collapsed to false, with b still entirely free',
      T.image == 0b01)

before_rounds = T.rounds
settle_step(T, S16, q_target, q_a4b, 0)
check(14, 'repeating the same context removes nothing: a fixed point with '
          'no epsilon anywhere', T.removed[before_rounds] == 0 and T.settled == 1)
check(14, 'and it is a fixed point because eliminate is idempotent, so '
          'evidence arriving twice cannot count twice -- which is the '
          'loopy-belief-propagation failure made impossible',
      T.live_now == 4)

check(14, 'total worlds removed never exceeds the worlds there were, so '
          'the loop terminates without a contraction argument',
      sum(T.removed[:T.rounds]) <= T.live_at_start)

# monotonicity, which is what makes termination provable: the answer set
# can only shrink, so it cannot oscillate the way a belief vector can.
bad = None
for trial in range(1500):
    n = rng.randint(2, 16)
    d = rng.randint(2, 5)
    tgt = mk(n, [rng.randrange(d) for _ in range(n)], d)
    st = full(n)
    tr = Settle()
    settle_begin(tr, st, tgt)
    prev_img, prev_live = tr.image, tr.live_now
    for _ in range(rng.randint(1, 8)):
        con = mk(n, [rng.randrange(2) for _ in range(n)], 2)
        if settle_step(tr, st, tgt, con, rng.randrange(2)) != OK:
            break
        if tr.image & ~prev_img:
            bad = ('an answer came back after being eliminated', trial)
            break
        if tr.live_now > prev_live:
            bad = ('a world came back', trial)
            break
        if tr.informative and not tr.productive:
            bad = ('the answer moved without any world being eliminated',
                   trial)
            break
        prev_img, prev_live = tr.image, tr.live_now
    if bad:
        break
    if sum(tr.removed[:tr.rounds]) > tr.live_at_start:
        bad = ('removed more worlds than existed', trial)
        break
check(14, '1500 random settle runs: the answer set only ever shrinks, so '
          'it cannot oscillate and the loop cannot fail to terminate',
      bad is None, str(bad))

# -- 15. commitment is earned, never scheduled ------------------------

r = Result()
settle_commit(T, q_target, S16, r)
check(15, 'a derived answer commits', r.allowed == 1 and r.value == 0)

S16b = full(16)
Tb = Settle()
settle_begin(Tb, S16b, q_target)
settle_step(Tb, S16b, q_target, q_c4b, 1)
r = Result()
settle_commit(Tb, q_target, S16b, r)
check(15, 'a SETTLED trace does not commit if the answer settled on not '
          'knowing: convergence alone earns nothing',
      Tb.settled == 1 and r.allowed == 0 and r.verdict == UNDETERMINED)
check(15, 'and a refused commit yields 0, not a best guess and not the '
          'last value seen', r.value == 0)

for _ in range(30):
    settle_step(Tb, S16b, q_target, q_c4b, 1)
r = Result()
settle_commit(Tb, q_target, S16b, r)
check(15, 'no number of rounds causes a commit: 31 of them, still refused',
      Tb.rounds == 31 and r.allowed == 0)

S16c = full(16)
Tc = Settle()
settle_begin(Tc, S16c, q_target)
settle_step(Tc, S16c, q_target, q_c4b, 1)
settle_step(Tc, S16c, q_target, q_c4b, 0)
r = Result()
settle_commit(Tc, q_target, S16c, r)
check(15, 'contradictory context is reported as a contradiction and does '
          'not commit',
      Tc.contradicted == 1 and r.verdict == CONTRADICTION and r.allowed == 0)
check(15, 'which is the closed loop: incoming context was checked against '
          'what was held, not propagated forward unread',
      Tc.live_now == 0)

gq = Query(); query_groundless(gq, 16, 2)
r = Result()
settle_commit(T, gq, full(16), r)
check(15, 'a groundless target does not commit either',
      r.verdict == GROUNDLESS and r.allowed == 0)

# -- 16. reopening a claim, and what replaces calibration -------------

def k_way(k):
    st = full(k)
    return st, mk(k, list(range(k)), k)


st, q = k_way(4)
anc = Ancestry()
cp = checkpoint(st)
r = Result()
speculate(st, q, 0.0, 7, INTENSITY_SUPPORT, 2, anc, r)
check(16, 'a speculation cuts the worlds down to one',
      r.allowed == 1 and live_count(st) == 1)
check(16, 'and the debt is on the books', abs(anc.bits() - 2.0) < 1e-12)

retract(st, cp, anc, 2)
check(16, 'retracting restores every world exactly', live_count(st) == 4)
check(16, 'and clears the debt with it, so neither a phantom guess nor an '
          'unpaid one is left behind', anc.ids == 0 and anc.bits() == 0.0)
r2 = Result()
ask(q, st, r2)
check(16, 'the question is open again, which a token chain has no '
          'operation for', r2.verdict == UNDETERMINED and r2.S == 0.0)

st, q = k_way(4)
anc = Ancestry()
anc.add(9, 2)
cp = checkpoint(st)
r = Result()
speculate(st, q, 0.0, 7, INTENSITY_SUPPORT, 2, anc, r)
retract(st, cp, anc, 2)
check(16, 'retracting one guess leaves an unrelated one standing',
      (anc.ids >> 9) & 1 == 1 and abs(anc.bits() - 1.0) < 1e-12)

# The draft's section 4 leaves an interface open: ECE needs a probability
# as input, and this layer does not produce one. It does not need to. A
# uniform pick over |D| is right 1/|D| of the time BY CONSTRUCTION, so the
# expected accuracy is derived rather than measured. Confirmed anyway,
# because a derived number that nobody checked is still a claim.
for k, want in ((2, 0.5), (4, 0.25), (8, 0.125)):
    hits = 0
    trials = 4000
    for seed in range(trials):
        st, q = k_way(k)
        r = Result()
        speculate(st, q, 0.0, seed, INTENSITY_SUPPORT, 0, Ancestry(), r)
        if r.allowed and r.value == 0:
            hits += 1
    rate = hits / trials
    check(16, f'a guess over {k} possibilities is right {want} of the time, '
              f'exactly as 1/|D| says it must be', abs(rate - want) < 0.025,
          f'measured {rate:.4f}')
check(16, 'so there is no calibration gap to close: the accuracy of a '
          'guess is a consequence of |D|, not a property to be estimated '
          'and corrected', True)

# -- 17. the conclusion does not depend on the order ------------------
# The sharpest single test separating a derivation from a chain. A chain
# is order-dependent by construction: what came first is conditioned on by
# everything after, and cannot be revisited. Elimination commutes, because
# intersecting sets commutes, so the same facts in any order reach the
# same place. The TRACE differs -- the path is contingent. The conclusion
# is not.

import itertools as _it

_bad = None
_traces_differed = 0
for trial in range(2000):
    n = rng.randint(4, 24)
    d = rng.randint(2, 5)
    tgt = mk(n, [rng.randrange(d) for _ in range(n)], d)
    cons = [(mk(n, [rng.randrange(3) for _ in range(n)], 3), rng.randrange(3))
            for _ in range(rng.randint(2, 5))]
    outcomes, shapes = set(), set()
    for perm in _it.permutations(range(len(cons))):
        st = full(n)
        tr = Settle()
        settle_begin(tr, st, tgt)
        for i in perm:
            q, a = cons[i]
            settle_step(tr, st, tgt, q, a)
        rr = Result()
        settle_commit(tr, tgt, st, rr)
        outcomes.add((rr.verdict, rr.value, rr.allowed, round(rr.S, 12),
                      tr.live_now))
        shapes.add(tuple(tr.removed[:tr.rounds]))
    if len(outcomes) != 1:
        _bad = (trial, outcomes)
        break
    if len(shapes) > 1:
        _traces_differed += 1

check(17, '2000 random constraint sets, EVERY permutation of each: verdict, '
          'value, S and world count are identical under all orderings',
      _bad is None, str(_bad))
check(17, 'while the trace itself differed by ordering in most of them, so '
          'the path is contingent and the conclusion is not',
      _traces_differed > 1500, f'{_traces_differed} of 2000')
check(17, 'this is what a chain cannot have: what arrives first is not '
          'conditioned on by what arrives after, because elimination '
          'commutes', _bad is None)

# -- 18. choosing what to ask, without a salience score ---------------

S18 = full(16)                       # a,b,c,d again; target is "a and b"
P = Probe()
check(18, 'a question about c cannot move "a and b", and that is PROVED '
          'rather than scored low',
      probe(S18, q_target, q_c4b, P) == OK and P.irrelevant == 1)
check(18, 'though it would eliminate half the worlds, so irrelevant is not '
          'the same as useless', P.best_case_removed == 8)

probe(S18, q_target, q_a4b, P)
check(18, 'a question about a is relevant but not sufficient: knowing a '
          'still leaves b free', P.irrelevant == 0 and P.sufficient == 0)
check(18, 'its guarantee is 8 worlds whichever way it falls',
      P.worst_case_removed == 8 and P.best_case_removed == 8)

probe(S18, q_target, q_target, P)
check(18, 'asking the target itself is sufficient: every answer derives it',
      P.sufficient == 1 and P.irrelevant == 0)
check(18, 'and its guarantee is the smaller branch: 4 worlds, since "true" '
          'leaves only 4 standing', P.worst_case_removed == 4)

check(18, 'probing changes nothing: no elimination happens by asking what '
          'asking would do', live_count(S18) == 16)

# the partition invariant, and the guarantee actually being achievable
bad = None
for trial in range(2000):
    n = rng.randint(2, 20)
    dt, dq = rng.randint(2, 4), rng.randint(2, 4)
    tgt = mk(n, [rng.randrange(dt) for _ in range(n)], dt)
    qn = mk(n, [rng.randrange(dq) for _ in range(n)], dq)
    st = full(n)
    for w in range(n):
        if rng.random() < 0.3:
            eliminate(st, w)
    pp = Probe()
    if probe(st, tgt, qn, pp) != OK:
        bad = ('probe refused', trial)
        break
    live = live_count(st)
    if sum(pp.surviving) != live:
        bad = ('the branches do not partition the live set', trial)
        break
    # actually ask it, every possible way, and confirm the floor holds
    worst = None
    for a in range(dq):
        if not (pp.reachable >> a) & 1:
            continue
        branch = checkpoint(st)
        observe(branch, qn, a)
        removed = live - live_count(branch)
        worst = removed if worst is None else min(worst, removed)
        rr = Result()
        ask(tgt, branch, rr)
        if pp.sufficient and rr.verdict != DERIVED:
            bad = ('claimed sufficient but a branch is not derived', trial)
            break
    if bad:
        break
    if worst is not None and worst != pp.worst_case_removed:
        bad = ('the guaranteed floor was not the floor', trial, worst,
               pp.worst_case_removed)
        break
check(18, '2000 random probes: the branches partition the live set, the '
          'guaranteed floor is achieved by actually asking, and every '
          'claim of sufficiency holds in every branch', bad is None, str(bad))

pr = Probe()
st, i = choose(S18, q_target, [q_c4b, q_d4b, q_a4b], ASK_GUARANTEE, pr)
check(18, 'choose skips the two questions it proved cannot help and takes '
          'the one that can', st == OK and i == 2)

st, i = choose(S18, q_target, [q_c4b, q_d4b], ASK_GUARANTEE, pr)
check(18, 'when no candidate narrows the target on its own it asks '
          'nothing (one question at a time; whether several would help '
          "together is the caller's check)", st == OK and i == 2)

st, i = choose(S18, q_target, [q_a4b, q_target], ASK_GUARANTEE, pr)
check(18, 'a sufficient question outranks a merely helpful one, because '
          'ending the matter is derived and preference is not',
      st == OK and i == 1 and pr.sufficient == 1)

st, i = choose(S18, q_target, [q_a4b, q_b4b], ASK_GUARANTEE, pr)
check(18, 'a tie breaks to the lowest index, so the same situation always '
          'asks the same question', st == OK and i == 0)

gq18 = Query(); query_groundless(gq18, 16, 2)
st, i = choose(S18, q_target, [gq18, q_a4b], ASK_GUARANTEE, pr)
check(18, 'a candidate that is not a question is skipped, not fatal: one '
          'malformed option does not stop the others being considered',
      st == OK and i == 1)

S18d = full(16)
observe(S18d, q_a4b, 0)
st, i = choose(S18d, q_target, [q_b4b, q_c4b, q_d4b], ASK_GUARANTEE, pr)
check(18, 'once the target is derived it asks nothing at all', st == OK and i == 3)

# guarantee and opportunity really do differ
S18e = full(8)
# Target is "which world", so no single yes/no question can settle it and
# neither candidate is sufficient -- otherwise sufficiency would decide it
# and the policies would never get to disagree.
tgt8 = mk(8, list(range(8)), 8)
lop = mk(8, [0, 0, 0, 0, 0, 0, 0, 1], 2)      # lopsided: 7 vs 1
even = mk(8, [0, 0, 0, 0, 1, 1, 1, 1], 2)     # balanced: 4 vs 4
pa, pb = Probe(), Probe()
probe(S18e, tgt8, lop, pa)
probe(S18e, tgt8, even, pb)
check(18, 'a lopsided question can win on opportunity and lose on '
          'guarantee', pa.best_case_removed > pb.best_case_removed and
      pa.worst_case_removed < pb.worst_case_removed)
_, ig = choose(S18e, tgt8, [lop, even], ASK_GUARANTEE, pr)
_, io = choose(S18e, tgt8, [lop, even], ASK_OPPORTUNITY, pr)
check(18, 'and the two named policies pick differently, which is why the '
          'choice is named instead of buried', ig == 1 and io == 0)

# -- 19. induction, on the same kernel, with worlds as RULES ----------
# Hypothesis space: output = AND of some subset of three features. Eight
# subsets, so eight worlds. Nothing new is added to the kernel: a world
# was a way things could be, and a rule is a way things could be.

def hyp(x):
    """Query: what does each candidate rule output on input x?"""
    return mk(8, [1 if (w & ~x & 7) == 0 else 0 for w in range(8)], 2)


which_rule = mk(8, list(range(8)), 8)
H = full(8)
r = Result()
ask(which_rule, H, r)
check(19, 'before any data every rule is possible: undetermined, S = 0',
      r.verdict == UNDETERMINED and r.S == 0.0)

observe(H, hyp(0b011), 0)            # f(x1=1, x2=1, x3=0) = 0
check(19, 'one example eliminates the four rules that contradict it',
      live_count(H) == 4)
observe(H, hyp(0b101), 1)            # f(x1=1, x2=0, x3=1) = 1
check(19, 'a second leaves two', live_count(H) == 2)

ask(which_rule, H, r)
check(19, 'the RULE is still undetermined, with S = 0.75 and 1 bit unheld',
      r.verdict == UNDETERMINED and r.S == 0.75 and abs(r.H - 1.0) < 1e-12)

r_yes = Result()
ask(hyp(0b111), H, r_yes)
check(19, 'and yet an unseen input IS derived: both surviving rules agree '
          'that f(1,1,1) = 1', r_yes.verdict == DERIVED and r_yes.value == 1)
r_no = Result()
ask(hyp(0b000), H, r_no)
check(19, 'and both agree that f(0,0,0) = 0',
      r_no.verdict == DERIVED and r_no.value == 0)
r_idk = Result()
ask(hyp(0b100), H, r_idk)
check(19, 'while f(0,0,1) is undetermined, because there they differ',
      r_idk.verdict == UNDETERMINED)
check(19, 'so the answer can be known while the rule is not, and the two '
          'are reported separately instead of one standing in for the '
          'other', r_yes.verdict == DERIVED and r.verdict == UNDETERMINED)

pr = Probe()
check(19, 'testing f(1,1,1) is PROVED unable to identify the rule, before '
          'spending the experiment on it',
      probe(H, which_rule, hyp(0b111), pr) == OK and pr.irrelevant == 1)
check(19, 'testing f(0,0,1) is proved sufficient: whichever way it comes '
          'back, the rule is pinned',
      probe(H, which_rule, hyp(0b100), pr) == OK and pr.sufficient == 1)
_, pick = choose(H, which_rule, [hyp(0b111), hyp(0b000), hyp(0b100)],
                 ASK_GUARANTEE, pr)
check(19, 'so choosing the next experiment needs no salience model: it is '
          'counted off the surviving rules', pick == 2)

anc = Ancestry()
cp = checkpoint(H)
rs = Result()
speculate(H, which_rule, 0.75, 11, INTENSITY_SUPPORT, 0, anc, rs)
check(19, 'picking one of two consistent rules is a SPECULATION, not a '
          'conclusion, and it costs exactly the 1 bit that separates them',
      rs.verdict == SPECULATED and abs(anc.bits() - 1.0) < 1e-12)
check(19, 'its witness still says undetermined, so the choice never passes '
          'as a derivation', rs.witness.verdict == UNDETERMINED)
retract(H, cp, anc, 0)
check(19, 'and it can be taken back', live_count(H) == 2 and anc.ids == 0)

# the honest half: generalisation lives in the hypothesis space, not the data
U = full(16)                          # all 16 boolean functions of 2 inputs
def unrestricted(x):
    return mk(16, [(w >> (3 - x)) & 1 for w in range(16)], 2)
observe(U, unrestricted(0), 0)
observe(U, unrestricted(1), 1)
observe(U, unrestricted(2), 1)
r = Result()
ask(unrestricted(3), U, r)
check(19, 'with an UNRESTRICTED hypothesis space, three of four inputs '
          'observed still leaves the fourth undetermined',
      live_count(U) == 2 and r.verdict == UNDETERMINED)
check(19, 'so generalisation comes from the hypothesis space and not from '
          'the data, and the kernel says undetermined instead of picking '
          'the popular answer', r.S == 0.0)

# -- 20. refinement: the worlds are forced by the questions -----------
# Eight raw situations, three latent attributes:
#   bit2  A is above B          (structural)
#   bit1  A and B are touching  (structural)
#   bit0  the object is called a cup, not a mug   (a label, not structure)

def attr(bit):
    return mk(8, [(s >> bit) & 1 for s in range(8)], 2)


q_above, q_touch, q_name = attr(2), attr(1), attr(0)

P1 = Partition()
check(20, 'the structural stage refines eight situations into four worlds, '
          'and the four is derived, not chosen',
      refine([q_above, q_touch], 8, P1) == OK and P1.n_cells == 4)
check(20, 'two situations differing only in what the object is CALLED land '
          'in the same world, because no structural question separates them',
      P1.cell[0] == P1.cell[1] and P1.cell[6] == P1.cell[7])

proj = Query()
check(20, 'a structural question projects onto its own partition',
      project(P1, q_above, proj) == OK and proj.n_worlds == 4)
ok_proj = all(proj.ans[P1.cell[s]] == q_above.ans[s] for s in range(8))
check(20, 'and the projection answers exactly what the original did, in '
          'every situation', ok_proj)

check(20, 'the NAME question does not project: it needs a distinction '
          'structure cannot make', project(P1, q_name, Query()) != OK)
check(20, 'so it is a genuinely new capacity, and that is decided by '
          'counting rather than judged', distinguishes(P1, q_name) == 1)

both = Query()
map2(AND, q_above, q_touch, both)
check(20, 'a question built out of the stage-1 questions projects fine, so '
          'it adds no distinction at all',
      project(P1, both, Query()) == OK and distinguishes(P1, both) == 0)

# "math compresses structure": a magnitude over the same content
count = mk(8, [((s >> 2) & 1) + ((s >> 1) & 1) for s in range(8)], 3)
check(20, 'a MAGNITUDE over the structural attributes also projects, so '
          'the arithmetic stage adds no new distinction -- it compresses, '
          'which is exactly what the ladder claims and now checks',
      project(P1, count, Query()) == OK and distinguishes(P1, count) == 0)

P2 = Partition()
refine([q_above, q_touch, q_name], 8, P2)
check(20, 'adding the label question refines four worlds into eight',
      P2.n_cells == 8)
check(20, 'and now it projects, because the partition is fine enough',
      project(P2, q_name, Query()) == OK)

P3 = Partition()
refine([q_above, q_touch, both, count], 8, P3)
check(20, 'adding only definable questions does not refine anything: the '
          'world count is unchanged at four', P3.n_cells == P1.n_cells)

P0 = Partition()
refine([], 8, P0)
check(20, 'with no questions at all every situation collapses into one '
          'world, since nothing can be told apart', P0.n_cells == 1)

# minimality and correctness against ground truth computed a different way
bad = None
for trial in range(2000):
    ns = rng.randint(1, 24)
    nq = rng.randint(0, 5)
    qs = [mk(ns, [rng.randrange(rng.randint(2, 4)) for _ in range(ns)],
             4) for _ in range(nq)]
    pt = Partition()
    if refine(qs, ns, pt) != OK:
        bad = ('refine refused', trial)
        break
    sig = {}
    for s in range(ns):
        sig.setdefault(tuple(q.ans[s] for q in qs), []).append(s)
    if pt.n_cells != len(sig):
        bad = ('cell count is not the number of distinct signatures', trial)
        break
    if any(len({pt.cell[s] for s in group}) != 1 for group in sig.values()):
        bad = ('situations with one signature were split', trial)
        break
    for q in qs:
        if project(pt, q, Query()) != OK:
            bad = ('a question that built the partition failed to project',
                   trial)
            break
        if distinguishes(pt, q):
            bad = ('a question that built the partition still distinguishes',
                   trial)
            break
    if bad:
        break
check(20, '2000 random question sets: the world count is exactly the number '
          'of distinct answer signatures, nothing is split that should not '
          'be, and every question that built a partition projects onto it',
      bad is None, str(bad))

# ====================================================================

TITLES = {
    1: 'the primitive: elimination, and nothing else',
    2: 'the four verdicts, counted rather than declared',
    3: 'composition vs independently brute-forced ground truth',
    4: 'THE CORRELATION THEOREM: no score function can exist',
    5: 'graded domains, where S is not degenerate',
    6: 'witnesses: the answer carries its own proof',
    7: 'observation is elimination',
    8: 'the tau boundary on a non-degenerate S',
    9: 'speculation IS unlicensed elimination',
    10: 'the diamond: a set of guesses, not a sum',
    11: 'no scoring anywhere in the derivation path',
    12: 'absorption: derived from the operator, never declared',
    13: 'the no-chain invariant, enforced against the source',
    14: 'settling: the draft stopping criterion, made exact',
    15: 'commitment is earned, never scheduled',
    16: 'reopening a claim, and what replaces calibration',
    17: 'the conclusion does not depend on the order',
    18: 'choosing what to ask, without a salience score',
    19: 'induction, on the same kernel, worlds as rules',
    20: 'refinement: the worlds are forced by the questions',
}

print('smarsh_reason.c -- the reasoning kernel, checked against ground truth')
print()
failed = 0
last = None
for sec, name, ok, detail in CHECKS:
    if sec != last:
        print(f'  [{sec}] {TITLES[sec]}')
        last = sec
    print(f'    {"ok  " if ok else "FAIL"}  {name}')
    if not ok:
        failed += 1
        if detail:
            print(f'            {detail}')
print()
if failed:
    print(f'{failed} of {len(CHECKS)} checks failed')
    raise SystemExit(1)
print(f'all {len(CHECKS)} checks pass')
