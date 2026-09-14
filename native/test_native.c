/*
 * test_native.c -- the four native/ test files, in C, on native_reasoner.c.
 *
 *   1. test_node_reasoner.py  S and H by hand, the contradiction branch
 *                             forced, uniformity pinned, determinism
 *   2. test_tau_boundary.py   the speculate/decline split at a real tau
 *   3. test_rank_domain.py    the rank formula against brute force
 *   4. test_compose.py        compose_and's truth table and ancestry
 *
 * Every Python check is here, with the same fixtures, seeds and
 * tolerances. Two additions, marked as such: the rank formula is also
 * checked against brute force on 300 random small worlds rather than the
 * six hand-built ones only, and every derivation's witness is checked to
 * be a real chain of edges.
 */

#include <math.h>
#include <stdio.h>

#include "native_reasoner.h"

#define BIT(i) ((uint64_t)1 << (i))

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static void world(nr_world_t *w, uint64_t known, const unsigned (*edges)[2], unsigned n) {
  unsigned i;
  nr_world_init(w, known);
  for (i = 0u; i < n; i++) nr_world_add_edge(w, edges[i][0], edges[i][1]);
  nr_world_close(w);
}

static uint64_t upto(unsigned k) { return k >= 64u ? ~(uint64_t)0 : BIT(k) - 1u; }

static void spec_seed(const nr_result_t *r, double tau, uint64_t seed, long id,
                      nr_result_t *out) {
  pyrand_t g;
  pyrand_seed(&g, seed);
  nr_speculate(r, tau, &g, id, out);
}

/* ---- 1. node maths ------------------------------------------------- */

static void test_node_reasoner(void) {
  static const unsigned E01[1][2] = {{0u, 1u}};
  static const unsigned CYC[2][2] = {{0u, 1u}, {1u, 0u}};
  nr_world_t w, w2, cyc, w3;
  nr_result_t r, r2, r3, rc;
  unsigned unknown[8], nu, a = 0u, b = 0u, i, j, n_true = 0u;
  int found = 0, saw_t = 0, saw_f = 0;
  uint64_t seed;

  printf("S(n) and H(n), regression-checked against hand-computed values\n");
  world(&w, BIT(0) | BIT(1), E01, 1u);
  nr_node_query(&w, 0u, 1u, &r);
  check("derived: |D|=1, |Dom|=2 gives S=0.5, not 1", r.S == 0.5);
  check("derived: H=0 (the emission was forced, not guessed)", r.H == 0.0);
  check("derived: verdict and value are correct", r.verdict == NR_DERIVED && r.value == 1);

  world(&w2, BIT(5) | BIT(6), NULL, 0u);
  nr_node_query(&w2, 5u, 6u, &r2);
  check("undetermined: |D|=2=|Dom|, S=0 exactly (nothing ruled out)", r2.S == 0.0);
  check("undetermined: H=1.0 bit (one of two, forced to guess)", r2.H == 1.0);
  check("undetermined: verdict is undetermined, no value yet",
        r2.verdict == NR_UNDETERMINED && !r2.has_value);
  check("S is degenerate on a boolean domain: only {0.0, 0.5} occur",
        r.S == 0.5 && r2.S == 0.0);

  nr_node_query(&w, 2u, 3u, &r3);
  check("groundless: no domain, S is None, not 0",
        r3.verdict == NR_GROUNDLESS && !r3.has_S && !r3.has_dom);

  printf("\nthe contradiction branch, forced by a cyclic world rather than assumed unreachable\n");
  world(&cyc, BIT(0) | BIT(1), CYC, 2u);
  nr_node_query(&cyc, 0u, 1u, &rc);
  check("a genuine cycle is reported as contradiction, not silently resolved",
        rc.verdict == NR_CONTRADICTION);
  check("contradiction carries no value to act on", !rc.has_value);
  check("contradiction is reported at maximum support -- the premises are certain and wrong",
        rc.S == 1.0);

  printf("\nspeculation is uniform: pinned with a tolerance, not read off a printout\n");
  nr_build(&w3, 14u, 18u, 0u, unknown, &nu);
  for (i = 0u; i < 14u && !found; i++) {
    for (j = 0u; j < 14u; j++) {
      if (i == j) continue;
      nr_node_query(&w3, i, j, &r);
      if (r.verdict == NR_UNDETERMINED) { a = i; b = j; found = 1; break; }
    }
  }
  for (seed = 0u; seed < 6000u; seed++) {
    nr_node_query(&w3, a, b, &r);
    spec_seed(&r, 0.0, seed, 1L, &r2);
    n_true += (unsigned)r2.value;
  }
  check("guessed True close to 0.5 over 6000 trials (tolerance 0.03)",
        fabs((double)n_true / 6000.0 - 0.5) < 0.03);

  printf("\ndeterminism: same seed reproduces exactly\n");
  nr_node_query(&w3, a, b, &r);
  spec_seed(&r, 0.0, 42u, 1L, &r2);
  spec_seed(&r, 0.0, 42u, 1L, &r3);
  check("two runs with the same seed agree on the guess", r2.value == r3.value);
  for (seed = 0u; seed < 20u; seed++) {
    spec_seed(&r, 0.0, seed, 1L, &r2);
    if (r2.value) saw_t = 1;
    else saw_f = 1;
  }
  check("different seeds are not all forced to the same guess", saw_t && saw_f);
}

