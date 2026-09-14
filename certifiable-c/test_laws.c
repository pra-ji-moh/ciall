/*
 * test_laws.c -- smarsh_law.c against arithmetic done directly in C.
 *
 * The laws are discovered from tables, then evaluated far outside them and
 * compared with a + b, a * b and n(n+1)/2 computed by the machine. The
 * leap labels are checked to be exact: empty inside what was seen, and
 * naming exactly the laws used outside it. And a table no law fits must
 * be refused, not forced into a form.
 */

#include <stdio.h>

#include "smarsh_law.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 31337u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static sr_query_t C[2], T;
static sc_discovery_t D;
static sc_op_t ONE, ADD, MUL, TRI, SQ, BAD;
static lw_book_t B;

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
static unsigned wrong_inc(unsigned s) { return s == 5u ? 7u : s + 1u; }
static unsigned h8(unsigned s) { return s / 8u; }
static unsigned l8(unsigned s) { return s % 8u; }
static unsigned sum8(unsigned s) { return s / 8u + s % 8u; }
static unsigned prod8(unsigned s) { return (s / 8u) * (s % 8u); }
static unsigned tri(unsigned s) { return s * (s + 1u) / 2u; }
static unsigned sq(unsigned s) { return s * s; }
static unsigned xr(unsigned s) { return (s / 8u) ^ (s % 8u); }

int main(void) {
  unsigned ia, im, it, t, bad = 0u, idx;
  uint64_t v;
  uint32_t leaps;

  printf("laws, checked against arithmetic done directly\n\n");

  learn(&ONE, 20u, 1u, 20u, 1u, 21u, id, 0, inc);
  learn(&ADD, 64u, 2u, 8u, 8u, 15u, h8, l8, sum8);
  learn(&MUL, 64u, 2u, 8u, 8u, 50u, h8, l8, prod8);
  learn(&TRI, 10u, 1u, 10u, 1u, 46u, id, 0, tri);
  learn(&SQ, 8u, 1u, 8u, 1u, 50u, id, 0, sq);
  learn(&BAD, 64u, 2u, 8u, 8u, 8u, h8, l8, xr);

  check("the primitive is accepted when it really is counting on",
        lw_init(&B, &ONE, 20u, "one more") == SM_OK);
  check("addition's law: start at a, each step one more",
        lw_discover(&B, &ADD, "add", "add", &ia) == SM_OK && B.law[ia].base == LW_BASE_A &&
            B.law[ia].step == LW_STEP_ONE_MORE);
  check("multiplication's law: start at 0, each step add a again",
        lw_discover(&B, &MUL, "mul", "mul", &im) == SM_OK && B.law[im].base == LW_BASE_ZERO &&
            B.law[im].step == LW_STEP_WITH_A && B.law[im].step_law == ia);
  check("the staircase's law: start at 0, each step add the next count",
        lw_discover(&B, &TRI, "tri", "tri", &it) == SM_OK && B.law[it].step == LW_STEP_WITH_NEXT &&
            B.law[it].step_law == ia);
  check("squares fit none of these forms, and are refused rather than forced",
        lw_discover(&B, &SQ, "sq", "sq", &idx) == SM_ERR_EMPTY_DOMAIN);
  check("so is a table with no recursive shape at all",
        lw_discover(&B, &BAD, "xor", "xor", &idx) == SM_ERR_EMPTY_DOMAIN);

  for (t = 0u; t < 2000u; t++) {
    uint64_t a = rnd(1000u), b = rnd(1000u), n = rnd(3000u);
    if (lw_eval(&B, ia, a, b, &v, &leaps) != SM_OK || v != a + b) bad++;
    if (t < 300u) {
      uint64_t x = rnd(200u), y = rnd(200u);
      if (lw_eval(&B, im, x, y, &v, &leaps) != SM_OK || v != x * y) bad++;
    }
    if (lw_eval(&B, it, n, 0u, &v, &leaps) != SM_OK || v != n * (n + 1u) / 2u) bad++;
  }
  check("far outside what was seen, every value equals the machine's own arithmetic",
        bad == 0u);

  lw_eval(&B, ia, 3u, 4u, &v, &leaps);
  check("inside what was seen, nothing is taken on trust", v == 7u && leaps == 0u);
  lw_eval(&B, ia, 40u, 70u, &v, &leaps);
  check("40 + 70 rests on exactly two leaps: one more past 19, and add past 7",
        v == 110u && leaps == ((1u << B.one_more) | (1u << ia)));
  lw_eval(&B, im, 3u, 4u, &v, &leaps);
  check("3 x 4 was seen, so it rests on nothing", v == 12u && leaps == 0u);
  lw_eval(&B, im, 9u, 2u, &v, &leaps);
  check("9 x 2 was not: it rests on the multiplication law, and on add past 7",
        v == 18u && (leaps & (1u << im)) && (leaps & (1u << ia)));

  check("a computation too long to finish is refused, not truncated",
        lw_eval(&B, ia, 1u, LW_MAX_STEPS + 1u, &v, &leaps) == SM_ERR_DOMAIN_TOO_LARGE);

  {
    lw_book_t other;
    learn(&ONE, 20u, 1u, 20u, 1u, 21u, id, 0, wrong_inc);
    check("a \"one more\" that skips a number is rejected as the ground",
          lw_init(&other, &ONE, 20u, "x") == SM_ERR_INDEX_OUT_OF_DOMAIN);
  }
  check("NULL arguments are checked errors",
        lw_eval(0, 0u, 1u, 1u, &v, &leaps) == SM_ERR_NULL_ARGUMENT &&
            lw_discover(&B, 0, "x", "x", &idx) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
