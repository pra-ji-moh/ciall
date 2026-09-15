/*
 * smarsh_explore.h -- reasoning over the situations it can reach.
 *
 * ============================================================
 * WHY THIS EXISTS
 * ============================================================
 * smarsh_play.h reasons by explaining: it finds a body, what each action
 * does to it, what blocks it and what ends a level. When a world fits those
 * explanations it plays well. When it does not (actions that rotate or
 * switch things, a body that changes shape, a level that ends when two
 * patterns match, a game played by pointing) it has nothing to fall back on.
 *
 * A person faced with a game they cannot yet explain does not act at random.
 * They remember what situations they have been in, what they tried in each,
 * and go deliberately to what they have not tried. That is what this is.
 *
 * ============================================================
 * WHAT IT HOLDS
 * ============================================================
 *   a situation   a frame, with whatever ticks by itself left out
 *   what it can   in each situation: every action on offer, and, where
 *   do there      pointing is on offer, pointing at each distinct thing in
 *                 view (one place in each patch of one colour)
 *   what happened for each thing it has tried in a situation: the situation
 *                 it led to, or that the level ended, or that it died
 *
 * ============================================================
 * HOW IT CHOOSES, AND WHY THAT IS REASONING
 * ============================================================
 * Every choice eliminates a possibility that has not been eliminated: an
 * untried action where it stands, or else the shortest way it knows to a
 * situation that still has one. When nothing untried can be reached from
 * where it is, it starts the level again and goes from the beginning. When
 * nothing untried can be reached from the beginning either, it has shown
 * that what it can do in this level cannot end it, and says so.
 *
 * It asks itself questions as it goes, the ones a person asks, and writes
 * them down with their answers: what can I do here, what does this do, why
 * does part of the picture change whatever I do, and above all, once a level
 * has ended, what did winning change, and where can I make that happen
 * again. The last is the one that makes a second level quick: among the
 * untried situations it can reach, it goes first to the ones already
 * changing things the way winning did, weighed against how far away they
 * are.
 *
 * Nothing about the game has to be understood for this to work, and nothing
 * about any game is written in. What it does learn, it carries from level to
 * level: which kinds of action, and pointing at which colours, have never
 * changed anything, and those are tried last.
 *
 * What ticks by itself is found, not assumed: a small change of one colour
 * into another that happens again under a different action, somewhere
 * else, and never changes back. The patch that colour belongs to is left out
 * of every situation from then on, so a clock running down does not make
 * every moment look new.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * It assumes the world is the same each time it is in the same situation.
 * Worlds where things move on their own every moment (not a clock, but
 * something wandering) make every situation new, and it degrades towards
 * trying things in order. It remembers up to EX_MAX_NODES situations in a
 * level. It is thorough before it is quick: it finds endings a person would
 * reason their way to, but usually with more actions than a person.
 */

#ifndef SMARSH_EXPLORE_H
#define SMARSH_EXPLORE_H

#include <stdio.h>

#include "smarsh_core.h"
#include "smarsh_play.h"

#define EX_MAX_NODES 32768u
#define EX_MAX_ACTS 64u
#define EX_MAX_LEVELS 16u

/* A world that can be pointed at, from outside. */
typedef struct ex_game ex_game_t;
struct ex_game {
  void *state;
  unsigned available[8];   /* the actions on offer now: 1..7, 6 is pointing */
  unsigned n_available;
  /* 0 still playing, 1 a level ended, 2 the game is won, 3 it died and the
     level started over by itself. out is what it shows now. x, y only for 6. */
  int (*act)(ex_game_t *g, unsigned action, unsigned x, unsigned y, pl_frame_t *out);
  /* start the level again */
  void (*reset)(ex_game_t *g, pl_frame_t *out);
};

typedef struct {
  unsigned actions;                     /* taken in all */
  unsigned levels_done;
  unsigned level_actions[EX_MAX_LEVELS];
  unsigned situations[EX_MAX_LEVELS];   /* distinct situations met in each level */
  unsigned deaths, resets, clock_cells, rebuilds;
  int exhausted;                        /* a level where nothing untried was left */
  int won;
  /* carried from level to level: how often each kind of action, and pointing
     at each colour, changed nothing */
  unsigned kind_tried[8], kind_nothing[8];
  unsigned point_tried[16], point_nothing[16];

  /* what winning changed, carried from level to level: for each colour turned
     into another, how much of that the winning situations held */
  double win_change[256];
  unsigned wins_seen;
  unsigned guided;          /* times it went first to what most resembled winning */

  /* how the last level was won, kept as the shortest route it knows, each step
     described by what was done and, for pointing, at what: the colour, and
     which of the things of that colour it was, counting across and down */
  unsigned plan_len;
  unsigned char plan_kind[256];
  unsigned char plan_colour[256];
  unsigned char plan_rank[256];
  unsigned plans_tried, plans_won;   /* levels it began by doing what won before */
  unsigned predicted;        /* pointings it did not spend, predicting they would do nothing */
  unsigned stood_back;       /* steps held back: they would return it where it stood */
  unsigned death_retries;    /* times it tried again steps a death was pinned on */
  unsigned fresh_starts;     /* times it drew its map of a level again */
  unsigned windows;          /* times it looked further along than the acts it held */
  unsigned runs_before, best_before, recalled;   /* carried between runs */
  unsigned ways_dropped, ways_reopened;   /* ways of going it gave up as getting nowhere */
  unsigned laws_found;   /* laws it formulated for a thing, of no kind given in advance */
  unsigned asks_that_answer, asks_ruled_out, predictions_broken;   /* learning how to answer itself */
  unsigned puzzles, puzzles_explained, puzzles_given_up;   /* what it could not lay at its own door */
  unsigned waited;   /* times it let a thing it had explained pass, instead of walking into it */
  unsigned stepped_clear;   /* times it kept out of the way of a thing it had explained */
  unsigned places_reopened;   /* times a place it had written off did something */
  unsigned sent_learned;   /* distances it found a held thing can be sent */
  unsigned unpredicted;      /* times it ran out and took those predictions back */

  /* where it writes its questions and answers down; NULL: it thinks silently */
  FILE *thinking;
  unsigned thoughts;        /* written in the current level */
} ex_explorer_t;

void ex_init(ex_explorer_t *ex);

/* Play from the frame the world shows now, spending at most budget actions. */
/* What it settled about this game, carried to its next run (ex_save after, ex_load before). */
sm_status_t ex_save(const ex_explorer_t *ex, FILE *out);
sm_status_t ex_load(ex_explorer_t *ex, FILE *in);

sm_status_t ex_play(ex_explorer_t *ex, ex_game_t *g, const pl_frame_t *first, unsigned budget);

void ex_report(FILE *out, const ex_explorer_t *ex);

#endif /* SMARSH_EXPLORE_H */
