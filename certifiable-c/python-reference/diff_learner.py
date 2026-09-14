"""The Python reference learner, printed in exactly the form diff_learner.c
prints, so the two can be diffed line for line. See diff_learner.c.
"""

import sys

import baby_auto as B

IDX = {n: i for i, n in enumerate(B.ORDER)}
OUT = {
    'derived': 0,
    'cannot form the question and nothing left to learn': 1,
    'undetermined, and nothing available would settle it': 2,
    'contradicted beyond repair': 3,
}


class Rec(B.Learner):
    def __init__(self, liar=None):
        super().__init__()
        self.liar = liar
        self.ev = []

    def acquire(self, name):
        before = self.partition().n_cells
        super().acquire(name)
        after = self.partition().n_cells
        self.ev.append(f'acq:{IDX[name]}:{before}:{after}')

    def measure(self, cap, truth):
        v = B.CAPS[cap].ans[truth]
        if cap == self.liar:
            v = (v + 1) % B.CAPS[cap].dom
        self.memory.append((cap, v))
        self.measurements += 1
        self.ev.append(f'mea:{IDX[cap]}:{v}')

    def glance(self, truth):
        super().glance(truth)
        if self.liar in [c for c, _ in self.memory]:
            raise RuntimeError('liars are only ever measured capacities')

    def recover(self, p, gq):
        bad = super().recover(p, gq)
        if bad is not None:
            self.ev.append(f'ret:{IDX[bad[0]]}:{bad[1]}')
        return bad


def line(cfg, g, truth, liar, kid, res):
    o = OUT[res['outcome']]
    derived = o == 0
    return (f"{cfg} g{g} t{truth} l{liar} st0 out{o} "
            f"val{res['value'] if derived else 0} w{res['worlds']} "
            f"m{res['measured']} a{res['acquired']} "
            f"p{1 if (derived and res['proved']) else 0} "
            f"H{res['H']:.6f} |" + ''.join(' ' + e for e in kid.ev))


goals = [q for _, q, _ in B.GOALS]
labels = [lbl for lbl, _, _ in B.GOALS]
out = []

for g, goal in enumerate(goals):
    for t in range(B.N_SIT):
        kid = Rec()
        res = kid.pursue(labels[g], goal, t)
        out.append(line('A', g, t, -1, kid, res))

for g, goal in enumerate(goals):
    for t in range(B.N_SIT):
        for c in (3, 4, 5):
            kid = Rec(liar=B.ORDER[c])
            kid.caps = list(B.ORDER)
            res = kid.pursue(labels[g], goal, t)
            out.append(line('B', g, t, c, kid, res))

full_order = list(B.ORDER)
for k in range(1, 6):
    B.ORDER = full_order[:k]
    for g, goal in enumerate(goals):
        for t in range(B.N_SIT):
            kid = Rec()
            res = kid.pursue(labels[g], goal, t)
            out.append(line('D', g, t, k, kid, res))
B.ORDER = full_order

kid = Rec()
for g, (lbl, goal, truth) in enumerate(B.GOALS):
    kid.ev = []
    res = kid.pursue(lbl, goal, truth)
    out.append(line('C', g, truth, -1, kid, res))

class Noisy(Rec):
    """Every sensor reports a fixed arbitrary value, whatever the truth."""

    def __init__(self, readings):
        super().__init__()
        self.readings = readings

    def glance(self, truth):
        self.memory = [(c, self.readings[c]) for c in self.caps
                       if c in B.IMMEDIATE]
        self.distrusted = set()

    def measure(self, cap, truth):
        v = self.readings[cap]
        self.memory.append((cap, v))
        self.measurements += 1
        self.ev.append(f'mea:{IDX[cap]}:{v}')


def lcg_next(x):
    return (x * 1103515245 + 12345) & 0xFFFFFFFF


lcg = 2026
for run in range(2000):
    held = []
    for c in range(6):
        lcg = lcg_next(lcg)
        if (lcg >> 16) & 1:
            held.append(B.ORDER[c])
    readings = {}
    for c in range(6):
        lcg = lcg_next(lcg)
        readings[B.ORDER[c]] = (lcg >> 16) % B.CAPS[B.ORDER[c]].dom
    lcg = lcg_next(lcg)
    g = (lcg >> 16) % 6
    kid = Noisy(readings)
    kid.caps = held
    res = kid.pursue(labels[g], goals[g], 0)
    out.append(line('E', g, run, -1, kid, res))

sys.stdout.write('\n'.join(out) + '\n')
