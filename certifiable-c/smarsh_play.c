/*
 * smarsh_play.c -- the child in a world it can only see and act in.
 *
 * See smarsh_play.h. Everything below the line "what it acquires" is an
 * elimination over what it has done and seen; nothing about colours,
 * shapes, directions or goals is written in.
 */

#include "smarsh_play.h"

#include <stdio.h>
#include <string.h>

#define PL_REACH 8          /* largest displacement it looks for between frames */
#define PL_RETRY 3u         /* an action that never moved it is retried this often */

/* ---- seeing a thing move -------------------------------------------------- */

static int changed(const pl_frame_t *a, const pl_frame_t *b, int r, int c) {
  return a->c[r][c] != b->c[r][c];
}

static int inside(const pl_frame_t *f, int r, int c) {
  return r >= 0 && c >= 0 && r < (int)f->h && c < (int)f->w;
}

/* Does the displaced cell at (r, c) share a fate under (dr, dc)? */
static int shares_fate(const pl_frame_t *a, const pl_frame_t *b, int r, int c, int dr, int dc) {
  int sr = r - dr, sc = c - dc;
  if (!changed(a, b, r, c) || !inside(a, sr, sc)) return 0;
  return b->c[r][c] == a->c[sr][sc] && changed(a, b, sr, sc);
}

static unsigned TMP_N;
static int TMP_DR[PL_BODY_CELLS], TMP_DC[PL_BODY_CELLS];
static unsigned char TMP_COL[PL_BODY_CELLS];

/* The cells sharing that fate, as a shape anchored at its top-left. */
static int shape_of(const pl_frame_t *a, const pl_frame_t *b, int dr, int dc) {
  int r, c, top = (int)b->h, left = (int)b->w;
  TMP_N = 0u;
  for (r = 0; r < (int)b->h; r++) {
    for (c = 0; c < (int)b->w; c++) {
      if (!shares_fate(a, b, r, c, dr, dc)) continue;
      if (r < top) top = r;
      if (c < left) left = c;
    }
  }
  for (r = 0; r < (int)b->h; r++) {
    for (c = 0; c < (int)b->w; c++) {
      if (!shares_fate(a, b, r, c, dr, dc)) continue;
      if (TMP_N >= PL_BODY_CELLS) return 0;
      TMP_DR[TMP_N] = r - top;
      TMP_DC[TMP_N] = c - left;
      TMP_COL[TMP_N] = b->c[r][c];
      TMP_N++;
    }
  }
  return TMP_N > 0u;
}

/* How many places in the frame that shape can be found, up to a limit. */
static unsigned occurrences(const pl_frame_t *f, unsigned limit) {
  int r, c;
  unsigned i, found = 0u;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) {
      int ok = 1;
      for (i = 0u; i < TMP_N && ok; i++) {
        int rr = r + TMP_DR[i], cc = c + TMP_DC[i];
        if (!inside(f, rr, cc) || f->c[rr][cc] != TMP_COL[i]) ok = 0;
      }
      if (ok && ++found >= limit) return found;
    }
  }
  return found;
}

/*
 * Common fate. Between two frames, find the displacement that the largest
 * set of changed cells agrees on: a cell arrived here carrying the colour
 * that was, one displacement back, in a cell that has since changed too.
 * Those cells moved together, so they are one thing. Nothing about colour
 * or connection decides it.
 *
 * One pair of frames can fit two accounts equally well: a thing moved one
 * way, or the ground it uncovered moved the other. Both are kept as
 * candidates, and the one that can be found again is chosen: a shape that
 * matches in many places at once cannot be followed, so it cannot be
 * what it is. That is counted off the frame, not assumed about colours.
 */
static int common_fate(pl_child_t *ch, const pl_frame_t *a, const pl_frame_t *b, int *out_dr,
                       int *out_dc) {
  int dr, dc, r, c, best_dr = 0, best_dc = 0;
  unsigned best = 0u, fewest = 0xFFFFFFFFu;
  unsigned count[2 * PL_REACH + 1][2 * PL_REACH + 1];
  for (dr = -PL_REACH; dr <= PL_REACH; dr++) {
    for (dc = -PL_REACH; dc <= PL_REACH; dc++) {
      unsigned n = 0u;
      if (!(dr == 0 && dc == 0)) {
        for (r = 0; r < (int)b->h; r++) {
          for (c = 0; c < (int)b->w; c++) n += (unsigned)shares_fate(a, b, r, c, dr, dc);
        }
      }
      count[dr + PL_REACH][dc + PL_REACH] = n;
      if (n > best) best = n;
    }
  }
  if (best == 0u || best > PL_BODY_CELLS) return 0;

  for (dr = -PL_REACH; dr <= PL_REACH; dr++) {
    for (dc = -PL_REACH; dc <= PL_REACH; dc++) {
      unsigned seen;
      if (count[dr + PL_REACH][dc + PL_REACH] != best) continue;
      if (!shape_of(a, b, dr, dc)) continue;
      seen = occurrences(b, fewest);
      if (seen == 0u || seen >= fewest) continue;
      fewest = seen;
      best_dr = dr;
      best_dc = dc;
    }
  }
  if (fewest == 0xFFFFFFFFu) return 0;

  shape_of(a, b, best_dr, best_dc);
  {
    unsigned i;
    for (i = 0u; i < TMP_N; i++) {
      ch->body_dr[i] = TMP_DR[i];
      ch->body_dc[i] = TMP_DC[i];
      ch->body_col[i] = TMP_COL[i];
    }
    ch->body_n = TMP_N;
    ch->have_body = 1;
  }
  *out_dr = best_dr;
  *out_dc = best_dc;
  return 1;
}

