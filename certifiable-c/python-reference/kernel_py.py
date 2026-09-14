"""The reasoning kernel transliterated from smarsh_reason.c.

Split out of verify_reason.py so a demonstration runs the EXACT code
the checks run, rather than a second copy that could drift from it.
This is the algorithm. The C is the transcription; neither has been
compiled, because no compiler is present.
"""

import math
import random

MAX_WORLDS = 256
WORLD_WORDS = (MAX_WORLDS + 63) // 64
MAX_ANSWERS = 64
MAX_TABLE = MAX_ANSWERS * MAX_ANSWERS
MAX_GUESSES = 64

# verdicts, matching sm_verdict_t
DERIVED, UNDETERMINED, CONTRADICTION, GROUNDLESS, SPECULATED = range(5)
VNAME = {DERIVED: 'derived', UNDETERMINED: 'undetermined',
         CONTRADICTION: 'contradiction', GROUNDLESS: 'groundless',
         SPECULATED: 'speculated'}

OK = 0
ERR_DOMAIN_TOO_LARGE = 1
ERR_EMPTY_DOMAIN = 2
ERR_INDEX_OUT_OF_DOMAIN = 3
ERR_BAD_THRESHOLD = 4
ERR_NULL_ARGUMENT = 5
ERR_GUESS_ID_OUT_OF_RANGE = 6
ERR_INTERNAL_INVARIANT = 7

INTENSITY_SUPPORT, INTENSITY_HEADROOM = 0, 1

MASK64 = (1 << 64) - 1


class Fail(Exception):
    pass


# ====================================================================
# transliteration of smarsh_reason.c
# ====================================================================

def popcount64(x):
    return bin(x & MASK64).count('1')


def splitmix64(state):
    state = (state + 0x9E3779B97F4A7C15) & MASK64
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK64
    return state, z ^ (z >> 31)


def word_mask(w, n):
    base = w * 64
    if n >= base + 64:
        return MASK64
    if n <= base:
        return 0
    return MASK64 >> (64 - (n - base))


class State:
    def __init__(self):
        self.live = [0] * WORLD_WORDS
        self.n_worlds = 0
        self.has_domain = 0


def state_init(s, n_worlds):
    if n_worlds == 0:
        return ERR_EMPTY_DOMAIN
    if n_worlds > MAX_WORLDS:
        return ERR_DOMAIN_TOO_LARGE
    for i in range(WORLD_WORDS):
        s.live[i] = word_mask(i, n_worlds)
    s.n_worlds = n_worlds
    s.has_domain = 1
    return OK


def state_groundless(s):
    for i in range(WORLD_WORDS):
        s.live[i] = 0
    s.n_worlds = 0
    s.has_domain = 0


def eliminate(s, world):
    if s.has_domain == 0:
        return ERR_EMPTY_DOMAIN
    if world >= s.n_worlds:
        return ERR_INDEX_OUT_OF_DOMAIN
    s.live[world >> 6] &= ~(1 << (world & 63)) & MASK64
    return OK


def world_possible(s, world):
    if s.has_domain == 0 or world >= s.n_worlds:
        return 0
    return 1 if (s.live[world >> 6] >> (world & 63)) & 1 else 0


def live_count(s):
    if s.has_domain == 0:
        return 0
    return sum(popcount64(s.live[i] & word_mask(i, s.n_worlds))
               for i in range(WORLD_WORDS))


W_WORLDS, W_ABSORB = 0, 1


class Query:
    def __init__(self):
        self.ans = [0] * MAX_WORLDS
        self.n_worlds = 0
        self.dom = 0
        self.has_domain = 0


def query_init(q, n_worlds, dom):
    if n_worlds == 0 or dom == 0:
        return ERR_EMPTY_DOMAIN
    if n_worlds > MAX_WORLDS or dom > MAX_ANSWERS:
        return ERR_DOMAIN_TOO_LARGE
    for i in range(MAX_WORLDS):
        q.ans[i] = 0
    q.n_worlds = n_worlds
    q.dom = dom
    q.has_domain = 1
    return OK


def query_groundless(q, n_worlds, dom):
    st = query_init(q, n_worlds, dom)
    if st != OK:
        return st
    q.has_domain = 0
    return OK


def query_set(q, world, answer):
    if q.has_domain == 0:
        return ERR_EMPTY_DOMAIN
    if world >= q.n_worlds or answer >= q.dom:
        return ERR_INDEX_OUT_OF_DOMAIN
    q.ans[world] = answer
    return OK


