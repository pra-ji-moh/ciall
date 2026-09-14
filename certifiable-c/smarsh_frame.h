/*
 * smarsh_frame.h -- the formulation, found by elimination.
 *
 * ============================================================
 * WHAT THIS CLOSES
 * ============================================================
 * Everything under this point reasons about a described world: variables,
 * constraints, questions (smarsh_space.h). Until now a person wrote that
 * description. That was the last place a human mind was doing the work.
 *
 * Here the description is found the same way everything else is found: by
 * eliminating possibility. The input is raw. A situation is a table of
 * READINGS, numbers taken off the world with no meaning attached, and one
 * OUTCOME per row. No names carry sense, no column is marked important, no
 * relation is suggested.
 *
 * From that it produces:
 *
 *   which readings matter   proved, by a pair of rows that differ in one
 *                           reading alone and disagree in the outcome. A
 *                           reading that varied without ever changing the
 *                           outcome, and that no surviving description
 *                           uses, is reported silent. One never varied on
 *                           its own is reported untested, not dismissed.
 *   quantities of its own    compound parts of the surviving description
 *                           become named quantities: it invents "how many
 *                           are filled" because that is what the shortest
 *                           surviving account of the outcome needs.
 *   the description          a relation over those quantities that survives
 *                           every row, the simplest one if several do.
 *   what it rests on         support, as everywhere else: the share of
 *                           candidate descriptions eliminated. Applying it
 *                           past the range witnessed is marked a guess.
 *
 * The output is written in the engine's own medium, as text the parser
 * reads, so fr_to_theory hands smarsh_space.h a theory it can be asked
 * questions about. Nothing sits on top of anything: the formulation is an
 * elimination, its result is a description, and the description is what
 * the engine already thinks in.
 *
 * ============================================================
 * HOW POSSIBILITY IS ENUMERATED
 * ============================================================
 * Candidates are built to a bounded depth over the readings and small
 * whole constants, with +, -, *, min and max, then (when the outcome is
 * yes/no) compared against constants and against each other. Candidates
 * that agree on every row are the same description written twice, and only
 * the shortest is kept. Every remaining candidate is tested against every
 * row and eliminated by the first row it contradicts. Arithmetic is
 * guaranteed (smarsh_interval.h): a candidate is eliminated only when the
 * outcome is certainly outside what it gives.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * The description found is the simplest one in the enumerated space that
 * survives. A law outside that space is not found, and the report says how
 * much was searched rather than claiming there is nothing else. Survival
 * is not truth: a description that survives every row is exactly that, and
 * carries its support and its range. Up to FR_MAX_READINGS readings and
 * FR_MAX_OBS rows.
 */

#ifndef SMARSH_FRAME_H
#define SMARSH_FRAME_H

#include "smarsh_core.h"
#include "smarsh_interval.h"
#include "smarsh_space.h"

#define FR_MAX_READINGS 8u
#define FR_MAX_SIZE 7u        /* longest description it will write: leaves plus operations */
#define FR_BUDGET 400000      /* how many descriptions it will consider before stopping */
#define FR_MAX_INVARIANTS 8u
#define FR_MAX_OBS 128u
#define FR_MAX_DEFS 4u
#define FR_TEXT 128u

/* What was seen: numbers, and nothing else. */
typedef struct {
  unsigned n_readings;
  char reading_name[FR_MAX_READINGS][SX_NAME];
  double lo[FR_MAX_READINGS], hi[FR_MAX_READINGS]; /* what is possible at all */
  int whole[FR_MAX_READINGS];                      /* whole numbers only */
  char outcome_name[SX_NAME];
  int outcome_whole;
  double outcome_lo, outcome_hi;
  unsigned n_obs;
  double reading[FR_MAX_OBS][FR_MAX_READINGS];
  double outcome[FR_MAX_OBS];
} fr_situation_t;

typedef enum {
  FR_MATTERS = 0,  /* proved: one reading changed alone, the outcome followed */
  FR_SILENT = 1,   /* changed alone and the outcome did not, and nothing uses it */
  FR_UNTESTED = 2  /* never changed on its own: no verdict either way */
} fr_standing_t;

typedef struct {
  fr_standing_t standing[FR_MAX_READINGS];
  unsigned witness_a[FR_MAX_READINGS], witness_b[FR_MAX_READINGS]; /* the two rows */
  double seen_lo[FR_MAX_READINGS], seen_hi[FR_MAX_READINGS];

  int found;                                   /* a description survived */
  char law[FR_TEXT];                           /* it, over the named quantities */
  char raw_law[FR_TEXT];                       /* it, over the readings alone */
  unsigned n_defs;
  char def_name[FR_MAX_DEFS][SX_NAME];
  char def_body[FR_MAX_DEFS][FR_TEXT];

  unsigned candidates;      /* descriptions possible before looking */
  unsigned survivors;       /* still standing after every row */
  double support;           /* (candidates - survivors) / candidates */
  char rival[FR_TEXT];      /* another survivor, when one exists */
  int has_rival;

  int conjectured;          /* the declared range reaches past what was seen */
  int capped;               /* the space of descriptions filled up: it saw part of it */
  unsigned shape;           /* the description's form, ignoring which readings */
} fr_frame_t;

void fr_situation_init(fr_situation_t *s);
/* Declare a reading. lo/hi bound what is possible, not what was seen. */
sm_status_t fr_reading(fr_situation_t *s, const char *name, double lo, double hi, int whole,
                       unsigned *idx);
sm_status_t fr_outcome(fr_situation_t *s, const char *name, double lo, double hi, int whole);
/* One row: a value per reading, in order, and what happened. */
sm_status_t fr_observe(fr_situation_t *s, const double *readings, double outcome);

/* Find the formulation. */
sm_status_t fr_formulate(const fr_situation_t *s, fr_frame_t *out);

/* Write it into a theory: variables, its own quantities, the description. */
sm_status_t fr_to_theory(const fr_situation_t *s, const fr_frame_t *f, sx_theory_t *th);

/* Say it, in words, including what it rests on and what it does not know. */
void fr_report(const fr_situation_t *s, const fr_frame_t *f);

/*
 * Two situations with nothing in common, formulated separately, whose
 * descriptions have the same form. That is an analogy, and it is not a
 * separate faculty: it falls out of both being formulated by elimination.
 */
int fr_same_shape(const fr_frame_t *a, const fr_frame_t *b);

/*
 * What is always true here, with no outcome named at all.
 *
 * Given a table and nothing else, every yes/no description that holds in
 * every row. Two are dropped: one true of every imaginable world in the
 * declared ranges, which says nothing about this one, and one that follows
 * from what it has already reported. Both are settled by asking the engine,
 * so "this is news" is proved, not assumed.
 */
typedef struct {
  unsigned n;
  char text[FR_MAX_INVARIANTS][FR_TEXT];
  unsigned candidates, kept;
} fr_invariants_t;

sm_status_t fr_invariants(const fr_situation_t *s, fr_invariants_t *out);

/*
 * A sequence x[0..n-1] as a situation: each step described by the two
 * before it and by where it sits. Growth, repetition and recurrence are
 * then the same search as everything else.
 */
sm_status_t fr_recurrence(const double *x, unsigned n, double lo, double hi, int whole,
                          fr_situation_t *sit, fr_frame_t *out);

#endif /* SMARSH_FRAME_H */
