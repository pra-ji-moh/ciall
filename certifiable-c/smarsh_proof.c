/*
 * smarsh_proof.c -- see smarsh_proof.h.
 *
 * Exact integer arithmetic on polynomials in the binomial basis. Every
 * multiplication and addition is overflow-checked; an overflow is
 * reported, never wrapped.
 */

#include "smarsh_proof.h"

#include <stdio.h>
#include <string.h>

#define D1 (PF_DEG + 1u)

/* ---- checked integer arithmetic ------------------------------------- */

static int add_ok(int64_t a, int64_t b, int64_t *r) {
  if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return 0;
  *r = a + b;
  return 1;
}

static int mul_ok(int64_t a, int64_t b, int64_t *r) {
  if (a == 0 || b == 0) { *r = 0; return 1; }
  if (a == INT64_MIN || b == INT64_MIN) return 0;
  if ((a < 0 ? -a : a) > INT64_MAX / (b < 0 ? -b : b)) return 0;
  *r = a * b;
  return 1;
}

static int64_t binom(int64_t n, unsigned k) {
  int64_t r = 1;
  unsigned t;
  if (n < 0 || (int64_t)k > n) return 0;
  for (t = 1u; t <= k; t++) r = r * (n - (int64_t)k + (int64_t)t) / (int64_t)t;
  return r;
}

static int64_t fact(unsigned n) {
  int64_t r = 1;
  unsigned t;
  for (t = 2u; t <= n; t++) r *= (int64_t)t;
  return r;
}

/* ---- polynomial operations ------------------------------------------ */

static void p_zero(pf_poly_t *p) { memset(p, 0, sizeof *p); }

static int p_is_zero(const pf_poly_t *p) {
  unsigned i, j;
  for (i = 0u; i < D1; i++) for (j = 0u; j < D1; j++) if (p->c[i][j] != 0) return 0;
  return 1;
}

static int p_add(const pf_poly_t *a, const pf_poly_t *b, int64_t sign, pf_poly_t *r) {
  unsigned i, j;
  pf_poly_t t;
  for (i = 0u; i < D1; i++) {
    for (j = 0u; j < D1; j++) {
      int64_t v;
      if (!mul_ok(sign, b->c[i][j], &v) || !add_ok(a->c[i][j], v, &t.c[i][j])) return 0;
    }
  }
  *r = t;
  return 1;
}

/* C(n, i) * C(n, j) = sum over k of C(k, i) * C(i, k - j) * C(n, k) */
static int64_t mcoef(unsigned i, unsigned j, unsigned k) {
  if (k < i || k < j || k > i + j) return 0;
  return binom((int64_t)k, i) * binom((int64_t)i, k - j);
}

static int p_mul(const pf_poly_t *a, const pf_poly_t *b, pf_poly_t *r) {
  unsigned i1, j1, i2, j2, kx, ky;
  pf_poly_t t;
  p_zero(&t);
  for (i1 = 0u; i1 < D1; i1++) for (j1 = 0u; j1 < D1; j1++) {
    if (a->c[i1][j1] == 0) continue;
    for (i2 = 0u; i2 < D1; i2++) for (j2 = 0u; j2 < D1; j2++) {
      int64_t ab;
      if (b->c[i2][j2] == 0) continue;
      if (!mul_ok(a->c[i1][j1], b->c[i2][j2], &ab)) return 0;
      for (kx = 0u; kx <= i1 + i2; kx++) {
        int64_t mx = mcoef(i1, i2, kx);
        if (mx == 0) continue;
        for (ky = 0u; ky <= j1 + j2; ky++) {
          int64_t my = mcoef(j1, j2, ky), v;
          if (my == 0) continue;
          if (kx >= D1 || ky >= D1) return 0;   /* degree past PF_DEG */
          if (!mul_ok(ab, mx, &v) || !mul_ok(v, my, &v) || !add_ok(t.c[kx][ky], v, &t.c[kx][ky])) {
            return 0;
          }
        }
      }
    }
  }
  *r = t;
  return 1;
}

