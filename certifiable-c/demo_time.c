/*
 * demo_time.c -- a toy car on a track, pushed around: learning what
 * pushing does, knowing where it is in the dark, acting without knowing,
 * and finding out what can never be found out.
 *
 * The track has 5 places, walls at both ends. The car is red or blue.
 * While learning, the child watches pushes with the lights on. Later, in
 * the dark, all it has is a touch sensor: "is the car against a wall?".
 */

#include <stdio.h>

#include "smarsh_time.h"

static tm_model_t M;
static tm_plan_t P;

enum { LEFT = 0, RIGHT = 1 };
static unsigned pos_of(unsigned s) { return s % 5u; }
static unsigned red(unsigned s) { return s >= 5u; }
static unsigned push(unsigned s, unsigned a) {
  unsigned p = pos_of(s);
  if (a == LEFT && p > 0u) p--;
  if (a == RIGHT && p < 4u) p++;
  return p + (red(s) ? 5u : 0u);
}

static void show(const char *label, tm_set_t set) {
  unsigned s, first = 1u;
  printf("  %-34s {", label);
  for (s = 0u; s < M.n_states; s++) {
    if (set & ((tm_set_t)1 << s)) { printf("%s%s", first ? "" : " ", M.state_name[s]); first = 0u; }
  }
  printf("}\n");
}

static void build(unsigned skip_state, unsigned skip_action) {
  unsigned s, a;
  static const char *names[10] = {"b0", "b1", "b2", "b3", "b4", "r0", "r1", "r2", "r3", "r4"};
  tm_init(&M, 10u, 2u);
  for (s = 0u; s < 10u; s++) {
    unsigned k;
    for (k = 0u; names[s][k] != '\0'; k++) M.state_name[s][k] = names[s][k];
    M.sensor[s] = pos_of(s) == 0u || pos_of(s) == 4u;
  }
  for (a = 0u; a < 2u; a++) {
    for (s = 0u; s < 10u; s++) {
      if (s == skip_state && a == skip_action) continue;
      tm_watch(&M, s, a, push(s, a));
    }
  }
}

static void plan(const char *label, tm_set_t from, tm_set_t goal) {
  static const char *act[2] = {"left", "right"};
  unsigned k;
  tm_plan(&M, from, goal, &P);
  printf("  %s\n", label);
  if (P.found) {
    printf("    plan:");
    for (k = 0u; k < P.length; k++) printf(" %s", act[P.action[k]]);
    printf("   (%u sets of possibilities searched; re-checked by running it: %s)\n", P.explored,
           tm_check_plan(&M, from, goal, &P) ? "works from every possible start" : "FAILS");
  } else if (P.proved_impossible) {
    unsigned a, all_seen = 1u;
    for (a = 0u; a < M.n_actions; a++) if (M.seen[a] != tm_all(&M)) all_seen = 0u;
    printf("    NO PLAN EXISTS%s. All %u sets of possibilities it could reach were tried.\n",
           all_seen ? "" : " using only pushes it has watched", P.explored);
  } else {
    printf("    none found within the length limit (not a proof)\n");
  }
}

int main(void) {
  unsigned cls[TM_MAX_STATES], n, unseen;
  tm_set_t now, blue_or_red_at0 = 0u, red_at0 = (tm_set_t)1 << 5;

  printf("TIME AND CHANGE: A TOY CAR ON A TRACK\n");
  printf("States: place 0 to 4, blue (b) or red (r). Pushes: left, right; walls at\n");
  printf("both ends. It learns by watching pushes with the lights on.\n\n");

  build(99u, 99u);
  printf("WHAT IS REALLY DIFFERENT\n");
  tm_merge(&M, cls, &n);
  printf("  10 states as described; %u that pushing and touching could ever tell apart.\n", n);
  printf("  Colour never changes what a push does or what the sensor feels, so no\n");
  printf("  amount of acting in the dark could reveal it. Time decides what matters.\n\n");

  printf("IN THE DARK. It has no idea where the car is.\n");
  now = tm_all(&M);
  show("at the start, possible:", now);
  now = tm_sense(&M, now, 0u);
  show("touch: not against a wall", now);
  now = tm_step(&M, now, RIGHT, &unseen);
  show("push right", now);
  now = tm_sense(&M, now, 1u);
  show("touch: against a wall", now);
  printf("  It knows exactly where the car is, and it knows it cannot know the colour.\n\n");

  printf("ACTING WITHOUT KNOWING\n");
  blue_or_red_at0 = ((tm_set_t)1 << 0) | ((tm_set_t)1 << 5);
  plan("get the car to place 0, starting with no idea where it is:", tm_all(&M), blue_or_red_at0);
  plan("get a RED car to place 0, starting with no idea:", tm_all(&M), red_at0);
  printf("  Pushing never changes the colour, and it proved that no sequence of\n");
  printf("  pushes, of any length, can make sure of a red car.\n\n");

  printf("A PUSH IT NEVER WATCHED\n");
  build(2u, LEFT);
  printf("  This time it never saw what pushing LEFT does to a blue car at place 2.\n");
  plan("get the car to place 0, starting with no idea:", tm_all(&M), blue_or_red_at0);
  printf("  It will not bet a plan on a push it has never seen. Any route to place 0\n");
  printf("  from total ignorance passes through that case.\n");
  tm_watch(&M, 2u, LEFT, push(2u, LEFT));
  printf("  It watches that one push, and asks again:\n");
  plan("get the car to place 0, starting with no idea:", tm_all(&M), blue_or_red_at0);
  return 0;
}
