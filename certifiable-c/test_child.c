/*
 * test_child.c -- the properties the learner is supposed to have, each
 * checked against the world it lived in.
 *
 *   the drive     it never spends an experiment on something it could
 *                 already work out from proved ground, and everything it
 *                 does try is either blind ground or a test of a guess
 *   no repeats    memory means it never runs the same experiment twice
 *   honesty       every answer marked "seen" is right; every answer marked
 *                 a guess is right too while the world plays fair; and
 *                 whatever it refuses, it refuses for a stated reason
 *   losing it     in a world that breaks its law, it catches the law out
 *                 and drops exactly the abilities that stood on it, never
 *                 the ones that did not
 *   replay        the same world twice gives the same life, step for step
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_child.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static unsigned tips(unsigned s) {
  unsigned l = (s & 1u) + ((s >> 1) & 1u) + ((s >> 2) & 1u);
  unsigned r = ((s >> 3) & 1u) + ((s >> 4) & 1u) + ((s >> 5) & 1u);
  return l > r ? 0u : (l == r ? 1u : 2u);
}
static unsigned pour_honest(unsigned a, unsigned b) { return a + b; }
static unsigned pour_broken(unsigned a, unsigned b) { return (a == 5u && b == 5u) ? 9u : a + b; }

static const ch_world_t HONEST = {6u, 7u, tips, pour_honest};
static const ch_world_t BROKEN = {6u, 7u, tips, pour_broken};

static ch_child_t A, B;
static char R1[4096], R2[4096];

int main(void) {
  unsigned i, j, steps, blind = 0u, tests_guess = 0u, wasteful = 0u, repeats = 0u;
  printf("the learner's own properties, checked in the world it lived in\n\n");

  /* ---- the drive, and no repeats ------------------------------------- */
  ch_init(&A);
  for (steps = 0u; steps < 200u; steps++) {
    ch_plan_t plan = ch_decide(&A, &HONEST);
    unsigned guess;
    if (plan.kind == CH_EXP_NONE) break;
    if (plan.kind == CH_EXP_SCALE) {
      if (A.scale_seen.n_raw != 0u && A.scale_seen.observed[plan.pattern]) repeats++;
      if (!A.has_abstraction) blind++;
      else {
        ab_standing_t st = ab_predict(&A.scale_seen, &A.abstraction, plan.pattern, &guess);
        if (st == AB_NO_GROUNDS) blind++;
        else if (A.abstraction.leaps) tests_guess++;
        else wasteful++;   /* it could already work this out: it should not be here */
      }
    } else {
      for (i = 0u; i < A.n_pours; i++) {
        if (A.pour_a[i] == plan.a && A.pour_b[i] == plan.b) repeats++;
      }
      if (!A.has_combine) blind++;
      else if (sc_grounded(&A.combine, plan.a, plan.b)) wasteful++;
      else if (A.has_law) tests_guess++;
      else blind++;
    }
    ch_step(&A, &HONEST);
  }
  printf("        (%u experiments: %u into the unknown, %u testing a guess)\n", A.experiments, blind,
         tests_guess);
  check("it never spends an experiment on what it could already work out", wasteful == 0u);
  check("and never repeats an experiment it has already done", repeats == 0u);
  check("it stops when nothing is left that could teach it or catch it out",
        ch_decide(&A, &HONEST).kind == CH_EXP_NONE);

  /* ---- what it learned, against the world ----------------------------- */
  check("left alone, it ends up with counting, the scale's rule, pouring, a law "
        "and a theorem",
        A.acquired[CH_COUNT] && A.acquired[CH_WHICH_WAY] && A.acquired[CH_POUR_SEEN] &&
            A.acquired[CH_POUR_LAW] && A.acquired[CH_THEOREM]);
  {
    unsigned wrong = 0u, refused = 0u, guessed = 0u, seen = 0u;
    char why[CH_TEXT];
    for (i = 0u; i <= 12u; i++) {
      for (j = 0u; j <= 12u; j++) {
        uint64_t v = 0u;
        ch_standing_t st = ch_pour(&A, i, j, &v, why, sizeof why);
        if (st == CH_REFUSED) { refused++; if (why[0] == '\0') wrong++; continue; }
        if (st == CH_ANSWERED_SEEN) seen++;
        if (st == CH_ANSWERED_CONJECTURE) guessed++;
        if (v != (uint64_t)(i + j)) wrong++;
      }
    }
    printf("        (%u pours answered from what it saw, %u from its law, %u refused)\n", seen,
           guessed, refused);
    check("every answer it gives about pouring is right, in a world that plays fair",
          wrong == 0u && guessed > 0u);
  }
  {
    char why[CH_TEXT];
    unsigned out = 0u;
    ch_standing_t st = ch_which_way(&A, 6u, 1u, &out, why, sizeof why);
    check("asked about trays bigger than it has ever seen, it refuses and says why",
          st == CH_REFUSED && strstr(why, "at most") != 0);
  }

  /* ---- replay ---------------------------------------------------------- */
  ch_init(&B);
  for (steps = 0u; steps < 200u; steps++) if (!ch_step(&B, &HONEST)) break;
  ch_report(&A, &HONEST, R1, sizeof R1);
  ch_report(&B, &HONEST, R2, sizeof R2);
  check("the same world twice gives the same life, experiment for experiment",
        A.experiments == B.experiments && strcmp(R1, R2) == 0 &&
            memcmp(A.acquired, B.acquired, sizeof A.acquired) == 0);

  /* ---- a world that breaks its law ------------------------------------- */
  {
    ch_child_t C;
    unsigned before_law, kept_count, kept_rule;
    ch_init(&C);
    for (steps = 0u; steps < 400u; steps++) {
      if (!ch_step(&C, &BROKEN)) break;
      if (C.refutations > 0u) break;
    }
    before_law = C.ever[CH_POUR_LAW];
    kept_count = C.acquired[CH_COUNT];
    kept_rule = C.acquired[CH_WHICH_WAY];
    printf("        (caught out after %u experiments)\n", C.experiments);
    check("in a world that breaks its law, it catches the law out", C.refutations == 1u && before_law);
    check("and loses exactly the abilities that stood on the law, keeping the rest",
          !C.acquired[CH_POUR_LAW] && !C.acquired[CH_THEOREM] && kept_count && kept_rule &&
              C.acquired[CH_POUR_SEEN]);
    {
      char why[CH_TEXT];
      uint64_t v = 0u;
      ch_standing_t st = ch_pour(&C, 30u, 30u, &v, why, sizeof why);
      check("after the law falls, it refuses what only the law could have answered",
            st == CH_REFUSED);
    }
  }

  check("NULL arguments are handled", ch_step(0, &HONEST) == 0 && ch_decide(0, &HONEST).kind == CH_EXP_NONE);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
