#!/bin/sh
# One unattended stretch of the child, anywhere with a shell: the practice worlds,
# then every ARC-AGI-3 game, remembering what it settled before.
set -e
cd "$(dirname "$0")/.."
./.build/grow.exe 24 child
# it reads itself: what it is made of goes to child/self_map.txt
./.build/self_map.exe certifiable-c/smarsh_core.c certifiable-c/smarsh_core.h certifiable-c/smarsh_ending.c certifiable-c/smarsh_ending.h certifiable-c/smarsh_explore.c certifiable-c/smarsh_explore.h certifiable-c/smarsh_self.h certifiable-c/smarsh_play.c certifiable-c/smarsh_grow.c certifiable-c/play_arc.c certifiable-c/self_map.c > child/self_map.txt
${PYTHON:-python3} arc/grow_arc.py
# one attempt to rewrite itself, unless told not to (child/self_write.off)
if [ ! -f child/self_write.off ]; then ${PYTHON:-python3} child/self_write.py; fi
