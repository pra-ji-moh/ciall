"""
self_evolve.py -- the child building itself, as an archive of every version of
itself (the Darwin Goedel Machine's way), with nothing predictive in it.

Every version of the child it has ever made is kept in child/archive.tsv: what
it is (every setting and switch in certifiable-c/smarsh_self.h), which version it
was made from, and how it did. Nothing is thrown away: a version that did worse
is a dead end kept on the record, and one that did no worse is a stepping stone
another change can start from. Keeping only the latest version is what stalls
self-improvement (the Darwin Goedel Machine's ablation); this keeps them all.

One generation, each hour:

  1. A PARENT, chosen uniformly among the versions still standing: those no other
     version beats on both measures of the development games (levels ended, and
     the ARC-AGI-3 score itself). Nothing is weighed; what is beaten is ruled out.
  2. A CHANGE. One, two or three of its settings or switches, each to a value it
     may take, chosen uniformly -- or, sometimes, each setting taken from one of
     two standing versions, chosen uniformly between them.
  3. THE JUDGE. The new version must compile and pass every check in build_c.sh
     (which it cannot touch); then it plays the development games, and goes into
     the archive whatever happens, with its scores.
  4. THE CHILD IN USE becomes the standing version with the best development
     score -- but only if it does no worse than the one in use on the games held
     back, which are never used to choose anything else. The judge must not be
     something it can bend.

Everything is written to child/archive.tsv and child/self_research.txt.
child/self_write.off stops it.
"""

import datetime
import json
import os
import random
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "arc"))
sys.path.insert(0, HERE)
import score                       # noqa: E402  the benchmark's own scoring (arc/score.py)
import self_write as sw            # noqa: E402  building, checks, the games

ARCHIVE = os.path.join(HERE, "archive.tsv")
LOG = os.path.join(HERE, "self_research.txt")
JOURNAL = os.path.join(HERE, "journal.txt")
BUDGET = "3000"


# ---- what a version is ------------------------------------------------------------

def read_self(path=sw.SELF):
    """Every setting in smarsh_self.h: {name: (value, [values it may take])}, and the lines."""
    lines = open(path, encoding="utf-8").read().splitlines()
    genome = {}
    for line in lines:
        m = sw.DEFINE.match(line.strip())
        if m:
            genome[m.group(2)] = (m.group(3), m.group(5).split())
    return genome, lines


def write_self(lines, values, path):
    out = []
    for line in lines:
        m = sw.DEFINE.match(line.strip())
        if m and m.group(2) in values:
            line = m.group(1) + values[m.group(2)] + m.group(4)
        out.append(line)
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(out) + "\n")


# ---- the archive ------------------------------------------------------------------

def load_archive():
    rows = []
    if os.path.exists(ARCHIVE):
        for line in open(ARCHIVE, encoding="utf-8"):
            p = line.rstrip("\n").split("\t")
            if len(p) < 9 or p[0] == "id":
                continue
            rows.append({"id": int(p[0]), "parent": p[1], "made": p[2], "change": p[3],
                         "values": json.loads(p[4]), "dev_levels": float(p[5]), "dev_score": float(p[6]),
                         "held_score": float(p[7]) if p[7] != "-" else None, "verdict": p[8]})
    return rows


def append_archive(row):
    new = not os.path.exists(ARCHIVE)
    with open(ARCHIVE, "a", encoding="utf-8") as f:
        if new:
            f.write("id\tparent\tmade\tchange\tvalues\tdev_levels\tdev_score\theld_score\tverdict\n")
        f.write("%d\t%s\t%s\t%s\t%s\t%s\t%.4f\t%s\t%s\n" % (
            row["id"], row["parent"], row["made"], row["change"], json.dumps(row["values"], sort_keys=True),
            row["dev_levels"], row["dev_score"],
            "-" if row["held_score"] is None else "%.4f" % row["held_score"], row["verdict"]))


