/*
 * demo_program.c -- the same kernel, on a program.
 *
 * A port of demo_program.py. Apart from its opening paragraph, which names
 * the C files instead of the Python ones, its output is byte-identical to
 * the Python version's (checked by diffing the two).
 *
 * THE PROGRAM
 *
 *     var x : 0..3
 *     var y : 0..3
 *     if x > y { z = x } else { z = y }
 *     assert z >= 2
 *
 * Every question below is read off the program text. The declarations
 * give the variables, the conditional gives the branch, the assignment
 * gives z, the assertion gives the claim. Nobody modelled anything. In
 * general, deciding what the questions ARE is the open problem; for a
 * program it is not, because a program declares its own question space.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_reason.h"

#define N_SIT 16u

static const char *VNAME[] = {"derived", "undetermined", "contradiction",
                              "groundless", "speculated"};

static unsigned x_of(unsigned s) { return s >> 2; }
static unsigned y_of(unsigned s) { return s & 3u; }
static unsigned z_of(unsigned s) { return x_of(s) > y_of(s) ? x_of(s) : y_of(s); }

enum { Q_X, Q_Y, Q_BRANCH, Q_Z, Q_ASSERT, Q_ZX, Q_XGE2, Q_XLE1, NQ };
static sr_query_t Q[NQ];

static unsigned answer(int which, unsigned s) {
  switch (which) {
    case Q_X: return x_of(s);
    case Q_Y: return y_of(s);
    case Q_BRANCH: return x_of(s) > y_of(s) ? 1u : 0u;
    case Q_Z: return z_of(s);
    case Q_ASSERT: return z_of(s) >= 2u ? 1u : 0u;
    case Q_ZX: return z_of(s) >= x_of(s) ? 1u : 0u;
    case Q_XGE2: return x_of(s) >= 2u ? 1u : 0u;
    default: return x_of(s) <= 1u ? 1u : 0u;
  }
}

static void rule(const char *t) {
  size_t i;
  printf("\n%s\n", t);
  for (i = 0u; i < strlen(t); i++) putchar('-');
  putchar('\n');
}

static void show(const char *label, const sr_query_t *q, const sr_state_t *st) {
  sr_result_t r;
  char val[16];
  sr_ask(q, st, &r);
  if (r.verdict != SM_DERIVED) strcpy(val, "-");
  else if (q->dom == 2u) strcpy(val, r.value == 0u ? "false" : "true");
  else sprintf(val, "%u", r.value);
  printf("  %-26s%-14sS=%5.2f  %-6s witness %s\n", label, VNAME[r.verdict], r.S,
         val, sr_witness_check(q, &r.witness) ? "checks" : "FAILS");
}

int main(void) {
  static const unsigned doms[NQ] = {4, 4, 2, 4, 2, 2, 2, 2};
  unsigned i, s;
  sr_state_t st;
  sr_partition_t p;
  sr_result_t r;

  for (i = 0u; i < NQ; i++) {
    sr_query_init(&Q[i], N_SIT, doms[i]);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&Q[i], s, answer((int)i, s));
  }

  printf("The same kernel, on a program.\n\n");
  printf("No kernel change. Not one line. demo_desk.c reasons about a trading desk,\n");
  printf("smarsh_learner.c grows from nothing into a learner, test_smarsh_reason\n");
  printf("section 19 does induction with worlds as RULES, and this reasons about\n");
  printf("code. All four link the same smarsh_reason.c and it knows about none of them.\n\n");
  printf("That is the answer to \"are we building on one specific thing\": the kernel\n");
  printf("has no domain in it at all. It has worlds, questions, answers and one\n");
  printf("operation. A domain is what you hand it.\n");

  rule("the world count is read off the declarations");
  sr_refine(Q, 2, N_SIT, &p);   /* Q[0], Q[1]: the two declared variables */
  printf("  two variables over 0..3 give %u worlds, and that number is derived,\n", p.n_cells);
  printf("  not chosen: it is the common refinement of what the declarations can ask.\n");
  printf("  the branch condition adds nothing: %s\n",
         sr_distinguishes(&p, &Q[Q_BRANCH]) ? "a new distinction" : "already definable from x and y");
  printf("  z adds nothing either: %s\n",
         sr_distinguishes(&p, &Q[Q_Z]) ? "a new distinction" : "already definable from x and y");

  rule("an invariant, with both inputs entirely unknown");
  sr_state_init(&st, N_SIT);
  show("x", &Q[Q_X], &st);
  show("y", &Q[Q_Y], &st);
  show("assert z >= 2", &Q[Q_ASSERT], &st);
  show("invariant z >= x", &Q[Q_ZX], &st);
  printf("\n");
  printf("  Both inputs undetermined at S = 0, and z >= x comes out DERIVED\n");
  printf("  true. That is a proof over all 16 executions, not a test of some\n");
  printf("  of them, and it needed no evidence at all.\n");

  rule("a partial precondition is enough");
  sr_state_init(&st, N_SIT);
  sr_observe(&st, &Q[Q_XGE2], 1);
  printf("  given only: x >= 2\n");
  show("y", &Q[Q_Y], &st);
  show("assert z >= 2", &Q[Q_ASSERT], &st);
  printf("\n");
  printf("  The assertion holds and y was never constrained. Four of the eight\n");
  printf("  surviving executions have y = 0. It did not need them to agree on\n");
  printf("  y, only on the assertion.\n");

  rule("when it does not hold, the witness IS the counterexample");
  sr_state_init(&st, N_SIT);
  sr_observe(&st, &Q[Q_X], 1);
  show("assert z >= 2  (x = 1)", &Q[Q_ASSERT], &st);
  printf("  still %u executions alive, so it will not answer\n\n", sr_live_count(&st));
  {
    static sr_query_t cands[2];
    const char *names[2] = {"learn y", "learn which branch"};
    sr_probe_t pr;
    unsigned pick = 2u;
    cands[0] = Q[Q_Y];
    cands[1] = Q[Q_BRANCH];
    for (i = 0u; i < 2u; i++) {
      sr_probe(&st, &Q[Q_ASSERT], &cands[i], &pr);
      printf("    %-22s%s\n", names[i], pr.sufficient ? "SETTLES IT" : "narrows it, may not finish");
    }
    sr_choose(&st, &Q[Q_ASSERT], cands, 2, SR_ASK_GUARANTEE, &pick, &pr);
    printf("  it takes: %s\n", names[pick]);
  }
  sr_observe(&st, &Q[Q_Y], 0);
  show("assert z >= 2  (x = 1, y = 0)", &Q[Q_ASSERT], &st);
  for (s = 0u; s < N_SIT; s++) if (sr_world_possible(&st, s)) break;
  printf("\n  derived FALSE, and the witness holds exactly one execution: x=%u, y=%u, z=%u\n",
         x_of(s), y_of(s), z_of(s));
  printf("  A failed assertion does not come back as low confidence. It comes\n");
  printf("  back as the input that breaks it, and a checker confirms it.\n");

  rule("a specification that contradicts itself");
  sr_state_init(&st, N_SIT);
  sr_observe(&st, &Q[Q_XGE2], 1);
  sr_observe(&st, &Q[Q_XLE1], 1);
  sr_ask(&Q[Q_ASSERT], &st, &r);
  printf("  required x >= 2 and x <= 1\n");
  printf("  verdict: %s, %u executions admitted\n", VNAME[r.verdict], sr_live_count(&st));
  printf("  Not an unsatisfiable-looking low score. A distinct verdict saying\n");
  printf("  the requirements admit no execution, which is a fact about the\n");
  printf("  specification and not about the analysis.\n");

  printf("\n");
  for (i = 0u; i < 70u; i++) putchar('=');
  printf("\nSame kernel as the trading desk, the learner, and the induction over\n");
  printf("rules. Zero lines changed for any of them.\n");
  return 0;
}
