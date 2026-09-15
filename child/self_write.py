"""
self_write.py -- the child working on itself the way a researcher works on a paper:
it reads how its own runs went, finds the biggest thing wrong, suspects a part of
itself, changes that part, tries it on the games that showed the trouble, and only
then checks it everywhere and keeps or drops it.

One pass, all of it by elimination and all of it written down:

  1. COMPLAINTS. It reads its own accounts of the last stretch (child/arc_journal.txt)
     and names what went wrong, per game: it stopped with actions left; the
     situations ran into thousands; it died again and again; it never found what it
     moves; nothing it can say ends a level; it wins but spends far more than a
     person.
  2. SUSPECTS. Each complaint has parts of itself that could cause it (SUSPECTS
     below): a mapping from trouble to the settings and mechanisms that bear on it.
     Nothing here says which value is right; it says where to look.
  3. A TRIAL. It takes the worst complaint, picks one untried change to a suspect
     (uniformly, as sm_decide guesses among what remains), writes a candidate copy
     of its own source, and plays only the games that complained. A change that does
     worse there is ruled out at once, cheaply.
  4. THE FULL CHECK. A change that survives the trial must then compile, pass every
     check in build_c.sh (which it cannot edit), and end at least as many levels on
     the development games and no fewer on the held-back ones.
  5. WRITING IT UP. Kept or ruled out, it goes to child/self_edits.txt (the record it
     never repeats) and child/self_research.txt (what it was trying to fix, what it
     suspected, and what happened), and an adopted change goes to its journal.

  child/self_write.off stops it. In the cloud each adoption is its own commit.
"""

import datetime
import os
import random
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "certifiable-c")
SELF = os.path.join(SRC, "smarsh_self.h")
LEDGER = os.path.join(ROOT, "child", "self_edits.txt")
LOG = os.path.join(ROOT, "child", "self_research.txt")
JOURNAL = os.path.join(ROOT, "child", "journal.txt")
ACCOUNTS = os.path.join(ROOT, "child", "arc_journal.txt")
CAND = os.path.join(ROOT, ".self_candidate")
CAND_OUT = os.path.join(ROOT, ".self_build")
BASE_OUT = os.path.join(ROOT, ".self_base")
DEV = ["ar25", "cd82", "dc22", "g50t", "lf52", "ls20", "r11l", "s5i5", "sc25", "sp80", "tn36", "tu93", "wa30"]
HELD = ["bp35", "cn04", "ft09", "ka59", "lp85", "m0r0", "re86", "sb26", "sk48", "su15", "tr87", "vc33"]
TRIAL_BUDGET = "2000"
FULL_BUDGET = "3000"
DEFINE = re.compile(r'^(#define\s+(\w+)\s+)(\S+)(\s*/\* self:\s*([^*]+?)\s*\*/)\s*$')

# what could be behind each kind of trouble: where to look, not what to think
SUSPECTS = {
    "stopped with actions left": ["SELF_SKIP_TAU", "SELF_OFF_SKIP", "SELF_OFF_STOOD", "SELF_FEW_SITUATIONS",
                                  "SELF_OFF_UNMASK", "SELF_OFF_WINDOW", "SELF_KEEP_ACTS", "SELF_FRESH_STARTS"],
    "situations in the thousands": ["SELF_RESTLESS_WINDOW", "SELF_TICK_MAX", "SELF_OFF_RESTLESS", "SELF_OFF_CLOCK",
                                    "SELF_KEEP_ACTS"],
    "dying again and again": ["SELF_DEATH_RETRIES", "SELF_OFF_DEATHS", "SELF_OFF_REDRAW"],
    "never found what it moves": ["SELF_BODY_VOTES", "SELF_SHIFT_SURE", "SELF_OFF_RESTLESS"],
    "nothing it can say ends a level": ["SELF_OFF_THEORY", "SELF_OFF_PANELS", "SELF_OFF_NEAR", "SELF_USE_LIVE",
                                        "SELF_TIE_MODE"],
    "spends far more than a person": ["SELF_OFF_STOOD", "SELF_MATCH_WEIGHT", "SELF_KEEP_ACTS", "SELF_OFF_REPLAY",
                                      "SELF_TIE_MODE"],
}


# ---- what it says about itself ------------------------------------------------