class Op1:
    def __init__(self, table, dom_a, dom_out):
        self.out = list(table) + [0] * (MAX_ANSWERS - len(table))
        self.dom_a = dom_a
        self.dom_out = dom_out


class Op2:
    def __init__(self, table, dom_a, dom_b, dom_out):
        self.out = list(table) + [0] * (MAX_TABLE - len(table))
        self.dom_a = dom_a
        self.dom_b = dom_b
        self.dom_out = dom_out


def map1(op, a, out):
    if a.has_domain == 0:
        return ERR_EMPTY_DOMAIN
    if op.dom_a != a.dom:
        return ERR_INDEX_OUT_OF_DOMAIN
    if op.dom_a > MAX_ANSWERS or op.dom_out > MAX_ANSWERS or op.dom_out == 0:
        return ERR_DOMAIN_TOO_LARGE
    for i in range(MAX_ANSWERS):
        if i < op.dom_a and op.out[i] >= op.dom_out:
            return ERR_INDEX_OUT_OF_DOMAIN
    st = query_init(out, a.n_worlds, op.dom_out)
    if st != OK:
        return st
    for i in range(MAX_WORLDS):
        src = a.ans[i]
        out.ans[i] = op.out[src] if src < op.dom_a else 0
    return OK


def map2(op, a, b, out):
    if a.has_domain == 0 or b.has_domain == 0:
        return ERR_EMPTY_DOMAIN
    if a.n_worlds != b.n_worlds:
        return ERR_INDEX_OUT_OF_DOMAIN
    if op.dom_a != a.dom or op.dom_b != b.dom:
        return ERR_INDEX_OUT_OF_DOMAIN
    if (op.dom_out == 0 or op.dom_out > MAX_ANSWERS or
            op.dom_a > MAX_ANSWERS or op.dom_b > MAX_ANSWERS):
        return ERR_DOMAIN_TOO_LARGE
    if op.dom_a * op.dom_b > MAX_TABLE:
        return ERR_DOMAIN_TOO_LARGE
    for i in range(MAX_TABLE):
        if i < op.dom_a * op.dom_b and op.out[i] >= op.dom_out:
            return ERR_INDEX_OUT_OF_DOMAIN
    st = query_init(out, a.n_worlds, op.dom_out)
    if st != OK:
        return st
    for i in range(MAX_WORLDS):
        va, vb = a.ans[i], b.ans[i]
        if va < op.dom_a and vb < op.dom_b:
            out.ans[i] = op.out[va * op.dom_b + vb]
        else:
            out.ans[i] = 0
    return OK


def image(q, s):
    if s.has_domain == 0 or q.has_domain == 0:
        return ERR_EMPTY_DOMAIN, 0
    if q.n_worlds != s.n_worlds:
        return ERR_INDEX_OUT_OF_DOMAIN, 0
    img = 0
    for w in range(MAX_WORLDS):
        if w < s.n_worlds and (s.live[w >> 6] >> (w & 63)) & 1:
            a = q.ans[w]
            if a >= q.dom or a >= MAX_ANSWERS:
                return ERR_INDEX_OUT_OF_DOMAIN, 0
            img |= 1 << a
    return OK, img


def verdict_of(img, has_domain):
    if has_domain == 0:
        return GROUNDLESS
    n = popcount64(img)
    if n == 0:
        return CONTRADICTION
    if n == 1:
        return DERIVED
    return UNDETERMINED


def support_of(img, dom, has_domain):
    if has_domain == 0:
        return -1.0
    return (dom - popcount64(img)) / dom   # one rounding, as the C


def hartley_of(img):
    n = popcount64(img)
    return 0.0 if n <= 1 else math.log2(n)


def lowest_answer(img):
    for i in range(MAX_ANSWERS):
        if (img >> i) & 1:
            return i
    return 0


class Witness:
    def __init__(self):
        self.kind = W_WORLDS
        self.live = [0] * WORLD_WORDS
        self.n_worlds = 0
        self.image = 0
        self.dom = 0
        self.has_domain = 0
        self.verdict = GROUNDLESS
        self.value = 0
        self.S = 0.0
        self.H = 0.0
        self.range_a = 0
        self.range_b = 0

    def copy(self):
        w = Witness()
        w.kind = self.kind
        w.live = list(self.live)
        w.n_worlds, w.image, w.dom = self.n_worlds, self.image, self.dom
        w.has_domain, w.verdict, w.value = self.has_domain, self.verdict, self.value
        w.S, w.H = self.S, self.H
        w.range_a, w.range_b = self.range_a, self.range_b
        return w


