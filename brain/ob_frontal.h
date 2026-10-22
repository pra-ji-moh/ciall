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
 *           is left). A hypothesis is held by a group of its DLPFC neurons
 *           that keeps itself firing: each group excites itself, through
 *           axons as strong as their myelination; all groups share one pool
 *           of inhibition. A group stays lit only while its own excitation
 *           outweighs the inhibition of all the others, so how many it can
 *           hold is not set anywhere: it is what that balance allows, and it
 *           grows as the frontal cortex myelinates. Before then nothing holds:
 *           it cannot reason yet
 *   IPS     counts: for each test it could make, how many of the hypotheses
 *           lit would say yes and how many no. The test that splits them
 *           most evenly is the one that teaches most
 *   OFC     experience: where tests split equally, what it has tried and seen
 *           work goes first. It orders; it never decides
 *   FEF     carries the test out: its eyes to the place, then the acts, its
 *           restless impulses held back
 *   ACC     the outcome against each hypothesis lit: whatever predicted
 *           otherwise, its ACC inhibits until its group goes dark, and it is
 *           ruled out. A hypothesis not lit cannot be: it is not being
 *           thought of
 *
 * Its long memory of what has been ruled out (so that a hypothesis struck out
 * does not come back to mind) is kept as a list here, the hippocampus's part,
 * not yet in neurons.
 *
 * Lesions: without the ACC nothing goes dark; without the DLPFC one group is
 * left (one hypothesis at a time); `no_frontal` none (it cannot reason at all).
 */

#ifndef OB_FRONTAL_H
#define OB_FRONTAL_H

#include "ob_one.h"

#define OF_MAX_HYP 512
#define OF_GROUP 6                    /* DLPFC neurons in a group */
#define OF_MAX_GROUPS 256

enum { OF_OTHER, OF_DLPFC, OF_ACC, OF_OFC, OF_FEF, OF_AREAS };

/* a test: do these acts (bits, as ob_one's), with its eyes on this place */
typedef struct {
  unsigned acts, place;
} of_test_t;

/* what hypothesis h says a test will bring (1 or 0) */
typedef int (*of_says_t)(unsigned h, const of_test_t *t);

typedef struct {
  ob_one_t *O;
  unsigned n;                         /* hypotheses in all */
  of_says_t says;
  unsigned char standing[OF_MAX_HYP]; /* not yet ruled out (the long memory of it) */
  unsigned order[OF_MAX_HYP];         /* the order they come to mind in */
  unsigned next;                      /* where in that order the next comes from */
  /* the DLPFC's groups: their neurons, how lit each is, what each holds (-1 nothing) */
  unsigned n_groups;
  unsigned neuron[OF_MAX_GROUPS][OF_GROUP];
  float lit[OF_MAX_GROUPS];
  int holds[OF_MAX_GROUPS];
  float acc[OF_MAX_GROUPS];           /* the ACC's inhibition on each, now */
  unsigned tests, struck;
  unsigned no_acc, no_dlpfc, no_frontal;   /* lesions */
  float tried_well[64];               /* OFC: how often each set of acts has brought something */
} of_rules_t;

/* which frontal area a frontal neuron is in (OF_OTHER for any other neuron) */
unsigned of_area(const ob_neuron_t *n);

/* how many hypotheses its DLPFC can hold now: loaded one after another until
   one more will not stay lit (measured on a copy of the groups) */
unsigned of_room(const of_rules_t *R);

/* begin a question: n hypotheses, what each says. Its DLPFC's groups are taken
   over from ob_one's step while it thinks, and brought to mind what will stay.
   The lesions: 1 to remove that part */
void of_begin(of_rules_t *R, ob_one_t *O, unsigned n, of_says_t says, unsigned seed, unsigned no_acc,
              unsigned no_dlpfc, unsigned no_frontal);
/* done thinking: its DLPFC goes back to ob_one's step */
void of_end(of_rules_t *R);

/* how many hypotheses are lit (its uncertainty is log2 of what stands) */
unsigned of_in_mind(const of_rules_t *R);

/* the test (of `n_tests` it could make) that splits what it holds most evenly */
of_test_t of_choose(of_rules_t *R, const of_test_t *tests, unsigned n_tests);

/* the outcome: the ACC darkens what it contradicts; more is brought to mind if
   it will stay. Returns how many were ruled out */
unsigned of_learn(of_rules_t *R, const of_test_t *t, int outcome);

/* the one hypothesis left standing in all (1), or not yet known (0) */
int of_known(const of_rules_t *R, unsigned *which);

#endif
