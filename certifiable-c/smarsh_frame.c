/*
 * smarsh_frame.c -- the formulation, found by elimination.
 *
 * See smarsh_frame.h. One formulation at a time: the workspace below is
 * static and reused, the same discipline as smarsh_space.c.
 *
 * The search is ordered by size, so the first description that survives is
 * the shortest one in the whole space, not the first one a generator
 * happened to write. Two candidates are the same description when they
 * agree at every point of the declared world, not merely on the rows that
 * were seen: so what survives is a list of genuinely different accounts,
 * and saying "another account fits just as well" means it.
 */

#include "smarsh_frame.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define FR_MAX_NODES 400000u
#define FR_POOL_CAP 6000u
#define FR_BOOL_CAP 6000u
#define FR_TBL 32768u
#define FR_MAX_SURV 16u
#define FR_PROBES 48u        /* points of the declared world used to tell descriptions apart */
#define FR_NONE 0xFFFFFFFFu
#define FR_SENTINEL (-1.2345678901e300)

typedef enum {
  FO_CONST = 0, FO_VAR,
  FO_ADD, FO_SUB, FO_MUL, FO_DIV, FO_MOD, FO_MIN, FO_MAX,
  FO_NEG, FO_ABS,
  FO_LE, FO_GE, FO_EQ,
  FO_AND, FO_OR, FO_NOT
} fo_t;

typedef struct {
  fo_t op;
  unsigned a, b;
  double c;
  unsigned size;
} fn_t;

static fn_t NODE[FR_MAX_NODES];
static unsigned N_NODES;

static unsigned POOL[FR_POOL_CAP];        /* number-valued, in order of size */
static unsigned N_POOL;
static unsigned SIZE_END[FR_MAX_SIZE + 2u];
static unsigned BOOL[FR_BOOL_CAP];        /* yes/no-valued */
static unsigned N_BOOL;

static uint64_t HA[FR_POOL_CAP + FR_BOOL_CAP], HB[FR_POOL_CAP + FR_BOOL_CAP];
static unsigned TBL[2u][FR_TBL];

static const fr_situation_t *S;
static double PROBE[FR_PROBES][FR_MAX_READINGS];
static unsigned N_PROBE;
static unsigned LEAF[FR_MAX_READINGS + 5u];
static unsigned N_LEAF;
static int BUDGET;

/* ---- building and evaluating candidate descriptions ---------------------- */

static unsigned mk(fo_t op, unsigned a, unsigned b, double c) {
  unsigned id;
  if (N_NODES >= FR_MAX_NODES) return FR_NONE;
  if (op != FO_CONST && op != FO_VAR && a == FR_NONE) return FR_NONE;
  if (op != FO_CONST && op != FO_VAR && op != FO_NEG && op != FO_ABS && op != FO_NOT &&
      b == FR_NONE)
    return FR_NONE;
  id = N_NODES++;
  NODE[id].op = op;
  NODE[id].a = a;
  NODE[id].b = b;
  NODE[id].c = c;
  if (op == FO_CONST || op == FO_VAR) NODE[id].size = 1u;
  else if (op == FO_NEG || op == FO_ABS || op == FO_NOT) NODE[id].size = 1u + NODE[a].size;
  else NODE[id].size = 1u + NODE[a].size + NODE[b].size;
  return id;
}

/* `row` points at the readings of one situation, real or imagined. */
static iv_t ev(unsigned id, const double *row) {
  const fn_t *n = &NODE[id];
  switch (n->op) {
    case FO_CONST: return iv_point(n->c);
    case FO_VAR: return iv_point(row[n->a]);
    case FO_ADD: return iv_add(ev(n->a, row), ev(n->b, row));
    case FO_SUB: return iv_sub(ev(n->a, row), ev(n->b, row));
    case FO_MUL: return iv_mul(ev(n->a, row), ev(n->b, row));
    case FO_DIV: return iv_div(ev(n->a, row), ev(n->b, row));
    case FO_MOD: return iv_mod(ev(n->a, row), ev(n->b, row));
    case FO_MIN: return iv_min(ev(n->a, row), ev(n->b, row));
    case FO_MAX: return iv_max(ev(n->a, row), ev(n->b, row));
    case FO_NEG: return iv_neg(ev(n->a, row));
    case FO_ABS: return iv_abs(ev(n->a, row));
    case FO_LE: return iv_le(ev(n->a, row), ev(n->b, row));
    case FO_GE: return iv_le(ev(n->b, row), ev(n->a, row));
    case FO_EQ: return iv_eq(ev(n->a, row), ev(n->b, row));
    case FO_AND: return iv_and(ev(n->a, row), ev(n->b, row));
    case FO_OR: return iv_or(ev(n->a, row), ev(n->b, row));
    case FO_NOT: return iv_not(ev(n->a, row));
    default: break;
  }
  return iv_empty();
}