/* Where is it in this frame? Its top-left, if its shape is there whole. */
static int locate(const pl_child_t *ch, const pl_frame_t *f, int *out_r, int *out_c) {
  int r, c;
  unsigned i;
  if (!ch->have_body) return 0;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) {
      int ok = 1;
      for (i = 0u; i < ch->body_n && ok; i++) {
        int rr = r + ch->body_dr[i], cc = c + ch->body_dc[i];
        if (!inside(f, rr, cc) || f->c[rr][cc] != ch->body_col[i]) ok = 0;
      }
      if (ok) {
        *out_r = r;
        *out_c = c;
        return 1;
      }
    }
  }
  return 0;
}

static int is_own_cell(const pl_child_t *ch, int at_r, int at_c, int r, int c) {
  unsigned i;
  for (i = 0u; i < ch->body_n; i++) {
    if (at_r + ch->body_dr[i] == r && at_c + ch->body_dc[i] == c) return 1;
  }
  return 0;
}

/*
 * The colours it would be covering with its top-left at (r, c), read off a
 * frame, not counting the cells it occupies there now at (own_r, own_c).
 * Returns 0 when any cell would fall outside the world.
 */
static int covered(const pl_child_t *ch, const pl_frame_t *f, int r, int c, int own_r, int own_c,
                   int have_own, int *present) {
  unsigned i;
  int k;
  for (k = 0; k < (int)PL_COLOURS; k++) present[k] = 0;
  for (i = 0u; i < ch->body_n; i++) {
    int rr = r + ch->body_dr[i], cc = c + ch->body_dc[i];
    if (!inside(f, rr, cc)) return 0;
    if (have_own && is_own_cell(ch, own_r, own_c, rr, cc)) continue;
    present[f->c[rr][cc]] = 1;
  }
  return 1;
}

/* ---- what it acquires ------------------------------------------------------ */

void pl_child_init(pl_child_t *ch) {
  unsigned k;
  if (ch == 0) return;
  memset(ch, 0, sizeof(*ch));
  for (k = 0u; k < PL_COLOURS; k++) {
    unsigned j;
    ch->end_alive[k] = 1;
    ch->pend_alive[k] = 1;
    for (j = 0u; j < PL_COLOURS; j++) ch->cause_to[k][j] = -1;
  }
}

/* A move went nowhere though the action has moved it before: something is in the way. */
static void learn_blocked(pl_child_t *ch, const pl_frame_t *f, int r, int c, unsigned a) {
  int present[PL_COLOURS];
  unsigned k;
  if (!covered(ch, f, r + ch->eff_dr[a], c + ch->eff_dc[a], r, c, 1, present)) return; /* the edge */
  ch->failures++;
  /* anything in the way may be what blocks. Several colours can block,
     so this is not narrowed to one: a colour stops being suspected only
     when it is moved onto. */
  for (k = 0u; k < PL_COLOURS; k++) {
    if (present[k]) ch->wall_alive[k] = 1;
  }
}

/* It moved onto these colours, so none of them blocks. */
static void learn_passed(pl_child_t *ch, const int *present) {
  unsigned k;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (present[k]) ch->wall_ruled_out[k] = 1;
  }
}

/* It covered these, and the level did or did not end. */
static void learn_ending(pl_child_t *ch, const int *present, int ended) {
  unsigned k;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (present[k]) ch->end_seen[k] = 1;
    if (ended) {
      if (!present[k]) ch->end_alive[k] = 0;  /* not covered at this ending */
    } else {
      if (present[k]) ch->end_alive[k] = 0;   /* covered, and nothing ended */
    }
  }
  if (ended) ch->endings++;
}

static int is_push_colour(const pl_child_t *ch, unsigned k) {
  unsigned i;
  if (!ch->push_known) return 0;
  for (i = 0u; i < ch->push_n; i++) {
    if (ch->push_col[i] == k) return 1;
  }
  return 0;
}

/* a colour it has found in the way, never moved onto, and has not pushed */
static int is_wall(const pl_child_t *ch, unsigned k) {
  return ch->wall_alive[k] && !ch->wall_ruled_out[k] && !is_push_colour(ch, k);
}

/* Where a shape is in a frame: its top-left, if it is there whole. */
static int locate_shape(unsigned n, const int *sdr, const int *sdc, const unsigned char *scol,
                        const pl_frame_t *f, int *out_r, int *out_c) {
  int r, c;
  unsigned i;
  if (n == 0u) return 0;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) {
      int ok = 1;
      for (i = 0u; i < n && ok; i++) {
        int rr = r + sdr[i], cc = c + sdc[i];
        if (!inside(f, rr, cc) || f->c[rr][cc] != scol[i]) ok = 0;
      }
      if (ok) {
        *out_r = r;
        *out_c = c;
        return 1;
      }
    }
  }
  return 0;
}

/* ---- consequences: what else changed, and why ------------------------------ */

