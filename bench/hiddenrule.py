"""
hiddenrule.py -- the hidden-rule benchmark: how many tries does it take to find
the rule?

The puzzle is the blicket box of brain/test_frontal.c, made larger. A box
chimes when a test (a set of acts, done with the eyes on one of K places)
matches its hidden rule. A rule is a place, and a function of the acts:

    single(a)         act a
    and2/or2/xor2     two acts, both / either / exactly one
    and3/or3/maj3     three acts, all / any / at least two

A level fixes the number of acts n and the places K, which fixes how many rules
there are. The solver may try any test it likes and is told chime or not. It is
done when it can name the rule; the score is how many tests it spent.

Every arm is given the same oracle. They differ in what they know beforehand:
    ideal      knows the family of rules, picks the test that splits what is
               left most evenly, holds all of them in mind (the best possible
               given the family)
    random     knows the family, picks tests at random
    wm         knows the family but holds only `cap` rules in mind at a time
               (our frontal cortex: ob_frontal.c), ruling out only what it holds
    wm+memory  the same, but remembers every outcome and checks every rule
               against it as it comes
    passive    knows nothing of the family: a small neural network trained on
               random tests and their outcomes (what a trained model sees)
"""
import itertools
import numpy as np

LEVELS = {  # name: (acts, places, kinds)
    "L1": (6, 4, (0, 1, 2)),
    "L2": (8, 4, (0, 1, 2, 3)),
    "L3": (10, 4, (0, 1, 2, 3, 4, 5, 6)),
    "L4": (14, 4, (0, 1, 2, 3, 4, 5, 6)),
    "L5": (18, 4, (0, 1, 2, 3, 4, 5, 6)),
}
KIND_NAME = ["single", "and2", "or2", "xor2", "and3", "or3", "maj3"]
ARITY = [1, 2, 2, 2, 3, 3, 3]


class Family:
    def __init__(self, n, k, kinds):
        self.n, self.k = n, k
        kind, a, b, c, place = [], [], [], [], []
        for kd in kinds:
            for combo in itertools.combinations(range(n), ARITY[kd]):
                idx = list(combo) + [0] * (3 - len(combo))
                for q in range(k):
                    kind.append(kd)
                    a.append(idx[0]); b.append(idx[1]); c.append(idx[2])
                    place.append(q)
        self.kind, self.a, self.b, self.c, self.place = (np.array(x) for x in (kind, a, b, c, place))
        self.N = len(self.kind)

    def predict(self, hyps, bits, places):
        """hyps: index array (H); bits: (m, n) 0/1; places: (m). -> (H, m) bool"""
        kd, a, b, c, pl = (x[hyps][:, None] for x in (self.kind, self.a, self.b, self.c, self.place))
        B = bits.T  # (n, m)
        A, Bb, C = B[a[:, 0]], B[b[:, 0]], B[c[:, 0]]
        s3 = A + Bb + C
        f = np.select(
            [kd == 0, kd == 1, kd == 2, kd == 3, kd == 4, kd == 5, kd == 6],
            [A == 1, (A & Bb) == 1, (A | Bb) == 1, (A ^ Bb) == 1, s3 == 3, s3 >= 1, s3 >= 2],
        )
        return f & (pl == places[None, :])

    def describe(self, h):
        kd = self.kind[h]
        acts = [self.a[h], self.b[h], self.c[h]][: ARITY[kd]]
        return f"{KIND_NAME[kd]}({','.join(map(str, acts))}) at place {self.place[h]}"


def random_queries(fam, m, rng):
    dens = rng.choice([0.15, 0.3, 0.5], size=(m, 1))
    bits = (rng.random((m, fam.n)) < dens).astype(np.int8)
    return bits, rng.integers(0, fam.k, size=m)


class Oracle:
    def __init__(self, fam, secret):
        self.fam, self.secret, self.used = fam, secret, 0

    def ask(self, bits, place):
        self.used += 1
        return bool(self.fam.predict(np.array([self.secret]), bits[None, :], np.array([place]))[0, 0])


# --- the arms -----------------------------------------------------------------------

def best_query(fam, hyps, rng, candidates=300):
    """the test that splits `hyps` most evenly (ties: most yes: a positive test)"""
    bits, places = random_queries(fam, candidates, rng)
    pred = fam.predict(hyps, bits, places)
    yes = pred.sum(0)
    split = np.minimum(yes, len(hyps) - yes)
    best = np.lexsort((yes, split))[-1]
    return bits[best], places[best]


def run_ideal(fam, secret, rng, max_tries=3000):
    o, alive = Oracle(fam, secret), np.arange(fam.N)
    while len(alive) > 1 and o.used < max_tries:
        q, p = best_query(fam, alive, rng)
        r = o.ask(q, p)
        alive = alive[fam.predict(alive, q[None, :], np.array([p]))[:, 0] == r]
    return dict(tries=o.used, ok=len(alive) == 1 and alive[0] == secret, thought=0)


def run_random(fam, secret, rng, max_tries=3000):
    o, alive = Oracle(fam, secret), np.arange(fam.N)
    while len(alive) > 1 and o.used < max_tries:
        bits, places = random_queries(fam, 1, rng)
        r = o.ask(bits[0], places[0])
        alive = alive[fam.predict(alive, bits, places)[:, 0] == r]
    return dict(tries=o.used, ok=len(alive) == 1 and alive[0] == secret, thought=0)


