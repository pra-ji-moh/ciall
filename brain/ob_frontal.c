/*
 * ob_frontal.c -- the frontal cortex's Hartley circuit. See ob_frontal.h.
 */

#include "ob_frontal.h"

#include <string.h>

/* the DLPFC's groups: self-excitation through myelinated axons (SELF times the
   frontal cortex's myelin), one shared pool of inhibition (SHARED for each
   lit group). A group stays fully lit while SELF * myelin - SHARED * (the
   others lit) reaches 1: with these, nothing holds before about nine months
   after birth (A-not-B, means-end: the first plans), a few at a year, dozens by
   three */
#define SELF 7.2f
#define SHARED 0.088f
#define LIT 0.5f                      /* lit enough to be in mind */
#define STRIKE 10.0f                  /* the ACC's inhibition: more than any group's own excitation (SELF), or a
                                         well myelinated group would shrug it off (it did, at first) */
#define CUE 8.0f                      /* a hypothesis brought to mind arrives strongly, from the cortex behind */

unsigned of_area(const ob_neuron_t *n) {
  /* front to back (sx), floor to roof (sy): the orbitofrontal cortex at the
     front and floor, the cingulate along the floor behind it, the eye fields at
     the back by the motor strip, the dorsolateral prefrontal cortex above */
  if (n->region != OB_FRONTAL) return OF_OTHER;
  if (n->sx > 0.8f) return OF_FEF;
  if (n->sy < 0.3f) return n->sx < 0.35f ? OF_OFC : OF_ACC;
  return OF_DLPFC;
}

static unsigned usable(const of_rules_t *R) {
  return R->no_frontal ? 0u : R->no_dlpfc ? (R->n_groups < 1u ? R->n_groups : 1u) : R->n_groups;
}

