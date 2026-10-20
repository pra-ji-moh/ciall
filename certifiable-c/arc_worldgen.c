/*
 * arc_worldgen.c -- worlds in the shape of ARC-AGI-3, of several kinds, made
 * on demand. See arc_worldgen.h. One world at a time.
 */

#include "arc_worldgen.h"

#include <string.h>

#define WG_ROWS 21
#define WG_COLS 21
#define WG_LEVELS 3
#define WG_OFF 4

typedef struct {
  wg_facts_t f;
  char orig[WG_LEVELS][WG_ROWS][WG_COLS];   /* each level as it was made */
  char grid[WG_LEVELS][WG_ROWS][WG_COLS];   /* W wall . floor P start T ending D decoy
                                               O door K key X target A common tile
                                               B the odd tile 1..4 pads, in order */
  int box_r[WG_LEVELS], box_c[WG_LEVELS];   /* where each level's box (or twin) starts */
  unsigned char common[WG_LEVELS], odd[WG_LEVELS];        /* odd: the two tile colours */
  unsigned char pad_col[WG_LEVELS][WG_MAX_PADS];         /* order: pad colours, in order */
  unsigned n_pads[WG_LEVELS];
  unsigned pool[16], n_pool;                /* colours nothing else in the world uses */
  unsigned marker;                          /* reverse: the mark where the legend starts */
  int tw_sr, tw_sc;                         /* mirror: how the twin's step is transformed */
  unsigned level, next;
  int pr, pc, br, bc;
  int opened;
  int dr[PL_MAX_ACTIONS + 1u], dc[PL_MAX_ACTIONS + 1u];
} wg_state_t;

static wg_state_t WG;

static uint32_t next_rand(uint32_t *s) {
  uint32_t x = *s ? *s : 2463534242u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  *s = x;
  return x;
}

static unsigned below(uint32_t *s, unsigned n) {
  return n == 0u ? 0u : next_rand(s) % n;
}

static int is_order(unsigned kind) { return kind == WG_ORDER || kind == WG_REVERSE; }
static int has_twin(unsigned kind) { return kind == WG_MIRROR || kind == WG_ODDTWIN; }
static int has_pair(unsigned kind) { return kind == WG_PUSH || has_twin(kind); }
/* what the second thing (box or twin) must cover to end the level */
static char pair_goal(unsigned kind) { return kind == WG_ODDTWIN ? 'B' : 'X'; }

static char cell(const wg_state_t *w, unsigned lv, int r, int c) {
  if (r < 0 || c < 0 || r >= (int)w->f.rows || c >= (int)w->f.cols) return 'W';
  return w->grid[lv][r][c];
}

const char *wg_kind_name(unsigned kind) {
  switch (kind) {
    case WG_MAZE: return "mazes";
    case WG_KEYS: return "keys and doors";
    case WG_PUSH: return "pushing";
    case WG_ODD: return "odd one out";
    case WG_MIRROR: return "mirror twin";
    case WG_ORDER: return "in order";
    case WG_REVERSE: return "reverse order";
    case WG_ODDTWIN: return "odd twin";
    default: return "?";
  }
}

/* ---- distances ------------------------------------------------------------ */

static int DIST[WG_ROWS][WG_COLS];
static int PREVR[WG_ROWS][WG_COLS], PREVC[WG_ROWS][WG_COLS];

/* Over squares that are not walls (and not doors, when doors_shut; and not
   pads later in the order than allow_pad, when it is a pad). */
static void spread(const wg_state_t *w, unsigned lv, int r0, int c0, int doors_shut, char allow_pad,
                   int *far_r, int *far_c) {
  static int qr[WG_ROWS * WG_COLS], qc[WG_ROWS * WG_COLS];
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  int head = 0, tail = 0, r, c, k;
  for (r = 0; r < WG_ROWS; r++) {
    for (c = 0; c < WG_COLS; c++) DIST[r][c] = -1;
  }
  DIST[r0][c0] = 0;
  PREVR[r0][c0] = r0;
  PREVC[r0][c0] = c0;
  qr[tail] = r0;
  qc[tail] = c0;
  tail++;
  *far_r = r0;
  *far_c = c0;
  while (head < tail) {
    r = qr[head];
    c = qc[head];
    head++;
    if (DIST[r][c] > DIST[*far_r][*far_c]) {
      *far_r = r;
      *far_c = c;
    }
    for (k = 0; k < 4; k++) {
      int nr = r + dr[k], nc = c + dc[k];
      char ch = cell(w, lv, nr, nc);
      if (ch == 'W' || (doors_shut && ch == 'O') || DIST[nr][nc] >= 0) continue;
      if (allow_pad && ch >= '1' && ch <= '9' && ch > allow_pad) continue;
      DIST[nr][nc] = DIST[r][c] + 1;
      PREVR[nr][nc] = r;
      PREVC[nr][nc] = c;
      qr[tail] = nr;
      qc[tail] = nc;
      tail++;
    }
  }
}