def run_wm(fam, secret, rng, cap, memory, max_tries=3000):
    """the frontal cortex of brain/ob_frontal.c: `cap` rules in mind at a time"""
    o = Oracle(fam, secret)
    order = rng.permutation(fam.N)
    standing = np.ones(fam.N, bool)
    held, nxt, thought = [], 0, 0
    hist_q, hist_p, hist_r = [], [], []

    def check_against_memory(h):
        if not hist_q:
            return True
        pr = fam.predict(np.array([h]), np.array(hist_q), np.array(hist_p))[0]
        return bool((pr == np.array(hist_r)).all())

    def refill():
        nonlocal nxt, thought
        scanned = 0
        while len(held) < cap and scanned < fam.N:
            h = order[nxt % fam.N]
            nxt += 1
            scanned += 1
            if not standing[h] or h in held:
                continue
            thought += 1
            if memory and not check_against_memory(h):
                standing[h] = False
                continue
            held.append(h)

    refill()
    while standing.sum() > 1 and o.used < max_tries:
        if not held:
            break
        q, p = best_query(fam, np.array(held), rng)
        r = o.ask(q, p)
        hist_q.append(q); hist_p.append(p); hist_r.append(r)
        pr = fam.predict(np.array(held), q[None, :], np.array([p]))[:, 0]
        struck = [h for h, x in zip(held, pr) if x != r]
        for h in struck:
            standing[h] = False
            held.remove(h)
        if memory:  # memory checks every rule against the new outcome, at once
            alive = np.flatnonzero(standing)
            bad = alive[fam.predict(alive, q[None, :], np.array([p]))[:, 0] != r]
            thought += len(alive)
            standing[bad] = False
            held[:] = [h for h in held if standing[h]]
        elif not struck and len(held) >= cap and standing.sum() > len(held):
            held.pop(0)  # full, nothing ruled out, rivals unthought of: the first lets go
        refill()
    ok = standing.sum() == 1 and standing[secret]
    return dict(tries=o.used, ok=bool(ok), thought=thought)


def run_passive(fam, secret, rng, checkpoints=(16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192), hidden=64):
    """no knowledge of the family: a network trained on random tests and what the box did.
    Done when it gets every test of a stratified set right (half of them chime)."""
    o = Oracle(fam, secret)
    # a test set: half the tests the box chimes for, half not
    tb, tp = random_queries(fam, 40000, rng)
    out = fam.predict(np.array([secret]), tb, tp)[0]
    pos, neg = np.flatnonzero(out), np.flatnonzero(~out)
    if len(pos) < 100:  # chimes are rare for some rules: build positives directly
        extra_b, extra_p = [], []
        while len(extra_b) < 300:
            b, p = random_queries(fam, 2000, rng)
            keep = np.flatnonzero(fam.predict(np.array([secret]), b, p)[0])
            extra_b += list(b[keep]); extra_p += list(p[keep])
        tb = np.concatenate([tb, np.array(extra_b[:300])]); tp = np.concatenate([tp, np.array(extra_p[:300])])
        out = fam.predict(np.array([secret]), tb, tp)[0]
        pos, neg = np.flatnonzero(out), np.flatnonzero(~out)
    sel = np.concatenate([rng.choice(pos, 300), rng.choice(neg, 300)])

    def feat(b, p):
        oh = np.zeros((len(p), fam.k)); oh[np.arange(len(p)), p] = 1
        return np.concatenate([b, oh], 1).astype(np.float32)

    Xt, yt = feat(tb[sel], tp[sel]), out[sel]
    Xb, Pb = random_queries(fam, max(checkpoints), rng)
    Y = fam.predict(np.array([secret]), Xb, Pb)[0].astype(np.float32)
    X = feat(Xb, Pb)
    for m in checkpoints:
        o.used = m
        W1 = rng.normal(0, 0.5, (X.shape[1], hidden)).astype(np.float32); b1 = np.zeros(hidden, np.float32)
        W2 = rng.normal(0, 0.5, (hidden, 1)).astype(np.float32); b2 = np.zeros(1, np.float32)
        params = [W1, b1, W2, b2]; mom = [np.zeros_like(p) for p in params]; vel = [np.zeros_like(p) for p in params]
        x, y = X[:m], Y[:m]
        w = np.where(y == 1, 0.5 * len(y) / max(y.sum(), 1), 0.5 * len(y) / max((1 - y).sum(), 1)).astype(np.float32)
        for t in range(1, 301):
            h = np.maximum(x @ W1 + b1, 0); z = (h @ W2 + b2)[:, 0]
            pr = 1 / (1 + np.exp(-z)); g = ((pr - y) * w / len(y))[:, None]
            gW2 = h.T @ g; gb2 = g.sum(0); gh = (g @ W2.T) * (h > 0)
            grads = [x.T @ gh, gh.sum(0), gW2, gb2]
            for i, (p, gr) in enumerate(zip(params, grads)):
                mom[i] = 0.9 * mom[i] + 0.1 * gr; vel[i] = 0.999 * vel[i] + 0.001 * gr * gr
                p -= 0.02 * (mom[i] / (1 - 0.9 ** t)) / (np.sqrt(vel[i] / (1 - 0.999 ** t)) + 1e-8)
        pred = (np.maximum(Xt @ W1 + b1, 0) @ W2 + b2)[:, 0] > 0
        if (pred == yt).all():
            return dict(tries=m, ok=True, thought=0)
    return dict(tries=checkpoints[-1], ok=False, thought=0)
