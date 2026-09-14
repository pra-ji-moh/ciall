"""The Python reference kernel, driven by the same generator as
diff_kernel.c and printed in exactly the same form. See diff_kernel.c.
"""

import sys

from kernel_py import *   # noqa: F401,F403

TRIALS = 3000
M = (1 << 64) - 1
X = 20260911


def rnd(n):
    global X
    X = (X * 6364136223846793005 + 1442695040888963407) & M
    return (X >> 33) % n


def hex64(v):
    return f'{v >> 32:08x}{v & 0xFFFFFFFF:08x}'


def rand_query(n, dom):
    q = Query()
    query_init(q, n, dom)
    for w in range(n):
        query_set(q, w, rnd(dom))
    return q


out = []
for i in range(TRIALS):
    n = 1 + rnd(256)
    da = 1 + rnd(5)
    db = 1 + rnd(5)
    dout = 1 + rnd(5)
    QA = rand_query(n, da)
    QB = rand_query(n, db)
    OP = Op2([rnd(dout) for _ in range(da * db)], da, db, dout)

    s = State()
    state_init(s, n)
    for w in range(n):
        if rnd(10) < 4:
            eliminate(s, w)

    line = f'T{i} n{n}'

    COMP = Query()
    st = map2(OP, QA, QB, COMP)
    r = Result()
    ask(COMP, s, r)
    line += (f' | m{st} v{r.verdict} x{r.value} S{r.S:.6f} H{r.H:.6f} '
             f'i{hex64(r.witness.image)} wc{witness_check(COMP, r.witness)} '
             f'lc{live_count(s)}')
    tw = r.witness.copy()
    tw.image ^= 1
    line += f' tam{witness_check(COMP, tw)}'
    tw = r.witness.copy()
    tw.live[WORLD_WORDS - 1] ^= 1 << 63
    line += f' tmw{witness_check(COMP, tw)}'

    st = observe(s, QA, rnd(da))
    r = Result()
    ask(QB, s, r)
    line += f' | o{st} v{r.verdict} x{r.value} S{r.S:.6f} lc{live_count(s)}'

    QP = rand_query(n, 1 + rnd(5))
    pr = Probe()
    st = probe(s, QB, QP, pr)
    line += (f' | p{st} r{hex64(pr.reachable)} wr{pr.worst_case_removed} '
             f'br{pr.best_case_removed} su{pr.sufficient} ir{pr.irrelevant} '
             f'ss{sum(pr.surviving)}')
    pol = rnd(2)
    st, idx = choose(s, QB, [QA, QP, COMP], pol, Probe())
    line += f' c{st} ix{idx}'

    nq = rnd(4)
    qs = []
    for k in range(nq):
        dom = 1 + rnd(4)
        qs.append(rand_query(n, dom))
    p = Partition()
    st = refine(qs, n, p)
    h = 0
    for w in range(n):
        h = (h * 31 + p.cell[w]) & 0xFFFFFFFF
    line += (f' | rf{st} nc{p.n_cells} h{h} d{distinguishes(p, QA)} '
             f'pj{project(p, QA, Query())}')

    tau = rnd(5) / 4.0
    seed = rnd(100000)
    spol = rnd(2)
    gid = rnd(8)
    s2 = checkpoint(s)
    snap = checkpoint(s2)
    anc = Ancestry()
    r = Result()
    st = speculate(s2, COMP, tau, seed, spol, gid, anc, r)
    line += (f' | sp{st} al{r.allowed} v{r.verdict} x{r.value} S{r.S:.6f} '
             f'in{r.intensity:.6f} b{anc.bits():.6f} lc{live_count(s2)}')
    st = retract(s2, snap, anc, gid)
    line += f' rt{st} lc{live_count(s2)} b{anc.bits():.6f}'

    fs = State()
    state_init(fs, n)
    t = Settle()
    settle_begin(t, fs, COMP)
    for k in range(3):
        PQ = rand_query(n, 3)
        settle_step(t, fs, COMP, PQ, rnd(3))
    r = Result()
    settle_commit(t, COMP, fs, r)
    line += (f' | se r{t.rounds} p{t.productive} i{t.informative} '
             f's{t.settled} c{t.contradicted} l{t.live_now} m{hex64(t.image)} '
             f'al{r.allowed} v{r.verdict} x{r.value}')

    QG = Query()
    if rnd(3) == 0:
        query_groundless(QG, n, db)
    else:
        QG = rand_query(n, db)
    COMP2 = Query()
    r = Result()
    st = ask2(OP, QA, QG, s, COMP2, r)
    line += (f' | a2{st} v{r.verdict} x{r.value} k{r.witness.kind} '
             f'ab{witness_check_absorb(OP, r.witness)}')
    out.append(line)

sys.stdout.write('\n'.join(out) + '\n')
