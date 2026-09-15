/*
 * test_explore.c -- reasoning over reachable situations, on games built so
 * that explaining them the old way cannot work.
 *
 *   the lock    three switches flipped by three actions, a fourth action
 *               that does nothing, a clock that runs down and sends it back
 *               to the start when it runs out, and a level that ends only
 *               when the switches match a pattern shown elsewhere. Nothing
 *               moves; the ending is a match between two things.
 *   the tiles   played only by pointing. Pointing at a tile turns it to the
 *               next colour; the level ends when every tile is the same
 *               colour as a sample. A frame round the edge does nothing.
 *
 * Checked: both are won; the clock is found and left out; what never does
 * anything is recognised and carried from level to level; a game with no way
 * to end is reported as exhausted rather than played forever.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_explore.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

/* ---- the lock -------------------------------------------------------------- */

typedef struct {
  unsigned level, sw[3], clock;
  int endless;
} lock_t;

static const unsigned TARGET[3][3] = {{1, 0, 1}, {0, 1, 1}, {1, 1, 0}};
#define LOCK_CLOCK 30u

static void lock_draw(const lock_t *s, pl_frame_t *f) {
  unsigned i;
  memset(f, 0, sizeof(*f));
  f->w = PL_SIZE;
  f->h = PL_SIZE;
  for (i = 0u; i < 3u; i++) {
    f->c[10][10u + 10u * i] = (unsigned char)(s->sw[i] ? 3u : 2u);
    if (!s->endless) f->c[30][10u + 10u * i] = (unsigned char)(TARGET[s->level][i] ? 3u : 2u);
  }
  for (i = 0u; i < LOCK_CLOCK; i++) f->c[60][i] = (unsigned char)(i < s->clock ? 11u : 0u);
}

static void lock_start(lock_t *s) {
  s->sw[0] = s->sw[1] = s->sw[2] = 0u;
  s->clock = LOCK_CLOCK;
}

static int lock_act(ex_game_t *g, unsigned action, unsigned x, unsigned y, pl_frame_t *out) {
  lock_t *s = (lock_t *)g->state;
  (void)x;
  (void)y;
  if (action >= 1u && action <= 3u) s->sw[action - 1u] ^= 1u;
  if (s->clock > 0u) s->clock--;
  if (!s->endless && s->sw[0] == TARGET[s->level][0] && s->sw[1] == TARGET[s->level][1] &&
      s->sw[2] == TARGET[s->level][2]) {
    s->level++;
    lock_start(s);
    lock_draw(s, out);
    return s->level >= 3u ? 2 : 1;
  }
  if (s->clock == 0u) {
    lock_start(s);
    lock_draw(s, out);
    return 3;
  }
  lock_draw(s, out);
  return 0;
}

static void lock_reset(ex_game_t *g, pl_frame_t *out) {
  lock_t *s = (lock_t *)g->state;
  lock_start(s);
  lock_draw(s, out);
}

/* ---- the tiles --------------------------------------------------------------- */

typedef struct {
  unsigned level, tile[4];
} tiles_t;

static const unsigned SAMPLE[3] = {4u, 3u, 2u};
static const unsigned TILE_R[4] = {20u, 20u, 34u, 34u}, TILE_C[4] = {20u, 34u, 20u, 34u};

static void tiles_draw(const tiles_t *s, pl_frame_t *f) {
  unsigned i, r, c;
  memset(f, 0, sizeof(*f));
  f->w = PL_SIZE;
  f->h = PL_SIZE;
  for (r = 0u; r < PL_SIZE; r++) {
    f->c[r][0] = f->c[r][PL_SIZE - 1u] = 5u;
    f->c[0][r] = f->c[PL_SIZE - 1u][r] = 5u;
  }
  for (i = 0u; i < 4u; i++) {
    for (r = 0u; r < 4u; r++) {
      for (c = 0u; c < 4u; c++) f->c[TILE_R[i] + r][TILE_C[i] + c] = (unsigned char)s->tile[i];
    }
  }
  for (r = 50u; r < 53u; r++) {
    for (c = 50u; c < 53u; c++) f->c[r][c] = (unsigned char)(SAMPLE[s->level] + 8u);
  }
}

static void tiles_start(tiles_t *s) {
  unsigned i;
  for (i = 0u; i < 4u; i++) s->tile[i] = 2u + (s->level + i) % 3u;
}