/* The fewest moves to get the second thing (the box, or the twin) onto the
   target, with the real rules. */
static unsigned pair_shortest(const wg_state_t *w, unsigned lv, int pr, int pc, int br, int bc) {
  static unsigned char seen[1u << 20];
  static unsigned q[1u << 18];
  static unsigned depth[1u << 18];
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  unsigned head = 0u, tail = 0u, answer = 0u;
  memset(seen, 0, sizeof seen);
#define PKEY(a, b, c2, d) ((((unsigned)(a)) << 15) | (((unsigned)(b)) << 10) | \
                           (((unsigned)(c2)) << 5) | ((unsigned)(d)))
  q[tail] = PKEY(pr, pc, br, bc);
  depth[tail] = 0u;
  seen[q[tail]] = 1u;
  tail++;
  while (head < tail) {
    unsigned st = q[head], d = depth[head], k;
    int r = (int)((st >> 15) & 31u), c = (int)((st >> 10) & 31u);
    int xr = (int)((st >> 5) & 31u), xc = (int)(st & 31u);
    head++;
    if (cell(w, lv, xr, xc) == pair_goal(w->f.kind)) {
      answer = d;
      break;
    }
    for (k = 0u; k < 4u; k++) {
      int nr = r + dr[k], nc = c + dc[k], nxr = xr, nxc = xc;
      unsigned key;
      if (has_twin(w->f.kind)) {
        /* each moves unless a wall stops it; the twin by the transformed step */
        if (cell(w, lv, nr, nc) == 'W') {
          nr = r;
          nc = c;
        }
        nxr = xr + w->tw_sr * dr[k];
        nxc = xc + w->tw_sc * dc[k];
        if (cell(w, lv, nxr, nxc) == 'W') {
          nxr = xr;
          nxc = xc;
        }
      } else if (cell(w, lv, nr, nc) == 'W') {
        continue;
      } else if (nr == xr && nc == xc) {
        nxr = xr + dr[k];
        nxc = xc + dc[k];
        if (cell(w, lv, nxr, nxc) == 'W') continue;
      }
      key = PKEY(nr, nc, nxr, nxc);
      if (seen[key] || tail >= (1u << 18)) continue;
      seen[key] = 1u;
      q[tail] = key;
      depth[tail] = d + 1u;
      tail++;
    }
  }
#undef PKEY
  return answer;
}

/* The fewest moves to cover the pads still to go, from pad `next` on, in
   order, never stepping on a pad out of turn. 0 when it cannot be done. */
static unsigned order_shortest(const wg_state_t *w, unsigned lv, int pr, int pc, unsigned next) {
  unsigned i, total = 0u;
  int r = pr, c = pc, far_r, far_c;
  for (i = next; i < w->n_pads[lv]; i++) {
    char want = (char)('1' + i);
    int tr = -1, tc = -1, rr, cc;
    for (rr = 0; rr < (int)w->f.rows; rr++) {
      for (cc = 0; cc < (int)w->f.cols; cc++) {
        if (w->grid[lv][rr][cc] == want) {
          tr = rr;
          tc = cc;
        }
      }
    }
    if (tr < 0) return 0u;
    spread(w, lv, r, c, 0, want, &far_r, &far_c);
    if (DIST[tr][tc] <= 0) return 0u;
    total += (unsigned)DIST[tr][tc];
    r = tr;
    c = tc;
  }
  return total;
}

/* ---- building levels ------------------------------------------------------ */

