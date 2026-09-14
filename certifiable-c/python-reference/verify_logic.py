"""A faithful transliteration of smarsh_core.c, run against the assertions
in test_smarsh_core.c.

This does NOT verify the C. It cannot: compilation, undefined behaviour,
integer promotion and the actual bit widths are all untested until a
compiler exists. What it DOES verify is the algorithm -- the branch order,
the bitset arithmetic, the verdict counting, the gate direction, the
absorption rule and the ancestry accumulation -- which is where a logic
error would live, and which is most of what could be wrong.

Written by transliterating the C line by line, deliberately keeping the
same structure (including the >> 6 and & 63 index math and the order of
the checks in compose_and) rather than writing idiomatic Python, so that
a divergence here is a divergence there.
"""

import io
import math
import os

# resolved against this file, so the suite works from any directory
# the C source sits one folder up, beside the C that replaced this file
_here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SM_MAX_DOMAIN = 256
SM_DOMAIN_WORDS = (SM_MAX_DOMAIN + 63) // 64
MASK64 = (1 << 64) - 1

SM_DERIVED, SM_UNDETERMINED, SM_CONTRADICTION, SM_GROUNDLESS, SM_SPECULATED = range(5)
SM_OK, SM_ERR_DOMAIN_TOO_LARGE, SM_ERR_EMPTY_DOMAIN, \
    SM_ERR_INDEX_OUT_OF_DOMAIN, SM_ERR_BAD_THRESHOLD, SM_ERR_NULL_ARGUMENT = range(6)

SM_FALSE_INDEX, SM_TRUE_INDEX = 0, 1
SM_INTENSITY_SUPPORT, SM_INTENSITY_HEADROOM = 0, 1
SM_MAX_GUESSES = 64
SM_ERR_GUESS_ID_OUT_OF_RANGE = 6


class P:
    def __init__(self):
        self.bits = [0] * SM_DOMAIN_WORDS
        self.size = 0
        self.has_domain = 0


class R:
    def __init__(self, verdict=SM_UNDETERMINED, value=0, S=0.0, H=0.0, ancestry=None):
        self.verdict, self.value, self.S, self.H = verdict, value, S, H
        # {guess_id: remaining} -- a SET of guesses, mirroring sm_ancestry_t
        # in the C. This was a running double, which counted a guess reached
        # by two paths twice over; a diamond of depth n reported 2^n bits for
        # a single guess. Tolerates the old positional callers that passed a
        # float here by ignoring anything that is not a dict.
        self.ancestry = dict(ancestry) if isinstance(ancestry, dict) else {}
        self.intensity = 0.0

    @property
    def ancestry_H(self):
        """Summed over DISTINCT guesses, so a shared ancestor counts once."""
        return sum(math.log2(n) for n in self.ancestry.values() if n > 1)


def popcount64(x):
    x = x & MASK64
    x = x - ((x >> 1) & 0x5555555555555555)
    x = (x & 0x3333333333333333) + ((x >> 2) & 0x3333333333333333)
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0F
    return ((x * 0x0101010101010101) & MASK64) >> 56


def splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, z ^ (z >> 31)


def sm_init(p, size):
    if size == 0:
        return SM_ERR_EMPTY_DOMAIN
    if size > SM_MAX_DOMAIN:
        return SM_ERR_DOMAIN_TOO_LARGE
    for w in range(SM_DOMAIN_WORDS):
        p.bits[w] = 0
    for i in range(size):
        p.bits[i >> 6] |= (1 << (i & 63))
    p.size = size
    p.has_domain = 1
    return SM_OK


def sm_init_groundless(p):
    for w in range(SM_DOMAIN_WORDS):
        p.bits[w] = 0
    p.size = 0
    p.has_domain = 0


def sm_eliminate(p, index):
    if p.has_domain == 0:
        return SM_ERR_EMPTY_DOMAIN
    if index >= p.size:
        return SM_ERR_INDEX_OUT_OF_DOMAIN
    p.bits[index >> 6] &= ~(1 << (index & 63)) & MASK64
    return SM_OK


