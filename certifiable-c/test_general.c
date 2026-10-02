/*
 * test_general.c -- one core, three systems that have nothing to do with one another.
 *
 *   A. approval logs     a hidden rule decides which applications are approved
 *   B. fault diagnosis   one part of a machine has failed, in one of several ways
 *   C. a small game      what each of four keys does is not told
 *
 * smarsh_general.c is the same for all three: not a line of it knows which it is
 * working on. Each system comes in only as an adapter: its hypotheses in a fixed
 * order, what each says a test would show, and what tests and decisions cost.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "smarsh_general.h"

static unsigned FAILED, CHECKS;

static void check(int ok, const char *what) {
  CHECKS++;
  if (!ok) {
    FAILED++;
    printf("  FAIL  %s\n", what);
  }
}

static sg_core_t CORE;   /* one core, used for all three in turn */

/* run tests chosen by the core until one decision is right under every survivor */
static unsigned settle(const sg_adapter_t *a, unsigned truth, unsigned *decision) {
  unsigned t, worst, gap = 0u, n = 0u;
  while (!sg_certificate(&CORE, decision, &gap) && sg_probe(&CORE, &t, &worst)) {
    unsigned before = sg_count(&CORE);
    unsigned cut = sg_observe(&CORE, t, a->predict(a->ctx, truth, t));
    check(cut > 0u, "a test it chose removed nothing");            /* no act wasted */
    check(sg_count(&CORE) <= worst, "more were left than the worst it counted on");
    check(sg_ledger(&CORE) >= sg_bits(&CORE) - 1e-9, "the ledger fell below the exact count");
    (void)before;
    n++;
  }
  return n;
}

/* ---- A. approval logs ---------------------------------------------------------------- */
/* a record: score 0..9, amount 0..9, region 0..3. The rule: approved if the score is at
   least S, the amount at most A, and the region is not R (R = 4: no region is barred). */

static int a_predict(void *ctx, unsigned h, unsigned test) {
  unsigned S = h % 10u, A = (h / 10u) % 10u, R = h / 100u;
  unsigned score = test % 10u, amount = (test / 10u) % 10u, region = test / 100u;
  (void)ctx;
  return score >= S && amount <= A && region != R;
}

#define A_TARGET (6u + 10u * 4u + 100u * 2u)   /* the application to decide: score 6, amount 4, region 2 */

static unsigned a_cost(void *ctx, unsigned h, unsigned d) {
  return (unsigned)a_predict(ctx, h, A_TARGET) == d ? 0u : 1u;   /* decide as the rule would, or be wrong */
}