static void carve(wg_state_t *w, unsigned lv, uint32_t *rng, int loops) {
  static int stack_r[WG_ROWS * WG_COLS], stack_c[WG_ROWS * WG_COLS];
  static const int dr[4] = {-2, 2, 0, 0}, dc[4] = {0, 0, -2, 2};
  int top, r, c;
  unsigned k;
  for (r = 0; r < (int)w->f.rows; r++) {
    for (c = 0; c < (int)w->f.cols; c++) w->grid[lv][r][c] = 'W';
  }
  w->grid[lv][1][1] = '.';
  stack_r[0] = 1;
  stack_c[0] = 1;
  top = 1;
  while (top > 0) {
    int open[4], n_open = 0;
    r = stack_r[top - 1];
    c = stack_c[top - 1];
    for (k = 0u; k < 4u; k++) {
      int nr = r + dr[k], nc = c + dc[k];
      if (nr <= 0 || nc <= 0 || nr >= (int)w->f.rows - 1 || nc >= (int)w->f.cols - 1) continue;
      if (w->grid[lv][nr][nc] != 'W') continue;
      open[n_open++] = (int)k;
    }
    if (n_open == 0) {
      top--;
      continue;
    }
    k = (unsigned)open[below(rng, (unsigned)n_open)];
    w->grid[lv][r + dr[k] / 2][c + dc[k] / 2] = '.';
    w->grid[lv][r + dr[k]][c + dc[k]] = '.';
    stack_r[top] = r + dr[k];
    stack_c[top] = c + dc[k];
    top++;
  }
  if (loops && w->f.difficulty >= 4u) {
    unsigned knock = (w->f.difficulty - 3u) * 2u, tries = 0u;
    while (knock > 0u && tries < 400u) {
      int rr = 1 + (int)below(rng, w->f.rows - 2u), cc = 1 + (int)below(rng, w->f.cols - 2u);
      tries++;
      if (w->grid[lv][rr][cc] != 'W') continue;
      if ((cell(w, lv, rr - 1, cc) == '.' && cell(w, lv, rr + 1, cc) == '.') ||
          (cell(w, lv, rr, cc - 1) == '.' && cell(w, lv, rr, cc + 1) == '.')) {
        w->grid[lv][rr][cc] = '.';
        knock--;
      }
    }
  }
}

static int random_floor(const wg_state_t *w, unsigned lv, uint32_t *rng, int *out_r, int *out_c) {
  unsigned tries;
  for (tries = 0u; tries < 400u; tries++) {
    int r = 1 + (int)below(rng, w->f.rows - 2u), c = 1 + (int)below(rng, w->f.cols - 2u);
    if (w->grid[lv][r][c] == '.') {
      *out_r = r;
      *out_c = c;
      return 1;
    }
  }
  return 0;
}

static void scatter_decoys(wg_state_t *w, unsigned lv, uint32_t *rng, unsigned n) {
  unsigned placed = 0u, tries = 0u;
  while (placed < n && tries < 400u) {
    int r, c;
    tries++;
    if (!random_floor(w, lv, rng, &r, &c)) break;
    w->grid[lv][r][c] = 'D';
    placed++;
  }
}

/* A maze: start somewhere, end at the farthest square, decoys on the way. */
static int build_maze(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int sr, sc, tr, tc, r, c;
  unsigned decoys = 1u + w->f.difficulty / 3u, placed = 0u;
  carve(w, lv, rng, 1);
  if (!random_floor(w, lv, rng, &sr, &sc)) return 0;
  spread(w, lv, sr, sc, 0, 0, &tr, &tc);
  w->grid[lv][sr][sc] = 'P';
  w->grid[lv][tr][tc] = 'T';
  r = PREVR[tr][tc];
  c = PREVC[tr][tc];
  while (!(r == sr && c == sc) && placed < (decoys + 1u) / 2u) {
    int pr = PREVR[r][c], pc = PREVC[r][c];
    if (below(rng, 3u) == 0u && w->grid[lv][r][c] == '.') {
      w->grid[lv][r][c] = 'D';
      placed++;
    }
    r = pr;
    c = pc;
  }
  scatter_decoys(w, lv, rng, decoys - placed);
  return 1;
}

/* Keys: a maze with no loops, a door on the only way to the end, and a key
   that can be reached without passing the door. */
static int build_keys(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int sr, sc, tr, tc, r, c, n_path = 0, kr, kc;
  static int path_r[WG_ROWS * WG_COLS], path_c[WG_ROWS * WG_COLS];
  carve(w, lv, rng, 0);
  if (!random_floor(w, lv, rng, &sr, &sc)) return 0;
  spread(w, lv, sr, sc, 0, 0, &tr, &tc);
  /* the way from the end back to the start */
  r = PREVR[tr][tc];
  c = PREVC[tr][tc];
  while (!(r == sr && c == sc)) {
    path_r[n_path] = r;
    path_c[n_path] = c;
    n_path++;
    {
      int pr = PREVR[r][c], pc = PREVC[r][c];
      r = pr;
      c = pc;
    }
  }
  if (n_path < 4) return 0;
  w->grid[lv][sr][sc] = 'P';
  w->grid[lv][tr][tc] = 'T';
  w->grid[lv][path_r[n_path / 3]][path_c[n_path / 3]] = 'O';
  /* the key: as far as it can be from the start without passing the door */
  spread(w, lv, sr, sc, 1, 0, &kr, &kc);
  if (kr == sr && kc == sc) return 0;
  if (w->grid[lv][kr][kc] != '.') return 0;
  w->grid[lv][kr][kc] = 'K';
  scatter_decoys(w, lv, rng, 1u + w->f.difficulty / 3u);
  return 1;
}

