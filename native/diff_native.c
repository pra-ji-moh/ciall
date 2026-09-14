/*
 * diff_native.c -- the C reasoner against the Python one, across many
 * seeded worlds. Paired with diff_native.py, which prints the same lines.
 *
 * For every world (seed 0..199, several sizes) and every ordered pair of
 * entities, one line: verdict, S, H, rank D, and the derivation. Verdicts,
 * S, H and D must match exactly. The derivation is the one place the two
 * may legitimately differ: Python's BFS visits neighbours in set order,
 * this one in ascending order, so where two shortest paths exist they can
 * pick different ones. So each C witness is also checked here, by this
 * program, to be a real chain of edges from a to b, and no longer than
 * the Python one (the diff script compares lengths).
 */

#include <stdio.h>

#include "native_reasoner.h"

static const char *VNAME[] = {"derived", "contradiction", "groundless", "undetermined"};

static int chain_ok(const nr_world_t *w, unsigned a, unsigned b, const nr_result_t *r,
                    int value) {
  unsigned k, at = value ? a : b, end = value ? b : a;
  if (r->witness_len == 0u) return 0;
  for (k = 0u; k < r->witness_len; k++) {
    if (r->wfrom[k] != at) return 0;
    if (!(w->adj[r->wfrom[k]] & ((uint64_t)1 << r->wto[k]))) return 0;
    at = r->wto[k];
  }
  return at == end;
}

int main(void) {
  static const unsigned SIZES[3][2] = {{8u, 10u}, {14u, 18u}, {20u, 40u}};
  unsigned si, a, b, k, bad = 0u;
  uint64_t seed;
  for (si = 0u; si < 3u; si++) {
    for (seed = 0u; seed < 200u; seed++) {
      static nr_world_t w;
      unsigned unknown[8], nu, n = SIZES[si][0];
      nr_build(&w, n, SIZES[si][1], seed, unknown, &nu);
      for (a = 0u; a < n + 2u; a++) {
        nr_result_t rk;
        nr_rank_query(&w, a, &rk);
        printf("R %u %llu %u %s", n, (unsigned long long)seed, a, VNAME[rk.verdict]);
        if (rk.has_S) printf(" %.17g %.17g", rk.S, rk.H);
        for (k = 0u; k < 64u; k++) if (rk.D & ((uint64_t)1 << k)) printf(" %u", k);
        printf("\n");
        for (b = 0u; b < n + 2u; b++) {
          nr_result_t r;
          if (a == b) continue;
          nr_node_query(&w, a, b, &r);
          printf("Q %u %llu %u %u %s", n, (unsigned long long)seed, a, b, VNAME[r.verdict]);
          if (r.has_S) printf(" %.17g %.17g", r.S, r.H);
          if (r.verdict == NR_DERIVED) {
            printf(" %s |", r.value ? "True" : "False");
            for (k = 0u; k < r.witness_len; k++) printf(" %u>%u", r.wfrom[k], r.wto[k]);
            if (!chain_ok(&w, a, b, &r, r.value)) bad++;
          }
          printf("\n");
        }
      }
    }
  }
  fprintf(stderr, "C witnesses that are not a valid chain: %u\n", bad);
  return bad == 0u ? 0 : 1;
}
