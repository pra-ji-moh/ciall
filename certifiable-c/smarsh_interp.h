/*
 * smarsh_interp.h -- the tree-walking evaluator in C.
 *
 * COMPILED AND RUN: test_frontend.c checks it against the JS engine (76
 * of 76 cases identical). Those cases found a crash no earlier case
 * reached, because every earlier list was flat: a nested list literal
 * such as [1, [2, 3]] was allocated over its own slots and contained
 * itself, and printing it recursed until the stack ran out. Fixed by
 * reserving a list's slots before evaluating its elements.
 *
 * STACK. Three recursions, each capped at 64 levels: evaluation
 * (SI_MAX_DEPTH), and comparing or printing nested lists
 * (SV_MAX_EQ_DEPTH). Past a cap is SI_ERR_DEPTH_EXCEEDED, a stated
 * divergence from the JS engine, which has no such cap. A list too long
 * to print is SI_ERR_OUTPUT_FULL rather than silently cut short, which it
 * used to be. ../build_c.sh measures the resulting worst-case stack.
 *
 * ============================================================
 * WHICH ENGINE THIS IS
 * ============================================================
 * Smarsh has two: a tree-walker that IS the specification, and a closure
 * compiler that is 4x faster and must be indistinguishable from it. This
 * ports the tree-walker, deliberately. The closure compiler's design --
 * compiling an AST into JavaScript closures -- has no C equivalent worth
 * transliterating; a C fast path would be a bytecode VM or direct
 * threading, which is a different program, not a port. Porting the
 * specification first also means there is something for a future C fast
 * path to be checked against, which is how the JS pair stays honest.
 *
 * ============================================================
 * STEP BUDGET
 * ============================================================
 * Every evaluation decrements a step counter. Exhausting it is
 * SI_ERR_STEPS_EXHAUSTED, which a program cannot catch, because a
 * program that could catch its own budget stop could ignore it. This
 * mirrors the JS engine, where the same property is tested adversarially
 * ("budget stops that an inner attempt cannot swallow").
 *
 * ============================================================
 * COVERAGE
 * ============================================================
 * Evaluates what the C parser builds: literals, identifiers, unary and
 * binary operators, and/or with short-circuit, calls to the small
 * builtin set below, indexing, list literals, let/var, assignment,
 * if/else, while, blocks, return, break, continue.
 *
 * Builtins: print, str, len. Deliberately few -- each one added is a
 * behaviour that must match the JS engine exactly, and an unverified
 * builtin is worse than a missing one.
 *
 * Not evaluated: functions, records, agents, and every capability,
 * label, taint and audit construct. Those return SI_ERR_UNSUPPORTED.
 * The audit chain in particular is NOT here, which means this engine
 * produces output but not evidence -- it is not yet the thing Smarsh
 * exists to be, and saying otherwise would be the exact overclaim this
 * project refuses.
 */

#ifndef SMARSH_INTERP_H
#define SMARSH_INTERP_H

#include "smarsh_ast.h"
#include "smarsh_value.h"

#define SI_MAX_OUTPUT 8192u
#define SI_DEFAULT_STEPS 1000000u

typedef enum {
  SI_OK = 0,
  SI_ERR_STEPS_EXHAUSTED = 1,
  SI_ERR_TYPE = 2,
  SI_ERR_UNKNOWN_NAME = 3,
  SI_ERR_IMMUTABLE = 4,
  SI_ERR_UNSUPPORTED = 5,
  SI_ERR_OUTPUT_FULL = 6,
  SI_ERR_DEPTH_EXCEEDED = 7,
  SI_ERR_DIVIDE_BY_ZERO = 8,
  SI_ERR_INDEX_OUT_OF_RANGE = 9,
  SI_ERR_INTERNAL = 10
} si_status_t;

typedef struct {
  si_status_t status;
  unsigned line;
  /* Everything the program printed, newline separated, so a caller can
     diff it against another engine's output without a file. */
  char output[SI_MAX_OUTPUT];
  unsigned output_used;
  unsigned steps_used;
} si_result_t;

/*
 * Run a parsed program. `arena` must have a valid root (sp_parse
 * succeeded); a partially parsed arena is never executed, because its
 * root stays SP_NO_NODE.
 */
si_status_t si_run(sp_arena_t *arena, sv_heap_t *heap, unsigned step_limit,
                   si_result_t *out);

#endif /* SMARSH_INTERP_H */