/* An open room walled round, with this many single blocks dropped in it. */
static void room(wg_state_t *w, unsigned lv, uint32_t *rng, unsigned blocks) {
  int r, c;
  unsigned placed = 0u, tries = 0u;
  for (r = 0; r < (int)w->f.rows; r++) {
    for (c = 0; c < (int)w->f.cols; c++) {
      int edge = (r == 0 || c == 0 || r == (int)w->f.rows - 1 || c == (int)w->f.cols - 1);
      w->grid[lv][r][c] = edge ? 'W' : '.';
    }
  }
  while (placed < blocks && tries < 200u) {
    r = 1 + (int)below(rng, w->f.rows - 2u);
    c = 1 + (int)below(rng, w->f.cols - 2u);
    tries++;
    w->grid[lv][r][c] = 'W';
    placed++;
  }
}

/* Push: an open room with a few blocks, a target, a box, and the body
   somewhere else. Kept only when getting the box onto the target takes real
   planning: at least a few moves more at every difficulty. */
static int build_push(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int xr, xc, bxr, bxc, pr, pc;
  unsigned needed, len;
  room(w, lv, rng, w->f.difficulty);
  if (!random_floor(w, lv, rng, &xr, &xc)) return 0;
  w->grid[lv][xr][xc] = 'X';
  /* the box away from the walls, so it can be pushed more than one way */
  bxr = 2 + (int)below(rng, w->f.rows - 4u);
  bxc = 2 + (int)below(rng, w->f.cols - 4u);
  if (w->grid[lv][bxr][bxc] != '.') return 0;
  w->box_r[lv] = bxr;
  w->box_c[lv] = bxc;
  if (!random_floor(w, lv, rng, &pr, &pc)) return 0;
  if (pr == bxr && pc == bxc) return 0;
  w->grid[lv][pr][pc] = 'P';
  needed = 4u + w->f.difficulty;
  len = pair_shortest(w, lv, pr, pc, bxr, bxc);
  if (len < needed) return 0;
  scatter_decoys(w, lv, rng, 1u + w->f.difficulty / 3u);
  /* a decoy must not sit where the box starts */
  if (w->grid[lv][bxr][bxc] != '.') return 0;
  return 1;
}

/* A colour from those nothing else in this world uses, other than `not`. */
static unsigned pick_colour(const wg_state_t *w, uint32_t *rng, int not) {
  unsigned i, tries;
  for (tries = 0u; tries < 64u; tries++) {
    i = w->pool[below(rng, w->n_pool)];
    if ((int)i != not) return i;
  }
  return w->pool[0] == (unsigned)not ? w->pool[1] : w->pool[0];
}

/* Each level, the colour that was odd before becomes the common one. */
static void odd_colours(wg_state_t *w, unsigned lv, uint32_t *rng) {
  if (lv == 0u) {
    w->common[lv] = (unsigned char)pick_colour(w, rng, -1);
  } else {
    w->common[lv] = w->odd[lv - 1u];
  }
  w->odd[lv] = (unsigned char)pick_colour(w, rng, w->common[lv]);
}

/* Odd one out: a maze with loops, tiles of one colour scattered in it, and
   one tile of another colour that ends the level. Each level the colour that
   was odd in the level before becomes the common one, so no colour can be
   carried forward as "what ends it": only the difference can. */
static int build_odd(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int sr, sc, r, c, far_r, far_c;
  unsigned n = 3u + w->f.difficulty / 2u, i;
  carve(w, lv, rng, 1);
  if (!random_floor(w, lv, rng, &sr, &sc)) return 0;
  w->grid[lv][sr][sc] = 'P';
  for (i = 0u; i < n; i++) {
    if (!random_floor(w, lv, rng, &r, &c)) return 0;
    w->grid[lv][r][c] = i == 0u ? 'B' : 'A';
  }
  odd_colours(w, lv, rng);
  spread(w, lv, sr, sc, 0, 0, &far_r, &far_c);
  for (r = 0; r < (int)w->f.rows; r++) {
    for (c = 0; c < (int)w->f.cols; c++) {
      if (w->grid[lv][r][c] == 'B' && DIST[r][c] < 3) return 0;
    }
  }
  return 1;
}

