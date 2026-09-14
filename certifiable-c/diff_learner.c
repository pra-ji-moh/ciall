/*
 * diff_learner.c -- the C learner, printed in a canonical form so it can
 * be diffed line for line against the Python reference (diff_learner.py).
 *
 * Five configurations, 3,734 runs in all:
 *   A  a newborn, truthful sensors: 6 goals x 32 scenes
 *   B  every capacity held, one sensor lying by one step:
 *      6 goals x 32 scenes x 3 liars
 *   D  a poorer world where only the first k capacities exist, so some
 *      goals genuinely cannot be phrased: 5 x 6 goals x 32 scenes
 *   C  one learner growing across the six goals in sequence
 *   E  noise: 2,000 runs, random starting knowledge, every sensor an
 *      arbitrary value. Reaches STUCK (104 runs) and the joint-relevance
 *      branch (21 runs end differently without it).
 *
 * CONTRADICTED is not reached because it cannot be: see smarsh_learner.h.
 * Every run here lives in 32 situations, the single-word bitset path;
 * test_learner.c runs the learner on 256, and diff_kernel.c covers the
 * kernel up to 256 worlds.
 *
 * Every line carries the outcome, the value, the world count, what it
 * measured and acquired, whether the witness checked, and every event in
 * order. Agreement on all of that is agreement on the kernel functions the
 * learner calls, not only on the learner.
 */

#include <stdio.h>

#include "smarsh_learner.h"

#define N_SIT 32u
#define N_CAPS 6u
#define N_GOALS 6u

static unsigned a_of(unsigned s) { return (s >> 3) & 3u; }
static unsigned b_of(unsigned s) { return (s >> 1) & 3u; }
static unsigned name_of(unsigned s) { return s & 1u; }

static sl_capacity_t TABLE[N_CAPS];
static sr_query_t GOAL[N_GOALS];

static const unsigned DEMO_TRUTH[N_GOALS] = {
  (2u << 3) | (0u << 1) | 0u,
  (1u << 3) | (2u << 1) | 1u,
  (0u << 3) | (3u << 1) | 1u,
  (3u << 3) | (1u << 1) | 1u,
  (3u << 3) | (1u << 1) | 1u,
  (2u << 3) | (2u << 1) | 0u
};

static unsigned cap_answer(unsigned cap, unsigned s) {
  unsigned a = a_of(s);
  unsigned b = b_of(s);
  switch (cap) {
    case 0: return a < b ? 0u : (a == b ? 1u : 2u);            /* more */
    case 1: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;   /* close */
    case 2: return name_of(s);                                   /* name */
    case 3: return a + b;                                        /* sum */
    case 4: return a;                                            /* exact_a */
    default: return b;                                           /* exact_b */
  }
}

static unsigned goal_answer(unsigned g, unsigned s) {
  unsigned a = a_of(s);
  unsigned b = b_of(s);
  switch (g) {
    case 0: return a > b ? 1u : 0u;
    case 1: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;
    case 2: return name_of(s);
    case 3: return a >= 2u ? 1u : 0u;
    case 4: return (a + b) == 4u ? 1u : 0u;
    default: return ((a + b) % 2u == 0u) ? 1u : 0u;
  }
}

static int setup(void) {
  static const char *names[N_CAPS] = {"more", "close", "name", "sum",
                                      "exact_a", "exact_b"};
  static const unsigned doms[N_CAPS] = {3u, 2u, 2u, 7u, 4u, 4u};
  unsigned c, g, s, k;

  for (c = 0u; c < N_CAPS; c++) {
    for (k = 0u; k < SL_NAME_LEN; k++) {
      TABLE[c].name[k] = '\0';
    }
    for (k = 0u; names[c][k] != '\0' && k + 1u < SL_NAME_LEN; k++) {
      TABLE[c].name[k] = names[c][k];
    }
    TABLE[c].immediate = (c <= 2u) ? 1 : 0;
    if (sr_query_init(&TABLE[c].q, N_SIT, doms[c]) != SM_OK) return 0;
    for (s = 0u; s < N_SIT; s++) {
      if (sr_query_set(&TABLE[c].q, s, cap_answer(c, s)) != SM_OK) return 0;
    }
  }
  for (g = 0u; g < N_GOALS; g++) {
    if (sr_query_init(&GOAL[g], N_SIT, 2u) != SM_OK) return 0;
    for (s = 0u; s < N_SIT; s++) {
      if (sr_query_set(&GOAL[g], s, goal_answer(g, s)) != SM_OK) return 0;
    }
  }
  return 1;
}

