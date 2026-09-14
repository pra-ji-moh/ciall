/*
 * demo_proof.c -- from "held every time it was checked" to "holds for
 * every number", by induction, with a counterexample when it does not.
 *
 * Same start as demo_laws.c: concepts discovered from observation, laws
 * discovered behind them. Then each law's closed form is derived and
 * PROVED by induction (base case and inductive step, checked exactly),
 * and claims about all numbers are settled: proved, or refuted with the
 * smallest counterexample the search meets.
 */

#include <stdio.h>

#include "smarsh_proof.h"

static sr_query_t C[2], T;
static sc_discovery_t D;
static sc_op_t ONE, COMBINE, REPEAT, STAIRS;
static lw_book_t BOOK;
static pf_book_t PB;
static pf_expr_t L, R;

static void learn(sc_op_t *op, unsigned n, unsigned n_args, unsigned dom_a, unsigned dom_b,
                  unsigned dom_out, unsigned (*arg_a)(unsigned), unsigned (*arg_b)(unsigned),
                  unsigned (*seen)(unsigned)) {
  unsigned s;
  sr_query_init(&C[0], n, dom_a);
  if (n_args == 2u) sr_query_init(&C[1], n, dom_b);
  sr_query_init(&T, n, dom_out);
  for (s = 0u; s < n; s++) {
    sr_query_set(&C[0], s, arg_a(s));
    if (n_args == 2u) sr_query_set(&C[1], s, arg_b(s));
    sr_query_set(&T, s, seen(s));
  }
  sc_discover(C, n_args, &T, n, &D);
  *op = D.op;
}

static unsigned id(unsigned s) { return s; }
static unsigned plus_one(unsigned s) { return s + 1u; }
static unsigned hi16(unsigned s) { return s / 16u; }
static unsigned lo16(unsigned s) { return s % 16u; }
static unsigned merged(unsigned s) { return s / 16u + s % 16u; }
static unsigned hi8(unsigned s) { return s / 8u; }
static unsigned lo8(unsigned s) { return s % 8u; }
static unsigned laid_out(unsigned s) { return (s / 8u) * (s % 8u); }
static unsigned blocks(unsigned s) { return s * (s + 1u) / 2u; }

static int uses_y(void) {
  unsigned i;
  for (i = 0u; i < L.n; i++) if (L.node[i].kind == PF_Y) return 1;
  for (i = 0u; i < R.n; i++) if (R.node[i].kind == PF_Y) return 1;
  return 0;
}

static void claim(const char *words) {
  pf_verdict_t v;
  printf("  %s\n", words);
  if (pf_prove(&BOOK, &PB, &L, &R, &v) != SM_OK) {
    printf("    cannot be decided by this prover\n");
    return;
  }
  if (v.proved) printf("    PROVED for every number: both sides reduce to the same formula\n");
  else if (uses_y()) printf("    FALSE: at x = %lld, y = %lld one side is %lld and the other %lld\n",
                            (long long)v.cx, (long long)v.cy, (long long)v.left, (long long)v.right);
  else printf("    FALSE: at x = %lld one side is %lld and the other %lld\n", (long long)v.cx,
              (long long)v.left, (long long)v.right);
}

int main(void) {
  unsigned ic, ir, is, i;
  char buf[160];

  learn(&ONE, 31u, 1u, 31u, 1u, 32u, id, 0, plus_one);
  learn(&COMBINE, 256u, 2u, 16u, 16u, 31u, hi16, lo16, merged);
  learn(&REPEAT, 64u, 2u, 8u, 8u, 50u, hi8, lo8, laid_out);
  learn(&STAIRS, 11u, 1u, 11u, 1u, 56u, id, 0, blocks);
  lw_init(&BOOK, &ONE, 31u, "one more");
  lw_discover(&BOOK, &COMBINE, "combine", "combine", &ic);
  lw_discover(&BOOK, &REPEAT, "repeat", "repeat", &ir);
  lw_discover(&BOOK, &STAIRS, "stairs", "stairs", &is);
  pf_laws(&BOOK, &PB);

  printf("PROOF BY INDUCTION, OVER LAWS IT FOUND ITSELF\n\n");
  printf("EACH LAW'S FORMULA, AND ITS PROOF\n");
  for (i = 1u; i < BOOK.n; i++) {
    const lw_law_t *law = &BOOK.law[i];
    if (!PB.proved[i]) {
      printf("  %-8s not proved: %s\n", law->name, PB.why[i]);
      continue;
    }
    pf_print(&PB.poly[i], law->arity == 2u ? "a" : "n", "b", buf, sizeof buf);
    printf("  %s%s = %s\n", law->name, law->arity == 2u ? "(a, b)" : "(n)", buf);
    printf("    base case and inductive step both checked as exact identities\n");
  }
  printf("  It never saw a formula. It saw piles, groups and staircases, found\n");
  printf("  the laws, and the formulas are what the laws must be.\n\n");

  printf("CLAIMS ABOUT EVERY NUMBER\n");
  pf_clear(&L); pf_clear(&R);
  pf_apply(&L, ic, pf_x(&L), pf_y(&L));
  pf_apply(&R, ic, pf_y(&R), pf_x(&R));
  claim("combine(x, y) = combine(y, x): order never matters when combining");

  pf_clear(&L); pf_clear(&R);
  pf_apply(&L, ir, pf_x(&L), pf_y(&L));
  pf_apply(&R, ir, pf_y(&R), pf_x(&R));
  claim("repeat(x, y) = repeat(y, x): x groups of y is y groups of x");

  pf_clear(&L); pf_clear(&R);
  {
    unsigned x = pf_x(&L), y = pf_y(&L), yy = pf_apply(&L, ic, y, y);
    pf_apply(&L, ir, x, yy);
  }
  {
    unsigned x = pf_x(&R), y = pf_y(&R), r1 = pf_apply(&R, ir, x, y);
    pf_apply(&R, ic, r1, r1);
  }
  claim("repeat(x, combine(y, y)) = combine(repeat(x, y), repeat(x, y)): doubling distributes");

  pf_clear(&L); pf_clear(&R);
  {
    unsigned x = pf_x(&L), s = pf_apply(&L, is, x, 0u);
    pf_apply(&L, ic, s, s);
  }
  {
    unsigned x = pf_x(&R), nx = pf_apply(&R, BOOK.one_more, x, 0u);
    pf_apply(&R, ir, x, nx);
  }
  claim("combine(stairs(x), stairs(x)) = repeat(x, x + 1): Gauss's rule");

  pf_clear(&L); pf_clear(&R);
  pf_apply(&L, is, pf_x(&L), 0u);
  {
    unsigned x = pf_x(&R);
    pf_apply(&R, ir, x, x);
  }
  claim("stairs(x) = repeat(x, x): a staircase is a square (a guess worth testing)");

  printf("\nWHAT IS AND IS NOT SETTLED\n");
  printf("  Gauss's rule was a conjecture in demo_laws: true everywhere checked.\n");
  printf("  Now it is a theorem about the laws, for every number. What stays a\n");
  printf("  leap is whether real piles keep obeying those laws past what was seen.\n");
  printf("  That is a question about the world, and only looking answers it.\n");
  return 0;
}
