/*
 * arc_standin.h -- small worlds in the shape of ARC-AGI-3, for the child
 * to learn in before it meets the real ones.
 *
 * Each is a 64 by 64 grid of 16 colours, a few numbered actions and a
 * level-ended signal, and nothing more reaches the child. The rules below
 * are known to this file and to nobody the child can ask.
 *
 *   maze      a body two cells square and two colours, top and bottom,
 *             moved two cells by each of four actions; walls it cannot
 *             enter; a patch of one colour that ends the level when the
 *             body covers it; a decoy patch that ends nothing
 *   scrambled the same mazes, but which action goes which way is shuffled,
 *             a fifth action does nothing at all, and the patch that ends
 *             the level is a different colour
 *
 * These are not the real benchmark and are not scored as it. They are the
 * nursery: every rule in them is one the child has to find, and each is
 * built so that one wrong built-in assumption (a thing is one colour, a
 * step is one cell, action 1 is up, the goal is colour 4) would fail it.
 */

#ifndef ARC_STANDIN_H
#define ARC_STANDIN_H

#include "smarsh_play.h"

void standin_maze(pl_game_t *g);
void standin_scrambled(pl_game_t *g);

#endif /* ARC_STANDIN_H */
