#!/usr/bin/env python3
"""
study.py -- what the child does between games.

In play, when every rule it has for a kind of thing and an act has been ruled out,
it looks for one fact that splits the times it saw into two sides a rule can stand
on. When no fact it can use in play does that, it writes the whole case down (the
dump). This is where it takes those cases up again, with time, with more words than
play can afford, and with a library.

  1. It re-reads each case with every family of fact it knows, the ones it can use in
     play and the ones it can so far only use here. A family that splits a case is an
     account of it. That is ruled in or out by the sightings themselves, never by who
     suggested it.

  2. It sets how hard each game is from what it could not account for there, and gives
     the hardest games, where study found something new, more practice.

  3. It reads. Sources on the web only SUGGEST. Nothing it reads is taken as true. A
     suggestion becomes an account only when the sightings bear it out, and a source is
     marked by whether its suggestions were borne out. It asks no other mind: what it
     comes to hold, it comes to by ruling out, on its own evidence.

  4. It says why. Every account it keeps is written with what it saw, what it thought,
     what it found, where the idea came from, and what would make it drop the account.

What comes out:
  child/facts.txt          the families of fact to hold in play (read by the child)
  child/knowledge.tsv      every account it keeps, accumulated from run to run
  child/study_journal.txt  the same, in words, with the reasons
  child/words_wanted.txt   what it would need a new word for: the language's edge
  child/study_plan.tsv     how hard each game is, and how much practice it gets
  child/library/           what it read, and whether each thing was borne out

Nothing online is used while a game is played: the games are played from the child's
own library and knowledge, so a score it reports is its own.

  python child/study.py --dumps DIR [--online] [--practice N]
"""
import argparse
import datetime
import glob
import json
import math
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LIB = os.path.join(HERE, "library")
SOURCES = os.path.join(LIB, "sources.jsonl")
CLAIMS = os.path.join(LIB, "claims.tsv")
FACTS = os.path.join(HERE, "facts.txt")
KNOWLEDGE = os.path.join(HERE, "knowledge.tsv")
JOURNAL = os.path.join(HERE, "study_journal.txt")
WANTED = os.path.join(HERE, "words_wanted.txt")
PLAN = os.path.join(HERE, "study_plan.tsv")
PRACTICE = os.path.join(HERE, "practice.tsv")

# ---- the child's own words for rules (as in smarsh_rules.c) --------------------------
REACH, SIDE = 8, 17
MOVES = SIDE * SIDE
GONE, SIZE, COLOUR = MOVES, MOVES + 1, MOVES + 2
NOTHING = REACH * SIDE + REACH
CROP = 12
MID = CROP // 2


