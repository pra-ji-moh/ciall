/*
 * smarsh_guess.h -- working out what a thing is doing, without being told what
 * kinds of thing there are to do.
 *
 * Until now the child could only hold the kinds of model I wrote for it: a thing
 * that moves by a fixed step, then a thing that keeps to a rhythm. A world with a
 * thing that appears, or counts down, or turns at a wall, was beyond saying, and
 * it could not invent the kind -- only fill in a kind it already had.
 *
 * Here the kind is not given either. What it sees of a thing (when, where) is a
 * table, and smarsh_frame formulates over that table by elimination: it
 * enumerates descriptions in order of size and throws out every one that some
 * row contradicts. What survives is the law, whatever shape it happens to be --
 * a step, a turn, a count, something with no name. The shape was never chosen in
 * advance, so nothing here says what kinds of thing there are.
 *
 * A law is only worth having if it says what happens next, so the law is handed
 * to smarsh_space as a theory and asked: with when and where known, what must
 * come next? If the answer is pinned to one value, that is the prediction, and
 * the child can check it against what the thing then does.
 */
#ifndef SMARSH_GUESS_H
#define SMARSH_GUESS_H

#include "smarsh_frame.h"
#include "smarsh_space.h"

#define GS_MAX_ROWS 48u

typedef struct {
  double t[GS_MAX_ROWS], r[GS_MAX_ROWS], c[GS_MAX_ROWS];
  unsigned n;

  int found;                  /* a law for where it goes next survived */
  char law_r[FR_TEXT];        /* what it says about the next row */
  char law_c[FR_TEXT];        /* and about the next column */
  double support_r, support_c;
  unsigned shape_r, shape_c;  /* the form of each law, for seeing one thing behave like another */

  sx_theory_t th_r, th_c;     /* the laws, as theories that can be asked */
  int askable;
} gs_guess_t;

void gs_begin(gs_guess_t *g);

/* one more look at the thing: when, and where it was */
sm_status_t gs_saw(gs_guess_t *g, double when, double row, double col);

/* Formulate over everything seen so far. SM_OK whether or not a law survives:
   g->found says. */
sm_status_t gs_formulate(gs_guess_t *g);

/* With when and where known, where must it be next? 1 if both are pinned. */
int gs_predict(gs_guess_t *g, double when, double row, double col, double *next_row,
               double *next_col);

/* Two things whose laws have the same form are doing the same kind of thing. */
int gs_same_kind(const gs_guess_t *a, const gs_guess_t *b);

#endif
