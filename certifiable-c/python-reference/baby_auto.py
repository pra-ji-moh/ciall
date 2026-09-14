"""An autonomous learner.

No curriculum. Nothing tells it what to acquire or when. It is given goals
and an environment, and it works out the rest:

    cannot even PHRASE the goal    -> acquire the smallest capacity that
                                      would let it, and try again
    can phrase it, cannot settle   -> work out which single measurement
                                      would settle it, and take that one
    settled                        -> answer, with a witness
    the evidence contradicts       -> find what it was told that cannot be
                                      true, drop it, carry on

Every one of those decisions is counted, not scored. The only preference
in the whole loop is which capacity to acquire when several would do, and
it is named as a preference rather than buried.

Runs on kernel_py, the code verify_reason.py checks.

WHAT IT LIVES IN
    Two piles and a label, 32 situations. a and b are 0..3, and the thing
    is called a cup or a mug.

WHAT MAKES THIS TERMINATE
    Every pass through the loop does exactly one of four things, and each
    has a bound that does not depend on what the world says:

      acquire   strictly refines the partition. At most once per capacity.
      measure   takes a capacity not already in memory and not distrusted.
      recover   drops one claim AND distrusts its source for the scene, so
                that source can never be measured again this scene.
      return

    So each capacity is measured at most once and recovered at most once
    per scene, and every pass consumes something finite.

    An earlier version of this docstring said instead that every pass
    "makes progress that cannot be undone". That was false: recovery IS an
    undo. With a sensor that returns an impossible value for the one
    measurement that would settle the goal, the learner measured it,
    contradicted, retracted, and measured it again, forever. It was found
    by writing the C port, which requires every loop to carry a stated
    bound -- stating the bound exposed that the proof did not hold.
    verify_auto.py had not caught it because every sensor it used was
    truthful, so recovery never ran inside the loop.
"""

from kernel_py import *   # noqa: F401,F403

N_SIT = 32


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


CAPS = {
    'more':    sit_q(lambda s: 0 if a_of(s) < b_of(s)
                     else (1 if a_of(s) == b_of(s) else 2), 3),
    'close':   sit_q(lambda s: 1 if abs(a_of(s) - b_of(s)) <= 1 else 0, 2),
    'name':    sit_q(lambda s: name_of(s), 2),
    'sum':     sit_q(lambda s: a_of(s) + b_of(s), 7),
    'exact_a': sit_q(lambda s: a_of(s), 4),
    'exact_b': sit_q(lambda s: b_of(s), 4),
}
ORDER = ['more', 'close', 'name', 'sum', 'exact_a', 'exact_b']

# A glance gives these. The rest are acts: you have to go and count.
IMMEDIATE = ('more', 'close', 'name')


def _live(st, w):
    return (st.live[w >> 6] >> (w & 63)) & 1


def jointly_relevant(st, target, qs):
    """Could the candidate questions, answered TOGETHER, narrow the target?

    choose() looks one question ahead, and misses information that only
    pays off in combination: for "is s divisible by 7" over s = 16*hi + lo,
    neither digit alone narrows the answer, both together settle it. The
    learner used to stop there and report "nothing available would settle
    it", which was false. Exact: group live worlds by their answers to all
    candidates at once; if any group's target values are fewer than the
    whole live set's, measuring can help. Found by the C port's first run
    on 256 situations (test_learner.c)."""
    live = [w for w in range(st.n_worlds) if _live(st, w)]
    whole = {target.ans[w] for w in live}
    groups = {}
    for w in live:
        groups.setdefault(tuple(q.ans[w] for q in qs), set()).add(target.ans[w])
    return any(g != whole for g in groups.values())


