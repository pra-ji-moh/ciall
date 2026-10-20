/*
 * smarsh_mind.c -- the child as a mind of parts. See smarsh_mind.h and
 * research/ARCHITECTURE.md.
 *
 * Nothing about any world is written in: not which colour is itself, which
 * blocks, which ends a level, nor which way an action goes. What is built in
 * is what the design says a newborn mind brings: that the world is made of
 * things, that things move or stay, and the blocks its ideas are made from.
 */

#include "smarsh_mind.h"

#include <string.h>

#define MI_W 64
#define MI_COMPS 512u
#define MI_TCELLS 64u        /* cells in a mover's template */
#define MI_REACH 16          /* largest step it looks for between frames */
#define MI_LEGEND 6u
#define MI_HISTORY 4096u     /* actions remembered from one world, for the inventor */
#define MI_DREAMS 6u         /* unexplained worlds kept for sleep */
#define MI_FEATURES 11u
#define MI_UNITS 128u
#define MI_STRANDED 6u
#define MI_GIVE_UP 60u

static int OFF[MI_PARTS];
static int EXPERIMENTING;   /* playing for a self-experiment: nothing is kept for sleep */

void mi_switch(mi_part_t part, int off) {
  if ((unsigned)part < MI_PARTS) OFF[part] = off ? 1 : 0;
}

int mi_switched_off(mi_part_t part) {
  return (unsigned)part < MI_PARTS ? OFF[part] : 0;
}

const char *mi_family_name(unsigned family) {
  switch (family) {
    case MI_COVER: return "a mover covers a colour";
    case MI_ODD: return "a mover covers the odd one out";
    case MI_SEQ_LR: return "a mover covers the legend in order";
    case MI_SEQ_RL: return "a mover covers the legend in reverse order";
    default: return "?";
  }
}

const char *mi_mode_name(unsigned mode) {
  switch (mode) {
    case MI_EXPLORE: return "explore";
    case MI_INTUITION: return "intuition";
    case MI_IMAGINE: return "imagine";
    case MI_ASK: return "ask";
    default: return "?";
  }
}

const char *mi_part_name(unsigned part) {
  switch (part) {
    case MI_PART_INTUITION: return "intuition";
    case MI_PART_INVENTOR: return "inventor";
    case MI_PART_IMAGINATION: return "imagination";
    case MI_PART_PACKING: return "packing";
    case MI_PART_CORRECTION: return "self-correction";
    default: return "?";
  }
}

void mi_library_init(mi_library_t *lib) {
  if (lib == 0) return;
  memset(lib, 0, sizeof(*lib));
}

/* ============================================================================
 * THE PARIETAL MAP: things, ground, movers
 * ==========================================================================*/

typedef struct {
  unsigned char col;
  unsigned n;
  int r0, c0, r1, c1;
  unsigned sig;   /* the shape, as a hash of its cells relative to its corner */
} mi_comp_t;

static short LAB[MI_W][MI_W];
static int STK_R[MI_W * MI_W], STK_C[MI_W * MI_W];

static int inside(const pl_frame_t *f, int r, int c) {
  return r >= 0 && c >= 0 && r < (int)f->h && c < (int)f->w;
}

static unsigned char background(const pl_frame_t *f) {
  unsigned count[PL_COLOURS] = {0}, k, best = 0u;
  int r, c;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) count[f->c[r][c]]++;
  }
  for (k = 1u; k < PL_COLOURS; k++) {
    if (count[k] > count[best]) best = k;
  }
  return (unsigned char)best;
}

/* Connected patches of one colour (4-neighbours), the background left out. */
static unsigned components(const pl_frame_t *f, unsigned char bg, mi_comp_t *out) {
  unsigned n = 0u;
  int r, c;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) LAB[r][c] = -1;
  }
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) {
      int top = 0;
      unsigned char col = f->c[r][c];
      mi_comp_t *k;
      if (col == bg || LAB[r][c] >= 0 || n >= MI_COMPS) continue;
      k = &out[n];
      k->col = col;
      k->n = 0u;
      k->r0 = k->r1 = r;
      k->c0 = k->c1 = c;
      LAB[r][c] = (short)n;
      STK_R[top] = r;
      STK_C[top] = c;
      top++;
      while (top > 0) {
        int rr, cc, d;
        static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
        top--;
        rr = STK_R[top];
        cc = STK_C[top];
        k->n++;
        if (rr < k->r0) k->r0 = rr;
        if (rr > k->r1) k->r1 = rr;
        if (cc < k->c0) k->c0 = cc;
        if (cc > k->c1) k->c1 = cc;
        for (d = 0; d < 4; d++) {
          int nr = rr + dr[d], nc = cc + dc[d];
          if (!inside(f, nr, nc) || LAB[nr][nc] >= 0 || f->c[nr][nc] != col) continue;
          LAB[nr][nc] = (short)n;
          STK_R[top] = nr;
          STK_C[top] = nc;
          top++;
        }
      }
      n++;
    }
  }
  /* the shape of each, relative to its corner */
  {
    unsigned i;
    for (i = 0u; i < n; i++) out[i].sig = 2166136261u;
    for (r = 0; r < (int)f->h; r++) {
      for (c = 0; c < (int)f->w; c++) {
        int l = LAB[r][c];
        unsigned v;
        if (l < 0) continue;
        v = (unsigned)((r - out[l].r0) * 97 + (c - out[l].c0) + 1);
        out[l].sig = (out[l].sig ^ v) * 16777619u;
      }
    }
  }
  return n;
}

static int same_shape(const mi_comp_t *a, const mi_comp_t *b) {
  return a->col == b->col && a->n == b->n && a->sig == b->sig && a->r1 - a->r0 == b->r1 - b->r0 &&
         a->c1 - a->c0 == b->c1 - b->c0;
}

/* A mover: a thing that moves, as the cells it is made of. */
typedef struct {
  unsigned n;
  int dr[MI_TCELLS], dc[MI_TCELLS];
  unsigned char col[MI_TCELLS];
  int r, c;            /* where it is: its template's corner */
  int seen;            /* found in the latest frame */
  /* how it moves: a set of what has not been ruled out */
  int may_table;       /* by a fixed step for each action, unless something blocks it */
  int may_pushed;      /* only when something moving steps into it */
  int known[PL_MAX_ACTIONS + 1u];
  int sr[PL_MAX_ACTIONS + 1u], sc[PL_MAX_ACTIONS + 1u];
  int moved_ever;
} mi_mover_t;

/* Everything about the world it is in now. */
typedef struct {
  unsigned n_actions;
  unsigned char bg;
  unsigned char ground[MI_W][MI_W];   /* what is under everything, as far as it knows */
  unsigned char visited[MI_W][MI_W];  /* where its first mover has been, this level */
  unsigned char failed[MI_W][MI_W];   /* actions that did not move it from here (bit each) */
  unsigned stayed[PL_MAX_ACTIONS + 1u];   /* times an action left its first mover where it was */
  mi_mover_t mv[MI_MAX_MOVERS];
  unsigned n_mv;
  int wall_alive[PL_COLOURS];         /* was in the way when a move failed */
  int wall_out[PL_COLOURS];           /* has been moved onto: does not block */
  unsigned tried[PL_MAX_ACTIONS + 1u];
  /* relations read off the ground at the start of the level */
  int odd;                            /* the odd colour, or -1 */
  unsigned n_legend;
  unsigned char legend[MI_LEGEND];
  int legend_mark;                    /* a mark at an end of the legend: 0 none, 1 left, 2 right */
  /* the language and what is still possible in it */
  int lang[MI_FAMILIES];
  int cover[MI_MAX_MOVERS][PL_COLOURS];
  int fam_alive[MI_FAMILIES][MI_MAX_MOVERS];   /* MI_ODD, MI_SEQ_LR, MI_SEQ_RL */
  unsigned prog[MI_FAMILIES][MI_MAX_MOVERS];   /* progress along the legend */
  unsigned endings;
  int transform;                      /* 0 none; 1 mirror left-right; 2 up-down; 3 both */
  /* for self-correction: which remembered moment ruled each idea out, and how
     much evidence each conclusion that something blocks rests on */
  int cover_by[MI_MAX_MOVERS][PL_COLOURS];
  int fam_by[MI_FAMILIES][MI_MAX_MOVERS];
  unsigned wall_fails[PL_COLOURS];
  int wall_doubted[PL_COLOURS];       /* doubted once, so it was tested in the world */
  int wall_confirmed[PL_COLOURS];     /* doubted, and the world blocked it again: settled */
  int action_doubted[PL_MAX_ACTIONS + 1u];
} mi_world_t;

static mi_world_t W;

/* Where a mover's template matches the frame, nearest to (near_r, near_c). */
static int locate(const mi_mover_t *m, const pl_frame_t *f, int near_r, int near_c, int *out_r,
                  int *out_c) {
  int r, c, best = -1, br = 0, bc = 0;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) {
      unsigned i;
      int ok = 1, d;
      for (i = 0u; i < m->n && ok; i++) {
        int rr = r + m->dr[i], cc = c + m->dc[i];
        if (!inside(f, rr, cc) || f->c[rr][cc] != m->col[i]) ok = 0;
      }
      if (!ok) continue;
      d = (r > near_r ? r - near_r : near_r - r) + (c > near_c ? c - near_c : near_c - c);
      if (best < 0 || d < best) {
        best = d;
        br = r;
        bc = c;
      }
    }
  }
  if (best < 0) return 0;
  *out_r = br;
  *out_c = bc;
  return 1;
}

static int in_template(const mi_mover_t *m, int at_r, int at_c, int r, int c) {
  unsigned i;
  for (i = 0u; i < m->n; i++) {
    if (at_r + m->dr[i] == r && at_c + m->dc[i] == c) return 1;
  }
  return 0;
}

static int under_mover(const mi_world_t *w, int r, int c, int skip) {
  unsigned i;
  for (i = 0u; i < w->n_mv; i++) {
    if ((int)i == skip || !w->mv[i].seen) continue;
    if (in_template(&w->mv[i], w->mv[i].r, w->mv[i].c, r, c)) return 1;
  }
  return 0;
}

/* The ground: every cell not under a mover is what the frame shows. */
static void refresh_ground(mi_world_t *w, const pl_frame_t *f) {
  int r, c;
  for (r = 0; r < (int)f->h; r++) {
    for (c = 0; c < (int)f->w; c++) {
      if (!under_mover(w, r, c, -1)) w->ground[r][c] = f->c[r][c];
    }
  }
}

static mi_comp_t CA[MI_COMPS], CB[MI_COMPS];