/* p(x, y + 1): C(y + 1, j) = C(y, j) + C(y, j - 1) */
static int p_shift_y(const pf_poly_t *p, pf_poly_t *r) {
  unsigned i, j;
  pf_poly_t t;
  for (i = 0u; i < D1; i++) {
    for (j = 0u; j < D1; j++) {
      int64_t next = j + 1u < D1 ? p->c[i][j + 1u] : 0;
      if (!add_ok(p->c[i][j], next, &t.c[i][j])) return 0;
    }
  }
  *r = t;
  return 1;
}

/* sum over k < y of q(x, k): C(k, j) sums to C(y, j + 1) */
static int p_sum_y(const pf_poly_t *q, pf_poly_t *r) {
  unsigned i, j;
  pf_poly_t t;
  p_zero(&t);
  for (i = 0u; i < D1; i++) {
    if (q->c[i][PF_DEG] != 0) return 0;
    for (j = 0u; j < PF_DEG; j++) t.c[i][j + 1u] = q->c[i][j];
  }
  *r = t;
  return 1;
}

static void p_transpose(const pf_poly_t *p, pf_poly_t *r) {
  unsigned i, j;
  pf_poly_t t;
  for (i = 0u; i < D1; i++) for (j = 0u; j < D1; j++) t.c[i][j] = p->c[j][i];
  *r = t;
}

/* C(E, i) for a polynomial E: E(E-1)...(E-i+1) / i!, divided exactly */
static int p_binom(const pf_poly_t *e, unsigned i, pf_poly_t *r) {
  unsigned t, a, b;
  pf_poly_t acc, f;
  int64_t d = fact(i);
  p_zero(&acc);
  acc.c[0][0] = 1;
  for (t = 0u; t < i; t++) {
    f = *e;
    if (!add_ok(f.c[0][0], -(int64_t)t, &f.c[0][0])) return 0;
    if (!p_mul(&acc, &f, &acc)) return 0;
  }
  for (a = 0u; a < D1; a++) {
    for (b = 0u; b < D1; b++) {
      if (acc.c[a][b] % d != 0) return 0;   /* cannot happen for whole-number polynomials */
      acc.c[a][b] /= d;
    }
  }
  *r = acc;
  return 1;
}

int64_t pf_value(const pf_poly_t *p, int64_t x, int64_t y) {
  unsigned i, j;
  int64_t s = 0;
  for (i = 0u; i < D1; i++) {
    for (j = 0u; j < D1; j++) {
      if (p->c[i][j] != 0) s += p->c[i][j] * binom(x, i) * binom(y, j);
    }
  }
  return s;
}

/* ---- the laws' closed forms, proved by induction --------------------- */

/* What one step of law L adds (q) and where it starts (base), from the
   closed forms of the laws it is built on. 0 with *why when there is none. */
static int step_and_base(const lw_book_t *laws, const pf_book_t *pb, unsigned L,
                         pf_poly_t *q, pf_poly_t *base, char *why, unsigned why_cap) {
  const lw_law_t *law = &laws->law[L];
  unsigned i, j;
  p_zero(q);
  p_zero(base);
  if (law->step == LW_STEP_ONE_MORE) {
    q->c[0][0] = 1;
  } else {
    const pf_poly_t *s = &pb->poly[law->step_law];
    if (!pb->proved[law->step_law]) {
      snprintf(why, why_cap, "rests on \"%s\", which is not proved", laws->law[law->step_law].name);
      return 0;
    }
    /* the step law must be "value so far + something": C(x,1) + g(y) */
    for (i = 1u; i < D1; i++) for (j = 0u; j < D1; j++) {
      int64_t want = (i == 1u && j == 0u) ? 1 : 0;
      if (s->c[i][j] != want) {
        snprintf(why, why_cap, "each step is not an addition: no polynomial form");
        return 0;
      }
    }
    for (j = 0u; j < D1; j++) {
      if (law->step == LW_STEP_WITH_A) q->c[j][0] = s->c[0][j];
      else q->c[0][j] = s->c[0][j];
    }
    if (law->step == LW_STEP_WITH_NEXT && !p_shift_y(q, q)) {
      snprintf(why, why_cap, "too large to handle");
      return 0;
    }
  }
  if (law->base == LW_BASE_A) base->c[1][0] = 1;
  return 1;
}