static unsigned char MINE[PL_SIZE][PL_SIZE];   /* cells already accounted for */
/* set when something it did changed the world beyond its own moving */
static int WORLD_CHANGED;

static void mark_body(const pl_child_t *ch, int r, int c) {
  unsigned i;
  for (i = 0u; i < ch->body_n; i++) {
    int rr = r + ch->body_dr[i], cc = c + ch->body_dc[i];
    if (rr >= 0 && cc >= 0 && rr < (int)PL_SIZE && cc < (int)PL_SIZE) MINE[rr][cc] = 1u;
  }
}

/*
 * It moved from (r0, c0) to (r1, c1) while covering the colours in
 * `under`. Its own cells explain some of what changed. Of the rest:
 *
 *   cells that moved the same way it did, from cells ahead of it, are a
 *   thing it pushed. What that thing now covers did not end the level.
 *
 *   anything else that changed colour, it caused by what it was covering
 *   (there is nothing else it did). Each colour it covered is kept as a
 *   possible cause while every change of that kind came with it, and
 *   ruled out the moment such a change happened without it.
 */
static void learn_consequences(pl_child_t *ch, const pl_frame_t *a, const pl_frame_t *b, int r0,
                               int c0, int r1, int c1, const int *under) {
  int dr = r1 - r0, dc = c1 - c0, r, c;
  unsigned pushed = 0u;
  int top = (int)PL_SIZE, left = (int)PL_SIZE;
  signed char becomes[PL_COLOURS];
  unsigned k, j;
  int any_change = 0;

  memset(MINE, 0, sizeof MINE);
  mark_body(ch, r0, c0);
  mark_body(ch, r1, c1);

  if (dr != 0 || dc != 0) {
    for (r = 0; r < (int)b->h; r++) {
      for (c = 0; c < (int)b->w; c++) {
        int sr = r - dr, sc = c - dc;
        if (MINE[r][c] || !changed(a, b, r, c) || !inside(a, sr, sc)) continue;
        if (b->c[r][c] != a->c[sr][sc] || !changed(a, b, sr, sc)) continue;
        if (is_own_cell(ch, r0, c0, sr, sc)) continue;
        pushed++;
        if (r < top) top = r;
        if (c < left) left = c;
      }
    }
  }
  if (pushed > 0u && pushed <= PL_BODY_CELLS) {
    int covered_x[PL_COLOURS];
    unsigned n = 0u;
    for (k = 0u; k < PL_COLOURS; k++) covered_x[k] = 0;
    ch->pushes++;
    WORLD_CHANGED = 1;   /* the pushed thing is somewhere new */
    for (r = 0; r < (int)b->h; r++) {
      for (c = 0; c < (int)b->w; c++) {
        int sr = r - dr, sc = c - dc;
        if (MINE[r][c] || !changed(a, b, r, c) || !inside(a, sr, sc)) continue;
        if (b->c[r][c] != a->c[sr][sc] || !changed(a, b, sr, sc)) continue;
        if (is_own_cell(ch, r0, c0, sr, sc)) continue;
        if (!ch->push_known) {
          ch->push_dr[n] = r - top;
          ch->push_dc[n] = c - left;
          ch->push_col[n] = b->c[r][c];
        }
        n++;
        MINE[r][c] = 1u;
        MINE[sr][sc] = 1u;
      }
    }
    if (!ch->push_known) {
      ch->push_n = n;
      ch->push_known = 1;
    }
    /* what the pushed thing arrived on: the colour there before it came */
    for (r = 0; r < (int)b->h; r++) {
      for (c = 0; c < (int)b->w; c++) {
        int sr = r - dr, sc = c - dc;
        int from_own;
        if (!inside(a, sr, sc) || b->c[r][c] != a->c[sr][sc]) continue;
        if (!changed(a, b, r, c) || !changed(a, b, sr, sc)) continue;
        from_own = is_own_cell(ch, r0, c0, sr, sc);
        if (from_own) continue;
        /* a cell the pushed thing already sat on is not a new arrival */
        if (inside(a, r + 0, c + 0) && is_push_colour(ch, a->c[r][c])) continue;
        covered_x[a->c[r][c]] = 1;
      }
    }
    for (k = 0u; k < PL_COLOURS; k++) {
      if (!covered_x[k]) continue;
      ch->pend_seen[k] = 1;
      ch->pend_alive[k] = 0;   /* it arrived there, and nothing ended */
    }
  }

  for (k = 0u; k < PL_COLOURS; k++) becomes[k] = -1;
  for (r = 0; r < (int)b->h; r++) {
    for (c = 0; c < (int)b->w; c++) {
      unsigned from, to;
      if (MINE[r][c] || !changed(a, b, r, c)) continue;
      from = a->c[r][c];
      to = b->c[r][c];
      if (ch->ticks[from]) continue;   /* this colour changes by itself */
      if (becomes[from] == -1) becomes[from] = (signed char)to;
      else if (becomes[from] != (signed char)to) becomes[from] = -3;
      any_change = 1;
    }
  }
  if (!any_change) return;
  ch->causes_seen++;
  WORLD_CHANGED = 1;
  for (j = 0u; j < PL_COLOURS; j++) {
    if (becomes[j] < 0) continue;
    for (k = 0u; k < PL_COLOURS; k++) {
      if (under[k]) {
        if (ch->cause_to[k][j] == -1) ch->cause_to[k][j] = becomes[j];
        else if (ch->cause_to[k][j] != becomes[j]) ch->cause_to[k][j] = -2;
      } else {
        ch->cause_to[k][j] = -2;   /* it happened without this colour being covered */
      }
    }
  }
}