/* ---- 2. tau boundary ----------------------------------------------- */

static void test_tau_boundary(void) {
  static const unsigned E[5][2] = {{0u, 3u}, {1u, 3u}, {3u, 4u}, {0u, 2u}, {2u, 4u}};
  static const unsigned EE[2][2] = {{0u, 1u}, {1u, 2u}};
  nr_world_t w, we;
  nr_result_t above, below, edge, s;
  int above_all_spec = 1, below_all_undet = 1, edge_all_spec = 1, varied = 0, first = -1;
  uint64_t seed;

  world(&w, upto(5u), E, 5u);
  nr_rank_query(&w, 3u, &above);
  nr_rank_query(&w, 2u, &below);
  printf("\nthe boundary world, checked before the boundary claim is tested on it\n");
  check("entity 3 sits just ABOVE tau: S=0.6", above.S == 0.6);
  check("entity 2 sits just BELOW tau: S=0.4", below.S == 0.4);
  check("both are undetermined going in, not already derived",
        above.verdict == NR_UNDETERMINED && below.verdict == NR_UNDETERMINED);
  check("entity 3's D matches brute-force ground truth", above.D == nr_brute_force_positions(&w, 3u));
  check("entity 2's D matches brute-force ground truth", below.D == nr_brute_force_positions(&w, 2u));

  printf("\ntau=0.5: does the split land correctly, across 500 seeds each?\n");
  for (seed = 0u; seed < 500u; seed++) {
    spec_seed(&above, 0.5, seed, 3L, &s);
    if (s.verdict != NR_SPECULATED) above_all_spec = 0;
    spec_seed(&below, 0.5, seed, 2L, &s);
    if (s.verdict != NR_UNDETERMINED) below_all_undet = 0;
  }
  check("S=0.6 >= tau=0.5: ALWAYS speculates, never declines, across every seed", above_all_spec);
  check("S=0.4 < tau=0.5: ALWAYS declines, never speculates, across every seed", below_all_undet);
  above_all_spec = 1;
  for (seed = 0u; seed < 200u; seed++) {
    spec_seed(&above, 0.5, seed, 3L, &s);
    if (s.verdict != NR_SPECULATED) above_all_spec = 0;
    if (first < 0) first = s.value;
    else if (s.value != first) varied = 1;
  }
  check("the decision to speculate does not depend on the seed (only the value does)",
        above_all_spec);
  check("but the value chosen does vary across seeds (it is a real guess, not fixed)", varied);

  printf("\nthe exact boundary: S == tau\n");
  world(&we, upto(4u), EE, 2u);
  nr_rank_query(&we, 1u, &edge);
  check("constructed case actually lands at S=0.5 exactly", edge.S == 0.5);
  for (seed = 0u; seed < 300u; seed++) {
    spec_seed(&edge, 0.5, seed, 1L, &s);
    if (s.verdict != NR_SPECULATED) edge_all_spec = 0;
  }
  check("S == tau counts as clearing the bar (>=), so it speculates", edge_all_spec);
}

