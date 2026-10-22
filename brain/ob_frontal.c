/*
 * ob_frontal.c -- the frontal cortex's Hartley circuit. See ob_frontal.h.
 */

#include "ob_frontal.h"

#include <math.h>
#include <string.h>

unsigned of_area(const ob_neuron_t *n) {
  /* front to back (sx), floor to roof (sy): the orbitofrontal cortex at the
     front and floor, the cingulate along the floor behind it, the eye fields at
     the back by the motor strip, the dorsolateral prefrontal cortex above */
  if (n->region != OB_FRONTAL) return OF_OTHER;
  if (n->sx > 0.8f) return OF_FEF;
  if (n->sy < 0.3f) return n->sx < 0.35f ? OF_OFC : OF_ACC;
  return OF_DLPFC;
}

unsigned of_capacity(const ob_brain_t *b, double months) {
  unsigned i, dl = 0u, cap;
  double myelin;
  for (i = 0u; i < b->n_neurons; i++)
    if (!b->neuron[i].inhibitory && of_area(&b->neuron[i]) == OF_DLPFC) dl++;
  /* the prefrontal cortex is the last to myelinate: by about three years
     two thirds of the way, and on into adulthood */
  myelin = months <= 0.0 ? 0.0 : 1.0 - exp(-months / 36.0);
  cap = (unsigned)(myelin * (double)(dl / OF_GROUP));
  return cap < 1u ? 1u : cap > OF_MAX_HYP ? OF_MAX_HYP : cap;
}

static unsigned cap_of(const of_rules_t *R) { return R->no_dlpfc ? 1u : R->capacity; }

/* bring hypotheses still standing to mind, in their order, while there is room */
static void refill(of_rules_t *R) {
  unsigned k, held = 0u, cap = cap_of(R);
  for (k = 0u; k < R->n; k++) held += R->held[k];
  for (k = 0u; k < R->n && held < cap; k++) {
    unsigned h = R->order[(R->next + k) % R->n];
    if (R->standing[h] && !R->held[h]) {
      R->held[h] = 1u;
      held++;
      R->next = (R->next + k + 1u) % R->n;
      k = (unsigned)-1;                /* scan again from the new place */
    }
  }
}

void of_begin(of_rules_t *R, unsigned n, of_says_t says, unsigned capacity, unsigned seed) {
  unsigned k, rng = seed * 2654435761u + 7u;
  memset(R, 0, sizeof(*R));
  R->n = n > OF_MAX_HYP ? OF_MAX_HYP : n;
  R->says = says;
  R->capacity = capacity;
  for (k = 0u; k < R->n; k++) {
    R->standing[k] = 1u;
    R->order[k] = k;
  }
  /* the order ideas come to mind in: no reason to prefer any, so shuffled */
  for (k = R->n; k > 1u; k--) {
    unsigned j, t;
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    j = rng % k;
    t = R->order[k - 1u];
    R->order[k - 1u] = R->order[j];
    R->order[j] = t;
  }
  refill(R);
}

unsigned of_in_mind(const of_rules_t *R) {
  unsigned k, n = 0u;
  for (k = 0u; k < R->n; k++) n += R->held[k] && R->standing[k];
  return n;
}

of_test_t of_choose(of_rules_t *R, const of_test_t *tests, unsigned n_tests) {
  unsigned i, k, held = of_in_mind(R), best = 0u;
  int best_split = -1, best_yes = -1;
  float best_tried = -1.0f;
  for (i = 0u; i < n_tests; i++) {
    unsigned yes = 0u, split;
    float tried = R->tried_well[tests[i].acts & 63u];
    for (k = 0u; k < R->n; k++)
      if (R->held[k] && R->standing[k] && R->says(k, &tests[i])) yes++;
    /* the IPS counts; the even split teaches most (if yes, the yes-sayers are
       left; if no, the no-sayers). Where all it holds agree, it tries what
       they say will happen (a positive test: if nothing happens, they all go).
       Only then does experience order */
    split = yes < held - yes ? yes : held - yes;
    if ((int)split > best_split || ((int)split == best_split && (int)yes > best_yes) ||
        ((int)split == best_split && (int)yes == best_yes && tried > best_tried)) {
      best_split = (int)split;
      best_yes = (int)yes;
      best_tried = tried;
      best = i;
    }
  }
  return tests[best];
}

unsigned of_learn(of_rules_t *R, const of_test_t *t, int outcome) {
  unsigned k, struck = 0u, standing = 0u;
  R->tests++;
  R->tried_well[t->acts & 63u] += outcome ? 1.0f : 0.0f;
  if (!R->no_acc) {
    /* the ACC: what said otherwise is struck out, for good; only what is in
       mind can be */
    for (k = 0u; k < R->n; k++) {
      if (!R->held[k] || !R->standing[k]) continue;
      if (R->says(k, t) != (outcome != 0)) {
        R->standing[k] = 0u;
        R->held[k] = 0u;
        struck++;
      }
    }
    R->struck += struck;
  }
  for (k = 0u; k < R->n; k++) standing += R->standing[k];
  /* its mind full, nothing struck, and rivals it has not yet thought of: what
     it holds has passed this test, so the first of it makes room for a rival
     (it stays standing, to come back to mind in its turn) */
  if (struck == 0u && of_in_mind(R) >= cap_of(R) && standing > of_in_mind(R)) {
    for (k = 0u; k < R->n; k++) {
      unsigned h = R->order[(R->next + k) % R->n];
      if (R->held[h]) {
        R->held[h] = 0u;
        break;
      }
    }
  }
  refill(R);
  return struck;
}

int of_known(const of_rules_t *R, unsigned *which) {
  unsigned k, n = 0u, w = 0u;
  for (k = 0u; k < R->n; k++) {
    if (R->standing[k]) {
      n++;
      w = k;
    }
  }
  if (which != 0) *which = w;
  return n == 1u;
}

void of_show(const of_rules_t *R, ob_one_t *O, unsigned struck_now) {
  const ob_brain_t *b = &O->brain;
  unsigned i, group = 0u, slot = 0u, k;
  unsigned n_held = 0u;
  for (k = 0u; k < R->n; k++) n_held += R->held[k] && R->standing[k];
  for (i = 0u; i < b->n_neurons; i++) {
    unsigned a = of_area(&b->neuron[i]);
    if (a == OF_DLPFC && !b->neuron[i].inhibitory) {
      O->act[i] = slot < n_held ? 1.0f : 0.0f;
      if (++group == OF_GROUP) {
        group = 0u;
        slot++;
      }
    } else if (a == OF_ACC) {
      O->act[i] = n_held + struck_now > 0u ? (float)struck_now / (float)(n_held + struck_now) : 0.0f;
    }
  }
}
