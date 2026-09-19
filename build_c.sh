#!/usr/bin/env bash
# build_c.sh -- compile and check every C file in certifiable-c/ and native/.
#
# No Python needed. Each test checks itself; each demo's output is compared
# with a frozen copy in golden/ (outputs that were first checked, line for
# line, against the Python originals now kept in python-reference/). The
# diff_kernel and diff_learner goldens are the Python outputs themselves.
#
#   ./build_c.sh            everything except the slow baseline (~1 min)
#   ./build_c.sh --slow     also train the baseline network and compare it
#
# Compiler: $CC if set, else `zig cc` on the PATH, else the Zig 0.16.0 in
# ~/tools/zig-0.16.0 (where this was built), else gcc, else cc. Every file is
# built as strict C99 with warnings on, and a warning counts as a failure.
#
# test_frontend compares the C front end with the JavaScript one, so it
# needs `node`; it is skipped, and says so, when node is absent.

set -u
cd "$(dirname "$0")"
ROOT=$(pwd)
# CIALL_SRC / CIALL_OUT: check a candidate copy of the source (the child's own rewrite)
# in another folder, without touching the real build.
OUT="${CIALL_OUT:-$ROOT/.build}"
mkdir -p "$OUT"

if [ -n "${CC:-}" ]; then :;
elif command -v zig >/dev/null 2>&1; then CC="zig cc";
elif [ -x "$HOME/tools/zig-0.16.0/zig.exe" ]; then CC="$HOME/tools/zig-0.16.0/zig.exe cc";
elif command -v gcc >/dev/null 2>&1; then CC=gcc;
else CC=cc; fi
FLAGS="-std=c99 -Wall -Wextra -pedantic -O2"

pass=0; fail=0
ok()  { echo "  ok    $1"; pass=$((pass + 1)); }
bad() { echo "  FAIL  $1"; fail=$((fail + 1)); }

# build NAME DIR FILES... : compile, treating any warning as a failure
build() {
  local name=$1 dir=$2; shift 2
  local log="$OUT/$name.log"
  if (cd "$dir" && $CC $FLAGS "$@" -o "$OUT/$name.exe" -lm) >"$log" 2>&1 && [ ! -s "$log" ]; then
    return 0
  fi
  bad "$name does not compile cleanly:"; sed 's/^/        /' "$log" | head -20
  return 1
}

# run_test NAME DIR : a self-checking program, must exit 0
run_test() {
  local name=$1 dir=$2
  if (cd "$dir" && "$OUT/$name.exe" >"$OUT/$name.out" 2>&1); then
    ok "$name  ($(tail -1 "$OUT/$name.out" | tr -d '\r'))"
  else
    bad "$name"; grep -i "fail" "$OUT/$name.out" | head -10 | sed 's/^/        /'
  fi
}

# run_golden NAME DIR GOLDEN [ARGS...] : output must equal the frozen copy
run_golden() {
  local name=$1 dir=$2 golden=$3; shift 3
  (cd "$dir" && "$OUT/$name.exe" "$@") 2>&1 | tr -d '\r' >"$OUT/$name.out"
  if diff -q "$OUT/$name.out" "$dir/golden/$golden" >/dev/null; then
    ok "$name  (output identical to golden/$golden)"
  else
    bad "$name  (output differs from golden/$golden)"
    diff "$dir/golden/$golden" "$OUT/$name.out" | head -10 | sed 's/^/        /'
  fi
}

echo "compiler: $CC"
echo
echo "certifiable-c/"
C="${CIALL_SRC:-certifiable-c}"
build test_smarsh_core   $C smarsh_core.c test_smarsh_core.c                  && run_test test_smarsh_core $C
build test_smarsh_reason $C smarsh_core.c smarsh_reason.c test_smarsh_reason.c && run_test test_smarsh_reason $C
build test_learner       $C smarsh_core.c smarsh_reason.c smarsh_learner.c test_learner.c && run_test test_learner $C
build test_boundary_kernel $C boundary_kernel.c test_boundary_kernel.c        && run_test test_boundary_kernel $C
for d in demo_desk demo_program demo_baby; do
  build $d $C smarsh_core.c smarsh_reason.c $d.c && run_golden $d $C $d.txt
