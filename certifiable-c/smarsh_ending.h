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
 * and the same four said of a ROLE instead of a colour, where a role is what a
 * thing does rather than which colour it is: the thing it moves, the ground, what
 * blocks it, what gets used up, the rare thing, what never changes, what appears,
 * the bulk. A theory in colours is true of one game only, because colour 9 means
 * nothing in the next game. A theory in roles -- "it ends when I step onto the
 * rare thing once what can be taken is gone" -- can be true of a game it has
 * never seen, and those are the ones worth carrying.
 *
 *   ONTO_R(r) LAST_R(r) GONE_R(r) POINT_R(r)
 *
 * and two that say which thing was pointed at rather than which colour it was:
 *
 *   ACT(k)    it did act k (the plainest fact of all: which key it pressed)
 *   TOUCH(v)  a cell of colour v was next to the body when it acted
 *   PSIZE(k)  the thing pointed at was of size class k
 *   PNEW      the thing pointed at had appeared since the level began
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
#define EN_ROLES 8u            /* what a thing does, rather than which colour it is */
#define EN_ATOMS 18u           /* ONTO LAST GONE POINT MATCH, the first four by role, and which thing */
#define EN_ACTS 8u             /* the acts it can do */
#define EN_SIZES 8u            /* how big the thing pointed at was, in classes */
#define EN_MAX_FAMILIES 208u
#define EN_WHERE 9u            /* which ninth of the board a thing was in */
#define EN_DEEP_MAX 64u        /* theories of three facts, formulated when two were not enough */
#define EN_DEEP_FACTS 3u
#define EN_WHERE 9u            /* which ninth of the board */
#define EN_DEEP_MAX 64u        /* theories of three facts, once two were not enough */
#define EN_DEEP_FACTS 3u
#define EN_LOG 16384u          /* acts remembered this game, to replay against a new family */

typedef enum {
  EN_ONTO = 0, EN_LAST = 1, EN_GONE = 2, EN_POINT = 3, EN_MATCH = 4,
  EN_ONTO_R = 5, EN_LAST_R = 6, EN_GONE_R = 7, EN_POINT_R = 8,
  EN_PSIZE = 9, EN_PNEW = 10, EN_ACT = 11, EN_TOUCH = 12, EN_TOUCH_R = 13,
  EN_PAT = 14, EN_BODYAT = 15, EN_ALIGN = 16, EN_EDGE = 17, EN_NONE = 18
} en_atom_t;

/* the roles, in the order the child fills them in (see smarsh_explore.c) */
typedef enum {
  EN_R_BODY = 0,    /* what it moves */
  EN_R_GROUND = 1,  /* what there is most of */
  EN_R_BLOCK = 2,   /* what refused it entry */
  EN_R_TAKEN = 3,   /* what there is less of than when the level began */
  EN_R_RARE = 4,    /* what there was least of */
  EN_R_STILL = 5,   /* what has not changed in number at all */
  EN_R_NEW = 6,     /* what was not there when the level began */
  EN_R_BULK = 7     /* what there is next-most of */
} en_role_t;

/* what was true of one act: a set of colours per atom, and whether panels matched */
typedef struct {
  uint16_t onto, last, gone, point;          /* the colours it was true of */
  uint16_t onto_r, last_r, gone_r, point_r;  /* and the roles those colours held */
  unsigned char match;
  /*
   * Which thing, not only which colour. Pointing at colour 9 twice can end a level
   * once and do nothing the other time, and then every theory said in colours alone
   * is ruled out and the child has nothing left to say. These two say what was
   * different about the thing itself: how big it was, and whether it had appeared
   * since the level began.
   */
  uint16_t act;                              /* which of the acts it did */
  /*
   * Where the body was standing when it acted. Some levels end on a plain key
   * press whose only other fact is which key: what told that press apart from
   * the hundred harmless ones before it was what the body was up against.
   */
  uint16_t one, few, most, grew, shrank;
  uint16_t bodyat, pat, align, edge;        /* where, not only what */
  uint16_t a_gone, a_one, a_few, a_most;   /* the same, of the board the act left behind */   /* how many there were, not only whether any */
  uint16_t touch, touch_r;                   /* colours, and roles, next to the body */
  uint16_t psize;                            /* size class of the thing pointed at */
  unsigned char pnew;                        /* that cell was not this colour when the level began */
  unsigned char ended;
} en_obs_t;

typedef struct {
  en_atom_t a, b;              /* b == EN_NONE: a single atom */
  sm_possibility_t dom;        /* index a-colour * 16 + b-colour (0 where an atom takes none) */
  unsigned size;
  int from_library;            /* formulated at the start because another game kept it */
  int survived_ending;         /* something in it held through an ending here */
} en_family_t;

/*
 * One theory of three facts: "it ends when I point at colour 15, at a thing of one
 * cell, in the middle of the board". These are formulated only when every theory of
 * one fact and every theory of two has been ruled out, because three facts joined is
 * a great many theories and the child has no business holding them until the world
 * has shown it that fewer will not do.
 */
typedef struct {
  unsigned char atom[EN_DEEP_FACTS];
  unsigned char val[EN_DEEP_FACTS];
} en_conj_t;

typedef struct {
  en_family_t fam[EN_MAX_FAMILIES];
  unsigned n_fam;
  unsigned depth;              /* 1: single atoms; 2: it widened to pairs */
  unsigned endings;
  unsigned widenings;          /* times it widened its own language */
  en_obs_t log[EN_LOG];
  unsigned n_log;
  unsigned blank_at;           /* acts seen when its whole language first went empty, 0 if never */
  unsigned blank_endings;      /* endings it had seen by then */
  unsigned blank_acts;         /* acts it has gone on taking with nothing left to say */
  en_conj_t deep[EN_DEEP_MAX]; /* what it settled on when two facts joined were not enough */
  unsigned n_deep;
  unsigned deep_tried;         /* three-fact theories it weighed against everything it had seen */
} en_theory_t;

/* A new game. `library` is a comma-separated list of family names kept from other
   games (e.g. "ONTO,ONTO+GONE"), or NULL: those are formulated now. */
sm_status_t en_begin(en_theory_t *t, const char *library);

/* One act and what came of it. Widens the language if it leaves nothing possible. */
sm_status_t en_observe(en_theory_t *t, const en_obs_t *o);

/* What to go towards, from the theories still possible that have earned it (an
   ending here, or a family kept from another game). Colours to go to, to have none of, to point at; whether matching matters; and the
   same said in roles, for the explorer to resolve into colours in the game it is in. */
sm_status_t en_aim(const en_theory_t *t, uint16_t *onto, uint16_t *gone, uint16_t *point, int *match,
                   uint16_t *onto_role, uint16_t *gone_role, uint16_t *point_role);

/* the name of a family, e.g. "ONTO+GONE" */
const char *en_family_name(const en_family_t *f, char *buf, unsigned cap);

/* one line per family: what remains of it, in words */
sm_status_t en_report(const en_theory_t *t, FILE *out);

#endif
