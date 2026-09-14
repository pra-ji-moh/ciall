/*
 * smarsh_space.h -- worlds described, not listed: the general
 * representation under every branch.
 *
 * ============================================================
 * WHY
 * ============================================================
 * Everything before this listed its worlds: 256 at most, each one named.
 * Mathematics and science do not fit in a list. A projectile's launch
 * speed is any real number between 10 and 20; a triangle has a continuum
 * of shapes. So here a set of worlds is DESCRIBED:
 *
 *     variables   with a type (yes/no, whole number, real) and a range
 *     constraints expressions that must hold: "x^2 + y^2 <= 1",
 *                 "3*a == c", "not (knave_a and knave_b)"
 *
 * and the worlds are every assignment that satisfies them. Questions are
 * expressions too: "v^2 * sin(2*t) / 9.81 > 40", "n*n + n % 2 == 0".
 *
 * ============================================================
 * ELIMINATION, ON REGIONS
 * ============================================================
 * The axiom does not change: to reason is to eliminate possibility. What
 * is eliminated now are REGIONS of worlds, and every step is a guarantee
 * (smarsh_interval.h), never an estimate:
 *
 *   propagation  a constraint cuts away the parts of each variable's range
 *                where it cannot hold (x + y <= 1 with y >= 0.5 cuts x to
 *                at most 0.5). This is constraint propagation, run to a
 *                standstill.
 *   splitting    what propagation cannot settle is split in half and each
 *                half examined, until every region is:
 *                  ELIMINATED  no world in it satisfies the constraints
 *                  CERTAIN     every world in it does, and the question has
 *                              one answer throughout it
 *                  BOUNDARY    too small to split further, still undecided
 *
 * ============================================================
 * VERDICTS
 * ============================================================
 *   TRUE IN EVERY WORLD   proved: every region is eliminated or certain
 *                         with the answer true (the regions are the proof)
 *   FALSE IN EVERY WORLD  the same, false
 *   BOTH                  undetermined: there is a certain world where it
 *                         holds AND one where it does not, both returned
 *   NO WORLDS             the constraints contradict each other: every
 *                         region was eliminated
 *   UNRESOLVED            some region stayed on the boundary at this
 *                         precision. NEVER reported as BOTH: "could not
 *                         tell here" is not "the facts allow either"
 *
 * For a number-valued question: the range it can take (a guarantee), and
 * values it certainly takes, at worlds that certainly exist. A single
 * whole-number value in every world is DERIVED.
 *
 * ============================================================
 * CALCULUS, NATIVELY
 * ============================================================
 * sx_diff builds the derivative of an expression symbolically. With it:
 *   a root is PROVED to exist in a region when the function certainly
 *   changes sign across it, and PROVED unique there when the derivative's
 *   whole range excludes zero (so the function is strictly monotone);
 *   monotonicity itself is proved the same way; and sx_integral encloses a
 *   definite integral between two guaranteed bounds.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * Equations in several real unknowns can be narrowed but not proved to
 * have a solution here (that needs an interval Newton method; not built):
 * such answers are reported as "if a world exists, it lies here". A root
 * where the function touches zero without crossing cannot be told from no
 * root by sign changes, and is reported UNRESOLVED. Up to SX_MAX_VARS
 * variables. The workspace is static: one question at a time.
 */

#ifndef SMARSH_SPACE_H
#define SMARSH_SPACE_H

#include "smarsh_core.h"
#include "smarsh_interval.h"

#define SX_MAX_VARS 12u
#define SX_MAX_NODES 1024u
#define SX_MAX_CONS 32u
#define SX_MAX_SOURCES 8u
#define SX_MAX_DEFS 16u
#define SX_NAME 16u
#define SX_NONE 0xFFFFFFFFu

typedef enum { SX_REAL = 0, SX_INT = 1, SX_BOOL = 2 } sx_type_t;

typedef enum {
  SX_CONST, SX_VAR,
  SX_ADD, SX_SUB, SX_MUL, SX_DIV, SX_NEG, SX_POWI,
  SX_SQRT, SX_EXP, SX_LOG, SX_SIN, SX_COS, SX_ABS, SX_MIN, SX_MAX, SX_MOD,
  SX_LT, SX_LE, SX_EQ, SX_NE, SX_AND, SX_OR, SX_NOT
} sx_op_t;

