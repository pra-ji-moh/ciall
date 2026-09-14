/*
 * smarsh_proof.h -- proof by induction over the laws: from "true every
 * time it was checked" to "true for every number", where that can be
 * shown, and a counterexample where it cannot.
 *
 * ============================================================
 * WHAT IS PROVED, EXACTLY
 * ============================================================
 * smarsh_law.c finds laws such as
 *     stairs(0) = 0,  stairs(n + 1) = combine(stairs(n), n + 1)
 * and uses them past experience, labeling the leap. This file proves
 * things ABOUT THOSE LAWS for every number at once:
 *
 *   1. Each law has a closed form, a polynomial, and that is PROVED by
 *      induction: the formula matches at 0 (base case), and the formula at
 *      n + 1 minus the formula at n is exactly what one step adds
 *      (inductive step). Both are identities between polynomials, decided
 *      exactly by comparing coefficients. That certificate is checked
 *      separately from how the formula was derived.
 *
 *   2. A claim "for every x (and y): left = right", where both sides are
 *      built from laws, is proved by reducing both sides to one normal form
 *      and comparing. Equal: a theorem. Different: the difference is a
 *      nonzero polynomial, and a counterexample is found by evaluating it.
 *
 * What is NOT proved, and stays labeled: that the laws describe the world
 * past what was seen. "combining works the same past 15" is still a leap
 * about piles. Mathematics proves what follows from the laws; whether the
 * world obeys them is a different question, answered only by looking.
 *
 * ============================================================
 * THE ARITHMETIC
 * ============================================================
 * Polynomials in two variables, written in the binomial basis
 *     sum of c[i][j] * C(x, i) * C(y, j)
 * because summing is exact there (sum over k < n of C(k, j) is C(n, j + 1))
 * and every coefficient of a whole-number-valued polynomial is a whole
 * number, so all arithmetic is exact integer arithmetic, with overflow
 * checked. For display only, formulas are converted to ordinary powers.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * A law's closed form is found when each step ADDS something that does not
 * depend on the value so far (one more; combine with something). A step
 * that multiplies the value so far (as powers would) has no polynomial
 * closed form, and such a law is reported as outside this prover, not
 * guessed at.
 */

#ifndef SMARSH_PROOF_H
#define SMARSH_PROOF_H

#include "smarsh_law.h"

#define PF_DEG 8u
#define PF_MAX_NODES 32u

typedef struct {
  int64_t c[PF_DEG + 1u][PF_DEG + 1u];   /* coefficient of C(x, i) * C(y, j) */
} pf_poly_t;

typedef struct {
  unsigned n;
  int proved[LW_MAX_LAWS];               /* closed form proved by induction */
  pf_poly_t poly[LW_MAX_LAWS];           /* two-argument laws: x = a, y = b;
                                            one-argument laws: x = argument */
  char why[LW_MAX_LAWS][64];             /* when not proved, why not */
} pf_book_t;

/* An expression over x and y built from laws: nodes, root last. */
typedef enum { PF_X = 0, PF_Y = 1, PF_CONST = 2, PF_APPLY = 3 } pf_kind_t;

typedef struct {
  pf_kind_t kind;
  int64_t value;          /* PF_CONST */
  unsigned law;           /* PF_APPLY */
  unsigned arg[2];        /* PF_APPLY: earlier nodes */
} pf_node_t;

typedef struct {
  unsigned n;
  pf_node_t node[PF_MAX_NODES];
} pf_expr_t;

/* Derive every law's closed form and prove it by induction. */
sm_status_t pf_laws(const lw_book_t *laws, pf_book_t *out);

/* The induction certificate on its own: 1 when `formula` matches law L at
   the base case and its step is exactly what one step of L adds. Used by
   pf_laws, and callable on any candidate formula, right or wrong. */
int pf_certify(const lw_book_t *laws, const pf_book_t *pb, unsigned law, const pf_poly_t *formula);

/* Building expressions. Each returns the new node's index. */
void pf_clear(pf_expr_t *e);
unsigned pf_x(pf_expr_t *e);
unsigned pf_y(pf_expr_t *e);
unsigned pf_const(pf_expr_t *e, int64_t v);
unsigned pf_apply(pf_expr_t *e, unsigned law, unsigned a, unsigned b);   /* b ignored for one-argument laws */

/* Normal form of an expression; SM_ERR_EMPTY_DOMAIN if it uses a law whose
   closed form is not proved, SM_ERR_DOMAIN_TOO_LARGE on overflow. */
sm_status_t pf_normal(const lw_book_t *laws, const pf_book_t *pb, const pf_expr_t *e,
                      pf_poly_t *out);

typedef struct {
  int proved;             /* 1: equal for every x and y */
  int refuted;            /* 1: a counterexample was found */
  int64_t cx, cy;         /* the counterexample */
  int64_t left, right;    /* both sides there */
} pf_verdict_t;

/* Prove or refute "for every x, y: lhs = rhs". */
sm_status_t pf_prove(const lw_book_t *laws, const pf_book_t *pb, const pf_expr_t *lhs,
                     const pf_expr_t *rhs, pf_verdict_t *out);

/* Value of a polynomial at (x, y), for checking against the laws. */
int64_t pf_value(const pf_poly_t *p, int64_t x, int64_t y);

/* Ordinary powers, for display: writes e.g. "(x^2 + x)/2". */
void pf_print(const pf_poly_t *p, const char *xname, const char *yname, char *buf, unsigned cap);

#endif /* SMARSH_PROOF_H */
