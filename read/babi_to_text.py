"""
babi_to_text.py -- relay only: the bAbI stories (facebook's 20 reasoning tasks,
from Hugging Face, Muennighoff/babi) turned into plain lines the child reads:

    T <task>
    S <a sentence of the story>        (one line each)
    Q <the question>
    A <the answer>

Words are lower-cased and the full stops and question marks dropped; nothing else
is changed and nothing is decided here.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def words(s):
    return " ".join(re.findall(r"[a-z0-9']+", s.lower()))

for split in ("train", "test"):
    src = os.path.join(ROOT, "books", "babi_%s.jsonl" % split)
    dst = os.path.join(ROOT, "books", "babi_%s.txt" % split)
    n = 0
    with open(src, encoding="utf-8") as f, open(dst, "w", encoding="utf-8") as out:
        for line in f:
            d = json.loads(line)
            out.write("T %d\n" % d["task"])
            for s in d["passage"].strip().split("\n"):
                if s.strip():
                    out.write("S %s\n" % words(s))
            out.write("Q %s\nA %s\n" % (words(d["question"]), words(d["answer"])))
            n += 1
    print(split, n, "stories ->", dst)