/* Two relations, read off the ground: the odd one out, and a legend. */
static void read_relations(mi_world_t *w) {
  static pl_frame_t gf;
  static mi_comp_t g[MI_COMPS];
  unsigned n, i, j;
  int r, c;
  gf.h = gf.w = MI_W;
  for (r = 0; r < MI_W; r++) {
    for (c = 0; c < MI_W; c++) gf.c[r][c] = w->ground[r][c];
  }
  n = components(&gf, w->bg, g);
  w->odd = -1;
  w->n_legend = 0u;
  w->legend_mark = 0;
  /* the odd one: among the colours of small things only, how much of each
     there is. The smallest thing is one unit; the odd colour is the only one
     that comes to exactly one unit, where another comes to several. (Counting
     amounts, not patches: two tiles alike that touch are still two tiles.) */
  {
    unsigned cells[PL_COLOURS] = {0}, k, unit = 0u, ones = 0u, many = 0u, total = 0u;
    int big[PL_COLOURS] = {0}, one = -1;
    for (i = 0u; i < n; i++) {
      cells[g[i].col] += g[i].n;
      total += g[i].n;
    }
    /* the colours that make up much of the picture (walls, floor) are not things */
    for (k = 0u; k < PL_COLOURS; k++) big[k] = 5u * cells[k] > total;
    for (i = 0u; i < n; i++) {
      if (!big[g[i].col] && (unit == 0u || g[i].n < unit)) unit = g[i].n;
    }
    for (k = 0u; k < PL_COLOURS && unit > 0u; k++) {
      if (big[k] || cells[k] == 0u) continue;
      if (cells[k] == unit) {
        ones++;
        one = (int)k;
      } else if (cells[k] >= 2u * unit && cells[k] % unit == 0u) {
        many++;
      }
    }
    if (ones == 1u && many >= 1u) w->odd = one;
#ifdef MI_DEBUG
    fprintf(stderr, "  relations: unit %u ones %u many %u cells", unit, ones, many);
    for (k = 0u; k < PL_COLOURS; k++) if (cells[k]) fprintf(stderr, " %u:%u%s", k, cells[k], big[k] ? "B" : "");
    fprintf(stderr, "\n");
#endif
  }
  /* a legend: a row of small patches of one shape and distinct colours, each
     colour also found somewhere off that row */
  for (i = 0u; i < n && w->n_legend == 0u; i++) {
    unsigned row[MI_LEGEND], nr = 0u, k, ok = 1u;
    if (g[i].n > 16u) continue;
    for (j = 0u; j < n && nr < MI_LEGEND; j++) {
      if (g[j].r0 == g[i].r0 && g[j].n == g[i].n && g[j].sig == g[i].sig) row[nr++] = j;
    }
    if (nr < 2u) continue;
    /* left to right */
    for (j = 1u; j < nr; j++) {
      unsigned t = row[j], q = j;
      while (q > 0u && g[row[q - 1u]].c0 > g[t].c0) {
        row[q] = row[q - 1u];
        q--;
      }
      row[q] = t;
    }
    for (j = 0u; j < nr && ok; j++) {
      int elsewhere = 0;
      for (k = 0u; k < nr; k++) {
        if (k != j && g[row[k]].col == g[row[j]].col) ok = 0;
      }
      for (k = 0u; k < n; k++) {
        if (g[k].col == g[row[j]].col && g[k].r0 != g[row[j]].r0) elsewhere = 1;
      }
      if (!elsewhere) ok = 0;
    }
    if (!ok) continue;
    for (j = 0u; j < nr; j++) w->legend[j] = g[row[j]].col;
    w->n_legend = nr;
    /* a small thing of another shape just off one end of the legend, in its rows */
    w->legend_mark = 0;
    for (j = 0u; j < n; j++) {
      const mi_comp_t *x = &g[j], *first = &g[row[0]], *last = &g[row[nr - 1u]];
      if (x->n > 4u || (x->n == first->n && x->sig == first->sig)) continue;
      if (x->r0 < first->r0 || x->r0 > first->r1) continue;
      if (x->c0 > last->c1 && x->c0 <= last->c1 + 3) w->legend_mark = 2;
      if (x->c1 < first->c0 && x->c1 >= first->c0 - 3) w->legend_mark = 1;
    }
  }
}

/* The colours under a mover's template at (r, c), from the ground. */
static unsigned covers(const mi_world_t *w, const mi_mover_t *m, int r, int c) {
  unsigned i, mask = 0u;
  for (i = 0u; i < m->n; i++) {
    int rr = r + m->dr[i], cc = c + m->dc[i];
    if (rr < 0 || cc < 0 || rr >= MI_W || cc >= MI_W) continue;
    mask |= 1u << w->ground[rr][cc];
  }
  return mask;
}

/* ============================================================================
 * SEEING THINGS MOVE: new movers, by moving together
 * ==========================================================================*/

typedef struct {
  unsigned a, b;
  int dr, dc;
} mi_pair_t;

static int part_of_known(const mi_world_t *w, const mi_comp_t *k) {
  unsigned i;
  for (i = 0u; i < w->n_mv; i++) {
    const mi_mover_t *m = &w->mv[i];
    if (!m->seen) continue;
    if (in_template(m, m->r, m->c, k->r0, k->c0)) return 1;
  }
  return 0;
}

/*
 * Between two frames: patches that are gone from where they were and found
 * again, the same shape and colour, a step away. Patches that moved by the
 * same step and touch are one thing. Each new thing becomes a mover.
 */
static unsigned find_new_movers(mi_world_t *w, const pl_frame_t *a, const pl_frame_t *b,
                                unsigned action, const int *was_r, const int *was_c) {
  static mi_pair_t pairs[MI_COMPS];
  static unsigned group[MI_COMPS];
  unsigned na = components(a, w->bg, CA), nb, np = 0u, i, j, made = 0u;
  static unsigned char still_a[MI_COMPS], still_b[MI_COMPS], used_b[MI_COMPS];
  nb = components(b, w->bg, CB);
  memset(still_a, 0, sizeof still_a);
  memset(still_b, 0, sizeof still_b);
  memset(used_b, 0, sizeof used_b);
  for (i = 0u; i < na; i++) {
    for (j = 0u; j < nb; j++) {
      if (same_shape(&CA[i], &CB[j]) && CA[i].r0 == CB[j].r0 && CA[i].c0 == CB[j].c0) {
        still_a[i] = 1u;
        still_b[j] = 1u;
      }
    }
  }
  for (i = 0u; i < na; i++) {
    int best = -1, bd = 0;
    if (still_a[i] || CA[i].n > MI_TCELLS) continue;
    for (j = 0u; j < nb; j++) {
      int dr, dc, d;
      if (still_b[j] || used_b[j] || !same_shape(&CA[i], &CB[j])) continue;
      {
        /* hidden where a mover just arrived and revealed where it just left:
           nothing moved, a mover passed over two patches alike */
        unsigned k;
        int passed = 0;
        for (k = 0u; k < w->n_mv; k++) {
          if (w->mv[k].seen && in_template(&w->mv[k], w->mv[k].r, w->mv[k].c, CA[i].r0, CA[i].c0) &&
              in_template(&w->mv[k], was_r[k], was_c[k], CB[j].r0, CB[j].c0))
            passed = 1;
        }
        if (passed) continue;
      }
      dr = CB[j].r0 - CA[i].r0;
      dc = CB[j].c0 - CA[i].c0;
      if (dr > MI_REACH || dr < -MI_REACH || dc > MI_REACH || dc < -MI_REACH) continue;
      d = (dr < 0 ? -dr : dr) + (dc < 0 ? -dc : dc);
      if (best < 0 || d < bd) {
        best = (int)j;
        bd = d;
      }
    }
    if (best < 0) continue;
    if (part_of_known(w, &CB[best])) continue;
    used_b[best] = 1u;
    pairs[np].a = i;
    pairs[np].b = (unsigned)best;
    pairs[np].dr = CB[best].r0 - CA[i].r0;
    pairs[np].dc = CB[best].c0 - CA[i].c0;
    np++;
  }
  /* one thing: the same step, touching (in the frame after) */
  for (i = 0u; i < np; i++) group[i] = i;
  for (i = 0u; i < np; i++) {
    for (j = i + 1u; j < np; j++) {
      const mi_comp_t *x = &CB[pairs[i].b], *y = &CB[pairs[j].b];
      if (pairs[i].dr != pairs[j].dr || pairs[i].dc != pairs[j].dc) continue;
      if (x->r0 > y->r1 + 1 || y->r0 > x->r1 + 1 || x->c0 > y->c1 + 1 || y->c0 > x->c1 + 1) continue;
      {
        unsigned gi = group[i], gj = group[j], k;
        for (k = 0u; k < np; k++) {
          if (group[k] == gj) group[k] = gi;
        }
      }
    }
  }
  for (i = 0u; i < np && w->n_mv < MI_MAX_MOVERS; i++) {
    mi_mover_t *m;
    int top = MI_W, left = MI_W, r, c;
    unsigned k;
    if (group[i] != i) continue;
    for (k = 0u; k < np; k++) {
      if (group[k] != i) continue;
      if (CB[pairs[k].b].r0 < top) top = CB[pairs[k].b].r0;
      if (CB[pairs[k].b].c0 < left) left = CB[pairs[k].b].c0;
    }
    m = &w->mv[w->n_mv];
    memset(m, 0, sizeof(*m));
    for (k = 0u; k < np; k++) {
      const mi_comp_t *x;
      if (group[k] != i) continue;
      x = &CB[pairs[k].b];
      for (r = x->r0; r <= x->r1; r++) {
        for (c = x->c0; c <= x->c1; c++) {
          if (b->c[r][c] != x->col || m->n >= MI_TCELLS) continue;
          m->dr[m->n] = r - top;
          m->dc[m->n] = c - left;
          m->col[m->n] = x->col;
          m->n++;
        }
      }
    }
    if (m->n == 0u) continue;
    m->r = top;
    m->c = left;
    m->seen = 1;
    m->moved_ever = 1;
    /* it moved: by its own step for this action, or because it was pushed */
    m->may_table = 1;
    m->may_pushed = w->n_mv > 0u;
    m->known[action] = 1;
    m->sr[action] = pairs[i].dr;
    m->sc[action] = pairs[i].dc;
    /* what it stood on before is uncovered now; what it stands on is unknown */
    for (k = 0u; k < m->n; k++) {
      int rr = m->r + m->dr[k], cc = m->c + m->dc[k];
      if (rr >= 0 && cc >= 0 && rr < MI_W && cc < MI_W) w->ground[rr][cc] = w->bg;
    }
#ifdef MI_DEBUG
    fprintf(stderr, "NEW MOVER %u: %u cells at %d,%d step %d,%d col0 %u (pairs %u)\n", w->n_mv, m->n, m->r, m->c,
            pairs[i].dr, pairs[i].dc, m->col[0], np);
#endif
    w->n_mv++;
    made++;
  }
  return made;
}

