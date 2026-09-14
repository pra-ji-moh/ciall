/*
 * smarsh_object.c -- worlds made of things, not of columns.
 *
 * See smarsh_object.h. One world at a time: the workspace is static and
 * reused, the same discipline as smarsh_space.c and smarsh_frame.c.
 *
 * Terms are number-valued and statements are yes/no-valued, and both are
 * enumerated shortest first. A term or statement may speak of one item (x)
 * or two (x and y); a quantifier closes those off and leaves something that
 * is about the scene as a whole, which is what can be compared against what
 * happened.
 */

#include "smarsh_object.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define OB_NODES 120000u
#define OB_TERM_CAP 2400u
#define OB_STMT_CAP 3000u
#define OB_TBL 16384u
#define OB_MAX_CONST 12u
#define OB_MAXN 3u             /* largest scene in the universe it settles things over */
#define OB_UNIV_CAP 65536u     /* and the most scenes that universe may hold */
#define OB_PROBE_SCENES 48u    /* of those, the ones used to tell descriptions apart */
#define OB_UNIV_WORDS (OB_UNIV_CAP / 32u)
#define OB_STEP_CAP 1024u      /* scenes one step from one it saw */
#define OB_ENOUGH 8u           /* scenes needed before "that follows" is a conclusion */
#define OB_MAX_POINTS 640u
#define OB_MAX_SURV 16u
#define OB_COUNT_RESERVE 500u   /* room kept for "how many x with ..." */
#define OB_NONE 0xFFFFFFFFu

/* ---- the language -------------------------------------------------------- */

typedef enum {
  /* terms */
  TO_CONST = 0, TO_ATTR, TO_COUNT, TO_JOINS, TO_DEG,
  TO_ADD, TO_SUB, TO_MUL, TO_MIN, TO_MAX, TO_ABS, TO_MOD,
  TO_TOTAL, TO_LARGEST, TO_HOWMANY,
  /* statements */
  SO_LE, SO_EQ, SO_JOINED, SO_AND, SO_OR, SO_NOT,
  SO_EVERY, SO_SOME, SO_EVERY2, SO_EVERYJOIN
} ob_op_t;

typedef struct {
  ob_op_t op;
  unsigned a, b;      /* children */
  unsigned k;         /* attribute index */
  unsigned which;     /* 0 = x, 1 = y */
  double c;
  unsigned size;
  unsigned mask;      /* bit 0: speaks of x, bit 1: speaks of y */
} ob_node_t;

static ob_node_t NODE[OB_NODES];
static unsigned N_NODES;

static unsigned TERM[OB_TERM_CAP];
static unsigned N_TERM;
static unsigned TERM_END[OB_TERM_SIZE + 2u];
static unsigned STMT[OB_STMT_CAP];
static unsigned N_STMT;
static unsigned STMT_END[OB_STMT_SIZE + 2u];

static uint64_t HA[OB_TERM_CAP + OB_STMT_CAP], HB[OB_TERM_CAP + OB_STMT_CAP];
static unsigned TBL[2u][OB_TBL];

static const ob_world_t *W;
static ob_scene_t ALL_SCENE[OB_MAX_SCENES + OB_PROBE_SCENES];
static unsigned N_ALL;                 /* real scenes first, then invented ones */
static unsigned N_REAL;
static struct { unsigned scene, x, y; } POINT[OB_MAX_POINTS];
static unsigned N_POINT;
static double KONST[OB_MAX_CONST];
static unsigned N_KONST;
static unsigned TERM_ROOM;
static unsigned REACHED;   /* longest description it got to write */
static int EVID_MODE;      /* 0 all, 1 must hold, 2 must match, 3 drop whole-scene ones */
static unsigned TRIED;     /* closed statements weighed against what it saw */
static int BUDGET;

/* ---- evaluation ---------------------------------------------------------- */

static iv_t t_eval(unsigned id, const ob_scene_t *sc, unsigned x, unsigned y);
static int s_eval(unsigned id, const ob_scene_t *sc, unsigned x, unsigned y);

static unsigned item_of(const ob_node_t *n, unsigned x, unsigned y) {
  return n->which == 0u ? x : y;
}

static iv_t t_eval(unsigned id, const ob_scene_t *sc, unsigned x, unsigned y) {
  const ob_node_t *n = &NODE[id];
  unsigned i;
  switch (n->op) {
    case TO_CONST: return iv_point(n->c);
    case TO_ATTR: {
      unsigned it = item_of(n, x, y);
      if (it >= sc->n_items) return iv_entire();
      return iv_point(sc->attr[it][n->k]);
    }
    case TO_COUNT: return iv_point((double)sc->n_items);
    case TO_JOINS: {
      unsigned c = 0u, j;
      for (i = 0u; i < sc->n_items; i++) {
        for (j = 0u; j < sc->n_items; j++) c += sc->join[i][j] ? 1u : 0u;
      }
      return iv_point((double)c);
    }
    case TO_DEG: {
      unsigned it = item_of(n, x, y), c = 0u;
      if (it >= sc->n_items) return iv_entire();
      for (i = 0u; i < sc->n_items; i++) c += sc->join[it][i] ? 1u : 0u;
      return iv_point((double)c);
    }
    case TO_ADD: return iv_add(t_eval(n->a, sc, x, y), t_eval(n->b, sc, x, y));
    case TO_SUB: return iv_sub(t_eval(n->a, sc, x, y), t_eval(n->b, sc, x, y));
    case TO_MUL: return iv_mul(t_eval(n->a, sc, x, y), t_eval(n->b, sc, x, y));
    case TO_MIN: return iv_min(t_eval(n->a, sc, x, y), t_eval(n->b, sc, x, y));
    case TO_MAX: return iv_max(t_eval(n->a, sc, x, y), t_eval(n->b, sc, x, y));
    case TO_ABS: return iv_abs(t_eval(n->a, sc, x, y));
    case TO_MOD: return iv_mod(t_eval(n->a, sc, x, y), iv_point(n->c));
    case TO_TOTAL: {
      iv_t s = iv_point(0.0);
      for (i = 0u; i < sc->n_items; i++) s = iv_add(s, iv_point(sc->attr[i][n->k]));
      return s;
    }
    case TO_LARGEST: {
      iv_t s;
      if (sc->n_items == 0u) return iv_entire();
      s = iv_point(sc->attr[0][n->k]);
      for (i = 1u; i < sc->n_items; i++) s = iv_max(s, iv_point(sc->attr[i][n->k]));
      return s;
    }
    case TO_HOWMANY: {
      unsigned c = 0u;
      for (i = 0u; i < sc->n_items; i++) {
        if (s_eval(n->a, sc, i, y) == 1) c++;
      }
      return iv_point((double)c);
    }
    default: break;
  }
  return iv_entire();
}

