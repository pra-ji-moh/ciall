/*
 * smarsh_rules.c -- see smarsh_rules.h.
 *
 * Rules are numbered: 0 .. RU_MOVES-1 are go(dr, dc) for dr, dc in -RU_REACH..RU_REACH
 * (go(0, 0) is "nothing happens to it"); then gone; then changed size; then recoloured.
 * Each (kind, act) holds one bit per rule still possible.
 */
#include "smarsh_rules.h"

#include <stdlib.h>
#include <string.h>

#define R_MOVE(dr, dc) ((unsigned)(((dr) + RU_REACH) * (int)RU_SIDE + ((dc) + RU_REACH)))
#define R_NOTHING R_MOVE(0, 0)
#define R_GONE (RU_MOVES)
#define R_SIZE (RU_MOVES + 1u)
#define R_COLOUR (RU_MOVES + 2u)
/*
 * go(dr, dc) unless something is in the way: it moves, or, where any cell it would
 * move into holds something other than ground (or lies off the board), it stays.
 * Solid things do not pass through each other. This is the rule a child knows
 * without being told; here it is only one more rule, and it too is ruled out the
 * first time a thing moves into something or stays with nothing in its way.
 */
#define R_BMOVE(dr, dc) (RU_MOVES + 3u + R_MOVE(dr, dc))
#define R_ALL (2u * RU_MOVES + 3u)

static unsigned GROUND;   /* the colour of the biggest thing in the picture being read */
/*
 * What a thing can move into is not assumed: in some games the floor is the
 * commonest colour, in others the walls are. A colour is passable once a thing has
 * been seen to move into it; every other colour is in the way until then.
 */
static uint16_t PASSABLE;

/* would a move by (dr, dc) put any cell of it where something other than ground, or itself, is? */
static int blocked(const ru_thing_t *t, const pl_frame_t *f, int dr, int dc) {
  unsigned r, c;
  for (r = t->top; r <= t->bottom; r++) {
    for (c = t->left; c <= t->right; c++) {
      int nr, nc;
      unsigned v;
      if (f->c[r][c] != t->colour) continue;
      nr = (int)r + dr;
      nc = (int)c + dc;
      if (nr < 0 || nc < 0 || nr >= (int)f->h || nc >= (int)f->w) return 1;
      v = f->c[nr][nc];
      if (v != t->colour && !((PASSABLE >> v) & 1u)) return 1;
    }
  }
  return 0;
}

static unsigned size_class(unsigned cells) {
  if (cells <= 1u) return 0u;
  if (cells <= 2u) return 1u;
  if (cells <= 4u) return 2u;
  if (cells <= 9u) return 3u;
  if (cells <= 30u) return 4u;
  return 5u;
}

unsigned ru_kind(const ru_thing_t *t) {
  /* colour and shape; a big field of ground is one kind whatever its shape */
  unsigned hgt = t->bottom - t->top + 1u, wid = t->right - t->left + 1u;
  if (t->cells > 400u) return (t->colour * 64u) % RU_KINDS;
  return (t->colour * 7919u + hgt * 131u + wid * 17u + t->cells) % RU_KINDS;
}

/* ---- sets of rules ------------------------------------------------------------ */

static void set_all(uint64_t *b) {
  unsigned i;
  for (i = 0u; i < RU_WORDS; i++) b[i] = 0u;
  for (i = 0u; i < R_ALL; i++) b[i / 64u] |= 1ull << (i % 64u);
}

static int has(const uint64_t *b, unsigned r) {
  return (int)((b[r / 64u] >> (r % 64u)) & 1ull);
}

static void put(uint64_t *b, unsigned r) {
  b[r / 64u] |= 1ull << (r % 64u);
}

static unsigned count(const uint64_t *b) {
  unsigned i, n = 0u;
  for (i = 0u; i < RU_WORDS; i++) {
    uint64_t x = b[i];
    while (x) {
      x &= x - 1u;
      n++;
    }
  }
  return n;
}

/* the one rule left, if one is */
static int only_rule(const uint64_t *b, unsigned *rule) {
  unsigned i;
  if (count(b) != 1u) return 0;
  for (i = 0u; i < R_ALL; i++) {
    if (has(b, i)) {
      *rule = i;
      return 1;
    }
  }
  return 0;
}

