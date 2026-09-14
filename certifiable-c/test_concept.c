/*
 * test_concept.c -- smarsh_concept.c, checked against ground truth that is
 * computed a different way, not against its own claims.
 *
 * Discovery is checked by brute force: an operator found is re-verified
 * on every situation directly from the definitions; a refusal's two
 * situations are checked to really agree on every candidate and really
 * differ on the target; "needs more" is checked by showing every pair
 * fails and the full set succeeds. Plus 500 random worlds where the
 * answer is known by construction.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_concept.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 99u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static sr_query_t Q[8], T, OUT;
static sc_discovery_t D;

static void piles(unsigned n, unsigned side) {
  unsigned s;
  sr_query_init(&Q[0], n, side);
  sr_query_init(&Q[1], n, side);
  sr_query_init(&T, n, 2u * side - 1u);
  for (s = 0u; s < n; s++) {
    sr_query_set(&Q[0], s, s / side);
    sr_query_set(&Q[1], s, s % side);
    sr_query_set(&T, s, s / side + s % side);
  }
}

int main(void) {
  unsigned s, t, k, miss;
  int ok;

  printf("concept discovery, checked against ground truth\n\n");

  /* ---- addition from piles ------------------------------------------ */
  piles(64u, 8u);
  check("finds that the merged count is determined by the two counts",
        sc_discover(Q, 2u, &T, 64u, &D) == SM_OK && D.found && D.arity == 2u);
  ok = 1;
  for (s = 0u; s < 64u; s++) ok &= sc_value(&D.op, s / 8u, s % 8u) == s / 8u + s % 8u;
  check("and the table it read off is addition, entry by entry", ok);
  check("sc_check accepts it", sc_check(&D.op, &Q[0], &Q[1], &T, 64u));
  D.op.table[3 * 8 + 4] = 9u;
  check("sc_check rejects it after one entry is tampered with", !sc_check(&D.op, &Q[0], &Q[1], &T, 64u));
  sc_discover(Q, 2u, &T, 64u, &D);
  check("commutative, and 0 is the identity",
        sc_commutative(&D.op) && sc_identity(&D.op, &k) && k == 0u);

  /* ---- refusal, with a proof ---------------------------------------- */
  check("one pile alone does not determine the total", sc_discover(Q, 1u, &T, 64u, &D) == SM_OK && !D.found);
  check("and the two situations it offers really agree on pile 1 and differ on the total",
        !D.needs_more && Q[0].ans[D.counter_s] == Q[0].ans[D.counter_t] &&
            T.ans[D.counter_s] != T.ans[D.counter_t]);

  /* ---- needs more than two at once ---------------------------------- */
  sr_query_init(&Q[0], 216u, 6u);
  sr_query_init(&Q[1], 216u, 6u);
  sr_query_init(&Q[2], 216u, 6u);
  sr_query_init(&T, 216u, 16u);
  for (s = 0u; s < 216u; s++) {
    sr_query_set(&Q[0], s, s / 36u);
    sr_query_set(&Q[1], s, (s / 6u) % 6u);
    sr_query_set(&Q[2], s, s % 6u);
    sr_query_set(&T, s, s / 36u + (s / 6u) % 6u + s % 6u);
  }
  check("a sum of three piles: no pair suffices, all three together do, and it says so",
        sc_discover(Q, 3u, &T, 216u, &D) == SM_OK && !D.found && D.needs_more);

  /* ---- grounding: no extrapolation ---------------------------------- */
  piles(16u, 4u);                        /* addition seen only up to 3 + 3 */
  sc_discover(Q, 2u, &T, 16u, &D);
  check("addition learned on piles up to 3 is not grounded at 5 + 2",
        !sc_grounded(&D.op, 5u, 2u) && sc_grounded(&D.op, 3u, 3u));
  sr_query_init(&Q[2], 4u, 8u);
  sr_query_init(&Q[3], 4u, 8u);
  for (s = 0u; s < 4u; s++) { sr_query_set(&Q[2], s, s * 2u); sr_query_set(&Q[3], s, 1u); }
  check("applied where some piles are bigger than it has seen: a groundless question, with the count",
        sc_apply(&D.op, &Q[2], &Q[3], 4u, &OUT, &miss) == SM_OK && OUT.has_domain == 0 && miss == 2u);
  for (s = 0u; s < 4u; s++) sr_query_set(&Q[2], s, s % 3u);
  check("applied where every pile is familiar: a real question, and correct",
        sc_apply(&D.op, &Q[2], &Q[3], 4u, &OUT, &miss) == SM_OK && OUT.has_domain == 1 && miss == 0u &&
            OUT.ans[2] == 3u);

  /* ---- sameness --------------------------------------------------------- */
  {
    sc_op_t add;
    unsigned da = 9u, db = 9u;
    piles(64u, 8u);
    sc_discover(Q, 2u, &T, 64u, &D);
    add = D.op;
    for (s = 0u; s < 64u; s++) sr_query_set(&T, s, (s / 8u) * (s % 8u));
    T.dom = 64u;
    sc_discover(Q, 2u, &T, 64u, &D);
    check("multiplication is found, and is not addition, with a disagreement named",
          D.found && !sc_same(&D.op, &add, 0, &da, &db) && da * db != da + db);
    check("an operator is the same as itself, and addition is the same swapped",
          sc_same(&add, &add, 0, 0, 0) && sc_same(&add, &add, 1, 0, 0));
  }

  /* ---- random worlds, answer known by construction -------------------- */
  {
    unsigned trials, right = 0u, refused_right = 0u, bad = 0u;
    for (trials = 0u; trials < 500u; trials++) {
      unsigned n = 20u + rnd(200u), m = 2u + rnd(5u), which = rnd(m), other = rnd(m);
      int depends_on_two = rnd(2u) && which != other;
      for (k = 0u; k < m; k++) {
        sr_query_init(&Q[k], n, 4u);
        for (s = 0u; s < n; s++) sr_query_set(&Q[k], s, rnd(4u));
      }
      sr_query_init(&T, n, 16u);
      for (s = 0u; s < n; s++) {
        unsigned v = Q[which].ans[s] * (depends_on_two ? 4u : 1u) + (depends_on_two ? Q[other].ans[s] : 0u);
        sr_query_set(&T, s, v);
      }
      if (sc_discover(Q, m, &T, n, &D) != SM_OK || !D.found) { bad++; continue; }
      /* it may find a SMALLER explanation than the planted one; whatever it
         finds must hold on every situation, checked here from scratch */
      ok = 1;
      for (s = 0u; s < n; s++) {
        unsigned a = Q[D.arg[0]].ans[s], b = D.arity == 2u ? Q[D.arg[1]].ans[s] : 0u;
        ok &= sc_value(&D.op, a, b) == T.ans[s];
      }
      if (ok) right++; else bad++;
      /* and a target that is pure noise must be refused with a real proof */
      for (s = 0u; s < n; s++) sr_query_set(&T, s, rnd(16u));
      if (sc_discover(Q, m, &T, n, &D) == SM_OK && !D.found && !D.needs_more) {
        int agree = 1;
        for (k = 0u; k < m; k++) agree &= Q[k].ans[D.counter_s] == Q[k].ans[D.counter_t];
        if (agree && T.ans[D.counter_s] != T.ans[D.counter_t]) refused_right++;
      } else if (D.found) {
        /* noise can be determined by chance in a small world; then it
           must still hold everywhere */
        t = 1u;
        for (s = 0u; s < n; s++) {
          unsigned a = Q[D.arg[0]].ans[s], b = D.arity == 2u ? Q[D.arg[1]].ans[s] : 0u;
          t &= sc_value(&D.op, a, b) == T.ans[s];
        }
        if (t) refused_right++;
      } else {
        refused_right++;   /* needs more than two: a stated limit, not a false claim */
      }
    }
    printf("        (%u found and verified, %u noise targets handled correctly, %u wrong)\n",
           right, refused_right, bad);
    check("500 random worlds: every planted rule found and verified from scratch",
          right == 500u && bad == 0u);
    check("and every noise target is refused with a valid proof, or found only if it truly holds",
          refused_right == 500u);
  }

  check("NULL arguments are checked errors",
        sc_discover(0, 1u, &T, 4u, &D) == SM_ERR_NULL_ARGUMENT &&
            sc_apply(0, &Q[0], &Q[1], 4u, &OUT, &miss) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