static float clip(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* the groups settle: each excites itself and is inhibited by the others' and
   the ACC; `cue` drives one group (-1 none) for the first few steps */
static void settle(float *lit, const float *acc, unsigned n, float myelin, int cue) {
  unsigned t, g;
  for (t = 0u; t < 40u; t++) {
    float total = 0.0f;
    for (g = 0u; g < n; g++) total += lit[g];
    for (g = 0u; g < n; g++) {
      float in = SELF * myelin * lit[g] - SHARED * (total - lit[g]) - (acc ? acc[g] : 0.0f);
      if ((int)g == cue && t < 5u) in += CUE;
      lit[g] += 0.5f * (clip(in) - lit[g]);
      if (lit[g] < 0.01f) lit[g] = 0.0f;
    }
  }
}

static void show(of_rules_t *R) {
  unsigned g, k;
  for (g = 0u; g < R->n_groups; g++)
    for (k = 0u; k < OF_GROUP; k++) R->O->act[R->neuron[g][k]] = R->lit[g];
}

/* bring one standing hypothesis to mind in a free group; 0 if it would not
   stay lit (no room) or none is left to think of */
static int bring_one(of_rules_t *R) {
  unsigned g, k, n = usable(R), free_g = n;
  float trial[OF_MAX_GROUPS];
  for (g = 0u; g < n && free_g == n; g++)
    if (R->holds[g] < 0) free_g = g;
  if (free_g == n) return 0;
  for (k = 0u; k < R->n; k++) {
    unsigned h = R->order[(R->next + k) % R->n], held = 0u;
    for (g = 0u; g < n; g++) held |= R->holds[g] == (int)h;
    if (!R->standing[h] || held) continue;
    memcpy(trial, R->lit, sizeof(float) * n);
    trial[free_g] = 0.0f;
    settle(trial, R->acc, n, R->O->myelin[OB_FRONTAL], (int)free_g);
    /* it would not stay, or it would put out one already held: no room */
    if (trial[free_g] < LIT) return 0;
    for (g = 0u; g < n; g++)
      if (g != free_g && R->lit[g] >= LIT && trial[g] < LIT) return 0;
    memcpy(R->lit, trial, sizeof(float) * n);
    R->holds[free_g] = (int)h;
    R->next = (R->next + k + 1u) % R->n;
    return 1;
  }
  return 0;
}

static void forget_dark(of_rules_t *R) {
  unsigned g;
  for (g = 0u; g < R->n_groups; g++)
    if (R->lit[g] < LIT) {
      R->lit[g] = 0.0f;
      R->holds[g] = -1;
    }
}

static void refill(of_rules_t *R) {
  while (bring_one(R)) {
  }
  forget_dark(R);
  show(R);
}

unsigned of_room(const of_rules_t *R) {
  float lit[OF_MAX_GROUPS], trial[OF_MAX_GROUPS];
  unsigned g, k, n = usable(R), room = 0u;
  memset(lit, 0, sizeof lit);
  for (g = 0u; g < n; g++) {
    unsigned now = 0u;
    memcpy(trial, lit, sizeof(float) * n);
    settle(trial, 0, n, R->O->myelin[OB_FRONTAL], (int)g);
    for (k = 0u; k < n; k++) now += trial[k] >= LIT;
    if (now <= room) break;            /* one more did not stay, or put another out */
    memcpy(lit, trial, sizeof(float) * n);
    room = now;
  }
  return room;
}

void of_begin(of_rules_t *R, ob_one_t *O, unsigned n, of_says_t says, unsigned seed, unsigned no_acc,
              unsigned no_dlpfc, unsigned no_frontal) {
  const ob_brain_t *b = &O->brain;
  unsigned i, k, in_group = 0u, rng = seed * 2654435761u + 7u;
  memset(R, 0, sizeof(*R));
  R->O = O;
  R->n = n > OF_MAX_HYP ? OF_MAX_HYP : n;
  R->says = says;
  R->no_acc = no_acc;
  R->no_dlpfc = no_dlpfc;
  R->no_frontal = no_frontal;
  for (i = 0u; i < b->n_neurons && R->n_groups < OF_MAX_GROUPS; i++) {
    if (b->neuron[i].inhibitory || of_area(&b->neuron[i]) != OF_DLPFC) continue;
    R->neuron[R->n_groups][in_group] = i;
    O->external[i] = 1u;
    if (++in_group == OF_GROUP) {
      in_group = 0u;
      R->holds[R->n_groups++] = -1;
    }
  }
  for (k = 0u; k < R->n_groups; k++) R->holds[k] = -1;
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

void of_end(of_rules_t *R) {
  const ob_brain_t *b = &R->O->brain;
  unsigned i;
  for (i = 0u; i < b->n_neurons; i++)
    if (of_area(&b->neuron[i]) == OF_DLPFC) R->O->external[i] = 0u;
}

unsigned of_in_mind(const of_rules_t *R) {
  unsigned g, n = 0u;
  for (g = 0u; g < R->n_groups; g++) n += R->holds[g] >= 0 && R->lit[g] >= LIT;
  return n;
}

of_test_t of_choose(of_rules_t *R, const of_test_t *tests, unsigned n_tests) {
  unsigned i, g, held = of_in_mind(R), best = 0u;
  int best_split = -1, best_yes = -1;
  float best_tried = -1.0f;
  for (i = 0u; i < n_tests; i++) {
    unsigned yes = 0u, split;
    float tried = R->tried_well[tests[i].acts & 63u];
    /* the IPS counts, over the groups lit */
    for (g = 0u; g < R->n_groups; g++)
      if (R->holds[g] >= 0 && R->lit[g] >= LIT && R->says((unsigned)R->holds[g], &tests[i])) yes++;
    /* the even split teaches most (if yes, the yes-sayers are left; if no, the
       no-sayers). Where all it holds agree, it tries what they say will happen
       (a positive test: if nothing happens, they all go). Only then does
       experience order */
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
  unsigned g, struck = 0u, standing = 0u, k, n = usable(R);
  R->tests++;
  R->tried_well[t->acts & 63u] += outcome ? 1.0f : 0.0f;
  /* the ACC: each lit group whose hypothesis said otherwise is inhibited */
  memset(R->acc, 0, sizeof R->acc);
  if (!R->no_acc)
    for (g = 0u; g < n; g++)
      if (R->holds[g] >= 0 && R->lit[g] >= LIT && R->says((unsigned)R->holds[g], t) != (outcome != 0))
        R->acc[g] = STRIKE;
  settle(R->lit, R->acc, n, R->O->myelin[OB_FRONTAL], -1);
  for (g = 0u; g < n; g++) {
    if (R->acc[g] > 0.0f && R->lit[g] < LIT) {
      R->standing[R->holds[g]] = 0u;   /* ruled out, and remembered so */
      struck++;
    }
  }
  memset(R->acc, 0, sizeof R->acc);
  forget_dark(R);
  R->struck += struck;
  for (k = 0u; k < R->n; k++) standing += R->standing[k];
  /* its mind full, nothing ruled out, and rivals it has not yet thought of:
     what it holds has passed this test, so the first of it lets go (it stays
     standing, to come back to mind in its turn) to make room */
  if (struck == 0u && of_in_mind(R) > 0u && standing > of_in_mind(R)) {
    int first = -1;
    unsigned best = R->n;
    for (g = 0u; g < n; g++) {
      if (R->holds[g] < 0) continue;
      for (k = 0u; k < R->n; k++)
        if (R->order[(R->next + k) % R->n] == (unsigned)R->holds[g] && k < best) {
          best = k;
          first = (int)g;
        }
    }
    if (first >= 0) {
      unsigned g0 = (unsigned)first;
      unsigned free_left = 0u;
      for (g = 0u; g < n; g++) free_left += R->holds[g] < 0;
      /* only if no room is left to bring a rival in beside it */
      if (free_left == 0u || !bring_one(R)) {
        R->acc[g0] = STRIKE;
        settle(R->lit, R->acc, n, R->O->myelin[OB_FRONTAL], -1);
        memset(R->acc, 0, sizeof R->acc);
        R->lit[g0] = 0.0f;
        R->holds[g0] = -1;
        settle(R->lit, 0, n, R->O->myelin[OB_FRONTAL], -1);
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
