/*
 * smarsh_rules.c -- see smarsh_rules.h.
 */
#include "smarsh_rules.h"

#include <string.h>

/* rule numbers: 0 nothing; 1..25 go(dr, dc) with dr, dc in -2..2; 26 gone; 27 size changed; 28 recoloured */
#define R_NOTHING 0u
#define R_GO 1u
#define R_GONE (R_GO + RU_MOVES)
#define R_SIZE (R_GONE + 1u)
#define R_COLOUR (R_SIZE + 1u)

static unsigned size_class(unsigned cells) {
  if (cells <= 1u) return 0u;
  if (cells <= 2u) return 1u;
  if (cells <= 4u) return 2u;
  if (cells <= 9u) return 3u;
  if (cells <= 30u) return 4u;
  return 5u;
}

unsigned ru_kind(const ru_thing_t *t) {
  return t->colour * 6u + size_class(t->cells);
}

void ru_begin(ru_world_t *w) {
  unsigned k, a;
  memset(w, 0, sizeof *w);
  for (k = 0u; k < RU_KINDS; k++) {
    for (a = 0u; a < RU_ACTS; a++) w->left[k][a] = (1u << RU_RULES) - 1u;
  }
}

/* ---- the things in a picture ---------------------------------------------------- */

static unsigned char SEEN[PL_SIZE][PL_SIZE];
static int QR[PL_SIZE * PL_SIZE], QC[PL_SIZE * PL_SIZE];

unsigned ru_things(const pl_frame_t *f, ru_thing_t *out, unsigned cap) {
  unsigned r, c, n = 0u;
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  memset(SEEN, 0, sizeof SEEN);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      unsigned head = 0u, tail = 0u, colour = f->c[r][c];
      long sr = 0, sc = 0;
      ru_thing_t t;
      if (SEEN[r][c]) continue;
      SEEN[r][c] = 1u;
      QR[tail] = (int)r;
      QC[tail] = (int)c;
      tail++;
      t.colour = colour;
      t.top = t.bottom = r;
      t.left = t.right = c;
      t.cells = 0u;
      while (head < tail) {
        int rr = QR[head], cc = QC[head], k;
        head++;
        t.cells++;
        sr += rr;
        sc += cc;
        if ((unsigned)rr < t.top) t.top = (unsigned)rr;
        if ((unsigned)rr > t.bottom) t.bottom = (unsigned)rr;
        if ((unsigned)cc < t.left) t.left = (unsigned)cc;
        if ((unsigned)cc > t.right) t.right = (unsigned)cc;
        for (k = 0; k < 4; k++) {
          int nr = rr + dr[k], nc = cc + dc[k];
          if (nr < 0 || nc < 0 || nr >= (int)f->h || nc >= (int)f->w) continue;
          if (SEEN[nr][nc] || f->c[nr][nc] != colour) continue;
          SEEN[nr][nc] = 1u;
          QR[tail] = nr;
          QC[tail] = nc;
          tail++;
        }
      }
      t.row = (unsigned)(sr / (long)t.cells);
      t.col = (unsigned)(sc / (long)t.cells);
      t.size = size_class(t.cells);
      if (n < cap) out[n++] = t;
    }
  }
  return n;
}

/* ---- what happened to one thing -------------------------------------------------- */

/* is every cell of `t`, moved by (dr, dc), of the thing's colour in `after`? */
static int moved_by(const ru_thing_t *t, const pl_frame_t *before, const pl_frame_t *after, int dr, int dc) {
  unsigned r, c, n = 0u;
  for (r = t->top; r <= t->bottom; r++) {
    for (c = t->left; c <= t->right; c++) {
      int nr, nc;
      if (before->c[r][c] != t->colour) continue;
      nr = (int)r + dr;
      nc = (int)c + dc;
      if (nr < 0 || nc < 0 || nr >= (int)after->h || nc >= (int)after->w) return 0;
      if (after->c[nr][nc] != t->colour) return 0;
      n++;
    }
  }
  return n > 0u;
}

