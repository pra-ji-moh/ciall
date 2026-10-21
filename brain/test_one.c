/*
 * test_one.c -- one brain: the grown brain, living, in the crib.
 *
 * The same checks as test_life.c, on the grown brain's own neurons and
 * synapses instead of the sketch (ob_life.c), with one life per twin (a life
 * here takes minutes, not seconds). Its results are weaker than the sketch's;
 * the checks hold it to what it does now, and the numbers it prints say how
 * far it is from the sketch.
 *
 *   test_one            three twins, a life each (about five minutes)
 */

#include <stdio.h>
#include <string.h>

#include "ob_crib.h"
#include "ob_one.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

#define LIFE 40000ul
#define EXAM 20000ul

static ob_brain_t GROWN;
static ob_one_t ONE, FROZEN;

typedef struct {
  ob_crib_life_t life;
  double exam[OC_EXAMS];
} lived_t;

static unsigned live_one(void *self, const float *in) { return ob_one_live((ob_one_t *)self, in); }

static void live(ob_one_t *O, unsigned seed, lived_t *out) {
  ob_crib_being_t b;
  b.self = O;
  b.live = live_one;
  b.err = O->err;
  b.energy = O->energy;
  ob_crib_live_any(&b, seed, LIFE, &out->life);
}

/* afterwards, learning stopped: how much of what came still surprises it, by cause */
static void examine(const ob_one_t *O, unsigned seed, double x[OC_EXAMS]) {
  ob_crib_being_t b;
  memcpy(&FROZEN, O, sizeof FROZEN);
  FROZEN.learning = 0u;
  b.self = &FROZEN;
  b.live = live_one;
  b.err = FROZEN.err;
  b.energy = FROZEN.energy;
  ob_crib_examine_any(&b, seed, EXAM, x);
}

int main(void) {
  static const char *const TWIN[3] = {"the being (learning progress)", "the twin that seeks surprise",
                                      "the twin with no dopamine"};
  static lived_t L[3];
  ob_genome_t g;
  unsigned twin, k, m;

  printf("ONE BRAIN: THE GROWN BRAIN, LIVING\n\n");
  ob_genome_default(&g);
  ob_grow(&GROWN, &g);
  for (twin = 0u; twin < 3u; twin++) {
    ob_one_born(&ONE, &GROWN, 1u, twin);
    live(&ONE, 1u, &L[twin]);
    examine(&ONE, 101u, L[twin].exam);
    printf("        %s: eyes on the snow %.3f, on each other corner %.3f\n", TWIN[twin], L[twin].life.tv, L[twin].life.corner);
    printf("          still surprised by:");
    for (k = 0u; k < OC_EXAMS; k++) printf("%s %s %.2f", k ? ";" : "", OC_EXAM_NAME[k], L[twin].exam[k]);
    printf("\n");
    if (twin == 0u) {
      /* what it learned is in its brain's own synapses */
      unsigned long pred = 0u, action = 0u, s;
      ob_one_settle(&ONE);
      for (s = 0u; s < ONE.brain.n_syn; s++) {
        const char *nm = ob_rule_name(ONE.brain.syn[s].rule);
        if (ONE.brain.syn[s].weight == 0) continue;
        if (strncmp(nm, "prediction:", 11) == 0 || strncmp(nm, "timing:", 7) == 0) pred++;
        if (strncmp(nm, "action:", 7) == 0) action++;
      }
      printf("        (its synapses: %lu of the cerebellum's predictions and %lu of the basal ganglia's urges learned)\n",
             pred, action);
      check("readable: what it learned is in its brain's own synapses, which started at nothing", pred > 1000u &&
                                                                                                    action > 100u);
    }
  }
  {
    double lo = 1.0, hi = 0.0, spread = 0.0;
    unsigned tenth;
    for (m = 0u; m < OL_MOTORS; m++) {
      double r = 0.0, rmin = 1.0, rmax = 0.0;
      for (tenth = 0u; tenth < OC_TENTHS; tenth++) {
        double a = (double)L[2].life.acts[tenth][m] / (LIFE / OC_TENTHS), b = (double)L[0].life.acts[tenth][m] / (LIFE / OC_TENTHS);
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
    check("its own: with no dopamine, each zone of its brainstem acts at its own restless rate (0.5% to 3%)",
          lo > 0.005 && hi < 0.03);
    check("its own: with curiosity, its basal ganglia change how often it does things over its life (2x or more)",
          spread > 2.0);
  }
  check("the snow: the being looks at it no more than at any other corner (under 1.6x)", L[0].life.tv < 1.6 * L[0].life.corner);
  check("the snow: the twin that seeks surprise is caught by it (over 1.4x any other corner, and more than the being)",
        L[1].life.tv > 1.4 * L[1].life.corner && L[1].life.tv > L[0].life.tv);
  check("me: it expects its own voice more than its mother's talk (under 0.6x as surprising)",
        L[0].exam[OC_VOICE] < 0.6 * L[0].exam[OC_TALK]);
  check("me: it expects its own jerks more than being rocked", L[0].exam[OC_JERK] < L[0].exam[OC_ROCKED]);
  check("not me: its mother's touch stays a surprise (over half)", L[0].exam[OC_TOUCHED] > 0.5);
  check("she answers: it expects her answer more than her talking of herself (under 0.8x)",
        L[0].exam[OC_ANSWER] < 0.8 * L[0].exam[OC_TALK]);
  check("its world: it predicts how the wall moves as its eyes move (under 0.2 left)",
        L[0].exam[OC_EYES] >= 0.0 && L[0].exam[OC_EYES] < 0.2);
  {
    static ob_one_t again;
    float in[OL_INPUT];
    ob_crib_t a, b;
    unsigned long t;
    int same = 1;
    ob_one_born(&ONE, &GROWN, 3u, OL_PROGRESS);
    ob_one_born(&again, &GROWN, 3u, OL_PROGRESS);
    ob_crib_make(&a, 3u);
    ob_crib_make(&b, 3u);
    for (t = 0u; t < 3000u && same; t++) {
      unsigned x, y;
      ob_crib_sense(&a, in);
      x = ob_one_live(&ONE, in);
      ob_crib_sense(&b, in);
      y = ob_one_live(&again, in);
      same = x == y;
      ob_crib_act(&a, x);
      ob_crib_act(&b, y);
    }
    check("the same brain and seed live the same life", same);
  }
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
