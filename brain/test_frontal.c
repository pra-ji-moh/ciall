/*
 * test_frontal.c -- the blicket box (after Gopnik): does its frontal cortex
 * reason, and does it do better than learning by reward?
 *
 * A box in the crib chimes when the being does the right thing with its
 * eyes on the right part of the wall. The rule is hidden, one of 144: one act
 * of its six, or both of two, or either of two (15 pairs each), times which
 * quarter of the wall its eyes must be on. log2(144) is about 7.2: an ideal
 * eliminator needs about seven or eight tries.
 *
 * Each try is carried out by its own body: its frontal eye fields move its eyes
 * (through its brainstem's zones) to the quarter, then it does the acts. The
 * box's chime reaches its cochlea, and it reads the outcome from there, not
 * from the box.
 *
 * Checked (each could fail):
 *   finds        a three-year-old's frontal cortex finds the rule, in every box
 *   quickly      in about log2(144) tries (no more than twice it)
 *   uses it      having found it, its next ten tries through its body sound
 *                the box at least eight times in ten
 *   reads        what it hears is what the box did (95%)
 *   reward       its twin learning by reward alone (dopamine when the box
 *                chimes, as it learned the mobile) sounds it far less often,
 *                after more moments than the reasoner took
 *   age          a three-month-old (a mind of little room: the prefrontal
 *                cortex has barely begun to myelinate) needs more tries
 *   ACC          without its anterior cingulate it never finds the rule
 *   DLPFC        without its dorsolateral prefrontal cortex (one hypothesis
 *                in mind at a time) it strikes most of them out but never
 *                finds the rule: it perseverates. With one idea in mind there
 *                is nothing to split, so it tests the idea by what it predicts,
 *                and the test that worked best before; when that confirms it,
 *                the rivals the same act would confirm are never ruled out.
 *                (Predicted first: that it would find it, three times slower.
 *                It did not, in any box. Patients with DLPFC damage
 *                perseverate on the Wisconsin card sort; it does too)
 *   distinct     without the ACC nothing is struck at all
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ob_crib.h"
#include "ob_frontal.h"
#include "ob_one.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  if (!ok) n_fail++;
  printf("  %s  %s\n", ok ? "pass" : "FAIL", name);
}

static ob_brain_t GROWN;
static ob_one_t ONE;

#define FORMS 36                      /* 6 single acts, 15 pairs needing both, 15 either */
#define N_HYP (FORMS * 4)
#define BOXES 10
#define MAX_TRIES 300

static unsigned PAIR_A[15], PAIR_B[15];

/* the acts a form needs: returns 1 if `acts` satisfies form f */
static int form_holds(unsigned f, unsigned acts) {
  if (f < 6u) return acts >> f & 1u;
  if (f < 21u) return (acts >> PAIR_A[f - 6u] & 1u) && (acts >> PAIR_B[f - 6u] & 1u);
  return (acts >> PAIR_A[f - 21u] & 1u) || (acts >> PAIR_B[f - 21u] & 1u);
}

/* hypothesis h: form h / 4, with its eyes on quarter h % 4 */
static int says(unsigned h, const of_test_t *t) { return t->place == h % 4u && form_holds(h / 4u, t->acts); }

static unsigned quarter(const ob_crib_t *c) { return (c->gx >= 0.5f) + 2u * (c->gy >= 0.5f); }

/* the box chimes, high: its cochlea's top band. (It first had a light: where
   the wall was already white, a light added nothing its retina could see, and
   one misread in ten struck out the true rule. It reads the moment just after
   its act: the chime lasts a moment, so one its eyes set off on the way there
   has passed) */
#define CHIME (OL_VIS + OL_EAR - 1)

/* the world: the crib, and the box; `flash` its chime reaching its ear now */
typedef struct {
  ob_crib_t crib;
  unsigned rule;
  int flash;
  unsigned long moments, lights, acted;
} box_t;

/* one moment: it lives (deliberately if `intent_on`), the world answers.
   Returns what its ear hears there now */