/* 1 true, 0 false, -1 could not be told */
static int s_eval(unsigned id, const ob_scene_t *sc, unsigned x, unsigned y) {
  const ob_node_t *n = &NODE[id];
  unsigned i, j;
  switch (n->op) {
    case SO_LE: {
      iv_t a = t_eval(n->a, sc, x, y), b = t_eval(n->b, sc, x, y);
      iv_t r;
      if (!(a.lo > -1e300 && a.hi < 1e300 && b.lo > -1e300 && b.hi < 1e300)) return -1;
      r = iv_le(a, b);
      return (r.lo > 0.5) ? 1 : ((r.hi < 0.5) ? 0 : -1);
    }
    case SO_EQ: {
      iv_t a = t_eval(n->a, sc, x, y), b = t_eval(n->b, sc, x, y);
      iv_t r;
      if (!(a.lo > -1e300 && a.hi < 1e300 && b.lo > -1e300 && b.hi < 1e300)) return -1;
      r = iv_eq(a, b);
      return (r.lo > 0.5) ? 1 : ((r.hi < 0.5) ? 0 : -1);
    }
    case SO_JOINED: {
      unsigned p = (n->which == 0u) ? x : y, q = (n->k == 0u) ? x : y;
      if (p >= sc->n_items || q >= sc->n_items) return -1;
      return sc->join[p][q] ? 1 : 0;
    }
    case SO_AND: {
      int a = s_eval(n->a, sc, x, y), b = s_eval(n->b, sc, x, y);
      if (a == 0 || b == 0) return 0;
      if (a == 1 && b == 1) return 1;
      return -1;
    }
    case SO_OR: {
      int a = s_eval(n->a, sc, x, y), b = s_eval(n->b, sc, x, y);
      if (a == 1 || b == 1) return 1;
      if (a == 0 && b == 0) return 0;
      return -1;
    }
    case SO_NOT: {
      int a = s_eval(n->a, sc, x, y);
      return (a < 0) ? -1 : (a == 0 ? 1 : 0);
    }
    case SO_EVERY: {
      int unknown = 0;
      if (sc->n_items == 0u) return 1;   /* nothing to be false of */
      for (i = 0u; i < sc->n_items; i++) {
        int r = s_eval(n->a, sc, i, y);
        if (r == 0) return 0;
        if (r < 0) unknown = 1;
      }
      return unknown ? -1 : 1;
    }
    case SO_SOME: {
      int unknown = 0;
      for (i = 0u; i < sc->n_items; i++) {
        int r = s_eval(n->a, sc, i, y);
        if (r == 1) return 1;
        if (r < 0) unknown = 1;
      }
      return unknown ? -1 : 0;
    }
    case SO_EVERY2: {
      int unknown = 0, any = 0;
      for (i = 0u; i < sc->n_items; i++) {
        for (j = 0u; j < sc->n_items; j++) {
          int r;
          if (i == j) continue;
          any = 1;
          r = s_eval(n->a, sc, i, j);
          if (r == 0) return 0;
          if (r < 0) unknown = 1;
        }
      }
      if (!any) return 1;              /* no two things: nothing to be false of */
      return unknown ? -1 : 1;
    }
    case SO_EVERYJOIN: {
      int unknown = 0, any = 0;
      for (i = 0u; i < sc->n_items; i++) {
        for (j = 0u; j < sc->n_items; j++) {
          int r;
          if (!sc->join[i][j]) continue;
          any = 1;
          r = s_eval(n->a, sc, i, j);
          if (r == 0) return 0;
          if (r < 0) unknown = 1;
        }
      }
      if (!any) return 1;              /* nothing joined: nothing to be false of */
      return unknown ? -1 : 1;
    }
    default: break;
  }
  return -1;
}

/* ---- building ------------------------------------------------------------ */

static unsigned mk(ob_op_t op, unsigned a, unsigned b, unsigned k, unsigned which, double c) {
  unsigned id;
  if (N_NODES >= OB_NODES) return OB_NONE;
  id = N_NODES++;
  NODE[id].op = op;
  NODE[id].a = a;
  NODE[id].b = b;
  NODE[id].k = k;
  NODE[id].which = which;
  NODE[id].c = c;
  switch (op) {
    case TO_CONST: case TO_COUNT: case TO_JOINS: case TO_TOTAL: case TO_LARGEST:
      NODE[id].size = 1u;
      NODE[id].mask = 0u;
      break;
    case TO_ATTR: case TO_DEG:
      NODE[id].size = 1u;
      NODE[id].mask = (which == 0u) ? 1u : 2u;
      break;
    case SO_JOINED:
      NODE[id].size = 1u;
      NODE[id].mask = ((which == 0u) ? 1u : 2u) | ((k == 0u) ? 1u : 2u);
      break;
    case TO_ABS: case SO_NOT:
      if (a == OB_NONE) return OB_NONE;
      NODE[id].size = 1u + NODE[a].size;
      NODE[id].mask = NODE[a].mask;
      break;
    case TO_MOD:
      if (a == OB_NONE) return OB_NONE;
      NODE[id].size = 1u + NODE[a].size;
      NODE[id].mask = NODE[a].mask;
      break;
    case TO_HOWMANY:
      if (a == OB_NONE) return OB_NONE;
      NODE[id].size = 1u + NODE[a].size;
      NODE[id].mask = NODE[a].mask & ~1u;
      break;
    case SO_EVERY: case SO_SOME:
      if (a == OB_NONE) return OB_NONE;
      NODE[id].size = 1u + NODE[a].size;
      NODE[id].mask = NODE[a].mask & ~1u;
      break;
    case SO_EVERY2: case SO_EVERYJOIN:
      if (a == OB_NONE) return OB_NONE;
      NODE[id].size = 1u + NODE[a].size;
      NODE[id].mask = 0u;
      break;
    default:
      if (a == OB_NONE || b == OB_NONE) return OB_NONE;
      NODE[id].size = 1u + NODE[a].size + NODE[b].size;
      NODE[id].mask = NODE[a].mask | NODE[b].mask;
      break;
  }
  return id;
}

static uint64_t bits_of(double v) {
  uint64_t b;
  if (v == 0.0) v = 0.0;
  if (!(v > -1e300 && v < 1e300)) v = -1.2345678901e300;
  memcpy(&b, &v, sizeof b);
  return b;
}

static void hash_node(unsigned id, int statement, uint64_t *ha, uint64_t *hb) {
  uint64_t u = 0xcbf29ce484222325ULL, v = 0x9e3779b97f4a7c15ULL;
  unsigned p;
  for (p = 0u; p < N_POINT; p++) {
    const ob_scene_t *sc = &ALL_SCENE[POINT[p].scene];
    uint64_t b;
    if (statement) {
      b = (uint64_t)(int64_t)s_eval(id, sc, POINT[p].x, POINT[p].y) + 7u;
    } else {
      iv_t r = t_eval(id, sc, POINT[p].x, POINT[p].y);
      b = bits_of((r.lo == r.hi) ? r.lo : (r.lo + r.hi) * 0.5);
    }
    u = (u ^ b) * 0x100000001b3ULL;
    v = (v + b + 0x9e3779b97f4a7c15ULL) * 0xff51afd7ed558ccdULL;
    v ^= v >> 29;
  }
  *ha = u;
  *hb = v;
}

static int stmt_holds_real(unsigned id);
static int stmt_fits(unsigned id);