/* Mirror: an open room, the body, a twin that steps the transformed way
   whenever the body steps, and a target only the twin can end the level on.
   Kept only when it takes some planning. */
static int build_mirror(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int pr, pc, tr, tc, xr, xc;
  unsigned len;
  room(w, lv, rng, w->f.difficulty);
  if (!random_floor(w, lv, rng, &pr, &pc)) return 0;
  tr = w->tw_sr < 0 ? (int)w->f.rows - 1 - pr : pr;
  tc = (int)w->f.cols - 1 - pc;
  if (w->grid[lv][tr][tc] != '.' || (tr == pr && tc == pc)) return 0;
  w->grid[lv][pr][pc] = 'P';
  w->box_r[lv] = tr;
  w->box_c[lv] = tc;
  if (!random_floor(w, lv, rng, &xr, &xc)) return 0;
  if (xr == tr && xc == tc) return 0;
  w->grid[lv][xr][xc] = 'X';
  len = pair_shortest(w, lv, pr, pc, tr, tc);
  return len >= 3u + w->f.difficulty / 2u;
}

/* Odd twin: the mirror's room, body and twin, and odd-one-out tiles; the
   twin covering the tile that differs ends the level. */
static int build_oddtwin(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int pr, pc, tr, tc, r, c;
  unsigned n = 3u + w->f.difficulty / 2u, i, len;
  room(w, lv, rng, w->f.difficulty);
  if (!random_floor(w, lv, rng, &pr, &pc)) return 0;
  tr = w->tw_sr < 0 ? (int)w->f.rows - 1 - pr : pr;
  tc = (int)w->f.cols - 1 - pc;
  if (w->grid[lv][tr][tc] != '.' || (tr == pr && tc == pc)) return 0;
  w->grid[lv][pr][pc] = 'P';
  w->grid[lv][tr][tc] = 'Q';   /* held while the tiles are placed */
  for (i = 0u; i < n; i++) {
    if (!random_floor(w, lv, rng, &r, &c)) return 0;
    w->grid[lv][r][c] = i == 0u ? 'B' : 'A';
  }
  w->grid[lv][tr][tc] = '.';
  w->box_r[lv] = tr;
  w->box_c[lv] = tc;
  odd_colours(w, lv, rng);
  len = pair_shortest(w, lv, pr, pc, tr, tc);
  return len >= 3u + w->f.difficulty / 2u;
}

/* In order: an open room with pads of distinct colours; the legend along the
   top of the picture shows the order they must be covered in. */
static int build_order(wg_state_t *w, unsigned lv, uint32_t *rng) {
  int pr, pc, r, c;
  unsigned n = 2u + w->f.difficulty / 3u, i, j;
  if (n > WG_MAX_PADS) n = WG_MAX_PADS;
  room(w, lv, rng, w->f.difficulty / 2u);
  if (!random_floor(w, lv, rng, &pr, &pc)) return 0;
  w->grid[lv][pr][pc] = 'P';
  w->n_pads[lv] = n;
  for (i = 0u; i < n; i++) {
    unsigned col;
    int fresh;
    do {
      col = pick_colour(w, rng, -1);
      fresh = 1;
      for (j = 0u; j < i; j++) {
        if (w->pad_col[lv][j] == col) fresh = 0;
      }
    } while (!fresh);
    w->pad_col[lv][i] = (unsigned char)col;
    if (!random_floor(w, lv, rng, &r, &c)) return 0;
    w->grid[lv][r][c] = (char)('1' + i);
  }
  return order_shortest(w, lv, pr, pc, 0u) >= n + 2u;
}

/* ---- playing -------------------------------------------------------------- */

static void paint(pl_frame_t *f, const wg_state_t *w, int r, int c, unsigned colour) {
  unsigned i, j;
  for (i = 0u; i < w->f.scale; i++) {
    for (j = 0u; j < w->f.scale; j++) {
      f->c[WG_OFF + r * (int)w->f.scale + (int)i][WG_OFF + c * (int)w->f.scale + (int)j] =
          (unsigned char)colour;
    }
  }
}