/* ---- 3. rank domain ------------------------------------------------ */

static void test_rank_domain(void) {
  static const unsigned CHAIN[4][2] = {{0u, 1u}, {1u, 2u}, {2u, 3u}, {3u, 4u}};
  static const unsigned ONE[1][2] = {{0u, 1u}};
  static const unsigned DIA[4][2] = {{0u, 1u}, {0u, 2u}, {1u, 3u}, {2u, 3u}};
  static const unsigned TWO[2][2] = {{0u, 2u}, {1u, 3u}};
  static const unsigned FAN[4][2] = {{0u, 1u}, {0u, 2u}, {0u, 3u}, {0u, 4u}};
  static const unsigned CYC[2][2] = {{0u, 1u}, {1u, 0u}};
  static const struct { const char *name; unsigned k; const unsigned (*e)[2]; unsigned n; } W[6] = {
    {"full chain (0<1<2<3<4)", 5u, CHAIN, 4u},
    {"one order, one free (a<b, c untouched)", 3u, ONE, 1u},
    {"diamond (a<b, a<c, b<d, c<d)", 4u, DIA, 4u},
    {"two independent chains (a<c, b<d)", 4u, TWO, 2u},
    {"wide fan (a < everything, rest free)", 5u, FAN, 4u},
    {"totally free (no edges at all)", 4u, NULL, 0u},
  };
  nr_world_t w;
  nr_result_t r;
  unsigned i, e, t, outside = 0u, loose = 0u;
  char name[160];
  pyrand_t rng;

  printf("\nthe range formula against brute-force ground truth, across six posets\n\n");
  for (i = 0u; i < 6u; i++) {
    world(&w, upto(W[i].k), W[i].e, W[i].n);
    for (e = 0u; e < W[i].k; e++) {
      nr_rank_query(&w, e, &r);
      sprintf(name, "%s: entity %u", W[i].name, e);
      check(name, r.D == nr_brute_force_positions(&w, e));
      if (r.S != 0.0 && r.S != 0.5) outside = 1u;
    }
  }
  check("S now takes values outside the old degenerate {0.0, 0.5} set", outside);

  world(&w, upto(5u), CHAIN, 4u);
  nr_rank_query(&w, 60u, &r);   /* the Python used 99; ids here stop at 63 */
  check("an unknown entity is still groundless, not merely a huge D", r.verdict == NR_GROUNDLESS);
  world(&w, upto(2u), CYC, 2u);
  nr_rank_query(&w, 0u, &r);
  check("a cycle is still reported as contradiction under the rank query too",
        r.verdict == NR_CONTRADICTION);

  /* Added in the C port: the six posets above were chosen by hand. These
     are not -- random DAGs on 3..7 entities, every entity checked. */
  pyrand_seed(&rng, 2026u);
  for (t = 0u; t < 300u; t++) {
    unsigned k = 3u + pyrand_randbelow(&rng, 5u), m = pyrand_randbelow(&rng, 2u * k), x;
    nr_world_init(&w, upto(k));
    for (x = 0u; x < m; x++) {
      unsigned p = pyrand_randbelow(&rng, k), q = pyrand_randbelow(&rng, k);
      if (p < q) nr_world_add_edge(&w, p, q);   /* ids in order keep it acyclic */
    }
    nr_world_close(&w);
    for (e = 0u; e < k; e++) {
      nr_rank_query(&w, e, &r);
      if (r.D != nr_brute_force_positions(&w, e)) loose++;
    }
  }
  check("added: the formula is exact on 300 random DAGs of 3 to 7 entities too", loose == 0u);
}

/* ---- 4. composition ------------------------------------------------ */

