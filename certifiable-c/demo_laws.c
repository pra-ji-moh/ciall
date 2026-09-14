/*
 * demo_laws.c -- from tables of what was seen to laws that reach past it,
 * and every leap said out loud.
 *
 * Everything here starts as observation: adding one thing to a pile,
 * pushing two piles together, laying out groups, building a staircase.
 * The concepts are discovered from those (smarsh_concept.c), then the
 * laws behind them (smarsh_law.c), then the laws are used where nothing
 * was ever seen, and each answer says which laws it took on trust there.
 */

#include <stdio.h>

#include "smarsh_law.h"

static sr_query_t C[2], T;
static sc_discovery_t D;
static sc_op_t ONE, COMBINE, REPEAT, STAIRS;
static lw_book_t BOOK;

/* learn a concept from a world of observations */
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

static void rests_on(uint32_t leaps) {
  unsigned i, first = 1u;
  if (leaps == 0u) { printf("      seen, not inferred\n"); return; }
  printf("      resting on:\n");
  for (i = 0u; i < BOOK.n; i++) {
    if (leaps & ((uint32_t)1 << i)) {
      printf("        - %s\n", BOOK.law[i].said);
      first = 0u;
    }
  }
  (void)first;
}

static void law_line(unsigned i) {
  const lw_law_t *L = &BOOK.law[i];
  static const char *step[4] = {"one more", "%s with the same amount again", "%s with the count so far",
                                "%s with the next count"};
  if (L->arity == 2u) printf("    %s(a, 0) = %s;  each step: ", L->name, L->base == LW_BASE_A ? "a" : "0");
  else printf("    %s(0) = 0;  each step: ", L->name);
  if (L->step == LW_STEP_ONE_MORE) printf("%s", step[0]);
  else printf(step[L->step], BOOK.law[L->step_law].name);
  printf("\n    agrees with all %u witnessed cases", L->checked);
  if (L->check_leaps) printf(" (some checks leaned on an earlier law past what it saw)");
  printf("\n");
}

static void show(const char *what, unsigned law, uint64_t a, uint64_t b) {
  uint64_t v;
  uint32_t leaps;
  lw_eval(&BOOK, law, a, b, &v, &leaps);
  printf("  %s = %llu\n", what, (unsigned long long)v);
  rests_on(leaps);
}

int main(void) {
  unsigned i_comb, i_rep, i_st;
  printf("LAWS: REACHING PAST WHAT WAS SEEN, AND SAYING SO\n\n");

  learn(&ONE, 31u, 1u, 31u, 1u, 32u, id, 0, plus_one);
  learn(&COMBINE, 256u, 2u, 16u, 16u, 31u, hi16, lo16, merged);
  learn(&REPEAT, 64u, 2u, 8u, 8u, 50u, hi8, lo8, laid_out);
  learn(&STAIRS, 11u, 1u, 11u, 1u, 56u, id, 0, blocks);
  printf("What it has seen: one thing added to piles of up to 30; two piles of\n");
  printf("up to 15 pushed together; up to 7 groups of up to 7; staircases of up\n");
  printf("to 10 steps. Each was already discovered as a concept, as a table.\n\n");

  lw_init(&BOOK, &ONE, 31u, "one more is always the next number, past 30 too");
  lw_discover(&BOOK, &COMBINE, "combine", "combining works the same past 15", &i_comb);
  lw_discover(&BOOK, &REPEAT, "repeat", "repeating works the same past 7 groups of 7", &i_rep);
  lw_discover(&BOOK, &STAIRS, "stairs", "staircases grow the same way past 10 steps", &i_st);

  printf("THE LAWS BEHIND THE TABLES\n");
  law_line(i_comb);
  law_line(i_rep);
  law_line(i_st);
  printf("  Addition is one more, again and again. Multiplication is addition,\n");
  printf("  again and again. A staircase is adding the next step. Found, in that\n");
  printf("  order, each resting on the one before.\n\n");

  printf("INSIDE WHAT IT HAS SEEN\n");
  show("combine(7, 8)", i_comb, 7u, 8u);
  show("repeat(6, 7)", i_rep, 6u, 7u);
  printf("\nPAST ANYTHING IT HAS SEEN\n");
  show("combine(40, 70)", i_comb, 40u, 70u);
  show("repeat(12, 13)", i_rep, 12u, 13u);
  show("stairs(100)", i_st, 100u, 0u);

  printf("\nA PATTERN IN THE STAIRS\n");
  {
    /* Two staircases side by side: is that some simple thing it already
       knows? Search every combination of its two-argument laws with
       arguments drawn from {n, one more than n}, and keep what matches on
       every staircase it has seen. Nothing points it at the answer. */
    static const char *arg_name[2] = {"n", "n + 1"};
    unsigned laws[2], op, x, y, tried = 0u, found = 0u;
    uint64_t n, s, lhs, rhs, xv, yv;
    uint32_t l;
    laws[0] = i_comb;
    laws[1] = i_rep;
    printf("  two staircases of n steps side by side: is that something it knows?\n");
    for (op = 0u; op < 2u; op++) {
      for (x = 0u; x < 2u; x++) {
        for (y = 0u; y < 2u; y++) {
          unsigned held = 0u;
          tried++;
          for (n = 0u; n <= 10u; n++) {
            lw_eval(&BOOK, i_st, n, 0u, &s, &l);
            lw_eval(&BOOK, i_comb, s, s, &lhs, &l);
            xv = x ? n + 1u : n;
            yv = y ? n + 1u : n;
            lw_eval(&BOOK, laws[op], xv, yv, &rhs, &l);
            if (lhs == rhs) held++;
          }
          if (held == 11u) {
            found++;
            printf("    matches %s(%s, %s) on all 11 staircases it has seen\n",
                   BOOK.law[laws[op]].name, arg_name[x], arg_name[y]);
          }
        }
      }
    }
    printf("    (%u candidates tried, %u matched)\n", tried, found);
    lw_eval(&BOOK, i_st, 1000u, 0u, &s, &l);
    lw_eval(&BOOK, i_comb, s, s, &lhs, &l);
    lw_eval(&BOOK, i_rep, 1000u, 1001u, &rhs, &l);
    printf("  at 1000 steps, computed the long way: %llu and %llu, %s\n",
           (unsigned long long)lhs, (unsigned long long)rhs, lhs == rhs ? "equal" : "DIFFERENT");
    printf("  That is Gauss's rule, found. It holds everywhere it was checked, and\n");
    printf("  agrees at 1000. It is still NOT proved for every n: that takes proof by\n");
    printf("  induction over the laws themselves, the next stage. Until then it is a\n");
    printf("  conjecture, and called one.\n");
  }
  return 0;
}