done
build demo_learner $C smarsh_core.c smarsh_reason.c smarsh_learner.c demo_learner.c && run_golden demo_learner $C demo_learner.txt
build test_concept $C smarsh_core.c smarsh_reason.c smarsh_concept.c test_concept.c && run_test test_concept $C
build demo_ladder $C smarsh_core.c smarsh_reason.c smarsh_concept.c demo_ladder.c && run_golden demo_ladder $C demo_ladder.txt
build test_analogy $C smarsh_core.c smarsh_reason.c smarsh_analogy.c test_analogy.c && run_test test_analogy $C
build demo_analogy $C smarsh_core.c smarsh_reason.c smarsh_analogy.c demo_analogy.c && run_golden demo_analogy $C demo_analogy.txt
build test_informant $C smarsh_core.c smarsh_reason.c smarsh_analogy.c smarsh_informant.c test_informant.c && run_test test_informant $C
build demo_informant $C smarsh_core.c smarsh_reason.c smarsh_analogy.c smarsh_informant.c demo_informant.c && run_golden demo_informant $C demo_informant.txt
build test_laws $C smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_law.c test_laws.c && run_test test_laws $C
build demo_laws $C smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_law.c demo_laws.c && run_golden demo_laws $C demo_laws.txt
build test_proof $C smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_law.c smarsh_proof.c test_proof.c && run_test test_proof $C
build demo_proof $C smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_law.c smarsh_proof.c demo_proof.c && run_golden demo_proof $C demo_proof.txt
build test_abstract $C smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_abstract.c test_abstract.c && run_test test_abstract $C
build demo_abstract $C smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_abstract.c demo_abstract.c && run_golden demo_abstract $C demo_abstract.txt
build test_time $C smarsh_core.c smarsh_time.c test_time.c && run_test test_time $C
build demo_time $C smarsh_core.c smarsh_time.c demo_time.c && run_golden demo_time $C demo_time.txt
build test_space $C smarsh_core.c smarsh_interval.c smarsh_space.c test_space.c && run_test test_space $C
CHILD="smarsh_core.c smarsh_reason.c smarsh_concept.c smarsh_abstract.c smarsh_law.c smarsh_proof.c smarsh_child.c"
build test_child $C $CHILD test_child.c && run_test test_child $C
build demo_child $C $CHILD demo_child.c && run_golden demo_child $C demo_child.txt
build demo_stem $C smarsh_core.c smarsh_interval.c smarsh_space.c demo_stem.c && run_golden demo_stem $C demo_stem.txt
build demo_integrated $C smarsh_core.c smarsh_interval.c smarsh_space.c demo_integrated.c && run_golden demo_integrated $C demo_integrated.txt
build test_frame $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_frame.c test_frame.c && run_test test_frame $C
build demo_frame $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_frame.c demo_frame.c && run_golden demo_frame $C demo_frame.txt
build test_object $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_object.c test_object.c && run_test test_object $C
build demo_object $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_object.c demo_object.c && run_golden demo_object $C demo_object.txt
build test_play $C smarsh_core.c smarsh_play.c arc_standin.c arc_worldgen.c test_play.c && run_test test_play $C
build demo_play $C smarsh_core.c smarsh_play.c arc_standin.c demo_play.c && run_golden demo_play $C demo_play.txt
build test_grow $C smarsh_core.c smarsh_play.c arc_worldgen.c smarsh_grow.c test_grow.c && run_test test_grow $C
build grow $C smarsh_core.c smarsh_play.c arc_worldgen.c smarsh_grow.c grow.c
build test_ending $C smarsh_core.c smarsh_ending.c test_ending.c && run_test test_ending $C
build test_guess $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_frame.c smarsh_guess.c test_guess.c && run_test test_guess $C
build self_map $C self_map.c
build test_explore $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_frame.c smarsh_guess.c smarsh_ending.c smarsh_explore.c test_explore.c && run_test test_explore $C
build play_arc $C smarsh_core.c smarsh_interval.c smarsh_space.c smarsh_frame.c smarsh_guess.c smarsh_ending.c smarsh_explore.c play_arc.c
# they need node and the Smarsh repo beside this one (C:/Users/USER/smarsh)
has_smarsh() { command -v node >/dev/null 2>&1 && [ -f "$HOME/smarsh/bin/smarsh.mjs" -o -f "/c/Users/USER/smarsh/bin/smarsh.mjs" -o -f "C:/Users/USER/smarsh/bin/smarsh.mjs" ]; }
if has_smarsh; then
  build test_frontend $C smarsh_lexer.c smarsh_ast.c smarsh_parser.c smarsh_value.c smarsh_interp.c test_frontend.c \
    && run_test test_frontend $C
else
  echo "  skip  test_frontend (needs node and the Smarsh repo for the JavaScript side)"
fi

build test_null_args $C smarsh_core.c smarsh_reason.c smarsh_learner.c smarsh_concept.c smarsh_analogy.c smarsh_informant.c smarsh_law.c smarsh_proof.c smarsh_abstract.c smarsh_time.c smarsh_interval.c smarsh_space.c smarsh_frame.c smarsh_object.c smarsh_play.c arc_worldgen.c smarsh_grow.c smarsh_guess.c smarsh_ending.c smarsh_explore.c smarsh_child.c test_null_args.c \
  && run_test test_null_args $C

# C against the Python reference, without Python: these goldens ARE the
# Python outputs (python-reference/diff_kernel.py, diff_learner.py).
build diff_kernel $C smarsh_core.c smarsh_reason.c diff_kernel.c \
  && run_golden diff_kernel $C diff_kernel.txt
