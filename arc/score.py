"""
score.py -- what the child would actually score on ARC-AGI-3.

Levels finished is not the benchmark. ARC Prize scores action efficiency against
the human baseline, squared, weighted towards later levels (docs.arcprize.org/methodology):

    level score = min(1.0, human_actions / its_actions) ** 2,  efficiency capped at 1.15
    game score  = sum(level_index * level_score) / sum(level_index over every level)
    score       = mean over games

Finishing a level slowly is worth almost nothing: twice a person's actions is a
quarter of the credit, ten times is a hundredth. This is the number to move.

    python arc/score.py                 all 25 games, seed 1
    python arc/score.py --seed 2        another seed
    python arc/score.py --off curious   with a part of itself switched off
"""

import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GROUPS = [
    ["ar25", "cd82", "dc22", "bp35", "cn04", "ft09"],
    ["g50t", "lf52", "ls20", "ka59", "lp85", "m0r0"],
    ["r11l", "s5i5", "sc25", "sp80", "re86", "sb26"],
    ["tn36", "tu93", "wa30", "sk48", "su15", "tr87", "vc33"],
]


def run(games, budget, env):
    r = subprocess.run([sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py")] + games +
                       ["--budget", budget], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    return r.stdout


def score_text(out, per_game):
    game = None
    for line in out.splitlines():
        m = re.search(r"ARC-AGI-3 game ([a-z0-9]{4})-", line)
        if m:
            game = m.group(1)
        m = re.search(r"against a person:(.*)", line)
        if m and game:
            per_game.setdefault(game, {})["against"] = m.group(1)
        m = re.search(r"levels finished: (\d+) of (\d+)", line)
        if m and game:
            per_game.setdefault(game, {})["done"] = int(m.group(1))
            per_game[game]["all"] = int(m.group(2))


def rhae(per_game):
    total, n, levels = 0.0, 0, 0
    rows = []
    for g in sorted(per_game):
        d = per_game[g]
        if "all" not in d:
            continue
        per = re.findall(r"level (\d+) took it (\d+), a person (\d+)", d.get("against", ""))
        num = den = 0.0
        for lv, mine, theirs in [(int(a), int(b), int(c)) for a, b, c in per]:
            eff = min(1.15, theirs / mine) if mine else 0.0
            num += lv * min(1.0, eff) ** 2
            den += lv
        for lv in range(len(per) + 1, d["all"] + 1):
            den += lv
        s = num / den if den else 0.0
        rows.append((g, d.get("done", 0), d["all"], 100.0 * s))
        total += s
        n += 1
        levels += d.get("done", 0)
    return rows, (100.0 * total / n if n else 0.0), levels


def main(argv):
    seed, off, budget = "1", "", "3000"
    i = 0
    while i < len(argv):
        if argv[i] == "--seed" and i + 1 < len(argv):
            seed = argv[i + 1]; i += 2; continue
        if argv[i] == "--off" and i + 1 < len(argv):
            off = argv[i + 1]; i += 2; continue
        if argv[i] == "--budget" and i + 1 < len(argv):
            budget = argv[i + 1]; i += 2; continue
        i += 1
    env = dict(os.environ)
    env["CIALL_SEED"] = seed
    env["CIALL_OFF"] = off
    env["CIALL_LIVE"] = ""
    env.pop("CIALL_MIND", None)   # fresh: no remembered routes
    per_game = {}
    procs = [subprocess.Popen([sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py")] + g +
                              ["--budget", budget], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding="utf-8",
                              errors="replace") for g in GROUPS]
    for p in procs:
        out, _ = p.communicate()
        score_text(out, per_game)
    rows, score, levels = rhae(per_game)
    for g, done, all_lv, s in sorted(rows, key=lambda r: -r[3]):
        print("  %-6s %d of %-2d levels   %6.3f%%" % (g, done, all_lv, s))
    print("\nseed %s off=[%s]:  ARC-AGI-3 score %.3f%%   (levels %d of 183)" % (seed, off, score, levels))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