def complaints():
    """Per game, from its own last account of it, what went wrong. {trouble: [games]}"""
    if not os.path.exists(ACCOUNTS):
        return {}
    text = open(ACCOUNTS, encoding="utf-8", errors="replace").read()
    blocks = {}
    game = None
    for line in text.splitlines():
        m = re.search(r"ARC-AGI-3 game ([a-z0-9]{4})-\S+.*?\((\d+) actions carried\)", line)
        if m:
            game = m.group(1)
            blocks[game] = {"actions": int(m.group(2)), "text": []}
            continue
        if game:
            blocks[game]["text"].append(line)
    out = {}

    def add(trouble, g):
        out.setdefault(trouble, [])
        if g not in out[trouble]:
            out[trouble].append(g)

    for g, b in blocks.items():
        t = "\n".join(b["text"])
        levels = re.search(r"levels finished: (\d+) of (\d+)", t)
        done = int(levels.group(1)) if levels else 0
        if "it ran out of anything untried" in t and b["actions"] < 3500:
            add("stopped with actions left", g)
        sits = [int(x) for x in re.findall(r"(\d+) situations met", t)]
        if sits and max(sits) > 1000:
            add("situations in the thousands", g)
        died = re.search(r"died (\d+) times", t)
        if died and int(died.group(1)) > 40:
            add("dying again and again", g)
        if "found what it moves" not in t and "itself:" not in t:
            add("never found what it moves", g)
        if done == 0:
            add("nothing it can say ends a level", g)
        mine = re.search(r"on those, (\d+) actions against a person's (\d+)", t)
        if mine and int(mine.group(2)) > 0 and int(mine.group(1)) > 10 * int(mine.group(2)):
            add("spends far more than a person", g)
    return out


# ---- its own source ------------------------------------------------------------

def options():
    lines = open(SELF, encoding="utf-8").read().splitlines()
    out = []
    for i, line in enumerate(lines):
        m = DEFINE.match(line.strip())
        if not m:
            continue
        name, cur, values = m.group(2), m.group(3), m.group(5).split()
        for v in values:
            if v != cur:
                out.append((name, cur, v, i))
    return lines, out


def tried():
    seen = set()
    if os.path.exists(LEDGER):
        for line in open(LEDGER, encoding="utf-8"):
            p = line.split("\t")
            if len(p) >= 4:
                seen.add((p[1], p[3]))
    return seen


def write_candidate(lines, idx, new):
    if os.path.exists(CAND):
        shutil.rmtree(CAND)
    # only what it is made of: not build leavings, and not "nul", a name Windows keeps
    shutil.copytree(SRC, CAND, ignore=shutil.ignore_patterns(
        "nul", "*.exe", "*.obj", "*.o", "*.pdb", "*.ilk", "node_modules", "__pycache__"))
    m = DEFINE.match(lines[idx].strip())
    lines2 = list(lines)
    lines2[idx] = m.group(1) + new + m.group(4)
    with open(os.path.join(CAND, "smarsh_self.h"), "w", encoding="utf-8", newline="") as f:
        f.write("\n".join(lines2) + "\n")


