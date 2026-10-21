/*
 * test_life.c -- a life of its own: acting from its own neurons, led by its
 * own curiosity, in the crib.
 *
 * Three twins live the same crib: the being, whose dopamine is its learning
 * progress; a twin whose dopamine is plain surprise; a twin with no dopamine
 * at all, whose basal ganglia never learn. Nobody chooses what any of them do.
 *
 * Checked:
 *   newborn       it is born expecting nothing
 *   its own       what it does comes from its own brainstem; with no dopamine,
 *                 at the brainstem's own restless rate; with curiosity, at
 *                 rates its basal ganglia change over its life
 *   the snow      the being looks at the television's snow no more than at
 *                 any other corner; the twin that seeks surprise is caught by
 *                 it (Schmidhuber's "noisy TV")
 *   me, not me    it expects its own voice and its own jerks, not its
 *                 mother's talk, her rocking or her touch: never told which
 *                 was which
 *   she answers   it comes to half expect her answer to its voice
 *   its world     it predicts how the wall moves as its eyes move, and the
 *                 mobile's swing
 *   repeatable    the same seed lives the same life
 */

#include <stdio.h>
#include <string.h>

#include "ob_crib.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

#define SEEDS 3u
#define LIFE 150000ul
#define EXAM 60000ul

static ol_life_t L;
static ob_crib_life_t W[3][SEEDS];
static double X[3][SEEDS][OC_EXAMS];

static double mean_exam(unsigned twin, unsigned k) {
  /* over the lives in which it happened at all */
  double s = 0.0;
  unsigned i, n = 0u;
  for (i = 0u; i < SEEDS; i++) {
    if (X[twin][i][k] < 0.0) continue;
    s += X[twin][i][k];
    n++;
  }
  return n ? s / n : -1.0;
}

int main(void) {
  static const char *const TWIN[3] = {"the being (learning progress)", "the twin that seeks surprise",
                                      "the twin with no dopamine"};
  unsigned twin, i, m, k;
  double tv[3] = {0}, corner[3] = {0};

  ol_born(&L, 1u, OL_PROGRESS);
  {
    int blank = 1;
    for (i = 0u; i < OL_SENSE; i++) blank = blank && L.expect[i] == 0.0f;
    for (i = 0u; i < OL_VIS + OL_EFF + 1u; i++) blank = blank && L.cb.see[0][0][i] == 0.0f;
    check("newborn: it expects nothing, and wants nothing in particular", blank && L.act[0][0] == 0.0f);
  }

  for (twin = 0u; twin < 3u; twin++) {
    for (i = 0u; i < SEEDS; i++) {
      ol_born(&L, i + 1u, twin);
      ob_crib_live(&L, i + 1u, LIFE, &W[twin][i]);
      ob_crib_examine(&L, i + 101u, EXAM, X[twin][i]);
      tv[twin] += W[twin][i].tv / SEEDS;
      corner[twin] += W[twin][i].corner / SEEDS;
    }
    printf("        %s: eyes on the snow %.3f, on each other corner %.3f\n", TWIN[twin], tv[twin], corner[twin]);
    printf("          still surprised by:");
    for (k = 0u; k < OC_EXAMS; k++) printf("%s %s %.2f", k ? ";" : "", OC_EXAM_NAME[k], mean_exam(twin, k));
    printf("\n");
  }

  {
    /* the twin with no dopamine acts at the brainstem's own rate; the being's
       rates move over its life */
    double lo = 1.0, hi = 0.0, spread = 0.0;
    unsigned tenth;
    for (m = 0u; m < OL_MOTORS; m++) {
      double r = 0.0, rmin = 1.0, rmax = 0.0;
      for (tenth = 0u; tenth < OC_TENTHS; tenth++) {
        double a = (double)W[2][0].acts[tenth][m] / (LIFE / OC_TENTHS), b = (double)W[0][0].acts[tenth][m] / (LIFE / OC_TENTHS);
        r += a / OC_TENTHS;
        if (b < rmin) rmin = b;
        if (b > rmax) rmax = b;
      }
      if (r < lo) lo = r;
      if (r > hi) hi = r;
      if (rmax / (rmin + 1e-3) > spread) spread = rmax / (rmin + 1e-3);
    }
    printf("        (no dopamine: each act %.3f to %.3f of moments; the being's busiest act varies %.1fx over its life)\n",
           lo, hi, spread);
    check("its own: with no dopamine, it acts at its brainstem's own restless rate (1% to 4% of moments, each act)",
          lo > 0.01 && hi < 0.04);
    check("its own: with curiosity, its basal ganglia change how often it does things over its life (2x or more)",
          spread > 2.0);
  }

  check("the snow: the being looks at it no more than at any other corner (under 1.6x)",
        tv[0] < 1.6 * corner[0]);
  check("the snow: the twin that seeks surprise is caught by it (over 2x any other corner, and more than the being)",
        tv[1] > 2.0 * corner[1] && tv[1] > 1.4 * tv[0]);

  check("me: it expects its own voice (under a quarter as surprising as its mother's talk)",
        mean_exam(0, OC_VOICE) < 0.25 * mean_exam(0, OC_TALK));
  check("me: it expects its own jerks (under a quarter as surprising as being rocked)",
        mean_exam(0, OC_JERK) < 0.25 * mean_exam(0, OC_ROCKED));
  check("not me: its mother's touch stays a surprise (over half)", mean_exam(0, OC_TOUCHED) > 0.5);
  check("she answers: it half expects her answer to its voice (under 0.6x her talking of herself)",
        mean_exam(0, OC_ANSWER) < 0.6 * mean_exam(0, OC_TALK));
  check("its world: it predicts how the wall moves as its eyes move (under 0.1 left)", mean_exam(0, OC_EYES) >= 0.0 && mean_exam(0, OC_EYES) < 0.1);
  check("its world: it predicts the mobile's swing (under 0.3 left)", mean_exam(0, OC_MOBILE) >= 0.0 && mean_exam(0, OC_MOBILE) < 0.3);

  {
    ob_crib_life_t again;
    ol_born(&L, 1u, OL_PROGRESS);
    ob_crib_live(&L, 1u, LIFE, &again);
    check("the same seed lives the same life", memcmp(&again, &W[0][0], sizeof again) == 0);
  }
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