static int still_there(const ru_thing_t *t, const pl_frame_t *after) {
  unsigned r, c;
  for (r = t->top; r <= t->bottom; r++) {
    for (c = t->left; c <= t->right; c++) {
      if (after->c[r][c] == t->colour) return 1;
    }
  }
  return 0;
}

/* how many cells of the thing's colour are in its box now */
static unsigned cells_now(const ru_thing_t *t, const pl_frame_t *after) {
  unsigned r, c, n = 0u;
  for (r = t->top; r <= t->bottom; r++) {
    for (c = t->left; c <= t->right; c++) {
      if (after->c[r][c] == t->colour) n++;
    }
  }
  return n;
}

/* the rules that could be true of what happened to this thing */
static uint32_t happened(const ru_thing_t *t, const pl_frame_t *before, const pl_frame_t *after) {
  uint32_t ok = 0u;
  int dr, dc;
  unsigned n = cells_now(t, after);
  if (n == t->cells && moved_by(t, before, after, 0, 0)) ok |= 1u << R_NOTHING;
  for (dr = -2; dr <= 2; dr++) {
    for (dc = -2; dc <= 2; dc++) {
      unsigned bit = R_GO + (unsigned)((dr + 2) * 5 + (dc + 2));
      if ((dr != 0 || dc != 0) && moved_by(t, before, after, dr, dc)) ok |= 1u << bit;
    }
  }
  if (!still_there(t, after)) ok |= 1u << R_GONE;
  if (n != t->cells && n > 0u) ok |= 1u << R_SIZE;
  {
    /* recoloured: its box now holds one other colour where its cells were */
    unsigned r, c, other = PL_COLOURS, one = 1u;
    for (r = t->top; r <= t->bottom && one; r++) {
      for (c = t->left; c <= t->right && one; c++) {
        if (before->c[r][c] != t->colour) continue;
        if (after->c[r][c] == t->colour) continue;
        if (other == PL_COLOURS) other = after->c[r][c];
        else if (other != after->c[r][c]) one = 0u;
      }
    }
    if (one && other != PL_COLOURS) ok |= 1u << R_COLOUR;
  }
  return ok;
}

/* ---- reading what an act did ------------------------------------------------------ */

static int only_rule(uint32_t bits, unsigned *rule);

static unsigned popcount32(uint32_t x) {
  unsigned n = 0u;
  while (x) {
    x &= x - 1u;
    n++;
  }
  return n;
}

unsigned ru_saw(ru_world_t *w, unsigned act, const pl_frame_t *before, const pl_frame_t *after) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n = ru_things(before, things, RU_MAX_THINGS), i, cut = 0u;
  if (act >= RU_ACTS) return 0u;
  for (i = 0u; i < n; i++) {
    unsigned k = ru_kind(&things[i]);
    uint32_t could = happened(&things[i], before, after);
    uint32_t was = w->left[k][act];
    {
      /* what it could have said of this thing before the act, and whether it was right */
      unsigned rule;
      if (only_rule(was, &rule)) {
        w->said++;
        if ((could >> rule) & 1u) w->said_right++;
        else w->said_wrong++;
      } else {
        w->cannot_say++;
      }
    }
    w->left[k][act] &= could;   /* every rule that says otherwise is ruled out */
    w->seen[k][act]++;
    cut += popcount32(was) - popcount32(w->left[k][act]);
  }
  w->ruled_out += cut;
  return cut;
}

/* ---- saying what will happen ------------------------------------------------------- */

static int only_rule(uint32_t bits, unsigned *rule) {
  if (bits == 0u || popcount32(bits) != 1u) return 0;
  {
    unsigned r = 0u;
    while (!((bits >> r) & 1u)) r++;
    *rule = r;
    return 1;
  }
}

