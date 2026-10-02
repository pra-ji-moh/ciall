/*
 * smarsh_general.h -- the core with no domain in it.
 *
 * A hidden rule governs some system. Tests can be run on it, each at a cost, and
 * each shows an exact outcome. A decision has to be reached that can be proved. This
 * is everything needed for that, said without one word about what the system is:
 *
 *   survivors     every hypothesis no evidence has contradicted yet, held in the
 *                 base's own possibility sets (smarsh_core.h); nothing here replaces
 *                 the base, it only arranges many of its domains side by side
 *   eliminate     an observation rules out each hypothesis that says otherwise.
 *                 Exact: no weight, no likelihood. Ruled out, or not
 *   ledger        how much is still unknown. Exactly: log2 of the survivors. And,
 *                 where the hypothesis is made of rows, the sum of log2 of what
 *                 each row may still be: a ceiling, equal only when the rows do
 *                 not constrain one another. The difference is how much they do
 *   probe         the test whose worst answer leaves the fewest survivors. A test
 *                 that could not remove anything is never chosen: no act is wasted
 *   certificate   stop testing when one decision is the best one under EVERY
 *                 survivor (the gap is 0). That is the proof: whatever the truth
 *                 is among what is left, the decision is right
 *   conflict      when nothing survives, the smallest set of observations that
 *                 cannot all be true together. Not "something is wrong": which
 *   carry         when the system has changed, what is nearest the old model
 *                 that the evidence since still allows. The survivors stay exact:
 *                 nearness orders where to look, it removes nothing
 *   trace         every step chained by hash, so a conclusion can be replayed and
 *                 checked byte for byte
 *
 * What the system IS comes in from outside, as an adapter: how many hypotheses
 * there are, in a fixed order; what each says a test would show; what tests and
 * decisions cost. The core calls nothing else and knows nothing else.
 */
#ifndef SMARSH_GENERAL_H
#define SMARSH_GENERAL_H

#include <stddef.h>
#include <stdint.h>

#include "smarsh_core.h"

#define SG_MAX_HYP 65536u
#define SG_PARTS (SG_MAX_HYP / SM_MAX_DOMAIN)
#define SG_MAX_OBS 4096u
#define SG_MAX_ROWS 16u

typedef struct {
  void *ctx;
  unsigned n_hyp;                      /* hypotheses 0..n_hyp-1, in an order that never changes */
  unsigned n_tests;
  unsigned n_decisions;
  /* optional: a hypothesis as rows, n_hyp == the product of row_size[]; 0 rows: not said */
  unsigned n_rows;
  unsigned row_size[SG_MAX_ROWS];
  /* exactly what test `test` would show if hypothesis h were the truth */
  int (*predict)(void *ctx, unsigned h, unsigned test);
  /* what running a test costs; null: every test costs 1 */
  unsigned (*test_cost)(void *ctx, unsigned test);
  /* what decision d costs if hypothesis h is the truth */
  unsigned (*decision_cost)(void *ctx, unsigned h, unsigned d);
  /* how far apart two hypotheses are; null: by their place in the order */
  unsigned (*distance)(void *ctx, unsigned h1, unsigned h2);
} sg_adapter_t;

typedef struct {
  const sg_adapter_t *a;
  sm_possibility_t part[SG_PARTS];     /* the survivors: the base's domains, side by side */
  unsigned n_parts;
  unsigned obs_test[SG_MAX_OBS];
  int obs_out[SG_MAX_OBS];
  unsigned n_obs;
  unsigned long spent;                 /* cost of the tests run */
  unsigned char head[32];              /* the head of the hash chain */
} sg_core_t;

sm_status_t sg_begin(sg_core_t *c, const sg_adapter_t *a);

int sg_possible(const sg_core_t *c, unsigned h);
unsigned sg_count(const sg_core_t *c);
double sg_bits(const sg_core_t *c);      /* exactly: log2 of the survivors */
double sg_ledger(const sg_core_t *c);    /* the ceiling by rows; the same as sg_bits when no rows are said */

/* an observation: test `test` showed `outcome`. Returns how many hypotheses it ruled out. */
unsigned sg_observe(sg_core_t *c, unsigned test, int outcome);

/* the test whose worst answer leaves the fewest survivors; 0 if no test can remove anything */
int sg_probe(const sg_core_t *c, unsigned *test, unsigned *worst_left);

/* the decision with the smallest worst-case regret, and that regret. Returns 1 when it is 0:
   the decision is the best one under every survivor, and testing may stop */
int sg_certificate(const sg_core_t *c, unsigned *decision, unsigned *gap);

/* when nothing survives: the smallest set of observations that cannot coexist, as indices
   into the observations made, in order. Returns how many; 0 if they can all be true */
unsigned sg_conflict(const sg_core_t *c, unsigned *which, unsigned cap);

/* after a change: the survivors of the evidence from observation `since` on (exact), the
   one nearest `old`, how far it is, and how many hypotheses lie within that distance */
unsigned sg_carry(sg_core_t *c, unsigned old, unsigned since, unsigned *nearest, unsigned *dist,
                  unsigned *ball);

/* the head of the chain, as 64 hex digits */
void sg_head(const sg_core_t *c, char out[65]);

/* run the same observations again from nothing; 1 if the chain comes out the same */
int sg_replay(const sg_adapter_t *a, const unsigned *tests, const int *outs, unsigned n,
              const unsigned char head[32]);

#endif
