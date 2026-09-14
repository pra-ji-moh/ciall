/*
 * smarsh_interval.h -- intervals that never lie: the arithmetic under
 * worlds described by constraints.
 *
 * A real number cannot be listed, so a set of worlds over the reals cannot
 * be a list. What can be held exactly is a guarantee: "the value is
 * somewhere in [lo, hi]". Every operation here takes guarantees and
 * returns a guarantee. That is the whole contract, and everything built on
 * it inherits it: if the true value could be outside the interval, the
 * code is wrong.
 *
 * ============================================================
 * HOW THE GUARANTEE IS KEPT
 * ============================================================
 * Floating point rounds. Each result is computed with ordinary rounding
 * and then moved outward, so the true result is always inside:
 *
 *   + - * /, sqrt   IEEE correctly rounded to within half a unit in the
 *                   last place; widened by one unit each way.
 *   exp log sin cos not guaranteed correctly rounded by the C library;
 *                   widened by IV_LIBM_SLACK units each way. That slack is
 *                   an assumption about the library, stated here.
 *
 * EXACT intervals skip the widening: when every input is an exact whole
 * number and the operation is +, -, *, negation, min, max, abs, mod or a
 * whole-number power, and the result stays below 2^53, doubles compute it
 * exactly. That is what lets whole-number questions (does 12 * 7 equal
 * 84?) be decided rather than merely bounded.
 *
 * ============================================================
 * TRUTH VALUES
 * ============================================================
 * A yes/no value is an interval inside [0, 1]: [1, 1] certainly true,
 * [0, 0] certainly false, [0, 1] not decided on this region. So "I do not
 * know yet" is carried by the same arithmetic as everything else.
 *
 * EMPTY (lo > hi) means no value: the expression is undefined everywhere
 * on the region (the square root of numbers that are all negative).
 */

#ifndef SMARSH_INTERVAL_H
#define SMARSH_INTERVAL_H

#define IV_LIBM_SLACK 4

typedef struct {
  double lo;
  double hi;
  int exact;       /* both ends exact whole numbers, computed without rounding */
} iv_t;

iv_t iv_make(double lo, double hi);        /* rounded input: not exact */
iv_t iv_int(double lo, double hi);         /* exact whole-number interval */
iv_t iv_point(double x);                   /* exact if x is a whole number */
iv_t iv_empty(void);
iv_t iv_entire(void);
int iv_is_empty(iv_t a);
double iv_width(iv_t a);
double iv_mid(iv_t a);
iv_t iv_meet(iv_t a, iv_t b);              /* intersection */
iv_t iv_hull(iv_t a, iv_t b);              /* smallest interval holding both */
int iv_subset(iv_t a, iv_t b);

iv_t iv_add(iv_t a, iv_t b);
iv_t iv_sub(iv_t a, iv_t b);
iv_t iv_mul(iv_t a, iv_t b);
iv_t iv_div(iv_t a, iv_t b);
iv_t iv_neg(iv_t a);
iv_t iv_abs(iv_t a);
iv_t iv_min(iv_t a, iv_t b);
iv_t iv_max(iv_t a, iv_t b);
iv_t iv_powi(iv_t a, int n);               /* n >= 0 */
iv_t iv_sqrt(iv_t a);
iv_t iv_exp(iv_t a);
iv_t iv_log(iv_t a);
iv_t iv_sin(iv_t a);
iv_t iv_cos(iv_t a);
iv_t iv_mod(iv_t a, iv_t m);               /* whole numbers, m a positive constant */

/* truth values */
iv_t iv_lt(iv_t a, iv_t b);
iv_t iv_le(iv_t a, iv_t b);
iv_t iv_eq(iv_t a, iv_t b);
iv_t iv_and(iv_t a, iv_t b);
iv_t iv_or(iv_t a, iv_t b);
iv_t iv_not(iv_t a);
int iv_true(iv_t a);                       /* certainly true */
int iv_false(iv_t a);                      /* certainly false */

/* round a range inward to the whole numbers inside it */
iv_t iv_to_int(iv_t a);

#endif /* SMARSH_INTERVAL_H */
