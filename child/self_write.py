"""
self_write.py -- the child reading and rewriting its own source.

It reads certifiable-c/smarsh_self.h, the part of its source declared open to it.
Each line there sets how it reasons and lists (in a "self:" comment) the values it
may take. This does, by elimination, what the child does to a game:

  - the hypotheses are the untried settings of its own source;
  - it picks one it has not tried, uniformly (CIALL_SEED for replay);
  - it writes a candidate copy of its whole source with that one change;
  - an observation rules the change in or out:
      * it must compile, and pass every check in build_c.sh (the child cannot
        edit the checks: build_c.sh and the tests are not in smarsh_self.h);
      * on the development games it must end at least as many levels as the
        current source, and no fewer on the held-back games;
  - a change that passes replaces its real source and is rebuilt; one that fails
    is written down as ruled out and never tried again.

Every attempt is appended to child/self_edits.txt, and adopted changes go to
child/journal.txt in the child's own words. In the cloud each adoption is its
own git commit, so any step can be undone.

Run one attempt:  python child/self_write.py
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
JOURNAL = os.path.join(ROOT, "child", "journal.txt")
CAND = os.path.join(ROOT, ".self_candidate")   # a copy of the source, to try a change in
CAND_OUT = os.path.join(ROOT, ".self_build")
DEV = ["ar25", "cd82", "dc22", "g50t", "lf52", "ls20", "r11l", "s5i5", "sc25", "sp80", "tn36", "tu93", "wa30"]
HELD = ["bp35", "cn04", "ft09", "ka59", "lp85", "m0r0", "re86", "sb26", "sk48", "su15", "tr87", "vc33"]
BUDGET = "3000"
DEFINE = re.compile(r'^(#define\s+(\w+)\s+)(\S+)(\s*/\* self:\s*([^*]+?)\s*\*/)\s*$')


def options():
    """Every (name, current, other value, line index) the child may try."""
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
                seen.add((p[1], p[3]))   # (name, new value) already tested
    return seen


def write_candidate(lines, idx, name, new):
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
    """build_c.sh over a source folder; True and the play_arc path if all checks pass."""
    env = dict(os.environ)
    env["CIALL_SRC"] = src_rel
    env["CIALL_OUT"] = out_dir
    for k in ("CIALL_SEED", "CIALL_TIE", "CIALL_OFF", "CIALL_LIVE", "CIALL_MIND"):
        env.pop(k, None)   # the checks run as the child normally is, not under this attempt
    r = subprocess.run(["bash", "build_c.sh"], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    ok = r.stdout.strip().splitlines()[-1] if r.stdout else ""
    passed = ", 0 failed" in ok or ok.endswith("0 failed")
    return passed, ok, os.path.join(out_dir, "play_arc.exe")


def levels(child, games, seed):
    env = dict(os.environ)
    env["CIALL_CHILD"] = child
    env["CIALL_SEED"] = str(seed)
    env.pop("CIALL_MIND", None)   # a fair test: no remembered routes
    total = 0
    r = subprocess.run([sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py")] + games +
                       ["--budget", BUDGET], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        m = re.search(r"levels finished: (\d+) of", line)
        if m:
            total += int(m.group(1))
    return total


def note(name, cur, new, verdict, detail):
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    with open(LEDGER, "a", encoding="utf-8") as f:
        f.write("%s\t%s\t%s\t%s\t%s\t%s\n" % (stamp, name, cur, new, verdict, detail))


def main():
    seed = int(os.environ.get("CIALL_SEED", "1"))
    random.seed(seed)
    lines, opts = options()
    seen = tried()
    fresh = [o for o in opts if (o[0], o[2]) not in seen]
    if not fresh:
        print("the child has tried every change it can make to itself for now")
        return 0
    name, cur, new, idx = random.choice(fresh)   # uniform over what it has not tried
    print("trying to rewrite itself: %s from %s to %s" % (name, cur, new))

    # what its source scores now, so the change is judged against itself
    ok0, tail0, child0 = build(os.path.basename(SRC), os.path.join(ROOT, ".self_base"))
    if not ok0:   # something else may have had a built program open: once more, then stop
        ok0, tail0, child0 = build(os.path.basename(SRC), os.path.join(ROOT, ".self_base"))
    if not ok0:
        print("its own source does not pass its checks; not touching it:", tail0)
        return 1
    base_dev = levels(child0, DEV, seed)
    base_held = levels(child0, HELD, seed)

    write_candidate(lines, idx, name, new)
    ok, tail, child = build(".self_candidate", CAND_OUT)
    if not ok:
        note(name, cur, new, "ruled-out", "did not pass its checks: " + tail)
        print("ruled out:", name, "->", new, "(", tail, ")")
        return 0
    dev = levels(child, DEV, seed)
    held = levels(child, HELD, seed)
    better = dev > base_dev and held >= base_held
    same_dev = dev == base_dev and held > base_held
    detail = "dev %d->%d held %d->%d" % (base_dev, dev, base_held, held)
    if better or same_dev:
        shutil.copy(os.path.join(CAND, "smarsh_self.h"), SELF)
        subprocess.run(["bash", "build_c.sh"], cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        note(name, cur, new, "adopted", detail)
        stamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
        with open(JOURNAL, "a", encoding="utf-8") as f:
            f.write("%s  I changed myself: %s from %s to %s, and kept it (%s)\n"
                    % (stamp, name, cur, new, detail))
        print("adopted:", name, cur, "->", new, "(", detail, ")")
    else:
        note(name, cur, new, "ruled-out", "no better: " + detail)
        print("ruled out:", name, "->", new, "(", detail, ")")
    return 0


if __name__ == "__main__":
    sys.exit(main())
