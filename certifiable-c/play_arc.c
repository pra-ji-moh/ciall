/*
 * play_arc.c -- the child, playing a real ARC-AGI-3 game it can only reach
 * through frames and actions.
 *
 * The game runs in the official engine, somewhere else. This program never
 * sees the game's code or its rules. It is handed exactly what ARC-AGI-3
 * hands any agent, over its standard input, and answers with actions on its
 * standard output:
 *
 *   to it     BASE <n> <actions a person needed for each level>   (once, first)
 *             OBS <state> <levels_completed> <height> <width> <n> <action>...
 *             then <height> lines of <width> hexadecimal colours
 *             and, just before the OBS of an act that ended a level:
 *             END <height> <width>, then the board as that act left it, before
 *             the next level was drawn over it
 *             state: 0 still playing, 1 won, 2 game over
 *   from it   ACT <n>          take action n
 *             ACT 6 <x> <y>    point at column x, row y
 *             RESET            start the level again
 *             QUIT             it is done
 *
 * It plays by reasoning over the situations it can reach (smarsh_explore.h):
 * what it has tried where, and the shortest way to what it has not. Its own
 * account of the game goes to standard error, which the bridge keeps.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smarsh_explore.h"

typedef struct {
  unsigned levels_completed;
  unsigned game_overs;
  unsigned sent;
  int won;
  int broken;
  unsigned n_base;
  unsigned base[EX_MAX_LEVELS];
} arc_link_t;

static arc_link_t LINK;

static int read_grid(unsigned h, unsigned w, pl_frame_t *out) {
  unsigned r, c;
  memset(out, 0, sizeof(*out));
  out->h = h;
  out->w = w;
  for (r = 0u; r < h; r++) {
    char row[PL_SIZE + 2u];
    if (scanf("%65s", row) != 1 || strlen(row) != w) return -1;
    for (c = 0u; c < w; c++) {
      char ch = row[c];
      unsigned v = (ch >= '0' && ch <= '9') ? (unsigned)(ch - '0')
                 : (ch >= 'a' && ch <= 'f') ? (unsigned)(ch - 'a' + 10)
                 : (ch >= 'A' && ch <= 'F') ? (unsigned)(ch - 'A' + 10) : 0u;
      out->c[r][c] = (unsigned char)v;
    }
  }
  return 0;
}

static pl_frame_t ENDED_ON;   /* the board an ending act left, when the bridge sent it */
static int HAS_END;

static int read_obs(ex_game_t *g, pl_frame_t *out) {
  char word[16];
  int state;
  unsigned levels, h, w, n, i, r, c;
  HAS_END = 0;
  if (scanf("%15s", word) != 1) return -1;
  if (strcmp(word, "END") == 0) {
    if (scanf("%u %u", &h, &w) != 2 || h == 0u || w == 0u || h > PL_SIZE || w > PL_SIZE) return -1;
    if (read_grid(h, w, &ENDED_ON) != 0) return -1;
    HAS_END = 1;
    if (scanf("%15s", word) != 1) return -1;
  }
  if (strcmp(word, "OBS") != 0) return -1;
  if (scanf("%d %u %u %u %u", &state, &levels, &h, &w, &n) != 5) return -1;
  if (h == 0u || w == 0u || h > PL_SIZE || w > PL_SIZE || n > 8u) return -1;
  g->n_available = n;
  for (i = 0u; i < n; i++) {
    if (scanf("%u", &g->available[i]) != 1) return -1;
  }
  memset(out, 0, sizeof(*out));
  out->h = h;
  out->w = w;
  for (r = 0u; r < h; r++) {
    char row[PL_SIZE + 2u];
    if (scanf("%65s", row) != 1 || strlen(row) != w) return -1;
    for (c = 0u; c < w; c++) {
      char ch = row[c];
      unsigned v = (ch >= '0' && ch <= '9') ? (unsigned)(ch - '0')
                 : (ch >= 'a' && ch <= 'f') ? (unsigned)(ch - 'a' + 10)
                 : (ch >= 'A' && ch <= 'F') ? (unsigned)(ch - 'A' + 10) : 0u;
      out->c[r][c] = (unsigned char)v;
    }
  }
  LINK.levels_completed = levels;
  return state;
}

static int send(ex_game_t *g, const char *cmd, pl_frame_t *out) {
  printf("%s\n", cmd);
  fflush(stdout);
  LINK.sent++;
  return read_obs(g, out);
}

