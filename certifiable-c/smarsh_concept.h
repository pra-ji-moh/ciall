/*
 * smarsh_concept.h -- concepts are discovered, not handed over.
 *
 * ============================================================
 * THE IDEA
 * ============================================================
 * A child is not given "+". They put two piles together and count, many
 * times, and at some point it becomes clear that the count of the merged
 * pile depends on nothing but the two counts they started with. That
 * dependence IS addition. It was found, and what it was found in is
 * grouping physical things.
 *
 * The kernel can already decide that exactly. sr_project succeeds when a
 * question is constant on every cell of a partition: when it is fully
 * determined by the questions that built the partition. So:
 *
 *   discovery   a target question (an observation, such as "count the
 *               merged pile") turns out to be DETERMINED by other questions
 *               (the counts of the two piles). The table read off the cells
 *               is the new concept: an operator, found, not given.
 *
 *   proof       the discovery carries its own check. For every situation
 *               ever seen, the operator applied to the arguments gives the
 *               target. sc_check re-derives that independently.
 *
 *   refusal     when the target is NOT determined, there are two
 *               situations that agree on every candidate and disagree on
 *               the target. That pair is returned: a proof that no
 *               combination of these questions could ever define it.
 *
 *   grounding   the operator is known only on the argument combinations
 *               actually witnessed. Applying it anywhere else is not a
 *               guess, it is refused: the concept does not reach there
 *               yet. Knowing 3 + 4 from piles says nothing yet about
 *               40 + 70. Reaching further needs a general law, which is
 *               worlds described by rules, a later stage.
 *
 * ============================================================
 * THE ONE POLICY
 * ============================================================
 * When several combinations of candidates would determine the target,
 * take the smallest: one argument before two, then the lowest indices.
 * Explain with as little as works. Like the learner's "acquire the least
 * refinement", this is a named preference, not something derived.
 *
 * Nothing here is learned statistically. One complete set of witnessed
 * situations is enough to discover and prove a concept, and no amount of
 * repetition changes it.
 */

#ifndef SMARSH_CONCEPT_H
#define SMARSH_CONCEPT_H

#include "smarsh_reason.h"

#define SC_NAME_LEN 24u
#define SC_GROUND_WORDS (SR_MAX_TABLE / 64u)

/* A discovered operator: one or two arguments, as a table, plus exactly
   which argument combinations it has ever been witnessed on. */
typedef struct {
  char name[SC_NAME_LEN];
  unsigned arity;                 /* 1 or 2 */
  unsigned dom_a;
  unsigned dom_b;                 /* 1 when arity is 1 */
  unsigned dom_out;
  uint8_t table[SR_MAX_TABLE];    /* out[a * dom_b + b] */
  uint64_t grounded[SC_GROUND_WORDS];
  unsigned n_grounded;            /* combinations witnessed */
  unsigned witnessed_in;          /* situations the discovery was checked on */
} sc_op_t;

typedef struct {
  int found;                      /* 1: the target is determined */
  unsigned arity;
  unsigned arg[2];                /* indices into the candidate list */
  sc_op_t op;
  /* When not found: two situations that agree on EVERY candidate and
     disagree on the target. A proof that no combination of these
     candidates, of any size, determines it. Only when even all of them
     together fail; see needs_more. */
  unsigned counter_s;
  unsigned counter_t;
  /* When not found but all candidates TOGETHER would determine it: it
     needs more than two at once. Not a refusal of the target, a limit of
     this search, stated. */
  int needs_more;
} sc_discovery_t;

/*
 * Look for the smallest combination of candidates that determines the
 * target, and read the operator off it. Candidates and target are
 * questions over the same n_situations situations.
 */
sm_status_t sc_discover(const sr_query_t *cands, unsigned n_cands,
                        const sr_query_t *target, unsigned n_situations,
                        sc_discovery_t *out);

/*
 * The proof, re-derived independently of the discovery: for every
 * situation, the combination is grounded and op(args) equals the target.
 * b is ignored for arity 1. Returns 1 or 0.
 */
int sc_check(const sc_op_t *op, const sr_query_t *a, const sr_query_t *b,
             const sr_query_t *target, unsigned n_situations);

/*
 * Use a concept in a new world: the derived question op(a, b), situation
 * by situation. If any situation needs a combination the concept was never
 * witnessed on, the result is a GROUNDLESS question (the concept does not
 * reach this world) and *n_ungrounded says how many situations fell
 * outside. Not an extrapolation, and not an error: a refusal with a count.
 */
sm_status_t sc_apply(const sc_op_t *op, const sr_query_t *a, const sr_query_t *b,
                     unsigned n_situations, sr_query_t *out,
                     unsigned *n_ungrounded);

/* Is op(a, b) defined here? 1 if the combination was witnessed. */
int sc_grounded(const sc_op_t *op, unsigned a, unsigned b);
unsigned sc_value(const sc_op_t *op, unsigned a, unsigned b);

/*
 * Are two concepts the same concept, on every combination both were
 * witnessed on? `swapped` compares p(a, b) with q(b, a). Returns 1 when
 * they agree everywhere both are grounded AND share at least one grounded
 * combination; *first_diff_a/b name a disagreement when they do not.
 */
int sc_same(const sc_op_t *p, const sc_op_t *q, int swapped,
            unsigned *first_diff_a, unsigned *first_diff_b);

/*
 * Properties, proved over the grounded region only, and scoped as such in
 * every message that uses them.
 *   commutative   op(a, b) == op(b, a) wherever both are grounded
 *   identity      some e with op(a, e) == a and op(e, a) == a wherever
 *                 grounded; *e receives it
 */
int sc_commutative(const sc_op_t *op);
int sc_identity(const sc_op_t *op, unsigned *e);

#endif /* SMARSH_CONCEPT_H */
