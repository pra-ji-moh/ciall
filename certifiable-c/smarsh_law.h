/*
 * smarsh_law.h -- laws: rules that reach past what was seen, with every
 * leap labeled.
 *
 * ============================================================
 * WHY TABLES ARE NOT ENOUGH
 * ============================================================
 * smarsh_concept.c discovers addition from piles, as a table: known
 * exactly where it was witnessed and nowhere else. A child does not stop
 * at the biggest pile they have seen. They notice a LAW: put one more in
 * the second pile, and the total goes up by one. That law, with where
 * counting starts, fixes addition for every number, including ones never
 * seen.
 *
 * ============================================================
 * WHAT A LAW IS HERE
 * ============================================================
 * The oldest form of mathematical law, recursion:
 *
 *     op(a, 0)     = a            or 0
 *     op(a, b + 1) = step(op(a, b))
 *
 * where step is "one more", or an earlier law applied with a (as in
 * repeat(a, b + 1) = combine(repeat(a, b), a)) or with the count so far
 * (stairs(n + 1) = combine(stairs(n), n + 1): the new step has n + 1
 * blocks). A law may
 * only use laws found before it, so there are no cycles, and evaluating
 * one is plain counting, however large the numbers.
 *
 * ============================================================
 * DISCOVERY, AND WHAT IT PROVES
 * ============================================================
 * A law is found by trying each form against the table: every witnessed
 * pair (a, b) and (a, b + 1) must obey it. What that proves is exactly
 * that: the law agrees with everything seen. Some checks can only be made
 * by using an earlier law outside ITS witnessed range; those are counted
 * and their leaps recorded, not hidden.
 *
 * ============================================================
 * THE LEAP, LABELED
 * ============================================================
 * Using a law where nothing was witnessed is induction: a real leap, not
 * a proof, and the most important thing here is that it is never
 * mistaken for one. Every value computed carries the set of laws it
 * relied on outside their witnessed range. An empty set means the value
 * was seen. "40 + 70 = 110, resting on: one more is always one more;
 * combining works the same past 15" is the whole truth about that number,
 * and it is what gets reported.
 *
 * What is ASSUMED, stated: numbers are a counting sequence without end.
 * That is the representation, like a child being taught to keep counting.
 * Everything built on it is found.
 */

#ifndef SMARSH_LAW_H
#define SMARSH_LAW_H

#include "smarsh_concept.h"

#define LW_MAX_LAWS 16u
#define LW_MAX_STEPS 1000000UL   /* past this, a computation is refused */

typedef enum {
  LW_ONE_MORE = 0,    /* the primitive: count after adding one thing */
  LW_RECURSIVE = 1
} lw_kind_t;

typedef enum { LW_BASE_A = 0, LW_BASE_ZERO = 1 } lw_base_t;

typedef enum {
  LW_STEP_ONE_MORE = 0,   /* op(a, b+1) = one_more(op(a, b)) */
  LW_STEP_WITH_A = 1,     /* op(a, b+1) = law(op(a, b), a) */
  LW_STEP_WITH_COUNT = 2, /* op(a, b+1) = law(op(a, b), b) */
  LW_STEP_WITH_NEXT = 3   /* op(a, b+1) = law(op(a, b), b + 1) */
} lw_step_t;

typedef struct {
  char name[SC_NAME_LEN];
  char said[64];               /* the law in words, for reporting its leaps */
  lw_kind_t kind;
  unsigned arity;              /* 2: op(a, b); 1: f(n), recursion on n */
  lw_base_t base;
  lw_step_t step;
  unsigned step_law;           /* an earlier law, for WITH_A / WITH_COUNT */
  const sc_op_t *table;        /* where it was witnessed (not owned) */
  uint64_t one_more_seen_below;/* LW_ONE_MORE: witnessed for n below this */
  unsigned checked;            /* witnessed cases the law was checked on */
  uint32_t check_leaps;        /* leaps some of those checks needed */
} lw_law_t;

typedef struct {
  unsigned n;
  unsigned one_more;           /* index of the primitive */
  lw_law_t law[LW_MAX_LAWS];
} lw_book_t;

/* The primitive, from a world where one thing is added to piles of every
   size below `seen_below`, and the count afterwards observed. */
sm_status_t lw_init(lw_book_t *book, const sc_op_t *one_more_table, uint64_t seen_below,
                    const char *said);

/*
 * Find the law behind a discovered table: try every base and every step
 * form built from laws already in the book, and keep the first that
 * agrees with every witnessed case. *idx receives the new law's index;
 * SM_ERR_EMPTY_DOMAIN when no form fits, which is reported, not forced.
 */
sm_status_t lw_discover(lw_book_t *book, const sc_op_t *table, const char *name,
                        const char *said, unsigned *idx);

/*
 * Evaluate law idx at (a, b); for a law of one argument, a is the argument
 * and b is ignored. *leaps receives the laws used outside their
 * witnessed range: bit i set means law i was taken on trust there.
 * SM_ERR_EMPTY_DOMAIN when the value does not exist; SM_ERR_DOMAIN_TOO_LARGE
 * when it would take more than LW_MAX_STEPS steps.
 */
sm_status_t lw_eval(const lw_book_t *book, unsigned idx, uint64_t a, uint64_t b,
                    uint64_t *value, uint32_t *leaps);

#endif /* SMARSH_LAW_H */
