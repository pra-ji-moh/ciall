/*
 * test_space.c -- the described-worlds engine against ground truth that
 * is computed another way.
 *
 * A separate evaluator here works with plain doubles at single points,
 * sharing no code with the interval engine, and random points are drawn
 * inside the ranges. Checked:
 *   intervals     every operation's result holds the value at sample points
 *   propagation   never cuts away a point that satisfies the constraints
 *   verdicts      "true in every world": no sample point satisfies the
 *                 constraints and fails the question (points within a hair
 *                 of a boundary are skipped, where doubles themselves
 *                 cannot say); witnesses really are worlds with the answer
 *                 claimed; "no worlds": no sample point satisfies them;
 *                 and the engine with and without propagation never
 *                 contradict each other
 *   counting      against enumeration of every whole-number world
 *   calculus      derivatives against finite differences, proved roots
 *                 against the known roots, integrals against exact values
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "smarsh_space.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 16180u;
static double unif(void) {
  lcg = lcg * 1103515245u + 12345u;
  return (double)(lcg >> 8) / 16777216.0;
}
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

/* ---- an independent point evaluator ---------------------------------- */

static double PV[SX_MAX_NODES];
static int PDEF[SX_MAX_NODES];

static int point_eval(const sx_theory_t *th, unsigned root, const double *x, double *out, double *margin) {
  unsigned k;
  double m = INFINITY;
  for (k = 0u; k <= root; k++) {
    const sx_node_t *n = &th->node[k];
    double a = PV[n->a], b = PV[n->b], v = 0.0;
    int ok = 1;
    if (n->op != SX_CONST && n->op != SX_VAR && (!PDEF[n->a] || (n->op >= SX_ADD && !PDEF[n->b] &&
        n->op != SX_NEG && n->op != SX_POWI && n->op != SX_SQRT && n->op != SX_EXP && n->op != SX_LOG &&
        n->op != SX_SIN && n->op != SX_COS && n->op != SX_ABS && n->op != SX_NOT))) ok = 0;
    switch (n->op) {
      case SX_CONST: v = iv_mid(n->c); break;
      case SX_VAR: v = x[n->var]; break;
      case SX_ADD: v = a + b; break;
      case SX_SUB: v = a - b; break;
      case SX_MUL: v = a * b; break;
      case SX_DIV: if (b == 0.0) ok = 0; else v = a / b; break;
      case SX_NEG: v = -a; break;
      case SX_POWI: v = pow(a, n->n); break;
      case SX_SQRT: if (a < 0.0) ok = 0; else v = sqrt(a); break;
      case SX_EXP: v = exp(a); break;
      case SX_LOG: if (a <= 0.0) ok = 0; else v = log(a); break;
      case SX_SIN: v = sin(a); break;
      case SX_COS: v = cos(a); break;
      case SX_ABS: v = fabs(a); break;
      case SX_MIN: v = a < b ? a : b; break;
      case SX_MAX: v = a > b ? a : b; break;
      case SX_MOD: v = fmod(a, b); break;
      case SX_LT: v = a < b; if (fabs(a - b) < m) m = fabs(a - b); break;
      case SX_LE: v = a <= b; if (fabs(a - b) < m) m = fabs(a - b); break;
      case SX_EQ: v = a == b; break;
      case SX_NE: v = a != b; break;
      case SX_AND: v = a != 0.0 && b != 0.0; break;
      case SX_OR: v = a != 0.0 || b != 0.0; break;
      case SX_NOT: v = a == 0.0; break;
    }
    PV[k] = v;
    PDEF[k] = ok;
  }
  *out = PV[root];
  if (margin != 0) *margin = m;
  return PDEF[root];
}

static sx_theory_t T;
static sx_answer_t A, B;

/* a random polynomial-ish expression in x and y, as text */
static void rand_poly(char *buf, unsigned cap) {
  static const char *terms[] = {"x", "y", "x^2", "y^2", "x*y", "x^3", "sin(x)", "cos(y)", "sqrt(x*x+1)", "exp(y/3)"};
  unsigned k, n = 2u + rnd(3u);
  size_t used = 0u;
  buf[0] = '\0';
  for (k = 0u; k < n; k++) {
    int c = (int)rnd(7u) - 3;
    used += (size_t)snprintf(buf + used, cap - used, "%s%d*%s", k ? " + " : "", c == 0 ? 1 : c, terms[rnd(10u)]);
  }
}