/*
 * Add to a pool unless something already there behaves identically.
 *
 * A statement with nothing left free is a claim about a whole scene, so
 * it can be weighed against what it saw the moment it is written. The
 * ones that are already contradicted are counted and dropped rather than
 * stored: otherwise the short contradicted ones fill the space and the
 * search never reaches the longer statements at all. Statements that
 * still have a free variable are kept regardless, since they are what
 * the quantified ones are built out of.
 */
static int keep(unsigned id, int statement) {
  uint64_t ha, hb;
  unsigned h, i, base, cap, *pool, *n;
  if (id == OB_NONE) return 0;
  pool = statement ? STMT : TERM;
  n = statement ? &N_STMT : &N_TERM;
  cap = statement ? OB_STMT_CAP : TERM_ROOM;
  base = statement ? OB_TERM_CAP : 0u;
  if (*n >= cap) return 0;
  BUDGET--;
  if (BUDGET < 0) return 0;
  if (statement && EVID_MODE != 0 && NODE[id].mask == 0u) {
    TRIED++;
    if (EVID_MODE == 1 && !stmt_holds_real(id)) return 0;
    if (EVID_MODE == 2 && !stmt_fits(id)) return 0;
    /* a number-valued outcome is answered by a term, so whole-scene
       statements are only worth building out of, never worth keeping */
    if (EVID_MODE == 3) return 0;
  }
  hash_node(id, statement, &ha, &hb);
  h = (unsigned)(ha & (uint64_t)(OB_TBL - 1u));
  for (i = 0u; i < OB_TBL; i++) {
    unsigned slot = TBL[statement ? 1u : 0u][(h + i) & (OB_TBL - 1u)];
    if (slot == 0u) break;
    if (HA[slot - 1u] == ha && HB[slot - 1u] == hb) return 0;
  }
  HA[base + *n] = ha;
  HB[base + *n] = hb;
  TBL[statement ? 1u : 0u][(h + i) & (OB_TBL - 1u)] = base + *n + 1u;
  pool[*n] = id;
  (*n)++;
  return 1;
}

/* ---- the scenes it reasons over, real and invented ----------------------- */

/* ---- the universe it settles things over --------------------------------- */
/*
 * Nothing here is sampled. The scenes below are EVERY scene of at most
 * OB_MAXN things whose numbers come from a small grid across the declared
 * range, with every possible set of joins among them. That is a finite
 * universe, written out in full, and every judgement this module makes
 * about a statement is an elimination over it:
 *
 *   it says nothing        true in every scene of the universe
 *   the others force it    false in no scene where they all hold
 *   it rules out this much exactly how many scenes it eliminates
 *
 * So "this is news" and "this follows from that" are settled by
 * elimination, with a scene as the witness, not by how often something
 * happened to hold. The universe is stated plainly in what it reports: a
 * claim proved over every scene of at most three things is exactly that
 * claim, and not a claim about larger scenes.
 */

static double GRID[OB_MAX_ATTR][4];
static unsigned N_GRID[OB_MAX_ATTR];
static unsigned UNIV_MAXN;
static unsigned UNIV_BLOCK[OB_MAXN + 2u];   /* scenes with exactly n things */
static unsigned UNIV_START[OB_MAXN + 2u];
static unsigned UNIV_GRID;                  /* scenes on the grid */
static unsigned UNIV_N;                     /* those, and the ones near what it saw */
static ob_scene_t STEP[OB_STEP_CAP];
static unsigned N_STEP;

static unsigned ipow(unsigned base, unsigned e) {
  unsigned r = 1u, i;
  for (i = 0u; i < e; i++) {
    if (r > OB_UNIV_CAP) return OB_UNIV_CAP * 4u;
    r *= base;
  }
  return r;
}

/* How many scenes the universe holds at this grid width and this size. */
static unsigned universe_size(unsigned maxn, unsigned width) {
  unsigned n, total = 0u;
  for (n = 0u; n <= maxn; n++) {
    unsigned a = ipow(width, n * W->n_attr);
    unsigned j = ipow(2u, n * n);
    UNIV_BLOCK[n] = (a > OB_UNIV_CAP || j > OB_UNIV_CAP) ? (OB_UNIV_CAP * 4u) : a * j;
    UNIV_START[n] = total;
    if (UNIV_BLOCK[n] > OB_UNIV_CAP) return OB_UNIV_CAP * 4u;
    total += UNIV_BLOCK[n];
    if (total > OB_UNIV_CAP) return OB_UNIV_CAP * 4u;
  }
  return total;
}

/*
 * Every scene one step from one it saw: one number moved by one, one join
 * added or taken away, one thing dropped, one thing repeated. Written out
 * in full, not sampled. Without these the universe holds nothing that
 * satisfies what it believes, and "does this follow from that" could not
 * be asked at all.
 */
static void add_step(const ob_scene_t *sc) {
  if (N_STEP >= OB_STEP_CAP) return;
  STEP[N_STEP] = *sc;
  STEP[N_STEP].has_outcome = 0;
  N_STEP++;
}

static void build_steps(void) {
  ob_scene_t sc;
  unsigned s, i, j, k, d;
  N_STEP = 0u;
  for (s = 0u; s < N_REAL; s++) {
    const ob_scene_t *base = &ALL_SCENE[s];
    add_step(base);
    for (i = 0u; i < base->n_items; i++) {
      for (k = 0u; k < W->n_attr; k++) {
        for (d = 0u; d < 2u; d++) {
          sc = *base;
          sc.attr[i][k] += (d == 0u) ? 1.0 : -1.0;
          if (sc.attr[i][k] < W->lo[k]) sc.attr[i][k] = W->lo[k];
          if (sc.attr[i][k] > W->hi[k]) sc.attr[i][k] = W->hi[k];
          add_step(&sc);
        }
      }
    }
    for (i = 0u; i < base->n_items; i++) {
      for (j = 0u; j < base->n_items; j++) {
        sc = *base;
        sc.join[i][j] = (unsigned char)(sc.join[i][j] ? 0u : 1u);
        add_step(&sc);
      }
    }
    for (i = 0u; i < base->n_items; i++) {
      unsigned a, b;
      sc = *base;
      for (a = i; a + 1u < sc.n_items; a++) {
        for (k = 0u; k < W->n_attr; k++) sc.attr[a][k] = sc.attr[a + 1u][k];
        for (b = 0u; b < sc.n_items; b++) {
          sc.join[a][b] = sc.join[a + 1u][b];
          sc.join[b][a] = sc.join[b][a + 1u];
        }
      }
      if (sc.n_items > 0u) sc.n_items--;
      add_step(&sc);
    }
    if (base->n_items + 1u <= OB_MAX_ITEMS) {
      sc = *base;
      for (k = 0u; k < W->n_attr; k++) sc.attr[sc.n_items][k] = sc.attr[0][k];
      for (j = 0u; j <= sc.n_items; j++) {
        sc.join[sc.n_items][j] = 0u;
        sc.join[j][sc.n_items] = 0u;
      }
      sc.n_items++;
      add_step(&sc);
    }
  }
}