static void approval_logs(void) {
  sg_adapter_t a;
  unsigned truth = 5u + 10u * 6u + 100u * 2u, decision = 9u, by_probe, in_order = 0u, gap = 0u, t;
  char head[65];
  memset(&a, 0, sizeof a);
  a.n_hyp = 500u;
  a.n_tests = 400u;
  a.n_decisions = 2u;
  a.n_rows = 3u;
  a.row_size[0] = 10u;
  a.row_size[1] = 10u;
  a.row_size[2] = 5u;
  a.predict = a_predict;
  a.decision_cost = a_cost;

  printf("A. approval logs: 500 possible rules, 400 records that could be pulled\n");
  check(sg_begin(&CORE, &a) == SM_OK, "begin");
  printf("   before any record: %u rules possible, %.2f bits exactly, ledger ceiling %.2f\n", sg_count(&CORE),
         sg_bits(&CORE), sg_ledger(&CORE));
  by_probe = settle(&a, truth, &decision);
  check(sg_certificate(&CORE, &decision, &gap), "no certificate for the application");
  check(decision == (unsigned)a_predict(0, truth, A_TARGET), "decided the application wrongly");
  check(sg_possible(&CORE, truth), "the true rule was ruled out");
  printf("   records pulled, each chosen for its worst case: %u; then %u rules still possible (%.2f bits; "
         "ledger %.2f), and every one of them %s the application: decided, with nothing left to chance\n",
         by_probe, sg_count(&CORE), sg_bits(&CORE), sg_ledger(&CORE), decision ? "approves" : "refuses");
  sg_head(&CORE, head);
  check(sg_replay(&a, CORE.obs_test, CORE.obs_out, CORE.n_obs, CORE.head), "the trace did not replay");
  {
    int other[SG_MAX_OBS];
    memcpy(other, CORE.obs_out, sizeof other);
    other[0] = !other[0];
    check(!sg_replay(&a, CORE.obs_test, other, CORE.n_obs, CORE.head), "a changed record replayed the same");
  }
  printf("   trace %.16s...: replays byte for byte; one record altered and it does not\n", head);

  /* the same, pulling records in the order they are filed */
  check(sg_begin(&CORE, &a) == SM_OK, "begin again");
  for (t = 0u; t < a.n_tests && !sg_certificate(&CORE, &decision, &gap); t++) {
    (void)sg_observe(&CORE, t, a_predict(0, truth, t));
    in_order++;
  }
  printf("   pulling them in filing order instead: %u records before the same proof\n", in_order);
  check(by_probe <= in_order, "choosing tests took more than taking them in order");

  /* one record entered wrongly: nothing can be true of all of them. Which ones clash? */
  check(sg_begin(&CORE, &a) == SM_OK, "begin for the conflict");
  {
    unsigned which[16], n, i, bad_at, found = 0u, worst;
    while (sg_probe(&CORE, &t, &worst)) (void)sg_observe(&CORE, t, a_predict(0, truth, t));
    bad_at = CORE.n_obs;
    (void)sg_observe(&CORE, A_TARGET, !a_predict(0, truth, A_TARGET));   /* the wrong entry */
    check(sg_count(&CORE) == 0u, "a wrong record left something standing");
    n = sg_conflict(&CORE, which, 16u);
    for (i = 0u; i < n; i++) found += (which[i] == bad_at);
    check(n >= 2u && found == 1u, "the conflict did not name the wrong record");
    printf("   one record entered wrongly among %u: no rule fits them all. The smallest set that cannot "
           "coexist is %u records, and the wrong one is among them\n", CORE.n_obs, n);
  }
}

/* ---- B. fault diagnosis --------------------------------------------------------------- */
/* 8 parts, each can fail 3 ways: 24 faults. 10 measurements, read 0, 1 or 2, some dearer. */

static int b_predict(void *ctx, unsigned h, unsigned test) {
  unsigned part = h % 8u, mode = h / 8u;
  (void)ctx;
  return (int)((part * (test + 3u) + mode * (test + 1u) + part * mode + (part >> (test % 3u))) % 3u);
}

static unsigned b_test_cost(void *ctx, unsigned test) {
  (void)ctx;
  return 1u + test % 4u;
}

static unsigned b_cost(void *ctx, unsigned h, unsigned d) {
  (void)ctx;
  return (h % 8u) == d ? 0u : 5u;   /* replace the part that failed, or a part that had not */
}

static void fault_diagnosis(void) {
  sg_adapter_t a;
  unsigned truth = 5u + 8u * 2u, decision = 99u, n, gap = 0u;
  memset(&a, 0, sizeof a);
  a.n_hyp = 24u;
  a.n_tests = 10u;
  a.n_decisions = 8u;
  a.n_rows = 2u;
  a.row_size[0] = 8u;
  a.row_size[1] = 3u;
  a.predict = b_predict;
  a.test_cost = b_test_cost;
  a.decision_cost = b_cost;

  printf("B. fault diagnosis: 24 possible faults, 10 measurements of differing cost\n");
  check(sg_begin(&CORE, &a) == SM_OK, "begin");
  n = settle(&a, truth, &decision);
  if (sg_certificate(&CORE, &decision, &gap)) {
    check(decision == truth % 8u, "replaced the wrong part");
    if (sg_count(&CORE) == 1u) {
      printf("   %u measurements (cost %lu): one fault left, in part %u: replace it\n", n, CORE.spent, decision);
    } else {
      printf("   %u measurements (cost %lu): %u faults still possible, all in part %u: replace it. Which way "
             "it failed is not settled, and does not need to be\n", n, CORE.spent, sg_count(&CORE), decision);
    }
  } else {
    /* no measurement can tell what is left apart: it says so, and what the least regret is */
    check(sg_possible(&CORE, truth), "the true fault was ruled out");
    printf("   %u measurements (cost %lu): %u faults left that no measurement tells apart; the least regret "
           "is to replace part %u (worst case %u)\n", n, CORE.spent, sg_count(&CORE), decision, gap);
  }
  check(sg_possible(&CORE, truth), "the true fault was ruled out");
}

