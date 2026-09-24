#!/bin/sh
# One unattended stretch of the child, anywhere with a shell: the practice worlds,
# then every ARC-AGI-3 game, remembering what it settled before.
set -e
cd "$(dirname "$0")/.."
./.build/grow.exe 24 child
# it reads itself: what it is made of goes to child/self_map.txt
./.build/self_map.exe certifiable-c/smarsh_core.c certifiable-c/smarsh_core.h certifiable-c/smarsh_ending.c certifiable-c/smarsh_ending.h certifiable-c/smarsh_explore.c certifiable-c/smarsh_explore.h certifiable-c/smarsh_self.h certifiable-c/smarsh_play.c certifiable-c/smarsh_grow.c certifiable-c/play_arc.c certifiable-c/self_map.c > child/self_map.txt
# it plays with the kinds of fact its last study said to hold (child/facts.txt), and
# writes down every case it could not account for, for study after; the kinds of goal
# still standing when a level ends go to child/goal_kinds.tsv, for other games to try first
rm -rf .build/dumps
CIALL_FACTS="$PWD/child/facts.txt" CIALL_GOALKINDS="$PWD/child/goal_kinds.tsv" CIALL_DEADDUMP_DIR="$PWD/.build/dumps" ${PYTHON:-python3} arc/grow_arc.py
# it studies what it could not account for: every family of fact it knows, the library,
# and practice on the hardest games
${PYTHON:-python3} child/study.py --dumps .build/dumps --online --practice 2 || echo "its study did not finish this hour"
# it reads: 500 more stories from Hugging Face each hour, then is tested on ones it never read
if ${PYTHON:-python3} read/fetch_books.py more 500; then
  ./.build/read_books.exe books/tinystories.txt > child/reading_now.txt || true
  printf '%s  ' "$(date -u +%Y-%m-%dT%H:%M)" >> child/reading.txt
  grep -E "read .*stories|SHOWN BOTH" child/reading_now.txt | sed -n '7,8p' | tr -s ' ' | paste -sd' ' - >> child/reading.txt
fi
# it learns to understand stories: bAbI from Hugging Face, choosing what it is curious about
if [ ! -f books/babi_train.txt ]; then
  for s in train test; do
    curl -sL -o books/babi_$s.jsonl "https://huggingface.co/datasets/Muennighoff/babi/resolve/main/babi_$s.jsonl" || true
  done
  ${PYTHON:-python3} read/babi_to_text.py || true
fi
if [ -f books/babi_train.txt ]; then ./.build/grasp_books.exe --curious 300 || true; fi
# one attempt to rewrite itself, unless told not to (child/self_write.off)
# a refusal to touch itself (its checks did not pass, or no game answered) is a
# decision, not a failure of the hour: what it played is still kept
# it builds itself: one generation of the archive of every version of itself (child/self_evolve.py)
if [ ! -f child/self_write.off ]; then ${PYTHON:-python3} child/self_evolve.py || echo "it did not build a new version of itself this hour"; fi
