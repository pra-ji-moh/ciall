/*
 * arc_worldgen.h -- an endless supply of worlds in the shape of ARC-AGI-3,
 * of several kinds, made on demand, harder on request.
 *
 * The child never sees inside this file. It asks for a world of some kind at
 * some difficulty, is handed frames and numbered actions, and must work the
 * rest out. The kinds, each needing something the one before did not:
 *
 *   maze   a body to move, walls, and a place that ends the level
 *   keys   the same, but a door stands in the way, and covering a key
 *          somewhere else makes every door vanish. Needs: a cause, where
 *          covering one thing changes another thing elsewhere
 *   push   an open room with a box and a target. The level ends when the box
 *          is on the target, not when the body is. Needs: another thing
 *          moving because it moved, and an ending about where that thing is
 *
 * What changes as difficulty rises, so that nothing can be carried in as an
 * assumption: the size of the world, the body's size and colours, how far a
 * step goes, which action goes which way, actions that do nothing, and every
 * colour. A seed fixes a world exactly.
 */

#ifndef ARC_WORLDGEN_H
#define ARC_WORLDGEN_H

#include "smarsh_play.h"

#define WG_MAX_DIFFICULTY 9u

typedef enum { WG_MAZE = 0, WG_KEYS = 1, WG_PUSH = 2, WG_KINDS = 3 } wg_kind_t;

typedef struct {
  unsigned kind, difficulty, seed;
  unsigned rows, cols, scale;
  unsigned floor, wall, ending, decoy, body_a, body_b, door, key, box;
  unsigned n_actions, n_dead;
} wg_facts_t;   /* what the world is, for the journal only: never shown to the child */

/* A maze at this difficulty from this seed. */
void wg_make(pl_game_t *g, unsigned difficulty, unsigned seed);

/* A world of this kind at this difficulty from this seed. */
void wg_make_kind(pl_game_t *g, unsigned kind, unsigned difficulty, unsigned seed);

void wg_facts(const pl_game_t *g, wg_facts_t *out);

const char *wg_kind_name(unsigned kind);

#endif /* ARC_WORLDGEN_H */