/* colours k whose covering is known to change a colour still in the frame */
static void openers(const pl_child_t *ch, const pl_frame_t *f, int *out) {
  int present[PL_COLOURS];
  unsigned k, j;
  int r, c;
  for (k = 0u; k < PL_COLOURS; k++) {
    present[k] = 0;
    out[k] = 0;
  }
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) present[f->c[r][c]] = 1;
  }
  for (k = 0u; k < PL_COLOURS; k++) {
    for (j = 0u; j < PL_COLOURS; j++) {
      if (ch->cause_to[k][j] >= 0 && present[j] && j != k && !ch->ticks[j]) out[k] = 1;
    }
  }
}

/* ---- deciding what to do ---------------------------------------------------- */

static unsigned char FAILED[PL_SIZE][PL_SIZE];   /* bit per action that failed from here */
static unsigned char VISITED[PL_SIZE][PL_SIZE];
static short PREV_R[PL_SIZE][PL_SIZE], PREV_C[PL_SIZE][PL_SIZE];
static unsigned char PREV_A[PL_SIZE][PL_SIZE];
static unsigned char SEEN_BFS[PL_SIZE][PL_SIZE];
static int QR[PL_SIZE * PL_SIZE], QC[PL_SIZE * PL_SIZE];

/*
 * Over its own account of the world: from where it is, the first action of
 * the shortest way to a place that satisfies `want`. want 1: covering a
 * colour that could still be what ends the level. want 2: a place it has
 * not been. want 3: a place where an action it thinks does nothing has
 * not yet been tried. want 4: covering a colour it knows changes something
 * still in the world. 0 when there is no such way.
 */
static unsigned plan(const pl_child_t *ch, const pl_frame_t *f, unsigned n_actions, int r0, int c0,
                     int want) {
  int head = 0, tail = 0;
  int opens[PL_COLOURS];
  if (want == 4) openers(ch, f, opens);
  memset(SEEN_BFS, 0, sizeof SEEN_BFS);
  SEEN_BFS[r0][c0] = 1;
  QR[tail] = r0;
  QC[tail] = c0;
  tail++;
  while (head < tail) {
    int r = QR[head], c = QC[head];
    unsigned a;
    head++;
    if (!(r == r0 && c == c0)) {
      int present[PL_COLOURS], hit = 0;
      unsigned k;
      if (want == 1 && covered(ch, f, r, c, r0, c0, 1, present)) {
        for (k = 0u; k < PL_COLOURS; k++) {
          if (present[k] && ch->end_alive[k] && !is_wall(ch, k)) hit = 1;
        }
      }
      if (want == 2 && !VISITED[r][c]) hit = 1;
      if (want == 4 && covered(ch, f, r, c, r0, c0, 1, present)) {
        for (k = 0u; k < PL_COLOURS; k++) {
          if (present[k] && opens[k]) hit = 1;
        }
      }
      if (want == 3) {
        unsigned t;
        for (t = 1u; t <= n_actions; t++) {
          if ((ch->effect[t] == PL_STILL || ch->effect[t] == PL_MIXED) &&
              !(FAILED[r][c] & (1u << t)))
            hit = 1;
        }
      }
      if (hit) {
        /* walk back to the first step */
        int br = r, bc = c;
        unsigned first = PREV_A[r][c];
        while (!(PREV_R[br][bc] == r0 && PREV_C[br][bc] == c0)) {
          int pr = PREV_R[br][bc], pc = PREV_C[br][bc];
          br = pr;
          bc = pc;
          first = PREV_A[br][bc];
        }
        return first;
      }
    }
    for (a = 1u; a <= n_actions; a++) {
      int nr, nc, present[PL_COLOURS], blocked = 0;
      unsigned k;
      if (ch->effect[a] != PL_MOVES) continue;
      if (r == r0 && c == c0 && (FAILED[r][c] & (1u << a))) continue;
      nr = r + ch->eff_dr[a];
      nc = c + ch->eff_dc[a];
      if (!inside(f, nr, nc) || SEEN_BFS[nr][nc]) continue;
      if (FAILED[r][c] & (1u << a)) continue;
      if (!covered(ch, f, nr, nc, r0, c0, 1, present)) continue;
      for (k = 0u; k < PL_COLOURS; k++) {
        /* stepping into something it pushes changes the world in a way this
           plan does not follow, so it is not stepped into here */
        if (present[k] && (is_wall(ch, k) || is_push_colour(ch, k))) blocked = 1;
      }
      if (blocked) continue;
      SEEN_BFS[nr][nc] = 1;
      PREV_R[nr][nc] = (short)r;
      PREV_C[nr][nc] = (short)c;
      PREV_A[nr][nc] = (unsigned char)a;
      QR[tail] = nr;
      QC[tail] = nc;
      tail++;
    }
  }
  return 0u;
}

/*
 * Planning with the thing it pushes. A state is where it is and where the
 * pushed thing is. Stepping into the pushed thing moves it the same way,
 * unless what is beyond it blocks. The places it and the pushed thing
 * start from are read as open, since what is under them cannot be seen.
 * The aim: the pushed thing covering a colour that could still be what
 * ends the level.
 */
