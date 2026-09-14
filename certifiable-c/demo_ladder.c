/*
 * demo_ladder.c -- from grouping objects to addition to multiplication,
 * with nothing handed over.
 *
 * What it is given, in every world: things it can observe directly. How
 * many are in this pile. Are these two piles the same size. How many are
 * there after pushing piles together. It is never given "+" or "x".
 *
 * What it does: notices when one observation is completely determined by
 * others, reads the rule off, proves it, names it, and carries it into the
 * next world. Every concept above the first rests on the one below it.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_concept.h"

#define N1 256u   /* two piles, 0..15 each */
#define N2 216u   /* three piles, 0..5 each */
#define N3 64u    /* k groups of n, 0..7 each */

static void observe(sr_query_t *q, unsigned n, unsigned dom, unsigned (*f)(unsigned)) {
  unsigned s;
  sr_query_init(q, n, dom);
  for (s = 0u; s < n; s++) sr_query_set(q, s, f(s));
}

/* world 1: situation s is piles (s / 16, s % 16) */
static unsigned w1_a(unsigned s) { return s / 16u; }
static unsigned w1_b(unsigned s) { return s % 16u; }
static unsigned w1_merged(unsigned s) { return s / 16u + s % 16u; }
static unsigned w1_same(unsigned s) { return s / 16u == s % 16u; }
static unsigned w1_bigger(unsigned s) { return s / 16u > s % 16u; }

/* world 2: s = 36a + 6b + c */
static unsigned w2_a(unsigned s) { return s / 36u; }
static unsigned w2_b(unsigned s) { return (s / 6u) % 6u; }
static unsigned w2_c(unsigned s) { return s % 6u; }
static unsigned w2_all(unsigned s) { return s / 36u + (s / 6u) % 6u + s % 6u; }

/* world 3: s = 8k + n, k groups of n things */
static unsigned w3_groups(unsigned s) { return s / 8u; }
static unsigned w3_each(unsigned s) { return s % 8u; }
static unsigned w3_total(unsigned s) { return (s / 8u) * (s % 8u); }

static void bar(void) { printf("--------------------------------------------------------------------------\n"); }

static sr_query_t C1[4], M1, C2[4], M2, C3[2], T3;
static sc_discovery_t D1, D2, D3, R;
static sc_op_t COMBINE, REPEAT;

