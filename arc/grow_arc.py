"""
grow_arc.py -- one unattended stretch of the child on every public ARC-AGI-3 game.

Run by the hourly task after the practice worlds (child/grow_hourly.cmd). The
child keeps what it settled about each game in child/arc_mind/<game>.txt, so each
stretch starts where the last one stood: levels it has a way through are walked,
and its actions go to the level it has not yet ended.

It writes:
  child/arc_progress.txt   one line per stretch: levels this stretch, best ever
  child/journal.txt        a short entry, beside the practice worlds' entries
Nothing here reasons; it starts the bridge and reads what the child reported.
"""

import datetime
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MIND = os.path.join(ROOT, "child", "arc_mind")
PROGRESS = os.path.join(ROOT, "child", "arc_progress.txt")
JOURNAL = os.path.join(ROOT, "child", "journal.txt")
BUDGET = "4000"

# four at a time, on four cores; each group about as long as the others
GROUPS = [
    ["ar25", "cd82", "dc22", "bp35", "cn04", "ft09"],
    ["g50t", "lf52", "ls20", "ka59", "lp85", "m0r0"],
    ["r11l", "s5i5", "sc25", "sp80", "re86", "sb26"],
    ["tn36", "tu93", "wa30", "sk48", "su15", "tr87", "vc33"],
]


def best_ever(game):
    path = os.path.join(MIND, game + ".txt")
    try:
        with open(path, encoding="utf-8") as f:
            for line in f:
                if line.startswith("best "):
                    return int(line.split()[1])
    except OSError:
        pass
    return 0


def main():
    os.makedirs(MIND, exist_ok=True)
    started = datetime.datetime.now()
    procs = []
    for group in GROUPS:
        cmd = [sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py")] + group + [
            "--budget", BUDGET, "--mind", MIND]
        procs.append(subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      text=True, encoding="utf-8", errors="replace"))
    levels = {}
    for p in procs:
        out, _ = p.communicate()
        game = None
        for line in out.splitlines():
            m = re.search(r"ARC-AGI-3 game ([a-z0-9]+)-", line)
            if m:
                game = m.group(1)
            m = re.search(r"levels finished: (\d+) of (\d+)", line)
            if m and game:
                levels[game] = (int(m.group(1)), int(m.group(2)))
    games = [g for group in GROUPS for g in group]
    if not levels:
        # nothing was played at all: the engine could not be reached, or the toolkit
        # failed. That is not a stretch of no levels, and writing it down as one would
        # tell the child a lie about itself.
        stamp = started.strftime("%Y-%m-%d %H:%M")
        with open(JOURNAL, "a", encoding="utf-8") as f:
            f.write("%s  ARC-AGI-3: no game could be played (the engine was out of reach);"
                    " nothing is written down as a score\n" % stamp)
        print("no game could be played; nothing recorded")
        return 1
    now_total = sum(levels.get(g, (0, 0))[0] for g in games)
    all_levels = sum(levels.get(g, (0, 0))[1] for g in games)
    best_total = sum(best_ever(g) for g in games)
    minutes = (datetime.datetime.now() - started).total_seconds() / 60.0
    stamp = started.strftime("%Y-%m-%d %H:%M")
    per_game = " ".join("%s:%d/%d" % (g, levels.get(g, (0, 0))[0], best_ever(g)) for g in games)
    with open(PROGRESS, "a", encoding="utf-8") as f:
        f.write("%s  this stretch %d of %d levels; best ever %d; %.0f min; game:now/best %s\n"
                % (stamp, now_total, all_levels, best_total, minutes, per_game))
    with open(JOURNAL, "a", encoding="utf-8") as f:
        f.write("%s  ARC-AGI-3, all %d public games, remembering what it settled before:\n"
                "    this stretch it ended %d of %d levels; the most it has ever ended is %d\n"
                % (stamp, len(games), now_total, all_levels, best_total))
    print("this stretch %d of %d levels; best ever %d" % (now_total, all_levels, best_total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
