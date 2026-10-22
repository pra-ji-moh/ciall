/*
 * test_frontal.c -- one life, from its genes to three years old: does
 * reasoning switch on as its frontal cortex myelinates, and does it then do
 * better than learning by reward?
 *
 * One being, never restarted: its brain grows from the genome; in the womb it
 * learns touch, smell, its mother's heartbeat, its eyes' maps, its reflexes;
 * it is born; it lives in the crib, of itself, curious, as its brain ages and
 * the myelination wave goes through it (ob_one_age), a thousand moments a
 * month. At 3, 9, 12, 24 and 36 months a blicket box is put in its crib, a
 * new hidden rule each time (one of 144: log2 is 7.2), for 1500 moments. If
 * its DLPFC can hold anything, it reasons (ob_frontal.c: hypotheses held by
 * groups of DLPFC neurons that keep themselves firing; the ACC darkening what
 * an outcome contradicts); if not, it lives as it would, and the chime's
 * dopamine teaches its basal ganglia, as the mobile did.
 *
 * At three years, copies of it, with the same life behind them, meet the same
 * boxes with a lesion: no ACC; one DLPFC group; no frontal reasoning at all.
 *
 * Checked (each could fail):
 *   room         what its DLPFC can hold, measured from its neurons, is
 *                nothing at 3 months and grows with age after
 *   onset        reasoning begins between 6 and 12 months (infants first
 *                plan, and pass A-not-B, around 8 to 12 months)
 *   infant       at 3 months it finds no rule, and sounds the box rarely
 *   three years  it finds every rule, in about log2(144) tries (at most twice)
 *   age          tries fall: a one-year-old needs more than a three-year-old
 *   uses it      knowing the rule, it sounds the box at least five times as
 *                often as the 3-month-old did
 *   reward       its twin at three years without frontal reasoning, the same
 *                life behind it, sounds the box far less often
 *   reads        what it hears is what the box did (95%)
 *   ACC          without its ACC it never finds a rule; nothing is ruled out
 *   DLPFC        with one DLPFC group it never finds one, but rules much out:
 *                it perseverates (as found before, and as patients do on the
 *                Wisconsin card sort)
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ob.h"
#include "ob_box.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  if (!ok) n_fail++;
  printf("  %s  %s\n", ok ? "pass" : "FAIL", name);
}

static ob_brain_t BRAIN;
static ob_one_t ONE, SAVED;
static ob_world_t WORLD, WORLD_SAVED;
static ob_womb_stats_t TOUCH;
static ob_smell_stats_t SMELL;
static ob_heart_stats_t HEART;
static ob_vision_stats_t VISION;
static ob_reflex_stats_t REFLEX;

#define PER_MONTH 1000ul
#define BOXES 3
#define WITH_BOX 1500ul
#define N_AGES 5

static const double AGE[N_AGES] = {3.0, 9.0, 12.0, 24.0, 36.0};

typedef struct {
  double room, found, tries, rate, struck, reads, reads_ok;
} at_t;

static unsigned rule_of(unsigned age, unsigned box) { return ((age * 7u + box + 1u) * 2654435761u >> 9) % OB_BOX_RULES; }

static void meet(ob_world_t *W, ob_one_t *O, unsigned age, unsigned lesions, at_t *a) {
  unsigned b;
  memset(a, 0, sizeof(*a));
  for (b = 0u; b < BOXES; b++) {
    ob_box_meeting_t m;
    ob_box_meet(W, O, rule_of(age, b), WITH_BOX, lesions, &m);
    a->room += m.room / (double)BOXES;
    a->found += m.found;
    a->tries += m.found ? m.tries / (double)BOXES : 0.0;
    a->rate += m.chime_rate / BOXES;
    a->struck += m.struck / (double)BOXES;
    a->reads += m.reads;
    a->reads_ok += m.reads_ok;
    ob_grow_up(W, O, O->months + 0.1, PER_MONTH);   /* a little while between */
  }
}

static void row(const char *name, const at_t *a) {
  printf("  %-26s %6.1f %5.0f/%u %7.1f %8.3f %8.1f\n", name, a->room, a->found, BOXES,
         a->found > 0 ? a->tries * BOXES / a->found : 0.0, a->rate, a->struck);
}

