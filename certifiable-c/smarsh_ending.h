/*
 * smarsh_ending.h -- what ends a level, as theories the child formulates by
 * elimination, in a language it widens itself.
 *
 * The only fixed part is the atoms: facts true of one act in the situation it
 * was done in, none of them naming a colour in advance.
 *
 *   ONTO(v)   the body stepped onto colour v
 *   LAST(v)   the body stepped onto the last cells of colour v there were
 *   GONE(v)   no colour v was left (though there was some when the level began)
 *   POINT(v)  it pointed at colour v
 *   MATCH     two panels were alike
 *
 * A family is a shape of theory: one atom, or two atoms both true ("ONTO+GONE":
 * the body steps onto some v once no u is left). A family's domain is every
 * choice of its colours (at most 16 x 16 = 256, one sm_possibility_t). Every act
 * is evidence: an act that ended the level rules out each theory that did not
 * hold for it; an act that ended nothing rules out each theory that did.
 *
 * Self-modification: the child starts with single atoms. When every theory in
 * every family it has is ruled out, that contradiction says its language was too
 * poor, and it widens the language itself: it formulates the two-atom families,
 * replays everything it has seen against them, and carries on. A family that
 * survives an ending is something it has learned the world can be like; its name
 * is kept, and a game it has never seen formulates it from the first act.
 */
#ifndef SMARSH_ENDING_H
#define SMARSH_ENDING_H

#include <stdint.h>
#include <stdio.h>

#include "smarsh_core.h"

#define EN_COLOURS 16u
#define EN_ATOMS 5u            /* ONTO LAST GONE POINT MATCH */
#define EN_MAX_FAMILIES 32u
#define EN_LOG 16384u          /* acts remembered this game, to replay against a new family */

typedef enum { EN_ONTO = 0, EN_LAST = 1, EN_GONE = 2, EN_POINT = 3, EN_MATCH = 4, EN_NONE = 5 } en_atom_t;

/* what was true of one act: a set of colours per atom, and whether panels matched */
typedef struct {
  uint16_t onto, last, gone, point;
  unsigned char match;
  unsigned char ended;
} en_obs_t;

typedef struct {
  en_atom_t a, b;              /* b == EN_NONE: a single atom */
  sm_possibility_t dom;        /* index a-colour * 16 + b-colour (0 where an atom takes none) */
  unsigned size;
  int from_library;            /* formulated at the start because another game kept it */
  int survived_ending;         /* something in it held through an ending here */
} en_family_t;

typedef struct {
  en_family_t fam[EN_MAX_FAMILIES];
  unsigned n_fam;
  unsigned depth;              /* 1: single atoms; 2: it widened to pairs */
  unsigned endings;
  unsigned widenings;          /* times it widened its own language */
  en_obs_t log[EN_LOG];
  unsigned n_log;
} en_theory_t;

/* A new game. `library` is a comma-separated list of family names kept from other
   games (e.g. "ONTO,ONTO+GONE"), or NULL: those are formulated now. */
sm_status_t en_begin(en_theory_t *t, const char *library);

/* One act and what came of it. Widens the language if it leaves nothing possible. */
sm_status_t en_observe(en_theory_t *t, const en_obs_t *o);

/* What to go towards, from the theories still possible that have earned it (an
   ending here, or a family kept from another game): colours to step onto,
   colours to have none of, colours to point at, and whether matching matters. */
sm_status_t en_aim(const en_theory_t *t, uint16_t *onto, uint16_t *gone, uint16_t *point, int *match);

/* the name of a family, e.g. "ONTO+GONE" */
const char *en_family_name(const en_family_t *f, char *buf, unsigned cap);

/* one line per family: what remains of it, in words */
sm_status_t en_report(const en_theory_t *t, FILE *out);

#endif