static int is_bool_op(fo_t op) {
  return op == FO_LE || op == FO_GE || op == FO_EQ || op == FO_AND || op == FO_OR ||
         op == FO_NOT;
}

/*
 * Two descriptions are the same description when they agree everywhere in
 * the declared world, not merely on the rows that were seen. The points
 * below stand in for "everywhere"; a pair that agrees on all of them and
 * differs somewhere else is possible and vanishingly unlikely, and is the
 * one place this module trades certainty for reach.
 */
static uint64_t bits(double v) {
  uint64_t b;
  if (v == 0.0) v = 0.0;                      /* -0 and 0 are one value */
  if (!(v > -1e300 && v < 1e300)) v = FR_SENTINEL; /* undefined here, and known so */
  memcpy(&b, &v, sizeof b);
  return b;
}

static void hash_of(unsigned id, uint64_t *ha, uint64_t *hb) {
  uint64_t x = 0xcbf29ce484222325ULL, y = 0x9e3779b97f4a7c15ULL;
  unsigned p;
  for (p = 0u; p < N_PROBE; p++) {
    iv_t v = ev(id, PROBE[p]);
    uint64_t b = bits((v.lo == v.hi) ? v.lo : (v.lo + v.hi) * 0.5);
    x = (x ^ b) * 0x100000001b3ULL;
    y = (y + b + 0x9e3779b97f4a7c15ULL) * 0xff51afd7ed558ccdULL;
    y ^= y >> 29;
  }
  *ha = x;
  *hb = y;
}

/* Add to a pool unless something already there behaves identically. */
static int keep(unsigned id, unsigned *pool, unsigned *n, unsigned cap, unsigned tbl,
                unsigned base) {
  uint64_t ha, hb;
  unsigned h, i;
  if (id == FR_NONE || *n >= cap) return 0;
  BUDGET--;
  if (BUDGET < 0) return 0;
  hash_of(id, &ha, &hb);
  h = (unsigned)(ha & (uint64_t)(FR_TBL - 1u));
  for (i = 0u; i < FR_TBL; i++) {
    unsigned slot = TBL[tbl][(h + i) & (FR_TBL - 1u)];
    if (slot == 0u) break;
    if (HA[slot - 1u] == ha && HB[slot - 1u] == hb) return 0;
  }
  HA[base + *n] = ha;
  HB[base + *n] = hb;
  TBL[tbl][(h + i) & (FR_TBL - 1u)] = base + *n + 1u;
  pool[*n] = id;
  (*n)++;
  return 1;
}

/*
 * The points at which two descriptions count as the same description.
 * Nothing is drawn at random: they are a grid laid across the declared
 * range of every reading, walked in step so that every combination of
 * grid values comes up before any repeats. The rows it actually saw are
 * put in as well, so two accounts that agree everywhere else and differ on
 * the evidence are never merged.
 */
static void draw_probes(void) {
  unsigned p, i;
  N_PROBE = FR_PROBES;
  for (p = 0u; p < N_PROBE; p++) {
    unsigned code = p;
    for (i = 0u; i < S->n_readings; i++) {
      unsigned steps = 4u, at;
      double v;
      if (S->whole[i] && S->hi[i] - S->lo[i] < 3.0) {
        steps = (unsigned)(S->hi[i] - S->lo[i]) + 1u;
      }
      at = code % steps;
      code /= steps;
      v = (steps < 2u) ? S->lo[i]
                       : S->lo[i] + (S->hi[i] - S->lo[i]) * ((double)at / (double)(steps - 1u));
      if (S->whole[i]) v = floor(v + 0.5);
      PROBE[p][i] = v;
    }
  }
  /* the rows themselves are part of "everywhere": two accounts that differ
     only off the evidence must still be told apart, and two that differ on
     it must never be merged */
  for (p = 0u; p < S->n_obs && p < N_PROBE; p++) {
    for (i = 0u; i < S->n_readings; i++) PROBE[p][i] = S->reading[p][i];
  }
}

