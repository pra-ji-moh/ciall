"""diff_native.py -- the Python side of diff_native.c.

Prints the same lines from the Python reasoner, then, given the C output
file as its argument, compares: every verdict, S, H and rank D must match
exactly, and every derivation must match in length. Derivations that
differ only in WHICH shortest path was taken are counted and reported,
not hidden (Python's BFS walks neighbours in set order).

    diff_native.exe > c.txt
    python diff_native.py c.txt
"""

import sys

from reasoner import build
from node_reasoner import node_query
from rank_domain import rank_query

SIZES = [(8, 10), (14, 18), (20, 40)]


def g(x):
    return format(x, '.17g')


def lines():
    for n, m in SIZES:
        for seed in range(200):
            w, _ = build(n_entities=n, n_edges=m, seed=seed)
            for a in range(n + 2):
                rk = rank_query(w, a)
                s = f'R {n} {seed} {a} {rk.verdict}'
                if rk.S is not None:
                    s += f' {g(rk.S)} {g(rk.H)}'
                for k in sorted(rk.D):
                    s += f' {k}'
                yield s
                for b in range(n + 2):
                    if a == b:
                        continue
                    r = node_query(w, a, b)
                    s = f'Q {n} {seed} {a} {b} {r.verdict}'
                    if r.S is not None:
                        s += f' {g(r.S)} {g(r.H)}'
                    if r.verdict == 'derived':
                        s += f' {r.value} |'
                        for step in r.witness:
                            x, y = step[3:-1].split(', ')
                            s += f' {x}>{y}'
                    yield s


def main():
    c = [ln.rstrip('\r\n') for ln in open(sys.argv[1])]
    py = list(lines())
    if len(c) != len(py):
        print(f'line counts differ: C {len(c)}, Python {len(py)}')
        raise SystemExit(1)
    exact = path_choice = mismatch = 0
    for cl, pl in zip(c, py):
        if cl == pl:
            exact += 1
            continue
        ch, _, cp = cl.partition(' |')
        ph, _, pp = pl.partition(' |')
        if ch == ph and len(cp.split()) == len(pp.split()):
            path_choice += 1
        else:
            mismatch += 1
            if mismatch <= 5:
                print('MISMATCH\n  C:  ' + cl + '\n  Py: ' + pl)
    print(f'{len(py)} lines: {exact} identical, {path_choice} differ only in which '
          f'shortest path was taken, {mismatch} real mismatches')
    raise SystemExit(1 if mismatch else 0)


if __name__ == '__main__':
    main()
