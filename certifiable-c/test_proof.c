/*
 * test_proof.c -- smarsh_proof.c checked against things computed another
 * way.
 *
 *   the formulas       against the laws evaluated step by step (lw_eval)
 *   the certificate    must accept each proved formula and reject every
 *                      formula with one coefficient changed
 *   the prover         on 400 random claims built from the laws, its
 *                      verdict must match brute-force comparison on the
 *                      grid, and every counterexample must really differ
 *   the scope          a law whose step multiplies (powers) is reported as
 *                      outside the prover, and claims using it are refused
 */

#include <stdio.h>

#include "smarsh_proof.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 2718u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static sr_query_t C[2], T;
static sc_discovery_t D;
static sc_op_t ONE, ADD, MUL, TRI, POW;
static lw_book_t B;
static pf_book_t P;
static pf_expr_t L, R;

static void learn(sc_op_t *op, unsigned n, unsigned args, unsigned da, unsigned db, unsigned dout,
                  unsigned (*fa)(unsigned), unsigned (*fb)(unsigned), unsigned (*ft)(unsigned)) {
  unsigned s;
  sr_query_init(&C[0], n, da);
  if (args == 2u) sr_query_init(&C[1], n, db);
  sr_query_init(&T, n, dout);
  for (s = 0u; s < n; s++) {
    sr_query_set(&C[0], s, fa(s));
    if (args == 2u) sr_query_set(&C[1], s, fb(s));
    sr_query_set(&T, s, ft(s));
  }
  sc_discover(C, args, &T, n, &D);
  *op = D.op;
}

static unsigned id(unsigned s) { return s; }
static unsigned inc(unsigned s) { return s + 1u; }
static unsigned h8(unsigned s) { return s / 8u; }
static unsigned l8(unsigned s) { return s % 8u; }
static unsigned sum8(unsigned s) { return s / 8u + s % 8u; }
static unsigned prod8(unsigned s) { return (s / 8u) * (s % 8u); }
static unsigned tri(unsigned s) { return s * (s + 1u) / 2u; }
static unsigned h4(unsigned s) { return s / 3u; }
static unsigned l3(unsigned s) { return s % 3u; }
static unsigned pw(unsigned s) { unsigned a = s / 3u, b = s % 3u, r = a, k; for (k = 0u; k < b; k++) r *= a; return r; }

/* a random expression over x and y, from the proved laws; the same random
   choices also build a direct evaluator, so the test does not trust
   pf_normal to evaluate anything */
static unsigned LAWS[4];
static unsigned rand_expr(pf_expr_t *e, unsigned depth) {
  unsigned pick = depth == 0u ? rnd(2u) : rnd(6u);
  if (pick == 0u) return pf_x(e);
  if (pick == 1u) return pf_y(e);
  if (pick == 2u) return pf_const(e, (int64_t)rnd(3u));
  {
    unsigned law = LAWS[rnd(4u)];
    unsigned a = rand_expr(e, depth - 1u);
    unsigned b = B.law[law].arity == 2u ? rand_expr(e, depth - 1u) : 0u;
    return pf_apply(e, law, a, b);
  }
}

static int direct(const pf_expr_t *e, uint64_t x, uint64_t y, uint64_t *out) {
  uint64_t v[PF_MAX_NODES];
  unsigned k;
  uint32_t leaps;
  for (k = 0u; k < e->n; k++) {
    const pf_node_t *n = &e->node[k];
    if (n->kind == PF_X) v[k] = x;
    else if (n->kind == PF_Y) v[k] = y;
    else if (n->kind == PF_CONST) v[k] = (uint64_t)n->value;
    else if (lw_eval(&B, n->law, v[n->arg[0]], B.law[n->law].arity == 2u ? v[n->arg[1]] : 0u, &v[k],
                     &leaps) != SM_OK) {
      return 0;
    }
  }
  *out = v[e->n - 1u];
  return 1;
}

