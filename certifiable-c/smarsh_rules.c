/*
 * smarsh_rules.c -- see smarsh_rules.h.
 *
 * Rules are numbered: 0 .. RU_MOVES-1 are go(dr, dc) for dr, dc in -RU_REACH..RU_REACH
 * (go(0, 0) is "nothing happens to it"); then gone; then changed size; then recoloured.
 * Each (kind, act) holds one bit per rule still possible.
 */
#include "smarsh_rules.h"

#include <stdlib.h>
#include <math.h>
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
static void gather(const ru_thing_t *t, const pl_frame_t *f);
static unsigned short CELL_R[PL_SIZE * PL_SIZE], CELL_C[PL_SIZE * PL_SIZE];
static unsigned N_CELLS;

/* would a move by (dr, dc) put any of its cells where something it cannot pass is? */
static int blocked_cells(const ru_thing_t *t, const pl_frame_t *f, int dr, int dc) {
  unsigned i;
  for (i = 0u; i < N_CELLS; i++) {
    int nr = (int)CELL_R[i] + dr, nc = (int)CELL_C[i] + dc;
    unsigned v;
    if (nr < 0 || nc < 0 || nr >= (int)f->h || nc >= (int)f->w) return 1;
    v = f->c[nr][nc];
    if (v != t->colour && !((PASSABLE >> v) & 1u)) return 1;
  }
  return 0;
}

static int blocked(const ru_thing_t *t, const pl_frame_t *f, int dr, int dc) {
  gather(t, f);
  return blocked_cells(t, f, dr, dc);
}