class Ancestry:
    def __init__(self):
        self.ids = 0
        self.remaining = [0] * MAX_GUESSES

    def copy(self):
        a = Ancestry()
        a.ids = self.ids
        a.remaining = list(self.remaining)
        return a

    def add(self, guess_id, remaining):
        if guess_id >= MAX_GUESSES:
            return ERR_GUESS_ID_OUT_OF_RANGE
        self.ids |= 1 << guess_id
        self.remaining[guess_id] = remaining
        return OK

    def bits(self):
        # summed over the DISTINCT guesses, so a shared ancestor counts once
        total = 0.0
        for i in range(MAX_GUESSES):
            if (self.ids >> i) & 1 and self.remaining[i] > 1:
                total += math.log2(self.remaining[i])
        return total


class Result:
    def __init__(self):
        self.verdict = GROUNDLESS
        self.value = 0
        self.S = 0.0
        self.H = 0.0
        self.intensity = 0.0
        self.ancestry = Ancestry()
        self.witness = Witness()
        self.allowed = 0


def ask(q, s, out):
    out.ancestry = Ancestry()
    out.intensity = 0.0
    out.allowed = 0
    out.value = 0

    if s.has_domain == 0 or q.has_domain == 0:
        out.witness = Witness()
        out.witness.dom = q.dom
        out.witness.S = -1.0
        out.witness.verdict = GROUNDLESS
        out.verdict, out.S, out.H = GROUNDLESS, -1.0, 0.0
        return OK

    st, img = image(q, s)
    if st != OK:
        return st

    out.verdict = verdict_of(img, 1)
    out.S = support_of(img, q.dom, 1)
    out.H = hartley_of(img)
    out.value = lowest_answer(img) if out.verdict == DERIVED else 0

    w = Witness()
    w.live = list(s.live)
    w.n_worlds = s.n_worlds
    w.image = img
    w.dom = q.dom
    w.has_domain = 1
    w.verdict = out.verdict
    w.value = out.value
    w.S = out.S
    w.H = out.H
    out.witness = w
    return OK


def witness_check(q, w):
    """Separate dumb loop. Shares no verdict-producing code with ask()."""
    if w.kind != W_WORLDS:
        return 0
    if q.dom != w.dom:
        return 0
    if w.has_domain == 0:
        return 1 if (w.verdict == GROUNDLESS and w.image == 0 and
                     w.n_worlds == 0 and w.S == -1.0 and w.H == 0.0) else 0
    if q.n_worlds != w.n_worlds or w.n_worlds > MAX_WORLDS:
        return 0
    for i in range(WORLD_WORDS):
        if w.live[i] & ~word_mask(i, w.n_worlds) & MASK64:
            return 0
    img = 0
    for i in range(MAX_WORLDS):
        if i < w.n_worlds and (w.live[i >> 6] >> (i & 63)) & 1:
            a = q.ans[i]
            if a >= q.dom:
                return 0
            img |= 1 << a
    if img != w.image:
        return 0
    n = popcount64(img)
    v_expect = CONTRADICTION if n == 0 else (DERIVED if n == 1 else UNDETERMINED)
    if v_expect != w.verdict:
        return 0
    if v_expect == DERIVED and w.value != lowest_answer(img):
        return 0
    s_expect = (w.dom - n) / w.dom
    h_expect = 0.0 if n <= 1 else math.log2(n)
    if not (s_expect - 1e-12 <= w.S <= s_expect + 1e-12):
        return 0
    if not (h_expect - 1e-12 <= w.H <= h_expect + 1e-12):
        return 0
    return 1


def rng_range(q, s):
    if q.has_domain != 0:
        return image(q, s)
    if q.dom == 0 or q.dom > MAX_ANSWERS:
        return ERR_DOMAIN_TOO_LARGE, 0
    return OK, sum(1 << i for i in range(q.dom))