def build(src_rel, out_dir):
    env = dict(os.environ)
    env["CIALL_SRC"] = src_rel
    env["CIALL_OUT"] = out_dir
    for k in ("CIALL_SEED", "CIALL_TIE", "CIALL_OFF", "CIALL_LIVE", "CIALL_MIND"):
        env.pop(k, None)   # the checks run as the child normally is, not under this attempt
    r = subprocess.run(["bash", "build_c.sh"], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    tail = r.stdout.strip().splitlines()[-1] if r.stdout else ""
    return tail.endswith("0 failed"), tail, os.path.join(out_dir, "play_arc.exe")


def levels(child, games, seed, budget):
    env = dict(os.environ)
    env["CIALL_CHILD"] = child
    env["CIALL_SEED"] = str(seed)
    env.pop("CIALL_MIND", None)   # a fair test: no remembered routes
    r = subprocess.run([sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py")] + list(games) +
                       ["--budget", budget], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    got = [int(m.group(1)) for m in re.finditer(r"levels finished: (\d+) of", r.stdout)]
    if not got:
        return None   # no game answered: a run that did not happen, not a score of nothing
    return sum(got)


# ---- writing it down -----------------------------------------------------------

def stamp():
    return datetime.datetime.now().strftime("%Y-%m-%d %H:%M")


def note(name, cur, new, verdict, detail):
    with open(LEDGER, "a", encoding="utf-8") as f:
        f.write("%s\t%s\t%s\t%s\t%s\t%s\n" % (stamp(), name, cur, new, verdict, detail))


def write_up(lines):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write("\n%s\n" % stamp())
        for line in lines:
            f.write("  %s\n" % line)


# ---- one pass of work on itself -------------------------------------------------

def main():
    seed = int(os.environ.get("CIALL_SEED", "1"))
    random.seed(seed)
    lines, opts = options()
    seen = tried()
    fresh = [o for o in opts if (o[0], o[2]) not in seen]
    if not fresh:
        print("it has tried every change it can make to itself for now")
        write_up(["nothing left to try: every change it can make to itself has been tried"])
        return 0

    # what went wrong, worst first; and one untried change to something that could cause it
    trouble = complaints()
    order = sorted(trouble.items(), key=lambda kv: -len(kv[1]))
    issue, games, choices = None, [], []
    for name, gs in order:
        cand = [o for o in fresh if o[0] in SUSPECTS.get(name, [])]
        if cand:
            issue, games, choices = name, gs, cand
            break
    if issue is None:   # nothing it can act on: take any untried change
        issue, games, choices = "nothing in particular", [], fresh
    pick = random.choice(choices)
    name, cur, new, idx = pick
    trial_games = [g for g in games if g in DEV][:6] or [g for g in games][:6]
    told = ["what is wrong: %s, in %d game%s (%s)" % (issue, len(games), "" if len(games) == 1 else "s",
                                                      " ".join(games) if games else "-"),
            "what I suspect in myself: %s, now %s; I try %s" % (name, cur, new)]
    print("issue: %s | trying %s %s -> %s" % (issue, name, cur, new))

    ok0, tail0, child0 = build(os.path.basename(SRC), BASE_OUT)
    if not ok0:
        ok0, tail0, child0 = build(os.path.basename(SRC), BASE_OUT)
    if not ok0:
        told.append("I stopped: my own source does not pass my checks (%s)" % tail0)
        write_up(told)
        print("its own source does not pass its checks; not touching it:", tail0)
        return 1

    write_candidate(lines, idx, new)
    ok, tail, child = build(".self_candidate", CAND_OUT)
    if not ok:
        note(name, cur, new, "ruled-out", "did not pass its checks: " + tail)
        told.append("ruled out: changed that way I do not pass my own checks (%s)" % tail)
        write_up(told)
        print("ruled out (checks):", name, "->", new)
        return 0

    # the cheap trial first: only the games that complained
    if trial_games:
        base_t = levels(child0, trial_games, seed, TRIAL_BUDGET)
        cand_t = levels(child, trial_games, seed, TRIAL_BUDGET)
        if base_t is None or cand_t is None:
            told.append("I could not play the games just now, so I decide nothing about myself")
            write_up(told)
            print("no games could be played; deciding nothing")
            return 1
        told.append("on the %d game%s that showed it: %d levels as I am, %d as I would be"
                    % (len(trial_games), "" if len(trial_games) == 1 else "s", base_t, cand_t))
        if cand_t < base_t:
            note(name, cur, new, "ruled-out", "worse where it hurt: trial %d->%d" % (base_t, cand_t))
            told.append("ruled out there and then: worse where the trouble is")
            write_up(told)
            print("ruled out (trial):", name, "->", new, base_t, "->", cand_t)
            return 0

    # then everywhere, before keeping anything
    base_dev = levels(child0, DEV, seed, FULL_BUDGET)
    base_held = levels(child0, HELD, seed, FULL_BUDGET)
    dev = levels(child, DEV, seed, FULL_BUDGET)
    held = levels(child, HELD, seed, FULL_BUDGET)
    if None in (base_dev, base_held, dev, held):
        told.append("I could not play the games just now, so I decide nothing about myself")
        write_up(told)
        print("no games could be played; deciding nothing")
        return 1
    detail = "dev %d->%d held %d->%d" % (base_dev, dev, base_held, held)
    told.append("everywhere: %s" % detail)
    if (dev > base_dev and held >= base_held) or (dev == base_dev and held > base_held):
        shutil.copy(os.path.join(CAND, "smarsh_self.h"), SELF)
        subprocess.run(["bash", "build_c.sh"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        note(name, cur, new, "adopted", detail)
        told.append("kept: I am now %s = %s" % (name, new))
        with open(JOURNAL, "a", encoding="utf-8") as f:
            f.write("%s  I was %s; I suspected %s in myself, changed it from %s to %s, and kept it (%s)\n"
                    % (stamp(), issue, name, cur, new, detail))
        print("adopted:", name, cur, "->", new, "(", detail, ")")
    else:
        note(name, cur, new, "ruled-out", "no better: " + detail)
        told.append("ruled out: no better once everything is counted")
        print("ruled out:", name, "->", new, "(", detail, ")")
    write_up(told)
    return 0


if __name__ == "__main__":
    sys.exit(main())
