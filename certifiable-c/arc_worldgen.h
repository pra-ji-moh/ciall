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
 * And kinds whose ending is not a thing or a place at all, but a relation,
 * a transformation or a sequence. Nothing that held in one level can be
 * carried to the next as a colour or a position; only the idea carries:
 *
 *   odd    tiles scattered about, all one colour but one. Covering the one
 *          that differs ends the level. Each level the colour that was odd
 *          becomes the common one, so "this colour ends it" is proved wrong
 *          at once. Needs: sameness and difference, a relation between
 *          things rather than a property of one
 *   mirror a twin that moves whenever it moves, but transformed: left and
 *          right swapped, and from difficulty 5 every direction reversed.
 *          The level ends when the twin, not the body, is on the target.
 *          Needs: an action's effect seen through a transformation, and a
 *          plan made for something it does not directly move
 *   order  coloured pads, and a legend along the top showing them in some
 *          order. Covering them in that order ends the level; covering one
 *          out of turn puts every pad back. The order differs each level.
 *          Needs: reading a symbol as an instruction, and a sequence
 *
 * And kinds that combine what came before, so that guessing costs real moves:
 *
 *   reverse   in order, but the legend is to be read from right to left. A
 *             small mark sits at the right-hand end of the legend, where the
 *             reading starts. A mind that has only ever read left to right
 *             will try that first and be wrong; one that has learned what the
 *             mark means will not. Needs: a cue read as an instruction about
 *             another instruction
 *   odd twin  the mirror twin, and odd-one-out tiles: the level ends when the
 *             twin, not the body, covers the tile that differs. Needs: two
 *             ideas at once, a transformation and a relation
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

typedef enum {
  WG_MAZE = 0, WG_KEYS = 1, WG_PUSH = 2,
  WG_ODD = 3, WG_MIRROR = 4, WG_ORDER = 5,
  WG_REVERSE = 6, WG_ODDTWIN = 7,
  WG_KINDS = 8
} wg_kind_t;

#define WG_MAX_PADS 4u

typedef struct {
  unsigned kind, difficulty, seed;
  unsigned rows, cols, scale;
  unsigned floor, wall, ending, decoy, body_a, body_b, door, key, box;
  unsigned n_actions, n_dead;
  unsigned n_parts;   /* odd: tiles; order: pads; 0 for the other kinds */
} wg_facts_t;   /* what the world is, for the journal only: never shown to the child */

/* A maze at this difficulty from this seed. */
void wg_make(pl_game_t *g, unsigned difficulty, unsigned seed);

/* A world of this kind at this difficulty from this seed. */
void wg_make_kind(pl_game_t *g, unsigned kind, unsigned difficulty, unsigned seed);

void wg_facts(const pl_game_t *g, wg_facts_t *out);

const char *wg_kind_name(unsigned kind);

#endif /* ARC_WORLDGEN_H */