def sm_count(p):
    if p.has_domain == 0:
        return 0
    return sum(popcount64(p.bits[w]) for w in range(SM_DOMAIN_WORDS))


def sm_verdict(p):
    if p.has_domain == 0:
        return SM_GROUNDLESS
    n = sm_count(p)
    if n == 0:
        return SM_CONTRADICTION
    if n == 1:
        return SM_DERIVED
    return SM_UNDETERMINED


def sm_support(p):
    if p.has_domain == 0 or p.size == 0:
        return -1.0
    return 1.0 - (sm_count(p) / p.size)


def sm_hartley(p):
    if p.has_domain == 0:
        return 0.0
    n = sm_count(p)
    if n <= 1:
        return 0.0
    return math.log2(n)


def nth_possible(p, k):
    seen = 0
    for i in range(SM_MAX_DOMAIN):
        if i >= p.size:
            break
        if (p.bits[i >> 6] & (1 << (i & 63))) != 0:
            if seen == k:
                return i
            seen += 1
    return 0


def intensity_of(s, tau, policy):
    if policy == SM_INTENSITY_HEADROOM:
        if tau >= 1.0:
            return 0.0
        return (s - tau) / (1.0 - tau)
    return s


def sm_decide(p, tau, seed, out, policy=SM_INTENSITY_SUPPORT, guess_id=0):
    if not (tau >= 0.0) or not (tau <= 1.0):
        return SM_ERR_BAD_THRESHOLD
    if guess_id >= SM_MAX_GUESSES:
        return SM_ERR_GUESS_ID_OUT_OF_RANGE
    out.ancestry = {}
    out.intensity = 0.0
    v = sm_verdict(p)
    if v == SM_GROUNDLESS:
        out.verdict, out.value, out.S, out.H = SM_GROUNDLESS, 0, -1.0, 0.0
        return SM_OK
    out.S = sm_support(p)
    out.H = sm_hartley(p)
    if v == SM_CONTRADICTION:
        out.verdict, out.value = SM_CONTRADICTION, 0
        return SM_OK
    if v == SM_DERIVED:
        out.verdict, out.value = SM_DERIVED, nth_possible(p, 0)
        return SM_OK
    if out.S < tau:
        out.verdict, out.value = SM_UNDETERMINED, 0
        return SM_OK
    n = sm_count(p)
    _, rnd = splitmix64(seed)
    k = rnd % n
    out.verdict, out.value = SM_SPECULATED, nth_possible(p, k)
    out.intensity = intensity_of(out.S, tau, policy)
    # A speculation records itself, so composing needs only a union.
    out.ancestry = {guess_id: n}
    return SM_OK


def definitely_false(r):
    return r.verdict in (SM_DERIVED, SM_SPECULATED) and r.value == SM_FALSE_INDEX


def definitely_true(r):
    return r.verdict in (SM_DERIVED, SM_SPECULATED) and r.value == SM_TRUE_INDEX


# ---------------------------------------------------------------------------
# the assertions from test_smarsh_core.c, run
# ---------------------------------------------------------------------------

FAILURES = []


def check(name, cond, detail=''):
    print(f'  {"ok  " if cond else "FAIL"}  {name}' + ('' if cond or not detail else f'  -- {detail}'))
    if not cond:
        FAILURES.append(name)


def close(a, b):
    return abs(a - b) < 1e-9


print('S and H against hand-computed values')
p = P(); sm_init(p, 2); sm_eliminate(p, 0)
check('derived: |D|=1 of 2 gives S=0.5', close(sm_support(p), 0.5), f'{sm_support(p)}')
check('derived: H=0', close(sm_hartley(p), 0.0))
check('derived: verdict is derived', sm_verdict(p) == SM_DERIVED)

q = P(); sm_init(q, 2)
check('undetermined: S=0 exactly', close(sm_support(q), 0.0))
check('undetermined: H=1.0 bit', close(sm_hartley(q), 1.0))
check('undetermined: verdict is undetermined', sm_verdict(q) == SM_UNDETERMINED)