#define PL_PUSH_Q 131072u
static unsigned char PSEEN[1u << 21];
static unsigned PQS[PL_PUSH_Q];
static int PQP[PL_PUSH_Q];
static unsigned char PQA[PL_PUSH_Q];

static unsigned pkey(int r, int c, int br, int bc) {
  return ((unsigned)r << 18) | ((unsigned)c << 12) | ((unsigned)br << 6) | (unsigned)bc;
}

static int in_shape(unsigned n, const int *sdr, const int *sdc, int at_r, int at_c, int r, int c) {
  unsigned i;
  for (i = 0u; i < n; i++) {
    if (at_r + sdr[i] == r && at_c + sdc[i] == c) return 1;
  }
  return 0;
}

/* the colour at a cell, or -1 where it or the pushed thing started (unseen) */
static int seen_colour(const pl_child_t *ch, const pl_frame_t *f, int r0, int c0, int b0r, int b0c,
                       int r, int c) {
  if (!inside(f, r, c)) return -2;
  if (in_shape(ch->body_n, ch->body_dr, ch->body_dc, r0, c0, r, c)) return -1;
  if (in_shape(ch->push_n, ch->push_dr, ch->push_dc, b0r, b0c, r, c)) return -1;
  return f->c[r][c];
}

static unsigned plan_push(const pl_child_t *ch, const pl_frame_t *f, unsigned n_actions, int r0,
                          int c0) {
  int b0r, b0c, any_goal = 0;
  unsigned head = 0u, tail = 0u, k, found = PL_PUSH_Q;
  if (!ch->push_known) return 0u;
  if (!locate_shape(ch->push_n, ch->push_dr, ch->push_dc, ch->push_col, f, &b0r, &b0c)) return 0u;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (ch->pend_alive[k] && !is_wall(ch, k) && !is_push_colour(ch, k)) any_goal = 1;
  }
  if (!any_goal) return 0u;

  PQS[0] = pkey(r0, c0, b0r, b0c);
  PQP[0] = -1;
  PQA[0] = 0u;
  PSEEN[PQS[0] >> 3] |= (unsigned char)(1u << (PQS[0] & 7u));
  tail = 1u;
  while (head < tail && found == PL_PUSH_Q) {
    unsigned st = PQS[head];
    int r = (int)((st >> 18) & 63u), c = (int)((st >> 12) & 63u);
    int br = (int)((st >> 6) & 63u), bc = (int)(st & 63u);
    unsigned a;
    for (a = 1u; a <= n_actions && found == PL_PUSH_Q; a++) {
      int nr, nc, nbr = br, nbc = bc, moved_box = 0, ok = 1;
      unsigned i, key;
      if (ch->effect[a] != PL_MOVES) continue;
      if (head == 0u && (FAILED[r][c] & (1u << a))) continue;   /* it failed from here */
      nr = r + ch->eff_dr[a];
      nc = c + ch->eff_dc[a];
      for (i = 0u; i < ch->body_n && ok; i++) {
        int rr = nr + ch->body_dr[i], cc = nc + ch->body_dc[i];
        int col = seen_colour(ch, f, r0, c0, b0r, b0c, rr, cc);
        if (col == -2) ok = 0;
        else if (in_shape(ch->push_n, ch->push_dr, ch->push_dc, br, bc, rr, cc)) moved_box = 1;
        else if (col >= 0 && (is_wall(ch, (unsigned)col) || is_push_colour(ch, (unsigned)col))) ok = 0;
      }
      if (!ok) continue;
      if (moved_box) {
        nbr = br + ch->eff_dr[a];
        nbc = bc + ch->eff_dc[a];
        for (i = 0u; i < ch->push_n && ok; i++) {
          int rr = nbr + ch->push_dr[i], cc = nbc + ch->push_dc[i];
          int col = seen_colour(ch, f, r0, c0, b0r, b0c, rr, cc);
          if (col == -2) ok = 0;
          else if (in_shape(ch->body_n, ch->body_dr, ch->body_dc, nr, nc, rr, cc)) ok = 0;
          else if (col >= 0 && (is_wall(ch, (unsigned)col) || is_push_colour(ch, (unsigned)col))) ok = 0;
        }
        if (!ok) continue;
      }
      if (nr < 0 || nc < 0 || nbr < 0 || nbc < 0 || nr > 63 || nc > 63 || nbr > 63 || nbc > 63) continue;
      key = pkey(nr, nc, nbr, nbc);
      if (PSEEN[key >> 3] & (1u << (key & 7u))) continue;
      if (tail >= PL_PUSH_Q) continue;
      PSEEN[key >> 3] |= (unsigned char)(1u << (key & 7u));
      PQS[tail] = key;
      PQP[tail] = (int)head;
      PQA[tail] = (unsigned char)a;
      if (moved_box && !(nbr == b0r && nbc == b0c)) {
        for (i = 0u; i < ch->push_n; i++) {
          int col = seen_colour(ch, f, r0, c0, b0r, b0c, nbr + ch->push_dr[i], nbc + ch->push_dc[i]);
          if (col >= 0 && ch->pend_alive[col] && !is_wall(ch, (unsigned)col) &&
              !is_push_colour(ch, (unsigned)col))
            found = tail;
        }
      }
      tail++;
    }
    head++;
  }
  /* clear only what was marked */
  {
    unsigned i;
    for (i = 0u; i < tail; i++) PSEEN[PQS[i] >> 3] = 0u;
  }
  if (found == PL_PUSH_Q) return 0u;
  while (PQP[found] != 0 && PQP[found] != -1) found = (unsigned)PQP[found];
  return PQA[found];
}