int main(void) {
  ob_genome_t g;
  at_t at[N_AGES], no_acc, no_dl, no_fr;
  unsigned k;
  int onset = -1;
  printf("ONE LIFE: FROM ITS GENES TO THREE YEARS OLD\n\n");
  ob_genome_default(&g);
  ob_grow(&BRAIN, &g);
  ob_womb_touch(&BRAIN, 20u, 300u, &TOUCH);
  ob_womb_smell(&BRAIN, 20u, 50u, &SMELL);
  ob_womb_heartbeat(&BRAIN, 20u, 400u, &HEART);
  ob_womb_vision(&BRAIN, 20u, 200u, 1, &VISION);
  ob_womb_reflex(&BRAIN, 20u, 100u, 1, &REFLEX);
  printf("  grown from its genes and the womb: %u neurons, %u connections; born\n", BRAIN.n_neurons, BRAIN.n_syn);
  printf("  the myelination wave (months after birth at which each is half myelinated):\n");
  {
    static const unsigned R[] = {OB_BRAINSTEM, OB_THALAMUS, OB_OCCIPITAL, OB_CEREBELLUM, OB_BASAL_GANGLIA,
                                 OB_PARIETAL, OB_TEMPORAL, OB_FRONTAL};
    static const char *const NAME[] = {"brainstem", "thalamus", "visual cortex", "cerebellum", "basal ganglia",
                                       "parietal", "temporal", "frontal"};
    for (k = 0u; k < sizeof R / sizeof R[0]; k++) {
      double m = -6.0;
      while (ob_myelin_at(R[k], m) < 0.5f) m += 0.5;
      printf("    %-14s %5.1f\n", NAME[k], m);
    }
  }
  ob_one_born(&ONE, &BRAIN, g.seed, OL_PROGRESS);
  ob_one_age(&ONE, 0.0);
  ob_world_make(&WORLD, g.seed);

  printf("\n  %-26s %6s %7s %7s %8s %8s\n", "", "room", "found", "tries", "chimes", "struck");
  for (k = 0u; k < N_AGES; k++) {
    char name[64];
    ob_grow_up(&WORLD, &ONE, AGE[k], PER_MONTH);
    meet(&WORLD, &ONE, k, 0u, &at[k]);
    snprintf(name, sizeof name, "%.0f months", AGE[k]);
    row(name, &at[k]);
    fflush(stdout);
    if (onset < 0 && at[k].room >= 1.0) onset = (int)k;
    if (k + 1u == N_AGES) {
      /* at three years: copies of it, the same life behind them, lesioned */
      memcpy(&SAVED, &ONE, sizeof SAVED);
      memcpy(&WORLD_SAVED, &WORLD, sizeof WORLD_SAVED);
#define LESIONED(flag, out, name)                         \
  do {                                                    \
    memcpy(&ONE, &SAVED, sizeof ONE);                     \
    memcpy(&WORLD, &WORLD_SAVED, sizeof WORLD);           \
    ONE.months = AGE[k];                                  \
    meet(&WORLD, &ONE, k, flag, &out);                    \
    row(name, &out);                                      \
    fflush(stdout);                                       \
  } while (0)
      LESIONED(OB_LESION_FRONTAL, no_fr, "36, no frontal reasoning");
      LESIONED(OB_LESION_ACC, no_acc, "36, no ACC");
      LESIONED(OB_LESION_DLPFC, no_dl, "36, one DLPFC group");
    }
  }
  printf("  (room: hypotheses its DLPFC holds; tries: to know the rule, where found;\n"
         "   chimes: per moment, in the last 300 moments with the box)\n\n");

  check("room: nothing at 3 months, growing with age after",
        at[0].room == 0.0 && at[2].room < at[3].room && at[3].room < at[4].room);
  check("onset: reasoning begins between 6 and 12 months", onset == 1 || onset == 2);
  check("infant: at 3 months no rule found, the box rarely sounded", at[0].found == 0.0 && at[0].rate < 0.1);
  check("three years: every rule found, in at most twice log2(144) tries",
        at[4].found == BOXES && at[4].tries <= 2.0 * log2((double)OB_BOX_RULES));
  check("age: a one-year-old needs more tries than a three-year-old",
        at[2].found > 0 && at[2].tries * BOXES / at[2].found > at[4].tries);
  check("uses it: knowing, it sounds the box 5x as often as at 3 months",
        at[4].rate >= 5.0 * at[0].rate && at[4].rate > 0.1);
  check("reward: without frontal reasoning, the same life, far fewer chimes", no_fr.rate < 0.2 * at[4].rate);
  check("reads: what it hears is what the box did (>= 0.95)", at[4].reads > 0 && at[4].reads_ok / at[4].reads >= 0.95);
  check("ACC: without it, no rule found and nothing ruled out", no_acc.found == 0.0 && no_acc.struck == 0.0);
  check("DLPFC: with one group, it rules much out but perseverates", no_dl.found == 0.0 && no_dl.struck >= 50.0);
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail != 0u;
}