static void print_run(char cfg, unsigned g, unsigned truth, int liar,
                      sm_status_t st, const sl_report_t *R) {
  unsigned i;

  printf("%c g%u t%u l%d st%d out%d val%u w%u m%u a%u p%d H%.6f |",
         cfg, g, truth, liar, (int)st, (int)R->outcome,
         R->outcome == SL_OUT_DERIVED ? R->value : 0u, R->worlds,
         R->measured, R->acquired, R->outcome == SL_OUT_DERIVED ? R->proved : 0,
         R->H);
  for (i = 0u; i < R->n_log; i++) {
    const sl_event_t *e = &R->log[i];
    if (e->kind == SL_EV_ACQUIRE) {
      printf(" acq:%u:%u:%u", e->cap, e->a, e->b);
    } else if (e->kind == SL_EV_MEASURE) {
      printf(" mea:%u:%u", e->cap, e->a);
    } else {
      printf(" ret:%u:%u", e->cap, e->a);
    }
  }
  printf("\n");
}

/* Learners are large (two query buffers), so they are static rather than
   on the stack -- the same reason the buffers live in the struct at all. */
static sl_learner_t LEARNER;
static sl_report_t REPORT;

int main(void) {
  unsigned g, t, c;
  sl_scene_t scene;
  sm_status_t st;

  if (!setup()) {
    printf("SETUP FAILED\n");
    return 1;
  }

  /* A: newborn, truthful */
  for (g = 0u; g < N_GOALS; g++) {
    for (t = 0u; t < N_SIT; t++) {
      sl_init(&LEARNER, TABLE, N_CAPS, N_SIT);
      sl_scene_truthful(TABLE, N_CAPS, t, &scene);
      st = sl_pursue(&LEARNER, &GOAL[g], &scene, &REPORT);
      print_run('A', g, t, -1, st, &REPORT);
    }
  }

  /* B: everything held, one liar */
  for (g = 0u; g < N_GOALS; g++) {
    for (t = 0u; t < N_SIT; t++) {
      for (c = 3u; c <= 5u; c++) {
        sl_init(&LEARNER, TABLE, N_CAPS, N_SIT);
        for (unsigned k = 0u; k < N_CAPS; k++) sl_grant(&LEARNER, k);
        sl_scene_truthful(TABLE, N_CAPS, t, &scene);
        scene.reading[c] = (scene.reading[c] + 1u) % TABLE[c].q.dom;
        st = sl_pursue(&LEARNER, &GOAL[g], &scene, &REPORT);
        print_run('B', g, t, (int)c, st, &REPORT);
      }
    }
  }

  /* D: a poorer world -- only the first k capacities exist, so some goals
     cannot be phrased at all and the other outcomes actually run */
  for (c = 1u; c <= 5u; c++) {
    for (g = 0u; g < N_GOALS; g++) {
      for (t = 0u; t < N_SIT; t++) {
        sl_init(&LEARNER, TABLE, c, N_SIT);
        sl_scene_truthful(TABLE, c, t, &scene);
        st = sl_pursue(&LEARNER, &GOAL[g], &scene, &REPORT);
        print_run('D', g, t, (int)c, st, &REPORT);
      }
    }
  }

  /* C: one learner, growing across the goals */
  sl_init(&LEARNER, TABLE, N_CAPS, N_SIT);
  for (g = 0u; g < N_GOALS; g++) {
    sl_scene_truthful(TABLE, N_CAPS, DEMO_TRUTH[g], &scene);
    st = sl_pursue(&LEARNER, &GOAL[g], &scene, &REPORT);
    print_run('C', g, DEMO_TRUTH[g], -1, st, &REPORT);
  }

  /* E: noise. Random starting knowledge, every sensor an arbitrary
     in-range value, a random goal. This is what reaches STUCK, and the
     joint-relevance branch (no single question helps, several together
     would). The same 32-bit LCG drives both sides. The "truth" field
     prints the run number. */
  {
    uint32_t lcg = 2026u;
    unsigned run;
    for (run = 0u; run < 2000u; run++) {
      sl_init(&LEARNER, TABLE, N_CAPS, N_SIT);
      for (c = 0u; c < N_CAPS; c++) {
        lcg = lcg * 1103515245u + 12345u;
        if ((lcg >> 16) & 1u) sl_grant(&LEARNER, c);
      }
      for (c = 0u; c < N_CAPS; c++) {
        lcg = lcg * 1103515245u + 12345u;
        scene.reading[c] = (lcg >> 16) % TABLE[c].q.dom;
      }
      lcg = lcg * 1103515245u + 12345u;
      g = (lcg >> 16) % N_GOALS;
      st = sl_pursue(&LEARNER, &GOAL[g], &scene, &REPORT);
      print_run('E', g, run, -1, st, &REPORT);
    }
  }
  return 0;
}
