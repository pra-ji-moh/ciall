#!/bin/sh
# Builds only what the unattended child needs: the practice-world grower and the
# ARC-AGI-3 player. Used where the full build_c.sh (every test) is not wanted.
set -e
cd "$(dirname "$0")/.."
CC=${CC:-cc}
FLAGS="-std=c99 -Wall -Wextra -pedantic -O2"
mkdir -p .build
(cd certifiable-c && $CC $FLAGS smarsh_core.c smarsh_play.c arc_worldgen.c smarsh_grow.c grow.c -o ../.build/grow.exe -lm)
(cd certifiable-c && $CC $FLAGS smarsh_core.c smarsh_ending.c smarsh_explore.c play_arc.c -o ../.build/play_arc.exe -lm)
(cd certifiable-c && $CC $FLAGS self_map.c -o ../.build/self_map.exe -lm)
echo "built .build/grow.exe, .build/play_arc.exe and .build/self_map.exe"