/* ============================================================================
 * BELIEFS, AND PREDICTING WITH THEM
 * ==========================================================================*/

static int is_wall(const mi_world_t *w, unsigned k) {
  unsigned i, j;
  if (!w->wall_alive[k] || w->wall_out[k]) return 0;
  for (i = 0u; i < w->n_mv; i++) {
    for (j = 0u; j < w->mv[i].n; j++) {
      if (w->mv[i].col[j] == k) return 0;
    }
  }
  return 1;
}

/* A mover's step for an action, if known: its own, or another's transformed. */
static int step_of(const mi_world_t *w, unsigned i, unsigned a, int *dr, int *dc) {
  const mi_mover_t *m = &w->mv[i];
  if (m->known[a]) {
    *dr = m->sr[a];
    *dc = m->sc[a];
    return 1;
  }
  if (w->transform != 0 && i > 0u && w->mv[0].known[a]) {
    *dr = (w->transform & 2) ? -w->mv[0].sr[a] : w->mv[0].sr[a];
    *dc = (w->transform & 1) ? -w->mv[0].sc[a] : w->mv[0].sc[a];
    return 1;
  }
  return 0;
}

static int moves_by_table(const mi_mover_t *m) {
  return m->may_table && !m->may_pushed;
}

/* Is a mover's template at (r, c) free of what blocks it? */
static int free_at(const mi_world_t *w, const mi_mover_t *m, int r, int c) {
  unsigned i;
  for (i = 0u; i < m->n; i++) {
    int rr = r + m->dr[i], cc = c + m->dc[i];
    if (rr < 0 || cc < 0 || rr >= MI_W || cc >= MI_W) return 0;
    if (is_wall(w, w->ground[rr][cc])) return 0;
  }
  return 1;
}

static int overlaps(const mi_mover_t *a, int ar, int ac, const mi_mover_t *b, int br, int bc) {
  unsigned i;
  for (i = 0u; i < a->n; i++) {
    if (in_template(b, br, bc, ar + a->dr[i], ac + a->dc[i])) return 1;
  }
  return 0;
}

/*
 * Where every mover will be after action a, from where they are now, by its
 * beliefs. Returns 0 when it cannot say for some mover (a step it has never
 * seen). Movers that move by their own step move unless blocked; a mover that
 * is pushed moves when one of them steps into it, unless it is blocked, in
 * which case the one pushing stays too.
 */
static int predict(const mi_world_t *w, unsigned a, const int *r0, const int *c0, int *r1, int *c1) {
  unsigned i, j;
  int sure = 1;
  for (i = 0u; i < w->n_mv; i++) {
    r1[i] = r0[i];
    c1[i] = c0[i];
  }
  for (i = 0u; i < w->n_mv; i++) {
    int dr, dc;
    if (!moves_by_table(&w->mv[i])) continue;
    if (!step_of(w, i, a, &dr, &dc)) {
      sure = 0;
      continue;
    }
    if (free_at(w, &w->mv[i], r0[i] + dr, c0[i] + dc)) {
      r1[i] = r0[i] + dr;
      c1[i] = c0[i] + dc;
    }
  }
  for (j = 0u; j < w->n_mv; j++) {
    if (moves_by_table(&w->mv[j])) continue;
    for (i = 0u; i < w->n_mv; i++) {
      int dr = r1[i] - r0[i], dc = c1[i] - c0[i];
      if (i == j || !moves_by_table(&w->mv[i]) || (dr == 0 && dc == 0)) continue;
      if (!overlaps(&w->mv[i], r1[i], c1[i], &w->mv[j], r0[j], c0[j])) continue;
      if (free_at(w, &w->mv[j], r0[j] + dr, c0[j] + dc)) {
        r1[j] = r0[j] + dr;
        c1[j] = c0[j] + dc;
      } else {
        r1[i] = r0[i];
        c1[i] = c0[i];
      }
    }
  }
  return sure;
}

/* ---- what ends a level ------------------------------------------------------ */

/* What one action did to the ending conditions, per mover. */
typedef struct {
  unsigned level;      /* which level: what the relations were */
  unsigned seg;        /* which attempt at it: progress starts over with each */
  unsigned char ended;
  unsigned char guessed;   /* where things were at this moment was inferred, not seen */
  unsigned char doubted;   /* set aside by a self-correction: no longer evidence */
  unsigned char moved[MI_MAX_MOVERS];
  unsigned short covered[MI_MAX_MOVERS];
} mi_event_t;

static mi_event_t HIST[MI_HISTORY];
static unsigned N_HIST;
static int LEVEL_ODD[PL_MAX_LEVELS];
static unsigned LEVEL_NLEG[PL_MAX_LEVELS];
static unsigned char LEVEL_LEG[PL_MAX_LEVELS][MI_LEGEND];

/* Progress along the legend: covering the next colour moves on, covering a
   colour further along starts again, an earlier one (already done) nothing. */
static unsigned advance(unsigned p, unsigned mask, const unsigned char *leg, unsigned n, int reverse) {
  unsigned j;
  for (j = 0u; j < n; j++) {
    unsigned idx = reverse ? n - 1u - j : j;
    if (!(mask & (1u << leg[idx]))) continue;
    if (j == p) return p + 1u;
    if (j > p) return 0u;
  }
  return p;
}

/*
 * Does family f, for mover m, say the level ends at this event? For the
 * sequences, *p is the progress, moved on by the event.
 */
static int says_ends(unsigned f, const mi_event_t *e, unsigned m, int odd, const unsigned char *leg,
                     unsigned nleg, unsigned *p) {
  if (f == MI_ODD) return odd >= 0 && (e->covered[m] & (1u << odd));
  if (f == MI_SEQ_LR || f == MI_SEQ_RL) {
    if (nleg < 2u) return 0;
    if (e->moved[m]) *p = advance(*p, e->covered[m], leg, nleg, f == MI_SEQ_RL);
    return *p >= nleg;
  }
  return 0;
}

/* Replay everything seen in this world through family f for mover m. 1 when
   it agrees with every action, and *prog is its progress now. */
static int replay(unsigned f, unsigned m, unsigned *prog) {
  unsigned i, p = 0u, seg = 0u;
  for (i = 0u; i < N_HIST; i++) {
    const mi_event_t *e = &HIST[i];
    unsigned level = e->level < PL_MAX_LEVELS ? e->level : PL_MAX_LEVELS - 1u;
    int end;
    if (e->seg != seg) {
      seg = e->seg;
      p = 0u;
    }
    if (e->doubted) continue;
    end = says_ends(f, e, m, LEVEL_ODD[level], LEVEL_LEG[level], LEVEL_NLEG[level], &p);
    if (end != (int)e->ended) {
#ifdef MI_DEBUG
      fprintf(stderr, "  replay family %u mover %u fails at event %u (level %u, ended %u, covered %x, odd %d)\n",
              f, m, i, level, e->ended, e->covered[m], LEVEL_ODD[level]);
#endif
      return 0;
    }
    if (e->ended) p = 0u;
  }
  *prog = p;
  return 1;
}

/* Build family f into the language, keeping only what agrees with everything seen. */
static unsigned invent(mi_world_t *w, unsigned f) {
  unsigned m, alive = 0u;
  w->lang[f] = 1;
  for (m = 0u; m < MI_MAX_MOVERS; m++) {
    unsigned p = 0u;
    w->fam_alive[f][m] = m < w->n_mv && replay(f, m, &p);
    w->prog[f][m] = p;
    if (w->fam_alive[f][m]) alive++;
  }
  /* nothing it could say agreed with what was seen: back on the shelf, to be
     tried again when there is more to go on */
  if (alive == 0u) w->lang[f] = 0;
  return alive;
}

/* Rule out, by one event, everything that said otherwise. Returns how many
   of its beliefs about endings predicted this ending (or 1 if none ended). */
static unsigned judge(mi_world_t *w, const mi_event_t *e, int at) {
  unsigned m, k, f, explained = 0u;
  for (m = 0u; m < w->n_mv && m < MI_MAX_MOVERS; m++) {
    for (k = 0u; k < PL_COLOURS; k++) {
      int says;
      if (!w->cover[m][k]) continue;
      says = (e->covered[m] & (1u << k)) != 0u;
      if (says != (int)e->ended) {
        w->cover[m][k] = 0;
        w->cover_by[m][k] = at;
      } else if (e->ended) {
        explained++;
      }
    }
    for (f = MI_ODD; f < MI_FAMILIES; f++) {
      int says;
      if (!w->lang[f] || !w->fam_alive[f][m]) continue;
      says = says_ends(f, e, m, w->odd, w->legend, w->n_legend, &w->prog[f][m]);
      if (says != (int)e->ended) {
        w->fam_alive[f][m] = 0;
        w->fam_by[f][m] = at;
      } else if (e->ended) {
        explained++;
      }
      if (e->ended) w->prog[f][m] = 0u;
    }
  }
  return e->ended ? explained : 1u;
}

static unsigned alive_in(const mi_world_t *w, unsigned f) {
  unsigned m, k, n = 0u;
  if (!w->lang[f]) return 0u;
  for (m = 0u; m < w->n_mv; m++) {
    if (f == MI_COVER) {
      for (k = 0u; k < PL_COLOURS; k++) n += (unsigned)w->cover[m][k];
    } else {
      n += (unsigned)w->fam_alive[f][m];
    }
  }
  return n;
}

/* log2 of what is still possible about what ends a level, in whole bits */
static unsigned hartley_bits(const mi_world_t *w) {
  unsigned f, n = 0u, bits = 0u;
  for (f = 0u; f < MI_FAMILIES; f++) n += alive_in(w, f);
  while ((1u << bits) < n && bits < 31u) bits++;
  return bits;
}

/* ============================================================================
 * SELF-CORRECTION: finding the culprit, the way optogenetics finds a cause
 * ==========================================================================*/

/* Does "mover m covering colour k ends a level" agree with every moment not
   set aside, and explain at least one ending? */