static unsigned choose(pl_child_t *ch, const pl_frame_t *f, unsigned n_actions, int *have_pos,
                       int *pr, int *pc) {
  unsigned a;
  *have_pos = ch->have_body && locate(ch, f, pr, pc);

  /* before it knows which cells are itself, it can only try things */
  if (!*have_pos) {
    if (ch->have_body) {
      /* the account of what it is no longer matches anything it sees: the
         cells that moved together may have been it and something else at
         once. The account is dropped and found again from the next move. */
      ch->lost_itself++;
      ch->have_body = 0;
    }
    for (a = 1u; a <= n_actions; a++) {
      if (ch->moved[a] + ch->stayed[a] == 0u) return a;
    }
    return 1u + (ch->actions_taken % n_actions);
  }

  /* an action it has never taken is where most can be learned */
  for (a = 1u; a <= n_actions; a++) {
    if (ch->effect[a] == PL_UNTRIED) return a;
  }
  /* one that has never moved it may only have been blocked: try it elsewhere */
  for (a = 1u; a <= n_actions; a++) {
    if (ch->effect[a] == PL_STILL && ch->stayed[a] < PL_RETRY &&
        !(FAILED[*pr][*pc] & (1u << a)))
      return a;
  }
  /* push something onto what could still be the ending */
  a = plan_push(ch, f, n_actions, *pr, *pc);
  if (a != 0u) return a;
  /* go and cover what could still be the ending */
  a = plan(ch, f, n_actions, *pr, *pc, 1);
  if (a != 0u) return a;
  /* cover what it knows changes the world, where that change is still to come */
  a = plan(ch, f, n_actions, *pr, *pc, 4);
  if (a != 0u) return a;
  /* nothing left to rule out within reach: go somewhere new */
  a = plan(ch, f, n_actions, *pr, *pc, 2);
  if (a != 0u) return a;
  /*
   * Nowhere left to go. Something it concluded must be wrong, and the
   * weakest conclusions are the ones drawn from an action staying put: that
   * only ever proved it did not move from the places it was tried. So those
   * are tried again from here, where they have not failed.
   */
  ch->cornered++;
  for (a = 1u; a <= n_actions; a++) {
    if ((ch->effect[a] == PL_STILL || ch->effect[a] == PL_MIXED) &&
        !(FAILED[*pr][*pc] & (1u << a)))
      return a;
  }
  /* tried here already: go to where it has not been tried */
  a = plan(ch, f, n_actions, *pr, *pc, 3);
  if (a != 0u) return a;
  return 1u + (ch->actions_taken % n_actions);
}

#define PL_STRANDED 6u   /* cornered this many times running after changing the world */