def ask2(op, a, b, s, composed, out):
    if op.dom_a != a.dom or op.dom_b != b.dom:
        return ERR_INDEX_OUT_OF_DOMAIN
    if a.n_worlds != b.n_worlds:
        return ERR_INDEX_OUT_OF_DOMAIN

    if a.has_domain != 0 and b.has_domain != 0:
        st = map2(op, a, b, composed)
        if st != OK:
            return st
        return ask(composed, s, out)

    st = query_groundless(composed, a.n_worlds, op.dom_out)
    if st != OK:
        return st

    out.ancestry = Ancestry()
    out.intensity = 0.0
    out.allowed = 0
    out.value = 0

    if s.has_domain == 0:
        out.witness = Witness()
        out.witness.dom = op.dom_out
        out.witness.S = -1.0
        out.witness.verdict = GROUNDLESS
        out.verdict, out.S, out.H = GROUNDLESS, -1.0, 0.0
        return OK

    st, ra = rng_range(a, s)
    if st != OK:
        return st
    st, rb = rng_range(b, s)
    if st != OK:
        return st

    img = 0
    for x in range(MAX_ANSWERS):
        if x >= op.dom_a or not (ra >> x) & 1:
            continue
        for y in range(MAX_ANSWERS):
            if y >= op.dom_b or not (rb >> y) & 1:
                continue
            v = op.out[x * op.dom_b + y]
            if v >= op.dom_out:
                return ERR_INDEX_OUT_OF_DOMAIN
            img |= 1 << v

    w = Witness()
    w.kind = W_ABSORB
    w.image = img
    w.dom = op.dom_out
    w.range_a, w.range_b = ra, rb
    out.witness = w

    if popcount64(img) == 1:
        w.has_domain = 1
        w.verdict = DERIVED
        w.value = lowest_answer(img)
        w.S = (op.dom_out - 1) / op.dom_out
        w.H = 0.0
        out.verdict, out.value, out.S, out.H = DERIVED, w.value, w.S, 0.0
        return OK

    w.has_domain = 0
    w.image = 0
    w.verdict = GROUNDLESS
    w.value = 0
    w.S = -1.0
    w.H = 0.0
    out.verdict, out.S, out.H = GROUNDLESS, -1.0, 0.0
    return OK


def witness_check_absorb(op, w):
    if w.kind != W_ABSORB:
        return 0
    if w.dom != op.dom_out or op.dom_out == 0 or op.dom_out > MAX_ANSWERS:
        return 0
    if (op.dom_a == 0 or op.dom_b == 0 or
            op.dom_a > MAX_ANSWERS or op.dom_b > MAX_ANSWERS):
        return 0
    for x in range(MAX_ANSWERS):
        if x >= op.dom_a and (w.range_a >> x) & 1:
            return 0
        if x >= op.dom_b and (w.range_b >> x) & 1:
            return 0
    if w.range_a == 0 or w.range_b == 0:
        return 0
    img = 0
    for x in range(MAX_ANSWERS):
        if x >= op.dom_a or not (w.range_a >> x) & 1:
            continue
        for y in range(MAX_ANSWERS):
            if y >= op.dom_b or not (w.range_b >> y) & 1:
                continue
            if op.out[x * op.dom_b + y] >= op.dom_out:
                return 0
            img |= 1 << op.out[x * op.dom_b + y]
    if popcount64(img) != 1:
        return 0
    if w.has_domain != 1 or w.verdict != DERIVED:
        return 0
    if w.image != img or w.value != lowest_answer(img):
        return 0
    s_expect = (w.dom - 1) / w.dom
    if not (s_expect - 1e-12 <= w.S <= s_expect + 1e-12):
        return 0
    if w.H != 0.0:
        return 0
    return 1


def observe(s, q, answer):
    if s.has_domain == 0:
        return ERR_EMPTY_DOMAIN
    if q.n_worlds != s.n_worlds:
        return ERR_INDEX_OUT_OF_DOMAIN
    if answer >= q.dom:
        return ERR_INDEX_OUT_OF_DOMAIN
    for w in range(MAX_WORLDS):
        if w < s.n_worlds and q.ans[w] != answer:
            s.live[w >> 6] &= ~(1 << (w & 63)) & MASK64
    return OK


MAX_ROUNDS = MAX_WORLDS


class Settle:
    def __init__(self):
        self.rounds = 0
        self.productive = 0
        self.informative = 0
        self.removed = [0] * MAX_ROUNDS
        self.live_at_start = 0
        self.live_now = 0
        self.image = 0
        self.settled = 0
        self.contradicted = 0


def settle_begin(t, s, target):
    for i in range(MAX_ROUNDS):
        t.removed[i] = 0
    t.rounds = 0
    t.productive = 0
    t.informative = 0
    t.live_at_start = live_count(s)
    t.live_now = t.live_at_start
    t.image = 0
    if s.has_domain != 0 and target.has_domain != 0:
        st, t.image = image(target, s)
        if st != OK:
            return st
    t.settled = 0
    t.contradicted = 1 if (s.has_domain != 0 and t.live_at_start == 0) else 0
    return OK


