/*
 * demo_stem.c -- one reasoning engine, unchanged, across the branches.
 *
 * Every problem below is stated the way it reads: variables with ranges,
 * constraints, a question. The same code answers all of them, by the same
 * operation: eliminating the regions where the answer cannot lie, with
 * every step a guarantee. What differs between branches is only what is
 * written, never how it is reasoned about.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_space.h"

static sx_theory_t T;
static sx_answer_t A;

static void var(const char *name, sx_type_t type, double lo, double hi) {
  unsigned idx;
  if (sx_var(&T, name, type, lo, hi, &idx) != SM_OK) printf("  [could not declare %s]\n", name);
}

static void require(const char *text) {
  if (sx_require(&T, text) != SM_OK) printf("  [constraint not understood: %s: %s]\n", text, T.error);
}

static void world(const sx_box_t *b) {
  unsigned i;
  printf("(");
  for (i = 0u; i < T.n_vars; i++) {
    double x = iv_mid(b->v[i]);
    if (T.type[i] == SX_BOOL) printf("%s%s=%s", i ? ", " : "", T.name[i], x > 0.5 ? "yes" : "no");
    else if (T.type[i] == SX_INT) printf("%s%s=%.0f", i ? ", " : "", T.name[i], x);
    else printf("%s%s=%.4g", i ? ", " : "", T.name[i], x);
  }
  printf(")");
}

static void ask(const char *question) {
  unsigned q;
  sx_opts_t o = sx_default_opts();
  printf("  Q: %s\n", question);
  if (sx_parse(&T, question, &q) != SM_OK) { printf("     [not understood: %s]\n", T.error); return; }
  sx_ask(&T, q, &o, &A);
  printf("     ");
  switch (A.verdict) {
    case SX_TRUE_ALL:
      printf("PROVED TRUE in every world%s", A.has_world ? "" : " (if any world exists)");
      break;
    case SX_FALSE_ALL:
      printf("PROVED FALSE in every world%s", A.has_world ? "" : " (if any world exists)");
      break;
    case SX_BOTH:
      printf("BOTH are possible: true at ");
      world(&A.witness_true);
      printf(", false at ");
      world(&A.witness_false);
      break;
    case SX_NO_WORLDS:
      printf("NO WORLD satisfies the constraints: they contradict each other");
      break;
    case SX_UNRESOLVED:
      printf("UNRESOLVED at this precision (%u regions undecided); not claimed either way", A.boundary);
      break;
    case SX_VALUE:
      printf("= %.10g in every world", A.range.lo);
      break;
    case SX_RANGE:
      printf("lies in [%.6g, %.6g]", A.range.lo, A.range.hi);
      if (A.has_seen) printf("; at real worlds it goes as low as %.6g and as high as %.6g", A.seen_lo, A.seen_hi);
      if (!A.has_world) printf(" (no world proved to exist)");
      break;
  }
  if (A.counted && A.is_bool && A.verdict != SX_NO_WORLDS) {
    printf("\n     counted exactly: true in %.0f of %.0f worlds", A.worlds_true, A.worlds);
  }
  printf("\n     (%u regions eliminated, %u certain, %u splits)\n", A.eliminated, A.certain, A.splits);
}

static void branch(const char *name) {
  printf("\n%s\n", name);
  sx_init(&T);
}

int main(void) {
  printf("ONE ENGINE, EVERY BRANCH\n");
  printf("Nothing below is special-cased. Each problem is variables, constraints\n");
  printf("and a question; each answer comes from eliminating what cannot be.\n");

  branch("NUMBER THEORY");
  var("n", SX_INT, 1, 1000);
  ask("(n*n + n) % 2 == 0");
  ask("n*n % 7 == 3 or n*n % 7 == 5 or n*n % 7 == 6");
  ask("n*n % 7 == 2");

  branch("COMBINATORICS: two dice");
  var("d1", SX_INT, 1, 6);
  var("d2", SX_INT, 1, 6);
  ask("d1 + d2 == 7");
  ask("d1 + d2 >= 11");

  branch("LOGIC: A says \"B is a liar\". B says \"we are both honest or both liars\".");
  var("a", SX_BOOL, 0, 1);
  var("b", SX_BOOL, 0, 1);
  require("a == not b");          /* A tells the truth exactly when B lies */
  require("b == (a == b)");       /* B tells the truth exactly when they match */
  ask("a");
  ask("b");

  branch("CHEMISTRY: balance  a C3H8 + b O2 -> c CO2 + d H2O  (smallest, a = 1)");
  var("a", SX_INT, 1, 10);
  var("b", SX_INT, 1, 20);
  var("c", SX_INT, 1, 20);
  var("d", SX_INT, 1, 20);
  require("3*a == c");            /* carbon */
  require("8*a == 2*d");          /* hydrogen */
  require("2*b == 2*c + d");      /* oxygen */
  require("a == 1");
  ask("b");
  ask("c");
  ask("d");

  branch("ECONOMICS: demand q = 100 - 2p, supply q = 10 + 3p");
  var("p", SX_INT, 0, 100);
  var("q", SX_INT, 0, 200);
  require("q == 100 - 2*p");
  require("q == 10 + 3*p");
  ask("p");
  ask("q");
  ask("p > 20");

  branch("GEOMETRY: a point anywhere in the unit square");
  var("x", SX_REAL, 0, 1);
  var("y", SX_REAL, 0, 1);
  ask("sqrt((x - 0.5)^2 + (y - 0.5)^2) <= 0.7072");
  ask("sqrt((x - 0.5)^2 + (y - 0.5)^2) <= 0.7");
  ask("sqrt((x - 0.5)^2 + (y - 0.5)^2)");

  branch("GEOMETRY: a triangle with sides a, b in [1, 2] and the angle between them in [0.5, 2.5]");
  var("a", SX_REAL, 1, 2);
  var("b", SX_REAL, 1, 2);
  var("C", SX_REAL, 0.5, 2.5);
  ask("sqrt(a^2 + b^2 - 2*a*b*cos(C))");
  ask("sqrt(a^2 + b^2 - 2*a*b*cos(C)) < a + b");

  branch("PHYSICS: a projectile, speed 10 to 20 m/s, angle 0.5 to 1.0 rad");
  var("v", SX_REAL, 10, 20);
  var("t", SX_REAL, 0.5, 1.0);
  ask("v^2 * sin(2*t) / 9.81");
  ask("v^2 * sin(2*t) / 9.81 > 40");
  ask("v^2 * sin(2*t) / 9.81 > 45");

  branch("PHYSICS: dropped from a height h between 1 and 10 m");
  var("h", SX_REAL, 1, 10);
  ask("sqrt(2*9.81*h) <= 14.01");
  require("sqrt(2*9.81*h) > 20");
  ask("h");

  branch("CALCULUS AND ALGEBRA: f(x) = x^3 - 6x^2 + 11x - 6 on [-10, 10]");
  {
    unsigned x, f, df, i;
    sx_roots_t R;
    char buf[160];
    iv_t I;
    sx_var(&T, "x", SX_REAL, -10, 10, &x);
    sx_parse(&T, "x^3 - 6*x^2 + 11*x - 6", &f);
    sx_diff(&T, f, x, &df);
    sx_print(&T, df, buf, sizeof buf);
    printf("  its derivative, worked out symbolically: %s\n", buf);
    sx_roots(&T, f, x, -10, 10, 1e-9, &R);
    printf("  roots, each PROVED to exist and to be the only one in its interval:\n");
    for (i = 0u; i < R.n; i++) printf("    in [%.10f, %.10f]\n", R.root[i].lo, R.root[i].hi);
    printf("  %s\n", R.complete ? "and every other part of [-10, 10] is proved root-free"
                                : "some parts could not be settled");
    sx_roots(&T, df, x, -10, 10, 1e-9, &R);
    printf("  where it turns (roots of the derivative):");
    for (i = 0u; i < R.n; i++) printf(" %.6f", iv_mid(R.root[i]));
    printf("\n");
    printf("  increasing on [3, 10]? %s\n", sx_monotone(&T, f, x, 3, 10) == 1 ? "PROVED" : "not shown");
    printf("  increasing on [1, 3]?  %s\n", sx_monotone(&T, f, x, 1, 3) == 1 ? "PROVED" :
                                           "no: the derivative changes sign there");
    sx_parse(&T, "x^2", &f);
    I = sx_integral(&T, f, x, 0, 1, 20000u);
    printf("  the integral of x^2 from 0 to 1 is certainly in [%.6f, %.6f]\n", I.lo, I.hi);
    sx_parse(&T, "sin(x)", &f);
    I = sx_integral(&T, f, x, 0, 3.141592653589793, 20000u);
    printf("  the integral of sin(x) from 0 to pi is certainly in [%.5f, %.5f]\n", I.lo, I.hi);
  }

  branch("SYSTEMS: where does the circle x^2 + y^2 = 4 meet the parabola y = x^2 - 1?");
  {
    unsigned x, y, f[3], i;
    sx_solutions_t S;
    sx_var(&T, "x", SX_REAL, -3, 3, &x);
    sx_var(&T, "y", SX_REAL, -3, 3, &y);
    sx_parse(&T, "x^2 + y^2 - 4", &f[0]);
    sx_parse(&T, "y - x^2 + 1", &f[1]);
    sx_solve(&T, f, 2u, 1e-9, &S);
    printf("  %u meeting points, each PROVED to be exactly one solution in its box:\n", S.n);
    for (i = 0u; i < S.n; i++) {
      printf("    x in [%.9f, %.9f], y in [%.9f, %.9f]\n", S.solution[i].v[0].lo, S.solution[i].v[0].hi,
             S.solution[i].v[1].lo, S.solution[i].v[1].hi);
    }
    printf("  %s\n", S.complete ? "and none anywhere else in the square, proved" : "some regions unresolved");

    branch("SYSTEMS: three unknowns with x + y + z = 6, xy + yz + zx = 11, xyz = 6");
    sx_var(&T, "x", SX_REAL, 0, 4, &x);
    sx_var(&T, "y", SX_REAL, 0, 4, &y);
    sx_var(&T, "z", SX_REAL, 0, 4, &i);
    sx_parse(&T, "x + y + z - 6", &f[0]);
    sx_parse(&T, "x*y + y*z + z*x - 11", &f[1]);
    sx_parse(&T, "x*y*z - 6", &f[2]);
    sx_solve(&T, f, 3u, 1e-9, &S);
    printf("  %u solutions, each proved unique in its box:", S.n);
    for (i = 0u; i < S.n; i++) {
      printf(" (%.0f, %.0f, %.0f)", iv_mid(S.solution[i].v[0]), iv_mid(S.solution[i].v[1]),
             iv_mid(S.solution[i].v[2]));
    }
    printf("\n  %s\n", S.complete ? "and no others in [0, 4]^3, proved" : "some regions unresolved");
  }

  printf("\nWHAT THIS DOES NOT DO YET\n");
  printf("  A root that touches zero without crossing is reported unresolved, and so\n");
  printf("  is a system whose solutions are not isolated (a whole curve of them).\n");
  printf("  And each problem was written as constraints by hand: reading one from\n");
  printf("  a description of the world is the stage built on this one.\n");
  return 0;
}