static void render(const wg_state_t *w, pl_frame_t *f) {
  int r, c;
  unsigned i, j;
  f->w = PL_SIZE;
  f->h = PL_SIZE;
  memset(f->c, (int)w->f.floor, sizeof f->c);
  for (r = 0; r < (int)w->f.rows; r++) {
    for (c = 0; c < (int)w->f.cols; c++) {
      char ch = w->grid[w->level][r][c];
      if (ch == 'W') paint(f, w, r, c, w->f.wall);
      if (ch == 'T' || ch == 'X') paint(f, w, r, c, w->f.ending);
      if (ch == 'D') paint(f, w, r, c, w->f.decoy);
      if (ch == 'O') paint(f, w, r, c, w->f.door);
      if (ch == 'K') paint(f, w, r, c, w->f.key);
      if (ch == 'A') paint(f, w, r, c, w->common[w->level]);
      if (ch == 'B') paint(f, w, r, c, w->odd[w->level]);
      if (ch >= '1' && ch <= '9') paint(f, w, r, c, w->pad_col[w->level][ch - '1']);
    }
  }
  if (has_pair(w->f.kind)) paint(f, w, w->br, w->bc, w->f.box);
  if (is_order(w->f.kind)) {
    /* the legend: the pads' colours in the order they are to be covered,
       in the strip above the room; reversed, with a mark at the right-hand
       end where the reading starts */
    unsigned n = w->n_pads[w->level];
    for (i = 0u; i < n; i++) {
      unsigned col = w->f.kind == WG_REVERSE ? w->pad_col[w->level][n - 1u - i] : w->pad_col[w->level][i];
      for (j = 0u; j < 4u; j++) f->c[1u + j / 2u][WG_OFF + 3u * i + j % 2u] = (unsigned char)col;
    }
    if (w->f.kind == WG_REVERSE) f->c[1][WG_OFF + 3u * n] = (unsigned char)w->marker;
  }
  for (i = 0u; i < w->f.scale; i++) {
    for (j = 0u; j < w->f.scale; j++) {
      f->c[WG_OFF + w->pr * (int)w->f.scale + (int)i][WG_OFF + w->pc * (int)w->f.scale + (int)j] =
          (unsigned char)(i == 0u ? w->f.body_a : w->f.body_b);
    }
  }
}

static void start_level(wg_state_t *w) {
  int r, c;
  for (r = 0; r < (int)w->f.rows; r++) {
    for (c = 0; c < (int)w->f.cols; c++) {
      if (w->grid[w->level][r][c] == 'P') {
        w->pr = r;
        w->pc = c;
      }
    }
  }
  w->br = w->box_r[w->level];
  w->bc = w->box_c[w->level];
  w->opened = 0;
  w->next = 0u;
  /* every pad back where it was, whatever was covered before */
  if (is_order(w->f.kind)) memcpy(w->grid[w->level], w->orig[w->level], sizeof w->grid[w->level]);
}

static void wg_reset(pl_game_t *g, pl_frame_t *out) {
  wg_state_t *w = (wg_state_t *)g->state;
  w->level = 0u;
  start_level(w);
  render(w, out);
}

static void wg_restart(pl_game_t *g, pl_frame_t *out) {
  wg_state_t *w = (wg_state_t *)g->state;
  memcpy(w->grid[w->level], w->orig[w->level], sizeof w->grid[w->level]);
  start_level(w);
  render(w, out);
}

static int blocks_body(const wg_state_t *w, int r, int c) {
  char ch = cell(w, w->level, r, c);
  return ch == 'W' || (ch == 'O' && !w->opened);
}