int main(void) {
  unsigned t, i, k;
  sx_opts_t o = sx_default_opts();
  printf("described worlds, checked against an independent point evaluator\n\n");

  /* ---- intervals hold every sample ---------------------------------- */
  {
    unsigned bad = 0u;
    for (t = 0u; t < 20000u; t++) {
      double a0 = (unif() - 0.5) * 20, a1 = a0 + unif() * 5, b0 = (unif() - 0.5) * 20, b1 = b0 + unif() * 5;
      iv_t A1 = iv_make(a0, a1), B1 = iv_make(b0, b1);
      double x = a0 + (a1 - a0) * unif(), y = b0 + (b1 - b0) * unif();
      if (!(x >= a0 && x <= a1)) continue;
      if (!iv_subset(iv_point(x + y), iv_add(A1, B1))) bad++;
      if (!iv_subset(iv_point(x - y), iv_sub(A1, B1))) bad++;
      if (!iv_subset(iv_point(x * y), iv_mul(A1, B1))) bad++;
      if (!(b0 <= 0.0 && b1 >= 0.0) && !iv_subset(iv_point(x / y), iv_div(A1, B1))) bad++;
      if (x >= 0.0 && !iv_subset(iv_point(sqrt(x)), iv_sqrt(A1))) bad++;
      if (!iv_subset(iv_point(exp(x / 4)), iv_exp(iv_mul(A1, iv_make(0.25, 0.25))))) bad++;
      if (x > 0.0 && !iv_subset(iv_point(log(x)), iv_log(A1))) bad++;
      if (!iv_subset(iv_point(sin(x)), iv_sin(A1))) bad++;
      if (!iv_subset(iv_point(cos(x)), iv_cos(A1))) bad++;
      if (!iv_subset(iv_point(pow(x, 3)), iv_powi(A1, 3))) bad++;
      if (!iv_subset(iv_point(pow(x, 4)), iv_powi(A1, 4))) bad++;
    }
    check("20000 random interval operations: every sampled value is inside the result", bad == 0u);
  }

  /* ---- random constraint problems ------------------------------------- */
  {
    unsigned prop_bad = 0u, true_bad = 0u, witness_bad = 0u, nowhere_bad = 0u, disagree = 0u;
    unsigned proved = 0u, both = 0u, none = 0u, samples = 0u;
    for (t = 0u; t < 120u; t++) {
      char c1[160], c2[160], q[200], cons[200];
      unsigned x, y, qi;
      double lim = (double)(rnd(9u)) - 4.0;
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, -2.0, 2.0, &x);
      sx_var(&T, "y", SX_REAL, -2.0, 2.0, &y);
      rand_poly(c1, sizeof c1);
      rand_poly(c2, sizeof c2);
      snprintf(cons, sizeof cons, "%s <= %g", c1, lim + 2.0);
      if (sx_require(&T, cons) != SM_OK) continue;
      snprintf(q, sizeof q, "%s < %g", c2, lim);
      if (sx_parse(&T, q, &qi) != SM_OK) continue;
      o.rel_eps = 5e-3;
      sx_ask(&T, qi, &o, &A);
      sx_ask_plain(&T, qi, &o, &B);
      /* the two searches may differ in what they resolve, never in what they prove */
      if ((A.verdict == SX_TRUE_ALL && (B.verdict == SX_FALSE_ALL || B.verdict == SX_BOTH)) ||
          (A.verdict == SX_FALSE_ALL && (B.verdict == SX_TRUE_ALL || B.verdict == SX_BOTH)) ||
          (A.verdict == SX_BOTH && (B.verdict == SX_TRUE_ALL || B.verdict == SX_FALSE_ALL)) ||
          ((A.verdict == SX_NO_WORLDS) != (B.verdict == SX_NO_WORLDS) &&
           (A.has_world || B.has_world))) {
        disagree++;
      }
      proved += A.verdict == SX_TRUE_ALL || A.verdict == SX_FALSE_ALL;
      both += A.verdict == SX_BOTH;
      none += A.verdict == SX_NO_WORLDS;
      /* witnesses */
      if (A.has_true || A.has_false) {
        double pt[2], cv, qv;
        const sx_box_t *w = A.has_true ? &A.witness_true : &A.witness_false;
        pt[0] = iv_mid(w->v[0]);
        pt[1] = iv_mid(w->v[1]);
        if (!point_eval(&T, T.con[0], pt, &cv, 0) || cv == 0.0) witness_bad++;
        if (!point_eval(&T, qi, pt, &qv, 0) || (qv != 0.0) != (A.has_true != 0)) witness_bad++;
      }
      /* samples */
      for (k = 0u; k < 400u; k++) {
        double pt[2], cv, qv, mc, mq;
        sx_box_t box = sx_start(&T);
        pt[0] = -2.0 + 4.0 * unif();
        pt[1] = -2.0 + 4.0 * unif();
        if (!point_eval(&T, T.con[0], pt, &cv, &mc) || mc < 1e-7) continue;
        if (cv == 0.0) continue;
        samples++;
        if (A.verdict == SX_NO_WORLDS) nowhere_bad++;
        if (sx_contract(&T, &box) == 0 || pt[0] < box.v[0].lo || pt[0] > box.v[0].hi ||
            pt[1] < box.v[1].lo || pt[1] > box.v[1].hi) {
          prop_bad++;
        }
        if (!point_eval(&T, qi, pt, &qv, &mq) || mq < 1e-7) continue;
        if (A.verdict == SX_TRUE_ALL && qv == 0.0) true_bad++;
        if (A.verdict == SX_FALSE_ALL && qv != 0.0) true_bad++;
      }
    }
    printf("        (%u problems proved one way, %u with witnesses both ways, %u with no worlds;\n"
           "         %u sample worlds checked)\n", proved, both, none, samples);
    check("propagation never cuts away a world that satisfies the constraints", prop_bad == 0u);
    check("no sample world contradicts a proved TRUE or FALSE", true_bad == 0u);
    check("every witness is a real world with the answer it is claimed to have", witness_bad == 0u);
    check("\"no worlds\" is never said when a sample world exists", nowhere_bad == 0u);
    check("with and without propagation, the verdicts never contradict", disagree == 0u);
  }

  /* ---- counting against enumeration --------------------------------- */
  {
    unsigned bad = 0u;
    for (t = 0u; t < 60u; t++) {
      unsigned a, b, c, qi;
      char cons[80], q[80];
      double enum_all = 0, enum_true = 0;
      int m = 2 + (int)rnd(6u), r = (int)rnd(20u);
      sx_init(&T);
      sx_var(&T, "a", SX_INT, 0, 9, &a);
      sx_var(&T, "b", SX_INT, 0, 9, &b);
      sx_var(&T, "c", SX_INT, 0, 4, &c);
      snprintf(cons, sizeof cons, "a + 2*b - c <= %d", r);
      snprintf(q, sizeof q, "(a*b + c) %% %d == 1", m);
      sx_require(&T, cons);
      sx_parse(&T, q, &qi);
      o = sx_default_opts();
      sx_ask(&T, qi, &o, &A);
      for (a = 0u; a <= 9u; a++) for (b = 0u; b <= 9u; b++) for (c = 0u; c <= 4u; c++) {
        if ((int)(a + 2u * b) - (int)c <= r) {
          enum_all++;
          if ((int)((a * b + c) % (unsigned)m) == 1) enum_true++;
        }
      }
      if (!A.counted || A.worlds != enum_all || A.worlds_true != enum_true) bad++;
    }
    check("60 whole-number problems: exact world counts equal full enumeration", bad == 0u);
  }

  /* ---- calculus -------------------------------------------------------- */
  {
    unsigned x, f, df, bad = 0u, roots_bad = 0u;
    static const char *exprs[] = {"x^3 - 2*x", "sin(x)*x", "exp(x/2) - x^2", "sqrt(x*x + 1)",
                                  "cos(x^2)", "log(x*x + 2) / (x*x + 1)", "x^5 - 3*x^3 + x"};
    for (i = 0u; i < 7u; i++) {
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, -3, 3, &x);
      sx_parse(&T, exprs[i], &f);
      if (sx_diff(&T, f, x, &df) != SM_OK) { bad++; continue; }
      for (k = 0u; k < 50u; k++) {
        double p = -3.0 + 6.0 * unif(), h = 1e-5, fp, fm, d, dn;
        double pp = p + h, pm = p - h;
        point_eval(&T, f, &pp, &fp, 0);
        point_eval(&T, f, &pm, &fm, 0);
        point_eval(&T, df, &p, &d, 0);
        dn = (fp - fm) / (2.0 * h);
        if (fabs(d - dn) > 1e-4 * (1.0 + fabs(dn))) bad++;
      }
    }
    check("symbolic derivatives agree with finite differences at 350 random points", bad == 0u);

    for (t = 0u; t < 40u; t++) {
      int r1 = (int)rnd(9u) - 4, r2 = r1 + 1 + (int)rnd(3u), r3 = r2 + 1 + (int)rnd(3u);
      char e[120];
      sx_roots_t R;
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, -10, 10, &x);
      snprintf(e, sizeof e, "(x - %d)*(x - %d)*(x - %d)", r1, r2, r3);
      sx_parse(&T, e, &f);
      sx_roots(&T, f, x, -9.7, 9.9, 1e-9, &R);
      if (R.n != 3u || !R.complete) { roots_bad++; continue; }
      if (!(R.root[0].lo <= r1 && r1 <= R.root[0].hi) || !(R.root[1].lo <= r2 && r2 <= R.root[1].hi) ||
          !(R.root[2].lo <= r3 && r3 <= R.root[2].hi)) {
        roots_bad++;
      }
    }
    check("40 random cubics: exactly the three true roots, each inside its proved interval",
          roots_bad == 0u);

    {
      sx_roots_t R;
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, -5, 5, &x);
      sx_parse(&T, "(x - 1)^2", &f);
      sx_roots(&T, f, x, -5, 5, 1e-6, &R);
      check("a root that touches zero without crossing is reported unresolved, not proved",
            R.n == 0u && R.n_unresolved > 0u && !R.complete);
    }
    {
      iv_t I;
      int ok = 1;
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, 0, 4, &x);
      sx_parse(&T, "x^3", &f);
      I = sx_integral(&T, f, x, 0, 2, 4000u);
      ok &= I.lo <= 4.0 && 4.0 <= I.hi;
      sx_parse(&T, "exp(x)", &f);
      I = sx_integral(&T, f, x, 0, 1, 4000u);
      ok &= I.lo <= exp(1.0) - 1.0 && exp(1.0) - 1.0 <= I.hi;
      sx_parse(&T, "1/(x+1)", &f);
      I = sx_integral(&T, f, x, 0, 3, 4000u);
      ok &= I.lo <= log(4.0) && log(4.0) <= I.hi;
      check("integral bounds hold the exact values of three known integrals", ok);
    }
    {
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, -5, 5, &x);
      sx_parse(&T, "x^3 + x", &f);
      check("x^3 + x proved increasing everywhere; x^2 not proved monotone across 0",
            sx_monotone(&T, f, x, -5, 5) == 1 &&
                (sx_parse(&T, "x^2", &f), sx_monotone(&T, f, x, -1, 1)) == 0);
    }
  }

  /* ---- systems of equations ----------------------------------------------- */
  {
    unsigned bad = 0u, tried = 0u, x, y, f[2];
    sx_solutions_t S;
    for (t = 0u; t < 150u; t++) {
      /* circle x^2 + y^2 = r^2 meets line y = k x + c where
         (1 + k^2) x^2 + 2 k c x + c^2 - r^2 = 0 */
      double r = 1.0 + 2.0 * unif(), kk = 4.0 * unif() - 2.0, c = 6.0 * unif() - 3.0;
      double A2 = 1.0 + kk * kk, B2 = 2.0 * kk * c, C2 = c * c - r * r, disc = B2 * B2 - 4.0 * A2 * C2;
      unsigned want = 0u, found;
      double xs[2];
      char e1[80], e2[80];
      if (fabs(disc) < 1e-3) continue;   /* a tangent line: not an isolated pair of solutions */
      if (disc > 0) {
        xs[0] = (-B2 - sqrt(disc)) / (2.0 * A2);
        xs[1] = (-B2 + sqrt(disc)) / (2.0 * A2);
        want = 2u;
      }
      sx_init(&T);
      sx_var(&T, "x", SX_REAL, -4, 4, &x);
      sx_var(&T, "y", SX_REAL, -4.5, 4.5, &y);
      snprintf(e1, sizeof e1, "x^2 + y^2 - %.17g", r * r);
      snprintf(e2, sizeof e2, "y - %.17g*x - %.17g", kk, c);
      sx_parse(&T, e1, &f[0]);
      sx_parse(&T, e2, &f[1]);
      if (sx_solve(&T, f, 2u, 1e-10, &S) != SM_OK || !S.complete) { bad++; continue; }
      tried++;
      found = S.n;
      if (found != want) { bad++; continue; }
      for (i = 0u; i < found; i++) {
        unsigned hit = 0u;
        for (k = 0u; k < want; k++) {
          double yy = kk * xs[k] + c;
          if (S.solution[i].v[0].lo - 1e-9 <= xs[k] && xs[k] <= S.solution[i].v[0].hi + 1e-9 &&
              S.solution[i].v[1].lo - 1e-9 <= yy && yy <= S.solution[i].v[1].hi + 1e-9) hit = 1u;
        }
        if (!hit) bad++;
      }
    }
    printf("        (%u circle-line systems solved)\n", tried);
    check("every system: exactly the true intersections, each inside its proved box, none missed",
          bad == 0u && tried > 100u);

    sx_init(&T);
    sx_var(&T, "x", SX_REAL, 0, 1, &x);
    sx_var(&T, "y", SX_REAL, 0, 1, &y);
    sx_parse(&T, "x - 1.0000001", &f[0]);
    sx_parse(&T, "y - 0.5", &f[1]);
    sx_solve(&T, f, 2u, 1e-9, &S);
    check("a solution just outside the range is not reported as one inside it", S.n == 0u);
  }

  /* ---- one medium: definitions, provenance, guesses ----------------------- */
  {
    unsigned x, y, q1, q2, src, book;
    uint32_t rests;
    int proved;
    sx_answer_t direct;
    sx_init(&T);
    sx_var(&T, "x", SX_INT, 0, 20, &x);
    sx_var(&T, "y", SX_INT, 0, 20, &y);
    sx_define(&T, "total", "x + y");
    sx_require(&T, "x == 3");
    sx_require(&T, "y == 4");
    sx_parse(&T, "total", &q1);
    sx_parse(&T, "x + y", &q2);
    sx_ask(&T, q1, &o, &A);
    sx_ask(&T, q2, &o, &direct);
    check("a name it defined answers exactly as the expression behind it",
          A.verdict == direct.verdict && A.range.lo == direct.range.lo && A.range.lo == 7.0);

    sx_init(&T);
    sx_var(&T, "x", SX_INT, 0, 20, &x);
    sx_var(&T, "y", SX_INT, 0, 20, &y);
    sx_source(&T, "its own law", &src);
    sx_require(&T, "x == 5");
    sx_claim(&T, "y == x + 1", src, 1);
    sx_parse(&T, "y == 6", &q1);
    sx_ask_grounded(&T, q1, &o, &A, &rests, &proved);
    check("an answer that needs a guess says so, and names it",
          A.verdict == SX_TRUE_ALL && !proved && rests != 0u);
    sx_require(&T, "y == 6");
    sx_ask_grounded(&T, q1, &o, &A, &rests, &proved);
    check("once seen for itself, the same answer is proved and rests on nothing",
          A.verdict == SX_TRUE_ALL && proved);

    sx_init(&T);
    sx_var(&T, "x", SX_INT, 0, 20, &x);
    sx_source(&T, "a book", &book);
    sx_require(&T, "x == 4");
    sx_claim(&T, "x == 9", book, 0);
    sx_parse(&T, "x > 0", &q1);
    sx_ask(&T, q1, &o, &A);
    check("a claim that cannot hold with what it saw leaves no world at all",
          A.verdict == SX_NO_WORLDS);
    sx_distrust(&T, book);
    sx_ask_grounded(&T, q1, &o, &A, &rests, &proved);
    check("catching the source out switches off what it said, and the rest still stands",
          A.verdict == SX_TRUE_ALL && proved);
    sx_claim(&T, "x == 11", book, 0);
    sx_ask(&T, q1, &o, &A);
    check("and a discredited source is not believed again later",
          A.verdict == SX_TRUE_ALL);
  }

  /* ---- parsing ----------------------------------------------------------- */
  {
    unsigned x, r;
    sx_init(&T);
    sx_var(&T, "x", SX_REAL, 0, 1, &x);
    check("an unknown name is refused with a message",
          sx_parse(&T, "x + z", &r) != SM_OK && strstr(T.error, "unknown name") != 0);
    check("a number is refused as a constraint", sx_require(&T, "x + 1") != SM_OK);
    check("\"a == not b\", \"=>\" and precedence parse as written",
          (sx_init(&T), sx_var(&T, "a", SX_BOOL, 0, 1, &x), sx_var(&T, "b", SX_BOOL, 0, 1, &x),
           sx_require(&T, "a == not b") == SM_OK && sx_require(&T, "a => b or not a") == SM_OK));
  }
  check("NULL arguments are checked errors",
        sx_ask(0, 0u, &o, &A) == SM_ERR_NULL_ARGUMENT && sx_parse(&T, 0, &k) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