static nr_result_t mk(nr_verdict_t v, int has_value, int value) {
  nr_result_t r;
  unsigned char *p = (unsigned char *)&r;
  unsigned i;
  for (i = 0u; i < sizeof r; i++) p[i] = 0u;
  r.verdict = v;
  r.is_bool = 1;
  r.has_value = has_value;
  r.value = value;
  if (v != NR_GROUNDLESS) { r.has_dom = 1; r.dom = 2u; r.has_S = 1; }
  if (v == NR_DERIVED) { r.D = BIT((unsigned)value); r.S = 0.5; }
  if (v == NR_UNDETERMINED) { r.D = 3u; r.S = 0.0; r.H = 1.0; }
  if (v == NR_CONTRADICTION) { r.S = 1.0; r.H = INFINITY; }
  return r;
}

static long next_guess = 1000L;

static nr_result_t speculated(int value, double H, long ident) {
  nr_result_t r = mk(NR_SPECULATED, 1, value);
  r.D = 3u;
  r.S = 0.0;
  r.H = H;
  r.n_anc = 1u;
  r.anc_id[0] = ident < 0 ? ++next_guess : ident;
  r.anc_H[0] = H;
  return r;
}

static int is(const nr_result_t *r, nr_verdict_t v, int has_value, int value) {
  return r->verdict == v && r->has_value == has_value && (!has_value || r->value == value);
}

static void test_compose(void) {
  static const char *NAMES[9] = {
    "case 0: derived(True) AND derived(True) -> derived(True)",
    "case 1: derived(True) AND derived(False) -> derived(False)",
    "case 2: derived(False) AND derived(True) -> derived(False)",
    "case 3: derived(False) AND derived(False) -> derived(False)",
    "case 4: derived(True) AND undetermined(None) -> undetermined(None)",
    "case 5: undetermined(None) AND derived(True) -> undetermined(None)",
    "case 6: derived(False) AND undetermined(None) -> derived(False)",
    "case 7: undetermined(None) AND derived(False) -> derived(False)",
    "case 8: undetermined(None) AND undetermined(None) -> undetermined(None)",
  };
  nr_result_t T = mk(NR_DERIVED, 1, 1), F = mk(NR_DERIVED, 1, 0);
  nr_result_t U = mk(NR_UNDETERMINED, 0, 0), G = mk(NR_GROUNDLESS, 0, 0);
  nr_result_t C = mk(NR_CONTRADICTION, 0, 0);
  const nr_result_t *lhs[9] = {&T, &T, &F, &F, &T, &U, &F, &U, &U};
  const nr_result_t *rhs[9] = {&T, &F, &T, &F, &U, &T, &U, &F, &U};
  const int want_v[9] = {NR_DERIVED, NR_DERIVED, NR_DERIVED, NR_DERIVED, NR_UNDETERMINED,
                         NR_UNDETERMINED, NR_DERIVED, NR_DERIVED, NR_UNDETERMINED};
  const int want_val[9] = {1, 0, 0, 0, -1, -1, 0, 0, -1};
  nr_result_t r, r2, r3, r4, s1, s3, g, g2, left, right, joined, deep, both, x, y;
  unsigned i;

  printf("\ncompose_and against a hand-computed three-valued truth table\n");
  for (i = 0u; i < 9u; i++) {
    nr_compose_and(lhs[i], rhs[i], &r);
    check(NAMES[i], is(&r, (nr_verdict_t)want_v[i], want_val[i] >= 0, want_val[i]));
  }

  printf("\nthe absorbing case: False wins even when the other side is groundless\n");
  nr_compose_and(&G, &F, &r);
  check("groundless AND definitely-False -> DERIVED False, not groundless", is(&r, NR_DERIVED, 1, 0));
  nr_compose_and(&F, &G, &r);
  check("definitely-False AND groundless -> DERIVED False (order should not matter)",
        is(&r, NR_DERIVED, 1, 0));
  nr_compose_and(&G, &T, &r);
  check("groundless AND definitely-True -> groundless (True does not absorb)", r.verdict == NR_GROUNDLESS);
  nr_compose_and(&G, &U, &r);
  check("groundless AND undetermined -> groundless (nothing forces False)", r.verdict == NR_GROUNDLESS);
  nr_compose_and(&G, &G, &r);
  check("groundless AND groundless -> groundless", r.verdict == NR_GROUNDLESS);

  printf("\ncontradiction propagates through composition\n");
  nr_compose_and(&C, &T, &r);
  check("contradiction AND anything -> contradiction", r.verdict == NR_CONTRADICTION);

  printf("\nancestry_H: this rests on a guess survives composition\n");
  s1 = speculated(1, 1.0, -1L);
  nr_compose_and(&s1, &T, &r);
  check("composing a speculated True with a derived True still resolves to True",
        is(&r, NR_DERIVED, 1, 1));
  check("but ancestry_H carries the upstream guess forward -- this is NOT fully derived",
        nr_ancestry_H(&r) == 1.0);
  check("the node's OWN H is still 0 -- the composition itself was forced, only its input was a guess",
        r.H == 0.0);
  s3 = speculated(1, 1.0, -1L);
  nr_compose_and(&r, &s3, &r2);
  check("a second speculated input adds to ancestry rather than replacing it",
        nr_ancestry_H(&r2) == 2.0);
  nr_compose_and(&T, &T, &r3);
  check("two genuine derivations compose with ancestry_H = 0 exactly", nr_ancestry_H(&r3) == 0.0);
  x = speculated(0, 1.0, -1L);
  nr_compose_and(&x, &U, &r4);
  check("absorption through a speculated False still carries that guess in ancestry",
        is(&r4, NR_DERIVED, 1, 0) && nr_ancestry_H(&r4) == 1.0);

  printf("\nthe diamond: one guess feeding two paths that recombine\n");
  g = speculated(1, 1.0, 1L);
  nr_compose_and(&g, &T, &left);
  nr_compose_and(&g, &T, &right);
  nr_compose_and(&left, &right, &joined);
  check("one shared guess counted once, not twice", nr_ancestry_H(&joined) == 1.0);
  deep = g;
  for (i = 0u; i < 5u; i++) { nr_compose_and(&deep, &deep, &y); deep = y; }
  check("a depth-5 diamond does not explode to 2^5 bits", nr_ancestry_H(&deep) == 1.0);
  g2 = speculated(1, 1.0, 2L);
  nr_compose_and(&g, &T, &x);
  nr_compose_and(&g2, &T, &y);
  nr_compose_and(&x, &y, &both);
  check("two genuinely distinct guesses still add to 2.0", nr_ancestry_H(&both) == 2.0);

  printf("\nancestry survives the branches that used to drop it\n");
  nr_compose_and(&g, &C, &r);
  check("a contradiction reached through a guess remembers the guess", nr_ancestry_H(&r) == 1.0);
  nr_compose_and(&g, &G, &r);
  check("a groundless result reached through a guess remembers the guess", nr_ancestry_H(&r) == 1.0);
}