static void pool_binary(unsigned a, unsigned b) {
  keep(mk(FO_ADD, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_MUL, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_SUB, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_SUB, b, a, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_MIN, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_MAX, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_DIV, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  keep(mk(FO_DIV, b, a, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  if (NODE[b].op == FO_CONST && NODE[b].c >= 2.0) {
    keep(mk(FO_MOD, a, b, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  }
  if (NODE[a].op == FO_CONST && NODE[a].c >= 2.0) {
    keep(mk(FO_MOD, b, a, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  }
}

/*
 * Every number-valued description, shortest first. Size 1 is a reading or
 * a small whole number; a description of size s is an operation over parts
 * whose sizes add to s - 1.
 */
static void build_pool(unsigned max_size) {
  static const double konst[5] = {0.0, 1.0, 2.0, 3.0, 10.0};
  unsigned s, i;

  N_NODES = 0u;
  N_POOL = 0u;
  N_BOOL = 0u;
  N_LEAF = 0u;
  memset(TBL, 0, sizeof TBL);

  for (i = 0u; i < S->n_readings; i++) LEAF[N_LEAF++] = mk(FO_VAR, i, 0u, 0.0);
  for (i = 0u; i < 5u; i++) LEAF[N_LEAF++] = mk(FO_CONST, 0u, 0u, konst[i]);
  for (i = 0u; i < N_LEAF; i++) keep(LEAF[i], POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
  SIZE_END[1] = N_POOL;

  for (s = 2u; s <= max_size; s++) {
    unsigned lo_a, hi_a;
    /* one operation over one part */
    lo_a = SIZE_END[s - 2u];
    hi_a = SIZE_END[s - 1u];
    for (i = lo_a; i < hi_a && BUDGET > 0; i++) {
      keep(mk(FO_NEG, POOL[i], FR_NONE, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
      keep(mk(FO_ABS, POOL[i], FR_NONE, 0.0), POOL, &N_POOL, FR_POOL_CAP, 0u, 0u);
    }
    /* one operation over two parts whose sizes add to s - 1 */
    for (i = 1u; i + 1u < s && BUDGET > 0; i++) {
      unsigned ja = SIZE_END[s - 1u - i - 1u], jb = SIZE_END[s - 1u - i];
      unsigned ia = SIZE_END[i - 1u], ib = SIZE_END[i];
      unsigned p, q;
      if (s - 1u - i < i) continue;   /* each unordered pair of sizes once */
      for (p = ia; p < ib && BUDGET > 0; p++) {
        for (q = ja; q < jb && BUDGET > 0; q++) {
          if (s - 1u - i == i && q < p) continue;
          pool_binary(POOL[p], POOL[q]);
        }
      }
    }
    SIZE_END[s] = N_POOL;
    if (N_POOL >= FR_POOL_CAP || BUDGET <= 0) {
      for (i = s + 1u; i <= max_size; i++) SIZE_END[i] = N_POOL;
      break;
    }
  }
  SIZE_END[0] = 0u;
}

/*
 * Every yes/no description: a comparison between number-valued parts, and
 * and/or/not over those. This is where rules, thresholds and plain logic
 * live, and it is the same enumeration.
 */
static void build_bool(unsigned max_cmp, unsigned logic) {
  unsigned i, j, first, n_atom;
  unsigned cmp_end = (max_cmp <= FR_MAX_SIZE) ? SIZE_END[max_cmp] : N_POOL;

  for (i = 0u; i < cmp_end && BUDGET > 0; i++) {
    for (j = 0u; j < N_LEAF && BUDGET > 0; j++) {
      keep(mk(FO_GE, POOL[i], LEAF[j], 0.0), BOOL, &N_BOOL, FR_BOOL_CAP, 1u, FR_POOL_CAP);
      keep(mk(FO_LE, POOL[i], LEAF[j], 0.0), BOOL, &N_BOOL, FR_BOOL_CAP, 1u, FR_POOL_CAP);
      keep(mk(FO_EQ, POOL[i], LEAF[j], 0.0), BOOL, &N_BOOL, FR_BOOL_CAP, 1u, FR_POOL_CAP);
    }
  }
  n_atom = N_BOOL;
  if (!logic) return;
  first = N_BOOL;
  for (i = 0u; i < n_atom && BUDGET > 0; i++) {
    keep(mk(FO_NOT, BOOL[i], FR_NONE, 0.0), BOOL, &N_BOOL, FR_BOOL_CAP, 1u, FR_POOL_CAP);
  }
  (void)first;
  for (i = 0u; i < n_atom && BUDGET > 0; i++) {
    for (j = i + 1u; j < n_atom && BUDGET > 0; j++) {
      keep(mk(FO_AND, BOOL[i], BOOL[j], 0.0), BOOL, &N_BOOL, FR_BOOL_CAP, 1u, FR_POOL_CAP);
      keep(mk(FO_OR, BOOL[i], BOOL[j], 0.0), BOOL, &N_BOOL, FR_BOOL_CAP, 1u, FR_POOL_CAP);
    }
  }
}

/* ---- writing a description out ------------------------------------------ */

typedef struct {
  unsigned n;
  unsigned id[FR_MAX_DEFS];
  const char *name[FR_MAX_DEFS];
} fr_subs_t;

static void emit(char *buf, unsigned cap, unsigned *pos, const char *s) {
  while (*s && *pos + 1u < cap) buf[(*pos)++] = *s++;
  buf[*pos] = '\0';
}

static void render(unsigned id, char *buf, unsigned cap, unsigned *pos, int shape,
                   const fr_subs_t *subs) {
  const fn_t *n;
  const char *sym = "";
  int fn = 0;
  unsigned i;

  if (id == FR_NONE) { emit(buf, cap, pos, "?"); return; }
  if (subs != 0) {
    for (i = 0u; i < subs->n; i++) {
      if (subs->id[i] == id) { emit(buf, cap, pos, subs->name[i]); return; }
    }
  }
  n = &NODE[id];
  if (n->op == FO_CONST) {
    char t[32];
    sprintf(t, "%g", n->c);
    emit(buf, cap, pos, t);
    return;
  }
  if (n->op == FO_VAR) {
    emit(buf, cap, pos, shape ? "#" : S->reading_name[n->a]);
    return;
  }
  if (n->op == FO_NEG || n->op == FO_ABS || n->op == FO_NOT) {
    emit(buf, cap, pos, n->op == FO_NEG ? "-" : (n->op == FO_ABS ? "abs(" : "not "));
    if (n->op == FO_NEG) {
      emit(buf, cap, pos, "(");
      render(n->a, buf, cap, pos, shape, subs);
      emit(buf, cap, pos, ")");
    } else if (n->op == FO_ABS) {
      render(n->a, buf, cap, pos, shape, subs);
      emit(buf, cap, pos, ")");
    } else {
      emit(buf, cap, pos, "(");
      render(n->a, buf, cap, pos, shape, subs);
      emit(buf, cap, pos, ")");
    }
    return;
  }
  switch (n->op) {
    case FO_ADD: sym = " + "; break;
    case FO_SUB: sym = " - "; break;
    case FO_MUL: sym = " * "; break;
    case FO_DIV: sym = " / "; break;
    case FO_MOD: sym = " % "; break;
    case FO_GE: sym = " >= "; break;
    case FO_LE: sym = " <= "; break;
    case FO_EQ: sym = " == "; break;
    case FO_AND: sym = " and "; break;
    case FO_OR: sym = " or "; break;
    case FO_MIN: sym = "min"; fn = 1; break;
    case FO_MAX: sym = "max"; fn = 1; break;
    default: break;
  }
  if (fn) {
    emit(buf, cap, pos, sym);
    emit(buf, cap, pos, "(");
    render(n->a, buf, cap, pos, shape, subs);
    emit(buf, cap, pos, ", ");
    render(n->b, buf, cap, pos, shape, subs);
    emit(buf, cap, pos, ")");
    return;
  }
  emit(buf, cap, pos, "(");
  render(n->a, buf, cap, pos, shape, subs);
  emit(buf, cap, pos, sym);
  render(n->b, buf, cap, pos, shape, subs);
  emit(buf, cap, pos, ")");
}

static void write_law(unsigned id, char *buf, unsigned cap, int shape, const fr_subs_t *subs) {
  unsigned pos = 0u;
  buf[0] = '\0';
  render(id, buf, cap, &pos, shape, subs);
}

static unsigned shape_of(unsigned id) {
  char t[FR_TEXT];
  unsigned h = 2166136261u, i;
  write_law(id, t, FR_TEXT, 1, 0);
  for (i = 0u; t[i] != '\0'; i++) {
    h ^= (unsigned)(unsigned char)t[i];
    h *= 16777619u;
  }
  return h;
}

static int uses_var(unsigned id, unsigned v) {
  const fn_t *n;
  if (id == FR_NONE) return 0;
  n = &NODE[id];
  if (n->op == FO_VAR) return n->a == v;
  if (n->op == FO_CONST) return 0;
  if (n->op == FO_NEG || n->op == FO_ABS || n->op == FO_NOT) return uses_var(n->a, v);
  return uses_var(n->a, v) || uses_var(n->b, v);
}

/* ---- the search ---------------------------------------------------------- */

static int outcome_is_yes_no(const fr_situation_t *s) {
  unsigned r;
  for (r = 0u; r < s->n_obs; r++) {
    if (s->outcome[r] != 0.0 && s->outcome[r] != 1.0) return 0;
  }
  return 1;
}

static int agrees(unsigned id, int yes_no) {
  unsigned r;
  for (r = 0u; r < S->n_obs; r++) {
    iv_t v = ev(id, S->reading[r]);
    if (yes_no) {
      int said = (v.lo > 0.5) ? 1 : ((v.hi < 0.5) ? 0 : -1);
      if (said != (int)S->outcome[r]) return 0;
    } else {
      /* a description that says nothing here is not a description of here:
         an unbounded or empty answer is eliminated, never counted as agreeing */
      if (!(v.lo > -1e300 && v.hi < 1e300)) return 0;
      if (!(v.lo <= S->outcome[r] && S->outcome[r] <= v.hi)) return 0;
    }
  }
  return 1;
}

/* true in every row, whatever the outcome column says */
static int holds_everywhere_seen(unsigned id) {
  unsigned r;
  for (r = 0u; r < S->n_obs; r++) {
    iv_t v = ev(id, S->reading[r]);
    if (!(v.lo > 0.5 && v.hi > 0.5)) return 0;   /* certainly true, and defined here */
  }
  return 1;
}

void fr_situation_init(fr_situation_t *s) {
  if (s == 0) return;
  memset(s, 0, sizeof(*s));
  strcpy(s->outcome_name, "outcome");
  s->outcome_lo = 0.0;
  s->outcome_hi = 0.0;
}

sm_status_t fr_reading(fr_situation_t *s, const char *name, double lo, double hi, int whole,
                       unsigned *idx) {
  unsigned i;
  if (s == 0 || name == 0 || idx == 0) return SM_ERR_NULL_ARGUMENT;
  if (s->n_readings >= FR_MAX_READINGS) return SM_ERR_DOMAIN_TOO_LARGE;
  if (strlen(name) + 1u > SX_NAME || lo > hi) return SM_ERR_BAD_THRESHOLD;
  i = s->n_readings++;
  strcpy(s->reading_name[i], name);
  s->lo[i] = lo;
  s->hi[i] = hi;
  s->whole[i] = whole;
  *idx = i;
  return SM_OK;
}

sm_status_t fr_outcome(fr_situation_t *s, const char *name, double lo, double hi, int whole) {
  if (s == 0 || name == 0) return SM_ERR_NULL_ARGUMENT;
  if (strlen(name) + 1u > SX_NAME || lo > hi) return SM_ERR_BAD_THRESHOLD;
  strcpy(s->outcome_name, name);
  s->outcome_lo = lo;
  s->outcome_hi = hi;
  s->outcome_whole = whole;
  return SM_OK;
}

sm_status_t fr_observe(fr_situation_t *s, const double *readings, double outcome) {
  unsigned i;
  if (s == 0 || readings == 0) return SM_ERR_NULL_ARGUMENT;
  if (s->n_obs >= FR_MAX_OBS) return SM_ERR_DOMAIN_TOO_LARGE;
  for (i = 0u; i < s->n_readings; i++) s->reading[s->n_obs][i] = readings[i];
  s->outcome[s->n_obs] = outcome;
  s->n_obs++;
  return SM_OK;
}

sm_status_t fr_formulate(const fr_situation_t *s, fr_frame_t *out) {
  unsigned surv[FR_MAX_SURV];
  unsigned n_surv = 0u, tried = 0u;
  unsigned i, r, chosen = FR_NONE, rival = FR_NONE;
  int yes_no;
  fr_subs_t subs;

  if (s == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (s->n_readings == 0u || s->n_obs < 2u) return SM_ERR_EMPTY_DOMAIN;

  memset(out, 0, sizeof(*out));
  S = s;
  yes_no = outcome_is_yes_no(s);
  BUDGET = FR_BUDGET;
  draw_probes();
  build_pool(FR_MAX_SIZE);
  if (yes_no) build_bool(FR_MAX_SIZE - 2u, 1);

  if (yes_no) {
    for (i = 0u; i < N_BOOL; i++) {
      tried++;
      if (agrees(BOOL[i], 1) && n_surv < FR_MAX_SURV) surv[n_surv++] = BOOL[i];
    }
  } else {
    for (i = 0u; i < N_POOL; i++) {
      tried++;
      if (agrees(POOL[i], 0) && n_surv < FR_MAX_SURV) surv[n_surv++] = POOL[i];
    }
  }

  for (i = 0u; i < n_surv; i++) {
    if (chosen == FR_NONE || NODE[surv[i]].size < NODE[chosen].size) chosen = surv[i];
  }
  for (i = 0u; i < n_surv; i++) {
    if (surv[i] == chosen) continue;
    if (rival == FR_NONE || NODE[surv[i]].size < NODE[rival].size) rival = surv[i];
  }

  out->capped = (N_POOL >= FR_POOL_CAP || N_BOOL >= FR_BOOL_CAP || BUDGET <= 0);
  out->candidates = tried;
  out->survivors = n_surv;
  out->support = (tried == 0u) ? 0.0 : (double)(tried - n_surv) / (double)tried;
  out->found = (chosen != FR_NONE);

  subs.n = 0u;
  if (chosen != FR_NONE && is_bool_op(NODE[chosen].op)) {
    unsigned kid[2];
    kid[0] = NODE[chosen].a;
    kid[1] = (NODE[chosen].op == FO_NOT) ? FR_NONE : NODE[chosen].b;
    for (i = 0u; i < 2u; i++) {
      const fn_t *k;
      if (kid[i] == FR_NONE) continue;
      k = &NODE[kid[i]];
      if (k->op == FO_CONST || k->op == FO_VAR) continue;
      if (is_bool_op(k->op)) continue;
      if (out->n_defs >= FR_MAX_DEFS) break;
      sprintf(out->def_name[out->n_defs], "q%u", (unsigned)(out->n_defs + 1u));
      write_law(kid[i], out->def_body[out->n_defs], FR_TEXT, 0, 0);
      subs.id[subs.n] = kid[i];
      subs.name[subs.n] = out->def_name[out->n_defs];
      subs.n++;
      out->n_defs++;
    }
  }
  if (chosen != FR_NONE) {
    write_law(chosen, out->raw_law, FR_TEXT, 0, 0);
    write_law(chosen, out->law, FR_TEXT, 0, &subs);
    out->shape = shape_of(chosen);
    if (rival != FR_NONE) {
      write_law(rival, out->rival, FR_TEXT, 0, 0);
      out->has_rival = 1;
    }
  }

  for (i = 0u; i < s->n_readings; i++) {
    out->seen_lo[i] = s->reading[0][i];
    out->seen_hi[i] = s->reading[0][i];
    for (r = 1u; r < s->n_obs; r++) {
      if (s->reading[r][i] < out->seen_lo[i]) out->seen_lo[i] = s->reading[r][i];
      if (s->reading[r][i] > out->seen_hi[i]) out->seen_hi[i] = s->reading[r][i];
    }
  }
  for (i = 0u; i < s->n_readings; i++) {
    int varied_alone = 0;
    unsigned j;
    out->standing[i] = FR_UNTESTED;
    for (r = 0u; r < s->n_obs && out->standing[i] != FR_MATTERS; r++) {
      unsigned t;
      for (t = r + 1u; t < s->n_obs; t++) {
        int only_this = 1;
        for (j = 0u; j < s->n_readings; j++) {
          if (j == i) continue;
          if (s->reading[r][j] != s->reading[t][j]) { only_this = 0; break; }
        }
        if (!only_this || s->reading[r][i] == s->reading[t][i]) continue;
        varied_alone = 1;
        if (s->outcome[r] != s->outcome[t]) {
          out->standing[i] = FR_MATTERS;
          out->witness_a[i] = r;
          out->witness_b[i] = t;
          break;
        }
      }
    }
    if (out->standing[i] != FR_MATTERS && varied_alone) {
      if (chosen == FR_NONE || !uses_var(chosen, i)) out->standing[i] = FR_SILENT;
    }
  }

  out->conjectured = 0;
  for (i = 0u; i < s->n_readings; i++) {
    if (chosen != FR_NONE && !uses_var(chosen, i)) continue;
    if (s->lo[i] < out->seen_lo[i] || s->hi[i] > out->seen_hi[i]) out->conjectured = 1;
  }
  return SM_OK;
}

/* ---- what is always true, with no outcome named at all ------------------- */

#define FR_WORKING 24u

/* Is `text` forced by everything in `keep_list` except the one at `skip`? */
static int follows_from(const fr_situation_t *s, char list[FR_WORKING][FR_TEXT], unsigned n,
                        unsigned skip, const char *text) {
  sx_theory_t T;
  sx_opts_t o = sx_default_opts();
  sx_answer_t A;
  unsigned j, q, v, used = 0u;
  sx_init(&T);
  for (j = 0u; j < s->n_readings; j++) {
    if (sx_var(&T, s->reading_name[j], s->whole[j] ? SX_INT : SX_REAL, s->lo[j], s->hi[j],
               &v) != SM_OK)
      return 0;
  }
  for (j = 0u; j < n; j++) {
    if (j == skip) continue;
    if (list[j][0] == 0) continue;
    if (sx_require(&T, list[j]) == SM_OK) used++;
  }
  if (sx_parse(&T, text, &q) != SM_OK) return 0;
  if (sx_ask(&T, q, &o, &A) != SM_OK) return 0;
  (void)used;
  return A.verdict == SX_TRUE_ALL;
}

sm_status_t fr_invariants(const fr_situation_t *s, fr_invariants_t *out) {
  static char list[FR_WORKING][FR_TEXT];
  unsigned n_list = 0u, i;
  int changed;

  if (s == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (s->n_readings == 0u || s->n_obs < 1u) return SM_ERR_EMPTY_DOMAIN;
  if (s->n_readings > SX_MAX_VARS) return SM_ERR_DOMAIN_TOO_LARGE;

  memset(out, 0, sizeof(*out));
  memset(list, 0, sizeof list);
  S = s;
  BUDGET = FR_BUDGET;
  draw_probes();
  build_pool(FR_MAX_SIZE - 2u);
  build_bool(FR_MAX_SIZE - 4u, 0);
  out->candidates = N_BOOL;

  /* everything that held in every situation it saw */
  for (i = 0u; i < N_BOOL && n_list < FR_WORKING; i++) {
    if (holds_everywhere_seen(BOOL[i])) write_law(BOOL[i], list[n_list++], FR_TEXT, 0, 0);
  }

  /* one true of every imaginable world in these ranges says nothing about
     this one. The engine settles that, so "this is news" is proved. */
  for (i = 0u; i < n_list; i++) {
    if (follows_from(s, list, 0u, 0u, list[i])) list[i][0] = 0;
  }

  /* and one that the others already force is not a second thing it knows.
     Dropping is repeated until nothing more falls, so what is left is a
     set that stands on its own. */
  do {
    changed = 0;
    for (i = 0u; i < n_list; i++) {
      if (list[i][0] == 0) continue;
      if (follows_from(s, list, n_list, i, list[i])) {
        list[i][0] = 0;
        changed = 1;
      }
    }
  } while (changed);

  for (i = 0u; i < n_list && out->n < FR_MAX_INVARIANTS; i++) {
    if (list[i][0] == 0) continue;
    strcpy(out->text[out->n], list[i]);
    out->n++;
  }
  out->kept = out->n;
  return SM_OK;
}

/* ---- a sequence, as a situation ------------------------------------------ */

sm_status_t fr_recurrence(const double *x, unsigned n, double lo, double hi, int whole,
                          fr_situation_t *sit, fr_frame_t *out) {
  unsigned k, idx;
  if (x == 0 || sit == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (n < 4u) return SM_ERR_EMPTY_DOMAIN;
  fr_situation_init(sit);
  fr_reading(sit, "prev", lo, hi, whole, &idx);
  fr_reading(sit, "prev2", lo, hi, whole, &idx);
  fr_reading(sit, "step", 0.0, (double)n, 1, &idx);
  fr_outcome(sit, "next", lo, hi, whole);
  for (k = 2u; k < n; k++) {
    double row[3];
    row[0] = x[k - 1u];
    row[1] = x[k - 2u];
    row[2] = (double)k;
    fr_observe(sit, row, x[k]);
  }
  return fr_formulate(sit, out);
}

int fr_same_shape(const fr_frame_t *a, const fr_frame_t *b) {
  if (a == 0 || b == 0) return 0;
  if (!a->found || !b->found) return 0;
  return a->shape == b->shape;
}

sm_status_t fr_to_theory(const fr_situation_t *s, const fr_frame_t *f, sx_theory_t *th) {
  char text[2u * FR_TEXT];
  unsigned i, idx;
  sm_status_t st;

  if (s == 0 || f == 0 || th == 0) return SM_ERR_NULL_ARGUMENT;
  if (!f->found) return SM_ERR_EMPTY_DOMAIN;
  if (s->n_readings + 1u > SX_MAX_VARS) return SM_ERR_DOMAIN_TOO_LARGE;

  sx_init(th);
  for (i = 0u; i < s->n_readings; i++) {
    st = sx_var(th, s->reading_name[i], s->whole[i] ? SX_INT : SX_REAL, s->lo[i], s->hi[i], &idx);
    if (st != SM_OK) return st;
  }
  st = sx_var(th, s->outcome_name,
              s->outcome_whole ? SX_INT : SX_REAL, s->outcome_lo, s->outcome_hi, &idx);
  if (st != SM_OK) return st;
  for (i = 0u; i < f->n_defs; i++) {
    st = sx_define(th, f->def_name[i], f->def_body[i]);
    if (st != SM_OK) return st;
  }
  sprintf(text, "%s == (%s)", s->outcome_name, f->law);
  return sx_claim(th, text, 0u, f->conjectured);
}

void fr_report(const fr_situation_t *s, const fr_frame_t *f) {
  unsigned i;
  if (s == 0 || f == 0) return;
  printf("  it was given %u readings and %u situations, and told nothing else\n",
         s->n_readings, s->n_obs);
  for (i = 0u; i < s->n_readings; i++) {
    printf("    %-8s ", s->reading_name[i]);
    switch (f->standing[i]) {
      case FR_MATTERS:
        printf("matters: situations %u and %u differ in it alone, and disagree\n",
               f->witness_a[i], f->witness_b[i]);
        break;
      case FR_SILENT:
        printf("silent: changed on its own and nothing followed\n");
        break;
      default:
        printf("untested: never changed on its own, so no verdict\n");
        break;
    }
  }
  if (!f->found) {
    printf("  no description among the %u it could write survived what it saw\n", f->candidates);
    return;
  }
  for (i = 0u; i < f->n_defs; i++) {
    printf("  it named a quantity of its own: %s = %s\n", f->def_name[i], f->def_body[i]);
  }
  printf("  %s is %s\n", s->outcome_name, f->law);
  printf("  %u descriptions were possible, %u survived, support %.4f\n",
         f->candidates, f->survivors, f->support);
  if (f->capped) {
    printf("  it ran out of room before writing every description it could,\n");
    printf("  so that share is over the part it searched, not over all of them\n");
  }
  if (f->has_rival) printf("  another account fits just as well: %s\n", f->rival);
  if (f->conjectured) {
    printf("  used outside what it saw, so it is held as a guess\n");
  } else {
    printf("  it was seen across the whole range it is being used on\n");
  }
}