int ru_say(ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n = ru_things(now, things, RU_MAX_THINGS), i, r, c;
  if (act >= RU_ACTS) return 0;
  w->said++;
  *out = *now;
  for (i = 0u; i < n; i++) {
    unsigned k = ru_kind(&things[i]), rule;
    if (!only_rule(w->left[k][act], &rule)) {
      w->cannot_say++;
      return 0;   /* more than one rule is still possible: it cannot say */
    }
    if (rule == R_GONE || rule == R_SIZE || rule == R_COLOUR) {
      w->cannot_say++;
      return 0;   /* it knows something changes, but not into what */
    }
  }
  /* every thing moved by what its one rule says; the ground shows where they were */
  for (i = 0u; i < n; i++) {
    const ru_thing_t *t = &things[i];
    unsigned rule;
    (void)only_rule(w->left[ru_kind(t)][act], &rule);
    if (rule == R_NOTHING) continue;
    for (r = t->top; r <= t->bottom; r++) {
      for (c = t->left; c <= t->right; c++) {
        if (now->c[r][c] == t->colour) out->c[r][c] = 0u;
      }
    }
  }
  for (i = 0u; i < n; i++) {
    const ru_thing_t *t = &things[i];
    unsigned rule;
    int dr, dc;
    (void)only_rule(w->left[ru_kind(t)][act], &rule);
    if (rule == R_NOTHING) continue;
    dr = (int)((rule - R_GO) / 5u) - 2;
    dc = (int)((rule - R_GO) % 5u) - 2;
    for (r = t->top; r <= t->bottom; r++) {
      for (c = t->left; c <= t->right; c++) {
        int nr = (int)r + dr, nc = (int)c + dc;
        if (now->c[r][c] != t->colour) continue;
        if (nr < 0 || nc < 0 || nr >= (int)now->h || nc >= (int)now->w) continue;
        out->c[nr][nc] = (unsigned char)t->colour;
      }
    }
  }
  return 1;
}

int ru_changes_nothing(const ru_world_t *w, unsigned act, const pl_frame_t *now) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n, i;
  if (act >= RU_ACTS) return 0;
  n = ru_things(now, things, RU_MAX_THINGS);
  for (i = 0u; i < n; i++) {
    unsigned k = ru_kind(&things[i]), rule, a2, stirs = 0u, met = 0u;
    if (w->seen[k][act] > 0u && only_rule(w->left[k][act], &rule)) {
      if (rule == R_NOTHING) continue;   /* this act does nothing to such a thing */
      return 0;
    }
    /* not settled for this act: it is still passed over if nothing has ever stirred
       such a thing, whatever was done -- the scenery of the world */
    for (a2 = 0u; a2 < RU_ACTS; a2++) {
      if (w->seen[k][a2] == 0u) continue;
      met++;
      if (!(w->left[k][a2] & (1u << R_NOTHING)) || popcount32(w->left[k][a2]) > 1u) stirs++;
    }
    if (met == 0u || stirs > 0u) return 0;
  }
  return n > 0u;
}

double ru_bits(const ru_world_t *w) {
  double bits = 0.0;
  unsigned k, a;
  for (k = 0u; k < RU_KINDS; k++) {
    for (a = 0u; a < RU_ACTS; a++) {
      unsigned n = popcount32(w->left[k][a]);
      if (w->seen[k][a] == 0u || n == 0u) continue;
      while (n > 1u) {   /* log2, counted the plain way */
        bits += 1.0;
        n >>= 1;
      }
    }
  }
  return bits;
}

sm_status_t ru_report(const ru_world_t *w, FILE *out) {
  unsigned k, a, settled = 0u, met = 0u;
  if (w == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  for (k = 0u; k < RU_KINDS; k++) {
    for (a = 0u; a < RU_ACTS; a++) {
      if (w->seen[k][a] == 0u) continue;
      met++;
      if (popcount32(w->left[k][a]) == 1u) settled++;
    }
  }
  fprintf(out, "  what each act does to each kind of thing: %u of %u settled to one rule (%.0f bits left); "
               "it said what would happen %u times, right %u, wrong %u, and would not say %u\n",
          settled, met, ru_bits(w), w->said, w->said_right, w->said_wrong, w->cannot_say);
  if (w->spared > 0u) {
    fprintf(out, "  acts it did not spend because its rules said they change nothing: %u\n", w->spared);
  }
  return SM_OK;
}