static int cover_holds(unsigned m, unsigned k) {
  unsigned i, explains = 0u;
  for (i = 0u; i < N_HIST; i++) {
    const mi_event_t *e = &HIST[i];
    int says;
    if (e->doubted) continue;
    says = (e->covered[m] & (1u << k)) != 0u;
    if (says != (int)e->ended) return 0;
    if (e->ended) explains++;
  }
  return explains > 0u;
}

static int fam_holds(unsigned f, unsigned m, unsigned *prog) {
  unsigned i, endings = 0u;
  for (i = 0u; i < N_HIST; i++) endings += !HIST[i].doubted && HIST[i].ended;
  return endings > 0u && replay(f, m, prog);
}

/*
 * Nothing it believes about endings is left standing (or an ending came that
 * nothing explained). Before concluding it needs a new idea, it asks whether an
 * idea it already had was ruled out wrongly: by a moment whose evidence was
 * inferred rather than seen (where things were when a level ended is never
 * seen). One such moment at a time, the most recent first, it sets the moment
 * aside (silences that evidence) and switches each idea that moment ruled out
 * back on, replaying everything else it has seen through it. If an idea then
 * agrees with everything and explains an ending, the moment is set aside for
 * good and the idea comes back. Only that moment and the ideas it ruled out are
 * touched. Returns how many ideas came back.
 */
static unsigned correct_endings(mi_world_t *w) {
  int at;
  for (at = (int)N_HIST - 1; at >= 0; at--) {
    unsigned m, k, f, back = 0u;
    mi_event_t *e = &HIST[at];
    if (!e->guessed || e->doubted) continue;
    e->doubted = 1u;
    for (m = 0u; m < w->n_mv && m < MI_MAX_MOVERS; m++) {
      for (k = 0u; k < PL_COLOURS; k++) {
        if (!w->cover[m][k] && w->cover_by[m][k] == at && cover_holds(m, k)) back++;
      }
      for (f = MI_ODD; f < MI_FAMILIES; f++) {
        unsigned p;
        if (w->lang[f] && !w->fam_alive[f][m] && w->fam_by[f][m] == at && fam_holds(f, m, &p)) back++;
      }
    }
    if (back == 0u) {
      e->doubted = 0u;   /* nothing it ruled out comes back: the moment stands */
      continue;
    }
    for (m = 0u; m < w->n_mv && m < MI_MAX_MOVERS; m++) {
      for (k = 0u; k < PL_COLOURS; k++) {
        if (!w->cover[m][k] && w->cover_by[m][k] == at && cover_holds(m, k)) {
          w->cover[m][k] = 1;
          w->cover_by[m][k] = -1;
        }
      }
      for (f = MI_ODD; f < MI_FAMILIES; f++) {
        unsigned p;
        if (w->lang[f] && !w->fam_alive[f][m] && w->fam_by[f][m] == at && fam_holds(f, m, &p)) {
          w->fam_alive[f][m] = 1;
          w->fam_by[f][m] = -1;
          w->prog[f][m] = p;
        }
      }
    }
    return back;
  }
  return 0u;
}

/* ============================================================================
 * IMAGINATION: plans made inside its beliefs
 * ==========================================================================*/

#define MI_HASH (1u << 20)
#define MI_QUEUE (1u << 18)

typedef struct {
  signed char r[MI_MAX_MOVERS], c[MI_MAX_MOVERS];
  unsigned char p;
} mi_state_t;

static unsigned long long HKEY[MI_HASH];
static unsigned char HUSED[MI_HASH];
static unsigned HSLOTS[MI_QUEUE];
static mi_state_t Q[MI_QUEUE];
static int QPARENT[MI_QUEUE];
static unsigned char QACT[MI_QUEUE];

static unsigned long long key_of(const mi_state_t *s, unsigned n) {
  unsigned long long k = s->p;
  unsigned i;
  for (i = 0u; i < n; i++) k = (k << 12) | ((unsigned long long)(unsigned char)s->r[i] << 6) |
                                (unsigned char)s->c[i];
  return k;
}

static int seen_or_add(unsigned long long k, unsigned *n_slots) {
  unsigned h = (unsigned)((k * 0x9E3779B97F4A7C15ull) >> 44) & (MI_HASH - 1u);
  while (HUSED[h]) {
    if (HKEY[h] == k) return 1;
    h = (h + 1u) & (MI_HASH - 1u);
  }
  if (*n_slots >= MI_QUEUE) return 1;
  HUSED[h] = 1u;
  HKEY[h] = k;
  HSLOTS[(*n_slots)++] = h;
  return 0;
}

/* What the plan is for. */
typedef struct {
  int family;      /* -1: somewhere its first mover has not been; -2: somewhere an
                      action it doubts has not yet failed (to test the doubt) */
  unsigned m;      /* for the families of one mover */
  int reverse;
} mi_goal_t;

static int goal_met(const mi_world_t *w, const mi_goal_t *g, const mi_state_t *s) {
  unsigned m, k;
  if (g->family == -2) {
    unsigned b, r = (unsigned char)s->r[0], c = (unsigned char)s->c[0];
    for (b = 1u; b <= w->n_actions; b++) {
      if (!w->mv[0].known[b] && w->tried[b] > 0u && !(w->failed[r][c] & (1u << b))) return 1;
    }
    return 0;
  }
  if (g->family < 0) return !w->visited[(unsigned char)s->r[0]][(unsigned char)s->c[0]];
  if (g->family == MI_COVER) {
    for (m = 0u; m < w->n_mv; m++) {
      unsigned mask = covers(w, &w->mv[m], s->r[m], s->c[m]);
      for (k = 0u; k < PL_COLOURS; k++) {
        if (w->cover[m][k] && (mask & (1u << k))) return 1;
      }
    }
    return 0;
  }
  if (g->family == MI_ODD) {
    return w->odd >= 0 && (covers(w, &w->mv[g->m], s->r[g->m], s->c[g->m]) & (1u << w->odd));
  }
  return s->p >= w->n_legend;
}

/*
 * Breadth-first over the states its beliefs say it can reach. Fills plan
 * with the actions (at most cap) and returns how many; 0 when no state that
 * meets the goal can be reached.
 */
static unsigned imagine(const mi_world_t *w, const mi_goal_t *g, unsigned *plan, unsigned cap) {
  unsigned head = 0u, tail = 0u, n_slots = 0u, i, found = MI_QUEUE, len = 0u;
  mi_state_t s0;
  memset(&s0, 0, sizeof s0);
  for (i = 0u; i < w->n_mv; i++) {
    s0.r[i] = (signed char)w->mv[i].r;
    s0.c[i] = (signed char)w->mv[i].c;
  }
  s0.p = g->family >= MI_SEQ_LR ? (unsigned char)w->prog[g->family][g->m] : 0u;
  Q[0] = s0;
  QPARENT[0] = -1;
  QACT[0] = 0u;
  seen_or_add(key_of(&s0, w->n_mv), &n_slots);
  tail = 1u;
  while (head < tail && found == MI_QUEUE) {
    unsigned a;
    for (a = 1u; a <= w->n_actions && found == MI_QUEUE; a++) {
      int r0[MI_MAX_MOVERS], c0[MI_MAX_MOVERS], r1[MI_MAX_MOVERS], c1[MI_MAX_MOVERS], moved = 0;
      mi_state_t s;
      for (i = 0u; i < w->n_mv; i++) {
        r0[i] = Q[head].r[i];
        c0[i] = Q[head].c[i];
      }
      if (!predict(w, a, r0, c0, r1, c1)) continue;
      s = Q[head];
      for (i = 0u; i < w->n_mv; i++) {
        if (r1[i] != r0[i] || c1[i] != c0[i]) moved = 1;
        s.r[i] = (signed char)r1[i];
        s.c[i] = (signed char)c1[i];
      }
      if (!moved) continue;
      if (g->family >= MI_SEQ_LR && (r1[g->m] != r0[g->m] || c1[g->m] != c0[g->m])) {
        s.p = (unsigned char)advance(s.p, covers(w, &w->mv[g->m], r1[g->m], c1[g->m]), w->legend,
                                     w->n_legend, g->reverse);
      }
      if (seen_or_add(key_of(&s, w->n_mv), &n_slots)) continue;
      Q[tail] = s;
      QPARENT[tail] = (int)head;
      QACT[tail] = (unsigned char)a;
      if (goal_met(w, g, &s)) found = tail;
      tail++;
    }
    head++;
  }
  for (i = 0u; i < n_slots; i++) HUSED[HSLOTS[i]] = 0u;
  if (found == MI_QUEUE) return 0u;
  for (i = found; QPARENT[i] >= 0; i = (unsigned)QPARENT[i]) len++;
  if (len > cap) return 0u;
  i = found;
  {
    unsigned k = len;
    while (QPARENT[i] >= 0) {
      plan[--k] = QACT[i];
      i = (unsigned)QPARENT[i];
    }
  }
  return len;
}

/* ============================================================================
 * NOVELTY AND INTUITION: a fly's fingerprint
 * ==========================================================================*/

static void features(const pl_frame_t *f, const mi_world_t *w, unsigned *x) {
  unsigned n = components(f, w->bg, CA), i, small = 0u, big = 0u, cols = 0u, k;
  unsigned seen[PL_COLOURS] = {0};
  for (i = 0u; i < n; i++) {
    if (CA[i].n <= 16u) small++;
    else big++;
    seen[CA[i].col] = 1u;
  }
  for (k = 0u; k < PL_COLOURS; k++) cols += seen[k];
  x[0] = small > 7u ? 7u : small;
  x[1] = big > 7u ? 7u : big;
  x[2] = cols > 7u ? 7u : cols;
  x[3] = w->odd >= 0 ? 7u : 0u;
  x[4] = w->n_legend >= 2u ? 7u : 0u;
  x[5] = w->n_legend > 7u ? 7u : w->n_legend;
  x[6] = n > 70u ? 7u : n / 10u;
  x[7] = small >= 3u ? 4u : 0u;
  x[8] = big == 1u ? 5u : 0u;
  x[9] = (small + big) > 3u ? 3u : 0u;
  x[10] = w->legend_mark == 2 ? 7u : w->legend_mark == 1 ? 3u : 0u;
}

/* A sparse random expansion of the features, and the strongest units kept. */
static void fingerprint(const unsigned *x, unsigned char *fp) {
  unsigned act[MI_UNITS], u, k;
  unsigned char taken[MI_UNITS] = {0};
  unsigned s = 0x2545F491u;
  for (u = 0u; u < MI_UNITS; u++) {
    unsigned j, sum = 0u;
    for (j = 0u; j < 3u; j++) {
      s ^= s << 13;
      s ^= s >> 17;
      s ^= s << 5;
      sum += x[s % MI_FEATURES];
    }
    act[u] = sum * 256u + u;   /* ties go to the lower unit, always the same way */
  }
  for (k = 0u; k < MI_FP_K; k++) {
    unsigned best = 0u, bu = 0u;
    for (u = 0u; u < MI_UNITS; u++) {
      if (!taken[u] && act[u] >= best) {
        best = act[u];
        bu = u;
      }
    }
    taken[bu] = 1u;
    fp[k] = (unsigned char)bu;
  }
}

