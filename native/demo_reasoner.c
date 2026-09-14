/*
 * demo_reasoner.c -- reasoner.py's demonstration, on native_reasoner.c.
 *
 * Same seeded world, same 600 queries, drawn through pyrand.c so the C
 * sees the numbers the Python saw. Output is compared line for line with
 * the Python version's.
 *
 * Four outcomes, and the reasoner knows which it is in every case,
 * because it knows what it derived and from what: true (a path a -> b),
 * false (a path b -> a), undetermined (both known, neither order
 * follows), groundless (an entity no fact mentions).
 */

#include <stdio.h>

#include "native_reasoner.h"

#define N_Q 600u

static const char *NAME[] = {"true", "false", "undetermined", "groundless"};

typedef struct { unsigned a, b; } pair_t;

static void queries(unsigned n_known, const unsigned *unknown, unsigned n_unknown,
                    unsigned n, uint64_t seed, pair_t *out) {
  pyrand_t rng;
  int pool[NR_MAX_ENTITIES];
  unsigned i;
  pyrand_seed(&rng, seed);
  for (i = 0u; i < n_known; i++) pool[i] = (int)i;
  for (i = 0u; i < n; i++) {
    if (pyrand_random(&rng) < 0.25) {
      unsigned a = (unsigned)pool[pyrand_randbelow(&rng, n_known)];
      unsigned b = unknown[pyrand_randbelow(&rng, n_unknown)];
      if (pyrand_random(&rng) < 0.5) { unsigned t = a; a = b; b = t; }
      out[i].a = a;
      out[i].b = b;
    } else {
      int s[2];
      pyrand_sample(&rng, pool, n_known, 2u, s);
      out[i].a = (unsigned)s[0];
      out[i].b = (unsigned)s[1];
    }
  }
}

/* the answer's grounds: the derivation, empty for undetermined/groundless */
static unsigned grounds(const nr_world_t *w, unsigned a, unsigned b, nr_answer_t v,
                        unsigned *f, unsigned *t) {
  if (v == NR_TRUE) return nr_path(w, a, b, f, t);
  if (v == NR_FALSE) return nr_path(w, b, a, f, t);
  return 0u;
}

int main(void) {
  static nr_world_t w;
  static pair_t qs[N_Q];
  unsigned unknown[8], n_unknown, counts[4] = {0u, 0u, 0u, 0u};
  unsigned with_grounds = 0u, i, k, f[NR_MAX_PATH], t[NR_MAX_PATH];
  int ui = -1, gi = -1;

  nr_build(&w, 14u, 18u, 0u, unknown, &n_unknown);
  queries(14u, unknown, n_unknown, N_Q, 1u, qs);

  for (i = 0u; i < N_Q; i++) {
    nr_answer_t v = nr_ask(&w, qs[i].a, qs[i].b);
    counts[v]++;
    if (grounds(&w, qs[i].a, qs[i].b, v, f, t) > 0u) with_grounds++;
    if (v == NR_UNDET && ui < 0) ui = (int)i;
    if (v == NR_GROUNDLESS_Q && gi < 0) gi = (int)i;
  }

  printf("reasoner: transitive closure, 0 learned parameters\n");
  printf("  facts: %u edges over %u entities\n", w.n_edges, nr_count_known(&w));
  printf("  %u entities appear in no fact at all\n\n", n_unknown);
  for (k = 0u; k < 4u; k++) printf("  %-14s%5u\n", NAME[k], counts[k]);
  printf("\n  answers carrying an explicit derivation: %u\n\n", with_grounds);

  printf("  lt(%u, %u) -> undetermined : both entities known, neither order follows.\n",
         qs[ui].a, qs[ui].b);
  printf("                                   The premises permit both. Answering\n");
  printf("                                   \"undetermined\" IS the grounded answer.\n");
  printf("  lt(%u, %u) -> groundless   : an entity no fact mentions. Nothing to\n",
         qs[gi].a, qs[gi].b);
  printf("                                   derive from. Abstention is correct.\n\n");
  printf("  A confidence number cannot express that difference. A derivation can,\n");
  printf("  because it knows what it had, not merely what it produced.\n");

  for (i = 0u; i < N_Q; i++) {
    nr_answer_t v = nr_ask(&w, qs[i].a, qs[i].b);
    unsigned n = grounds(&w, qs[i].a, qs[i].b, v, f, t);
    if (v == NR_TRUE && n >= 3u) {
      printf("\n  lt(%u, %u) -> true, and here is why:\n", qs[i].a, qs[i].b);
      for (k = 0u; k < n; k++) printf("      lt(%u, %u)\n", f[k], t[k]);
      break;
    }
  }
  return 0;
}
