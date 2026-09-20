/*
 * smarsh_rules.h -- what each act does to each kind of thing on the board, learned
 * by elimination, and used to say what the next picture will be.
 *
 * A THING is a patch of one colour, joined side to side. Its KIND is what the child
 * can say about it without naming it: its colour, and how big it is, in classes.
 * Things of a kind behave alike, so what is learned of one is learned of all.
 *
 * For each (kind, act) it holds every rule its language can say:
 *
 *     nothing            the thing is where it was
 *     go(dr, dc)         every cell of it moves by that much
 *     gone               it is no longer there
 *     grew / shrank      it has more or fewer cells
 *     recoloured(v)      its cells are colour v now
 *
 * and at the start every one of them is possible. An act is done; what actually
 * happened to each thing rules out every rule that says otherwise. What is left is
 * what it knows, and log2 of how many are left is how much it still does not know.
 * Nothing is counted and nothing is weighed: a rule is ruled out or it is not.
 *
 * Where one rule is left for a (kind, act), it can say what that act will do before
 * doing it. Where more than one is left, it cannot, and says so. Everything it says
 * is checked against what happens, and being wrong rules more out.
 */
#ifndef SMARSH_RULES_H
#define SMARSH_RULES_H

#include <stdint.h>
#include <stdio.h>

#include "smarsh_play.h"

#define RU_KINDS (PL_COLOURS * 6u)     /* colour, and six classes of size */
#define RU_ACTS 8u
#define RU_MOVES 25u                   /* go(dr, dc), each -2..2 */
#define RU_RULES (1u + RU_MOVES + 2u + 1u)   /* nothing, the moves, gone, changed size, recoloured */
#define RU_MAX_THINGS 256u

typedef struct {
  unsigned colour, size, cells;
  unsigned top, left, bottom, right;
  unsigned row, col;                   /* where its middle is */
} ru_thing_t;

typedef struct {
  uint32_t left[RU_KINDS][RU_ACTS];    /* a bit per rule still possible */
  unsigned seen[RU_KINDS][RU_ACTS];    /* times an act was done with such a thing there */
  unsigned said, said_right, said_wrong, cannot_say, spared;
  unsigned long long ruled_out;
} ru_world_t;

/* every rule possible again: a new game */
void ru_begin(ru_world_t *w);

/* the things in a picture; how many, up to RU_MAX_THINGS */
unsigned ru_things(const pl_frame_t *f, ru_thing_t *out, unsigned cap);

/* what kind a thing is: its colour and how big it is, in classes */
unsigned ru_kind(const ru_thing_t *t);

/*
 * An act, and the picture before and after it: every rule that says something else
 * happened to a thing of that kind is ruled out. Returns how many were ruled out.
 */
unsigned ru_saw(ru_world_t *w, unsigned act, const pl_frame_t *before, const pl_frame_t *after);

/*
 * What it says this act will do to the things now, where one rule is left for each
 * kind it can see: 1 if it could say and `out` holds its picture of what follows,
 * 0 if more than one rule is still possible for something the act touches.
 */
int ru_say(ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out);

/*
 * 1 when every thing it can see has settled to "nothing happens to it" for this act:
 * the act is known to change nothing, and spending one on it would buy nothing.
 */
int ru_changes_nothing(const ru_world_t *w, unsigned act, const pl_frame_t *now);

/* how much it still does not know about acts and kinds, in bits */
double ru_bits(const ru_world_t *w);

sm_status_t ru_report(const ru_world_t *w, FILE *out);

#endif