static unsigned overlap(const unsigned char *a, const unsigned char *b) {
  unsigned i, j, n = 0u;
  for (i = 0u; i < MI_FP_K; i++) {
    for (j = 0u; j < MI_FP_K; j++) n += a[i] == b[j];
  }
  return n;
}

/* ============================================================================
 * PLAYING: the controller's loop
 * ==========================================================================*/

static mi_event_t DREAM[MI_DREAMS][MI_HISTORY];
static unsigned DREAM_N[MI_DREAMS];
static int DREAM_ODD[MI_DREAMS][PL_MAX_LEVELS];
static unsigned DREAM_NLEG[MI_DREAMS][PL_MAX_LEVELS];
static unsigned char DREAM_LEG[MI_DREAMS][PL_MAX_LEVELS][MI_LEGEND];
static unsigned DREAM_MOVERS[MI_DREAMS];
static unsigned N_DREAMS;

static void keep_for_sleep(void) {
  unsigned slot = N_DREAMS < MI_DREAMS ? N_DREAMS++ : MI_DREAMS - 1u;
  memcpy(DREAM[slot], HIST, sizeof(mi_event_t) * N_HIST);
  DREAM_N[slot] = N_HIST;
  memcpy(DREAM_ODD[slot], LEVEL_ODD, sizeof LEVEL_ODD);
  memcpy(DREAM_NLEG[slot], LEVEL_NLEG, sizeof LEVEL_NLEG);
  memcpy(DREAM_LEG[slot], LEVEL_LEG, sizeof LEVEL_LEG);
  DREAM_MOVERS[slot] = W.n_mv;
}

static void start_level(mi_world_t *w, const pl_frame_t *f, unsigned level) {
  unsigned i, m, f2;
  int r, c;
  w->bg = background(f);
  for (r = 0; r < MI_W; r++) {
    for (c = 0; c < MI_W; c++) {
      w->ground[r][c] = f->c[r][c];
      w->visited[r][c] = 0u;
      w->failed[r][c] = 0u;
    }
  }
  for (i = 0u; i < w->n_mv; i++) {
    mi_mover_t *mv = &w->mv[i];
    unsigned k;
    mv->seen = locate(mv, f, mv->r, mv->c, &mv->r, &mv->c);
    if (!mv->seen) continue;
    for (k = 0u; k < mv->n; k++) w->ground[mv->r + mv->dr[k]][mv->c + mv->dc[k]] = w->bg;
  }
  read_relations(w);
#ifdef MI_DEBUG
  fprintf(stderr, "LEVEL %u: odd %d legend %u bg %u\n", level, w->odd, w->n_legend, w->bg);
#endif
  if (level < PL_MAX_LEVELS) {
    LEVEL_ODD[level] = w->odd;
    LEVEL_NLEG[level] = w->n_legend;
    memcpy(LEVEL_LEG[level], w->legend, sizeof w->legend);
  }
  for (f2 = 0u; f2 < MI_FAMILIES; f2++) {
    for (m = 0u; m < MI_MAX_MOVERS; m++) w->prog[f2][m] = 0u;
  }
}

/* The order in which to try the families: what explained worlds like this one
   most often first (intuition), then what explained most worlds at all. */
static void family_order(const mi_library_t *lib, const unsigned *votes, unsigned *order) {
  unsigned i, j;
  unsigned score[MI_FAMILIES];
  for (i = 0u; i < MI_FAMILIES; i++) {
    score[i] = (mi_switched_off(MI_PART_INTUITION) ? 0u : votes[i] * 1000u) + lib->wins[i] * 10u +
               (MI_FAMILIES - i);
    order[i] = i;
  }
  for (i = 1u; i < MI_FAMILIES; i++) {
    unsigned t = order[i];
    j = i;
    while (j > 0u && score[order[j - 1u]] < score[t]) {
      order[j] = order[j - 1u];
      j--;
    }
    order[j] = t;
  }
}

/* A plan for the first family in order that has something alive and reachable. */
static unsigned make_plan(const mi_world_t *w, const unsigned *order, unsigned *plan, unsigned cap,
                          int *planned_family) {
  unsigned i, m, len;
  for (i = 0u; i < MI_FAMILIES; i++) {
    unsigned f = order[i];
    if (alive_in(w, f) == 0u) continue;
    if (f == MI_COVER) {
      mi_goal_t g;
      g.family = MI_COVER;
      g.m = 0u;
      g.reverse = 0;
      len = imagine(w, &g, plan, cap);
      if (len > 0u) {
        *planned_family = (int)f;
        return len;
      }
      continue;
    }
    for (m = 0u; m < w->n_mv; m++) {
      mi_goal_t g;
      if (!w->fam_alive[f][m]) continue;
      if (f == MI_ODD && w->odd < 0) continue;
      if (f >= MI_SEQ_LR && w->n_legend < 2u) continue;
      g.family = (int)f;
      g.m = m;
      g.reverse = f == MI_SEQ_RL;
      len = imagine(w, &g, plan, cap);
      if (len > 0u) {
        *planned_family = (int)f;
        return len;
      }
    }
  }
  return 0u;
}

/* Any plan at all: to test what could end the level, or else to go somewhere new. */
static unsigned find_way(const mi_world_t *w, const unsigned *order, unsigned *plan, int *planned_family) {
  mi_goal_t gl;
  unsigned len = make_plan(w, order, plan, 512u, planned_family);
  if (len > 0u) return len;
  gl.family = -1;
  gl.m = 0u;
  gl.reverse = 0;
  *planned_family = -1;
  len = imagine(w, &gl, plan, 512u);
  if (len > 0u) return len;
  gl.family = -2;
  return imagine(w, &gl, plan, 512u);
}

/*
 * Stuck: nowhere its beliefs let it go. Rather than give up every conclusion of
 * a kind at once, it doubts one at a time, in its head: an action it concluded
 * does nothing, or a colour it concluded blocks it, the one resting on the least
 * evidence first. If with that one conclusion silenced a way opens, that
 * conclusion is the culprit: it is dropped, and the way is taken, so the world
 * says whether the doubt was right (if the colour blocks after all, it will be
 * found blocking again). Returns 1 when a way opened; *what says which kind.
 */
static int doubt_one(mi_world_t *w, const unsigned *order, unsigned *plan, unsigned *len,
                     int *planned_family, unsigned *what) {
  unsigned b, k;
  if (w->n_mv == 0u) return 0;
  for (b = 1u; b <= w->n_actions; b++) {
    if (!(w->mv[0].known[b] && w->mv[0].sr[b] == 0 && w->mv[0].sc[b] == 0)) continue;
    w->mv[0].known[b] = 0;
    *len = find_way(w, order, plan, planned_family);
    if (*len > 0u || w->tried[b] < 6u) {
      /* a way opens, or it has too little evidence to keep: tried again, but
         only where it has not already failed (where it failed is kept: the
         doubt is tested somewhere new, not in the dead end that raised it) */
      w->action_doubted[b]++;
      w->stayed[b] = 0u;
      *what = 1u;
      return 1;
    }
    w->mv[0].known[b] = 1;
  }
  for (;;) {
    int best = -1;
    for (k = 0u; k < PL_COLOURS; k++) {
      if (!is_wall(w, k) || w->wall_confirmed[k] || w->wall_fails[k] == 0xFFFFFFFFu) continue;
      if (best < 0 || w->wall_fails[k] < w->wall_fails[best]) best = (int)k;
    }
    if (best < 0) break;
    w->wall_alive[best] = 0;
    *len = find_way(w, order, plan, planned_family);
    if (*len > 0u) {
      w->wall_fails[best] = 0u;
      w->wall_doubted[best] = 1;
      memset(w->failed, 0, sizeof w->failed);
      *what = 2u;
      return 1;
    }
    w->wall_alive[best] = 1;
    w->wall_fails[best] = 0xFFFFFFFFu;   /* tried: not doubted again this time */
  }
  for (k = 0u; k < PL_COLOURS; k++) {
    if (w->wall_fails[k] == 0xFFFFFFFFu) w->wall_fails[k] = 1u;
  }
  return 0;
}

/* Does one mover step as another, transformed, on every action seen for both? */
static int find_transform(const mi_world_t *w) {
  unsigned i, a;
  int t;
  for (i = 1u; i < w->n_mv; i++) {
    if (!moves_by_table(&w->mv[i])) continue;
    for (t = 1; t <= 3; t++) {
      unsigned both = 0u, agree = 0u;
      for (a = 1u; a <= w->n_actions; a++) {
        int dr, dc;
        if (!w->mv[0].known[a] || !w->mv[i].known[a]) continue;
        dr = (t & 2) ? -w->mv[0].sr[a] : w->mv[0].sr[a];
        dc = (t & 1) ? -w->mv[0].sc[a] : w->mv[0].sc[a];
        both++;
        if (dr == w->mv[i].sr[a] && dc == w->mv[i].sc[a]) agree++;
      }
      if (both >= 2u && agree == both) {
        /* a transform that changes nothing on any step seen is no transform */
        int changes = 0;
        for (a = 1u; a <= w->n_actions; a++) {
          if (w->mv[0].known[a] && w->mv[i].known[a] &&
              (w->mv[0].sr[a] != w->mv[i].sr[a] || w->mv[0].sc[a] != w->mv[i].sc[a]))
            changes = 1;
        }
        if (changes) return t;
      }
    }
  }
  return 0;
}

/* A fingerprint of everything a plan is made from: beliefs, ground, where the
   movers are. A search that found nothing is not repeated until this changes. */
static unsigned long long belief_key(const mi_world_t *w) {
  unsigned long long h = 1469598103934665603ull;
  const unsigned char *p;
  size_t i, n;
#define MI_MIX(ptr, len)                                     \
  do {                                                       \
    p = (const unsigned char *)(ptr);                        \
    n = (len);                                               \
    for (i = 0u; i < n; i++) h = (h ^ p[i]) * 1099511628211ull; \
  } while (0)
  MI_MIX(w->ground, sizeof w->ground);
  MI_MIX(w->visited, sizeof w->visited);
  MI_MIX(w->mv, sizeof w->mv);
  MI_MIX(w->wall_alive, sizeof w->wall_alive);
  MI_MIX(w->wall_out, sizeof w->wall_out);
  MI_MIX(w->lang, sizeof w->lang);
  MI_MIX(w->cover, sizeof w->cover);
  MI_MIX(w->fam_alive, sizeof w->fam_alive);
  MI_MIX(w->prog, sizeof w->prog);
  MI_MIX(&w->transform, sizeof w->transform);
#undef MI_MIX
  return h;
}