static void build_universe(void) {
  unsigned width = 3u, maxn = OB_MAXN, k, i;
  for (;;) {
    for (k = 0u; k < W->n_attr; k++) {
      double lo = W->lo[k], hi = W->hi[k], mid;
      N_GRID[k] = 0u;
      mid = lo + (hi - lo) * 0.5;
      if (W->whole[k]) mid = floor(mid + 0.5);
      GRID[k][N_GRID[k]++] = lo;
      if (width >= 3u && mid > lo && mid < hi) GRID[k][N_GRID[k]++] = mid;
      if (hi > lo) GRID[k][N_GRID[k]++] = hi;
    }
    UNIV_N = universe_size(maxn, width);
    if (UNIV_N <= OB_UNIV_CAP) break;
    if (width > 2u) { width = 2u; continue; }
    if (maxn > 1u) { maxn--; continue; }
    UNIV_N = universe_size(1u, 2u);
    break;
  }
  UNIV_MAXN = maxn;
  UNIV_GRID = UNIV_N;
  build_steps();
  if (UNIV_GRID + N_STEP > OB_UNIV_CAP) N_STEP = OB_UNIV_CAP - UNIV_GRID;
  UNIV_N = UNIV_GRID + N_STEP;
  for (i = 0u; i < W->n_attr; i++) {
    if (N_GRID[i] == 0u) { GRID[i][0] = W->lo[i]; N_GRID[i] = 1u; }
  }
}

/* The scene with this number, built out in full. */
static void universe_scene(unsigned index, ob_scene_t *sc) {
  unsigned n = 0u, rest, a_code, j_code, i, j, k;
  if (index >= UNIV_GRID) {
    *sc = STEP[index - UNIV_GRID];
    return;
  }
  memset(sc, 0, sizeof(*sc));
  while (n < UNIV_MAXN && index >= UNIV_START[n] + UNIV_BLOCK[n]) n++;
  rest = index - UNIV_START[n];
  j_code = rest % ipow(2u, n * n);
  a_code = rest / ipow(2u, n * n);
  sc->n_items = n;
  for (i = 0u; i < n; i++) {
    for (k = 0u; k < W->n_attr; k++) {
      sc->attr[i][k] = GRID[k][a_code % N_GRID[k]];
      a_code /= N_GRID[k];
    }
  }
  for (i = 0u; i < n; i++) {
    for (j = 0u; j < n; j++) {
      sc->join[i][j] = (unsigned char)(j_code & 1u);
      j_code >>= 1;
    }
  }
}

static void make_scenes(void) {
  unsigned s, i, j, k, pass, stride;
  double seen[OB_MAX_CONST * 3u];
  unsigned n_seen = 0u;

  N_REAL = W->n_scenes;
  for (s = 0u; s < W->n_scenes; s++) ALL_SCENE[s] = W->scene[s];
  build_universe();   /* the grid, and everything a step from what it saw */

  /* a spread of the universe, used only to tell two descriptions apart
     while they are being written out; nothing is decided here */
  stride = (UNIV_N > OB_PROBE_SCENES) ? (UNIV_N / OB_PROBE_SCENES) : 1u;
  for (s = 0u; s < OB_PROBE_SCENES; s++) {
    unsigned idx = (s * stride) % (UNIV_N == 0u ? 1u : UNIV_N);
    universe_scene(idx, &ALL_SCENE[N_REAL + s]);
  }
  N_ALL = N_REAL + OB_PROBE_SCENES;

  /* the points at which two descriptions are compared, spread over every
     scene rather than used up on the first few */
  N_POINT = 0u;
  for (pass = 0u; pass < OB_MAX_ITEMS * OB_MAX_ITEMS && N_POINT < OB_MAX_POINTS; pass++) {
    int added = 0;
    for (s = 0u; s < N_ALL && N_POINT < OB_MAX_POINTS; s++) {
      const ob_scene_t *sc = &ALL_SCENE[s];
      unsigned n = sc->n_items;
      if (n == 0u) {
        if (pass == 0u) {
          POINT[N_POINT].scene = s;
          POINT[N_POINT].x = OB_NONE;
          POINT[N_POINT].y = OB_NONE;
          N_POINT++;
          added = 1;
        }
        continue;
      }
      if (pass >= n * n) continue;
      POINT[N_POINT].scene = s;
      POINT[N_POINT].x = pass / n;
      POINT[N_POINT].y = pass % n;
      N_POINT++;
      added = 1;
    }
    if (!added) break;
  }

  /* numbers it has actually met: what things carry, the totals it has
     formed from them, and how many things and joins there were */
  N_KONST = 0u;
  KONST[N_KONST++] = 0.0;
  KONST[N_KONST++] = 1.0;
  KONST[N_KONST++] = 2.0;
  for (s = 0u; s < N_REAL; s++) {
    const ob_scene_t *sc = &ALL_SCENE[s];
    unsigned joins = 0u;
    for (i = 0u; i < sc->n_items; i++) {
      for (j = 0u; j < sc->n_items; j++) joins += sc->join[i][j] ? 1u : 0u;
    }
    if (n_seen < OB_MAX_CONST * 3u) seen[n_seen++] = (double)sc->n_items;
    if (n_seen < OB_MAX_CONST * 3u) seen[n_seen++] = (double)joins;
    for (k = 0u; k < W->n_attr; k++) {
      double tot = 0.0;
      for (i = 0u; i < sc->n_items; i++) tot += sc->attr[i][k];
      if (n_seen < OB_MAX_CONST * 3u) seen[n_seen++] = tot;
    }
  }
  for (s = 0u; s < N_REAL; s++) {
    const ob_scene_t *sc = &ALL_SCENE[s];
    for (i = 0u; i < sc->n_items; i++) {
      for (k = 0u; k < W->n_attr; k++) {
        if (n_seen < OB_MAX_CONST * 3u) seen[n_seen++] = sc->attr[i][k];
      }
    }
  }
  for (i = 0u; i < n_seen && N_KONST < OB_MAX_CONST; i++) {
    int have = 0;
    for (j = 0u; j < N_KONST; j++) {
      if (KONST[j] == seen[i]) have = 1;
    }
    if (!have) KONST[N_KONST++] = seen[i];
  }
}

/* ---- enumeration --------------------------------------------------------- */