static int tiles_act(ex_game_t *g, unsigned action, unsigned x, unsigned y, pl_frame_t *out) {
  tiles_t *s = (tiles_t *)g->state;
  unsigned i, same = 1u;
  if (action == 6u) {
    for (i = 0u; i < 4u; i++) {
      if (y >= TILE_R[i] && y < TILE_R[i] + 4u && x >= TILE_C[i] && x < TILE_C[i] + 4u) {
        s->tile[i] = s->tile[i] == 4u ? 2u : s->tile[i] + 1u;
      }
    }
  }
  for (i = 0u; i < 4u; i++) {
    if (s->tile[i] != SAMPLE[s->level]) same = 0u;
  }
  if (same) {
    s->level++;
    if (s->level >= 3u) {
      s->level = 2u;
      tiles_draw(s, out);
      return 2;
    }
    tiles_start(s);
    tiles_draw(s, out);
    return 1;
  }
  tiles_draw(s, out);
  return 0;
}

static void tiles_reset(ex_game_t *g, pl_frame_t *out) {
  tiles_t *s = (tiles_t *)g->state;
  tiles_start(s);
  tiles_draw(s, out);
}

int main(void) {
  static pl_frame_t first;
  ex_game_t g;
  ex_explorer_t ex;

  printf("REASONING OVER REACHABLE SITUATIONS\n\n");

  {
    lock_t s;
    memset(&s, 0, sizeof s);
    lock_start(&s);
    memset(&g, 0, sizeof g);
    g.state = &s;
    g.available[0] = 1u; g.available[1] = 2u; g.available[2] = 3u; g.available[3] = 4u;
    g.n_available = 4u;
    g.act = lock_act;
    g.reset = lock_reset;
    lock_draw(&s, &first);
    ex_init(&ex);
    check("the lock is won: switches, not movement, and an ending that is a match",
          ex_play(&ex, &g, &first, 3000u) == SM_OK && ex.won && ex.levels_done == 3u);
    check("the clock is found as something that ticks by itself, and left out",
          ex.clock_cells > 0u);
    /*
     * What ticks, and what each act does, are carried from level to level, so by
     * the last level it costs no more than a walk over the eight settings of three
     * switches, and the whole game is no wandering. Not "later levels cost less
     * than the first": a first level ended by luck teaches nothing, and this must
     * hold whatever its uniform choices are (every CIALL_SEED).
     */
    check("what it has learned bounds the last level: no more than a walk over the settings",
          ex.level_actions[2] <= 24u &&
          ex.level_actions[0] + ex.level_actions[1] + ex.level_actions[2] <= 120u);
    ex_report(stdout, &ex);
  }

  {
    tiles_t s;
    memset(&s, 0, sizeof s);
    tiles_start(&s);
    memset(&g, 0, sizeof g);
    g.state = &s;
    g.available[0] = 6u;
    g.n_available = 1u;
    g.act = tiles_act;
    g.reset = tiles_reset;
    tiles_draw(&s, &first);
    ex_init(&ex);
    check("the tiles are won by pointing alone",
          ex_play(&ex, &g, &first, 3000u) == SM_OK && ex.won && ex.levels_done == 3u);
    /* It may win without ever pointing at the frame (its choices among equals are
       uniform), so the claim is about what pointing there did, not that it tried. */
    check("pointing at the frame round the edge never does anything",
          ex.point_nothing[5] == ex.point_tried[5]);
    ex_report(stdout, &ex);
  }

  {
    lock_t s;
    memset(&s, 0, sizeof s);
    s.endless = 1;
    lock_start(&s);
    memset(&g, 0, sizeof g);
    g.state = &s;
    g.available[0] = 1u; g.available[1] = 2u; g.available[2] = 3u; g.available[3] = 4u;
    g.n_available = 4u;
    g.act = lock_act;
    g.reset = lock_reset;
    lock_draw(&s, &first);
    ex_init(&ex);
    check("a lock with no way to open it is reported exhausted, not played forever",
          ex_play(&ex, &g, &first, 100000u) == SM_OK && !ex.won && ex.exhausted &&
              ex.actions < 1000u);
  }

  check("NULL arguments are checked errors",
        ex_play(0, &g, &first, 1u) == SM_ERR_NULL_ARGUMENT &&
            ex_play(&ex, 0, &first, 1u) == SM_ERR_NULL_ARGUMENT &&
            ex_play(&ex, &g, 0, 1u) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