class Learner:
    def __init__(self):
        self.caps = []
        self.memory = []        # (capacity, answer) for the scene in front of it
        # Sources that contradicted everything else this scene. Asking a
        # source again after it has handed back an impossible value only
        # gets the same value back, and asking again is what looped.
        self.distrusted = set()
        self.measurements = 0
        self.acquisitions = 0
        self.log = []

    # ---- what it can currently tell apart ---------------------------
    def partition(self, extra=None):
        names = self.caps + ([extra] if extra else [])
        p = Partition()
        assert refine([CAPS[n] for n in names], N_SIT, p) == OK
        return p

    def state(self, p):
        """Rebuilt from what it OBSERVED, never from what it concluded.
        That is what lets a newly acquired distinction pay off against a
        memory that is already in hand: the observation is replayed onto
        the finer partition rather than being stuck at the resolution it
        was first understood at."""
        st = State()
        assert state_init(st, p.n_cells) == OK
        for cap, ans in self.memory:
            pq = Query()
            if project(p, CAPS[cap], pq) != OK:
                continue
            observe(st, pq, ans)
        return st

    # ---- acquisition ------------------------------------------------
    def candidates(self):
        p = self.partition()
        out = []
        for n in ORDER:
            if n in self.caps:
                continue
            if distinguishes(p, CAPS[n]):
                out.append(n)
        return out

    def pick_capacity(self, goal):
        """POLICY, and the only one in this file. Among capacities that
        would let it phrase the goal, take the one that refines LEAST:
        acquire no more power to tell things apart than the question
        actually needs. If none of them suffices alone, take the smallest
        refining one and come back -- that terminates, since each pass
        strictly refines a finite partition.

        Nothing derives this preference. Preferring the largest refinement
        would be just as consistent with the axiom, and would produce a
        learner that jumps to exact counting immediately. That would not
        be wrong. It would be a different learner."""
        cands = self.candidates()
        if not cands:
            return None
        enabling = [c for c in cands
                    if project(self.partition(c), goal, Query()) == OK]
        pool = enabling if enabling else cands
        return min(pool, key=lambda c: (self.partition(c).n_cells,
                                        ORDER.index(c)))

    def acquire(self, name):
        before = self.partition().n_cells
        self.caps.append(name)
        after = self.partition().n_cells
        self.acquisitions += 1
        self.log.append(f'acquire {name:<8} worlds {before} -> {after}')

    # ---- a new scene -------------------------------------------------
    def glance(self, truth):
        self.memory = [(c, CAPS[c].ans[truth])
                       for c in self.caps if c in IMMEDIATE]
        self.distrusted = set()     # trust resets with the scene

    def measure(self, cap, truth):
        self.memory.append((cap, CAPS[cap].ans[truth]))
        self.measurements += 1
        self.log.append(f'measure {cap}')

    # ---- the loop ----------------------------------------------------
    def pursue(self, label, goal, truth):
        self.log = []
        self.glance(truth)
        start_m, start_a = self.measurements, self.acquisitions

        while True:
            p = self.partition()
            gq = Query()

            if project(p, goal, gq) != OK:
                nxt = self.pick_capacity(goal)
                if nxt is None:
                    return self._report(label, 'cannot form the question and '
                                        'nothing left to learn', None, p,
                                        start_m, start_a)
                self.acquire(nxt)
                continue

            st = self.state(p)
            r = Result()
            assert ask(gq, st, r) == OK

            if r.verdict == CONTRADICTION:
                dropped = self.recover(p, gq)
                if dropped is None:
                    return self._report(label, 'contradicted beyond repair',
                                        None, p, start_m, start_a)
                continue

            if r.verdict == DERIVED:
                return self._report(label, 'derived', r, p, start_m, start_a,
                                    gq)

            # expressible, undetermined: what would settle it?
            held = [c for c in self.caps
                    if c not in [m[0] for m in self.memory]
                    and c not in self.distrusted]
            projected, keys = [], []
            for c in held:
                mq = Query()
                if project(p, CAPS[c], mq) == OK:
                    projected.append(mq)
                    keys.append(c)
            pr = Probe()
            pick = len(projected)
            if projected:
                _, pick = choose(st, gq, projected, ASK_GUARANTEE, pr)
            if pick == len(projected) and projected and                     jointly_relevant(st, gq, projected):
                # No single question narrows the goal, but together they
                # would. Take the one ruling out the most worlds in the
                # worst case; lowest index on a tie.
                best = 0
                for i, mq in enumerate(projected):
                    assert probe(st, gq, mq, pr) == OK
                    if pr.worst_case_removed > best:
                        best, pick = pr.worst_case_removed, i
                assert pick < len(projected), 'jointly relevant, yet nothing splits'
            if pick < len(projected):
                self.measure(keys[pick], truth)
                continue

            nxt = self.pick_capacity(goal)
            if nxt is None:
                return self._report(label, 'undetermined, and nothing '
                                    'available would settle it', r, p,
                                    start_m, start_a, gq)
            self.acquire(nxt)

    def recover(self, p, gq):
        """Something it was told cannot be true alongside the rest. Drop
        the most recent claim and see if the world comes back. Most recent
        first is a POLICY: the machinery says the set is jointly
        impossible, not which member is the liar."""
        while self.memory:
            bad = self.memory.pop()
            # Distrusted whether or not it turns out to be the culprit:
            # most-recent-first is a crude policy and may drop an honest
            # claim, and distrusting it then loses information. It does not
            # lose termination, which is what this line is for.
            self.distrusted.add(bad[0])
            st = self.state(p)
            if live_count(st) > 0:
                self.log.append(f'RETRACT {bad[0]}={bad[1]} and stop asking '
                                f'it (it cannot be true alongside the rest)')
                return bad
        return None

    def _report(self, label, outcome, r, p, start_m, start_a, gq=None):
        proved = (r is not None and gq is not None
                  and witness_check(gq, r.witness))
        return {
            'label': label, 'outcome': outcome,
            'value': r.value if (r and r.verdict == DERIVED) else None,
            'H': r.H if r else 0.0,
            'worlds': p.n_cells,
            'measured': self.measurements - start_m,
            'acquired': self.acquisitions - start_a,
            'proved': proved, 'log': list(self.log),
        }


