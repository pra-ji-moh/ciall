/*
 * smarsh_play.h -- the child in a world it can only see and act in.
 *
 * ============================================================
 * WHAT IT IS GIVEN
 * ============================================================
 * The same thing ARC-AGI-3 gives an agent, and nothing more:
 *
 *   a frame      64 by 64 cells, each one of 16 colours
 *   the actions  the ones this world offers, by number only
 *   a signal     that a level has ended
 *
 * No instructions. No goal. No idea which colour is itself, what a wall
 * is, which way an action moves anything, or whether it moves anything at
 * all. Nothing about a colour or a shape is built in.
 *
 * ============================================================
 * WHAT IT ACQUIRES, AND HOW
 * ============================================================
 * Every one of these is an elimination over what it has done and seen:
 *
 *   itself      the cells that moved together when it acted. Which cells
 *               belong to one thing is not assumed; they are the ones that
 *               shared a fate. A thing may be several colours.
 *   what an     for each action, the displacement it produced. A second,
 *   action does different displacement eliminates the idea that the action
 *               has one; an action that never moves anything is found to
 *               do nothing.
 *   what blocks a colour that was in the way every time a move failed, and
 *               that it has never moved onto. Moving onto a colour even
 *               once rules it out as a wall for good.
 *   what ends   a colour it was covering at every level that ended, and
 *   a level     that it has never covered without a level ending. Covering
 *               a colour once without an ending rules it out.
 *
 * Knowledge is carried from level to level. The layout is forgotten; what
 * actions do, what blocks, and what ends a level are not.
 *
 * ============================================================
 * WHAT IT DOES NEXT
 * ============================================================
 * One rule, and it is the same rule as the rest: go where something can
 * still be eliminated. An action it has never tried is tried. Otherwise it
 * plans, over its own account of the world, a way to cover a colour that
 * could still be what ends the level. When nothing can be eliminated
 * nearby, it goes somewhere it has not been. There is no reward and no
 * learned policy: it acts to find out, and what it finds out narrows what
 * it acts on.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * It learns a single body that moves rigidly, actions that displace it, walls
 * as colours, and an ending as a colour covered. Worlds where actions
 * rotate or recolour things, where several things move at once, or where
 * the ending is an arrangement rather than a place, are beyond this and are
 * the next work. A move that overlaps its own previous cells is seen only
 * in part.
 */

#ifndef SMARSH_PLAY_H
#define SMARSH_PLAY_H

#include <stdio.h>

#include "smarsh_core.h"

#define PL_SIZE 64u
#define PL_COLOURS 16u
#define PL_MAX_ACTIONS 5u
#define PL_BODY_CELLS 64u
#define PL_MAX_LEVELS 16u

typedef struct {
  unsigned w, h;
  unsigned char c[PL_SIZE][PL_SIZE]; /* [row][col], a colour 0..15 */
} pl_frame_t;

/* The world, from outside. The child sees only frames and the signal. */
typedef struct pl_game pl_game_t;
struct pl_game {
  void *state;
  unsigned n_actions;                /* actions 1..n_actions are offered */
  unsigned levels;
  void (*reset)(pl_game_t *g, pl_frame_t *out);
  /* 0 nothing ended, 1 a level ended and out is the next one, 2 the game ended,
     3 the world started over by itself (a game over): out is the fresh start */
  int (*act)(pl_game_t *g, unsigned action, pl_frame_t *out);
  /* the shortest number of actions for the current level; for scoring only */
  unsigned (*shortest)(pl_game_t *g);
  /* start the current level again, as ARC-AGI-3's RESET does. May be NULL. */
  void (*restart)(pl_game_t *g, pl_frame_t *out);
};

typedef enum {
  PL_UNTRIED = 0,  /* never taken with itself in view */
  PL_MOVES = 1,    /* displaces itself, by a displacement that has held every time */
  PL_STILL = 2,    /* has never moved it */
  PL_MIXED = 3     /* has moved it by two different displacements: no single effect */
} pl_effect_t;

typedef struct {
  /* itself, as the cells that shared a fate */
  int have_body;
  unsigned body_n;
  int body_dr[PL_BODY_CELLS], body_dc[PL_BODY_CELLS];
  unsigned char body_col[PL_BODY_CELLS];

  /* what each action does */
  pl_effect_t effect[PL_MAX_ACTIONS + 1u];
  int eff_dr[PL_MAX_ACTIONS + 1u], eff_dc[PL_MAX_ACTIONS + 1u];
  unsigned moved[PL_MAX_ACTIONS + 1u], stayed[PL_MAX_ACTIONS + 1u];

  /* what blocks */
  unsigned failures;
  int wall_alive[PL_COLOURS];     /* has been in the way when a move failed */
  int wall_ruled_out[PL_COLOURS]; /* has been moved onto, so it does not block */

  /* what ends a level */
  unsigned endings;
  int end_alive[PL_COLOURS];      /* not yet ruled out */
  int end_seen[PL_COLOURS];       /* has been covered at all */

  /* what covering a colour causes elsewhere: while it covered k, cells of
     colour j became colour cause_to[k][j]. -1 not seen, -2 ruled out */
  signed char cause_to[PL_COLOURS][PL_COLOURS];
  unsigned causes_seen;

  /* something that moved along ahead of it, the same way it moved */
  int push_known;
  unsigned push_n;
  int push_dr[PL_BODY_CELLS], push_dc[PL_BODY_CELLS];
  unsigned char push_col[PL_BODY_CELLS];
  unsigned pushes;
  /* an ending brought about by the pushed thing covering a colour */
  unsigned push_endings;
  int pend_alive[PL_COLOURS];
  int pend_seen[PL_COLOURS];

  /* what it did */
  unsigned actions_taken;
  unsigned level_actions[PL_MAX_LEVELS];
  unsigned level_shortest[PL_MAX_LEVELS];
  unsigned levels_done;
  unsigned tries_to_find_itself;  /* actions before it knew which cells it was */
  /* what it could not explain: the frontier of what it knows */
  unsigned lost_itself;           /* times its own shape could not be found */
  unsigned mixed;                 /* actions that moved it by more than one displacement */
  unsigned unexplained_endings;   /* levels that ended while it covered nothing it suspected */
  unsigned cornered;              /* times it had nowhere left to go and nothing left to try */
  unsigned restarts;              /* levels it started again after stranding itself */
  unsigned started_over;          /* times the world started over without being asked */
  /* colours that changed even when it did nothing new: not its doing */
  int ticks[PL_COLOURS];
} pl_child_t;

void pl_child_init(pl_child_t *ch);

/*
 * Play the whole game, spending at most budget actions. Knowledge persists
 * across levels. Returns SM_OK when every level ended within the budget,
 * SM_ERR_EMPTY_DOMAIN when the budget ran out first.
 */
sm_status_t pl_play(pl_child_t *ch, pl_game_t *g, unsigned budget);

/* The same, for a world already started elsewhere: first is what it shows now. */
sm_status_t pl_play_from(pl_child_t *ch, pl_game_t *g, unsigned budget, const pl_frame_t *first);

/* Say what it made out, and how it went. */
void pl_report(const pl_child_t *ch, const pl_game_t *g);
/* The same, to any stream. */
void pl_report_to(FILE *out, const pl_child_t *ch, const pl_game_t *g);

#endif /* SMARSH_PLAY_H */
