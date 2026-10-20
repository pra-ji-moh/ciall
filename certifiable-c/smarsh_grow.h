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
 * and three abstract kinds, whose ending is a relation, a transformation or
 * a sequence rather than a colour or a place (see arc_worldgen.h):
 *
 *   odd one out     \
 *   mirror twin      > open as soon as it has handled any maze at all
 *   in order        /
 *   reverse order   opens once it handles in-order worlds at difficulty 3
 *   odd twin        opens once it handles mirror twins at difficulty 3
 *
 * The abstract kinds are not held back until it is "ready". There is no
 * right age for a hard idea: a mind meets it early, struggles, and the
 * struggle is what builds the capacity. So once they open, every other world
 * it plays is an abstract one, whether or not it is yet any good at them, and
 * the concrete ladder carries on in the worlds between.
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
 * stands on the map, everything that surprised it, and its library: the
 * ideas that have explained worlds, and a fingerprint of every world it has
 * met, with what ended it (smarsh_mind.h).
 *
 * ============================================================
 * THE MIND, AND WHAT HAPPENS AROUND IT
 * ============================================================
 * Each world is played by the mind (smarsh_mind.h): it sees things, predicts,
 * invents, imagines and acts. Around it:
 *
 *   a teacher   five worlds of one kind in a row not finished well, and the
 *               teacher names, before the next world of that kind, what kind
 *               of idea ends it. Only the kind of idea: the child still has to
 *               find which, and the idea still has to survive everything it
 *               sees. Struggle first, then explanation.
 *   sleep       at the end of every stretch, it replays the worlds it could
 *               not explain and tries every block it has on them.
 *   itself      at the end of every stretch it switches each of its parts off
 *               in turn, plays the same worlds without it, and writes down what
 *               each part is worth.
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
#include "smarsh_mind.h"

#define GW_KINDS 8u

typedef struct {
  int unlocked;
  int has_frontier;
  unsigned difficulty;      /* what it is practising in this kind */
  unsigned frontier;        /* the hardest it has shown it handles here */
  unsigned streak;          /* finished well in a row here */
  unsigned stuck;           /* not finished in a row here */
  unsigned worlds, finished, finished_well;
  unsigned struggling;      /* not finished well in a row here: the teacher's cue */
} gw_track_t;

typedef struct {
  unsigned runs;            /* separate stretches of growth */
  unsigned worlds, finished, finished_well;
  unsigned next_seed;
  unsigned actions, shortest;   /* over worlds finished */
  unsigned lost_itself, mixed, unexplained, cornered, restarts;
  unsigned predictions, mispredicted, taught;
  gw_track_t track[GW_KINDS];
  mi_library_t lib;
} gw_mind_t;

void gw_mind_init(gw_mind_t *m);

/* SM_OK when a mind was read; SM_ERR_EMPTY_DOMAIN when there was none, and m starts fresh. */
sm_status_t gw_load(gw_mind_t *m, const char *path);
sm_status_t gw_save(const gw_mind_t *m, const char *path);

/* Grow through this many worlds. journal may be NULL: then nothing is written down. */
sm_status_t gw_grow(gw_mind_t *m, unsigned worlds, FILE *journal);

void gw_report(const gw_mind_t *m);

#endif /* SMARSH_GROW_H */
