#!/bin/sh
# One unattended stretch of the child, anywhere with a shell: the practice worlds,
# then every ARC-AGI-3 game, remembering what it settled before.
set -e
cd "$(dirname "$0")/.."
./.build/grow.exe 24 child
${PYTHON:-python3} arc/grow_arc.py