static sm_status_t play(pl_child_t *ch, pl_game_t *g, unsigned budget, const pl_frame_t *first) {
  static pl_frame_t now, next;
  unsigned level_start = 0u, cornered_run = 0u, changed_here = 0u;
  int have_start = 0, start_r = 0, start_c = 0;   /* where it stood when this level began */

  if (g->n_actions == 0u || g->n_actions > PL_MAX_ACTIONS || g->levels == 0u ||
      g->levels > PL_MAX_LEVELS)
    return SM_ERR_DOMAIN_TOO_LARGE;

  if (first != 0) {
    now = *first;   /* the world was already started elsewhere */
  } else {
    g->reset(g, &now);
  }
  memset(FAILED, 0, sizeof FAILED);
  memset(VISITED, 0, sizeof VISITED);
  ch->level_shortest[0] = g->shortest ? g->shortest(g) : 0u;

  while (ch->actions_taken < budget) {
    int have_pos, r0 = 0, c0 = 0, outcome;
    unsigned before = ch->cornered, pushes_before = ch->pushes, causes_before = ch->causes_seen;
    unsigned a = choose(ch, &now, g->n_actions, &have_pos, &r0, &c0);
    int present[PL_COLOURS];

    cornered_run = (ch->cornered > before) ? cornered_run + 1u : 0u;
    /*
     * Stranded: nowhere left to go, again and again, after it changed the
     * world itself. What it did may have made the level impossible, so it
     * starts the level again. Everything it has worked out is kept; only
     * the layout, where it has been, and what failed where are forgotten.
     */
    if (cornered_run >= PL_STRANDED && changed_here > 0u && g->restart != 0) {
      g->restart(g, &now);
      ch->actions_taken++;
      ch->restarts++;
      cornered_run = 0u;
      changed_here = 0u;
      have_start = 0;
      memset(FAILED, 0, sizeof FAILED);
      memset(VISITED, 0, sizeof VISITED);
      continue;
    }

    if (have_pos) VISITED[r0][c0] = 1;
    if (have_pos && !have_start) {
      have_start = 1;
      start_r = r0;
      start_c = c0;
    }
    outcome = g->act(g, a, &next);
    ch->actions_taken++;

    if (outcome == 3) {
      /* the world started over, and not because of this action: where it is now
         says nothing about what the action does, so nothing is learned from it */
      ch->started_over++;
      have_start = 0;
      now = next;
      memset(FAILED, 0, sizeof FAILED);
      memset(VISITED, 0, sizeof VISITED);
      changed_here = 0u;
      cornered_run = 0u;
      continue;
    }

    if (outcome == 0) {
      int r1, c1;
      if (!ch->have_body) {
        int dr, dc;
        if (common_fate(ch, &now, &next, &dr, &dc)) {
          ch->tries_to_find_itself = ch->actions_taken;
          ch->effect[a] = PL_MOVES;
          ch->eff_dr[a] = dr;
          ch->eff_dc[a] = dc;
          ch->moved[a]++;
          /* where it came from: one displacement back from where it is now */
          if (locate(ch, &next, &r1, &c1)) {
            r0 = r1 - dr;
            c0 = c1 - dc;
            if (!have_start) {   /* where it began is one step back from where it found itself */
              have_start = 1;
              start_r = r0;
              start_c = c0;
            }
            if (covered(ch, &now, r1, c1, r0, c0, 1, present)) {
              learn_passed(ch, present);
              learn_ending(ch, present, 0);
            }
          }
        } else {
          ch->stayed[a]++;
        }
      } else if (have_pos && locate(ch, &next, &r1, &c1)) {
        int dr = r1 - r0, dc = c1 - c0;
        if (dr == 0 && dc == 0) {
          int rr, cc;
          /* it went nowhere, yet something changed: whatever changed does so
             without it, like a clock, and is not a consequence of anything it did */
          for (rr = 0; rr < (int)next.h; rr++) {
            for (cc = 0; cc < (int)next.w; cc++) {
              if (now.c[rr][cc] != next.c[rr][cc] && !is_own_cell(ch, r0, c0, rr, cc)) {
                ch->ticks[now.c[rr][cc]] = 1;
              }
            }
          }
          ch->stayed[a]++;
          FAILED[r0][c0] |= (unsigned char)(1u << a);
          if (ch->effect[a] == PL_UNTRIED) ch->effect[a] = PL_STILL;
          if (ch->effect[a] == PL_MOVES) learn_blocked(ch, &now, r0, c0, a);
        } else if (ch->effect[a] == PL_MOVES && (ch->eff_dr[a] != dr || ch->eff_dc[a] != dc) &&
                   have_start && r1 == start_r && c1 == start_c) {
          /* not the step this action takes, and it lands exactly where the level
             began: it did not move there, the world put it back. Nothing about the
             action is learned from that, and what it knew of this layout stands. */
          ch->started_over++;
          memset(FAILED, 0, sizeof FAILED);
          memset(VISITED, 0, sizeof VISITED);
          changed_here = 0u;
          cornered_run = 0u;
        } else {
          ch->moved[a]++;
          if (ch->effect[a] == PL_UNTRIED || ch->effect[a] == PL_STILL) {
            ch->effect[a] = PL_MOVES;
            ch->eff_dr[a] = dr;
            ch->eff_dc[a] = dc;
          } else if (ch->effect[a] == PL_MOVES && (ch->eff_dr[a] != dr || ch->eff_dc[a] != dc)) {
            ch->effect[a] = PL_MIXED;
            ch->mixed++;
          }
          if (covered(ch, &now, r1, c1, r0, c0, 1, present)) {
            learn_passed(ch, present);
            learn_ending(ch, present, 0);
            learn_consequences(ch, &now, &next, r0, c0, r1, c1, present);
            if (WORLD_CHANGED) {
              /* what it did changed what is in the way. A move that failed from
                 some place before may not fail now, so that is no longer known. */
              memset(FAILED, 0, sizeof FAILED);
              WORLD_CHANGED = 0;
            }
          }
        }
      } else if (have_pos) {
        ch->stayed[a]++;
      }
      if (ch->pushes > pushes_before || ch->causes_seen > causes_before) changed_here++;
      now = next;
      continue;
    }

    /* a level ended. What it was covering is read off the frame before. */
    if (have_pos && ch->effect[a] == PL_MOVES) {
      int r1 = r0 + ch->eff_dr[a], c1 = c0 + ch->eff_dc[a];
      int pushed_suspect = 0, into = 0;
      int br, bc;
      /* if it stepped into the thing it pushes, that thing arrived one step on */
      if (ch->push_known &&
          locate_shape(ch->push_n, ch->push_dr, ch->push_dc, ch->push_col, &now, &br, &bc)) {
        unsigned i;
        for (i = 0u; i < ch->body_n; i++) {
          if (in_shape(ch->push_n, ch->push_dr, ch->push_dc, br, bc, r1 + ch->body_dr[i],
                       c1 + ch->body_dc[i]))
            into = 1;
        }
        if (into) {
          int arrived[PL_COLOURS];
          unsigned k;
          for (k = 0u; k < PL_COLOURS; k++) arrived[k] = 0;
          for (i = 0u; i < ch->push_n; i++) {
            int col = seen_colour(ch, &now, r0, c0, br, bc, br + ch->eff_dr[a] + ch->push_dr[i],
                                  bc + ch->eff_dc[a] + ch->push_dc[i]);
            if (col >= 0) arrived[col] = 1;
          }
          for (k = 0u; k < PL_COLOURS; k++) {
            if (arrived[k]) {
              if (ch->pend_alive[k]) pushed_suspect = 1;
              ch->pend_seen[k] = 1;
            } else {
              ch->pend_alive[k] = 0;   /* it was not there at this ending */
            }
          }
          ch->push_endings++;
        }
      }
      if (covered(ch, &now, r1, c1, r0, c0, 1, present)) {
        unsigned k;
        int suspected = pushed_suspect;
        for (k = 0u; k < PL_COLOURS; k++) {
          if (present[k] && ch->end_alive[k]) suspected = 1;
        }
        if (!suspected) ch->unexplained_endings++;
        learn_passed(ch, present);
        /* when it ended by pushing, what it stood on was only where the pushed
           thing had been, so it says nothing about its own covering */
        if (!into) learn_ending(ch, present, 1);
      }
    } else {
      ch->unexplained_endings++;
    }
    if (ch->levels_done < PL_MAX_LEVELS) {
      ch->level_actions[ch->levels_done] = ch->actions_taken - level_start;
    }
    ch->levels_done++;
    level_start = ch->actions_taken;
    if (outcome == 2 || ch->levels_done >= g->levels) return SM_OK;

    /* a new layout: where it has been and what failed where no longer apply */
    now = next;
    changed_here = 0u;
    cornered_run = 0u;
    have_start = 0;
    memset(FAILED, 0, sizeof FAILED);
    memset(VISITED, 0, sizeof VISITED);
    if (ch->levels_done < PL_MAX_LEVELS) {
      ch->level_shortest[ch->levels_done] = g->shortest ? g->shortest(g) : 0u;
    }
  }
  return SM_ERR_EMPTY_DOMAIN;
}

