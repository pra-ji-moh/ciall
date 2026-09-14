/*
 * arc_standin.c -- small worlds in the shape of ARC-AGI-3. See arc_standin.h.
 */

#include "arc_standin.h"

#include <string.h>

#define MZ_MAX 16
#define MZ_OFF 8          /* where the maze sits in the 64 by 64 frame */
#define MZ_SCALE 2        /* each maze square is two cells */

#define COL_FLOOR 0u
#define COL_WALL 5u
#define COL_TOP 3u        /* the body's top row */
#define COL_BOTTOM 6u     /* and its bottom row */
#define COL_DECOY 8u

/* W wall, . floor, P where the body starts, T what ends it, D a decoy */
static const char *const LEVEL_A[] = {
    "WWWWWWWWWW",
    "WP...W...W",
    "W.WW.W.W.W",
    "W.W..D.W.W",
    "W.W.WWWW.W",
    "W...W...TW",
    "WWWWWWWWWW",
};
static const char *const LEVEL_B[] = {
    "WWWWWWWWWWWW",
    "WT.....W...W",
    "WWWWWW.W.W.W",
    "W......W.W.W",
    "W.WWWWWW.W.W",
    "W....D...WPW",
    "WWWWWWWWWWWW",
};
static const char *const LEVEL_C[] = {
    "WWWWWWWWWWWW",
    "W....W.....W",
    "W.WW.W.WWW.W",
    "WPW..D...W.W",
    "WWW.WWWW.W.W",
    "W......W.WTW",
    "W.WWWW.....W",
    "WWWWWWWWWWWW",
};

typedef struct {
  const char *const *rows;
  int n_rows, n_cols;
} level_t;

static const level_t LEVELS[3] = {
    {LEVEL_A, 7, 10},
    {LEVEL_B, 7, 12},
    {LEVEL_C, 8, 12},
};

typedef struct {
  unsigned level;
  int pr, pc;             /* the body, in maze squares */
  int dr[6], dc[6];       /* what each action does; action 0 unused */
  unsigned goal_colour;
} maze_state_t;

static maze_state_t MAZE, SCRAMBLED;

static char at(const level_t *lv, int r, int c) {
  if (r < 0 || c < 0 || r >= lv->n_rows || c >= lv->n_cols) return 'W';
  return lv->rows[r][c];
}

static void paint(pl_frame_t *f, int r, int c, unsigned colour) {
  int i, j;
  for (i = 0; i < MZ_SCALE; i++) {
    for (j = 0; j < MZ_SCALE; j++) {
      f->c[MZ_OFF + r * MZ_SCALE + i][MZ_OFF + c * MZ_SCALE + j] = (unsigned char)colour;
    }
  }
}

static void render(const maze_state_t *s, pl_frame_t *f) {
  const level_t *lv = &LEVELS[s->level];
  int r, c;
  f->w = PL_SIZE;
  f->h = PL_SIZE;
  memset(f->c, COL_FLOOR, sizeof f->c);
  for (r = 0; r < lv->n_rows; r++) {
    for (c = 0; c < lv->n_cols; c++) {
      char ch = at(lv, r, c);
      if (ch == 'W') paint(f, r, c, COL_WALL);
      if (ch == 'T') paint(f, r, c, s->goal_colour);
      if (ch == 'D') paint(f, r, c, COL_DECOY);
    }
  }
  /* the body: two cells square, top row one colour, bottom row another */
  {
    int y = MZ_OFF + s->pr * MZ_SCALE, x = MZ_OFF + s->pc * MZ_SCALE;
    f->c[y][x] = (unsigned char)COL_TOP;
    f->c[y][x + 1] = (unsigned char)COL_TOP;
    f->c[y + 1][x] = (unsigned char)COL_BOTTOM;
    f->c[y + 1][x + 1] = (unsigned char)COL_BOTTOM;
  }
}

static void start_level(maze_state_t *s) {
  const level_t *lv = &LEVELS[s->level];
  int r, c;
  for (r = 0; r < lv->n_rows; r++) {
    for (c = 0; c < lv->n_cols; c++) {
      if (at(lv, r, c) == 'P') {
        s->pr = r;
        s->pc = c;
      }
    }
  }
}

