/*
 * ob_frontal.h -- the frontal cortex's Hartley circuit: evidence decides,
 * experience guides.
 *
 * Its frontal lobe is patterned into areas by position, as the cortex is into
 * lobes (front to back, floor to roof): the orbitofrontal cortex at the front
 * and floor, the anterior cingulate along the floor behind it, the frontal eye
 * fields at the back (by the motor strip), and the dorsolateral prefrontal
 * cortex (DLPFC) over the rest. Each does one part of reasoning:
 *
 *   DLPFC   holds in mind the hypotheses still standing: a set, never one
 *           best guess (Hartley 1928: what it does not know is log2 of what
 *           is left). Each is held by its own group of DLPFC neurons, firing
 *           while it stands; how many it can hold at once is how many groups
 *           its DLPFC has, times how far its myelination has come (the
 *           prefrontal cortex myelinates last, into adulthood)
 *   IPS     counts: for each test it could make, how many of the hypotheses
 *           held would say yes and how many no. The test that splits them
 *           most evenly is the one that teaches most
 *   OFC     experience: where tests split equally, what it has tried and seen
 *           work goes first. It orders; it never decides
 *   FEF     carries the test out: its eyes to the place, then the acts, its
 *           restless impulses held back
 *   ACC     the outcome against each hypothesis held: whatever predicted
 *           otherwise is struck out, for good. A hypothesis not held in mind
 *           cannot be struck: it is not being thought of
 *
 * Switches (for lesions): without the ACC nothing is struck out; without the
 * DLPFC it can hold one hypothesis at a time.
 */

#ifndef OB_FRONTAL_H
#define OB_FRONTAL_H

#include "ob_one.h"

#define OF_MAX_HYP 512
#define OF_GROUP 6                    /* DLPFC neurons that hold one hypothesis */

enum { OF_OTHER, OF_DLPFC, OF_ACC, OF_OFC, OF_FEF, OF_AREAS };

/* a test: do these acts (bits, as ob_one's), with its eyes on this place */
typedef struct {
  unsigned acts, place;
} of_test_t;

/* what hypothesis h says a test will bring (1 or 0) */
typedef int (*of_says_t)(unsigned h, const of_test_t *t);

typedef struct {
  unsigned n;                         /* hypotheses in all */
  of_says_t says;
  unsigned char standing[OF_MAX_HYP]; /* not yet struck out */
  unsigned char held[OF_MAX_HYP];     /* in mind now */
  unsigned capacity;                  /* how many it can hold at once */
  unsigned order[OF_MAX_HYP];         /* the order they come to mind in */
  unsigned next;                      /* where in that order the next comes from */
  unsigned tests, struck;
  unsigned no_acc, no_dlpfc;          /* lesions */
  float tried_well[64];               /* OFC: how often each set of acts has brought something */
} of_rules_t;

/* which frontal area a frontal neuron is in (OF_OTHER for any other neuron) */
unsigned of_area(const ob_neuron_t *n);

/* how many hypotheses its DLPFC can hold at `months` after birth: its groups,
   times its myelination then */
unsigned of_capacity(const ob_brain_t *b, double months);

/* begin a question: n hypotheses, what each says, how many it can hold */
void of_begin(of_rules_t *R, unsigned n, of_says_t says, unsigned capacity, unsigned seed);

/* how many still stand, of those it has in mind (its uncertainty is log2 of it) */
unsigned of_in_mind(const of_rules_t *R);

/* the test (of `n_tests` it could make) that splits what it holds most evenly */
of_test_t of_choose(of_rules_t *R, const of_test_t *tests, unsigned n_tests);

/* the outcome: strike out what it contradicts; bring more to mind if room.
   Returns how many it struck */
unsigned of_learn(of_rules_t *R, const of_test_t *t, int outcome);

/* the one hypothesis left standing in all (1), or not yet known (0) */
int of_known(const of_rules_t *R, unsigned *which);

/* show it on its frontal neurons: each DLPFC group firing while its hypothesis
   is held, its ACC firing as it strikes */
void of_show(const of_rules_t *R, ob_one_t *O, unsigned struck_now);

#endif