def settle_step(t, s, target, constraint, answer):
    if t.rounds >= MAX_ROUNDS:
        return ERR_DOMAIN_TOO_LARGE
    before = live_count(s)
    img_before = t.image
    st = observe(s, constraint, answer)
    if st != OK:
        return st
    after = live_count(s)
    img_after = 0
    if s.has_domain != 0 and target.has_domain != 0:
        st, img_after = image(target, s)
        if st != OK:
            return st
    t.removed[t.rounds] = before - after
    t.rounds += 1
    t.live_now = after
    t.image = img_after
    if before != after:
        t.productive += 1
    if img_after != img_before:
        t.informative += 1
        t.settled = 0
    else:
        t.settled = 1
    if after == 0 and s.has_domain != 0:
        t.contradicted = 1
    return OK


def settle_commit(t, target, s, out):
    st = ask(target, s, out)
    if st != OK:
        return st
    out.allowed = 1 if out.verdict == DERIVED else 0
    if out.allowed == 0:
        out.value = 0
    return OK


MAX_SITUATIONS = MAX_WORLDS
MAX_QUESTIONS = 64


class Partition:
    def __init__(self):
        self.cell = [0] * MAX_SITUATIONS
        self.n_situations = 0
        self.n_cells = 0


def refine(questions, n_situations, out):
    n_questions = len(questions)
    if n_situations == 0:
        return ERR_EMPTY_DOMAIN
    if n_situations > MAX_SITUATIONS or n_questions > MAX_QUESTIONS:
        return ERR_DOMAIN_TOO_LARGE
    for q in questions:
        if q.has_domain == 0:
            return ERR_EMPTY_DOMAIN
        if q.n_worlds != n_situations:
            return ERR_INDEX_OUT_OF_DOMAIN
    for i in range(MAX_SITUATIONS):
        out.cell[i] = 0
    out.n_situations = n_situations
    out.n_cells = 0
    for i in range(n_situations):
        placed = 0
        for j in range(i):
            same = 1
            for q in questions:
                if q.ans[i] != q.ans[j]:
                    same = 0
                    break
            if same:
                out.cell[i] = out.cell[j]
                placed = 1
                break
        if not placed:
            if out.n_cells >= MAX_WORLDS:
                return ERR_DOMAIN_TOO_LARGE
            out.cell[i] = out.n_cells
            out.n_cells += 1
    return OK


def project(p, situation_q, out):
    if situation_q.has_domain == 0:
        return ERR_EMPTY_DOMAIN
    if situation_q.n_worlds != p.n_situations or p.n_cells == 0:
        return ERR_INDEX_OUT_OF_DOMAIN
    answer_of = [0] * MAX_WORLDS
    seen = [0] * MAX_WORLDS
    for i in range(p.n_situations):
        c, a = p.cell[i], situation_q.ans[i]
        if c >= p.n_cells or a >= situation_q.dom:
            return ERR_INDEX_OUT_OF_DOMAIN
        if not seen[c]:
            seen[c], answer_of[c] = 1, a
        elif answer_of[c] != a:
            return ERR_INDEX_OUT_OF_DOMAIN
    st = query_init(out, p.n_cells, situation_q.dom)
    if st != OK:
        return st
    for i in range(p.n_cells):
        out.ans[i] = answer_of[i]
    return OK


def distinguishes(p, situation_q):
    if situation_q.has_domain == 0 or situation_q.n_worlds != p.n_situations:
        return 0
    answer_of = [0] * MAX_WORLDS
    seen = [0] * MAX_WORLDS
    for i in range(p.n_situations):
        c, a = p.cell[i], situation_q.ans[i]
        if c >= p.n_cells:
            return 0
        if not seen[c]:
            seen[c], answer_of[c] = 1, a
        elif answer_of[c] != a:
            return 1
    return 0


ASK_GUARANTEE, ASK_OPPORTUNITY = 0, 1


class Probe:
    def __init__(self):
        self.surviving = [0] * MAX_ANSWERS
        self.reachable = 0
        self.worst_case_removed = 0
        self.best_case_removed = 0
        self.sufficient = 0
        self.irrelevant = 0


