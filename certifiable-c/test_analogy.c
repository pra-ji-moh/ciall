/*
 * test_analogy.c -- smarsh_analogy.c against a brute-force matcher written
 * a different way (nested loops over every renaming of three yes/no
 * factors), on thousands of random structures.
 *
 * Checked: the search finds a renaming exactly when brute force says one
 * exists; every renaming it returns survives an independent re-check; the
 * invariant filter never throws away a real match; and properties that no
 * renaming can break (a factor value forcing the answer, a factor that
 * never matters) always survive transfer.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_analogy.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 4242u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static unsigned TABLE[64];
static unsigned from_table(const unsigned *v) { return TABLE[(v[0] * 2u + v[1]) * 2u + v[2]]; }

static const char *const NAMES[3] = {"x", "y", "z"};
static const unsigned D3[3] = {2u, 2u, 2u};

/* Independent: every pairing (6), every flip pattern (8), both answer
   labelings (2), written as plain nested loops over the raw tables. */
static int brute_same(const unsigned *ta, const unsigned *tb) {
  static const unsigned P[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
  unsigned p, flip, of, e;
  for (p = 0u; p < 6u; p++) {
    for (flip = 0u; flip < 8u; flip++) {
      for (of = 0u; of < 2u; of++) {
        int ok = 1;
        for (e = 0u; e < 8u && ok; e++) {
          unsigned va[3], vb[3], i;
          va[0] = e >> 2; va[1] = (e >> 1) & 1u; va[2] = e & 1u;
          for (i = 0u; i < 3u; i++) vb[P[p][i]] = va[i] ^ ((flip >> i) & 1u);
          ok = (ta[e] ^ of) == tb[(vb[0] * 2u + vb[1]) * 2u + vb[2]];
        }
        if (ok) return 1;
      }
    }
  }
  return 0;
}

static sa_struct_t A, B;
static sa_library_t LIB;
static sa_found_t F;

int main(void) {
  unsigned t, e, agree = 0u, maps_ok = 1u, filter_sound = 1u, transfers = 0u, transfer_bad = 0u;
  unsigned ta[8], tb[8], matches = 0u;

  printf("analogy by structure, checked against brute force\n\n");

  for (t = 0u; t < 3000u; t++) {
    sa_map_t m;
    int got, want;
    for (e = 0u; e < 8u; e++) ta[e] = rnd(2u);
    /* half the time b is a disguised copy of a, half the time random */
    if (rnd(2u)) {
      static const unsigned P[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
      unsigned p = rnd(6u), flip = rnd(8u), of = rnd(2u);
      for (e = 0u; e < 8u; e++) {
        unsigned va[3], vb[3], i;
        va[0] = e >> 2; va[1] = (e >> 1) & 1u; va[2] = e & 1u;
        for (i = 0u; i < 3u; i++) vb[P[p][i]] = va[i] ^ ((flip >> i) & 1u);
        tb[(vb[0] * 2u + vb[1]) * 2u + vb[2]] = ta[e] ^ of;
      }
    } else {
      for (e = 0u; e < 8u; e++) tb[e] = rnd(2u);
    }
    for (e = 0u; e < 8u; e++) TABLE[e] = ta[e];
    sa_define(&A, "a", "test", 3u, NAMES, D3, 2u, from_table);
    for (e = 0u; e < 8u; e++) TABLE[e] = tb[e];
    sa_define(&B, "b", "test", 3u, NAMES, D3, 2u, from_table);

    want = brute_same(ta, tb);
    got = sa_match(&A, &B, &m);
    if (got == want) agree++;
    if (got == 1) {
      sa_prop_t props[SA_MAX_PROPS], here;
      unsigned np, i;
      matches++;
      if (!sa_check_map(&A, &B, &m)) maps_ok = 0u;
      if (!sa_same_invariants(&A, &B)) filter_sound = 0u;
      np = sa_properties(&B, props, SA_MAX_PROPS);
      for (i = 0u; i < np; i++) {
        if (props[i].kind == SA_INTERCHANGEABLE) continue;   /* may not survive relabeling */
        transfers++;
        if (!sa_transfer(&m, &A, &B, &props[i], &here) || !sa_holds(&A, &here)) transfer_bad++;
      }
    }
  }
  printf("        (%u of 3000 pairs the same shape)\n", matches);
  check("3000 random pairs: the search finds a renaming exactly when brute force does",
        agree == 3000u);
  check("every renaming it returns passes the independent re-check", maps_ok);
  check("the invariant filter never drops a pair that really is the same shape", filter_sound);
  printf("        (%u properties carried across)\n", transfers);
  check("every forcing or never-matters property survives transfer and holds on arrival",
        transfers > 0u && transfer_bad == 0u);

  /* the library search agrees with matching one by one */
  {
    unsigned i, direct = 0u;
    memset(&LIB, 0, sizeof LIB);
    for (i = 0u; i < 40u; i++) {
      for (e = 0u; e < 8u; e++) TABLE[e] = rnd(2u);
      sa_define(&B, "b", "test", 3u, NAMES, D3, 2u, from_table);
      sa_add(&LIB, &B);
    }
    for (e = 0u; e < 8u; e++) TABLE[e] = LIB.s[7].table[e];
    sa_define(&A, "a", "test", 3u, NAMES, D3, 2u, from_table);
    sa_find(&LIB, &A, &F);
    for (i = 0u; i < 40u; i++) {
      sa_map_t m;
      if (sa_match(&A, &LIB.s[i], &m) == 1) direct++;
    }
    check("library search: filtered + searched = everything, and it finds what direct matching finds",
          F.filtered + F.n_matches + F.searched_no_match == 40u && F.n_matches == direct &&
              F.n_matches >= 1u);
  }

  /* different shapes: sizes differ, so nothing is even searched */
  {
    static const unsigned D2[2] = {2u, 2u};
    for (e = 0u; e < 8u; e++) TABLE[e] = e & 1u;
    sa_define(&A, "a", "test", 3u, NAMES, D3, 2u, from_table);
    sa_define(&B, "b", "test", 2u, NAMES, D2, 2u, from_table);
    check("structures with different numbers of factors are never the same shape",
          !sa_same_invariants(&A, &B));
  }

  check("NULL arguments are checked errors",
        sa_find(0, &A, &F) == SM_ERR_NULL_ARGUMENT && sa_add(0, &A) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
