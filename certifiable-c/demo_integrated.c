/*
 * demo_integrated.c -- nothing on top of anything.
 *
 * Every faculty the earlier modules had their own tables for is here one
 * thing: a description of worlds, and elimination in it.
 *
 *   a concept          a named quantity, defined by an expression
 *   learning a rule    eliminating the rules that contradict what it saw
 *   a symmetry, a law  a statement it can prove, in the same engine
 *   a guess            a constraint marked as one, so every answer can say
 *                      whether it stood on it
 *   being told         a constraint carrying its source, switched off
 *                      everywhere the moment that source is caught out
 *   time               the same quantities at t and t + 1, tied by
 *                      constraints, so planning is elimination too
 *
 * One medium, one operation, one kind of verdict.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_space.h"

static sx_theory_t T;
static sx_answer_t A;

static void say_answer(const sx_theory_t *th, const sx_answer_t *a, uint32_t rests_on, int proved) {
  unsigned i;
  switch (a->verdict) {
    case SX_TRUE_ALL: printf("true in every world"); break;
    case SX_FALSE_ALL: printf("false in every world"); break;
    case SX_BOTH: printf("both are possible"); break;
    case SX_NO_WORLDS: printf("NOTHING fits: what it holds cannot all be true"); break;
    case SX_UNRESOLVED: printf("unresolved at this precision"); break;
    case SX_VALUE: printf("= %.10g", a->range.lo); break;
    default:
      printf("in [%.6g, %.6g]", a->range.lo, a->range.hi);
      break;
  }
  if (a->verdict == SX_NO_WORLDS) { printf("\n"); return; }
  if (proved) printf("   (proved, standing on nothing it guessed)\n");
  else if (rests_on == 0u) printf("   (not proved either way)\n");
  else {
    printf("   (resting on what it guessed:");
    for (i = 0u; i < th->n_cons; i++) {
      if (rests_on & ((uint32_t)1 << i)) {
        printf(" a claim of %s;", th->source_name[th->source[i]]);
      }
    }
    printf(")\n");
  }
}

static void show_witness(const char *what, const sx_box_t *b) {
  unsigned i;
  printf("      %s", what);
  for (i = 0u; i < T.n_vars; i++) printf(" %s=%.0f", T.name[i], iv_mid(b->v[i]));
  printf("\n");
}

static void ask(const char *q) {
  unsigned root;
  uint32_t rests;
  int proved;
  sx_opts_t o = sx_default_opts();
  printf("  %-46s ", q);
  if (sx_parse(&T, q, &root) != SM_OK) { printf("[%s]\n", T.error); return; }
  if (sx_ask_grounded(&T, root, &o, &A, &rests, &proved) != SM_OK) { printf("[failed]\n"); return; }
  say_answer(&T, &A, rests, proved);
}

int main(void) {
  unsigned idx, book;

  printf("ONE MEDIUM: EVERYTHING IS ELIMINATION IN A DESCRIPTION\n");

  /* ---- a concept is a named quantity, not a table ---------------------- */
  printf("\nA CONCEPT IT NAMES FOR ITSELF\n");
  sx_init(&T);
  sx_var(&T, "s1", SX_BOOL, 0, 1, &idx);
  sx_var(&T, "s2", SX_BOOL, 0, 1, &idx);
  sx_var(&T, "s3", SX_BOOL, 0, 1, &idx);
  sx_var(&T, "k", SX_INT, 0, 4, &idx);
  sx_define(&T, "filled", "s1 + s2 + s3");
  printf("  it defines: filled = s1 + s2 + s3, and can now speak of it\n");

  /* ---- learning a rule IS elimination ---------------------------------- */
  printf("\nLEARNING A RULE: THE RULES THAT CONTRADICT WHAT IT SAW ARE ELIMINATED\n");
  printf("  a bell rings when enough slots are filled, and it does not know how many\n");
  /* what it saw, as facts about those very situations: two filled and the
     bell rang, one filled and it did not. Any threshold that disagrees
     with either is eliminated, and what is left is the rule. */
  sx_require(&T, "2 >= k");
  sx_require(&T, "not (1 >= k)");
  ask("k");
  printf("  the threshold was not fitted or guessed: every other value was ruled out\n");

  /* ---- a property of its own rule, proved ------------------------------ */
  printf("\nA PROPERTY OF THAT RULE, PROVED IN THE SAME ENGINE\n");
  ask("filled >= k => filled + 1 >= k");
  printf("  once the bell rings, adding a pebble never silences it: not tested, proved\n");

  /* ---- a guess, and what an answer stands on --------------------------- */
  printf("\nA GUESS, MARKED AS ONE. WHAT DOES AN ANSWER STAND ON?\n");
  sx_init(&T);
  sx_var(&T, "a", SX_INT, 0, 100, &idx);
  sx_var(&T, "b", SX_INT, 0, 100, &idx);
  sx_var(&T, "total", SX_INT, 0, 200, &idx);
  sx_source(&T, "its own law", &idx);
  sx_require(&T, "a == 30");
  sx_require(&T, "b == 25");
  ask("total > 50");
  printf("  it has poured nothing this big, so nothing follows yet. Now its law:\n");
  sx_claim(&T, "total == a + b", idx, 1);   /* a guess: its law, past what it saw */
  ask("total");
  ask("total > 50");

  /* ---- being told, and catching the teller out ------------------------- */
  printf("\nSOMETHING IT IS TOLD, AND WHAT HAPPENS WHEN THE TELLER IS CAUGHT\n");
  sx_source(&T, "a book", &book);
  sx_claim(&T, "total == a + b + 1", book, 0);
  printf("  the book says the pile is one bigger than the two trays together\n");
  ask("total");
  printf("  it pours them and counts 55, which the book cannot survive:\n");
  sx_require(&T, "total == 55");
  ask("total");
  sx_distrust(&T, book);
  printf("  so the book is caught out, and everything it said stops counting:\n");
  ask("total");
  ask("total > 50");

  /* ---- time, in the same description ------------------------------------ */
  printf("\nTIME: THE SAME QUANTITIES AT t, t+1, t+2, TIED BY CONSTRAINTS\n");
  sx_init(&T);
  sx_var(&T, "p0", SX_INT, 0, 4, &idx);
  sx_var(&T, "p1", SX_INT, 0, 4, &idx);
  sx_var(&T, "p2", SX_INT, 0, 4, &idx);
  sx_var(&T, "a1", SX_INT, 0, 1, &idx);
  sx_var(&T, "a2", SX_INT, 0, 1, &idx);
  sx_require(&T, "p1 == max(0, min(4, p0 + 2*a1 - 1))");
  sx_require(&T, "p2 == max(0, min(4, p1 + 2*a2 - 1))");
  printf("  a car on a track of five places, walls at both ends, pushed twice\n");
  sx_require(&T, "p0 == 2");
  ask("p2 == 4");
  if (A.has_true) show_witness("a world where it does, pushes and all:", &A.witness_true);
  printf("  that witness IS the plan: no separate planner, just a world that fits\n");
  ask("p2 >= 1");
  ask("p2 <= 4");
  ask("abs(p2 - p0) <= 2");
  printf("  whatever it does, it cannot move more than two places in two pushes:\n");
  printf("  proved, not searched for and not tested\n");

  printf("\nWHY THIS IS NOT LAYERS\n");
  printf("  The concept, the rule it learned, the proof about that rule, the guess,\n");
  printf("  the book it was told, the plan and the safety property are all the same\n");
  printf("  kind of thing: constraints on a described world. One operation runs on\n");
  printf("  all of them: eliminate what cannot be. Take any piece out and the rest\n");
  printf("  still works, because there is no stack to fall over.\n");
  return 0;
}