def probe(s, target, question, out):
    if (s.has_domain == 0 or target.has_domain == 0
            or question.has_domain == 0):
        return ERR_EMPTY_DOMAIN
    if question.n_worlds != s.n_worlds or target.n_worlds != s.n_worlds:
        return ERR_INDEX_OUT_OF_DOMAIN

    img = [0] * MAX_ANSWERS
    whole = 0
    live = 0
    for a in range(MAX_ANSWERS):
        out.surviving[a] = 0
    out.reachable = 0

    for w in range(MAX_WORLDS):
        if w >= s.n_worlds:
            continue
        if not (s.live[w >> 6] >> (w & 63)) & 1:
            continue
        qa, ta = question.ans[w], target.ans[w]
        if qa >= question.dom or ta >= target.dom:
            return ERR_INDEX_OUT_OF_DOMAIN
        live += 1
        out.surviving[qa] += 1
        out.reachable |= 1 << qa
        img[qa] |= 1 << ta
        whole |= 1 << ta

    out.sufficient = 1
    out.irrelevant = 1
    most, fewest, seen = 0, 1 << 32, 0
    for a in range(MAX_ANSWERS):
        if not (out.reachable >> a) & 1:
            continue
        seen = 1
        most = max(most, out.surviving[a])
        fewest = min(fewest, out.surviving[a])
        if popcount64(img[a]) != 1:
            out.sufficient = 0
        if img[a] != whole:
            out.irrelevant = 0

    if seen == 0:
        out.worst_case_removed = 0
        out.best_case_removed = 0
        out.sufficient = 0
        out.irrelevant = 1
        return OK
    out.worst_case_removed = live - most
    out.best_case_removed = live - fewest
    return OK


def choose(s, target, questions, policy, out_probe):
    n = len(questions)
    if s.has_domain == 0 or target.has_domain == 0:
        return ERR_EMPTY_DOMAIN, n
    st, img = image(target, s)
    if st != OK:
        return st, n
    if popcount64(img) <= 1:
        return OK, n

    have, best_score, best_i, best = 0, 0, n, None
    for i in range(n):
        if questions[i].has_domain == 0:
            continue          # not a question; skipped, not fatal
        p = Probe()
        st = probe(s, target, questions[i], p)
        if st != OK:
            return st, n
        if p.irrelevant:
            continue
        if p.sufficient:
            out_probe.__dict__.update(p.__dict__)
            return OK, i
        score = p.best_case_removed if policy == ASK_OPPORTUNITY \
            else p.worst_case_removed
        if have == 0 or score > best_score:
            have, best_score, best_i, best = 1, score, i, p
    if have:
        out_probe.__dict__.update(best.__dict__)
        return OK, best_i
    return OK, n


def checkpoint(s):
    c = State()
    c.live = list(s.live)
    c.n_worlds = s.n_worlds
    c.has_domain = s.has_domain
    return c


def retract(s, cp, anc, guess_id):
    if guess_id >= MAX_GUESSES:
        return ERR_GUESS_ID_OUT_OF_RANGE
    s.live = list(cp.live)
    s.n_worlds = cp.n_worlds
    s.has_domain = cp.has_domain
    anc.ids &= ~(1 << guess_id) & MASK64
    anc.remaining[guess_id] = 0
    return OK


def intensity_of(s, tau, policy):
    if policy == INTENSITY_HEADROOM:
        if tau >= 1.0:
            return 0.0
        return (s - tau) / (1.0 - tau)
    return s


def speculate(s, q, tau, seed, policy, guess_id, ancestry, out):
    if not (tau >= 0.0) or not (tau <= 1.0):   # rejects NaN; see the C
        return ERR_BAD_THRESHOLD
    if guess_id >= MAX_GUESSES:
        return ERR_GUESS_ID_OUT_OF_RANGE

    st = ask(q, s, out)
    if st != OK:
        return st
    out.ancestry = ancestry.copy()
    out.allowed = 0
    out.intensity = 0.0

    if out.verdict != UNDETERMINED:
        return OK
    if out.S < tau:
        return OK

    n = popcount64(out.witness.image)
    if n < 2:
        return ERR_INTERNAL_INVARIANT
    rng_state, r = splitmix64(seed)
    k = r % n
    pick, seen = 0, 0
    for i in range(MAX_ANSWERS):
        if (out.witness.image >> i) & 1:
            if seen == k:
                pick = i
            seen += 1

    for w in range(MAX_WORLDS):
        if w < s.n_worlds and q.ans[w] != pick:
            s.live[w >> 6] &= ~(1 << (w & 63)) & MASK64

    st = ancestry.add(guess_id, n)
    if st != OK:
        return st
    out.ancestry = ancestry.copy()
    out.verdict = SPECULATED
    out.value = pick
    out.intensity = intensity_of(out.S, tau, policy)
    out.allowed = 1
    return OK