build diff_learner $C smarsh_core.c smarsh_reason.c smarsh_learner.c diff_learner.c \
  && run_golden diff_learner $C diff_learner.txt
# C against Smarsh's JavaScript speculation gate
if has_smarsh; then
  build diff_core $C smarsh_core.c diff_core.c && run_test diff_core $C
else
  echo "  skip  diff_core (needs node and the Smarsh repo for the JavaScript side)"
fi

# Stack bounds, measured from the compiler's own assembly (stack_depth.c).
# The -d 64 figures rest on every recursive cycle adding one to a depth
# counter capped at 64; the grep checks the source for that: the only
# recursive calls that keep the same depth are the three known ones, each
# of which leads straight into a call that adds one.
echo
echo "stack bounds"
if build stack_depth $C stack_depth.c; then
  for f in smarsh_core smarsh_reason smarsh_learner smarsh_concept smarsh_analogy smarsh_informant smarsh_law smarsh_proof smarsh_abstract smarsh_time smarsh_interval smarsh_space smarsh_child smarsh_lexer smarsh_ast smarsh_parser \
           smarsh_value smarsh_interp boundary_kernel; do
    (cd $C && $CC -std=c99 -O2 -S $f.c -o "$OUT/$f.s") 2>/dev/null
  done
  same=$(cd $C && grep -hE '(parse_[a-z]+|eval[a-z_]*|render_at|equal_at)\((p|in|h), [^;]*depth\)' \
           smarsh_parser.c smarsh_interp.c smarsh_value.c | grep -vc 'depth + 1u')
  if [ "$same" -eq 3 ]; then ok "every recursive cycle raises the depth counter (3 known same-depth hops)"
  else bad "recursive calls that keep the same depth: $same, expected the 3 known ones"; fi
  # laws: step_of keeps the depth once, and hands it straight to eval_at + 1
  up=$(grep -c 'eval_at(book, [^;]*depth + 1u)' $C/smarsh_law.c)
  keep=$(grep -c 'step_of(book, [^;]*, depth)' $C/smarsh_law.c)
  if [ "$up" -eq 4 ] && [ "$keep" -eq 1 ]; then ok "every law evaluation one law deeper raises the depth counter"
  else bad "law recursion: $up raising calls (4 expected), $keep same-depth (1 expected)"; fi
  stack() {
    local label=$1; shift
    if r=$("$OUT/stack_depth.exe" "$@" 2>&1); then ok "$label: $r"; else bad "$label: $r"; fi
  }
  O=$OUT
  stack "kernel" 3072 $O/smarsh_core.s $O/smarsh_reason.s
  stack "kernel + learner" 6144 $O/smarsh_core.s $O/smarsh_reason.s $O/smarsh_learner.s
  stack "kernel + concepts" 8192 $O/smarsh_core.s $O/smarsh_reason.s $O/smarsh_concept.s
  stack "analogy + informants" 8192 $O/smarsh_analogy.s $O/smarsh_informant.s
  stack "laws" 16384 -d 16 $O/smarsh_law.s
  stack "proof" 65536 $O/smarsh_proof.s
  stack "abstraction" 8192 $O/smarsh_core.s $O/smarsh_reason.s $O/smarsh_abstract.s
  stack "time" 8192 $O/smarsh_time.s
  stack "the learner's loop" 16384 $O/smarsh_child.s $O/smarsh_abstract.s $O/smarsh_concept.s
  # the parser caps nesting at 64 and the printer at 256; 256 bounds both
  stack "described worlds" 1048576 -d 256 $O/smarsh_interval.s $O/smarsh_space.s
  stack "parser" 131072 -d 64 $O/smarsh_lexer.s $O/smarsh_ast.s $O/smarsh_parser.s
  stack "interpreter" 65536 -d 64 $O/smarsh_ast.s $O/smarsh_value.s $O/smarsh_interp.s
  stack "boundary kernel" 16384 $O/boundary_kernel.s
fi

echo
echo "native/"
N=native
build test_native $N pyrand.c native_reasoner.c test_native.c && run_test test_native $N
build test_nprand $N nprand.c test_nprand.c && run_test test_nprand $N
build test_dataset $N pyrand.c dataset.c test_dataset.c && run_golden test_dataset $N test_dataset.txt
build demo_reasoner $N pyrand.c native_reasoner.c demo_reasoner.c && run_golden demo_reasoner $N demo_reasoner.txt
build demo_node_reasoner $N pyrand.c native_reasoner.c demo_node_reasoner.c && run_golden demo_node_reasoner $N demo_node_reasoner.txt
build baseline $N pyrand.c nprand.c dataset.c baseline.c
if [ "${1:-}" = "--slow" ]; then
  run_golden baseline $N baseline.txt
else
  echo "  skip  baseline training (about a minute; run with --slow)"
fi

echo
echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