int pf_certify(const lw_book_t *laws, const pf_book_t *pb, unsigned L, const pf_poly_t *formula) {
  pf_poly_t q, base, P, shifted, diff;
  char why[64];
  unsigned i;
  if (laws == 0 || pb == 0 || formula == 0 || L >= laws->n || laws->law[L].kind != LW_RECURSIVE) {
    return 0;
  }
  if (!step_and_base(laws, pb, L, &q, &base, why, sizeof why)) return 0;
  P = *formula;
  if (laws->law[L].arity == 1u) p_transpose(&P, &P);   /* the recursion runs in y */
  /* Base case: at y = 0 only the C(y, 0) column survives, and it must be
     the base. Inductive step: P(x, y + 1) - P(x, y) must be exactly q. */
  for (i = 0u; i < D1; i++) if (P.c[i][0] != base.c[i][0]) return 0;
  if (!p_shift_y(&P, &shifted) || !p_add(&shifted, &P, -1, &diff) || !p_add(&diff, &q, -1, &diff)) {
    return 0;
  }
  return p_is_zero(&diff);
}

sm_status_t pf_laws(const lw_book_t *laws, pf_book_t *out) {
  unsigned L;
  if (laws == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  memset(out, 0, sizeof *out);
  out->n = laws->n;
  /* one more: x + 1. That is the counting sequence itself, the one thing
     assumed rather than found. */
  out->poly[laws->one_more].c[1][0] = 1;
  out->poly[laws->one_more].c[0][0] = 1;
  out->proved[laws->one_more] = 1;

  for (L = 0u; L < LW_MAX_LAWS; L++) {
    const lw_law_t *law;
    pf_poly_t q, base, sum, P;
    if (L >= laws->n) break;
    law = &laws->law[L];
    if (law->kind != LW_RECURSIVE) continue;
    if (!step_and_base(laws, out, L, &q, &base, out->why[L], sizeof out->why[L])) continue;

    /* the closed form: base plus the sum of every step */
    if (!p_sum_y(&q, &sum) || !p_add(&base, &sum, 1, &P)) {
      snprintf(out->why[L], sizeof out->why[L], "too large to handle");
      continue;
    }
    if (law->arity == 1u) p_transpose(&P, &P);   /* one-argument laws live in x */

    /* THE PROOF, checked by pf_certify, independently of the derivation */
    if (!pf_certify(laws, out, L, &P)) {
      snprintf(out->why[L], sizeof out->why[L], "the induction does not go through");
      continue;
    }

    /* and it must agree with everything that was seen */
    if (law->table != 0) {
      unsigned a, b, amax = law->table->dom_a;
      unsigned bmax = law->arity == 2u ? law->table->dom_b : 1u;
      int agrees = 1;
      for (a = 0u; a < SR_MAX_ANSWERS && a < amax; a++) {
        for (b = 0u; b < SR_MAX_ANSWERS && b < bmax; b++) {
          if (!sc_grounded(law->table, a, b)) continue;
          if (pf_value(&P, (int64_t)a, (int64_t)b) != (int64_t)sc_value(law->table, a, b)) agrees = 0;
        }
      }
      if (!agrees) {
        snprintf(out->why[L], sizeof out->why[L], "the formula disagrees with what was seen");
        continue;
      }
    }
    out->poly[L] = P;
    out->proved[L] = 1;
  }
  return SM_OK;
}

/* ---- expressions ----------------------------------------------------- */

void pf_clear(pf_expr_t *e) { if (e != 0) e->n = 0u; }

static unsigned push(pf_expr_t *e, pf_node_t n) {
  if (e->n >= PF_MAX_NODES) return PF_MAX_NODES;
  e->node[e->n] = n;
  return e->n++;
}

unsigned pf_x(pf_expr_t *e) { pf_node_t n = {PF_X, 0, 0u, {0u, 0u}}; return push(e, n); }
unsigned pf_y(pf_expr_t *e) { pf_node_t n = {PF_Y, 0, 0u, {0u, 0u}}; return push(e, n); }
unsigned pf_const(pf_expr_t *e, int64_t v) { pf_node_t n = {PF_CONST, 0, 0u, {0u, 0u}}; n.value = v; return push(e, n); }
unsigned pf_apply(pf_expr_t *e, unsigned law, unsigned a, unsigned b) {
  pf_node_t n = {PF_APPLY, 0, 0u, {0u, 0u}};
  n.law = law;
  n.arg[0] = a;
  n.arg[1] = b;
  return push(e, n);
}

sm_status_t pf_normal(const lw_book_t *laws, const pf_book_t *pb, const pf_expr_t *e,
                      pf_poly_t *out) {
  pf_poly_t val[PF_MAX_NODES];
  unsigned k, i, j;
  if (laws == 0 || pb == 0 || e == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (e->n == 0u || e->n > PF_MAX_NODES) return SM_ERR_EMPTY_DOMAIN;
  for (k = 0u; k < PF_MAX_NODES; k++) {
    const pf_node_t *n;
    if (k >= e->n) break;
    n = &e->node[k];
    p_zero(&val[k]);
    if (n->kind == PF_X) { val[k].c[1][0] = 1; continue; }
    if (n->kind == PF_Y) { val[k].c[0][1] = 1; continue; }
    if (n->kind == PF_CONST) {
      if (n->value < 0) return SM_ERR_INDEX_OUT_OF_DOMAIN;
      val[k].c[0][0] = n->value;
      continue;
    }
    if (n->law >= laws->n || n->arg[0] >= k || (laws->law[n->law].arity == 2u && n->arg[1] >= k)) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
    if (!pb->proved[n->law]) return SM_ERR_EMPTY_DOMAIN;
    /* substitute the arguments into the law's closed form */
    for (i = 0u; i < D1; i++) {
      pf_poly_t bx;
      if (laws->law[n->law].arity == 1u) {
        if (pb->poly[n->law].c[i][0] == 0) continue;
        if (!p_binom(&val[n->arg[0]], i, &bx)) return SM_ERR_DOMAIN_TOO_LARGE;
        {
          pf_poly_t term;
          p_zero(&term);
          term.c[0][0] = pb->poly[n->law].c[i][0];
          if (!p_mul(&term, &bx, &term) || !p_add(&val[k], &term, 1, &val[k])) return SM_ERR_DOMAIN_TOO_LARGE;
        }
        continue;
      }
      for (j = 0u; j < D1; j++) {
        pf_poly_t by, term;
        if (pb->poly[n->law].c[i][j] == 0) continue;
        if (!p_binom(&val[n->arg[0]], i, &bx) || !p_binom(&val[n->arg[1]], j, &by)) {
          return SM_ERR_DOMAIN_TOO_LARGE;
        }
        p_zero(&term);
        term.c[0][0] = pb->poly[n->law].c[i][j];
        if (!p_mul(&term, &bx, &term) || !p_mul(&term, &by, &term) ||
            !p_add(&val[k], &term, 1, &val[k])) {
          return SM_ERR_DOMAIN_TOO_LARGE;
        }
      }
    }
  }
  *out = val[e->n - 1u];
  return SM_OK;
}

sm_status_t pf_prove(const lw_book_t *laws, const pf_book_t *pb, const pf_expr_t *lhs,
                     const pf_expr_t *rhs, pf_verdict_t *out) {
  pf_poly_t l, r, d;
  sm_status_t st;
  int64_t x, y;
  if (laws == 0 || pb == 0 || lhs == 0 || rhs == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  memset(out, 0, sizeof *out);
  st = pf_normal(laws, pb, lhs, &l);
  if (st != SM_OK) return st;
  st = pf_normal(laws, pb, rhs, &r);
  if (st != SM_OK) return st;
  if (!p_add(&l, &r, -1, &d)) return SM_ERR_DOMAIN_TOO_LARGE;
  if (p_is_zero(&d)) {
    out->proved = 1;
    return SM_OK;
  }
  /* A nonzero polynomial of degree at most PF_DEG in each variable cannot
     vanish on the whole grid 0..PF_DEG x 0..PF_DEG (its binomial-basis
     coefficients are its finite differences there), so this search always
     finds a counterexample. */
  for (x = 0; x <= (int64_t)PF_DEG; x++) {
    for (y = 0; y <= (int64_t)PF_DEG; y++) {
      if (pf_value(&d, x, y) != 0) {
        out->refuted = 1;
        out->cx = x;
        out->cy = y;
        out->left = pf_value(&l, x, y);
        out->right = pf_value(&r, x, y);
        return SM_OK;
      }
    }
  }
  return SM_ERR_INTERNAL_INVARIANT;
}

/* ---- display --------------------------------------------------------- */

static int64_t gcd64(int64_t a, int64_t b) {
  if (a < 0) a = -a;
  if (b < 0) b = -b;
  while (b != 0) { int64_t t = a % b; a = b; b = t; }
  return a;
}

/* falling factorial x(x-1)...(x-k+1) in ordinary powers */
static void falling(unsigned k, int64_t *coef) {
  unsigned t, m;
  for (m = 0u; m < D1; m++) coef[m] = 0;
  coef[0] = 1;
  for (t = 0u; t < k; t++) {
    for (m = k; m > 0u; m--) coef[m] = coef[m - 1u] - (int64_t)t * coef[m];
    coef[0] = -(int64_t)t * coef[0];
  }
}

void pf_print(const pf_poly_t *p, const char *xn, const char *yn, char *buf, unsigned cap) {
  int64_t num[D1][D1], fx[D1], fy[D1], den = 1, g = 0;
  unsigned i, j, m, l, maxi = 0u, maxj = 0u, used = 0u, terms = 0u;
  int deg;
  memset(num, 0, sizeof num);
  for (i = 0u; i < D1; i++) for (j = 0u; j < D1; j++) {
    if (p->c[i][j] != 0) { if (i > maxi) maxi = i; if (j > maxj) maxj = j; }
  }
  den = fact(maxi) * fact(maxj);
  for (i = 0u; i < D1; i++) for (j = 0u; j < D1; j++) {
    int64_t scale;
    if (p->c[i][j] == 0) continue;
    falling(i, fx);
    falling(j, fy);
    scale = p->c[i][j] * (den / (fact(i) * fact(j)));
    for (m = 0u; m <= i; m++) for (l = 0u; l <= j; l++) num[m][l] += scale * fx[m] * fy[l];
  }
  for (m = 0u; m < D1; m++) for (l = 0u; l < D1; l++) g = gcd64(g, num[m][l]);
  g = gcd64(g, den);
  if (g > 1) {
    den /= g;
    for (m = 0u; m < D1; m++) for (l = 0u; l < D1; l++) num[m][l] /= g;
  }
  buf[0] = '\0';
  if (den != 1) used += (unsigned)snprintf(buf + used, cap - used, "(");
  for (deg = 2 * (int)PF_DEG; deg >= 0; deg--) {
    for (m = D1; m-- > 0u;) {
      int64_t c;
      if ((int)m > deg || deg - (int)m >= (int)D1) continue;
      l = (unsigned)(deg - (int)m);
      c = num[m][l];
      if (c == 0) continue;
      if (used + 32u >= cap) return;
      if (terms > 0u) used += (unsigned)snprintf(buf + used, cap - used, c < 0 ? " - " : " + ");
      else if (c < 0) used += (unsigned)snprintf(buf + used, cap - used, "-");
      if (c < 0) c = -c;
      if (c != 1 || (m == 0u && l == 0u)) used += (unsigned)snprintf(buf + used, cap - used, "%lld", (long long)c);
      if (m > 0u) used += (unsigned)snprintf(buf + used, cap - used, "%s%s", c != 1 ? "*" : "", xn);
      if (m > 1u) used += (unsigned)snprintf(buf + used, cap - used, "^%u", m);
      if (l > 0u) used += (unsigned)snprintf(buf + used, cap - used, "%s%s", (m > 0u || c != 1) ? "*" : "", yn);
      if (l > 1u) used += (unsigned)snprintf(buf + used, cap - used, "^%u", l);
      terms++;
    }
  }
  if (terms == 0u) used += (unsigned)snprintf(buf + used, cap - used, "0");
  if (den != 1) (void)snprintf(buf + used, cap - used, ")/%lld", (long long)den);
}