void ru_begin(ru_world_t *w) {
  unsigned k, a;
  memset(w, 0, sizeof *w);
  for (k = 0u; k < RU_KINDS; k++) {
    for (a = 0u; a < RU_ACTS; a++) set_all(w->left[k][a]);
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
static void happened(const ru_thing_t *t, const pl_frame_t *before, const pl_frame_t *after, uint64_t *ok) {
  int dr, dc;
  unsigned n = cells_now(t, after), i;
  for (i = 0u; i < RU_WORDS; i++) ok[i] = 0u;
  for (dr = -RU_REACH; dr <= RU_REACH; dr++) {
    for (dc = -RU_REACH; dc <= RU_REACH; dc++) {
      if (dr == 0 && dc == 0) {
        if (n == t->cells && moved_by(t, before, after, 0, 0)) put(ok, R_NOTHING);
      } else if (moved_by(t, before, after, dr, dc)) {
        put(ok, R_MOVE(dr, dc));
      }
    }
  }
  {
    /* each move, "unless something is in the way": true if it moved and nothing was in the
       way, or if it stayed and something was */
    int stayed = (n == t->cells && moved_by(t, before, after, 0, 0));
    for (dr = -RU_REACH; dr <= RU_REACH; dr++) {
      for (dc = -RU_REACH; dc <= RU_REACH; dc++) {
        int in_way;
        if (dr == 0 && dc == 0) continue;
        in_way = blocked(t, before, dr, dc);
        if ((in_way && stayed) || (!in_way && moved_by(t, before, after, dr, dc))) put(ok, R_BMOVE(dr, dc));
      }
    }
  }
  if (!still_there(t, after)) put(ok, R_GONE);
  if (n != t->cells && n > 0u) put(ok, R_SIZE);
  {
    unsigned r, c, other = PL_COLOURS, one = 1u;
    for (r = t->top; r <= t->bottom && one; r++) {
      for (c = t->left; c <= t->right && one; c++) {
        if (before->c[r][c] != t->colour) continue;
        if (after->c[r][c] == t->colour) continue;
        if (other == PL_COLOURS) other = after->c[r][c];
        else if (other != after->c[r][c]) one = 0u;
      }
    }
    if (one && other != PL_COLOURS) put(ok, R_COLOUR);
  }
}

/* ---- reading what an act did ------------------------------------------------------ */

unsigned ru_saw(ru_world_t *w, unsigned act, const pl_frame_t *before, const pl_frame_t *after) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n = ru_things(before, things, RU_MAX_THINGS), i, j, cut = 0u, most = 0u;
  if (act >= RU_ACTS) return 0u;
  for (i = 0u; i < n; i++) {
    if (things[i].cells > most) {
      most = things[i].cells;
      GROUND = things[i].colour;
    }
  }
  /* first, what the things that moved moved into: those colours are passable */
  PASSABLE = w->passable;
  for (i = 0u; i < n; i++) {
    int dr, dc;
    for (dr = -RU_REACH; dr <= RU_REACH; dr++) {
      for (dc = -RU_REACH; dc <= RU_REACH; dc++) {
        unsigned r, c;
        if ((dr == 0 && dc == 0) || !moved_by(&things[i], before, after, dr, dc)) continue;
        for (r = things[i].top; r <= things[i].bottom; r++) {
          for (c = things[i].left; c <= things[i].right; c++) {
            int nr = (int)r + dr, nc = (int)c + dc;
            unsigned v;
            if (before->c[r][c] != things[i].colour) continue;
            v = before->c[nr][nc];
            if (v != things[i].colour) PASSABLE |= (uint16_t)(1u << v);
          }
        }
      }
    }
  }
  w->passable = PASSABLE;
  for (i = 0u; i < n; i++) {
    unsigned k = ru_kind(&things[i]), rule, was;
    uint64_t could[RU_WORDS];
    uint64_t *left = w->left[k][act];
    happened(&things[i], before, after, could);
    /* what it could have said of this thing before the act, and whether it was right */
    if (w->seen[k][act] > 0u && only_rule(left, &rule)) {
      w->said++;
      if (has(could, rule)) w->said_right++;
      else w->said_wrong++;
    } else {
      w->cannot_say++;
    }
    was = count(left);
    for (j = 0u; j < RU_WORDS; j++) left[j] &= could[j];   /* every rule that says otherwise is ruled out */
    w->seen[k][act]++;
    cut += was - count(left);
  }
  w->ruled_out += cut;
  return cut;
}

/* ---- saying and imagining ------------------------------------------------------------ */

int ru_say(ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n = ru_things(now, things, RU_MAX_THINGS), i;
  if (act >= RU_ACTS) return 0;
  for (i = 0u; i < n; i++) {
    unsigned rule;
    if (!only_rule(w->left[ru_kind(&things[i])][act], &rule) || (rule >= RU_MOVES && rule < RU_MOVES + 3u)) return 0;
  }
  ru_imagine(w, act, now, out);
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
      if (rule == R_NOTHING) continue;
      return 0;
    }
    for (a2 = 0u; a2 < RU_ACTS; a2++) {
      if (w->seen[k][a2] == 0u) continue;
      met++;
      if (!has(w->left[k][a2], R_NOTHING) || count(w->left[k][a2]) > 1u) stirs++;
    }
    if (met == 0u || stirs > 0u) return 0;
  }
  return n > 0u;
}