static void build_terms(void) {
  unsigned s, i, k, w;

  N_TERM = 0u;
  TERM_ROOM = OB_TERM_CAP - OB_COUNT_RESERVE;
  for (i = 0u; i < N_KONST; i++) keep(mk(TO_CONST, 0u, 0u, 0u, 0u, KONST[i]), 0);
  keep(mk(TO_COUNT, 0u, 0u, 0u, 0u, 0.0), 0);
  keep(mk(TO_JOINS, 0u, 0u, 0u, 0u, 0.0), 0);
  for (k = 0u; k < W->n_attr; k++) {
    for (w = 0u; w < 2u; w++) keep(mk(TO_ATTR, 0u, 0u, k, w, 0.0), 0);
    keep(mk(TO_TOTAL, 0u, 0u, k, 0u, 0.0), 0);
    keep(mk(TO_LARGEST, 0u, 0u, k, 0u, 0.0), 0);
  }
  for (w = 0u; w < 2u; w++) keep(mk(TO_DEG, 0u, 0u, 0u, w, 0.0), 0);
  TERM_END[1] = N_TERM;

  for (s = 2u; s <= OB_TERM_SIZE; s++) {
    unsigned p, q;
    for (p = TERM_END[s - 2u]; p < TERM_END[s - 1u] && BUDGET > 0; p++) {
      keep(mk(TO_ABS, TERM[p], 0u, 0u, 0u, 0.0), 0);
      keep(mk(TO_MOD, TERM[p], 0u, 0u, 0u, 2.0), 0);
      keep(mk(TO_MOD, TERM[p], 0u, 0u, 0u, 3.0), 0);
    }
    for (i = 1u; i + 1u < s && BUDGET > 0; i++) {
      unsigned oa, ob_, ja, jb;
      if (s - 1u - i < i) continue;
      oa = TERM_END[i - 1u];
      ob_ = TERM_END[i];
      ja = TERM_END[s - 2u - i];
      jb = TERM_END[s - 1u - i];
      for (p = oa; p < ob_ && BUDGET > 0; p++) {
        for (q = ja; q < jb && BUDGET > 0; q++) {
          if (s - 1u - i == i && q < p) continue;
          keep(mk(TO_ADD, TERM[p], TERM[q], 0u, 0u, 0.0), 0);
          keep(mk(TO_MUL, TERM[p], TERM[q], 0u, 0u, 0.0), 0);
          keep(mk(TO_SUB, TERM[p], TERM[q], 0u, 0u, 0.0), 0);
          keep(mk(TO_SUB, TERM[q], TERM[p], 0u, 0u, 0.0), 0);
          keep(mk(TO_MIN, TERM[p], TERM[q], 0u, 0u, 0.0), 0);
          keep(mk(TO_MAX, TERM[p], TERM[q], 0u, 0u, 0.0), 0);
        }
      }
    }
    TERM_END[s] = N_TERM;
    if (N_TERM >= TERM_ROOM || BUDGET <= 0) {
      for (i = s + 1u; i <= OB_TERM_SIZE; i++) TERM_END[i] = N_TERM;
      break;
    }
  }
  TERM_END[0] = 0u;
}

/*
 * Every yes/no description, shortest first, so that the short quantified
 * ones are reached before the long comparisons can use up the search.
 */
static void build_statements(void) {
  unsigned s, i, p, q, t, before;

  N_STMT = 0u;
  keep(mk(SO_JOINED, 0u, 0u, 1u, 0u, 0.0), 1);   /* x joined to y */
  keep(mk(SO_JOINED, 0u, 0u, 0u, 1u, 0.0), 1);   /* y joined to x */
  keep(mk(SO_JOINED, 0u, 0u, 0u, 0u, 0.0), 1);   /* x joined to itself */
  STMT_END[0] = 0u;
  STMT_END[1] = N_STMT;
  REACHED = 1u;

  for (s = 2u; s <= OB_STMT_SIZE; s++) {
    /* a comparison between two number-valued parts */
    for (i = 1u; i + 1u < s && BUDGET > 0; i++) {
      unsigned j = s - 1u - i;
      if (j > OB_TERM_SIZE || i > OB_TERM_SIZE) continue;
      for (p = TERM_END[i - 1u]; p < TERM_END[i] && BUDGET > 0; p++) {
        for (q = TERM_END[j - 1u]; q < TERM_END[j] && BUDGET > 0; q++) {
          keep(mk(SO_LE, TERM[p], TERM[q], 0u, 0u, 0.0), 1);
          keep(mk(SO_EQ, TERM[p], TERM[q], 0u, 0u, 0.0), 1);
        }
      }
    }
    /* not, and the quantifiers, over statements one shorter */
    for (p = STMT_END[s - 2u]; p < STMT_END[s - 1u] && BUDGET > 0; p++) {
      unsigned m = NODE[STMT[p]].mask;
      keep(mk(SO_NOT, STMT[p], 0u, 0u, 0u, 0.0), 1);
      if ((m & 2u) == 0u) {
        /* binding x is only saying something if the body speaks of x */
        if ((m & 1u) != 0u) {
          keep(mk(SO_EVERY, STMT[p], 0u, 0u, 0u, 0.0), 1);
          keep(mk(SO_SOME, STMT[p], 0u, 0u, 0u, 0.0), 1);
        }
      } else {
        keep(mk(SO_EVERY2, STMT[p], 0u, 0u, 0u, 0.0), 1);
        keep(mk(SO_EVERYJOIN, STMT[p], 0u, 0u, 0u, 0.0), 1);
      }
    }
    /* and, or, over two statements whose sizes add to s - 1 */
    for (i = 1u; i + 1u < s && BUDGET > 0; i++) {
      unsigned j = s - 1u - i;
      if (j < i) continue;
      for (p = STMT_END[i - 1u]; p < STMT_END[i] && BUDGET > 0; p++) {
        for (q = STMT_END[j - 1u]; q < STMT_END[j] && BUDGET > 0; q++) {
          if (j == i && q <= p) continue;
          keep(mk(SO_AND, STMT[p], STMT[q], 0u, 0u, 0.0), 1);
          keep(mk(SO_OR, STMT[p], STMT[q], 0u, 0u, 0.0), 1);
        }
      }
    }
    STMT_END[s] = N_STMT;
    REACHED = s;
    if (N_STMT >= OB_STMT_CAP || BUDGET <= 0) {
      for (i = s + 1u; i <= OB_STMT_SIZE; i++) STMT_END[i] = N_STMT;
      break;
    }
  }

  /* "how many x with ..." is a number, so it goes back among the terms and
     is compared like any other. Some of the search is kept back for this:
     counting the things a condition holds of is too useful to lose to a
     long tail of comparisons. */
  if (BUDGET < OB_BUDGET / 4) BUDGET = OB_BUDGET / 4;
  TERM_ROOM = OB_TERM_CAP;
  before = N_TERM;
  for (i = 0u; i < N_STMT && BUDGET > 0; i++) {
    unsigned m = NODE[STMT[i]].mask;
    if ((m & 2u) != 0u || m == 0u) continue;
    if (NODE[STMT[i]].size + 1u > OB_TERM_SIZE) continue;
    keep(mk(TO_HOWMANY, STMT[i], 0u, 0u, 0u, 0.0), 0);
  }
  for (t = before; t < N_TERM && BUDGET > 0; t++) {
    for (q = 0u; q < TERM_END[1]; q++) {
      keep(mk(SO_LE, TERM[t], TERM[q], 0u, 0u, 0.0), 1);
      keep(mk(SO_EQ, TERM[t], TERM[q], 0u, 0u, 0.0), 1);
      keep(mk(SO_LE, TERM[q], TERM[t], 0u, 0u, 0.0), 1);
    }
  }
}

/* ---- saying it ----------------------------------------------------------- */

static void emit(char *buf, unsigned cap, unsigned *pos, const char *s) {
  while (*s && *pos + 1u < cap) buf[(*pos)++] = *s++;
  buf[*pos] = '\0';
}

