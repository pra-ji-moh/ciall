/*
 * test_time.c -- smarsh_time.c against independent computations on random
 * machines.
 *
 *   planning      shortest plan length, and "no plan exists", checked
 *                 against a separate exhaustive search over sets of states
 *                 written differently (a plain array walk, depth-first
 *                 layers)
 *   merging       two states put in one class must produce identical
 *                 sensor traces under every action sequence long enough
 *                 to tell any two states of a deterministic machine apart;
 *                 two in different classes must differ on some sequence
 *   tracking      step and sense against direct set computation
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_time.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t lcg = 5150u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

static tm_model_t M;
static unsigned DET[TM_MAX_ACTIONS][TM_MAX_STATES];

/* independent: layer by layer, all sets reachable, the first layer that
   has a set inside the goal is the shortest plan length */
static tm_set_t LAYER[2][4096], SEEN_SETS[8192];
static int shortest(tm_set_t from, tm_set_t goal) {
  unsigned nl = 1u, ns = 1u, depth, cur = 0u, i, a;
  LAYER[0][0] = from;
  SEEN_SETS[0] = from;
  for (depth = 0u; depth <= 40u; depth++) {
    unsigned nn = 0u;
    for (i = 0u; i < nl; i++) if ((LAYER[cur][i] & ~goal) == 0u) return (int)depth;
    for (i = 0u; i < nl; i++) {
      for (a = 0u; a < M.n_actions; a++) {
        tm_set_t s = LAYER[cur][i], out = 0u;
        unsigned x, k, dup = 0u;
        if ((s & ~M.seen[a]) != 0u) continue;
        for (x = 0u; x < M.n_states; x++) if (s >> x & 1u) out |= M.next[a][x];
        for (k = 0u; k < ns; k++) if (SEEN_SETS[k] == out) { dup = 1u; break; }
        if (dup) continue;
        SEEN_SETS[ns++] = out;
        LAYER[1u - cur][nn++] = out;
      }
    }
    if (nn == 0u) return -1;   /* nothing new: no plan exists */
    nl = nn;
    cur = 1u - cur;
  }
  return -2;
}

static void trace(unsigned s, unsigned seq, unsigned len, unsigned *out) {
  unsigned k;
  for (k = 0u; k < len; k++) {
    out[k] = M.sensor[s];
    s = DET[(seq >> k) & 1u][s];
  }
  out[len] = M.sensor[s];
}

int main(void) {
  unsigned t, s, u, a, k;
  unsigned plans_ok = 1u, checked_plans = 0u, impossible_ok = 1u, n_impossible = 0u;
  unsigned merge_sound = 1u, merge_tight = 1u, track_ok = 1u;
  tm_plan_t P;

  printf("time and change, checked independently on random machines\n\n");

  for (t = 0u; t < 400u; t++) {
    unsigned n = 3u + rnd(8u), na = 1u + rnd(3u);
    tm_set_t from, goal;
    int want;
    tm_init(&M, n, na);
    for (s = 0u; s < n; s++) M.sensor[s] = (uint8_t)rnd(2u);
    for (a = 0u; a < na; a++) {
      for (s = 0u; s < n; s++) {
        if (rnd(8u) == 0u) continue;                  /* some transitions never watched */
        tm_watch(&M, s, a, rnd(n));
        if (rnd(4u) == 0u) tm_watch(&M, s, a, rnd(n)); /* and some are uncertain */
      }
    }
    from = (tm_set_t)(1u + rnd((1u << n) - 1u));
    goal = (tm_set_t)rnd(1u << n);
    want = shortest(from, goal);
    if (want == -2) continue;
    if (tm_plan(&M, from, goal, &P) != SM_OK) { plans_ok = 0u; continue; }
    if (want >= 0) {
      checked_plans++;
      if (!P.found || P.length != (unsigned)want || !tm_check_plan(&M, from, goal, &P)) plans_ok = 0u;
    } else {
      n_impossible++;
      if (P.found || !P.proved_impossible) impossible_ok = 0u;
    }
    /* tracking against direct computation */
    for (a = 0u; a < na; a++) {
      tm_set_t direct = 0u;
      unsigned unseen = 0u, un2;
      for (s = 0u; s < n; s++) {
        if (!(from >> s & 1u)) continue;
        if (M.seen[a] >> s & 1u) direct |= M.next[a][s];
        else { unseen++; direct = tm_all(&M); }
      }
      if (tm_step(&M, from, a, &un2) != (direct & tm_all(&M)) || un2 != unseen) track_ok = 0u;
    }
  }
  printf("        (%u plans compared, %u impossible cases)\n", checked_plans, n_impossible);
  check("every plan found is shortest, matches an independent search, and works when run",
        plans_ok && checked_plans > 50u);
  check("every \"no plan exists\" agrees with the independent exhaustive search",
        impossible_ok && n_impossible > 10u);
  check("stepping forward matches direct computation, unwatched cases counted", track_ok);

  /* merging, on fully watched deterministic machines */
  for (t = 0u; t < 300u; t++) {
    unsigned n = 2u + rnd(9u), cls[TM_MAX_STATES], nc;
    tm_init(&M, n, 2u);
    for (s = 0u; s < n; s++) M.sensor[s] = (uint8_t)rnd(2u);
    for (a = 0u; a < 2u; a++) for (s = 0u; s < n; s++) {
      DET[a][s] = rnd(n);
      tm_watch(&M, s, a, DET[a][s]);
    }
    tm_merge(&M, cls, &nc);
    for (s = 0u; s < n; s++) for (u = s + 1u; u < n; u++) {
      unsigned seq, len = n, differ = 0u;
      for (seq = 0u; seq < (1u << len); seq++) {
        unsigned ts[TM_MAX_STATES + 1u], tu[TM_MAX_STATES + 1u];
        trace(s, seq, len, ts);
        trace(u, seq, len, tu);
        for (k = 0u; k <= len; k++) if (ts[k] != tu[k]) { differ = 1u; break; }
        if (differ) break;
      }
      if (cls[s] == cls[u] && differ) merge_sound = 0u;
      if (cls[s] != cls[u] && !differ) merge_tight = 0u;
    }
  }
  check("300 machines: states merged together can never be told apart by acting and sensing",
        merge_sound);
  check("and states kept apart always can be, by some sequence", merge_tight);

  check("NULL arguments are checked errors",
        tm_plan(0, 1u, 1u, &P) == SM_ERR_NULL_ARGUMENT && tm_watch(0, 0u, 0u, 0u) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