static int wg_act(pl_game_t *g, unsigned action, pl_frame_t *out) {
  wg_state_t *w = (wg_state_t *)g->state;
  int nr, nc, ended = 0;
  if (action == 0u || action > g->n_actions) {
    render(w, out);
    return 0;
  }
  nr = w->pr + w->dr[action];
  nc = w->pc + w->dc[action];
  if (w->f.kind == WG_PUSH && nr == w->br && nc == w->bc) {
    int xr = w->br + w->dr[action], xc = w->bc + w->dc[action];
    if (!blocks_body(w, nr, nc) && !blocks_body(w, xr, xc)) {
      w->br = xr;
      w->bc = xc;
      w->pr = nr;
      w->pc = nc;
    }
  } else if (!blocks_body(w, nr, nc)) {
    w->pr = nr;
    w->pc = nc;
  }
  if (has_twin(w->f.kind)) {
    int tr = w->br + w->tw_sr * w->dr[action], tc = w->bc + w->tw_sc * w->dc[action];
    if (cell(w, w->level, tr, tc) != 'W') {
      w->br = tr;
      w->bc = tc;
    }
  }
  if (is_order(w->f.kind)) {
    char ch = cell(w, w->level, w->pr, w->pc);
    if (ch >= '1' && ch <= '9') {
      if ((unsigned)(ch - '1') == w->next) {
        w->grid[w->level][w->pr][w->pc] = '.';
        w->next++;
      } else {
        /* out of turn: every pad comes back */
        memcpy(w->grid[w->level], w->orig[w->level], sizeof w->grid[w->level]);
        w->next = 0u;
      }
    }
  }

  if (w->f.kind == WG_KEYS && cell(w, w->level, w->pr, w->pc) == 'K') {
    int r, c;
    w->opened = 1;
    for (r = 0; r < (int)w->f.rows; r++) {
      for (c = 0; c < (int)w->f.cols; c++) {
        char ch = w->grid[w->level][r][c];
        if (ch == 'K' || ch == 'O') w->grid[w->level][r][c] = '.';
      }
    }
  }
  if (has_pair(w->f.kind)) ended = cell(w, w->level, w->br, w->bc) == pair_goal(w->f.kind);
  else if (w->f.kind == WG_ODD) ended = cell(w, w->level, w->pr, w->pc) == 'B';
  else if (is_order(w->f.kind)) ended = w->next >= w->n_pads[w->level];
  else ended = cell(w, w->level, w->pr, w->pc) == 'T';

  if (ended) {
    w->level++;
    if (w->level >= g->levels) {
      w->level = g->levels - 1u;
      render(w, out);
      return 2;
    }
    start_level(w);
    render(w, out);
    return 1;
  }
  render(w, out);
  return 0;
}

static unsigned wg_shortest(pl_game_t *g) {
  wg_state_t *w = (wg_state_t *)g->state;
  int r, c, far_r, far_c, kr = -1, kc = -1, tr = -1, tc = -1;
  if (has_pair(w->f.kind)) return pair_shortest(w, w->level, w->pr, w->pc, w->br, w->bc);
  if (is_order(w->f.kind)) return order_shortest(w, w->level, w->pr, w->pc, w->next);
  for (r = 0; r < (int)w->f.rows; r++) {
    for (c = 0; c < (int)w->f.cols; c++) {
      if (w->grid[w->level][r][c] == 'T' || w->grid[w->level][r][c] == 'B') { tr = r; tc = c; }
      if (w->grid[w->level][r][c] == 'K') { kr = r; kc = c; }
    }
  }
  if (tr < 0) return 0u;
  if (w->f.kind == WG_KEYS && kr >= 0 && !w->opened) {
    unsigned to_key;
    spread(w, w->level, w->pr, w->pc, 1, 0, &far_r, &far_c);
    if (DIST[kr][kc] < 0) return 0u;
    to_key = (unsigned)DIST[kr][kc];
    spread(w, w->level, kr, kc, 0, 0, &far_r, &far_c);
    return DIST[tr][tc] < 0 ? 0u : to_key + (unsigned)DIST[tr][tc];
  }
  spread(w, w->level, w->pr, w->pc, 0, 0, &far_r, &far_c);
  return DIST[tr][tc] < 0 ? 0u : (unsigned)DIST[tr][tc];
}