static void say(unsigned id, char *buf, unsigned cap, unsigned *pos) {
  const ob_node_t *n;
  const char *who;
  char t[48];
  if (id == OB_NONE) { emit(buf, cap, pos, "?"); return; }
  n = &NODE[id];
  who = (n->which == 0u) ? "x" : "y";
  switch (n->op) {
    case TO_CONST: sprintf(t, "%g", n->c); emit(buf, cap, pos, t); return;
    case TO_ATTR:
      emit(buf, cap, pos, who);
      emit(buf, cap, pos, ".");
      emit(buf, cap, pos, W->attr_name[n->k]);
      return;
    case TO_COUNT: emit(buf, cap, pos, "how many things"); return;
    case TO_JOINS: emit(buf, cap, pos, "how many joins"); return;
    case TO_DEG:
      emit(buf, cap, pos, who);
      emit(buf, cap, pos, "'s joins");
      return;
    case TO_TOTAL:
      emit(buf, cap, pos, "total ");
      emit(buf, cap, pos, W->attr_name[n->k]);
      return;
    case TO_LARGEST:
      emit(buf, cap, pos, "largest ");
      emit(buf, cap, pos, W->attr_name[n->k]);
      return;
    case TO_ABS:
      emit(buf, cap, pos, "abs(");
      say(n->a, buf, cap, pos);
      emit(buf, cap, pos, ")");
      return;
    case TO_MOD:
      emit(buf, cap, pos, "(");
      say(n->a, buf, cap, pos);
      sprintf(t, " %% %g)", n->c);
      emit(buf, cap, pos, t);
      return;
    case TO_HOWMANY:
      emit(buf, cap, pos, "how many x with (");
      say(n->a, buf, cap, pos);
      emit(buf, cap, pos, ")");
      return;
    case SO_JOINED:
      emit(buf, cap, pos, (n->which == 0u) ? "x" : "y");
      emit(buf, cap, pos, " joined to ");
      emit(buf, cap, pos, (n->k == 0u) ? "x" : "y");
      return;
    case SO_NOT:
      emit(buf, cap, pos, "not (");
      say(n->a, buf, cap, pos);
      emit(buf, cap, pos, ")");
      return;
    case SO_EVERY:
      emit(buf, cap, pos, "every x: ");
      say(n->a, buf, cap, pos);
      return;
    case SO_SOME:
      emit(buf, cap, pos, "some x: ");
      say(n->a, buf, cap, pos);
      return;
    case SO_EVERY2:
      emit(buf, cap, pos, "every x and y: ");
      say(n->a, buf, cap, pos);
      return;
    case SO_EVERYJOIN:
      emit(buf, cap, pos, "every joined x and y: ");
      say(n->a, buf, cap, pos);
      return;
    default: break;
  }
  {
    const char *sym = "?";
    int fn = 0;
    switch (n->op) {
      case TO_ADD: sym = " + "; break;
      case TO_SUB: sym = " - "; break;
      case TO_MUL: sym = " * "; break;
      case TO_MIN: sym = "min"; fn = 1; break;
      case TO_MAX: sym = "max"; fn = 1; break;
      case SO_LE: sym = " <= "; break;
      case SO_EQ: sym = " == "; break;
      case SO_AND: sym = " and "; break;
      case SO_OR: sym = " or "; break;
      default: break;
    }
    if (fn) {
      emit(buf, cap, pos, sym);
      emit(buf, cap, pos, "(");
      say(n->a, buf, cap, pos);
      emit(buf, cap, pos, ", ");
      say(n->b, buf, cap, pos);
      emit(buf, cap, pos, ")");
      return;
    }
    emit(buf, cap, pos, "(");
    say(n->a, buf, cap, pos);
    emit(buf, cap, pos, sym);
    say(n->b, buf, cap, pos);
    emit(buf, cap, pos, ")");
  }
}

static void write_text(unsigned id, char *buf, unsigned cap) {
  unsigned pos = 0u;
  buf[0] = '\0';
  say(id, buf, cap, &pos);
}

/* ---- the world it is given ----------------------------------------------- */

void ob_world_init(ob_world_t *w) {
  if (w == 0) return;
  memset(w, 0, sizeof(*w));
  strcpy(w->outcome_name, "outcome");
}

sm_status_t ob_attr(ob_world_t *w, const char *name, double lo, double hi, int whole,
                    unsigned *idx) {
  unsigned i;
  if (w == 0 || name == 0 || idx == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_attr >= OB_MAX_ATTR) return SM_ERR_DOMAIN_TOO_LARGE;
  if (strlen(name) + 1u > SX_NAME || lo > hi) return SM_ERR_BAD_THRESHOLD;
  i = w->n_attr++;
  strcpy(w->attr_name[i], name);
  w->lo[i] = lo;
  w->hi[i] = hi;
  w->whole[i] = whole;
  *idx = i;
  return SM_OK;
}

sm_status_t ob_outcome_name(ob_world_t *w, const char *name) {
  if (w == 0 || name == 0) return SM_ERR_NULL_ARGUMENT;
  if (strlen(name) + 1u > SX_NAME) return SM_ERR_BAD_THRESHOLD;
  strcpy(w->outcome_name, name);
  return SM_OK;
}

sm_status_t ob_scene(ob_world_t *w) {
  if (w == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_scenes >= OB_MAX_SCENES) return SM_ERR_DOMAIN_TOO_LARGE;
  memset(&w->scene[w->n_scenes], 0, sizeof(w->scene[0]));
  w->n_scenes++;
  return SM_OK;
}

sm_status_t ob_item(ob_world_t *w, const double *attrs, unsigned *idx) {
  ob_scene_t *sc;
  unsigned i, k;
  if (w == 0 || attrs == 0 || idx == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_scenes == 0u) return SM_ERR_EMPTY_DOMAIN;
  sc = &w->scene[w->n_scenes - 1u];
  if (sc->n_items >= OB_MAX_ITEMS) return SM_ERR_DOMAIN_TOO_LARGE;
  i = sc->n_items++;
  for (k = 0u; k < w->n_attr; k++) sc->attr[i][k] = attrs[k];
  *idx = i;
  return SM_OK;
}

sm_status_t ob_join(ob_world_t *w, unsigned a, unsigned b) {
  ob_scene_t *sc;
  if (w == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_scenes == 0u) return SM_ERR_EMPTY_DOMAIN;
  sc = &w->scene[w->n_scenes - 1u];
  if (a >= sc->n_items || b >= sc->n_items) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  sc->join[a][b] = 1u;
  return SM_OK;
}

sm_status_t ob_says(ob_world_t *w, double outcome) {
  if (w == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_scenes == 0u) return SM_ERR_EMPTY_DOMAIN;
  w->scene[w->n_scenes - 1u].outcome = outcome;
  w->scene[w->n_scenes - 1u].has_outcome = 1;
  return SM_OK;
}

/* ---- the search ---------------------------------------------------------- */

static int outcome_yes_no_of(const ob_world_t *w) {
  unsigned s;
  for (s = 0u; s < w->n_scenes; s++) {
    if (w->scene[s].outcome != 0.0 && w->scene[s].outcome != 1.0) return 0;
  }
  return 1;
}