typedef struct {
  sx_op_t op;
  unsigned a, b;      /* children, earlier nodes */
  unsigned var;       /* SX_VAR */
  int n;              /* SX_POWI exponent */
  iv_t c;             /* SX_CONST */
} sx_node_t;

typedef struct {
  iv_t v[SX_MAX_VARS];
} sx_box_t;

typedef struct {
  unsigned n_vars;
  char name[SX_MAX_VARS][SX_NAME];
  sx_type_t type[SX_MAX_VARS];
  iv_t domain[SX_MAX_VARS];
  unsigned n_nodes;
  sx_node_t node[SX_MAX_NODES];
  unsigned n_cons;
  unsigned con[SX_MAX_CONS];
  /* every constraint carries where it came from and how solid it is:
     nothing sits "on top of" the engine, it is all in the description */
  unsigned source[SX_MAX_CONS];       /* who said it; 0 is what it saw itself */
  int conjectured[SX_MAX_CONS];       /* a guess: a law past its range, a symmetry not fully seen */
  int active[SX_MAX_CONS];            /* off when its source was caught out */
  unsigned n_sources;
  char source_name[SX_MAX_SOURCES][SX_NAME];
  int distrusted[SX_MAX_SOURCES];

  /* named quantities it has defined for itself: concepts, in the same
     medium as everything else */
  unsigned n_defs;
  char def_name[SX_MAX_DEFS][SX_NAME];
  unsigned def_root[SX_MAX_DEFS];

  char error[80];     /* the last parse error, in words */
} sx_theory_t;

typedef struct {
  double rel_eps;       /* smallest region, as a fraction of each real range */
  double value_tol;     /* how tightly to pin a number-valued answer */
  unsigned max_regions; /* past this, what is left is reported unresolved */
} sx_opts_t;

typedef enum {
  SX_TRUE_ALL = 0,
  SX_FALSE_ALL = 1,
  SX_BOTH = 2,
  SX_NO_WORLDS = 3,
  SX_UNRESOLVED = 4,
  SX_VALUE = 5,         /* number-valued: one value in every world */
  SX_RANGE = 6          /* number-valued: a range, with values it certainly takes */
} sx_verdict_t;

typedef struct {
  sx_verdict_t verdict;
  int is_bool;
  iv_t range;                 /* number-valued: guaranteed to hold every answer */
  int has_seen;
  double seen_lo, seen_hi;    /* values certainly taken, at real worlds */
  int has_world;              /* some world certainly exists */
  int has_true, has_false;    /* witness worlds for each answer */
  sx_box_t witness_true, witness_false, witness_world;
  unsigned eliminated, certain, boundary, splits;
  int capped;
  int counted;                /* all whole-number and fully decided */
  double worlds, worlds_true; /* exact counts when counted */
} sx_answer_t;

void sx_init(sx_theory_t *th);
sx_opts_t sx_default_opts(void);

sm_status_t sx_var(sx_theory_t *th, const char *name, sx_type_t type, double lo, double hi,
                   unsigned *idx);
/* Parse an expression over the declared variables. On failure th->error
   says what and where. */
sm_status_t sx_parse(sx_theory_t *th, const char *text, unsigned *root);
/* Parse and add as a constraint every world must satisfy. */
sm_status_t sx_require(sx_theory_t *th, const char *text);

/*
 * A name for a quantity, defined by an expression: a concept of its own,
 * usable in everything it writes afterwards. "left = s1 + s2 + s3" makes
 * counting a thing it can talk about, in the same medium as the rest.
 */
sm_status_t sx_define(sx_theory_t *th, const char *name, const char *text);

/* Who is speaking. Source 0 is always "what it saw itself". */
sm_status_t sx_source(sx_theory_t *th, const char *name, unsigned *id);

/*
 * A constraint with its provenance: which source said it, and whether it
 * is a guess (a law used past where it was witnessed, a symmetry not seen
 * in every case). Guesses are used, and never mistaken for the rest.
 */
sm_status_t sx_claim(sx_theory_t *th, const char *text, unsigned source, int conjectured);