def standing(rows):
    """The versions no other version beats on both development measures."""
    ok = [r for r in rows if r["verdict"] != "failed-checks"]
    out = []
    for r in ok:
        beaten = any((o["dev_levels"] >= r["dev_levels"] and o["dev_score"] >= r["dev_score"]) and
                     (o["dev_levels"] > r["dev_levels"] or o["dev_score"] > r["dev_score"]) for o in ok)
        if not beaten:
            out.append(r)
    return out


# ---- playing ----------------------------------------------------------------------

def play(child, games, seed):
    """Levels ended and the ARC-AGI-3 score (%), fresh, no memory. None if nothing answered.
    The games are played four at a time, on four cores; nothing about the child changes."""
    env = dict(os.environ)
    env["CIALL_CHILD"] = child
    env["CIALL_SEED"] = str(seed)
    env.pop("CIALL_MIND", None)
    games = list(games)
    groups = [games[i::4] for i in range(4) if games[i::4]]
    procs = [subprocess.Popen([sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py")] + g +
                              ["--budget", BUDGET], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
             for g in groups]
    per_game = {}
    for pr in procs:
        out, _ = pr.communicate()
        score.score_text(out, per_game)
    if not per_game:
        return None
    rows, total, levels = score.rhae(per_game)
    return levels, total


SEEDS = (1, 2)   # one seed alone is luck: the same child scores 0.64% on one and 0.23% on another


def play_seeds(child, games):
    """play() on every seed in SEEDS, averaged. None if any seed answered nothing."""
    got = [play(child, games, sd) for sd in SEEDS]
    if any(g is None for g in got):
        return None
    return (sum(g[0] for g in got) / len(got), sum(g[1] for g in got) / len(got))


def stamp():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M")


def write_up(lines):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write("\n%s  (building itself: one generation)\n" % stamp())
        for line in lines:
            f.write("  %s\n" % line)


def build_version(lines, values, tag):
    """Its whole source, with these settings, built and checked in a folder of its own."""
    cand = os.path.join(ROOT, ".evolve_" + tag)
    out = os.path.join(ROOT, ".evolve_build_" + tag)
    if os.path.exists(cand):
        shutil.rmtree(cand)
    shutil.copytree(sw.SRC, cand, ignore=shutil.ignore_patterns(
        "nul", "*.exe", "*.obj", "*.o", "*.pdb", "*.ilk", "node_modules", "__pycache__"))
    write_self(lines, values, os.path.join(cand, "smarsh_self.h"))
    ok, tail, child = sw.build(os.path.basename(cand), out)
    return ok, tail, child, cand


# ---- one generation ---------------------------------------------------------------

def main():
    seed = int(os.environ.get("CIALL_SEED", "1"))
    rng = random.Random()
    genome, lines = read_self()
    in_use = {k: v[0] for k, v in genome.items()}
    rows = load_archive()
    told = []

    if not rows:
        # the first entry is the child as it is now
        ok, tail, child, _ = build_version(lines, in_use, "seed")
        if not ok:
            write_up(["its own source does not pass its checks; it does not build on it (%s)" % tail])
            return 1
        dev, held = play_seeds(child, sw.DEV), play_seeds(child, sw.HELD)
        if dev is None or held is None:
            write_up(["no game could be played; nothing recorded"])
            return 1
        append_archive({"id": 0, "parent": "-", "made": stamp(), "change": "the child as it was",
                        "values": in_use, "dev_levels": dev[0], "dev_score": dev[1],
                        "held_score": held[1], "verdict": "in-use"})
        write_up(["the archive begins with the child as it is (two seeds averaged): dev %.1f levels, %.3f%%; held back %.3f%%"
                  % (dev[0], dev[1], held[1])])
        print("archive begun")
        return 0

    stand = standing(rows)
    parent = rng.choice(stand)
    values = dict(parent["values"])
    if len(stand) >= 2 and rng.randrange(4) == 0:
        other = rng.choice([r for r in stand if r["id"] != parent["id"]])
        for k in values:
            if k in other["values"] and rng.randrange(2) == 0:
                values[k] = other["values"][k]
        change = "combined %d and %d" % (parent["id"], other["id"])
    else:
        names = [k for k in genome if len(genome[k][1]) > 1 and k in values]
        changed = []
        for k in rng.sample(names, rng.choice([1, 1, 2, 3])):
            choices = [v for v in genome[k][1] if v != values[k]]
            if choices:
                new = rng.choice(choices)
                changed.append("%s %s->%s" % (k, values[k], new))
                values[k] = new
        change = "; ".join(changed) or "no change"
    seen = {json.dumps(r["values"], sort_keys=True) for r in rows}
    if json.dumps(values, sort_keys=True) in seen:
        write_up(["from version %d it made a version it had already made (%s); it tries again next time"
                  % (parent["id"], change)])
        print("already made")
        return 0

    new_id = max(r["id"] for r in rows) + 1
    told.append("from version %d (one of %d still standing) it made version %d: %s"
                % (parent["id"], len(stand), new_id, change))
    ok, tail, child, _ = build_version(lines, values, "cand")
    if not ok:
        append_archive({"id": new_id, "parent": str(parent["id"]), "made": stamp(), "change": change,
                        "values": values, "dev_levels": 0, "dev_score": 0.0, "held_score": None,
                        "verdict": "failed-checks"})
        told.append("it does not pass its own checks (%s): kept on the record as a dead end" % tail)
        write_up(told)
        return 0
    dev = play_seeds(child, sw.DEV)
    if dev is None:
        told.append("no game could be played; nothing recorded")
        write_up(told)
        return 1
    held = play_seeds(child, sw.HELD)
    row = {"id": new_id, "parent": str(parent["id"]), "made": stamp(), "change": change, "values": values,
           "dev_levels": dev[0], "dev_score": dev[1], "held_score": None if held is None else held[1],
           "verdict": "archived"}
    told.append("development games, two seeds averaged: %.1f levels, %.3f%% (its parent: %.1f, %.3f%%)"
                % (dev[0], dev[1], parent["dev_levels"], parent["dev_score"]))

    # the child in use: the best standing version on development, if no worse where it was never tuned
    current = next((r for r in rows if r["verdict"] == "in-use"), rows[0])
    best_dev = max(stand + [row], key=lambda r: (r["dev_score"], r["dev_levels"]))
    if (best_dev is row and held is not None and current["held_score"] is not None
            and row["dev_score"] > current["dev_score"] and held[1] >= current["held_score"]):
        row["verdict"] = "in-use"
        write_self(lines, values, sw.SELF)
        subprocess.run(["bash", "build_c.sh"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        told.append("it is now this version: better on the development games (%.3f%% -> %.3f%%) and no worse "
                    "on the games held back (%.3f%% -> %.3f%%)" % (current["dev_score"], row["dev_score"],
                                                                  current["held_score"], held[1]))
        with open(JOURNAL, "a", encoding="utf-8") as f:
            f.write("%s  I built a new version of myself (%s) and became it: ARC score on my practice games "
                    "%.3f%% -> %.3f%%, on games I never tuned on %.3f%% -> %.3f%%\n"
                    % (stamp(), change, current["dev_score"], row["dev_score"], current["held_score"], held[1]))
        # the one in use before is no longer the one in use
        rewrite = load_archive()
        for r in rewrite:
            if r["verdict"] == "in-use":
                r["verdict"] = "was-in-use"
        os.replace(ARCHIVE, ARCHIVE + ".old")
        for r in rewrite:
            append_archive(r)
        os.remove(ARCHIVE + ".old")
    else:
        told.append("kept in the archive (%s); the child in use stays as it is"
                    % ("still standing" if not any(
                        (o["dev_levels"] >= row["dev_levels"] and o["dev_score"] >= row["dev_score"]) and
                        (o["dev_levels"] > row["dev_levels"] or o["dev_score"] > row["dev_score"])
                        for o in rows if o["verdict"] != "failed-checks") else "a dead end, beaten by another"))
    append_archive(row)
    write_up(told)
    print("\n".join(told))
    return 0


if __name__ == "__main__":
    # "--generations N": N generations one after another (at the laptop, pushing hard)
    n = int(sys.argv[sys.argv.index("--generations") + 1]) if "--generations" in sys.argv else 1
    code = 0
    for _ in range(n):
        code = main()
    sys.exit(code)
