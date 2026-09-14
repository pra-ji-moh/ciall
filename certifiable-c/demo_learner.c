/*
 * demo_learner.c -- the autonomous learner, running on smarsh_learner.c.
 *
 * The C counterpart of baby_auto.py's demonstration. Apart from its
 * opening paragraph, which names the C files, its output is
 * byte-identical to the Python version's (checked by diffing the two).
 *
 * No curriculum. It is given goals and a world, and works out the rest:
 * acquire a capacity when a goal cannot be phrased, take the one
 * measurement that settles it, answer with a witness, and when the
 * evidence contradicts, drop a claim and distrust its source.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_learner.h"

#define N_SIT 32u
#define N_CAPS 6u
#define N_GOALS 6u

static const char *VNAME[] = {"derived", "undetermined", "contradiction",
                              "groundless", "speculated"};
static const char *LABEL[N_GOALS] = {"is a bigger than b", "are they within one",
                                     "is it called a cup", "is a at least two",
                                     "is the total four", "is the total even"};
static const unsigned TRUTH[N_GOALS] = {
  (2u << 3) | (0u << 1) | 0u, (1u << 3) | (2u << 1) | 1u,
  (0u << 3) | (3u << 1) | 1u, (3u << 3) | (1u << 1) | 1u,
  (3u << 3) | (1u << 1) | 1u, (2u << 3) | (2u << 1) | 0u
};

static sl_capacity_t TABLE[N_CAPS];
static sr_query_t GOAL[N_GOALS];
static sl_learner_t KID;
static sl_report_t R;

static unsigned a_of(unsigned s) { return (s >> 3) & 3u; }
static unsigned b_of(unsigned s) { return (s >> 1) & 3u; }

static unsigned cap_answer(unsigned c, unsigned s) {
  unsigned a = a_of(s), b = b_of(s);
  switch (c) {
    case 0: return a < b ? 0u : (a == b ? 1u : 2u);
    case 1: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;
    case 2: return s & 1u;
    case 3: return a + b;
    case 4: return a;
    default: return b;
  }
}

static unsigned goal_answer(unsigned g, unsigned s) {
  unsigned a = a_of(s), b = b_of(s);
  switch (g) {
    case 0: return a > b ? 1u : 0u;
    case 1: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;
    case 2: return s & 1u;
    case 3: return a >= 2u ? 1u : 0u;
    case 4: return a + b == 4u ? 1u : 0u;
    default: return (a + b) % 2u == 0u ? 1u : 0u;
  }
}

static const char *outcome_text(sl_outcome_t o) {
  switch (o) {
    case SL_OUT_DERIVED: return "derived";
    case SL_OUT_CANNOT_PHRASE: return "cannot form the question and nothing left to learn";
    case SL_OUT_STUCK: return "undetermined, and nothing available would settle it";
    default: return "contradicted beyond repair";
  }
}

static void bar(char c, unsigned n) { unsigned i; for (i = 0u; i < n; i++) putchar(c); putchar('\n'); }

int main(void) {
  static const char *names[N_CAPS] = {"more", "close", "name", "sum", "exact_a", "exact_b"};
  static const unsigned doms[N_CAPS] = {3u, 2u, 2u, 7u, 4u, 4u};
  unsigned c, g, s, i;
  sl_scene_t scene;

  for (c = 0u; c < N_CAPS; c++) {
    memset(TABLE[c].name, 0, SL_NAME_LEN);
    strncpy(TABLE[c].name, names[c], SL_NAME_LEN - 1u);
    TABLE[c].immediate = c <= 2u;
    sr_query_init(&TABLE[c].q, N_SIT, doms[c]);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&TABLE[c].q, s, cap_answer(c, s));
  }
  for (g = 0u; g < N_GOALS; g++) {
    sr_query_init(&GOAL[g], N_SIT, 2u);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&GOAL[g], s, goal_answer(g, s));
  }

  printf("An autonomous learner, running on smarsh_learner.c.\n\n");
  printf("No curriculum. Nothing tells it what to acquire or when. It is given goals\n");
  printf("and an environment, and it works out the rest:\n\n");
  printf("    cannot even PHRASE the goal    -> acquire the smallest capacity that\n");
  printf("                                      would let it, and try again\n");
  printf("    can phrase it, cannot settle   -> work out which single measurement\n");
  printf("                                      would settle it, and take that one\n");
  printf("    settled                        -> answer, with a witness\n");
  printf("    the evidence contradicts       -> find what it was told that cannot be\n");
  printf("                                      true, drop it, carry on\n\n");
  printf("Every one of those decisions is counted, not scored. The only preference\n");
  printf("in the whole loop is which capacity to acquire when several would do, and\n");
  printf("it is named as a preference rather than buried.\n\n");

  bar('=', 74);
  printf("%-24s%8s%10s%10s%12s%8s\n", "goal", "worlds", "acquired", "measured",
         "outcome", "proved");
  bar('=', 74);
  sl_init(&KID, TABLE, N_CAPS, N_SIT);
  for (g = 0u; g < N_GOALS; g++) {
    char out[96];
    sl_scene_truthful(TABLE, N_CAPS, TRUTH[g], &scene);
    sl_pursue(&KID, &GOAL[g], &scene, &R);
    if (R.outcome == SL_OUT_DERIVED) sprintf(out, "%s = %u", outcome_text(R.outcome), R.value);
    else sprintf(out, "%s", outcome_text(R.outcome));
    printf("%-24s%8u%10u%10u%12s%8s\n", LABEL[g], R.worlds, R.acquired, R.measured,
           out, R.proved && R.outcome == SL_OUT_DERIVED ? "yes" : "-");
    for (i = 0u; i < R.n_log; i++) {
      const sl_event_t *e = &R.log[i];
      if (e->kind == SL_EV_ACQUIRE) {
        printf("      acquire %-8s worlds %u -> %u\n", TABLE[e->cap].name, e->a, e->b);
      } else if (e->kind == SL_EV_MEASURE) {
        printf("      measure %s\n", TABLE[e->cap].name);
      } else {
        printf("      RETRACT %s=%u and stop asking it (it cannot be true alongside the rest)\n",
               TABLE[e->cap].name, e->a);
      }
    }
  }
  printf("\n  it now holds: ");
  for (i = 0u; i < KID.n_held; i++) printf("%s%s", i ? ", " : "", TABLE[KID.held[i]].name);
  printf("\n  total acquisitions %u, total measurements %u\n\n", KID.acquisitions,
         KID.measurements);
  printf("  Nobody told it the order. Each capacity was acquired at the moment\n");
  printf("  a goal could not be phrased without it, and never before.\n");

  printf("\nTHE SAME EVIDENCE, A FINER VOCABULARY\n");
  bar('-', 74);
  {
    sr_partition_t p;
    sr_state_t st;
    sr_query_t gq;
    sr_result_t r;
    unsigned truth = (3u << 3) | (1u << 1) | 1u;
    sl_init(&KID, TABLE, N_CAPS, N_SIT);
    sl_grant(&KID, 0); sl_grant(&KID, 1); sl_grant(&KID, 2);
    sl_scene_truthful(TABLE, N_CAPS, truth, &scene);
    sl_glance(&KID, &scene);
    sl_partition(&KID, SL_NONE, &p);
    printf("  glance recorded: [");
    for (i = 0u; i < KID.n_memory; i++) {
      printf("%s('%s', %u)", i ? ", " : "", TABLE[KID.memory[i].cap].name,
             KID.memory[i].answer);
    }
    printf("]\n");
    printf("  with %u capacities (%u worlds): %s\n", KID.n_held, p.n_cells,
           sr_project(&p, &GOAL[3], &gq) == SM_OK ? "can phrase it" : "cannot even phrase it");
    sl_grant(&KID, 4);
    sl_partition(&KID, SL_NONE, &p);
    sl_state(&KID, &p, &st);
    sr_project(&p, &GOAL[3], &gq);
    sr_ask(&gq, &st, &r);
    printf("  after acquiring exact_a (%u worlds), with NO new measurement: %s = %u\n",
           p.n_cells, r.verdict == SM_DERIVED ? "derived" : "still cannot say", r.value);
  }
  printf("\n  It never counted the pile. It learned to ask about counts, replayed\n");
  printf("  the glance it already had, and the answer was sitting in it. The\n");
  printf("  vocabulary was the missing part, not the evidence.\n");

  printf("\nBEING MISLED, AND GETTING OUT OF IT\n");
  bar('-', 74);
  {
    sr_partition_t p;
    sr_state_t st;
    sr_query_t gq;
    sr_result_t r;
    sl_obs_t dropped;
    unsigned truth = (3u << 3) | (1u << 1) | 1u;
    sl_init(&KID, TABLE, N_CAPS, N_SIT);
    for (c = 0u; c < 6u; c++) if (c != 3u) sl_grant(&KID, c);
    sl_scene_truthful(TABLE, N_CAPS, truth, &scene);
    sl_glance(&KID, &scene);
    KID.memory[KID.n_memory].cap = 4u; KID.memory[KID.n_memory].answer = 3u; KID.n_memory++;
    KID.memory[KID.n_memory].cap = 5u; KID.memory[KID.n_memory].answer = 1u; KID.n_memory++;
    KID.memory[KID.n_memory].cap = 0u; KID.memory[KID.n_memory].answer = 0u; KID.n_memory++;
    printf("  told, after counting 3 and 1, that the first pile is the smaller one\n");
    sl_partition(&KID, SL_NONE, &p);
    sl_state(&KID, &p, &st);
    sr_project(&p, &GOAL[4], &gq);
    sr_ask(&gq, &st, &r);
    printf("  state: %s, %u worlds standing\n", VNAME[r.verdict], sr_live_count(&st));
    sl_recover(&KID, &p, &dropped);
    sl_state(&KID, &p, &st);
    sr_ask(&gq, &st, &r);
    printf("  after recovery: dropped %s=%u, %u worlds standing, the total is four: %s = %u\n",
           TABLE[dropped.cap].name, dropped.answer, sr_live_count(&st), VNAME[r.verdict],
           r.value);
  }
  printf("\n  It did not lower a confidence and carry on. The claims were jointly\n");
  printf("  impossible, which is a verdict of its own, and it dropped one and\n");
  printf("  said which. Choosing the most recent is a policy; that the set was\n");
  printf("  impossible is not.\n");
  return 0;
}