void wg_make_kind(pl_game_t *g, unsigned kind, unsigned difficulty, unsigned seed) {
  wg_state_t *w = &WG;
  uint32_t rng = seed * 2654435761u + difficulty * 40503u + kind * 9176u + 1u;
  unsigned palette[16], i, lv, n_dirs = 4u, slots[PL_MAX_ACTIONS];
  static const int dir_r[4] = {-1, 1, 0, 0}, dir_c[4] = {0, 0, -1, 1};

  if (g == 0) return;
  if (difficulty > WG_MAX_DIFFICULTY) difficulty = WG_MAX_DIFFICULTY;
  if (kind >= WG_KINDS) kind = WG_MAZE;
  memset(w, 0, sizeof(*w));
  w->f.kind = kind;
  w->f.difficulty = difficulty;
  w->f.seed = seed;
  w->f.rows = 2u * (3u + difficulty / 2u) + 1u;
  w->f.cols = 2u * (4u + difficulty / 2u) + 1u;

  if (difficulty < 4u) {
    w->f.scale = 2u;
  } else if (difficulty < 7u) {
    w->f.scale = 1u + below(&rng, 2u);
  } else {
    w->f.scale = 1u + below(&rng, 3u);
  }

  for (i = 0u; i < 16u; i++) palette[i] = i;
  if (difficulty == 0u) {
    w->f.floor = 0u; w->f.wall = 5u; w->f.ending = 4u; w->f.decoy = 8u;
    w->f.body_a = 3u; w->f.body_b = 6u; w->f.door = 9u; w->f.key = 10u; w->f.box = 12u;
  } else {
    unsigned *out[8], n = 0u, k;
    for (i = 15u; i > 0u; i--) {
      unsigned j = below(&rng, i + 1u), t = palette[i];
      palette[i] = palette[j];
      palette[j] = t;
    }
    w->f.floor = difficulty >= 5u ? palette[0] : 0u;
    out[0] = &w->f.wall; out[1] = &w->f.ending; out[2] = &w->f.decoy; out[3] = &w->f.body_a;
    out[4] = &w->f.body_b; out[5] = &w->f.door; out[6] = &w->f.key; out[7] = &w->f.box;
    for (k = 0u; k < 16u && n < 8u; k++) {
      if (palette[k] == w->f.floor) continue;
      *out[n++] = palette[k];
    }
  }
  if (w->f.scale == 1u) w->f.body_b = w->f.body_a;
  for (i = 0u; i < 16u; i++) {
    if (i == w->f.floor || i == w->f.wall || i == w->f.ending || i == w->f.decoy ||
        i == w->f.body_a || i == w->f.body_b || i == w->f.box) continue;
    w->pool[w->n_pool++] = i;
  }
  w->tw_sc = -1;
  w->tw_sr = difficulty >= 5u ? -1 : 1;

  w->f.n_dead = difficulty >= 2u ? 1u : 0u;
  w->f.n_actions = n_dirs + w->f.n_dead;
  for (i = 0u; i < w->f.n_actions; i++) slots[i] = i + 1u;
  if (difficulty >= 1u) {
    for (i = w->f.n_actions - 1u; i > 0u; i--) {
      unsigned j = below(&rng, i + 1u), t = slots[i];
      slots[i] = slots[j];
      slots[j] = t;
    }
  }
  for (i = 0u; i < n_dirs; i++) {
    w->dr[slots[i]] = dir_r[i];
    w->dc[slots[i]] = dir_c[i];
  }

  for (lv = 0u; lv < WG_LEVELS; lv++) {
    unsigned attempt;
    int ok = 0;
    for (attempt = 0u; attempt < 400u && !ok; attempt++) {
      if (kind == WG_KEYS) ok = build_keys(w, lv, &rng);
      else if (kind == WG_PUSH) ok = build_push(w, lv, &rng);
      else if (kind == WG_ODD) ok = build_odd(w, lv, &rng);
      else if (kind == WG_MIRROR) ok = build_mirror(w, lv, &rng);
      else if (is_order(kind)) ok = build_order(w, lv, &rng);
      else if (kind == WG_ODDTWIN) ok = build_oddtwin(w, lv, &rng);
      else ok = build_maze(w, lv, &rng);
    }
    if (!ok) {
      /* could not be made at this size: fall back to a maze, which always can */
      w->f.kind = WG_MAZE;
      kind = WG_MAZE;
      lv = (unsigned)-1;
      continue;
    }
  }

  if (w->f.kind == WG_ODD || w->f.kind == WG_ODDTWIN) w->f.n_parts = 3u + difficulty / 2u;
  if (is_order(w->f.kind)) w->f.n_parts = w->n_pads[0];
  if (w->f.kind == WG_REVERSE) {
    /* a colour no pad has, at any level */
    unsigned tries, l2, j2;
    for (tries = 0u; tries < 64u; tries++) {
      int clash = 0;
      w->marker = pick_colour(w, &rng, -1);
      for (l2 = 0u; l2 < WG_LEVELS; l2++) {
        for (j2 = 0u; j2 < w->n_pads[l2]; j2++) clash |= w->pad_col[l2][j2] == w->marker;
      }
      if (!clash) break;
    }
  }
  memcpy(w->orig, w->grid, sizeof w->orig);
  g->state = w;
  g->n_actions = w->f.n_actions;
  g->levels = WG_LEVELS;
  g->restart = wg_restart;
  g->reset = wg_reset;
  g->act = wg_act;
  g->shortest = wg_shortest;
}

void wg_make(pl_game_t *g, unsigned difficulty, unsigned seed) {
  wg_make_kind(g, WG_MAZE, difficulty, seed);
}

void wg_facts(const pl_game_t *g, wg_facts_t *out) {
  if (g == 0 || out == 0 || g->state == 0) return;
  *out = ((const wg_state_t *)g->state)->f;
}