static int term_fits(unsigned id) {
  unsigned s;
  for (s = 0u; s < N_REAL; s++) {
    iv_t v = t_eval(id, &ALL_SCENE[s], OB_NONE, OB_NONE);
    if (!(v.lo > -1e300 && v.hi < 1e300)) return 0;
    if (!(v.lo <= ALL_SCENE[s].outcome && ALL_SCENE[s].outcome <= v.hi)) return 0;
  }
  return 1;
}

static int stmt_fits(unsigned id) {
  unsigned s;
  for (s = 0u; s < N_REAL; s++) {
    int r = s_eval(id, &ALL_SCENE[s], OB_NONE, OB_NONE);
    if (r != (int)ALL_SCENE[s].outcome) return 0;
  }
  return 1;
}

static int stmt_holds_real(unsigned id) {
  unsigned s;
  for (s = 0u; s < N_REAL; s++) {
    if (s_eval(id, &ALL_SCENE[s], OB_NONE, OB_NONE) != 1) return 0;
  }
  return 1;
}

static void prepare(const ob_world_t *w, int mode) {
  W = w;
  BUDGET = OB_BUDGET;
  EVID_MODE = 0;
  TRIED = 0u;
  N_NODES = 0u;
  memset(TBL, 0, sizeof TBL);
  make_scenes();
  build_terms();
  EVID_MODE = mode;   /* the scenes exist now, so a claim can be weighed */
  build_statements();
}

sm_status_t ob_find_law(const ob_world_t *w, ob_law_t *out) {
  unsigned surv[OB_MAX_SURV];
  unsigned n_surv = 0u, tried = 0u, i, chosen = OB_NONE, rival = OB_NONE;
  int yes_no;

  if (w == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_attr == 0u || w->n_scenes < 2u) return SM_ERR_EMPTY_DOMAIN;
  memset(out, 0, sizeof(*out));
  yes_no = outcome_yes_no_of(w);
  prepare(w, yes_no ? 2 : 3);

  if (yes_no) {
    tried = TRIED;
    for (i = 0u; i < N_STMT; i++) {
      if (NODE[STMT[i]].mask != 0u) continue;
      if (n_surv < OB_MAX_SURV) surv[n_surv++] = STMT[i];
    }
  } else {
    for (i = 0u; i < N_TERM; i++) {
      if (NODE[TERM[i]].mask != 0u) continue;
      tried++;
      if (term_fits(TERM[i]) && n_surv < OB_MAX_SURV) surv[n_surv++] = TERM[i];
    }
  }
  for (i = 0u; i < n_surv; i++) {
    if (chosen == OB_NONE || NODE[surv[i]].size < NODE[chosen].size) chosen = surv[i];
  }
  for (i = 0u; i < n_surv; i++) {
    if (surv[i] == chosen) continue;
    if (rival == OB_NONE || NODE[surv[i]].size < NODE[rival].size) rival = surv[i];
  }
  out->candidates = tried;
  out->survivors = n_surv;
  out->support = (tried == 0u) ? 0.0 : (double)(tried - n_surv) / (double)tried;
  out->reached = REACHED;
  out->capped = (N_TERM >= OB_TERM_CAP || N_STMT >= OB_STMT_CAP || BUDGET <= 0);
  out->found = (chosen != OB_NONE);
  if (chosen != OB_NONE) write_text(chosen, out->law, OB_TEXT);
  if (rival != OB_NONE) {
    write_text(rival, out->rival, OB_TEXT);
    out->has_rival = 1;
  }
  return SM_OK;
}

/* ---- what it knows, settled by elimination over the universe ------------- */

#define OB_HELD 384u

static unsigned HOLDS[OB_HELD][OB_UNIV_WORDS];   /* a bit per scene of the universe */
static unsigned held[OB_HELD];
static int live[OB_HELD];
static unsigned rules_out[OB_HELD];
/*
 * A statement that no scene of the grid satisfies is one the universe
 * cannot weigh: "there are at least four things" is like that when the
 * grid reaches three. Such a statement is still reported, since it is
 * true of everything it saw, but it is never used to force another and
 * never ranked above the ones the universe can actually speak to.
 */
static int IN_RANGE[OB_HELD];
static unsigned PRE[OB_HELD + 1u][OB_UNIV_WORDS];   /* all the live ones before this */
static unsigned SUF[OB_HELD + 1u][OB_UNIV_WORDS];   /* and all of them after */
static unsigned ORDER[OB_HELD];
static unsigned CAND[OB_STMT_CAP];
static unsigned SCORE[OB_STMT_CAP];


/*
 * Does A force B? In no scene of the universe does A hold while B fails.
 * A must hold somewhere, so the question is a real one, and fail somewhere,
 * so A is not merely saying nothing. Both are checked over every scene,
 * not over a sample of them.
 */
static unsigned UNIV_WORDS;
static unsigned LAST_WORD_MASK;

static int forces(unsigned ia, unsigned ib) {
  unsigned w;
  int saw_true = 0, saw_false = 0;
  if (!IN_RANGE[ia]) return 0;   /* the universe cannot speak to this one */
  for (w = 0u; w < UNIV_WORDS; w++) {
    unsigned a = HOLDS[ia][w], b = HOLDS[ib][w];
    unsigned full = (w + 1u == UNIV_WORDS) ? LAST_WORD_MASK : 0xFFFFFFFFu;
    if (a & ~b & full) return 0;      /* a scene where A holds and B fails */
    if (a & full) saw_true = 1;
    if (~a & full) saw_false = 1;
  }
  return saw_true && saw_false;
}