static unsigned size_class(unsigned cells) {
  if (cells <= 1u) return 0u;
  if (cells <= 2u) return 1u;
  if (cells <= 4u) return 2u;
  if (cells <= 9u) return 3u;
  if (cells <= 30u) return 4u;
  return 5u;
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

unsigned ru_kind_way(const ru_thing_t *t, unsigned way) {
  unsigned hgt = t->bottom - t->top + 1u, wid = t->right - t->left + 1u;
  if (way == 2u) return t->colour % RU_KINDS;                                  /* its colour */
  if (way == 1u) return (t->colour * 64u + size_class(t->cells)) % RU_KINDS;   /* colour and size */
  if (t->cells > 400u) return (t->colour * 64u) % RU_KINDS;                    /* a whole field of ground */
  return (t->colour * 7919u + hgt * 131u + wid * 17u + t->cells) % RU_KINDS;   /* colour and shape */
}

unsigned ru_kind(const ru_thing_t *t) {
  return ru_kind_way(t, 0u);
}

/* the rule of the narrowest way of saying what it is that has settled to one */
static int settled_rule(const ru_world_t *w, const ru_thing_t *t, unsigned act, unsigned *rule) {
  unsigned way;
  for (way = 0u; way < RU_WAYS; way++) {
    unsigned k = ru_kind_way(t, way);
    if (w->seen[way][k][act] == 0u) continue;
    if (only_rule(w->left[way][k][act], rule)) return 1;
  }
  return 0;
}

void ru_begin(ru_world_t *w) {
  unsigned k, a;
  memset(w, 0, sizeof *w);
  {
    unsigned way;
    for (way = 0u; way < RU_WAYS; way++) {
      for (k = 0u; k < RU_KINDS; k++) {
        for (a = 0u; a < RU_ACTS; a++) set_all(w->left[way][k][a]);
      }
    }
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

/*
 * The cells of a thing, gathered once. Walking its whole box for each of the 289
 * moves it might have made, and again for each move something might be in the way
 * of, was nearly all the time a turn took.
 */

static void gather(const ru_thing_t *t, const pl_frame_t *f) {
  unsigned r, c;
  N_CELLS = 0u;
  for (r = t->top; r <= t->bottom; r++) {
    for (c = t->left; c <= t->right; c++) {
      if (f->c[r][c] != t->colour) continue;
      CELL_R[N_CELLS] = (unsigned short)r;
      CELL_C[N_CELLS] = (unsigned short)c;
      N_CELLS++;
    }
  }
}

/* is every cell of `t`, moved by (dr, dc), of the thing's colour in `after`? */
static int moved_cells(const ru_thing_t *t, const pl_frame_t *after, int dr, int dc) {
  unsigned i;
  if (N_CELLS == 0u) return 0;
  {   /* one cell first: nearly every move is ruled out by it, and costs one look */
    int nr = (int)CELL_R[0] + dr, nc = (int)CELL_C[0] + dc;
    if (nr < 0 || nc < 0 || nr >= (int)after->h || nc >= (int)after->w) return 0;
    if (after->c[nr][nc] != t->colour) return 0;
  }
  for (i = 1u; i < N_CELLS; i++) {
    int nr = (int)CELL_R[i] + dr, nc = (int)CELL_C[i] + dc;
    if (nr < 0 || nc < 0 || nr >= (int)after->h || nc >= (int)after->w) return 0;
    if (after->c[nr][nc] != t->colour) return 0;
  }
  return 1;
}

static int moved_by(const ru_thing_t *t, const pl_frame_t *before, const pl_frame_t *after, int dr, int dc) {
  gather(t, before);
  return moved_cells(t, after, dr, dc);
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
  gather(t, before);
  for (dr = -RU_REACH; dr <= RU_REACH; dr++) {
    for (dc = -RU_REACH; dc <= RU_REACH; dc++) {
      if (dr == 0 && dc == 0) {
        if (n == t->cells && moved_cells(t, after, 0, 0)) put(ok, R_NOTHING);
      } else if (moved_cells(t, after, dr, dc)) {
        put(ok, R_MOVE(dr, dc));
      }
    }
  }
  {
    /* each move, "unless something is in the way": true if it moved and nothing was in the
       way, or if it stayed and something was */
    int stayed = (n == t->cells && moved_cells(t, after, 0, 0));
    for (dr = -RU_REACH; dr <= RU_REACH; dr++) {
      for (dc = -RU_REACH; dc <= RU_REACH; dc++) {
        int in_way;
        if (dr == 0 && dc == 0) continue;
        if (!stayed && !moved_cells(t, after, dr, dc)) continue;   /* neither: nothing to say */
        in_way = blocked_cells(t, before, dr, dc);
        if ((in_way && stayed) || (!in_way && moved_cells(t, after, dr, dc))) put(ok, R_BMOVE(dr, dc));
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

/* what was learned from one act, without keeping the picture: used live and in replay */
static unsigned ru_learn(ru_world_t *w, unsigned act, const pl_frame_t *before, const pl_frame_t *after) {
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
    unsigned rule, was, way;
    uint64_t could[RU_WORDS];
    happened(&things[i], before, after, could);
    /* what it could have said of this thing before the act, and whether it was right */
    if (settled_rule(w, &things[i], act, &rule)) {
      w->said++;
      if (has(could, rule)) w->said_right++;
      else w->said_wrong++;
    } else {
      w->cannot_say++;
    }
    /*
     * Nothing left to say is not the same as too much left to say. Many rules still
     * standing means wait and watch. None standing means no rule in this language is
     * true of what just happened, and no further watching can help: the language is
     * the thing at fault. They are told apart here so the second can be acted on.
     */
    {
      unsigned way2, dead = 0u, met = 0u;
      for (way2 = 0u; way2 < RU_WAYS; way2++) {
        unsigned k2 = ru_kind_way(&things[i], way2);
        if (w->seen[way2][k2][act] == 0u) continue;
        met++;
        if (count(w->left[way2][k2][act]) == 0u) dead++;
      }
      if (met > 0u && dead == met) w->mute++;   /* met again, and still with nothing to say */
      else if (met > 0u) w->unsettled++;
    }
    for (way = 0u; way < RU_WAYS; way++) {
      unsigned k = ru_kind_way(&things[i], way);
      uint64_t *left = w->left[way][k][act];
      was = count(left);
      for (j = 0u; j < RU_WORDS; j++) left[j] &= could[j];   /* what says otherwise is ruled out */
      w->seen[way][k][act]++;
      {
        unsigned now = count(left);
        /* the last rule has just gone: from here it is the language that is at fault */
        if (was > 0u && now == 0u) w->no_words++;
        cut += was - now;
      }
    }
  }
  w->ruled_out += cut;
  return cut;
}

unsigned ru_saw(ru_world_t *w, unsigned act, const pl_frame_t *before, const pl_frame_t *after) {
  /* keep the picture, then learn from it */
  if (act < RU_ACTS) {
    int fresh = (w->logged == 0u) ||
                (w->log[w->logged - 1u].h != before->h) ||
                (w->log[w->logged - 1u].w != before->w) ||
                (memcmp(w->log[w->logged - 1u].c, before->c, sizeof before->c) != 0);
    if (fresh) {
      if (w->logged > 0u) w->log_act[w->logged - 1u] = (unsigned char)RU_NO_ACT;
      if (w->logged + 1u < RU_LOG) w->log[w->logged++] = *before;
      else w->lost++;
    }
    if (w->logged > 0u && w->logged + 1u <= RU_LOG) {
      w->log_act[w->logged - 1u] = (unsigned char)act;
      if (w->logged < RU_LOG) w->log[w->logged++] = *after;
      else w->lost++;
    }
  }
  return ru_learn(w, act, before, after);
}

/* which bit of a word is the lowest set one, without leaning on the compiler */
static unsigned ctz64(uint64_t x) {
  unsigned n = 0u;
  while ((x & 1u) == 0u) {
    x >>= 1;
    n++;
  }
  return n;
}
/* ---- which act asks the most ------------------------------------------------------ */

/*
 * Doing something is asking the world a question, and the answers are not equally
 * worth having. Before an act, a kind of thing has some number of rules still
 * standing. Two rules that would look exactly the same here cannot be told apart by
 * doing it, however the world answers; rules that would look different are separated
 * the moment the answer comes.
 *
 * So group the standing rules by what they would look like if done here, and take the
 * largest group. Whatever the world answers, at least everything outside that group
 * goes. That is the guaranteed harvest, log2(standing) - log2(largest group) bits, and
 * it is a worst case, not an average: no likelihoods are used, and none are needed.
 * The act with the most guaranteed bits is the sharpest question available.
 */
#define RU_SIG_SLOTS 2048u
#define RU_SIG_NONE 0xFFFFFFFFu

static unsigned SIG_KEY[RU_SIG_SLOTS], SIG_N[RU_SIG_SLOTS], SIG_STAMP[RU_SIG_SLOTS], SIG_NOW;

static unsigned biggest_group(const uint64_t *left, const ru_thing_t *t, const pl_frame_t *f,
                              unsigned *standing) {
  unsigned word, best = 0u, m = 0u;
  SIG_NOW++;
  for (word = 0u; word < RU_WORDS; word++) {
    uint64_t bits = left[word];
    while (bits != 0u) {
      unsigned r = word * 64u + (unsigned)ctz64(bits), key, slot;
      int dr = 0, dc = 0, off = 0;
      bits &= bits - 1u;
      m++;
      if (r >= RU_MOVES + 3u) {   /* moves unless something is in the way: which it is, is known now */
        unsigned q = r - RU_MOVES - 3u;
        dr = (int)(q / RU_SIDE) - RU_REACH;
        dc = (int)(q % RU_SIDE) - RU_REACH;
        if (blocked(t, f, dr, dc)) dr = dc = 0;
      } else if (r < RU_MOVES) {
        dr = (int)(r / RU_SIDE) - RU_REACH;
        dc = (int)(r % RU_SIDE) - RU_REACH;
      } else {
        off = (int)(r - RU_MOVES) + 1;   /* gone, bigger or smaller, another colour: each its own look */
      }
      if (off != 0) {
        key = 0xF0000000u + (unsigned)off;
      } else if ((int)t->top + dr < 0 || (int)t->left + dc < 0 ||
                 (int)t->bottom + dr >= (int)f->h || (int)t->right + dc >= (int)f->w) {
        key = 0xE0000000u + (unsigned)(r + 1u);   /* off the board: it cannot look like anything */
      } else {
        key = (unsigned)((int)t->top + dr) * 256u + (unsigned)((int)t->left + dc);
      }
      slot = (key * 2654435761u) % RU_SIG_SLOTS;
      while (SIG_STAMP[slot] == SIG_NOW && SIG_KEY[slot] != key) slot = (slot + 1u) % RU_SIG_SLOTS;
      if (SIG_STAMP[slot] != SIG_NOW) {
        SIG_STAMP[slot] = SIG_NOW;
        SIG_KEY[slot] = key;
        SIG_N[slot] = 0u;
      }
      SIG_N[slot]++;
      if (SIG_N[slot] > best) best = SIG_N[slot];
    }
  }
  *standing = m;
  return best;
}

double ru_worst_bits(const ru_world_t *w, unsigned act, const pl_frame_t *now) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n, i, way;
  double bits = 0.0;
  if (act >= RU_ACTS) return 0.0;
  n = ru_things(now, things, RU_MAX_THINGS);
  PASSABLE = w->passable;
  for (i = 0u; i < n; i++) {
    for (way = 0u; way < RU_WAYS; way++) {
      unsigned k = ru_kind_way(&things[i], way), standing = 0u, big;
      /*
       * A kind this act has never been tried on still has its whole language standing,
       * which is the most there is to win, so it is counted like any other. Passing
       * over those would have it favour the acts it has already asked, which is the
       * opposite of asking.
       */
      big = biggest_group(w->left[way][k][act], &things[i], now, &standing);
      if (standing > 1u && big > 0u) bits += log2((double)standing) - log2((double)big);
    }
  }
  return bits;
}

unsigned ru_no_words(const ru_world_t *w) {
  return w->no_words;
}

void ru_replay(ru_world_t *w) {
  unsigned i, way, k, a;
  for (way = 0u; way < RU_WAYS; way++) {
    for (k = 0u; k < RU_KINDS; k++) {
      for (a = 0u; a < RU_ACTS; a++) {
        set_all(w->left[way][k][a]);
        w->seen[way][k][a] = 0u;
      }
    }
  }
  w->passable = 0u;
  w->said = w->said_right = w->said_wrong = w->cannot_say = 0u;
  w->unsettled = w->no_words = 0u;
  for (i = 0u; i + 1u < w->logged; i++) {
    if (w->log_act[i] == (unsigned char)RU_NO_ACT) continue;
    (void)ru_learn(w, w->log_act[i], &w->log[i], &w->log[i + 1u]);
  }
}

/* ---- saying and imagining ------------------------------------------------------------ */

int ru_say(ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out) {
  static ru_thing_t things[RU_MAX_THINGS];
  unsigned n = ru_things(now, things, RU_MAX_THINGS), i;
  if (act >= RU_ACTS) return 0;
  for (i = 0u; i < n; i++) {
    unsigned rule;
    if (!settled_rule(w, &things[i], act, &rule) || (rule >= RU_MOVES && rule < RU_MOVES + 3u)) return 0;
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
    if (settled_rule(w, &things[i], act, &rule)) {
      if (rule == R_NOTHING) continue;
      return 0;
    }
    for (a2 = 0u; a2 < RU_ACTS; a2++) {
      if (w->seen[0][k][a2] == 0u) continue;
      met++;
      if (!has(w->left[0][k][a2], R_NOTHING) || count(w->left[0][k][a2]) > 1u) stirs++;
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
    if (!settled_rule(w, &things[i], act, &rule)) {
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
      unsigned n = count(w->left[0][k][a]);
      if (w->seen[0][k][a] == 0u || n == 0u) continue;
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
      if (w->seen[0][k][a] == 0u) continue;
      met++;
      if (count(w->left[0][k][a]) == 1u) settled++;
    }
  }
  if (w->no_words > 0u) {
    fprintf(out, "  %u times its last rule went, leaving nothing that could account for what\n"
                 "  happened (and %u acts met afterwards it still had nothing to say of): no\n"
                 "  amount of further watching settles those, only more words\n",
            w->no_words, w->mute);
  }
  fprintf(out, "  pictures kept to put a wider language to: %u%s\n", w->logged,
          w->lost > 0u ? " (and some let go)" : "");
  fprintf(out, "  what each act does to each kind of thing: %u of %u settled to one rule (%.0f bits left); "
               "of things it could say about before an act, right %u of %u (%.0f%%); could not say of %u\n",
          settled, met, ru_bits(w), w->said_right, w->said,
          w->said ? 100.0 * (double)w->said_right / (double)w->said : 0.0, w->cannot_say);
  if (w->spared > 0u) {
    fprintf(out, "  acts it did not spend because its rules said they change nothing: %u\n", w->spared);
  }
  return SM_OK;
}
