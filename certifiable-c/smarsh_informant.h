/*
 * smarsh_informant.h -- knowledge from outside, taken as claims, never as
 * reasoning.
 *
 * A child learns most of what they know from being told. What keeps that
 * from being mere imitation is that what they are told meets what they
 * see, and a source that says things that turn out false stops being
 * believed. This is that, and nothing more.
 *
 * ============================================================
 * THE RULES
 * ============================================================
 *   A claim is a structure (smarsh_analogy.h: a question and the table of
 *   answers over its factors) plus WHO said it.
 *
 *   An observation is one row of truth: these factor values, this answer,
 *   seen directly.
 *
 *   An observation that agrees with a claim's row CONFIRMS that row.
 *   One that disagrees CONTRADICTS the claim, and its source is
 *   distrusted: every claim from that source is set aside, not only the
 *   one that was caught. A source that was wrong once about what could be
 *   checked gives no grounds for what could not.
 *
 *   Two trusted sources that disagree about a row, with no observation of
 *   that row, leave it DISPUTED. It is not settled by how many sources say
 *   each thing: agreement between sources is not evidence that either is
 *   right, and counting heads would make this a popularity contest.
 *
 *   What the store can offer for reasoning is, row by row: confirmed (seen),
 *   told (resting on named sources that nothing has contradicted), disputed,
 *   or unknown. Anything built on a told row says whose word it rests on.
 *
 * The informants can be anything: a book, a person, a sensor, another AI.
 * None of them reasons here. They supply rows; the kernel does the rest.
 */

#ifndef SMARSH_INFORMANT_H
#define SMARSH_INFORMANT_H

#include "smarsh_analogy.h"

#define SK_MAX_SOURCES 16u
#define SK_MAX_CLAIMS 64u

typedef enum {
  SK_UNKNOWN = 0,    /* nobody trusted has said, nothing seen */
  SK_TOLD = 1,       /* said by trusted sources, all agreeing, not yet seen */
  SK_CONFIRMED = 2,  /* seen */
  SK_DISPUTED = 3    /* trusted sources disagree and nothing seen settles it */
} sk_standing_t;

typedef struct {
  unsigned source;
  sa_struct_t claim;
} sk_claim_t;

typedef struct {
  unsigned n_sources;
  char source_name[SK_MAX_SOURCES][SA_NAME];
  int distrusted[SK_MAX_SOURCES];
  unsigned caught_on[SK_MAX_SOURCES];     /* index of the claim that was caught */

  unsigned n_claims;
  sk_claim_t claims[SK_MAX_CLAIMS];

  /* rows seen, per claim name: claim index -> which rows were observed,
     and the observed answer */
  uint8_t seen[SK_MAX_CLAIMS][SA_MAX_ENTRIES];      /* 0 unseen, 1 + answer */
} sk_store_t;

void sk_init(sk_store_t *k);
sm_status_t sk_source(sk_store_t *k, const char *name, unsigned *id);

/* Record a claim from a source. Claims about the same question (same
   name, same factors) are compared row by row. */
sm_status_t sk_tell(sk_store_t *k, unsigned source, const sa_struct_t *claim);

/*
 * An observation of the question `name`: factor values and the answer
 * seen. Every claim about that question is checked against it; any that
 * disagrees gets its source distrusted. *caught receives how many sources
 * were newly distrusted by this observation.
 */
sm_status_t sk_observe(sk_store_t *k, const char *name, const unsigned *values,
                       unsigned answer, unsigned *caught);

/*
 * Where the store stands on one row of one question, and on whose word:
 * *answer is the answer when confirmed or told; *sources is a bitmask of
 * the trusted sources that said it (0 when confirmed purely by sight).
 */
sk_standing_t sk_row(const sk_store_t *k, const char *name, const unsigned *values,
                     unsigned *answer, uint32_t *sources);

/*
 * The best structure the store can offer for `name`: *out gets a table
 * with every row confirmed or told; returns SM_ERR_EMPTY_DOMAIN if any row
 * is disputed or unknown (then *n_open says how many), since a structure
 * with holes cannot be matched exactly. *sources is every source the
 * offered table rests on; 0 means everything in it was seen.
 */
sm_status_t sk_best(const sk_store_t *k, const char *name, sa_struct_t *out,
                    uint32_t *sources, unsigned *n_open);

#endif /* SMARSH_INFORMANT_H */