static void note_mode(mi_result_t *res, unsigned mode) {
  res->modes[mode]++;
}

sm_status_t mi_play(mi_library_t *lib, pl_game_t *g, unsigned budget, unsigned hint,
                    mi_result_t *out) {
  static pl_frame_t now, next;
  static unsigned plan[512];
  unsigned plan_len = 0u, plan_at = 0u, level_start = 0u, i, f, stuck = 0u, changed_here = 0u;
  unsigned seg = 0u;
  unsigned votes[MI_FAMILIES] = {0}, order[MI_FAMILIES];
  unsigned char fp[MI_FP_K];
  int planned_family = -1, trusted;
  unsigned long long no_plan_at = 0ull, no_way_at = 0ull, key = 0ull;
  mi_world_t *w = &W;
  mi_result_t res;

  if (lib == 0 || g == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (g->n_actions == 0u || g->n_actions > PL_MAX_ACTIONS || g->levels == 0u ||
      g->levels > PL_MAX_LEVELS)
    return SM_ERR_DOMAIN_TOO_LARGE;

  memset(&res, 0, sizeof res);
  memset(w, 0, sizeof(*w));
  res.winner = -1;
  res.intuition = -1;
  res.hinted = hint < MI_FAMILIES;
  w->n_actions = g->n_actions;
  N_HIST = 0u;
  for (i = 0u; i < MI_MAX_MOVERS; i++) {
    unsigned k;
    for (k = 0u; k < PL_COLOURS; k++) w->cover[i][k] = 1;
  }
  /* the language it starts with: one block, plus what it has packed */
  w->lang[MI_COVER] = 1;
  for (f = MI_ODD; f < MI_FAMILIES; f++) {
    if (lib->packed[f] && !mi_switched_off(MI_PART_PACKING)) w->lang[f] = 1;
  }
  if (hint < MI_FAMILIES) w->lang[hint] = 1;
  for (f = 0u; f < MI_FAMILIES; f++) {
    unsigned m;
    for (m = 0u; m < MI_MAX_MOVERS; m++) {
      w->fam_alive[f][m] = 1;
      w->fam_by[f][m] = -1;
    }
  }
  for (i = 0u; i < MI_MAX_MOVERS; i++) {
    unsigned k;
    for (k = 0u; k < PL_COLOURS; k++) w->cover_by[i][k] = -1;
  }

  g->reset(g, &now);
  start_level(w, &now, 0u);
  res.level_shortest[0] = g->shortest ? g->shortest(g) : 0u;

  /* novelty: how like any world before is this one; intuition: what ended those */
  {
    unsigned x[MI_FEATURES], best = 0u, k;
    features(&now, w, x);
    fingerprint(x, fp);
    for (k = 0u; k < lib->n_mem; k++) {
      unsigned o = overlap(fp, lib->mem_fp[k]);
      if (o > best) best = o;
    }
    res.novelty = MI_FP_K - best;
    if (!mi_switched_off(MI_PART_INTUITION)) {
      for (k = 0u; k < lib->n_mem; k++) {
        unsigned o = overlap(fp, lib->mem_fp[k]);
        if (o + 2u >= MI_FP_K && lib->mem_family[k] > 0u) votes[lib->mem_family[k] - 1u] += o;
      }
    }
    {
      unsigned bestv = 0u;
      for (k = 0u; k < MI_FAMILIES; k++) {
        if (votes[k] > bestv) {
          bestv = votes[k];
          res.intuition = (int)k;
        }
      }
    }
  }
  trusted = res.intuition >= 0 && lib->intuition_hit[res.intuition] >= 2u &&
            lib->intuition_hit[res.intuition] >=
                3u * (lib->intuition_named[res.intuition] - lib->intuition_hit[res.intuition]);
  res.trusted = trusted;
  /* a trusted intuition can bring back an idea it has had before, from the start */
  if (trusted && (lib->wins[res.intuition] > 0u || lib->dreamt[res.intuition] > 0u)) {
    w->lang[res.intuition] = 1;
  }
  family_order(lib, votes, order);

  while (res.actions < budget) {
    int r0[MI_MAX_MOVERS], c0[MI_MAX_MOVERS], r1[MI_MAX_MOVERS], c1[MI_MAX_MOVERS];
    int sure, outcome;
    unsigned a = 0u, mode = MI_EXPLORE;
    mi_event_t e;

    for (i = 0u; i < w->n_mv; i++) {
      r0[i] = w->mv[i].r;
      c0[i] = w->mv[i].c;
    }
    if (w->n_mv > 0u && w->mv[0].seen) w->visited[w->mv[0].r][w->mv[0].c] = 1u;

    /* ---- the controller: what to do with this move ---- */
    for (i = 1u; i <= g->n_actions && a == 0u; i++) {
      /* an action never taken, or one that has only ever left it where it was
         (which may have been a wall, not the action): nothing can be planned
         with it until it is known, so it is tried, somewhere it has not failed */
      if (w->tried[i] == 0u) a = i;
      else if (w->n_mv > 0u && w->mv[0].seen && !w->mv[0].known[i] && w->stayed[i] < 3u &&
               !(w->failed[w->mv[0].r][w->mv[0].c] & (1u << i)))
        a = i;
    }
    if (a == 0u && plan_at < plan_len) {
      a = plan[plan_at++];
      mode = (trusted && planned_family == res.intuition) ? MI_INTUITION : MI_IMAGINE;
    }
    if (a == 0u && w->n_mv > 0u) key = belief_key(w);
    if (a == 0u && w->n_mv > 0u && !mi_switched_off(MI_PART_IMAGINATION) && key != no_plan_at) {
      plan_len = make_plan(w, order, plan, 512u, &planned_family);
      plan_at = 0u;
      if (plan_len == 0u && !mi_switched_off(MI_PART_INVENTOR)) {
        /* nothing it believes can be tested from here: what is left is out of
           reach, so the language is too small to say what ends this. The
           inventor builds the next blocks, keeping what agrees with everything */
        unsigned grew = 0u;
        for (f = MI_ODD; f < MI_FAMILIES; f++) {
          if (w->lang[f]) continue;
          if (invent(w, f) > 0u) {
            res.invented |= 1u << f;
            lib->inventions++;
            grew = 1u;
          }
        }
        if (grew) plan_len = make_plan(w, order, plan, 512u, &planned_family);
      }
      if (plan_len == 0u) no_plan_at = belief_key(w);
      if (plan_len > 0u) {
        a = plan[plan_at++];
        mode = (trusted && planned_family == res.intuition) ? MI_INTUITION : MI_IMAGINE;
      }
    }
    if (a == 0u && w->n_mv > 0u && !mi_switched_off(MI_PART_IMAGINATION) && key != no_way_at) {
      mi_goal_t gl;
      gl.family = -1;
      gl.m = 0u;
      gl.reverse = 0;
      plan_len = imagine(w, &gl, plan, 512u);
      if (plan_len == 0u && !mi_switched_off(MI_PART_CORRECTION)) {
        /* nowhere new: go and test a doubt, somewhere it has not been tested */
        gl.family = -2;
        plan_len = imagine(w, &gl, plan, 512u);
      }
      plan_at = 0u;
      planned_family = -1;
      if (plan_len > 0u) a = plan[plan_at++];
      else no_way_at = key;
      mode = MI_EXPLORE;
    }
    if (a == 0u) {
      /* nothing left to imagine: it is stuck */
      stuck++;
      if (stuck == 1u && w->n_mv > 0u && !mi_switched_off(MI_PART_CORRECTION)) {
        unsigned what = 0u;
        if (doubt_one(w, order, plan, &plan_len, &planned_family, &what)) {
          lib->corrections++;
          if (what == 1u) res.corrected_actions++;
          else res.corrected_walls++;
          plan_at = 0u;
          stuck = 0u;
          no_plan_at = no_way_at = 0ull;
          continue;
        }
      }
      if (stuck == 1u && w->n_mv > 0u) {
        /* the weakest thing it holds is that an action does nothing: that was
           only ever inferred from moves that went nowhere. Given up first */
        unsigned b, forgot = 0u;
        for (b = 1u; b <= g->n_actions; b++) {
          if (w->mv[0].known[b] && w->mv[0].sr[b] == 0 && w->mv[0].sc[b] == 0) {
            w->mv[0].known[b] = 0;
            w->stayed[b] = 0u;
            forgot++;
          }
        }
        if (forgot > 0u) {
          memset(w->failed, 0, sizeof w->failed);
          stuck = 0u;
          continue;
        }
      }
      if (stuck >= MI_STRANDED && changed_here > 0u && g->restart != 0) {
        g->restart(g, &now);
        res.actions++;
        seg++;
        start_level(w, &now, res.levels_done);
        stuck = 0u;
        changed_here = 0u;
        continue;
      }
      if (stuck >= MI_STRANDED) {
        mode = MI_ASK;
        res.asked = 1;
        /* what it inferred blocks it is the weakest thing it holds */
        if (stuck == MI_STRANDED) {
          memset(w->wall_alive, 0, sizeof w->wall_alive);
          memset(w->visited, 0, sizeof w->visited);
        }
      }
      if (stuck >= MI_GIVE_UP) break;
      a = 1u + (res.actions % g->n_actions);
    } else {
      stuck = 0u;
    }
    note_mode(&res, mode);
#ifdef MI_DEBUG
    if (res.actions < 80u) {
      unsigned q, aliv = 0u, walls = 0u;
      for (q = 0u; q < MI_FAMILIES; q++) aliv += alive_in(w, q);
      for (q = 0u; q < PL_COLOURS; q++) walls |= (unsigned)is_wall(w, q) << q;
      fprintf(stderr, "t%u a%u mode %s plan %u/%u nmv %u", res.actions, a, mi_mode_name(mode), plan_at, plan_len, w->n_mv);
      for (q = 0u; q < w->n_mv; q++) {
        unsigned b;
        fprintf(stderr, " [m%u n%u at %d,%d seen%d tab%d push%d steps", q, w->mv[q].n, w->mv[q].r, w->mv[q].c, w->mv[q].seen, w->mv[q].may_table, w->mv[q].may_pushed);
        for (b = 1u; b <= w->n_actions; b++) fprintf(stderr, " %d:%d,%d", w->mv[q].known[b], w->mv[q].sr[b], w->mv[q].sc[b]);
        fprintf(stderr, "]");
      }
      fprintf(stderr, " alive %u walls %x stuck %u\n", aliv, walls, stuck);
    }
#endif

    /* ---- predict ---- */
    sure = predict(w, a, r0, c0, r1, c1);

    outcome = g->act(g, a, &next);
    res.actions++;
    w->tried[a]++;
    if (outcome == 3) {   /* the world started over by itself */
      now = next;
      seg++;
      start_level(w, &now, res.levels_done);
      plan_len = plan_at = 0u;
      continue;
    }

    /* ---- compare: where every mover is, against where it was predicted ---- */
    memset(&e, 0, sizeof e);
    if (!sure) e.guessed = 0xFFu;
    e.level = res.levels_done;
    e.seg = seg;
    e.ended = outcome != 0;
    if (outcome == 0) {
      unsigned before = w->n_mv, k;
      for (i = 0u; i < w->n_mv; i++) {
        mi_mover_t *m = &w->mv[i];
        int nr, nc, dr, dc;
        if (!m->seen) {
          m->seen = locate(m, &next, m->r, m->c, &m->r, &m->c);
          continue;
        }
        res.predictions++;
        if (!locate(m, &next, r1[i], c1[i], &nr, &nc)) {
          /* hidden or gone: it trusts its prediction for now */
          res.errors++;
          e.guessed |= (unsigned char)(1u << i);
          m->r = r1[i];
          m->c = c1[i];
          continue;
        }
        dr = nr - r0[i];
        dc = nc - c0[i];
        if (nr != r1[i] || nc != c1[i] || !sure) {
          if (nr != r1[i] || nc != c1[i]) res.errors++;
          /* the mismatch corrects the belief that made it */
          if (dr == 0 && dc == 0) {
            int sr, sc;
            if (moves_by_table(m) && step_of(w, i, a, &sr, &sc)) {
              /* it should have moved and did not: what was in the way may block */
              unsigned c2;
              for (c2 = 0u; c2 < m->n; c2++) {
                int rr = r0[i] + sr + m->dr[c2], cc = c0[i] + sc + m->dc[c2];
                if (rr >= 0 && cc >= 0 && rr < MI_W && cc < MI_W && !in_template(m, r0[i], c0[i], rr, cc)) {
                  w->wall_alive[w->ground[rr][cc]] = 1;
                  w->wall_fails[w->ground[rr][cc]]++;
                  /* it had doubted this, and the world answered: it blocks */
                  if (w->wall_doubted[w->ground[rr][cc]]) w->wall_confirmed[w->ground[rr][cc]] = 1;
                }
              }
            } else if (m->may_table && !m->known[a] && !m->may_pushed && i == 0u) {
              /* it did not move it from here: weak, since a wall may have stopped it.
                 Only after failing in three places is it taken to do nothing */
              w->failed[r0[0]][c0[0]] |= (unsigned char)(1u << a);
              if (++w->stayed[a] >= 3u) {
                m->known[a] = 1;
                m->sr[a] = 0;
                m->sc[a] = 0;
              }
            }
          } else {
            int pushed_by = -1;
            unsigned j;
            for (j = 0u; j < w->n_mv; j++) {
              if (j != i && r1[j] - r0[j] == dr && c1[j] - c0[j] == dc &&
                  overlaps(&w->mv[j], r1[j], c1[j], m, r0[i], c0[i]))
                pushed_by = (int)j;
            }
            if (pushed_by < 0) m->may_pushed = 0;   /* it moved with nothing pushing it */
            if (m->may_table && (!m->known[a] || m->sr[a] != dr || m->sc[a] != dc)) {
              if (m->known[a] && (m->sr[a] != 0 || m->sc[a] != 0) && pushed_by < 0) {
                /* a step it was sure of, contradicted: a puzzle */
                res.puzzles++;
              }
              if (pushed_by < 0) {
                m->known[a] = 1;
                m->sr[a] = dr;
                m->sc[a] = dc;
              }
            }
            m->moved_ever = 1;
          }
        } else if (moves_by_table(m) && !m->known[a]) {
          m->known[a] = 1;
          m->sr[a] = dr;
          m->sc[a] = dc;
        }
        if (dr != 0 || dc != 0) {
          /* it moved onto these: none of them blocks */
          unsigned msk = covers(w, m, nr, nc);
          for (k = 0u; k < PL_COLOURS; k++) {
            if (msk & (1u << k)) w->wall_out[k] = 1;
          }
        }
        /* a mover that stayed still while others moved, and was never pushed,
           was pushed only by what touched it: the table belief goes */
        m->r = nr;
        m->c = nc;
      }
      /* the ground and anything new that moved */
      if (find_new_movers(w, &now, &next, a, r0, c0) > 0u) {
        res.puzzles += before > 0u ? 1u : 0u;
        plan_len = plan_at = 0u;
        refresh_ground(w, &next);
        /* now that it knows which things are movers, the rest is read again */
        read_relations(w);
        if (res.levels_done < PL_MAX_LEVELS) {
          LEVEL_ODD[res.levels_done] = w->odd;
          LEVEL_NLEG[res.levels_done] = w->n_legend;
          memcpy(LEVEL_LEG[res.levels_done], w->legend, sizeof w->legend);
        }
      }
      refresh_ground(w, &next);
      /* something changed beyond the movers: the world is not what its plan assumed */
      {
        int r, c, other = 0;
        for (r = 0; r < MI_W && !other; r++) {
          for (c = 0; c < MI_W; c++) {
            if (now.c[r][c] != next.c[r][c] && !under_mover(w, r, c, -1) &&
                !(w->n_mv > 0u && (in_template(&w->mv[0], r0[0], c0[0], r, c)))) {
              int was_mover = 0;
              for (i = 0u; i < w->n_mv; i++) {
                if (in_template(&w->mv[i], r0[i], c0[i], r, c)) was_mover = 1;
              }
              if (!was_mover) {
                other = 1;
                break;
              }
            }
          }
        }
        if (other) {
          changed_here++;
          plan_len = plan_at = 0u;
          read_relations(w);
        }
      }
      if (!w->transform) {
        w->transform = find_transform(w);
        if (w->transform) res.transform = w->transform;
      }
      /* any mismatch: the plan was made from beliefs that were wrong */
      for (i = 0u; i < w->n_mv; i++) {
        if (w->mv[i].r != r1[i] || w->mv[i].c != c1[i]) plan_len = plan_at = 0u;
      }
    } else {
      /* the level ended: the frame shown is the next one, so where things were
         at the ending is what it predicted, not what it saw */
      e.guessed = 0xFFu;
      for (i = 0u; i < w->n_mv; i++) {
        w->mv[i].r = r1[i];
        w->mv[i].c = c1[i];
      }
    }

    /* ---- the event, and judging every belief about endings by it ---- */
    for (i = 0u; i < w->n_mv && i < MI_MAX_MOVERS; i++) {
      e.moved[i] = (unsigned char)(w->mv[i].r != r0[i] || w->mv[i].c != c0[i]);
      e.covered[i] = (unsigned short)covers(w, &w->mv[i], w->mv[i].r, w->mv[i].c);
    }
    if (N_HIST < MI_HISTORY) HIST[N_HIST++] = e;
    {
      unsigned before = hartley_bits(w), explained;
      int predicted_end = 0;
      unsigned m, k;
      /* did what it believed predict this? */
      for (m = 0u; m < w->n_mv; m++) {
        for (k = 0u; k < PL_COLOURS; k++) {
          if (w->cover[m][k] && (e.covered[m] & (1u << k))) predicted_end = 1;
        }
      }
      res.predictions++;
      explained = judge(w, &HIST[N_HIST - 1u], (int)N_HIST - 1);
      if (e.ended && explained == 0u) {
        res.puzzles++;
        lib->puzzles++;
      }
      /* an ending nothing it believed explains, or nothing it believes left
         standing at all: the language is too small, and the inventor's turn */
      if ((e.ended && explained == 0u) || hartley_bits(w) == 0u) {
        unsigned total = 0u;
        for (f = 0u; f < MI_FAMILIES; f++) total += alive_in(w, f);
#ifdef MI_DEBUG
        fprintf(stderr, "  inventor? ended %u explained %u total %u lang %d%d%d%d alive %u %u %u %u\n", e.ended, explained, total,
                w->lang[0], w->lang[1], w->lang[2], w->lang[3], alive_in(w, 0), alive_in(w, 1), alive_in(w, 2), alive_in(w, 3));
#endif
        if (total == 0u && w->n_mv > 0u && !mi_switched_off(MI_PART_CORRECTION)) {
          /* first: was an idea it had ruled out wrongly, by a moment it only inferred? */
          unsigned back = correct_endings(w);
          if (back > 0u) {
            res.corrected_endings += back;
            lib->corrections++;
            total = back;
          }
        }
        if (total == 0u && w->n_mv > 0u && !mi_switched_off(MI_PART_INVENTOR)) {
          for (f = MI_ODD; f < MI_FAMILIES; f++) {
            if (w->lang[f]) continue;
            if (invent(w, f) > 0u) {
              res.invented |= 1u << f;
              lib->inventions++;
            }
          }
        }
      }
      if ((int)e.ended != predicted_end) res.errors++;
      if (hartley_bits(w) != before) plan_len = plan_at = 0u;
    }

    if (outcome == 0) {
      now = next;
      continue;
    }

    /* ---- a level ended ---- */
    w->endings++;
    if (res.levels_done < PL_MAX_LEVELS) res.level_actions[res.levels_done] = res.actions - level_start;
    res.levels_done++;
    level_start = res.actions;
    plan_len = plan_at = 0u;
    stuck = 0u;
    changed_here = 0u;
    if (outcome == 2 || res.levels_done >= g->levels) break;
    now = next;
    seg++;
    start_level(w, &now, res.levels_done);
    if (res.levels_done < PL_MAX_LEVELS) res.level_shortest[res.levels_done] = g->shortest ? g->shortest(g) : 0u;
  }

  /* ---- after the world: what explained it, and what to carry ---- */
  res.movers = w->n_mv;
  if (w->endings > 0u) {
    for (f = 0u; f < MI_FAMILIES && res.winner < 0; f++) {
      if (alive_in(w, f) > 0u) res.winner = (int)f;
    }
  }
  lib->worlds++;
  if (res.winner >= 0 && w->endings >= 2u) {
    lib->wins[res.winner]++;
    if (lib->wins[res.winner] >= MI_PACK_AT) lib->packed[res.winner] = 1;
  }
  if (res.transform) lib->mirror_wins++;
  if (res.intuition >= 0 && res.winner >= 0) {
    lib->intuition_named[res.intuition]++;
    if (res.intuition == res.winner) {
      lib->intuition_right++;
      lib->intuition_hit[res.intuition]++;
    } else {
      lib->intuition_wrong++;
    }
  }
  {
    unsigned slot = lib->n_mem < MI_MEMORY ? lib->n_mem++ : (lib->worlds % MI_MEMORY);
    memcpy(lib->mem_fp[slot], fp, MI_FP_K);
    lib->mem_family[slot] = (unsigned char)(res.winner >= 0 && w->endings >= 2u ? res.winner + 1 : 0);
  }
  if (!EXPERIMENTING && (res.levels_done < g->levels || res.puzzles > 0u)) keep_for_sleep();
  *out = res;
  return res.levels_done >= g->levels ? SM_OK : SM_ERR_EMPTY_DOMAIN;
}

unsigned mi_score(const mi_result_t *r, unsigned levels) {
  unsigned i, weight = 0u;
  double total = 0.0;
  if (r == 0 || levels == 0u) return 0u;
  for (i = 0u; i < levels && i < PL_MAX_LEVELS; i++) {
    weight += i + 1u;
    if (i < r->levels_done && r->level_actions[i] > 0u && r->level_shortest[i] > 0u) {
      double e = (double)r->level_shortest[i] / (double)r->level_actions[i];
      if (e > 1.0) e = 1.0;
      total += (double)(i + 1u) * e * e;
    }
  }
  return (unsigned)(1000.0 * total / (double)weight + 0.5);
}

/* ============================================================================
 * SLEEP
 * ==========================================================================*/

void mi_sleep(mi_library_t *lib, FILE *journal) {
  unsigned d, f, found = 0u;
  if (lib == 0) return;
  for (d = 0u; d < N_DREAMS; d++) {
    /* replay the world through every block, the way the inventor would have */
    memcpy(HIST, DREAM[d], sizeof(mi_event_t) * DREAM_N[d]);
    N_HIST = DREAM_N[d];
    memcpy(LEVEL_ODD, DREAM_ODD[d], sizeof LEVEL_ODD);
    memcpy(LEVEL_NLEG, DREAM_NLEG[d], sizeof LEVEL_NLEG);
    memcpy(LEVEL_LEG, DREAM_LEG[d], sizeof LEVEL_LEG);
    for (f = MI_ODD; f < MI_FAMILIES; f++) {
      unsigned m, endings = 0u, i;
      for (i = 0u; i < N_HIST; i++) endings += HIST[i].ended;
      if (endings == 0u) continue;
      for (m = 0u; m < DREAM_MOVERS[d] && m < MI_MAX_MOVERS; m++) {
        unsigned p;
        if (replay(f, m, &p)) {
          lib->dreamt[f]++;
          found++;
          if (journal != 0) {
            fprintf(journal, "    in sleep, a world it could not explain fits: %s\n",
                    mi_family_name(f));
          }
          break;
        }
      }
    }
  }
  if (journal != 0) {
    fprintf(journal, "  it sleeps: %u world%s replayed, %u idea%s found to fit;", N_DREAMS,
            N_DREAMS == 1u ? "" : "s", found, found == 1u ? "" : "s");
    fprintf(journal, " packed:");
    for (f = 0u; f < MI_FAMILIES; f++) {
      if (lib->packed[f]) fprintf(journal, " [%s]", mi_family_name(f));
    }
    fprintf(journal, "\n");
  }
  N_DREAMS = 0u;
}

/* ============================================================================
 * OPTOGENETICS ON ITSELF
 * ==========================================================================*/

static unsigned score_over(mi_library_t *lib, void (*make)(pl_game_t *g, unsigned i), unsigned n,
                           unsigned budget) {
  unsigned i, total = 0u;
  for (i = 0u; i < n; i++) {
    pl_game_t g;
    mi_result_t r;
    make(&g, i);
    mi_play(lib, &g, budget, MI_FAMILIES, &r);
    total += mi_score(&r, g.levels);
  }
  return total / (n > 0u ? n : 1u);
}

void mi_probe(mi_library_t *lib, void (*make)(pl_game_t *g, unsigned i), unsigned n,
              unsigned budget, FILE *journal) {
  static mi_library_t copy;
  unsigned p, q, base;
  unsigned alone[MI_PARTS];
  int was[MI_PARTS];
  if (lib == 0 || make == 0) return;
  for (p = 0u; p < MI_PARTS; p++) was[p] = OFF[p];
  EXPERIMENTING = 1;
  copy = *lib;
  base = score_over(&copy, make, n, budget);
  if (journal != 0) {
    fprintf(journal, "  it experiments on itself, over %u worlds: score %u.%u%% with every part on\n", n,
            base / 10u, base % 10u);
  }
  for (p = 0u; p < MI_PARTS; p++) {
    OFF[p] = 1;
    copy = *lib;
    alone[p] = score_over(&copy, make, n, budget);
    OFF[p] = was[p];
    lib->probe_delta[p] = (int)base - (int)alone[p];
    if (journal != 0) {
      fprintf(journal, "    without its %s: %u.%u%% (%+.1f)\n", mi_part_name(p), alone[p] / 10u,
              alone[p] % 10u, ((double)alone[p] - (double)base) / 10.0);
    }
  }
  for (p = 0u; p < MI_PARTS; p++) {
    for (q = p + 1u; q < MI_PARTS; q++) {
      unsigned both;
      int lost_both, lost_each;
      OFF[p] = 1;
      OFF[q] = 1;
      copy = *lib;
      both = score_over(&copy, make, n, budget);
      OFF[p] = was[p];
      OFF[q] = was[q];
      lib->probe_pair[p][q] = lib->probe_pair[q][p] = (int)base - (int)both;
      lost_both = (int)base - (int)both;
      lost_each = ((int)base - (int)alone[p]) + ((int)base - (int)alone[q]);
      /* both together cost clearly more than each alone added up: each was
         doing the other's work */
      if (journal != 0 && lost_both >= 20 && lost_both > lost_each + 20) {
        fprintf(journal, "    its %s and its %s stand in for each other: without both, %u.%u%%\n",
                mi_part_name(p), mi_part_name(q), both / 10u, both % 10u);
      }
    }
  }
  EXPERIMENTING = 0;
  lib->probes++;
}

/* ============================================================================
 * MEMORY ON DISK
 * ==========================================================================*/

void mi_library_save(const mi_library_t *lib, FILE *out) {
  unsigned i, k;
  if (lib == 0 || out == 0) return;
  fprintf(out, "lib.worlds %u\n", lib->worlds);
  for (i = 0u; i < MI_FAMILIES; i++) {
    fprintf(out, "lib.family %u %u %u %d\n", i, lib->wins[i], lib->dreamt[i], lib->packed[i]);
  }
  fprintf(out, "lib.mirror %u\n", lib->mirror_wins);
  fprintf(out, "lib.intuition %u %u\n", lib->intuition_right, lib->intuition_wrong);
  fprintf(out, "lib.counts %u %u %u %u\n", lib->puzzles, lib->inventions, lib->probes, lib->corrections);
  for (i = 0u; i < MI_PARTS; i++) fprintf(out, "lib.probe %u %d\n", i, lib->probe_delta[i]);
  for (i = 0u; i < MI_PARTS; i++) {
    for (k = i + 1u; k < MI_PARTS; k++) fprintf(out, "lib.pair %u %u %d\n", i, k, lib->probe_pair[i][k]);
  }
  for (i = 0u; i < MI_FAMILIES; i++) {
    fprintf(out, "lib.trust %u %u %u\n", i, lib->intuition_named[i], lib->intuition_hit[i]);
  }
  for (i = 0u; i < lib->n_mem; i++) {
    fprintf(out, "lib.mem %u %u", i, lib->mem_family[i]);
    for (k = 0u; k < MI_FP_K; k++) fprintf(out, " %u", lib->mem_fp[i][k]);
    fprintf(out, "\n");
  }
}

int mi_library_line(mi_library_t *lib, const char *line) {
  unsigned a, b, c, k, v[MI_FP_K];
  int d;
  if (lib == 0 || line == 0 || strncmp(line, "lib.", 4) != 0) return 0;
  if (sscanf(line, "lib.worlds %u", &a) == 1) {
    lib->worlds = a;
  } else if (sscanf(line, "lib.family %u %u %u %d", &a, &b, &c, &d) == 4 && a < MI_FAMILIES) {
    lib->wins[a] = b;
    lib->dreamt[a] = c;
    lib->packed[a] = d != 0;
  } else if (sscanf(line, "lib.mirror %u", &a) == 1) {
    lib->mirror_wins = a;
  } else if (sscanf(line, "lib.intuition %u %u", &a, &b) == 2) {
    lib->intuition_right = a;
    lib->intuition_wrong = b;
  } else if (sscanf(line, "lib.counts %u %u %u", &a, &b, &c) == 3) {
    unsigned corr = 0u;
    lib->puzzles = a;
    lib->inventions = b;
    lib->probes = c;
    if (sscanf(line, "lib.counts %*u %*u %*u %u", &corr) == 1) lib->corrections = corr;
  } else if (sscanf(line, "lib.probe %u %d", &a, &d) == 2 && a < MI_PARTS) {
    lib->probe_delta[a] = d;
  } else if (sscanf(line, "lib.pair %u %u %d", &a, &b, &d) == 3 && a < MI_PARTS && b < MI_PARTS) {
    lib->probe_pair[a][b] = lib->probe_pair[b][a] = d;
  } else if (sscanf(line, "lib.trust %u %u %u", &a, &b, &c) == 3 && a < MI_FAMILIES) {
    lib->intuition_named[a] = b;
    lib->intuition_hit[a] = c;
  } else if (sscanf(line, "lib.mem %u %u %u %u %u %u %u %u %u %u", &a, &b, &v[0], &v[1], &v[2],
                    &v[3], &v[4], &v[5], &v[6], &v[7]) == 10 &&
             a < MI_MEMORY) {
    for (k = 0u; k < MI_FP_K; k++) lib->mem_fp[a][k] = (unsigned char)v[k];
    lib->mem_family[a] = (unsigned char)b;
    if (a + 1u > lib->n_mem) lib->n_mem = a + 1u;
  }
  return 1;
}
