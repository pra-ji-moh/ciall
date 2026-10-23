"""Run the hidden-rule benchmark: python3 run_bench.py [rules_per_level] [out.json]"""
import json, sys, time
from multiprocessing import Pool
import numpy as np
from hiddenrule import LEVELS, Family, run_ideal, run_random, run_wm, run_passive

ARMS = {  # name: function(fam, secret, rng)
    "ideal": lambda f, s, r: run_ideal(f, s, r),
    "random": lambda f, s, r: run_random(f, s, r),
    "wm cap=4 (1y)": lambda f, s, r: run_wm(f, s, r, 4, False),
    "wm cap=39 (3y)": lambda f, s, r: run_wm(f, s, r, 39, False),
    "wm cap=66 (grown)": lambda f, s, r: run_wm(f, s, r, 66, False),
    "wm+memory cap=39": lambda f, s, r: run_wm(f, s, r, 39, True),
    "passive net": lambda f, s, r: run_passive(f, s, r),
}

def job(args):
    level, arm, i = args
    fam = Family(*LEVELS[level])
    rng = np.random.default_rng(1000 * i + 7)
    secret = int(np.random.default_rng(i * 31 + 5).integers(fam.N))
    t = time.time()
    res = ARMS[arm](fam, secret, np.random.default_rng(i + 99))
    res.update(level=level, arm=arm, i=i, N=fam.N, secs=time.time() - t)
    return res

if __name__ == "__main__":
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 5
    out = sys.argv[2] if len(sys.argv) > 2 else "results.json"
    jobs = [(l, a, i) for l in LEVELS for a in ARMS for i in range(n)]
    with Pool(4) as p:
        res = p.map(job, jobs, chunksize=1)
    json.dump(res, open(out, "w"), default=lambda o: o.item() if hasattr(o, 'item') else str(o))
    print(f"{'':20}" + "".join(f"{l+' N='+str(Family(*LEVELS[l]).N):>16}" for l in LEVELS))
    for a in ARMS:
        row = f"{a:20}"
        for l in LEVELS:
            rs = [r for r in res if r['arm'] == a and r['level'] == l]
            ok = sum(r['ok'] for r in rs)
            med = float(np.median([r['tries'] for r in rs]))
            row += f"{('%g' % med) + ' (' + str(ok) + '/' + str(len(rs)) + ')':>16}"
        print(row)
