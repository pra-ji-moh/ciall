/*
 * demo_child.c -- one learner, several days of its life.
 *
 * Nobody tells it what to do at any point. It decides what to try, tries
 * it, works out what it can, and knows where it stands. Its abilities
 * arrive in an order it chose for itself, and one of them is later taken
 * away by an experiment that catches a law of its own out.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_child.h"

static ch_child_t KID;
static char REPORT[4096];

/* the world: three slots each side, and the scale */
static unsigned tips(unsigned s) {
  unsigned l = (s & 1u) + ((s >> 1) & 1u) + ((s >> 2) & 1u);
  unsigned r = ((s >> 3) & 1u) + ((s >> 4) & 1u) + ((s >> 5) & 1u);
  return l > r ? 0u : (l == r ? 1u : 2u);
}
static unsigned pour_honest(unsigned a, unsigned b) { return a + b; }
/* the same world, except one pebble is quietly stuck to the tray: pouring
   six and six gives eleven, not twelve */
static unsigned pour_odd(unsigned a, unsigned b) { return (a == 6u && b == 6u) ? 11u : a + b; }

static const ch_world_t HONEST = {6u, 7u, tips, pour_honest};
static const ch_world_t ODD = {6u, 7u, tips, pour_odd};

static void day(const char *title, const ch_world_t *w, unsigned budget, int show_all) {
  unsigned i, was_refuted = KID.refutations;
  int had[CH_N_ABILITIES];
  int caught;
  printf("\n%s\n", title);
  for (i = 0u; i < CH_N_ABILITIES; i++) had[i] = KID.acquired[i];
  for (i = 0u; i < budget; i++) {
    ch_plan_t plan = ch_decide(&KID, w);
    unsigned k;
    if (!ch_step(&KID, w)) {
      printf("  it stops: %s\n", KID.note);
      break;
    }
    caught = KID.refutations > was_refuted;
    was_refuted = KID.refutations;
    if (show_all || i < 2u || caught) {
      printf("  it chooses: %s\n", plan.why);
      printf("    %s\n", KID.note);
    }
    for (k = 0u; k < CH_N_ABILITIES; k++) {
      if (KID.acquired[k] && !had[k]) {
        printf("    >>> IT CAN NOW: %s\n", CH_ABILITY_NAME[k]);
        had[k] = 1;
      } else if (!KID.acquired[k] && had[k]) {
        printf("    >>> IT HAS LOST: %s\n", CH_ABILITY_NAME[k]);
        had[k] = 0;
      }
    }
  }
}

static void asking(const char *what, ch_standing_t st, uint64_t value, const char *why) {
  static const char *label[4] = {"seen", "proved", "a guess", "refused"};
  printf("  %-34s ", what);
  if (st == CH_REFUSED) printf("REFUSES: %s\n", why);
  else printf("%llu   (%s: %s)\n", (unsigned long long)value, label[st], why);
}

int main(void) {
  char why[CH_TEXT];
  uint64_t v;
  unsigned u;
  ch_standing_t st;

  printf("A CHILD, LEFT ALONE WITH A SCALE AND SOME PEBBLES\n");
  printf("Nothing tells it what to try. It has a scale with three slots each\n");
  printf("side, and it can pour two trays together and count the pile.\n");
  ch_init(&KID);

  day("DAY ONE. It has never seen anything.", &HONEST, 12u, 0);
  ch_report(&KID, &HONEST, REPORT, sizeof REPORT);
  printf("%s", REPORT);

  day("DAY TWO. It carries on, remembering everything.", &HONEST, 60u, 0);
  ch_report(&KID, &HONEST, REPORT, sizeof REPORT);
  printf("%s", REPORT);

  printf("\nSOMEBODY ASKS IT THINGS\n");
  st = ch_which_way(&KID, 2u, 1u, &u, why, sizeof why);
  asking("two pebbles against one:", st, u, why);
  st = ch_which_way(&KID, 5u, 2u, &u, why, sizeof why);
  asking("five against two:", st, u, why);
  st = ch_pour(&KID, 3u, 4u, &v, why, sizeof why);
  asking("pour three and four:", st, v, why);
  st = ch_pour(&KID, 40u, 70u, &v, why, sizeof why);
  asking("pour forty and seventy:", st, v, why);

  day("DAY THREE. A pebble is quietly stuck to one tray, and it does not know.",
      &ODD, 400u, 0);
  ch_report(&KID, &ODD, REPORT, sizeof REPORT);
  printf("%s", REPORT);

  printf("\nWHAT HAPPENED\n");
  printf("  It chose every experiment itself, always where it was blind or where\n");
  printf("  it had guessed and could be caught out. It gained counting, then a\n");
  printf("  rule for the scale, then pouring, then a law reaching past anything it\n");
  printf("  had poured, then a theorem about every number. It remembered all of it\n");
  printf("  from one day to the next. And when the world stopped agreeing with its\n");
  printf("  law, it did not explain the result away: it lost the ability that\n");
  printf("  rested on it and said so.\n");
  printf("\n  What is still missing: it cannot make up a NEW KIND of experiment,\n");
  printf("  only choose among the ones its world offers. Inventing the question\n");
  printf("  \"what if I took pebbles away?\" is the next thing to build.\n");
  return 0;
}