# =====================================================================
GOALS = [
    ('is a bigger than b',
     sit_q(lambda s: 1 if a_of(s) > b_of(s) else 0, 2), (2 << 3) | (0 << 1) | 0),
    ('are they within one',
     sit_q(lambda s: 1 if abs(a_of(s) - b_of(s)) <= 1 else 0, 2),
     (1 << 3) | (2 << 1) | 1),
    ('is it called a cup',
     sit_q(lambda s: name_of(s), 2), (0 << 3) | (3 << 1) | 1),
    ('is a at least two',
     sit_q(lambda s: 1 if a_of(s) >= 2 else 0, 2), (3 << 3) | (1 << 1) | 1),
    ('is the total four',
     sit_q(lambda s: 1 if a_of(s) + b_of(s) == 4 else 0, 2),
     (3 << 3) | (1 << 1) | 1),
    ('is the total even',
     sit_q(lambda s: 1 if (a_of(s) + b_of(s)) % 2 == 0 else 0, 2),
     (2 << 3) | (2 << 1) | 0),
]

def _demo():
    print(__doc__.split('WHAT IT LIVES IN')[0].strip())
    print()
    print('=' * 74)
    print(f'{"goal":<24}{"worlds":>8}{"acquired":>10}{"measured":>10}'
          f'{"outcome":>12}{"proved":>8}')
    print('=' * 74)

    kid = Learner()
    for label, goal, truth in GOALS:
        res = kid.pursue(label, goal, truth)
        val = '' if res['value'] is None else f" = {res['value']}"
        print(f"{label:<24}{res['worlds']:>8}{res['acquired']:>10}"
              f"{res['measured']:>10}{res['outcome']+val:>12}"
              f"{('yes' if res['proved'] else '-'):>8}")
        for line in res['log']:
            print(f'      {line}')

    print()
    print(f'  it now holds: {", ".join(kid.caps)}')
    print(f'  total acquisitions {kid.acquisitions}, total measurements '
          f'{kid.measurements}')
    print()
    print('  Nobody told it the order. Each capacity was acquired at the moment')
    print('  a goal could not be phrased without it, and never before.')

    # =====================================================================
    print()
    print('THE SAME EVIDENCE, A FINER VOCABULARY')
    print('-' * 74)

    coarse = Learner()
    coarse.caps = ['more', 'close', 'name']
    truth = (3 << 3) | (1 << 1) | 1                # a=3, b=1, a cup
    coarse.glance(truth)
    goal = GOALS[3][1]                             # is a at least two
    p = coarse.partition()
    print(f'  glance recorded: {[(c, v) for c, v in coarse.memory]}')
    print(f'  with {len(coarse.caps)} capacities ({p.n_cells} worlds): '
          f'{"can phrase it" if project(p, goal, Query()) == OK else "cannot even phrase it"}')

    coarse.acquire('exact_a')
    p = coarse.partition()
    st = coarse.state(p)
    gq = Query()
    assert project(p, goal, gq) == OK
    r = Result()
    assert ask(gq, st, r) == OK
    print(f'  after acquiring exact_a ({p.n_cells} worlds), with NO new '
          f'measurement: {"derived" if r.verdict == DERIVED else "still cannot say"}'
          f' = {r.value}')
    print()
    print('  It never counted the pile. It learned to ask about counts, replayed')
    print('  the glance it already had, and the answer was sitting in it. The')
    print('  vocabulary was the missing part, not the evidence.')

    # =====================================================================
    print()
    print('BEING MISLED, AND GETTING OUT OF IT')
    print('-' * 74)

    liar = Learner()
    liar.caps = ['more', 'close', 'name', 'exact_a', 'exact_b']
    truth = (3 << 3) | (1 << 1) | 1
    liar.glance(truth)
    liar.memory.append(('exact_a', 3))
    liar.memory.append(('exact_b', 1))
    liar.memory.append(('more', 0))            # a report that a is the SMALLER pile
    print('  told, after counting 3 and 1, that the first pile is the smaller one')

    p = liar.partition()
    st = liar.state(p)
    r = Result()
    gq = Query()
    project(p, GOALS[4][1], gq)
    ask(gq, st, r)
    print(f'  state: {VNAME[r.verdict]}, {live_count(st)} worlds standing')
    dropped = liar.recover(p, gq)
    st = liar.state(p)
    ask(gq, st, r)
    print(f'  after recovery: dropped {dropped[0]}={dropped[1]}, '
          f'{live_count(st)} worlds standing, the total is four: '
          f'{VNAME[r.verdict]} = {r.value}')
    print()
    print('  It did not lower a confidence and carry on. The claims were jointly')
    print('  impossible, which is a verdict of its own, and it dropped one and')
    print('  said which. Choosing the most recent is a policy; that the set was')
    print('  impossible is not.')


if __name__ == '__main__':
    _demo()