static float moment(box_t *x, ob_one_t *O, unsigned intent_on, unsigned intent, int reward) {
  float in[OL_INPUT];
  unsigned bits, lit;
  ob_crib_sense(&x->crib, in);
  if (x->flash) in[CHIME] = 1.0f;
  O->reward_now = reward && x->flash ? 1.0f : 0.0f;
  O->intent_on = intent_on;
  O->intent = intent;
  bits = ob_one_live(O, in);
  /* the box judges what it did, where its eyes were */
  lit = bits != 0u && says(x->rule, &(of_test_t){bits, quarter(&x->crib)});
  x->flash = (int)lit;
  x->lights += lit;
  x->acted += bits != 0u;
  ob_crib_act(&x->crib, bits);
  x->moments++;
  return in[CHIME];
}

/* its frontal eye fields: move its eyes into quarter q, clear of the middle */
static void look_to(box_t *x, ob_one_t *O, unsigned q) {
  unsigned k;
  float tx = q & 1u ? 0.75f : 0.25f, ty = q & 2u ? 0.75f : 0.25f;
  for (k = 0u; k < 24u; k++) {
    unsigned want = 0u;
    if (x->crib.gx > tx + 0.06f) want |= 1u << OL_LOOK_LEFT;
    if (x->crib.gx < tx - 0.06f) want |= 1u << OL_LOOK_RIGHT;
    if (x->crib.gy > ty + 0.06f) want |= 1u << OL_LOOK_UP;
    if (x->crib.gy < ty - 0.06f) want |= 1u << OL_LOOK_DOWN;
    if (!want) return;
    moment(x, O, 1u, want, 0);
  }
}

/* one try, through its body: eyes to the place, the acts, then still while it
   sees. Returns what it read (1: the box lit); *truth what the box did */
static int try_it(box_t *x, ob_one_t *O, const of_test_t *t, int *truth) {
  unsigned long before;
  float heard;
  look_to(x, O, t->place);
  before = x->lights;
  moment(x, O, 1u, t->acts, 0);
  *truth = x->lights > before;
  heard = moment(x, O, 1u, 0u, 0);
  return heard > 0.5f;
}

typedef struct {
  double tries, found, after_ok, reads_ok, reads, moments, struck, left;
} result_t;

static of_test_t TESTS[63 * 4];

static void reason(unsigned box, double months, unsigned no_acc, unsigned no_dlpfc, result_t *r) {
  static of_rules_t R;
  box_t x;
  unsigned k, which, tries = 0u, ok = 0u;
  int truth;
  memset(&x, 0, sizeof(x));
  ob_crib_make(&x.crib, 100u + box);
  x.rule = (box * 2654435761u >> 7) % N_HYP;
  ob_one_born(&ONE, &GROWN, 1u + box, OL_PROGRESS);
  of_begin(&R, N_HYP, says, of_capacity(&GROWN, months), box);
  R.no_acc = no_acc;
  R.no_dlpfc = no_dlpfc;
  while (!of_known(&R, &which) && tries < MAX_TRIES) {
    of_test_t t = of_choose(&R, TESTS, 63u * 4u);
    int seen = try_it(&x, &ONE, &t, &truth);
    unsigned struck = of_learn(&R, &t, seen);
    of_show(&R, &ONE, struck);
    r->reads += 1.0;
    r->reads_ok += seen == truth;
    tries++;
  }
  r->tries += tries;
  r->struck += R.struck;
  for (k = 0u; k < N_HYP; k++) r->left += R.standing[k];
  r->moments += (double)x.moments;
  if (of_known(&R, &which) && which == x.rule) {
    r->found += 1.0;
    /* and now it uses what it knows: the simplest acts its rule needs */
    for (k = 0u; k < 10u; k++) {
      of_test_t t = {0u, which % 4u};
      unsigned a;
      for (a = 1u; a < 64u && !t.acts; a++)
        if (form_holds(which / 4u, a)) t.acts = a;
      try_it(&x, &ONE, &t, &truth);
      ok += truth;
    }
    r->after_ok += ok / 10.0;
  }
}