sm_status_t ob_facts(const ob_world_t *w, ob_facts_t *out) {
  static ob_scene_t sc;
  unsigned n_held = 0u, i, j;
  int changed;

  if (w == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (w->n_attr == 0u || w->n_scenes < 1u) return SM_ERR_EMPTY_DOMAIN;
  memset(out, 0, sizeof(*out));
  prepare(w, 1);
  out->candidates = TRIED;
  out->universe = UNIV_N;
  out->reached = REACHED;
  UNIV_WORDS = (UNIV_N + 31u) / 32u;
  LAST_WORD_MASK = (UNIV_N % 32u == 0u) ? 0xFFFFFFFFu : ((1u << (UNIV_N % 32u)) - 1u);
  out->universe_items = UNIV_MAXN;

  out->candidates = TRIED;

  /*
   * Everything that held in every scene it saw, which is now far more than
   * it can weigh in full. They are ranked first by how much each rules out
   * of a spread of the universe, which is cheap, and only the strongest are
   * carried forward to be settled against the universe entire. Without
   * this the short weak statements take every slot and a long one that says
   * something is never looked at.
   */
  {
    unsigned n_cand = 0u, k;
    for (i = 0u; i < N_STMT; i++) {
      unsigned sc, fails = 0u;
      if (NODE[STMT[i]].mask != 0u) continue;
      for (sc = N_REAL; sc < N_ALL; sc++) {
        if (s_eval(STMT[i], &ALL_SCENE[sc], OB_NONE, OB_NONE) != 1) fails++;
      }
      CAND[n_cand] = STMT[i];
      SCORE[n_cand] = fails;
      n_cand++;
    }
    for (k = 0u; k < OB_HELD && k < n_cand; k++) {
      unsigned best = k, t;
      for (i = k + 1u; i < n_cand; i++) {
        if (SCORE[i] > SCORE[best]) { best = i; continue; }
        if (SCORE[i] == SCORE[best] && NODE[CAND[i]].size < NODE[CAND[best]].size) best = i;
      }
      t = CAND[k]; CAND[k] = CAND[best]; CAND[best] = t;
      t = SCORE[k]; SCORE[k] = SCORE[best]; SCORE[best] = t;
      held[n_held] = CAND[k];
      live[n_held] = 1;
      n_held++;
    }
  }

  /* where each one stands in the universe: true here, false there. This
     is the whole of the judgement, and it is an elimination. */
  for (i = 0u; i < n_held; i++) {
    memset(HOLDS[i], 0, sizeof(HOLDS[i]));
    rules_out[i] = 0u;
  }
  for (i = 0u; i < n_held; i++) IN_RANGE[i] = 0;
  for (j = 0u; j < UNIV_N; j++) {
    universe_scene(j, &sc);
    for (i = 0u; i < n_held; i++) {
      if (s_eval(held[i], &sc, OB_NONE, OB_NONE) == 1) {
        HOLDS[i][j >> 5] |= 1u << (j & 31u);
        if (j < UNIV_GRID) IN_RANGE[i] = 1;
      } else {
        rules_out[i]++;
      }
    }
  }

  /* one true of every scene there is rules nothing out, so it says
     nothing about these scenes */
  for (i = 0u; i < n_held; i++) {
    if (rules_out[i] == 0u) live[i] = 0;
  }

  /* one that another already forces is not a second thing it knows: in no
     scene of the universe does that one hold while this one fails, and
     there are scenes on both sides, so the question was a real one. The
     weakest such one goes first, so letting go never costs it the fact
     that was carrying the others. */
  do {
    unsigned drop = OB_HELD, n_live = 0u, k, w;
    changed = 0;
    for (i = 0u; i < n_held; i++) {
      if (live[i]) ORDER[n_live++] = i;
    }
    for (w = 0u; w < UNIV_WORDS; w++) {
      PRE[0][w] = 0xFFFFFFFFu;
      SUF[n_live][w] = 0xFFFFFFFFu;
    }
    for (k = 0u; k < n_live; k++) {
      for (w = 0u; w < UNIV_WORDS; w++) {
        PRE[k + 1u][w] = PRE[k][w] & (IN_RANGE[ORDER[k]] ? HOLDS[ORDER[k]][w] : 0xFFFFFFFFu);
      }
    }
    for (k = n_live; k > 0u; k--) {
      for (w = 0u; w < UNIV_WORDS; w++) {
        unsigned m = IN_RANGE[ORDER[k - 1u]] ? HOLDS[ORDER[k - 1u]][w] : 0xFFFFFFFFu;
        SUF[k - 1u][w] = SUF[k][w] & m;
      }
    }
    for (k = 0u; k < n_live; k++) {
      unsigned j = ORDER[k];
      int forced = 0;
      for (i = 0u; i < n_held && !forced; i++) {
        if (i == j || !live[i]) continue;
        if (!forces(i, j)) continue;
        if (forces(j, i)) {
          if (NODE[held[i]].size > NODE[held[j]].size) continue;
          if (NODE[held[i]].size == NODE[held[j]].size && i > j) continue;
        }
        forced = 1;
      }
      if (!forced) {
        /* or the rest of them together, where no one of them does it alone.
           The scenes where they all hold are counted, and a handful is not
           enough to conclude anything from: with too few, the only scenes
           left standing are the ones it saw, and everything it believes
           would look forced. */
        unsigned w, evidence = 0u;
        int all = 1;
        for (w = 0u; w < UNIV_WORDS; w++) {
          unsigned full = (w + 1u == UNIV_WORDS) ? LAST_WORD_MASK : 0xFFFFFFFFu;
          unsigned others = PRE[k][w] & SUF[k + 1u][w] & full;
          unsigned b = others;
          while (b != 0u) { evidence++; b &= b - 1u; }
          if (others & ~HOLDS[j][w]) all = 0;
        }
        if (all && evidence >= OB_ENOUGH) forced = 1;
      }
      if (!forced) continue;
      if (drop == OB_HELD || rules_out[j] < rules_out[drop]) drop = j;
    }
    if (drop != OB_HELD) {
      live[drop] = 0;
      changed = 1;
    }
  } while (changed);
  /* what it knows, the most telling first */
  while (out->n < OB_MAX_FACTS) {
    unsigned best = OB_HELD;
    for (i = 0u; i < n_held; i++) {
      if (!live[i]) continue;
      if (best == OB_HELD) { best = i; continue; }
      if (IN_RANGE[i] != IN_RANGE[best]) {
        if (IN_RANGE[i]) best = i;
        continue;
      }
      if (rules_out[i] > rules_out[best]) { best = i; continue; }
      if (rules_out[i] == rules_out[best] && NODE[held[i]].size < NODE[held[best]].size)
        best = i;
    }
    if (best == OB_HELD) break;
    live[best] = 0;
    write_text(held[best], out->text[out->n], OB_TEXT);
    out->ruled_out[out->n] = rules_out[best];
    out->n++;
  }
  out->capped = (N_STMT >= OB_STMT_CAP || BUDGET <= 0);
  return SM_OK;
}

void ob_report_law(const ob_world_t *w, const ob_law_t *l) {
  if (w == 0 || l == 0) return;
  printf("  %u scenes, made of things and joins, with nothing else said\n", w->n_scenes);
  if (!l->found) {
    printf("  no description among the %u it could write survived every scene\n", l->candidates);
    return;
  }
  printf("  %s is %s\n", w->outcome_name, l->law);
  printf("  %u descriptions were possible, %u survived, support %.4f\n", l->candidates,
         l->survivors, l->support);
  if (l->has_rival) printf("  another account fits just as well: %s\n", l->rival);
  if (l->capped) {
    printf("  the longest description it got to write was %u steps of the %u it\n",
           l->reached, OB_STMT_SIZE);
    printf("  allows, so nothing longer was searched, and nothing longer is claimed\n");
  }
}

void ob_report_facts(const ob_world_t *w, const ob_facts_t *f) {
  unsigned i;
  if (w == 0 || f == 0) return;
  printf("  out of %u things it could say about every scene, %u stand on their own:\n",
         f->candidates, f->n);
  for (i = 0u; i < f->n; i++) {
    printf("    %-42s rules out %u of %u\n", f->text[i], f->ruled_out[i], f->universe);
  }
  printf("  settled against every scene of at most %u things on a grid, and every\n",
         f->universe_items);
  printf("  scene one step from one it saw: %u scenes in all, written out in full,\n",
         f->universe);
  printf("  so what is dropped as saying nothing, and what is dropped as already\n");
  printf("  forced, are eliminations with a scene behind them, not guesses\n");
  if (f->n == 0u) printf("    nothing it can say holds in every scene\n");
  if (f->capped) {
    printf("  the longest thing it got to say was %u steps of the %u it allows:\n",
           f->reached, OB_STMT_SIZE);
    printf("  anything needing greater length was never written down, and so was\n");
    printf("  never eliminated either: nothing longer is claimed against\n");
  }
}
