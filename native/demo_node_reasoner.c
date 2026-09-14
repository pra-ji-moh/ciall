/*
 * demo_node_reasoner.c -- node_reasoner.py's demonstration, in C.
 *
 * Closure-derived D(n), S(n), H(n) per query, and forced collapse
 * isolated in one function, nr_speculate. The guesses are drawn through
 * pyrand.c, so the C and Python runs make the same guesses; the output is
 * compared line for line.
 */

#include <stdio.h>

#include "native_reasoner.h"

static const char *VNAME[] = {"derived", "contradiction", "groundless", "undetermined"};

static const char *pybool(int v) { return v ? "True" : "False"; }

static long guess_id(unsigned a, unsigned b) { return (long)(a * 64u + b); }

int main(void) {
  static nr_world_t w;
  unsigned unknown[8], n_unknown, all[NR_MAX_ENTITIES], n_all = 0u;
  unsigned counts[4] = {0u, 0u, 0u, 0u}, i, j, k;
  unsigned ex_a = 0u, ex_b = 0u;
  int have_example = 0;
  nr_result_t r, spec, example;
  pyrand_t rng;

  nr_build(&w, 14u, 18u, 0u, unknown, &n_unknown);
  for (i = 0u; i < 14u; i++) all[n_all++] = i;
  for (i = 0u; i < n_unknown; i++) all[n_all++] = unknown[i];

  pyrand_seed(&rng, 1u);
  for (i = 0u; i < n_all; i++) {
    for (j = 0u; j < n_all; j++) {
      if (i == j) continue;
      nr_node_query(&w, all[i], all[j], &r);
      counts[r.verdict]++;
      if (r.verdict == NR_UNDETERMINED) {
        /* tau = 0.0: boolean D(n) is {True, False} whenever undetermined,
           so S(n) is exactly 0, and 0.0 is the only tau it can clear. */
        nr_speculate(&r, 0.0, &rng, guess_id(all[i], all[j]), &spec);
        if (spec.verdict == NR_SPECULATED && !have_example) {
          example = spec;
          ex_a = all[i];
          ex_b = all[j];
          have_example = 1;
        }
      }
    }
  }

  printf("node_reasoner: closure-derived D(n), S(n), H(n); forced collapse isolated\n\n");
  for (k = 0u; k < 4u; k++) printf("  %-14s%5u\n", VNAME[k], counts[k]);
  printf("\n");
  printf("  lt(%u, %u) undetermined, S=%.2f, H=%.2f bits\n", ex_a, ex_b, example.S, example.H);
  printf("  speculated at tau=0.0 (the only tau a boolean query clears) -> %s\n\n",
         pybool(example.value));

  printf("  Is the forced guess weighted toward being right? Checking rather than\n");
  printf("  asserting -- run the same undecided pair 4000 times, seed varying only:\n");
  {
    unsigned n_true = 0u, n_false = 0u, total;
    double p_true;
    uint64_t seed;
    for (seed = 0u; seed < 4000u; seed++) {
      pyrand_t g;
      pyrand_seed(&g, seed);
      nr_node_query(&w, ex_a, ex_b, &r);
      nr_speculate(&r, 0.0, &g, guess_id(ex_a, ex_b), &spec);
      if (spec.verdict == NR_SPECULATED) {
        if (spec.value) n_true++;
        else n_false++;
      }
    }
    total = n_true + n_false;
    p_true = (double)n_true / (double)total;
    printf("    guessed True:  %5u / %u  (%.3f)\n", n_true, total, p_true);
    printf("    guessed False: %5u / %u  (%.3f)\n", n_false, total, 1.0 - p_true);
    if ((p_true > 0.5 ? p_true - 0.5 : 0.5 - p_true) < 0.03) {
      printf("  Close to 0.5. The guess carries no signal -- it is not trying to be\n");
      printf("  right more often than a coin flip, which is what \"not predictive\"\n");
      printf("  has to mean for a forced binary guess.\n");
    } else {
      printf("  NOT close to 0.5 -- something here IS weighted, and the claim below\n");
      printf("  is false as this file currently stands.\n");
    }
  }

  printf("\n");
  for (i = 0u; i < 14u; i++) {
    for (j = 0u; j < 14u; j++) {
      nr_node_query(&w, i, j, &r);
      if (r.verdict == NR_DERIVED) {
        printf("  lt(%u, %u) -> %s, derived, H=0. The witness:\n", i, j, pybool(r.value));
        for (k = 0u; k < r.witness_len; k++) printf("      lt(%u, %u)\n", r.wfrom[k], r.wto[k]);
        return 0;
      }
    }
  }
  return 0;
}
