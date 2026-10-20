/*
 * test_womb.c -- stage 1: in the womb, touch first, "me" and "not me".
 *
 * Checked:
 *   learning      over its days in the womb, the share of its own touch it
 *                 still feels falls, clearly
 *   not me        the mother's touch, which no twitch caused, is still felt in
 *                 full: nothing it can predict, so nothing cancelled
 *   the boundary  at the end, its own touch is felt clearly less than the
 *                 mother's, though nothing ever told it which was which
 *   the cortex    the parietal cortex responds less to its own touch than to
 *                 the mother's
 *   readable      what it learned is in the brain's own connections: the
 *                 prediction connections, which started at nothing, now cancel
 *   smell         smells that came with sweet come to mean approach, with
 *                 bitter avoid; smells with no taste of note stay as they were
 *   repeatable    the same genome lives the same womb
 */

#include <stdio.h>
#include <string.h>

#include "ob.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static ob_brain_t A, B;

int main(void) {
  ob_genome_t g;
  ob_womb_stats_t s1, s2;
  unsigned k, learned = 0u, last;
  printf("IN THE WOMB: TOUCH, AND WHAT IS ME\n\n");
  ob_genome_default(&g);
  ob_grow(&A, &g);
  ob_womb_touch(&A, 12u, 300u, &s1);
  last = s1.days - 1u;
  printf("        (own touch felt: day 0 %.3f, last %.3f; mother's: %.3f, %.3f)\n", s1.self_left[0],
         s1.self_left[last], s1.other_left[0], s1.other_left[last]);
  check("over its days in the womb, the share of its own touch it feels falls clearly",
        s1.self_left[last] < s1.self_left[0] - 0.08 && s1.self_left[last] < 0.8);
  check("the mother's touch, which nothing it did caused, is still felt in full", s1.other_left[last] > 0.95);
  check("at the end its own touch is felt clearly less than the mother's, never having been told which is which",
        s1.self_left[last] + 0.2 < s1.other_left[last]);
  check("the parietal cortex responds less to its own touch than to the mother's",
        s1.parietal_self[last] < s1.parietal_other[last]);
  for (k = 0u; k < A.n_syn; k++) {
    if (strncmp(ob_rule_name(A.syn[k].rule), "prediction:", 11) == 0 && A.syn[k].weight < -100) learned++;
  }
  check("what it learned is written in its own connections: prediction connections now cancel", learned > 200u);
  {
    ob_smell_stats_t sm;
    unsigned untouched = 1u;
    ob_womb_smell(&A, 12u, 50u, &sm);
    for (k = 0u; k < OB_SMELLS; k++) {
      if (k != 0u && k != 1u && k != 5u && sm.after[k] != sm.before[k]) untouched = 0u;
    }
    check("smell: the mother's smell and the anise of her meals come to mean approach",
          sm.after[0] > 0.5 && sm.after[1] > 0.5 && sm.after[0] > sm.before[0] + 0.5);
    check("smell: the smell of her stress comes to mean avoid", sm.after[5] < -0.5);
    check("smell: smells that came with no taste of note are left as they were", untouched);
  }
  ob_grow(&B, &g);
  ob_womb_touch(&B, 12u, 300u, &s2);
  check("the same genome lives the same womb",
        memcmp(&s1, &s2, sizeof s1) == 0);
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