sm_status_t pl_play(pl_child_t *ch, pl_game_t *g, unsigned budget) {
  if (ch == 0 || g == 0) return SM_ERR_NULL_ARGUMENT;
  return play(ch, g, budget, 0);
}

sm_status_t pl_play_from(pl_child_t *ch, pl_game_t *g, unsigned budget, const pl_frame_t *first) {
  if (ch == 0 || g == 0 || first == 0) return SM_ERR_NULL_ARGUMENT;
  return play(ch, g, budget, first);
}

void pl_report_to(FILE *out, const pl_child_t *ch, const pl_game_t *g) {
  unsigned a, k, total = 0u, best = 0u;
  int any;
  if (out == 0 || ch == 0 || g == 0) return;

  if (ch->have_body) {
    unsigned cols[PL_COLOURS] = {0};
    unsigned i;
    fprintf(out, "  itself: %u cells that moved together, first seen after %u action%s, colours",
           ch->body_n, ch->tries_to_find_itself, ch->tries_to_find_itself == 1u ? "" : "s");
    for (i = 0u; i < ch->body_n; i++) cols[ch->body_col[i]] = 1u;
    for (k = 0u; k < PL_COLOURS; k++) {
      if (cols[k]) fprintf(out, " %u", k);
    }
    fprintf(out, "\n");
  } else {
    fprintf(out, "  itself: never found; nothing it did moved anything\n");
  }
  for (a = 1u; a <= g->n_actions; a++) {
    fprintf(out, "  action %u: ", a);
    switch (ch->effect[a]) {
      case PL_MOVES:
        fprintf(out, "moves it %+d rows, %+d columns (held %u times", ch->eff_dr[a], ch->eff_dc[a],
               ch->moved[a]);
        if (ch->stayed[a] > 0u) fprintf(out, ", blocked %u", ch->stayed[a]);
        fprintf(out, ")\n");
        break;
      case PL_STILL:
        fprintf(out, "does nothing it could see (%u tries)\n", ch->stayed[a]);
        break;
      case PL_MIXED:
        fprintf(out, "moves it by more than one displacement: no single effect\n");
        break;
      default:
        fprintf(out, "never tried\n");
        break;
    }
  }
  fprintf(out, "  what blocks:");
  any = 0;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (is_wall(ch, k)) { fprintf(out, " colour %u", k); any = 1; }
  }
  fprintf(out, "%s  (from %u failed moves)\n", any ? "" : " nothing it can name", ch->failures);
  fprintf(out, "  what ends a level:");
  any = 0;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (ch->end_alive[k] && ch->end_seen[k] && ch->endings > 0u) {
      fprintf(out, " covering colour %u", k);
      any = 1;
    }
  }
  fprintf(out, "%s  (over %u endings)\n", any ? "" : " not settled", ch->endings);
  for (k = 0u; k < ch->levels_done && k < PL_MAX_LEVELS; k++) {
    if (ch->level_shortest[k] > 0u) {
      fprintf(out, "  level %u: %u actions, shortest possible %u\n", k + 1u, ch->level_actions[k],
              ch->level_shortest[k]);
    } else {
      fprintf(out, "  level %u: %u actions\n", k + 1u, ch->level_actions[k]);
    }
    total += ch->level_actions[k];
    best += ch->level_shortest[k];
  }
  if (ch->levels_done > 0u && best > 0u) {
    fprintf(out, "  all levels: %u actions against a shortest possible %u\n", total, best);
  }
}

void pl_report(const pl_child_t *ch, const pl_game_t *g) {
  pl_report_to(stdout, ch, g);
}