p = P(); sm_init(p, 2); sm_eliminate(p, 0); sm_eliminate(p, 1)
check('contradiction: everything eliminated but a domain existed',
      sm_verdict(p) == SM_CONTRADICTION)
check('contradiction reports S=1.0', close(sm_support(p), 1.0))

p = P(); sm_init_groundless(p)
check('groundless: no domain', sm_verdict(p) == SM_GROUNDLESS)
check('groundless: S is -1, not 0', close(sm_support(p), -1.0))

p = P(); sm_init(p, 2); sm_eliminate(p, 0); sm_eliminate(p, 0)
check('eliminating twice equals eliminating once', sm_count(p) == 1)

print()
print('the tau boundary')
p = P(); sm_init(p, 5)
for i in (0, 1, 2):
    sm_eliminate(p, i)
check('constructed S=0.6', close(sm_support(p), 0.6), f'{sm_support(p)}')
r = R(); sm_decide(p, 0.5, 1, r)
check('S=0.6 >= tau=0.5 speculates', r.verdict == SM_SPECULATED)

q = P(); sm_init(q, 5); sm_eliminate(q, 0); sm_eliminate(q, 1)
check('constructed S=0.4', close(sm_support(q), 0.4), f'{sm_support(q)}')
r = R(); sm_decide(q, 0.5, 1, r)
check('S=0.4 < tau=0.5 declines', r.verdict == SM_UNDETERMINED)

e = P(); sm_init(e, 4); sm_eliminate(e, 0); sm_eliminate(e, 1)
check('constructed S=0.5 exactly', close(sm_support(e), 0.5))
r = R(); sm_decide(e, 0.5, 7, r)
check('S == tau clears the bar', r.verdict == SM_SPECULATED)

verdicts = set()
for seed in range(200):
    r = R(); sm_decide(p, 0.5, seed, r); verdicts.add(r.verdict)
check('the decision is seed-independent', verdicts == {SM_SPECULATED}, f'{verdicts}')

r1, r2 = R(), R()
sm_decide(p, 0.5, 42, r1); sm_decide(p, 0.5, 42, r2)
check('same seed reproduces the same guess', r1.value == r2.value)

counts = {}
for seed in range(4000):
    r = R(); sm_decide(p, 0.5, seed, r)
    counts[r.value] = counts.get(r.value, 0) + 1
expected = 4000 / sm_count(p)
ratios = {v: c / expected for v, c in counts.items()}
check('the guess is uniform over what remains',
      all(0.94 < x < 1.06 for x in ratios.values()), f'{ratios}')

print()
print('composition moved to smarsh_reason.c')
check('sm_compose_and is gone from the core',
      'sm_compose_and' not in io.open(os.path.join(_here, 'smarsh_core.c'), encoding='utf-8').read())
check('and gone from its header too, except as a note explaining why',
      io.open(os.path.join(_here, 'smarsh_core.h'), encoding='utf-8').read().count('sm_compose_and') == 1)
# WHY: it took two RESULTS, so its inputs were the two answer sets. See
# verify_reason.py section 4 -- three composites share one operand
# signature and land on three verdicts, so no such function exists. What
# replaces it composes the QUESTIONS instead: sr_ask2().

print()
print('ancestry as a set of guesses -- the part that survived')


def union(a, b):
    # sm_ancestry_union: a set union, never a sum.
    m = dict(a)
    m.update(b)
    return m


def bits(anc):
    return sum(math.log2(n) for n in anc.values() if n > 1)


one = {5: 2}
check('a single 1-bit guess costs 1.0 bit', close(bits(one), 1.0), f'{bits(one)}')
check('unioning a guess with itself does not double it',
      close(bits(union(one, one)), 1.0), f'{bits(union(one, one))}')
check('a genuinely different guess does add', close(bits(union(one, {6: 2})), 2.0))

# The diamond: the bug this representation exists to fix. One guess, two
# paths, recombined.
left = union(one, {})
right = union(one, {})
joined = union(left, right)
check('one shared guess counted once, not once per path',
      close(bits(joined), 1.0), f'{bits(joined)}')