int main(void) {
  unsigned ia, im, it, ip, i, j, t;
  unsigned bad = 0u, rejected = 0u, tampered = 0u;
  printf("proof by induction, checked against direct computation\n\n");

  learn(&ONE, 20u, 1u, 20u, 1u, 21u, id, 0, inc);
  learn(&ADD, 64u, 2u, 8u, 8u, 15u, h8, l8, sum8);
  learn(&MUL, 64u, 2u, 8u, 8u, 50u, h8, l8, prod8);
  learn(&TRI, 10u, 1u, 10u, 1u, 46u, id, 0, tri);
  learn(&POW, 12u, 2u, 4u, 3u, 64u, h4, l3, pw);
  lw_init(&B, &ONE, 20u, "one more");
  lw_discover(&B, &ADD, "add", "add", &ia);
  lw_discover(&B, &MUL, "mul", "mul", &im);
  lw_discover(&B, &TRI, "tri", "tri", &it);
  check("a power law is found (each step repeats the value so far)",
        lw_discover(&B, &POW, "pow", "pow", &ip) == SM_OK && B.law[ip].step == LW_STEP_WITH_A &&
            B.law[ip].step_law == im);
  pf_laws(&B, &P);

  check("add, mul and tri each get a closed form proved by induction",
        P.proved[ia] && P.proved[im] && P.proved[it]);
  check("the power law is reported as outside this prover, not guessed",
        !P.proved[ip] && P.why[ip][0] != '\0');

  for (t = 0u; t < 500u; t++) {
    uint64_t a = rnd(500u), b = rnd(500u), v;
    uint32_t leaps;
    lw_eval(&B, ia, a, b, &v, &leaps);
    if ((uint64_t)pf_value(&P.poly[ia], (int64_t)a, (int64_t)b) != v) bad++;
    lw_eval(&B, im, a % 100u, b % 100u, &v, &leaps);
    if ((uint64_t)pf_value(&P.poly[im], (int64_t)(a % 100u), (int64_t)(b % 100u)) != v) bad++;
    lw_eval(&B, it, a, 0u, &v, &leaps);
    if ((uint64_t)pf_value(&P.poly[it], (int64_t)a, 0) != v) bad++;
  }
  check("every closed form equals the law evaluated step by step, far past what was seen", bad == 0u);

  {
    unsigned laws3[3];
    laws3[0] = ia; laws3[1] = im; laws3[2] = it;
    for (t = 0u; t < 3u; t++) {
      if (!pf_certify(&B, &P, laws3[t], &P.poly[laws3[t]])) rejected++;
      for (i = 0u; i < 4u; i++) for (j = 0u; j < 4u; j++) {
        pf_poly_t wrong = P.poly[laws3[t]];
        wrong.c[i][j] += 1;
        tampered++;
        if (!pf_certify(&B, &P, laws3[t], &wrong)) rejected++;
      }
    }
    check("the certificate accepts every proved formula and rejects every altered one",
          rejected == tampered);
  }

  LAWS[0] = ia; LAWS[1] = im; LAWS[2] = it; LAWS[3] = B.one_more;
  {
    unsigned agree = 0u, proved = 0u, refuted_ok = 0u, refuted = 0u, skipped = 0u;
    for (t = 0u; t < 400u; t++) {
      pf_verdict_t v;
      uint64_t x, y, lv, rv;
      int same = 1, ok = 1;
      pf_clear(&L);
      pf_clear(&R);
      rand_expr(&L, 2u);
      if (rnd(3u) == 0u) R = L; else rand_expr(&R, 2u);
      if (pf_prove(&B, &P, &L, &R, &v) != SM_OK) { skipped++; continue; }
      for (x = 0u; x <= PF_DEG && ok; x++) for (y = 0u; y <= PF_DEG && ok; y++) {
        if (!direct(&L, x, y, &lv) || !direct(&R, x, y, &rv)) { ok = 0; break; }
        if (lv != rv) same = 0;
      }
      if (!ok) { skipped++; continue; }
      if (v.proved == same) agree++;
      proved += (unsigned)v.proved;
      if (v.refuted) {
        refuted++;
        if (direct(&L, (uint64_t)v.cx, (uint64_t)v.cy, &lv) && direct(&R, (uint64_t)v.cx, (uint64_t)v.cy, &rv) &&
            lv != rv) {
          refuted_ok++;
        }
      }
    }
    printf("        (%u decided: %u proved, %u refuted; %u too large for this prover)\n",
           400u - skipped, proved, refuted, skipped);
    check("400 random claims: proved exactly when both sides agree everywhere on the grid",
          agree == 400u - skipped && skipped < 100u);
    check("and every counterexample it gives really does tell the two sides apart",
          refuted_ok == refuted);
  }

  pf_clear(&L);
  pf_clear(&R);
  pf_apply(&L, ip, pf_x(&L), pf_y(&L));
  pf_apply(&R, im, pf_x(&R), pf_y(&R));
  {
    pf_verdict_t v;
    check("a claim that uses the unproved power law is refused, not decided",
          pf_prove(&B, &P, &L, &R, &v) == SM_ERR_EMPTY_DOMAIN);
  }
  check("NULL arguments are checked errors",
        pf_laws(0, &P) == SM_ERR_NULL_ARGUMENT && pf_normal(&B, &P, 0, &P.poly[0]) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