static void maze_reset(pl_game_t *g, pl_frame_t *out) {
  maze_state_t *s = (maze_state_t *)g->state;
  s->level = 0u;
  start_level(s);
  render(s, out);
}

static int maze_act(pl_game_t *g, unsigned action, pl_frame_t *out) {
  maze_state_t *s = (maze_state_t *)g->state;
  const level_t *lv = &LEVELS[s->level];
  int nr, nc;
  if (action == 0u || action > g->n_actions) {
    render(s, out);
    return 0;
  }
  nr = s->pr + s->dr[action];
  nc = s->pc + s->dc[action];
  if (at(lv, nr, nc) != 'W') {
    s->pr = nr;
    s->pc = nc;
  }
  if (at(lv, s->pr, s->pc) == 'T') {
    s->level++;
    if (s->level >= g->levels) {
      s->level = g->levels - 1u;
      render(s, out);
      return 2;
    }
    start_level(s);
    render(s, out);
    return 1;
  }
  render(s, out);
  return 0;
}

/* The shortest way through, for scoring against. */
static unsigned maze_shortest(pl_game_t *g) {
  maze_state_t *s = (maze_state_t *)g->state;
  const level_t *lv = &LEVELS[s->level];
  static int dist[MZ_MAX][MZ_MAX], qr[MZ_MAX * MZ_MAX], qc[MZ_MAX * MZ_MAX];
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  int head = 0, tail = 0, r, c, k;
  for (r = 0; r < MZ_MAX; r++) {
    for (c = 0; c < MZ_MAX; c++) dist[r][c] = -1;
  }
  dist[s->pr][s->pc] = 0;
  qr[tail] = s->pr;
  qc[tail] = s->pc;
  tail++;
  while (head < tail) {
    r = qr[head];
    c = qc[head];
    head++;
    if (at(lv, r, c) == 'T') return (unsigned)dist[r][c];
    for (k = 0; k < 4; k++) {
      int nr = r + dr[k], nc = c + dc[k];
      if (at(lv, nr, nc) == 'W' || dist[nr][nc] >= 0) continue;
      dist[nr][nc] = dist[r][c] + 1;
      qr[tail] = nr;
      qc[tail] = nc;
      tail++;
    }
  }
  return 0u;
}

void standin_maze(pl_game_t *g) {
  memset(&MAZE, 0, sizeof MAZE);
  /* 1 up, 2 down, 3 left, 4 right, as a person might guess */
  MAZE.dr[1] = -1; MAZE.dc[1] = 0;
  MAZE.dr[2] = 1;  MAZE.dc[2] = 0;
  MAZE.dr[3] = 0;  MAZE.dc[3] = -1;
  MAZE.dr[4] = 0;  MAZE.dc[4] = 1;
  MAZE.goal_colour = 4u;
  g->state = &MAZE;
  g->n_actions = 4u;
  g->levels = 3u;
  g->reset = maze_reset;
  g->act = maze_act;
  g->shortest = maze_shortest;
  g->restart = 0;
}

void standin_scrambled(pl_game_t *g) {
  memset(&SCRAMBLED, 0, sizeof SCRAMBLED);
  /* 1 right, 2 up, 3 does nothing, 4 left, 5 down */
  SCRAMBLED.dr[1] = 0;  SCRAMBLED.dc[1] = 1;
  SCRAMBLED.dr[2] = -1; SCRAMBLED.dc[2] = 0;
  SCRAMBLED.dr[3] = 0;  SCRAMBLED.dc[3] = 0;
  SCRAMBLED.dr[4] = 0;  SCRAMBLED.dc[4] = -1;
  SCRAMBLED.dr[5] = 1;  SCRAMBLED.dc[5] = 0;
  SCRAMBLED.goal_colour = 11u;
  g->state = &SCRAMBLED;
  g->n_actions = 5u;
  g->levels = 3u;
  g->reset = maze_reset;
  g->act = maze_act;
  g->shortest = maze_shortest;
  g->restart = 0;
}
