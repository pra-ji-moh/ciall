/*
 * native_reasoner.h -- the first-generation reasoner, in C.
 *
 * A port of reasoner.py, node_reasoner.py, rank_domain.py and compose.py.
 * This is the EARLIER form of the idea: a transitive-closure World over
 * lt(a, b) facts, the per-node D(n)/S(n)/H(n) maths, the rank domain that
 * gave S real values, and compose_and. It is superseded by the world-set
 * kernel in ../certifiable-c/smarsh_reason.h, and kept, in C, as the
 * record of how that kernel was arrived at.
 *
 * compose_and in particular is superseded for a stated reason: it takes
 * two RESULTS, so its inputs are the two answer sets, and it cannot tell
 * "a and b" from "a and not a". See ../native/NO-CHAIN.md.
 *
 * Entities are ids below NR_MAX_ENTITIES, so every set here is a single
 * 64-bit mask.
 */

#ifndef NATIVE_REASONER_H
#define NATIVE_REASONER_H

#include <stdint.h>

#include "pyrand.h"

#define NR_MAX_ENTITIES 64u
#define NR_MAX_PATH 64u
#define NR_MAX_ANCESTRY 64u

/* ---- the World: facts and their closure ---------------------------- */

typedef struct {
  uint64_t known;                      /* entities any fact may mention */
  uint64_t adj[NR_MAX_ENTITIES];       /* direct edges a -> b */
  uint64_t reach[NR_MAX_ENTITIES];     /* transitive closure */
  unsigned n_edges;
} nr_world_t;

void nr_world_init(nr_world_t *w, uint64_t known);
/* Returns 1 if the edge was new, 0 if it was already present -- the
   Python version kept edges in a set, so duplicates are ignored. */
int nr_world_add_edge(nr_world_t *w, unsigned a, unsigned b);
void nr_world_close(nr_world_t *w);
int nr_known(const nr_world_t *w, unsigned e);
unsigned nr_count_known(const nr_world_t *w);

/* A shortest derivation a -> b by breadth-first search, neighbours in
   ascending order. Returns its length; 0 if there is none. */
unsigned nr_path(const nr_world_t *w, unsigned a, unsigned b,
                 unsigned *from, unsigned *to);

/* reasoner.py's four-way answer */
typedef enum { NR_TRUE = 0, NR_FALSE = 1, NR_UNDET = 2, NR_GROUNDLESS_Q = 3 } nr_answer_t;
nr_answer_t nr_ask(const nr_world_t *w, unsigned a, unsigned b);

/* build(): a random DAG, reproducing reasoner.py's seeded construction */
void nr_build(nr_world_t *w, unsigned n_entities, unsigned n_edges, uint64_t seed,
              unsigned *unknown, unsigned *n_unknown);

/* ---- node results (node_reasoner.py) ------------------------------- */

typedef enum {
  NR_DERIVED = 0,
  NR_CONTRADICTION = 1,
  NR_GROUNDLESS = 2,
  NR_UNDETERMINED = 3,
  NR_SPECULATED = 4
} nr_verdict_t;

typedef struct {
  nr_verdict_t verdict;
  int has_value;
  int value;             /* 0/1 for boolean queries, a position for rank */
  int is_bool;           /* boolean domain {False, True} vs rank positions */
  uint64_t D;            /* bitset over domain values */
  int has_dom;
  unsigned dom;          /* |Dom| */
  int has_S;
  double S;
  double H;              /* +inf for contradiction */
  unsigned witness_len;
  unsigned wfrom[NR_MAX_PATH], wto[NR_MAX_PATH];
  /* WHICH guesses this rests on, as (id, H) pairs -- a set, not a sum */
  unsigned n_anc;
  long anc_id[NR_MAX_ANCESTRY];
  double anc_H[NR_MAX_ANCESTRY];
} nr_result_t;

double nr_ancestry_H(const nr_result_t *r);

void nr_node_query(const nr_world_t *w, unsigned a, unsigned b, nr_result_t *out);

/* The one place a point value is produced without being forced. Refuses
   below tau; otherwise picks uniformly from D (sorted by the value's
   string, as the Python did) and records (guess_id, H) in the ancestry. */
void nr_speculate(const nr_result_t *in, double tau, pyrand_t *rng, long guess_id,
                  nr_result_t *out);

/* ---- rank domain (rank_domain.py) ---------------------------------- */

void nr_rank_query(const nr_world_t *w, unsigned e, nr_result_t *out);
/* Ground truth: every linear extension of the known entities, enumerated.
   Exponential; for checking the formula on small worlds only. */
uint64_t nr_brute_force_positions(const nr_world_t *w, unsigned e);

/* ---- composition (compose.py, superseded) -------------------------- */

void nr_compose_and(const nr_result_t *r1, const nr_result_t *r2, nr_result_t *out);

#endif /* NATIVE_REASONER_H */