deep = joined
for _ in range(5):
    deep = union(deep, deep)
check('a depth-5 diamond does not explode to 2^5 bits', close(bits(deep), 1.0),
      f'{bits(deep)}')
check('a 4-way guess costs 2.0 bits, not 1', close(bits({0: 4}), 2.0))

rr = R()
check('a guess_id past SM_MAX_GUESSES is refused',
      sm_decide(p2 if 'p2' in dir() else P(), 0.5, 0, rr, SM_INTENSITY_SUPPORT,
                SM_MAX_GUESSES) == SM_ERR_GUESS_ID_OUT_OF_RANGE)

print()
print('g(): intensity, and the default matching shipped speculate.js')

gp = P(); sm_init(gp, 5)
for i in (0, 1, 2):
    sm_eliminate(gp, i)          # S = 0.6
r = R(); sm_decide(gp, 0.5, 1, r, SM_INTENSITY_SUPPORT)
check('SUPPORT policy: intensity == S, matching speculate.js identity',
      close(r.intensity, 0.6), f'{r.intensity}')

r = R(); sm_decide(gp, 0.5, 1, r, SM_INTENSITY_HEADROOM)
check('HEADROOM policy: (0.6-0.5)/(1-0.5) = 0.2',
      close(r.intensity, 0.2), f'{r.intensity}')

# At the bar exactly, headroom must be 0 -- barely clearing is not
# near-total confidence, which is the whole argument for this policy.
eb = P(); sm_init(eb, 4); sm_eliminate(eb, 0); sm_eliminate(eb, 1)   # S = 0.5
r = R(); sm_decide(eb, 0.5, 3, r, SM_INTENSITY_HEADROOM)
check('HEADROOM at S == tau gives intensity 0, not tau', close(r.intensity, 0.0),
      f'{r.intensity}')
r = R(); sm_decide(eb, 0.5, 3, r, SM_INTENSITY_SUPPORT)
check('SUPPORT at S == tau gives 0.5 -- the behaviour headroom argues against',
      close(r.intensity, 0.5), f'{r.intensity}')

# Declining must not leave a stale intensity from a previous decision.
dec = P(); sm_init(dec, 5); sm_eliminate(dec, 0); sm_eliminate(dec, 1)  # S = 0.4
r = R(); sm_decide(gp, 0.5, 1, r, SM_INTENSITY_SUPPORT)   # speculates, sets 0.6
sm_decide(dec, 0.5, 1, r, SM_INTENSITY_SUPPORT)           # declines, must reset
check('a declined decision reports intensity 0, not a stale value',
      r.verdict == SM_UNDETERMINED and close(r.intensity, 0.0), f'{r.intensity}')

gl = P(); sm_init_groundless(gl)
r = R(); sm_decide(gl, 0.5, 1, r, SM_INTENSITY_SUPPORT)
check('groundless reports intensity 0', close(r.intensity, 0.0))

print()
print('checked rejections')
p2 = P()
check('domain too large refused', sm_init(p2, SM_MAX_DOMAIN + 1) == SM_ERR_DOMAIN_TOO_LARGE)
check('empty domain refused', sm_init(p2, 0) == SM_ERR_EMPTY_DOMAIN)
sm_init(p2, 4)
check('eliminating outside the domain refused',
      sm_eliminate(p2, 9) == SM_ERR_INDEX_OUT_OF_DOMAIN)
r = R()
check('tau outside [0,1] refused', sm_decide(p2, 1.5, 0, r) == SM_ERR_BAD_THRESHOLD)
check('NaN tau refused', sm_decide(p2, float('nan'), 0, r) == SM_ERR_BAD_THRESHOLD)

print()
if FAILURES:
    print(f'{len(FAILURES)} check(s) failed: {", ".join(FAILURES)}')
    raise SystemExit(1)
print('all checks passed -- the ALGORITHM is verified; the C itself is not,')
print('until a compiler exists to build smarsh_core.c and run test_smarsh_core.c.')