/* the twin: no frontal reasoning; dopamine when the box chimes */
static double by_reward(unsigned box, unsigned long moments) {
  box_t x;
  unsigned long t, lit0 = 0u, act0 = 0u;
  memset(&x, 0, sizeof(x));
  ob_crib_make(&x.crib, 100u + box);
  x.rule = (box * 2654435761u >> 7) % N_HYP;
  ob_one_born(&ONE, &GROWN, 1u + box, OL_PROGRESS);
  for (t = 0u; t < moments; t++) {
    if (t == moments - 2000u) {
      lit0 = x.lights;
      act0 = x.acted;
    }
    moment(&x, &ONE, 0u, 0u, 1);
  }
  /* in its last 2000 moments: of the moments it acted, how many sounded the box */
  return x.acted > act0 ? (double)(x.lights - lit0) / (double)(x.acted - act0) : 0.0;
}

int main(void) {
  ob_genome_t g;
  unsigned i, j, n = 0u, b;
  result_t adult = {0}, young = {0}, no_acc = {0}, no_dl = {0};
  double reward = 0.0;
  for (i = 0u; i < 6u; i++)
    for (j = i + 1u; j < 6u; j++) {
      PAIR_A[n] = i;
      PAIR_B[n] = j;
      n++;
    }
  for (i = 0u; i < 63u; i++)
    for (j = 0u; j < 4u; j++) TESTS[i * 4u + j] = (of_test_t){i + 1u, j};
  printf("THE BLICKET BOX: THE FRONTAL CORTEX REASONS\n\n");
  ob_genome_default(&g);
  ob_grow(&GROWN, &g);
  printf("  its DLPFC can hold %u hypotheses at 3 months, %u at 3 years (of %u)\n\n",
         of_capacity(&GROWN, 3.0), of_capacity(&GROWN, 36.0), N_HYP);

  for (b = 0u; b < BOXES; b++) {
    reason(b, 36.0, 0u, 0u, &adult);
    reason(b, 3.0, 0u, 0u, &young);
    reason(b, 36.0, 1u, 0u, &no_acc);
    reason(b, 36.0, 0u, 1u, &no_dl);
  }
  printf("  %-24s %8s %8s %8s %8s %8s\n", "", "found", "tries", "moments", "struck", "left");
#define ROW(name, r) printf("  %-24s %5.0f/%-2u %8.1f %8.0f %8.1f %8.1f\n", name, r.found, BOXES, r.tries / BOXES, \
                            r.moments / BOXES, r.struck / BOXES, r.left / BOXES)
  ROW("three years", adult);
  ROW("three months", young);
  ROW("three years, no ACC", no_acc);
  ROW("three years, no DLPFC", no_dl);
  printf("  after finding it, its tries sound the box: %.2f\n", adult.found > 0 ? adult.after_ok / adult.found : 0.0);
  printf("  heard correctly: %.3f\n", adult.reads_ok / adult.reads);

  for (b = 0u; b < 3u; b++) reward += by_reward(b, 20000u) / 3.0;
  printf("  its twin by reward alone, 20000 moments: the box chimes on %.3f of its acts\n\n", reward);

  check("finds: a three-year-old finds the rule in every box", adult.found == BOXES);
  check("quickly: in no more than twice log2(144) tries", adult.tries / BOXES <= 2.0 * log2((double)N_HYP));
  check("uses it: its next tries sound the box (>= 0.8)", adult.found > 0 && adult.after_ok / adult.found >= 0.8);
  check("reads: what it hears is what the box did (>= 0.95)", adult.reads_ok / adult.reads >= 0.95);
  check("reward: its twin sounds it far less (< 0.8, after more moments)",
        reward < 0.8 && 20000.0 > adult.moments / BOXES);
  check("age: a three-month-old needs more tries", young.tries > adult.tries);
  check("ACC: without it, it never finds the rule", no_acc.found == 0.0);
  check("DLPFC: without it, it strikes much out, but perseverates (never finds it)",
        no_dl.found == 0.0 && no_dl.struck / BOXES >= 0.8 * (N_HYP - 1) && no_dl.left > no_dl.found);
  check("the two lesions differ: without the ACC nothing at all is struck", no_acc.struck == 0.0);
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail != 0u;
}