/* Catching a source out switches off everything it said, everywhere. */
void sx_distrust(sx_theory_t *th, unsigned source);

/*
 * The answer, and what it stands on. The question is asked twice: once
 * with everything, once on proved ground alone (no guesses, no untrusted
 * source). Same proved verdict both times: it is proved. Only with the
 * guesses: the answer names them in *rests_on (a bit per constraint).
 */
sm_status_t sx_ask_grounded(sx_theory_t *th, unsigned question, const sx_opts_t *opts,
                            sx_answer_t *out, uint32_t *rests_on, int *proved_outright);

int sx_is_bool(const sx_theory_t *th, unsigned root);
iv_t sx_eval(sx_theory_t *th, unsigned root, const sx_box_t *box);
sx_box_t sx_start(const sx_theory_t *th);
/* Cut the box down by every constraint; 0 when nothing is left. */
int sx_contract(sx_theory_t *th, sx_box_t *box);

/* Ask a question (SX_NONE: just "what worlds exist?"). */
sm_status_t sx_ask(sx_theory_t *th, unsigned question, const sx_opts_t *opts, sx_answer_t *out);
/* The same search with forward evaluation only, no propagation: slower,
   simpler, and a second opinion for testing. */
sm_status_t sx_ask_plain(sx_theory_t *th, unsigned question, const sx_opts_t *opts,
                         sx_answer_t *out);

/* d(root)/d(var), built as new nodes. SM_ERR_INDEX_OUT_OF_DOMAIN when the
   expression uses something without a derivative (abs, min, max, mod,
   comparisons). */
sm_status_t sx_diff(sx_theory_t *th, unsigned root, unsigned var, unsigned *out);

void sx_print(const sx_theory_t *th, unsigned root, char *buf, unsigned cap);

#define SX_MAX_ROOTS 32u

typedef struct {
  unsigned n;
  iv_t root[SX_MAX_ROOTS];   /* each holds EXACTLY one root, proved */
  unsigned n_unresolved;
  iv_t unresolved[SX_MAX_ROOTS];
  int complete;              /* every other part of the range proved root-free */
} sx_roots_t;

/* Roots of f (an expression in the one variable var) in [lo, hi]. */
sm_status_t sx_roots(sx_theory_t *th, unsigned f, unsigned var, double lo, double hi, double eps,
                     sx_roots_t *out);

/* Is f strictly increasing (1), strictly decreasing (-1) on [lo, hi],
   proved from its derivative; 0 if that cannot be shown. */
int sx_monotone(sx_theory_t *th, unsigned f, unsigned var, double lo, double hi);

/* Guaranteed bounds on the integral of f over [lo, hi], in `pieces` parts. */
iv_t sx_integral(sx_theory_t *th, unsigned f, unsigned var, double lo, double hi, unsigned pieces);

/*
 * Systems of equations: f_1 = 0, ..., f_n = 0 in the theory's n real
 * variables, over their ranges. Each region is tested with the Krawczyk
 * operator: from the region X, its midpoint m, the Jacobian over X (built
 * symbolically by sx_diff) and Y, an approximate inverse of the Jacobian
 * at m,
 *     K = m - Y f(m) + (I - Y J(X)) (X - m).
 * If K lies strictly inside X, X holds EXACTLY ONE solution (a theorem;
 * it holds for any matrix Y, so Y's own rounding does not matter). If K
 * misses X, X holds none. Otherwise X is cut to X meet K and split.
 * Everything else in the range is proved solution-free, or listed as
 * unresolved.
 */
#define SX_MAX_SOLUTIONS 32u

typedef struct {
  unsigned n;
  sx_box_t solution[SX_MAX_SOLUTIONS];   /* each holds exactly one solution */
  unsigned n_unresolved;
  sx_box_t unresolved[SX_MAX_SOLUTIONS];
  int complete;                          /* the rest of the range proved solution-free */
  unsigned regions;
} sx_solutions_t;

sm_status_t sx_solve(sx_theory_t *th, const unsigned *f, unsigned n, double rel_eps,
                     sx_solutions_t *out);

#endif /* SMARSH_SPACE_H */
