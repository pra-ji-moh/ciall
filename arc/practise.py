#!/usr/bin/env python3
"""
practise.py -- what the child does with a game it may play as often as it likes.

A first sight of a game is one thing; a game practised is another, and is scored
apart (see arc/score.py: first sight is what is comparable to anyone's published
figure). Practice keeps, for every level it has ever won, the exact acts that won it
from the level's start, in child/solutions/<game>.json, and makes them shorter:

  collect   play each game again and again. Each run first replays the levels it
            already knows, then spends what is left of its budget on the next one,
            so each run starts where the last got to. Every winning life is kept,
            the shortest per level.

  shorten   for each level, replay its way from the level's start and take pieces
            out of it: what still wins without them was never needed. Each try is
            checked on the real game, from the start, so nothing kept is a guess.

  python arc/practise.py collect [rounds] [budget]
  python arc/practise.py shorten [games...]
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SOL = os.path.join(ROOT, "child", "solutions")
sys.path.insert(0, HERE)

GROUPS = [
    ["ar25", "cd82", "dc22", "bp35", "cn04", "ft09"],
    ["g50t", "lf52", "ls20", "ka59", "lp85", "m0r0"],
    ["r11l", "s5i5", "sc25", "sp80", "re86", "sb26"],
    ["tn36", "tu93", "wa30", "sk48", "su15", "tr87", "vc33"],
]


def known():
    out = {}
    for g in sum(GROUPS, []):
        try:
            with open(os.path.join(SOL, g + ".json"), encoding="utf-8") as f:
                out[g] = json.load(f)
        except (OSError, ValueError):
            out[g] = {}
    return out


def summary(tag):
    k = known()
    n = sum(len(v) for v in k.values())
    acts = sum(len(s) for v in k.values() for s in v.values())
    print("%s: %d levels with a known way, %d acts in all: %s" % (
        tag, n, acts, " ".join("%s:%s" % (g, ",".join("%s=%d" % (lv, len(s)) for lv, s in sorted(v.items(), key=lambda kv: int(kv[0]))))
                                for g, v in k.items() if v)))


def collect(rounds, budget, first_seed=1):
    os.makedirs(SOL, exist_ok=True)
    env = dict(os.environ, CIALL_SOLUTIONS_DIR=SOL, CIALL_PREFIX_DIR=SOL)
    for r in range(rounds):
        env["CIALL_SEED"] = str(first_seed + r)
        procs = [subprocess.Popen([sys.executable, os.path.join(HERE, "arc_bridge.py")] + g + ["--budget", str(budget)],
                                  cwd=ROOT, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                 for g in GROUPS]
        for p in procs:
            p.wait()
        summary("after round %d (seed %d)" % (r + 1, first_seed + r))


# ---- shortening, on the real game ---------------------------------------------------

def make_game(gid_short):
    import arc_agi
    import arc_bridge as B
    arcade = arc_agi.Arcade(environments_dir=os.path.join(HERE, "environment_files"),
                            recordings_dir=os.path.join(HERE, "recordings"),
                            operation_mode=arc_agi.OperationMode.OFFLINE)
    for info in arcade.get_environments() or []:
        gid = getattr(info, "game_id", "")
        if gid.split("-")[0] == gid_short:
            return lambda: B.RealGame(arcade, gid, getattr(info, "baseline_actions", None) or [])
    raise SystemExit("no such game here: %s" % gid_short)


def levels_of(text):
    for line in text.split("\n"):
        if line.startswith("OBS "):
            p = line.split()
            return int(p[2]), int(p[1])
    return 0, 0


def wins(new_game, ways, k, candidate):
    """from a fresh game: the known ways for levels 1..k-1, then the candidate: does level k end?"""
    g = new_game()
    obs = g.reset()
    for lv in range(1, k + 1):
        seq = candidate if lv == k else ways[str(lv)]
        start, _ = levels_of(obs)
        for a in seq:
            obs = g.step(a[0], a[1], a[2]) if a[0] == 6 else g.step(a[0])
            now, state = levels_of(obs)
            if now > start:
                break
            if state == 2:   # over: this way does not win here
                return False
        if levels_of(obs)[0] < lv:
            return False
    return True


def shorten_level(new_game, ways, k, tries):
    """take out pieces, halves first, then smaller, and keep each cut that still wins"""
    seq = ways[str(k)]
    if not wins(new_game, ways, k, seq):
        return seq, 0
    used = 1
    chunk = max(1, len(seq) // 2)
    while chunk >= 1 and used < tries:
        i = 0
        cut_any = False
        while i < len(seq) and used < tries:
            cand = seq[:i] + seq[i + chunk:]
            used += 1
            if cand and wins(new_game, ways, k, cand):
                seq = cand
                cut_any = True
            else:
                i += chunk
        if not cut_any:
            chunk //= 2
    return seq, used


def shorten(games, tries=400):
    k_all = known()
    for g in games:
        ways = k_all.get(g, {})
        if not ways:
            continue
        new_game = make_game(g)
        for lv in sorted(ways, key=int):
            before = len(ways[lv])
            ways[lv], used = shorten_level(new_game, ways, int(lv), tries)
            print("  %s level %s: %d acts -> %d (%d tries on the game)" % (g, lv, before, len(ways[lv]), used), flush=True)
        # merge with what is there now: collection may have added levels meanwhile
        try:
            with open(os.path.join(SOL, g + ".json"), encoding="utf-8") as f:
                now = json.load(f)
        except (OSError, ValueError):
            now = {}
        for lv, seq in ways.items():
            if lv not in now or len(seq) < len(now[lv]):
                now[lv] = seq
        with open(os.path.join(SOL, g + ".json"), "w", encoding="utf-8") as f:
            json.dump(now, f)


if __name__ == "__main__":
    what = sys.argv[1] if len(sys.argv) > 1 else "collect"
    if what == "collect":
        collect(int(sys.argv[2]) if len(sys.argv) > 2 else 3, int(sys.argv[3]) if len(sys.argv) > 3 else 6000,
                int(sys.argv[4]) if len(sys.argv) > 4 else 1)
    elif what == "shorten":
        shorten(sys.argv[2:] or sum(GROUPS, []))
    elif what == "summary":
        summary("known")