static int arc_act(ex_game_t *g, unsigned action, unsigned x, unsigned y, pl_frame_t *out) {
  char cmd[32];
  unsigned before = LINK.levels_completed;
  int state;
  if (action == 6u) sprintf(cmd, "ACT 6 %u %u", x, y);
  else sprintf(cmd, "ACT %u", action);
  state = send(g, cmd, out);
  if (state < 0) {
    LINK.broken = 1;
    return 2;
  }
  if (HAS_END) ex_saw_final(&ENDED_ON);   /* the board the ending left, before the next was drawn */
  if (state == 1) {
    LINK.won = 1;
    return 2;
  }
  if (state == 2) {
    /* the engine needs a reset to go on: the world starts over, not by its choice */
    LINK.game_overs++;
    if (send(g, "RESET", out) < 0) {
      LINK.broken = 1;
      return 2;
    }
    return 3;
  }
  return LINK.levels_completed > before ? 1 : 0;
}

static void arc_reset(ex_game_t *g, pl_frame_t *out) {
  if (send(g, "RESET", out) < 0) LINK.broken = 1;
}

int main(int argc, char **argv) {
  static pl_frame_t first;
  ex_game_t g;
  ex_explorer_t ex;
  unsigned budget = argc > 1 ? (unsigned)strtoul(argv[1], 0, 10) : 2000u;
  unsigned i;
  char word[16];

  memset(&g, 0, sizeof g);
  if (scanf("%15s", word) != 1 || strcmp(word, "BASE") != 0 || scanf("%u", &LINK.n_base) != 1) {
    fprintf(stderr, "  the bridge sent nothing it could read\n");
    printf("QUIT\n");
    return 2;
  }
  for (i = 0u; i < LINK.n_base; i++) {
    unsigned v;
    if (scanf("%u", &v) != 1) break;
    if (i < EX_MAX_LEVELS) LINK.base[i] = v;
  }
  if (LINK.n_base > EX_MAX_LEVELS) LINK.n_base = EX_MAX_LEVELS;
  if (read_obs(&g, &first) < 0) {
    fprintf(stderr, "  the bridge sent nothing it could read\n");
    printf("QUIT\n");
    return 2;
  }
  fprintf(stderr, "  frame %u by %u, actions offered:", first.h, first.w);
  for (i = 0u; i < g.n_available; i++) fprintf(stderr, " %u", g.available[i]);
  fprintf(stderr, "\n");

  g.state = &LINK;
  g.act = arc_act;
  g.reset = arc_reset;
  ex_init(&ex);
  ex.thinking = stderr;   /* its questions and answers go into the record of the game */
  {
    /* what it settled about this game in earlier runs, if it is told where that is kept */
    const char *mind = getenv("CIALL_MIND");
    FILE *in = mind != 0 ? fopen(mind, "r") : 0;
    if (in != 0) {
      (void)ex_load(&ex, in);
      fclose(in);
    }
  }
  ex_play(&ex, &g, &first, budget);
  {
    const char *mind = getenv("CIALL_MIND");
    FILE *out = mind != 0 ? fopen(mind, "w") : 0;
    if (out != 0) {
      (void)ex_save(&ex, out);
      fclose(out);
    }
  }

  fprintf(stderr, "  %s; %u sent to the engine, games over %u\n",
          LINK.won ? "WON" : (LINK.broken ? "the link broke" : "not won"), LINK.sent,
          LINK.game_overs);
  ex_report(stderr, &ex);
  if (LINK.n_base > 0u) {
    unsigned k, mine = 0u, theirs = 0u;
    unsigned done = ex.levels_done < LINK.n_base ? ex.levels_done : LINK.n_base;
    fprintf(stderr, "  against a person:");
    for (k = 0u; k < done; k++) {
      fprintf(stderr, " level %u took it %u, a person %u;", k + 1u, ex.level_actions[k],
              LINK.base[k]);
      mine += ex.level_actions[k];
      theirs += LINK.base[k];
    }
    if (done == 0u) fprintf(stderr, " no level ended; a person needed %u for the first", LINK.base[0]);
    fprintf(stderr, "\n  levels finished: %u of %u", done, LINK.n_base);
    if (theirs > 0u) fprintf(stderr, "; on those, %u actions against a person's %u", mine, theirs);
    fprintf(stderr, "\n");
  }
  if (!LINK.broken) {
    printf("QUIT\n");
    fflush(stdout);
  }
  return 0;
}