def rule_words(r):
    if r == NOTHING:
        return "stay where it is"
    if r < MOVES:
        return "move %d down and %d across" % (r // SIDE - REACH, r % SIDE - REACH)
    if r == GONE:
        return "go"
    if r == SIZE:
        return "change size"
    if r == COLOUR:
        return "change colour"
    q = r - MOVES - 3
    return "move %d down and %d across unless something is in the way" % (q // SIDE - REACH, q % SIDE - REACH)


def seen_words(could):
    if NOTHING in could:
        return "it stayed where it was"
    moves = [r for r in could if r < MOVES]
    if moves:
        return "it moved %d down and %d across" % (moves[0] // SIDE - REACH, moves[0] % SIDE - REACH)
    if GONE in could:
        return "it went"
    if SIZE in could:
        return "it changed size"
    if COLOUR in could:
        return "it changed colour"
    return "it did something none of my words can say"


def outcome_kind(could):
    if NOTHING in could:
        return "stayed"
    if any(r < MOVES for r in could):
        return "moved"
    if GONE in could:
        return "gone"
    if SIZE in could:
        return "size"
    if COLOUR in could:
        return "colour"
    return "other"


# ---- families of fact --------------------------------------------------------------
#
# Each family is a set of facts about one sighting. PLAYABLE families the child can
# hold in play (smarsh_rules.c); the rest it can so far only use here, and a case one
# of those accounts for is a word the child wants.

SIDES = ["above", "below", "to the left of", "to the right of"]


def crop(s):
    if "_grid" not in s:   # decoded once per sighting
        g = s["crop"]
        s["_grid"] = [[16 if g[r * CROP + c] == "x" else int(g[r * CROP + c], 16) for c in range(CROP)]
                      for r in range(CROP)]
    return s["_grid"]


def facts_of_family(name, sightings):
    """Every (arg, holds(sighting), words) of one family, for these sightings."""
    out = []
    if name == "touch":
        for v in range(16):
            out.append((v, lambda s, v=v: (s["touch"] >> v) & 1 == 1, "whether it was touching colour %d" % v))
    elif name == "ahead":
        for d in range(4):
            for v in range(16):
                out.append((d * 16 + v, lambda s, d=d, v=v: (s["ahead"][d] >> v) & 1 == 1,
                            "whether colour %d was right %s it" % (v, SIDES[d])))
    elif name == "cycle":
        for p in (2, 3, 4):
            for r in range(p):
                out.append((p * 16 + r, lambda s, p=p, r=r: s["nth"] % p == r,
                            "whether it was time %d of every %d" % (r + 1, p)))
    elif name == "nth":
        for m in sorted({s["nth"] for s in sightings})[1:]:
            out.append((m, lambda s, m=m: s["nth"] >= m,
                        "whether this had already happened %d times: something unseen changed then" % m))
    elif name == "level":
        for L in sorted({s["level"] for s in sightings})[1:]:
            out.append((L, lambda s, L=L: s["level"] >= L, "whether it was level %d yet" % (L + 1)))
    elif name == "near":
        for v in range(16):
            for d in (2, 3, 4):
                def holds(s, v=v, d=d):
                    g = crop(s)
                    return any(g[r][c] == v for r in range(MID - d, MID + d + 1) for c in range(MID - d, MID + d + 1)
                               if 0 <= r < CROP and 0 <= c < CROP)
                out.append((v * 16 + d, holds, "whether colour %d was within %d cells of it" % (v, d)))
    elif name == "line":
        for v in range(16):
            def holds(s, v=v):
                g = crop(s)
                return any(g[MID][c] == v for c in range(CROP)) or any(g[r][MID] == v for r in range(CROP))
            out.append((v, holds, "whether colour %d was in line with it, the same row or column" % v))
    elif name == "size":
        for n in sorted({s["cells"] for s in sightings})[1:]:
            out.append((n, lambda s, n=n: s["cells"] >= n, "whether it was at least %d cells big" % n))
    elif name == "many":
        for v in range(16):
            for k in (2, 4, 8, 16):
                def holds(s, v=v, k=k):
                    return sum(row.count(v) for row in crop(s)) >= k
                out.append((v * 16 + k, holds, "whether there were at least %d cells of colour %d around it" % (k, v)))
    return out


# The same several kinds of sense as the goals (smarsh_goal.h), plus the one the rules
# need that the goals do not: what a thing turns into. Every family, and every word it
# wants, belongs to one; a modality with nothing in it is a kind of sense it lacks.
MODALITY = {
    "touch": "others", "ahead": "space", "near": "space", "line": "pattern",
    "nth": "time", "cycle": "time", "level": "time",
    "size": "number", "many": "number",
    "follow": "others", "appears": "others", "teleport": "space",
    "rotate": "transformation", "becomes-size": "transformation", "becomes-colour": "transformation",
    "merge-split": "transformation",
}
MODALITIES = ["space", "number", "body", "time", "pattern", "others", "transformation"]
# what the last sighting of a case no family reaches says about the sense it needed
UNREACHED_MODALITY = {"size": "transformation", "colour": "transformation", "other": "transformation",
                      "moved": "space", "stayed": "space", "gone": "others"}

PLAYABLE = ["touch", "nth", "level", "ahead", "cycle"]
STUDY_ONLY = ["near", "line", "size", "many"]
FIRST_FOUR = ["touch", "nth", "level", "misread"]   # what the child holds when nothing says otherwise


def split(sightings, holds):
    """Rules standing on each side of a fact, or None if the fact does not split them."""
    yes, no, ny, nn = None, None, 0, 0
    for s in sightings:
        c = set(s["could"])
        if holds(s):
            yes = c if yes is None else yes & c
            ny += 1
        else:
            no = c if no is None else no & c
            nn += 1
    if ny == 0 or nn == 0 or not yes or not no:
        return None
    return yes, no, ny, nn


def side_words(rules):
    if len(rules) == 1:
        return rule_words(next(iter(rules)))
    return "do any of %d things" % len(rules)


def account_for(case, families):
    """Every fact, in these families, that splits this case."""
    found = []
    for fam in families:
        for arg, holds, words in facts_of_family(fam, case["sightings"]):
            got = split(case["sightings"], holds)
            if got:
                found.append({"family": fam, "arg": arg, "words": words, "yes": got[0], "no": got[1],
                              "ny": got[2], "nn": got[3]})
    return found


# ---- the library: it reads, and believes nothing it reads ---------------------------

# the words for mechanics a source might use, and which family each suggests
MECHANICS = [
    (r"\bpush(es|ed|ing)?\b", "ahead"), (r"\bblock(s|ed|ing)?\b", "ahead"), (r"\bobstacles?\b", "ahead"),
    (r"\bwalls?\b", "ahead"), (r"\bgravity\b", "ahead"), (r"\bfall(s|ing)?\b", "ahead"),
    (r"\bswitch(es|ed)?\b", "cycle"), (r"\btoggl(e|es|ed|ing)\b", "cycle"), (r"\blevers?\b", "cycle"),
    (r"\balternat(e|es|ing)\b", "cycle"), (r"\bcount(s|er|ers|ing)?\b", "nth"), (r"\btimers?\b", "nth"),
    (r"\bkeys?\b", "touch"), (r"\bdoors?\b", "touch"), (r"\block(s|ed)?\b", "touch"),
    (r"\bcollect(s|ed|ing)?\b", "touch"), (r"\bpick(s|ed)? up\b", "touch"),
    (r"\bnear(by)?\b", "near"), (r"\badjacent\b", "near"), (r"\bproximity\b", "near"),
    (r"\bline(s|d)?\b", "line"), (r"\balign(s|ed)?\b", "line"), (r"\brows?\b", "line"),
    (r"\bfollow(s|ed|ing)?\b", "WANT:follow"), (r"\bchas(e|es|ing)\b", "WANT:follow"),
    (r"\benem(y|ies)\b", "WANT:follow"), (r"\bteleport(s|ed|ing)?\b", "WANT:teleport"),
    (r"\bportals?\b", "WANT:teleport"), (r"\brotat(e|es|ed|ing|ion)\b", "WANT:rotate"),
    (r"\bgrow(s|ing)?\b", "WANT:becomes-size"), (r"\bshrink(s|ing)?\b", "WANT:becomes-size"),
    (r"\bpaint(s|ed|ing)?\b", "WANT:becomes-colour"), (r"\bmerg(e|es|ed|ing)\b", "WANT:merge-split"),
    (r"\bsplit(s|ting)?\b", "WANT:merge-split"), (r"\bspawn(s|ed|ing)?\b", "WANT:appears"),
]

QUERIES = {
    "moved": ["Sokoban", "puzzle video game block pushing"],
    "stayed": ["puzzle video game obstacle", "tile-based video game collision"],
    "gone": ["puzzle video game collectible item", "Pac-Man"],
    "size": ["puzzle video game object grows", "Snake (video game genre)"],
    "colour": ["puzzle video game tile colour change", "Lights Out (game)"],
    "other": ["abstraction and reasoning corpus", "puzzle video game mechanics"],
}


UNREACHABLE = set()   # sources that refused this run: not asked again until the next


def fetch(url, timeout=20):
    """One request, spaced from the last; a refusal to go faster is waited out once, then respected."""
    import urllib.error
    host = urllib.parse.urlparse(url).netloc
    if host in UNREACHABLE:
        raise RuntimeError("%s refused earlier this run" % host)
    for attempt in range(2):
        time.sleep(1.5)   # a library is not to be read at the speed of a machine
        req = urllib.request.Request(url, headers={"User-Agent": "ciall-child-study/1.0 (research; github.com/pra-ji-moh/ciall)"})
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                return r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            if e.code == 429 and attempt == 0:
                time.sleep(int(e.headers.get("Retry-After", "20") or 20))
                continue
            if e.code in (403, 406, 429):
                UNREACHABLE.add(host)
            raise


def read_sources():
    out = []
    if os.path.exists(SOURCES):
        with open(SOURCES, encoding="utf-8") as f:
            for line in f:
                try:
                    out.append(json.loads(line))
                except ValueError:
                    pass
    return out


def library_read(queries, say):
    """Fetch what it has not read in the last week. Nothing here is believed; it is kept to be tested."""
    os.makedirs(LIB, exist_ok=True)
    have = read_sources()
    fresh = {(s["query"], s["url"]) for s in have
             if time.time() - s.get("fetched_at", 0) < 7 * 86400}
    asked = {s["query"] for s in have if time.time() - s.get("fetched_at", 0) < 7 * 86400}
    added = 0
    with open(SOURCES, "a", encoding="utf-8") as out:
        for q in queries:
            if q in asked:
                continue
            try:
                api = ("https://en.wikipedia.org/w/api.php?action=query&list=search&format=json&srlimit=3&srsearch="
                       + urllib.parse.quote(q))
                hits = json.loads(fetch(api)).get("query", {}).get("search", [])
                for h in hits:
                    title = h["title"]
                    url = "https://en.wikipedia.org/wiki/" + urllib.parse.quote(title.replace(" ", "_"))
                    if (q, url) in fresh:
                        continue
                    ex = ("https://en.wikipedia.org/w/api.php?action=query&prop=extracts&exintro=1&explaintext=1"
                          "&format=json&titles=" + urllib.parse.quote(title))
                    pages = json.loads(fetch(ex)).get("query", {}).get("pages", {})
                    text = " ".join(p.get("extract", "") for p in pages.values())
                    rec = {"query": q, "url": url, "title": title, "text": text[:6000], "fetched_at": time.time(),
                           "from": "wikipedia"}
                    out.write(json.dumps(rec) + "\n")
                    added += 1
                    time.sleep(0.5)
            except Exception as e:   # the library being out of reach is not a reason to stop studying
                say("  could not read about %r: %s" % (q, e))
            try:
                ax = ("http://export.arxiv.org/api/query?max_results=2&search_query="
                      + urllib.parse.quote("all:%s" % q))
                feed = fetch(ax)
                for entry in re.findall(r"<entry>(.*?)</entry>", feed, re.S):
                    title = re.sub(r"\s+", " ", (re.findall(r"<title>(.*?)</title>", entry, re.S) or [""])[0]).strip()
                    url = (re.findall(r"<id>(.*?)</id>", entry, re.S) or [""])[0].strip()
                    summary = re.sub(r"\s+", " ", (re.findall(r"<summary>(.*?)</summary>", entry, re.S) or [""])[0])
                    if not url or (q, url) in fresh:
                        continue
                    rec = {"query": q, "url": url, "title": title, "text": summary[:6000], "fetched_at": time.time(),
                           "from": "arxiv"}
                    out.write(json.dumps(rec) + "\n")
                    added += 1
                time.sleep(3.0)   # arXiv asks for a pause between requests
            except Exception as e:
                say("  could not read arXiv about %r: %s" % (q, e))
    return added


def library_suggestions(sources):
    """What the library suggests: (family, sentence, source). A suggestion, not a fact."""
    out = []
    for s in sources:
        for sentence in re.split(r"(?<=[.!?])\s+", s.get("text", "")):
            for pat, fam in MECHANICS:
                if re.search(pat, sentence, re.I):
                    out.append((fam, sentence.strip()[:300], s["url"], s.get("title", "")))
    return out


# ---- the study -------------------------------------------------------------------

def load_cases(dumps):
    cases = []
    for path in sorted(glob.glob(os.path.join(dumps, "*.jsonl"))):
        game = os.path.splitext(os.path.basename(path))[0]
        with open(path, encoding="utf-8") as f:
            for line in f:
                try:
                    c = json.loads(line)
                except ValueError:
                    continue
                if len(c.get("sightings", [])) >= 2:
                    c["game"] = game
                    cases.append(c)
    return cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dumps", default=os.path.join(ROOT, ".build", "dumps"))
    ap.add_argument("--online", action="store_true", help="read sources on the web")
    ap.add_argument("--practice", type=int, default=0, help="games to practise on after study")
    args = ap.parse_args()

    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    journal = []

    def say(line):
        journal.append(line)
        print(line)

    cases = load_cases(args.dumps)
    say("== study, %s: %d cases it could not account for in play, from %d games" %
        (now, len(cases), len({c["game"] for c in cases})))
    if not cases:
        say("nothing to study: play with CIALL_DEADDUMP_DIR set first")
        return

    # 1. every family it knows, the ones for play and the ones for study
    kept, by_family, unexplained = [], {}, []
    for c in cases:
        found = account_for(c, PLAYABLE + STUDY_ONLY)
        c["found"] = found
        for a in found:
            by_family.setdefault(a["family"], set()).add(id(c))
        if found:
            kept.append(c)
        else:
            unexplained.append(c)
    say("")
    say("families of fact, and how many cases each accounts for:")
    for fam in PLAYABLE + STUDY_ONLY:
        n = len(by_family.get(fam, ()))
        say("  %-6s %4d   %s" % (fam, n, "(usable in play)" if fam in PLAYABLE else "(study only: a word it wants)"))
    say("accounted for: %d of %d; still with no account: %d" % (len(kept), len(cases), len(unexplained)))

    # 2. how hard each game is, and practice to match
    games = {}
    for c in cases:
        g = games.setdefault(c["game"], {"cases": 0, "unexplained": 0, "bits": 0.0, "newly": 0})
        g["cases"] += 1
        g["bits"] += math.log2(1 + len(c["sightings"]))
        if not c["found"]:
            g["unexplained"] += 1
        elif any(a["family"] in PLAYABLE for a in c["found"]):
            g["newly"] += 1
    ranked = sorted(games.items(), key=lambda kv: -kv[1]["bits"])
    total = sum(g["bits"] for _, g in ranked) or 1.0
    with open(PLAN, "w", encoding="utf-8") as f:
        f.write("game\tcases\tunexplained\tdifficulty_bits\tnewly_playable\tpractice_runs\n")
        for name, g in ranked:
            # more practice where it is hard AND study found something it can now use in play
            runs = 1 + int(round(3 * g["bits"] / total)) if g["newly"] else 0
            g["runs"] = runs
            f.write("%s\t%d\t%d\t%.1f\t%d\t%d\n" % (name, g["cases"], g["unexplained"], g["bits"], g["newly"], runs))
    say("")
    say("how hard each game is (bits of evidence it could not account for) and practice given:")
    for name, g in ranked[:8]:
        say("  %-6s %6.1f bits, %d cases, %d newly playable, %d practice runs" %
            (name, g["bits"], g["cases"], g["newly"], g["runs"]))

    # 3. the library: suggestions, tested against the sightings
    suggestions = []
    if args.online:
        queries = sorted({q for c in cases for q in QUERIES[outcome_kind(c["sightings"][-1]["could"])]})
        say("")
        say("reading: %d questions" % len(queries))
        added = library_read(queries, say)
        say("  %d new sources" % added)
    sources = read_sources()
    for fam, sentence, url, title in library_suggestions(sources):
        suggestions.append({"family": fam, "says": sentence, "source": url, "title": title, "by": "library"})

    # judge the suggestions: a family is borne out if it accounts for any case; untestable ones are words wanted
    judged = {}
    for s in suggestions:
        fam = s["family"]
        if fam.startswith("want:"):
            fam = fam[5:]
            s["family"] = "WANT:" + fam
        known = fam in PLAYABLE + STUDY_ONLY
        if known:
            n = len(by_family.get(fam, ()))
            s["status"] = ("borne out: accounts for %d cases" % n) if n else ("not borne out by %d cases" % len(cases))
        else:
            s["status"] = "cannot test: I do not have a word for it"
        judged.setdefault((s["family"], s["status"]), []).append(s)
    os.makedirs(LIB, exist_ok=True)
    with open(CLAIMS, "w", encoding="utf-8") as f:
        f.write("family\tstatus\tby\tsource\tsays\n")
        for (fam, status), group in sorted(judged.items()):
            for s in group[:5]:
                f.write("%s\t%s\t%s\t%s\t%s\n" % (fam, status, s["by"], s["source"],
                                                  s["says"].replace("\t", " ").replace("\n", " ")))
    if suggestions:
        say("")
        say("what it read and was told, and whether its own sightings bore it out:")
        for (fam, status), group in sorted(judged.items(), key=lambda kv: -len(kv[1])):
            say("  %-22s %-44s (%d suggestions, e.g. %s)" % (fam, status, len(group), group[0]["title"][:40]))

    # 4. what to hold in play: the first four, and every playable family that accounted for something
    hold = list(FIRST_FOUR)
    for fam in PLAYABLE:
        if fam not in hold and by_family.get(fam):
            hold.append(fam)
    with open(FACTS, "w", encoding="utf-8") as f:
        f.write("\n".join(hold) + "\n")
    say("")
    say("holding in play from now: %s" % ", ".join(hold))

    # the edge of its language: what it could only use here, and what no family reaches
    wanted = {}
    for fam in STUDY_ONLY:
        if by_family.get(fam):
            wanted[fam] = "accounts for %d cases in study, but cannot be held in play yet" % len(by_family[fam])
    for (fam, status), group in judged.items():
        if fam.startswith("WANT:") or status.startswith("cannot test"):
            wanted.setdefault(fam.replace("WANT:", ""), "suggested %d times (%s); I have no word to test it with" %
                              (len(group), group[0]["by"]))
    kinds = {}
    for c in unexplained:
        k = outcome_kind(c["sightings"][-1]["could"])
        kinds[k] = kinds.get(k, 0) + 1
    # the same, by kind of sense: which the child has words for, and which it lacks
    by_mod = {m: {"accounted": 0, "wanted": [], "unreached": 0} for m in MODALITIES}
    for fam in PLAYABLE + STUDY_ONLY:
        by_mod[MODALITY.get(fam, "others")]["accounted"] += len(by_family.get(fam, ()))
    for fam in wanted:
        by_mod[MODALITY.get(fam, "others")]["wanted"].append(fam)
    for k, n in kinds.items():
        by_mod[UNREACHED_MODALITY.get(k, "others")]["unreached"] += n
    say("")
    say("by kind of sense: cases its words account for / words it wants / cases nothing reaches:")
    for m in MODALITIES:
        b = by_mod[m]
        flag = "   <- a sense it lacks" if b["accounted"] == 0 and (b["wanted"] or b["unreached"]) else ""
        say("  %-15s %4d / %-38s / %3d%s" % (m, b["accounted"], ", ".join(b["wanted"]) or "-", b["unreached"], flag))
    with open(WANTED, "w", encoding="utf-8") as f:
        f.write("# the edge of the child's language, %s\n" % now)
        f.write("# by kind of sense (cases accounted for, words wanted, cases nothing reaches):\n")
        for m in MODALITIES:
            b = by_mod[m]
            f.write("#   %-15s %d | %s | %d\n" % (m, b["accounted"], ", ".join(b["wanted"]) or "-", b["unreached"]))
        f.write("# words it wants: what study could use but play cannot, and what was suggested it cannot test\n")
        for fam, why in sorted(wanted.items()):
            f.write("%s\t%s\n" % (fam, why))
        f.write("# cases no family reaches at all, by what happened in the last sighting\n")
        for k, n in sorted(kinds.items(), key=lambda kv: -kv[1]):
            f.write("unreached:%s\t%d cases\n" % (k, n))

    # every account kept, with why: compounded from run to run
    new_file = not os.path.exists(KNOWLEDGE)
    with open(KNOWLEDGE, "a", encoding="utf-8") as f:
        if new_file:
            f.write("when\tgame\tcolour\tcells\tact\tfamily\targ\tsightings\tif_so\tif_not\tsuggested_by\n")
        for c in kept:
            for a in c["found"][:3]:
                by = next((s["by"] for s in suggestions if s["family"] == a["family"]), "its own study")
                f.write("%s\t%s\t%d\t%d\t%d\t%s\t%d\t%d\t%s\t%s\t%s\n" %
                        (now, c["game"], c["colour"], c["cells"], c["act"], a["family"], a["arg"],
                         len(c["sightings"]), side_words(a["yes"]), side_words(a["no"]), by))
    say("")
    say("why, case by case (the first few):")
    for c in kept[:12]:
        a = c["found"][0]
        by = next((s for s in suggestions if s["family"] == a["family"]), None)
        say("  colour %d, act %d, in %s: %s. In play I had no rule that says so. Studying the %d times, it turns "
            "on %s: if so, it would %s; if not, it would %s. I hold this because every one of the %d sightings "
            "agrees with it%s. The first time that fact holds and it does something else, I drop it." %
            (c["colour"], c["act"], c["game"], seen_words(c["sightings"][-1]["could"]), len(c["sightings"]),
             a["words"], side_words(a["yes"]), side_words(a["no"]), len(c["sightings"]),
             (", and the idea was suggested by %s (%s)" % (by["by"], by["title"][:50])) if by else ""))
    for c in unexplained[:5]:
        say("  colour %d, act %d, in %s: %s, and no fact in any family I have splits the %d times. "
            "I need a word I do not have." %
            (c["colour"], c["act"], c["game"], seen_words(c["sightings"][-1]["could"]), len(c["sightings"])))

    with open(JOURNAL, "a", encoding="utf-8") as f:
        f.write("\n".join(journal) + "\n\n")

    # practice: the games study found something new for, with what it now holds, against without
    if args.practice > 0:
        chosen = [n for n, g in ranked if g["runs"] > 0][:args.practice]
        if chosen:
            say("")
            say("practice, with what study found against without:")
            child = os.environ.get("CIALL_CHILD") or os.path.join(ROOT, ".build", "play_arc.exe")
            rows = []
            for game in chosen:
                res = {}
                for arm, facts in (("without", ""), ("with", FACTS)):
                    env = dict(os.environ, CIALL_CHILD=child, CIALL_FACTS=facts)
                    env.pop("CIALL_DEADDUMP_DIR", None)
                    out = subprocess.run([sys.executable, os.path.join(ROOT, "arc", "arc_bridge.py"), game,
                                          "--budget", "300"], cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                         stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace").stdout
                    lv = re.search(r"levels finished: (\d+) of (\d+)", out)
                    un = re.search(r"(\d+) it could not account for", out)
                    said = re.search(r"said from those accounts: (\d+), right (\d+)", out)
                    res[arm] = (int(lv.group(1)) if lv else 0, int(un.group(1)) if un else -1,
                                "%s/%s" % (said.group(2), said.group(1)) if said else "-")
                say("  %-6s without: %d levels, %d unaccounted, right %s  |  with: %d levels, %d unaccounted, right %s" %
                    ((game,) + res["without"] + res["with"]))
                rows.append((now, game) + res["without"] + res["with"])
            new_file = not os.path.exists(PRACTICE)
            with open(PRACTICE, "a", encoding="utf-8") as f:
                if new_file:
                    f.write("when\tgame\tlevels_without\tunaccounted_without\tright_without\t"
                            "levels_with\tunaccounted_with\tright_with\n")
                for r in rows:
                    f.write("\t".join(str(x) for x in r) + "\n")
            with open(JOURNAL, "a", encoding="utf-8") as f:
                f.write("\n".join(journal[-(len(rows) + 2):]) + "\n\n")


if __name__ == "__main__":
    main()