int main(void) {
  unsigned a, b, i, miss, proved = 0u, failed = 0u, beyond = 0u;
  static const char *names1[4] = {"same size?", "first bigger?", "count pile 1", "count pile 2"};
  static const char *names2[4] = {"count pile 1", "count pile 2", "count pile 3", "combine(1, 2)"};

  printf("A LADDER OF CONCEPTS, BUILT FROM PILES OF THINGS\n\n");
  printf("It is never given + or x. It is given what a child can see: how many\n");
  printf("are in a pile, whether two piles match, and how many there are after\n");
  printf("pushing piles together. Everything else it has to find.\n\n");

  /* ---- world 1 ------------------------------------------------------- */
  printf("WORLD 1: two piles of up to 15 things, all 256 ways\n");
  bar();
  observe(&C1[0], N1, 2u, w1_same);
  observe(&C1[1], N1, 2u, w1_bigger);
  observe(&C1[2], N1, 16u, w1_a);
  observe(&C1[3], N1, 16u, w1_b);
  observe(&M1, N1, 31u, w1_merged);

  sc_discover(&C1[2], 1u, &M1, N1, &R);
  printf("  is \"how many after pushing together\" fixed by the first pile alone?\n");
  printf("    no. proof: %u+%u and %u+%u look the same from pile 1 and merge to %u and %u\n",
         w1_a(R.counter_s), w1_b(R.counter_s), w1_a(R.counter_t), w1_b(R.counter_t),
         w1_merged(R.counter_s), w1_merged(R.counter_t));

  sc_discover(C1, 4u, &M1, N1, &D1);
  COMBINE = D1.op;
  strcpy(COMBINE.name, "combine");
  printf("  searching everything it can observe for what fixes it:\n");
  printf("    found: merged = combine(%s, %s)\n", names1[D1.arg[0]], names1[D1.arg[1]]);
  printf("    a rule over %u witnessed pairs, checked on all %u situations: %s\n",
         COMBINE.n_grounded, N1, sc_check(&COMBINE, &C1[2], &C1[3], &M1, N1) ? "holds" : "FAILS");
  printf("    a corner of what it found:\n");
  for (a = 0u; a < 5u; a++) {
    printf("      ");
    for (b = 0u; b < 5u; b++) printf("%4u", sc_value(&COMBINE, a, b));
    printf("\n");
  }
  {
    unsigned e = 0u;
    int has_e = sc_identity(&COMBINE, &e);
    printf("    order does not matter (checked on every pair): %s\n",
           sc_commutative(&COMBINE) ? "yes" : "no");
    printf("    an empty pile changes nothing: %s (e = %u)\n", has_e ? "yes" : "no", e);
  }
  printf("  It has found addition. Nobody named it; it is called \"combine\" here\n");
  printf("  because that is all the learner knows about it: what it does.\n\n");

  /* ---- world 2 ------------------------------------------------------- */
  printf("WORLD 2: three piles of up to 5, all 216 ways\n");
  bar();
  observe(&C2[0], N2, 6u, w2_a);
  observe(&C2[1], N2, 6u, w2_b);
  observe(&C2[2], N2, 6u, w2_c);
  observe(&M2, N2, 16u, w2_all);
  sc_apply(&COMBINE, &C2[0], &C2[1], N2, &C2[3], &miss);
  printf("  it brings combine along and applies it to piles 1 and 2: %s\n",
         miss == 0u ? "every situation is covered" : "some situations are out of reach");
  sc_discover(C2, 3u, &M2, N2, &R);
  printf("  from the three counts alone, taken two at a time: %s\n",
         R.found ? "found" : (R.needs_more ? "not enough; it needs all three at once" : "impossible"));
  sc_discover(C2, 4u, &M2, N2, &D2);
  printf("  with its own concept on the list: all merged = f(%s, %s)\n", names2[D2.arg[0]],
         names2[D2.arg[1]]);
  printf("    checked on all %u situations: %s\n", N2,
         sc_check(&D2.op, &C2[D2.arg[0]], &C2[D2.arg[1]], &M2, N2) ? "holds" : "FAILS");
  printf("    is f just combine again? %s\n",
         sc_same(&D2.op, &COMBINE, D2.arg[0] == 2u, 0, 0) ? "yes: three piles is combine used twice" : "no");
  printf("  It needed nothing new. The old concept was a building block, and\n");
  printf("  what took three things at once became two steps of one idea.\n\n");

  /* ---- world 3 ------------------------------------------------------- */
  printf("WORLD 3: some number of groups, each with the same number of things\n");
  bar();
  observe(&C3[0], N3, 8u, w3_groups);
  observe(&C3[1], N3, 8u, w3_each);
  observe(&T3, N3, 50u, w3_total);
  sc_discover(C3, 2u, &T3, N3, &D3);
  REPEAT = D3.op;
  strcpy(REPEAT.name, "repeat");
  printf("  total = f(groups, each): %s, checked on all %u: %s\n", D3.found ? "found" : "not found",
         N3, sc_check(&REPEAT, &C3[0], &C3[1], &T3, N3) ? "holds" : "FAILS");
  {
    unsigned da = 0u, db = 0u;
    int same = sc_same(&REPEAT, &COMBINE, 0, &da, &db);
    printf("  is it combine? %s", same ? "yes\n" : "no. ");
    if (!same) {
      printf("%u groups of %u is %u, combine(%u, %u) is %u\n", da, db, sc_value(&REPEAT, da, db),
             da, db, sc_value(&COMBINE, da, db));
    }
  }
  printf("  a new concept. Call it \"repeat\". How does it relate to the old one?\n");
  for (i = 0u; i < N3; i++) {
    unsigned k = w3_groups(i), n = w3_each(i), prev;
    if (k == 0u) continue;
    prev = sc_value(&REPEAT, k - 1u, n);
    if (!sc_grounded(&COMBINE, prev, n)) { beyond++; continue; }
    if (sc_value(&COMBINE, prev, n) == sc_value(&REPEAT, k, n)) proved++;
    else failed++;
  }
  printf("    one more group = combine(what it was, one group's worth):\n");
  printf("      holds in %u of the %u cases where combine is known,\n", proved, proved + failed);
  printf("      and %u more need combine on a pile bigger than 15, which it has\n", beyond);
  printf("      never seen. Those are refused, not guessed.\n");
  printf("  It has found multiplication, and found it to be repeated addition,\n");
  printf("  as far as its experience of addition reaches.\n\n");

  printf("WHAT IT CANNOT DO YET\n");
  bar();
  printf("  Its addition stops at 15 + 15 because that is all it has seen. A\n");
  printf("  child gets past that with a LAW (one more in a pile is one more in\n");
  printf("  the total), which covers numbers never seen. Laws over worlds too\n");
  printf("  many to list are the next stage, and the one that leads to summation\n");
  printf("  and, further up, to calculus.\n");
  return 0;
}
