/*
 * test_abstract.c -- smarsh_abstract.c on random worlds with a hidden
 * structure planted in them, every claim checked against the full truth.
 *
 * Each world: up to 8 raw switches, split at random into groups whose
 * members the outcome treats alike, plus switches that never matter, with
 * a random rule over the group counts. From complete observation:
 *   - every group it finds must really be interchangeable (brute force),
 *   - every planted group must lie inside a group it finds (it may find
 *     MORE symmetry than planted, when the random rule happens to have it,
 *     and that is checked to be real too),
 *   - every "never matters" must be true, every "matters" must be true,
 *   - and its questions must determine the outcome, which the theorem
 *     promises and the kernel checks.
 * From partial observation: every PROVED claim must still be true, and
 * every refusal of a symmetry must come with a real counter-swap.
 */

#include <stdio.h>

#include "smarsh_abstract.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 8080u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static unsigned N, GROUP[8], NG, RULE[512];
static unsigned count_in(unsigned s, unsigned g) {
  unsigned i, c = 0u;
  for (i = 0u; i < N; i++) if (GROUP[i] == g) c += (s >> i) & 1u;
  return c;
}
static unsigned truth(unsigned s) {
  unsigned g, key = 0u;
  for (g = 0u; g < NG; g++) key = key * 9u + count_in(s, g);
  return RULE[key % 512u] % 3u;
}

static int really_interchangeable(unsigned i, unsigned j) {
  unsigned s;
  for (s = 0u; s < (1u << N); s++) {
    unsigned bi = (s >> i) & 1u, bj = (s >> j) & 1u, t = s;
    if (bi != bj) t ^= (1u << i) | (1u << j);
    if (truth(s) != truth(t)) return 0;
  }
  return 1;
}

static int really_matters(unsigned i) {
  unsigned s;
  for (s = 0u; s < (1u << N); s++) if (truth(s) != truth(s ^ (1u << i))) return 1;
  return 0;
}

static ab_raw_t RAW;
static ab_result_t AB;

int main(void) {
  unsigned t, i, j, s;
  unsigned groups_real = 1u, planted_inside = 1u, rel_right = 1u, determined = 1u;
  unsigned proved_sound = 1u, refusals_real = 1u, partial_runs = 0u;

  printf("building worlds from raw observation, checked against planted truth\n\n");

  for (t = 0u; t < 300u; t++) {
    N = 3u + rnd(6u);
    NG = 1u + rnd(3u);
    for (i = 0u; i < N; i++) GROUP[i] = rnd(NG + 1u);     /* NG: never matters */
    for (i = 0u; i < 512u; i++) RULE[i] = rnd(3u);

    ab_clear(&RAW, N, 3u);
    for (s = 0u; s < (1u << N); s++) ab_observe(&RAW, s, truth(s));
    if (ab_abstract(&RAW, &AB) != SM_OK) {
      /* only allowed when truly nothing matters */
      for (i = 0u; i < N; i++) if (really_matters(i)) rel_right = 0u;
      continue;
    }
    if (!AB.determined) determined = 0u;
    for (i = 0u; i < N; i++) {
      if (AB.relevance[i] == AB_RELEVANT && !really_matters(i)) rel_right = 0u;
      if (AB.relevance[i] == AB_IRRELEVANT && really_matters(i)) rel_right = 0u;
      if (AB.relevance[i] == AB_UNDETERMINED || AB.relevance[i] == AB_IRRELEVANT_CONJ) rel_right = 0u;
      for (j = i + 1u; j < N; j++) {
        int same_found = AB.group_of[i] != AB_MAX_RAW && AB.group_of[i] == AB.group_of[j];
        if (same_found && !really_interchangeable(i, j)) groups_real = 0u;
        if (GROUP[i] == GROUP[j] && GROUP[i] < NG && really_matters(i) && really_matters(j) &&
            !same_found) {
          planted_inside = 0u;
        }
      }
    }

    /* the same world, partly seen */
    ab_clear(&RAW, N, 3u);
    for (s = 0u; s < (1u << N); s++) if (rnd(10u) < 7u) ab_observe(&RAW, s, truth(s));
    if (ab_abstract(&RAW, &AB) != SM_OK) continue;
    partial_runs++;
    for (i = 0u; i < N; i++) {
      if (AB.relevance[i] == AB_IRRELEVANT && really_matters(i)) proved_sound = 0u;
      if (AB.relevance[i] == AB_RELEVANT && !really_matters(i)) proved_sound = 0u;
    }
    {
      unsigned g;
      for (g = 0u; g < AB.n_groups; g++) {
        if (!AB.group_proved[g] || AB.group_size[g] < 2u) continue;
        for (i = 0u; i < N; i++) for (j = i + 1u; j < N; j++) {
          if ((AB.group_members[g] >> i & 1u) && (AB.group_members[g] >> j & 1u) &&
              !really_interchangeable(i, j)) {
            proved_sound = 0u;
          }
        }
      }
    }
    /* a refusal (two kept attributes in different groups, a swap pair seen)
       must be backed by an observed counter-swap: checked directly */
    for (i = 0u; i < N; i++) for (j = i + 1u; j < N; j++) {
      int seen_counter = 0, seen_any = 0;
      if (AB.group_of[i] == AB_MAX_RAW || AB.group_of[j] == AB_MAX_RAW) continue;
      if (AB.group_of[i] == AB.group_of[j]) continue;
      for (s = 0u; s < (1u << N); s++) {
        unsigned bi = (s >> i) & 1u, bj = (s >> j) & 1u, u;
        if (bi == bj || !RAW.observed[s]) continue;
        u = s ^ ((1u << i) | (1u << j));
        if (!RAW.observed[u]) continue;
        seen_any = 1;
        if (RAW.outcome[s] != RAW.outcome[u]) seen_counter = 1;
      }
      /* seen swaps, none contradicting, yet kept apart: a false refusal */
      if (seen_any && !seen_counter) refusals_real = 0u;
    }
  }

  check("300 planted worlds, fully seen: every group it finds is truly interchangeable",
        groups_real);
  check("every planted group lies inside a group it found", planted_inside);
  check("every \"matters\" and \"never matters\" is true, and nothing is left undecided",
        rel_right);
  check("its invented questions always determine the outcome", determined);
  printf("        (%u partly seen worlds)\n", partial_runs);
  check("partly seen: every PROVED relevance, irrelevance and symmetry is true", proved_sound);
  check("partly seen: it never refuses a symmetry that every seen swap supports", refusals_real);

  ab_clear(&RAW, 2u, 2u);
  ab_observe(&RAW, 1u, 0u);
  check("the same raw situation seen with two outcomes is refused as contradictory",
        ab_observe(&RAW, 1u, 1u) == SM_ERR_INTERNAL_INVARIANT);
  check("NULL arguments are checked errors",
        ab_abstract(0, &AB) == SM_ERR_NULL_ARGUMENT && ab_observe(0, 0u, 0u) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