/* ---- added: witnesses are real derivations -------------------------- */

static void test_witnesses(void) {
  nr_world_t w;
  nr_result_t r;
  unsigned unknown[8], nu, a, b, k, bad = 0u, total = 0u;
  uint64_t seed;
  printf("\nadded: every witness is a chain of stated facts from one end to the other\n");
  for (seed = 0u; seed < 100u; seed++) {
    nr_build(&w, 14u, 18u, seed, unknown, &nu);
    for (a = 0u; a < 14u; a++) {
      for (b = 0u; b < 14u; b++) {
        unsigned at, end;
        if (a == b) continue;
        nr_node_query(&w, a, b, &r);
        if (r.verdict != NR_DERIVED) continue;
        total++;
        at = r.value ? a : b;
        end = r.value ? b : a;
        for (k = 0u; k < r.witness_len; k++) {
          if (r.wfrom[k] != at || !(w.adj[r.wfrom[k]] & BIT(r.wto[k]))) break;
          at = r.wto[k];
        }
        if (k != r.witness_len || at != end || r.witness_len == 0u) bad++;
      }
    }
  }
  printf("  (%u derivations checked)\n", total);
  check("added: no derivation's witness is broken, across 100 seeded worlds", bad == 0u && total > 0u);
}

int main(void) {
  test_node_reasoner();
  test_tau_boundary();
  test_rank_domain();
  test_compose();
  test_witnesses();
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