void ru_imagine(const ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out) {
  static ru_thing_t things[RU_MAX_THINGS];
  static unsigned rule_of[RU_MAX_THINGS];
  unsigned n = ru_things(now, things, RU_MAX_THINGS), i, r, c, ground = 0u, most = 0u;
  *out = *now;
  if (act >= RU_ACTS) return;
  for (i = 0u; i < n; i++) {
    if (things[i].cells > most) {
      most = things[i].cells;
      ground = things[i].colour;
    }
  }
  GROUND = ground;
  PASSABLE = w->passable;
  for (i = 0u; i < n; i++) {
    unsigned rule = R_NOTHING;
    if (w->seen[ru_kind(&things[i])][act] == 0u || !only_rule(w->left[ru_kind(&things[i])][act], &rule)) {
      rule = R_NOTHING;   /* unsettled: imagined where it is -- a hypothesis, checked on the way */
    }
    if (rule == R_SIZE || rule == R_COLOUR) rule = R_NOTHING;   /* it changes, but it cannot say into what */
    if (rule >= RU_MOVES + 3u) {   /* moves unless something is in the way */
      int dr = (int)((rule - RU_MOVES - 3u) / RU_SIDE) - RU_REACH, dc = (int)((rule - RU_MOVES - 3u) % RU_SIDE) - RU_REACH;
      rule = blocked(&things[i], now, dr, dc) ? R_NOTHING : R_MOVE(dr, dc);
    }
    rule_of[i] = rule;
    if (rule == R_NOTHING) continue;
    for (r = things[i].top; r <= things[i].bottom; r++) {
      for (c = things[i].left; c <= things[i].right; c++) {
        if (now->c[r][c] == things[i].colour) out->c[r][c] = (unsigned char)ground;
      }
    }
  }
  for (i = 0u; i < n; i++) {
    int dr, dc;
    if (rule_of[i] == R_NOTHING || rule_of[i] >= RU_MOVES) continue;
    dr = (int)(rule_of[i] / RU_SIDE) - RU_REACH;
    dc = (int)(rule_of[i] % RU_SIDE) - RU_REACH;
    for (r = things[i].top; r <= things[i].bottom; r++) {
      for (c = things[i].left; c <= things[i].right; c++) {
        int nr = (int)r + dr, nc = (int)c + dc;
        if (now->c[r][c] != things[i].colour) continue;
        if (nr < 0 || nc < 0 || nr >= (int)now->h || nc >= (int)now->w) continue;
        out->c[nr][nc] = (unsigned char)things[i].colour;
      }
    }
  }
}

double ru_bits(const ru_world_t *w) {
  double bits = 0.0;
  unsigned k, a;
  for (k = 0u; k < RU_KINDS; k++) {
    for (a = 0u; a < RU_ACTS; a++) {
      unsigned n = count(w->left[k][a]);
      if (w->seen[k][a] == 0u || n == 0u) continue;
      while (n > 1u) {
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
      if (count(w->left[k][a]) == 1u) settled++;
    }
  }
  fprintf(out, "  what each act does to each kind of thing: %u of %u settled to one rule (%.0f bits left); "
               "of things it could say about before an act, right %u of %u (%.0f%%); could not say of %u\n",
          settled, met, ru_bits(w), w->said_right, w->said,
          w->said ? 100.0 * (double)w->said_right / (double)w->said : 0.0, w->cannot_say);
  if (w->spared > 0u) {
    fprintf(out, "  acts it did not spend because its rules said they change nothing: %u\n", w->spared);
  }
  return SM_OK;
}
