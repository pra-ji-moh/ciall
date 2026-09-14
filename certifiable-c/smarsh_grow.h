/*
 * smarsh_grow.h -- the child growing by itself, through a map of worlds.
 *
 * ============================================================
 * THE MAP
 * ============================================================
 * Worlds come in kinds, and a kind is closed until the child has earned it,
 * the way a game holds back its later levels:
 *
 *   mazes           open from the start
 *   keys and doors  opens once it handles mazes at difficulty 3. A door
 *                   stands between it and the end, and only a cause it has
 *                   never met opens it.
 *   pushing         opens once it handles mazes at difficulty 3. The end is
 *                   about where something else is, not where it is.
 *
 * Nothing unlocks a kind except what the child has shown. It is not told
 * the new kind exists until the moment it has earned it, and then it is
 * told nothing about how that kind works: it meets it the way it met the
 * first maze, knowing nothing, and must reason its way through.
 *
 * ============================================================
 * WHAT GROWING MEANS HERE
 * ============================================================
 * Each kind has its own progress. The child always practises the kind it is
 * least far along in, so a newly opened kind is worked on until it catches
 * up. For each world it plays it judges itself:
 *
 *   finished well   every level ended, and the levels after the first (where it
 *                   finds out how the world works) within twice their shortest.
 *                   Three in a row and it moves itself up a difficulty in
 *                   that kind, and that difficulty becomes its frontier.
 *   finished        but wastefully. It stays where it is and tries again.
 *   not finished    it writes down what it could not explain. Five in a row
 *                   and it steps back one difficulty in that kind, and
 *                   records that this is its edge for now.
 *
 * What a world is made of is never carried from one world to the next,
 * because in the next world it is different. What is carried is where it
 * stands on the map, and everything that surprised it.
 *
 * ============================================================
 * WHEN NOBODY IS WATCHING
 * ============================================================
 * Where it stands lives in a file, and what happened in a journal. A stretch
 * of growth reads the file, plays, writes it back and adds to the journal.
 * Run on a schedule, it keeps growing, and the journal is how anyone finds
 * out what it did. A mind written before the map existed is read as
 * progress in mazes.
 */

#ifndef SMARSH_GROW_H
#define SMARSH_GROW_H

#include <stdio.h>

#include "smarsh_core.h"

#define GW_KINDS 3u

typedef struct {
  int unlocked;
  int has_frontier;
  unsigned difficulty;      /* what it is practising in this kind */
  unsigned frontier;        /* the hardest it has shown it handles here */
  unsigned streak;          /* finished well in a row here */
  unsigned stuck;           /* not finished in a row here */
  unsigned worlds, finished, finished_well;
} gw_track_t;

typedef struct {
  unsigned runs;            /* separate stretches of growth */
  unsigned worlds, finished, finished_well;
  unsigned next_seed;
  unsigned actions, shortest;   /* over worlds finished */
  unsigned lost_itself, mixed, unexplained, cornered, restarts;
  gw_track_t track[GW_KINDS];
} gw_mind_t;

void gw_mind_init(gw_mind_t *m);

/* SM_OK when a mind was read; SM_ERR_EMPTY_DOMAIN when there was none, and m starts fresh. */
sm_status_t gw_load(gw_mind_t *m, const char *path);
sm_status_t gw_save(const gw_mind_t *m, const char *path);

/* Grow through this many worlds. journal may be NULL: then nothing is written down. */
sm_status_t gw_grow(gw_mind_t *m, unsigned worlds, FILE *journal);

void gw_report(const gw_mind_t *m);

#endif /* SMARSH_GROW_H */
