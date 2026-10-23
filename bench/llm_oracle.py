"""
The box, for an AI to play through a shell (the language-model arm of the benchmark).

  python3 llm_oracle.py setup STATE_DIR LEVEL SEED    (the experimenter, once)
  python3 llm_oracle.py ask STATE_DIR 101000 2        (a test: which acts, which place)
  python3 llm_oracle.py answer STATE_DIR "and2(0,3) at place 1"
"""
import json, os, sys, time
import numpy as np
from hiddenrule import LEVELS, Family, KIND_NAME, ARITY

MAX_ASKS = 400

def load(d):
    s = json.load(open(os.path.join(d, "state.json")))
    return s, Family(*LEVELS[s["level"]])

def save(d, s):
    json.dump(s, open(os.path.join(d, "state.json"), "w"))

if __name__ == "__main__":
    cmd, d = sys.argv[1], sys.argv[2]
    if cmd == "setup":
        level, seed = sys.argv[3], int(sys.argv[4])
        fam = Family(*LEVELS[level])
        secret = int(np.random.default_rng(seed * 31 + 5).integers(fam.N))
        os.makedirs(d, exist_ok=True)
        save(d, dict(level=level, secret=secret, asks=0, log=[], answered=None, correct=None))
        print(f"{level}: {fam.N} rules, {fam.n} acts, {fam.k} places")
        sys.exit()
    s, fam = load(d)
    if s["answered"] is not None:
        print("already answered"); sys.exit(1)
    if cmd == "ask":
        if s["asks"] >= MAX_ASKS:
            print(f"no tests left ({MAX_ASKS} used)"); sys.exit(1)
        acts, place = sys.argv[3], int(sys.argv[4])
        if len(acts) != fam.n or set(acts) - {"0", "1"} or not 0 <= place < fam.k:
            print(f"bad test: give {fam.n} digits of 0/1 and a place 0..{fam.k-1}"); sys.exit(1)
        bits = np.array([int(c) for c in acts], dtype=np.int8)
        r = bool(fam.predict(np.array([s["secret"]]), bits[None, :], np.array([place]))[0, 0])
        s["asks"] += 1; s["log"].append([acts, place, r]); save(d, s)
        print("CHIME" if r else "silence")
    elif cmd == "answer":
        text = sys.argv[3].replace(" ", "").lower()
        right = fam.describe(s["secret"]).replace(" ", "").lower()
        s["answered"] = text; s["correct"] = text == right; save(d, s)
        print("recorded; tests used:", s["asks"])
