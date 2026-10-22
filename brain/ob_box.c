/*
 * ob_box.c -- growing up in the crib, and the blicket box. See ob_box.h.
 */

#include "ob_box.h"

#include <string.h>

/* the box chimes, high: its cochlea's top band. (It first had a light: where
   the wall was already white, a light added nothing its retina could see, and
   one misread in ten struck out the true rule. It reads the moment just after
   its act: the chime lasts a moment, so one its eyes set off on the way there
   has passed) */
#define CHIME (OL_VIS + OL_EAR - 1)

static unsigned PAIR_A[15], PAIR_B[15];
static of_test_t TESTS[OB_BOX_TESTS];
static int ready;

static void setup(void) {
  unsigned i, j, n = 0u;
  if (ready) return;
  for (i = 0u; i < 6u; i++)
    for (j = i + 1u; j < 6u; j++) {
      PAIR_A[n] = i;
      PAIR_B[n] = j;
      n++;
    }
  for (i = 0u; i < 63u; i++)
    for (j = 0u; j < 4u; j++) TESTS[i * 4u + j] = (of_test_t){i + 1u, j};
  ready = 1;
}

/* the acts a form needs: 6 single acts, 15 pairs needing both, 15 either */
static int form_holds(unsigned f, unsigned acts) {
  if (f < 6u) return acts >> f & 1u;
  if (f < 21u) return (acts >> PAIR_A[f - 6u] & 1u) && (acts >> PAIR_B[f - 6u] & 1u);
  return (acts >> PAIR_A[f - 21u] & 1u) || (acts >> PAIR_B[f - 21u] & 1u);
}

/* rule h: form h / 4, with its eyes on quarter h % 4 */
int ob_box_says(unsigned h, const of_test_t *t) { return t->place == h % 4u && form_holds(h / 4u, t->acts); }

static unsigned quarter(const ob_crib_t *c) { return (c->gx >= 0.5f) + 2u * (c->gy >= 0.5f); }

void ob_world_make(ob_world_t *W, unsigned seed) {
  setup();
  memset(W, 0, sizeof(*W));
  ob_crib_make(&W->crib, seed);
}

float ob_world_moment(ob_world_t *W, ob_one_t *O, unsigned intent_on, unsigned intent) {
  float in[OL_INPUT];
  unsigned bits;
  int lit;
  ob_crib_sense(&W->crib, in);
  if (W->chime) in[CHIME] = 1.0f;
  O->reward_now = W->chime ? 1.0f : 0.0f;
  O->intent_on = intent_on;
  O->intent = intent;
  bits = ob_one_live(O, in);
  /* the box judges what it did, where its eyes were */
  lit = W->box_on && bits != 0u && ob_box_says(W->rule, &(of_test_t){bits, quarter(&W->crib)});
  W->chime = lit;
  W->chimes += (unsigned long)lit;
  ob_crib_act(&W->crib, bits);
  W->moments++;
  return in[CHIME];
}

void ob_grow_up(ob_world_t *W, ob_one_t *O, double months, unsigned long per_month) {
  unsigned long t, tenth = per_month / 10u ? per_month / 10u : 1u;
  double start = O->months;
  unsigned long n = (unsigned long)((months - start) * (double)per_month + 0.5);
  for (t = 0u; t < n; t++) {
    if (t % tenth == 0u) ob_one_age(O, start + (double)t / (double)per_month);
    ob_world_moment(W, O, 0u, 0u);
  }
  ob_one_age(O, months);
}

/* its frontal eye fields: move its eyes into quarter q, clear of the middle */
static void look_to(ob_world_t *W, ob_one_t *O, unsigned q) {
  unsigned k;
  float tx = q & 1u ? 0.75f : 0.25f, ty = q & 2u ? 0.75f : 0.25f;
  for (k = 0u; k < 24u; k++) {
    unsigned want = 0u;
    if (W->crib.gx > tx + 0.06f) want |= 1u << OL_LOOK_LEFT;
    if (W->crib.gx < tx - 0.06f) want |= 1u << OL_LOOK_RIGHT;
    if (W->crib.gy > ty + 0.06f) want |= 1u << OL_LOOK_UP;
    if (W->crib.gy < ty - 0.06f) want |= 1u << OL_LOOK_DOWN;
    if (!want) return;
    ob_world_moment(W, O, 1u, want);
  }
}

/* one try, through its body: eyes to the place, the acts, then still while it
   hears. Returns what it heard (1: a chime); *truth what the box did */
static int try_it(ob_world_t *W, ob_one_t *O, const of_test_t *t, int *truth) {
  look_to(W, O, t->place);
  ob_world_moment(W, O, 1u, t->acts);
  *truth = W->chime;
  return ob_world_moment(W, O, 1u, 0u) > 0.5f;
}

void ob_box_meet(ob_world_t *W, ob_one_t *O, unsigned rule, unsigned long moments, unsigned lesions,
                 ob_box_meeting_t *out) {
  static of_rules_t R;
  unsigned long start = W->moments, mark_t = 0u, mark_c = 0u;
  unsigned which = 0u, k;
  int knows = 0, marked = 0, truth;
  setup();
  memset(out, 0, sizeof(*out));
  W->box_on = 1;
  W->rule = rule;
  /* its frontal cortex, lesioned as asked, and what it brings to mind */
  of_begin(&R, O, OB_BOX_RULES, ob_box_says, rule + 1u, lesions & OB_LESION_ACC ? 1u : 0u,
           lesions & OB_LESION_DLPFC ? 1u : 0u, lesions & OB_LESION_FRONTAL ? 1u : 0u);
  out->room = of_room(&R);
  while (W->moments - start < moments) {
    if (!marked && W->moments - start >= moments - 300u) {
      marked = 1;
      mark_t = W->moments;
      mark_c = W->chimes;
    }
    if (knows) {
      /* it uses what it knows: the simplest acts its rule needs */
      of_test_t t = {0u, which % 4u};
      unsigned a;
      for (a = 1u; a < 64u && !t.acts; a++)
        if (form_holds(which / 4u, a)) t.acts = a;
      try_it(W, O, &t, &truth);
    } else if (of_in_mind(&R) > 0u) {
      of_test_t t = of_choose(&R, TESTS, OB_BOX_TESTS);
      int heard = try_it(W, O, &t, &truth);
      out->reads++;
      out->reads_ok += heard == truth;
      of_learn(&R, &t, heard);
      out->tries++;
      knows = of_known(&R, &which);
    } else {
      ob_world_moment(W, O, 0u, 0u);   /* it holds nothing in mind: it lives as it would */
    }
  }
  out->found = knows && which == rule;
  if (!knows) out->tries = 0u;
  out->struck = R.struck;
  for (k = 0u; k < OB_BOX_RULES; k++) out->left += R.standing[k];
  out->chime_rate = marked && W->moments > mark_t ? (double)(W->chimes - mark_c) / (double)(W->moments - mark_t) : 0.0;
  of_end(&R);
  W->box_on = 0;
}