/* ---- C. a small game ------------------------------------------------------------------- */
/* a 3 by 3 board; four keys; each key is one of: stay, up, down, left, right. 625 games. */

static unsigned moved(unsigned move, unsigned pos) {
  unsigned r = pos / 3u, c = pos % 3u;
  if (move == 1u && r > 0u) r--;
  if (move == 2u && r < 2u) r++;
  if (move == 3u && c > 0u) c--;
  if (move == 4u && c < 2u) c++;
  return r * 3u + c;
}

static unsigned key_move(unsigned h, unsigned key) {
  unsigned k;
  for (k = 0u; k < key; k++) h /= 5u;
  return h % 5u;
}

static int c_predict(void *ctx, unsigned h, unsigned test) {
  (void)ctx;
  return (int)moved(key_move(h, test / 9u), test % 9u);   /* key test/9 pressed at place test%9 */
}

/* plans: one key (0..3), or two (4 + first*4 + second). From the corner, to the middle. */
static unsigned c_cost(void *ctx, unsigned h, unsigned d) {
  unsigned pos = 0u;
  (void)ctx;
  if (d < 4u) return moved(key_move(h, d), pos) == 4u ? 1u : 9u;
  pos = moved(key_move(h, (d - 4u) / 4u), pos);
  return moved(key_move(h, (d - 4u) % 4u), pos) == 4u ? 2u : 9u;
}

static unsigned c_distance(void *ctx, unsigned h1, unsigned h2) {
  unsigned k, n = 0u;
  (void)ctx;
  for (k = 0u; k < 4u; k++) n += key_move(h1, k) != key_move(h2, k);
  return n;   /* how many keys do something different */
}

static void small_game(void) {
  sg_adapter_t a;
  unsigned truth = 4u + 5u * 2u + 25u * 3u + 125u * 1u;   /* keys: right, down, left, up */
  unsigned changed = 0u + 5u * 2u + 25u * 3u + 125u * 1u; /* the first key now does nothing */
  unsigned decision = 99u, n, gap = 0u, since, k, nearest = 0u, dist = 0u, ball = 0u, left;
  memset(&a, 0, sizeof a);
  a.n_hyp = 625u;
  a.n_tests = 36u;
  a.n_decisions = 20u;
  a.n_rows = 4u;
  a.row_size[0] = a.row_size[1] = a.row_size[2] = a.row_size[3] = 5u;
  a.predict = c_predict;
  a.decision_cost = c_cost;
  a.distance = c_distance;

  printf("C. a small game: 625 possible games, 36 things to try, 20 plans\n");
  check(sg_begin(&CORE, &a) == SM_OK, "begin");
  n = settle(&a, truth, &decision);
  check(sg_certificate(&CORE, &decision, &gap), "no certificate for a plan");
  check(c_cost(0, truth, decision) == 2u, "the plan does not reach the middle in two");
  printf("   %u tries: %u games still possible (%.2f bits), and one plan is the best in every one of them: "
         "it stops trying and plays it\n", n, sg_count(&CORE), sg_bits(&CORE));

  /* the game changes under it: the first key, which its plan leans on, stops doing anything */
  since = CORE.n_obs;
  for (k = 0u; k < 4u; k++) (void)sg_observe(&CORE, k * 9u + 4u, c_predict(0, changed, k * 9u + 4u));
  check(sg_count(&CORE) == 0u, "the old game survived a change");
  {
    unsigned which[16];
    unsigned nc = sg_conflict(&CORE, which, 16u);
    check(nc >= 2u, "no conflict reported after a change");
    printf("   the game changes: nothing fits any more, and %u observations are named that cannot coexist\n", nc);
  }
  left = sg_carry(&CORE, truth, since, &nearest, &dist, &ball);
  check(left == 1u && nearest == changed && dist == 1u && ball == 17u, "the carry did not find the changed game");
  printf("   looking near the old game first: %u game fits the evidence since, %u key away from the old one, "
         "inside a ball of %u games (%.2f bits) instead of all 625 (%.2f bits)\n", left, dist, ball,
         log2((double)ball), log2(625.0));
}

int main(void) {
  approval_logs();
  fault_diagnosis();
  small_game();
  printf("\none core, three systems, no line of the core changed between them\n");
  printf("%u checks, %u checks failed\n", CHECKS, FAILED);
  return FAILED != 0u;
}
