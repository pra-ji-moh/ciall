/*
 * test_learner.c -- the autonomous learner's checks, in C.
 *
 * A port of verify_auto.py. A loop that decides its own next move cannot
 * be validated by one run, so these are checked over EVERY goal and EVERY
 * scene: 6 goals x 32 scenes from a newborn, 576 lying-sensor runs driven
 * through sl_pursue, and the limits pinned as checks so nobody later
 * claims the stronger version.
 *
 * Build and run from this directory (section 6 reads smarsh_learner.c):
 *   zig cc -std=c99 -Wall -Wextra -pedantic smarsh_core.c smarsh_reason.c \
 *       smarsh_learner.c test_learner.c -o test_learner && ./test_learner
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_learner.h"

#define N_SIT 32u
#define N_CAPS 6u
#define N_GOALS 6u

static int n_checks = 0, n_failed = 0;
static char detail[256];

static void check(const char *name, int ok) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok && detail[0] != '\0') printf("          %s\n", detail);
  detail[0] = '\0';
  n_checks++;
  if (!ok) n_failed++;
}

static unsigned a_of(unsigned s) { return (s >> 3) & 3u; }
static unsigned b_of(unsigned s) { return (s >> 1) & 3u; }
static unsigned name_of(unsigned s) { return s & 1u; }

static unsigned cap_answer(unsigned cap, unsigned s) {
  unsigned a = a_of(s), b = b_of(s);
  switch (cap) {
    case 0: return a < b ? 0u : (a == b ? 1u : 2u);
    case 1: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;
    case 2: return name_of(s);
    case 3: return a + b;
    case 4: return a;
    default: return b;
  }
}

static unsigned goal_answer(unsigned g, unsigned s) {
  unsigned a = a_of(s), b = b_of(s);
  switch (g) {
    case 0: return a > b ? 1u : 0u;
    case 1: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;
    case 2: return name_of(s);
    case 3: return a >= 2u ? 1u : 0u;
    case 4: return (a + b) == 4u ? 1u : 0u;
    default: return ((a + b) % 2u == 0u) ? 1u : 0u;
  }
}

static const unsigned DEMO_TRUTH[N_GOALS] = {
  (2u << 3) | (0u << 1) | 0u, (1u << 3) | (2u << 1) | 1u,
  (0u << 3) | (3u << 1) | 1u, (3u << 3) | (1u << 1) | 1u,
  (3u << 3) | (1u << 1) | 1u, (2u << 3) | (2u << 1) | 0u
};

static sl_capacity_t TABLE[N_CAPS];
static sr_query_t GOAL[N_GOALS];
static sl_learner_t L, L2;
static sl_report_t R, R2;

static void setup(void) {
  static const char *names[N_CAPS] = {"more", "close", "name", "sum",
                                      "exact_a", "exact_b"};
  static const unsigned doms[N_CAPS] = {3u, 2u, 2u, 7u, 4u, 4u};
  unsigned c, g, s;
  for (c = 0u; c < N_CAPS; c++) {
    memset(TABLE[c].name, 0, SL_NAME_LEN);
    strncpy(TABLE[c].name, names[c], SL_NAME_LEN - 1u);
    TABLE[c].immediate = (c <= 2u) ? 1 : 0;
    sr_query_init(&TABLE[c].q, N_SIT, doms[c]);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&TABLE[c].q, s, cap_answer(c, s));
  }
  for (g = 0u; g < N_GOALS; g++) {
    sr_query_init(&GOAL[g], N_SIT, 2u);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&GOAL[g], s, goal_answer(g, s));
  }
}

/* ---- source audit helpers ---------------------------------------- */

static char SRC[120000];

static int load_code(const char *path) {
  static char raw[120000];
  FILE *f = fopen(path, "rb");
  size_t n, i, o = 0u;
  if (f == 0) return 0;
  n = fread(raw, 1u, sizeof raw - 1u, f);
  fclose(f);
  for (i = 0u; i < n; i++) {
    if (raw[i] == '/' && i + 1u < n && raw[i + 1u] == '*') {
      i += 2u;
      while (i + 1u < n && !(raw[i] == '*' && raw[i + 1u] == '/')) i++;
      i++;
      continue;
    }
    if (raw[i] == '"') {           /* skip string literals too */
      i++;
      while (i < n && raw[i] != '"') { if (raw[i] == '\\') i++; i++; }
      continue;
    }
    SRC[o++] = raw[i];
  }
  SRC[o] = '\0';
  return 1;
}

static int is_ident(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

/* a read of a field called exactly S, via . or -> */
static int reads_field_S(const char *code) {
  const char *p;
  for (p = code; *p != '\0'; p++) {
    if (p[0] == 'S' && !is_ident(p[1])) {
      if (p > code && p[-1] == '.') return 1;
      if (p > code + 1 && p[-1] == '>' && p[-2] == '-') return 1;
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  unsigned g, t, c, k;
  sl_scene_t scene;
  sm_status_t st;
  int bad;

  setup();
  printf("the autonomous learner, checked over every goal and every scene\n\n");

  /* ---- 1. exhaustive from a newborn -------------------------------- */
  {
    unsigned derived = 0u, terminated = 0u;
    int wrong_any = 0, unproved_any = 0, valued_nonderived = 0, over = 0;
    for (g = 0u; g < N_GOALS; g++) {
      for (t = 0u; t < N_SIT; t++) {
        sl_init(&L, TABLE, N_CAPS, N_SIT);
        sl_scene_truthful(TABLE, N_CAPS, t, &scene);
        st = sl_pursue(&L, &GOAL[g], &scene, &R);
        if (st != SM_OK) continue;
        terminated++;
        if (R.outcome == SL_OUT_DERIVED) {
          derived++;
          if (R.value != goal_answer(g, t)) wrong_any = 1;
          if (!R.proved) unproved_any = 1;
        } else if (R.value != 0u) {
          valued_nonderived = 1;
        }
        if (L.acquisitions > N_CAPS) over = 1;
      }
    }
    check("192 runs (6 goals x 32 scenes) from a newborn: every derivation is "
          "CORRECT against the real situation", !wrong_any);
    check("every derivation carries a witness an independent checker accepts",
          !unproved_any);
    check("nothing that was not derived ever returned a value", !valued_nonderived);
    check("no run ever acquired more capacities than exist", !over);
    sprintf(detail, "%u of 192", terminated);
    check("all 192 terminated inside the stated bound, none hit the invariant "
          "guard", terminated == 192u);
    sprintf(detail, "%u of 192", derived);
    check("and every single one of them reached a derivation", derived == 192u);
  }

  /* ---- 2. acquisition bounded and monotone ------------------------- */
  {
    unsigned sizes[N_GOALS];
    int mono = 1, dup = 0, took_b = 0;
    sl_init(&L, TABLE, N_CAPS, N_SIT);
    for (g = 0u; g < N_GOALS; g++) {
      sr_partition_t p;
      sl_scene_truthful(TABLE, N_CAPS, DEMO_TRUTH[g], &scene);
      sl_pursue(&L, &GOAL[g], &scene, &R);
      sl_partition(&L, SL_NONE, &p);
      sizes[g] = p.n_cells;
      if (g > 0u && sizes[g] < sizes[g - 1u]) mono = 0;
    }
    for (c = 0u; c < L.n_held; c++) {
      for (k = c + 1u; k < L.n_held; k++) if (L.held[c] == L.held[k]) dup = 1;
      if (L.held[c] == 5u) took_b = 1;
    }
    check("worlds it can tell apart never decreases as it learns", mono);
    check("it never acquires a capacity it already has", !dup);
    check("it stops acquiring once the goals are covered: it never took "
          "exact_b at all", !took_b);
  }

  /* ---- 3. transfer ------------------------------------------------- */
  {
    int never_more = 1, some_less = 0;
    for (g = 0u; g < N_GOALS; g++) {
      unsigned fresh, grown;
      sl_scene_truthful(TABLE, N_CAPS, DEMO_TRUTH[g], &scene);
      sl_init(&L, TABLE, N_CAPS, N_SIT);
      sl_pursue(&L, &GOAL[g], &scene, &R);
      fresh = R.acquired + R.measured;
      sl_init(&L2, TABLE, N_CAPS, N_SIT);
      for (c = 0u; c < N_CAPS; c++) sl_grant(&L2, c);
      sl_pursue(&L2, &GOAL[g], &scene, &R2);
      grown = R2.acquired + R2.measured;
      if (grown > fresh) never_more = 0;
      if (grown < fresh) some_less = 1;
    }
    check("a learner that already holds every capacity never does MORE work "
          "than one starting from nothing", never_more);
    check("and on at least one goal it does strictly less", some_less);
  }

  /* ---- 4. the acquisition policy is minimal ------------------------ */
  {
    sr_partition_t p, pc;
    sr_query_t tmp;
    int minimal = 1;
    sl_init(&L, TABLE, N_CAPS, N_SIT);
    sl_scene_truthful(TABLE, N_CAPS, DEMO_TRUTH[0], &scene);
    sl_pursue(&L, &GOAL[0], &scene, &R);
    check("pursuing \"is a bigger than b\" acquires exactly one capacity, the "
          "cheapest that can phrase it", L.n_held == 1u && L.held[0] == 0u);
    sl_partition(&L, SL_NONE, &p);
    for (c = 0u; c < N_CAPS; c++) {
      sl_init(&L2, TABLE, N_CAPS, N_SIT);
      sl_partition(&L2, c, &pc);
      if (sr_project(&pc, &GOAL[0], &tmp) == SM_OK && pc.n_cells < p.n_cells) minimal = 0;
    }
    check("and that capacity really is minimal: no held-out capacity phrases "
          "the goal with fewer worlds", minimal);
  }

  /* ---- 5. contradiction detected, never absorbed ------------------- */
  {
    uint64_t Xs = 31u;
    bad = 0;
    for (k = 0u; k < 400u && !bad; k++) {
      unsigned truth, cap, real, wrong;
      sr_partition_t p;
      sr_state_t s;
      sl_obs_t dropped;
      Xs = Xs * 6364136223846793005ULL + 1442695040888963407ULL;
      truth = (unsigned)((Xs >> 33) % N_SIT);
      Xs = Xs * 6364136223846793005ULL + 1442695040888963407ULL;
      cap = 3u + (unsigned)((Xs >> 33) % 3u);
      sl_init(&L, TABLE, N_CAPS, N_SIT);
      for (c = 0u; c < N_CAPS; c++) sl_grant(&L, c);
      sl_scene_truthful(TABLE, N_CAPS, truth, &scene);
      sl_glance(&L, &scene);
      real = scene.reading[cap];
      wrong = (real + 1u) % TABLE[cap].q.dom;
      L.memory[L.n_memory].cap = cap; L.memory[L.n_memory].answer = real; L.n_memory++;
      L.memory[L.n_memory].cap = cap; L.memory[L.n_memory].answer = wrong; L.n_memory++;
      sl_partition(&L, SL_NONE, &p);
      sl_state(&L, &p, &s);
      if (sr_live_count(&s) != 0u) { bad = 1; break; }
      if (sl_recover(&L, &p, &dropped) != SM_OK) { bad = 2; break; }
      sl_state(&L, &p, &s);
      if (sr_live_count(&s) == 0u) { bad = 3; break; }
    }
    check("400 misleading reports: each one contradicts rather than being "
          "averaged in, and each is recovered from by dropping one claim", bad == 0);
  }

  /* ---- 5b. a broken sensor inside the loop ------------------------- */
  {
    unsigned hung = 0u, right = 0u, wrong = 0u, refused = 0u;
    int wrong_witness_checks = 1;
    for (g = 0u; g < N_GOALS; g++) {
      for (t = 0u; t < N_SIT; t++) {
        for (c = 3u; c <= 5u; c++) {
          sl_init(&L, TABLE, N_CAPS, N_SIT);
          for (k = 0u; k < N_CAPS; k++) sl_grant(&L, k);
          sl_scene_truthful(TABLE, N_CAPS, t, &scene);
          scene.reading[c] = (scene.reading[c] + 1u) % TABLE[c].q.dom;
          st = sl_pursue(&L, &GOAL[g], &scene, &R);
          if (st == SM_ERR_INTERNAL_INVARIANT) { hung++; continue; }
          if (R.outcome != SL_OUT_DERIVED) refused++;
          else if (R.value == goal_answer(g, t)) right++;
          else { wrong++; if (!R.proved) wrong_witness_checks = 0; }
        }
      }
    }
    sprintf(detail, "%u hit the bound", hung);
    check("576 runs with one lying sensor (6 goals x 32 scenes x 3 liars), "
          "driven through sl_pursue: every one terminates inside the stated bound",
          hung == 0u);
    sprintf(detail, "%u right, %u wrong, %u refused", right, wrong, refused);
    check("and at least 97% still end correct, by distrusting the source that "
          "contradicted and routing around it", right * 100u >= 97u * 576u);
    sprintf(detail, "%u of 576", wrong);
    check("LIMIT: a lie consistent with all other evidence produces a derived "
          "WRONG answer -- this is expected, and it happens", wrong > 0u);
    check("LIMIT: and in every such case the witness still checks, because the "
          "answer really does follow from what the learner was told",
          wrong_witness_checks);
    check("so the honest claim is not \"never wrong\" but \"every wrong answer "
          "rests on a false input that the witness names\"",
          wrong > 0u && wrong_witness_checks);

    sl_init(&L, TABLE, N_CAPS, N_SIT);
    for (k = 0u; k < N_CAPS; k++) sl_grant(&L, k);
    sl_scene_truthful(TABLE, N_CAPS, (3u << 3) | (1u << 1) | 1u, &scene);
    scene.reading[3] = 6u;
    st = sl_pursue(&L, &GOAL[4], &scene, &R);
    check("the case that used to loop forever: sum reports an impossible 6, "
          "is distrusted, and the answer comes from counting the second pile",
          st == SM_OK && R.outcome == SL_OUT_DERIVED && R.value == 1u);
  }

  /* ---- 5c. what only the C boundary checks -------------------------- */
  sl_init(&L, TABLE, N_CAPS, N_SIT);
  sl_scene_truthful(TABLE, N_CAPS, 0u, &scene);
  scene.reading[0] = 9u;
  check("a reading outside its capacity's answer space is a checked error "
        "at the boundary, not skipped silently deep in the loop",
        sl_pursue(&L, &GOAL[0], &scene, &R) == SM_ERR_INDEX_OUT_OF_DOMAIN);
  check("a NULL learner is a checked error, not a crash",
        sl_pursue(0, &GOAL[0], &scene, &R) == SM_ERR_NULL_ARGUMENT);

  /* ---- 5d. the outcomes no run had reached --------------------------- */
  {
    /* STUCK. Four situations, x = 0..3. It holds "parity" (read at a
       glance) and "exact" (must be measured); the goal is x >= 2. The
       truth is 3 but parity lies and says even. Measuring exact gives 3,
       which contradicts "even"; the most recent claim is dropped and its
       source distrusted. Parity alone cannot settle x >= 2, the one source
       that could is distrusted, and there is nothing left to learn. */
    static sl_capacity_t T4[2];
    static sr_query_t G4;
    static sl_learner_t L4;
    unsigned x;
    strcpy(T4[0].name, "parity");
    strcpy(T4[1].name, "exact");
    T4[0].immediate = 1;
    T4[1].immediate = 0;
    sr_query_init(&T4[0].q, 4u, 2u);
    sr_query_init(&T4[1].q, 4u, 4u);
    sr_query_init(&G4, 4u, 2u);
    for (x = 0u; x < 4u; x++) {
      sr_query_set(&T4[0].q, x, x % 2u);
      sr_query_set(&T4[1].q, x, x);
      sr_query_set(&G4, x, x >= 2u);
    }
    sl_init(&L4, T4, 2u, 4u);
    sl_grant(&L4, 0u);
    sl_grant(&L4, 1u);
    sl_scene_truthful(T4, 2u, 3u, &scene);
    scene.reading[0] = 0u;   /* the lie */
    st = sl_pursue(&L4, &G4, &scene, &R);
    check("STUCK is reached: the one source that could settle it was "
          "distrusted, and nothing is left to learn",
          st == SM_OK && R.outcome == SL_OUT_STUCK && R.n_log == 2u &&
              R.log[0].kind == SL_EV_MEASURE && R.log[1].kind == SL_EV_RETRACT &&
              R.log[1].cap == 1u);
    check("and STUCK reports the uncertainty it stopped at: 1 bit, x in {0, 2}",
          st == SM_OK && R.H == 1.0);
    L4.n_memory = 0u;
    {
      sr_partition_t p;
      sl_obs_t o;
      sl_partition(&L4, SL_NONE, &p);
      check("recovering with nothing to retract is a checked error, not a "
            "claim dropped out of thin air",
            sl_recover(&L4, &p, &o) == SM_ERR_EMPTY_DOMAIN);
    }
  }
  {
    /* CONTRADICTED is provably unreachable (smarsh_learner.h). The proof
       is also tested against the worst input there is: every sensor
       reports an arbitrary in-range value, not one lie among truths, from
       random starting knowledge. If the proof were wrong, some run would
       return SM_ERR_INTERNAL_INVARIANT. */
    uint32_t lcg = 12345u;
    unsigned runs = 0u, invariant = 0u, other_err = 0u, stuck = 0u;
    for (t = 0u; t < 20000u; t++) {
      sl_init(&L, TABLE, N_CAPS, N_SIT);
      for (c = 0u; c < N_CAPS; c++) {
        lcg = lcg * 1103515245u + 12345u;
        if ((lcg >> 16) & 1u) sl_grant(&L, c);
      }
      for (c = 0u; c < N_CAPS; c++) {
        lcg = lcg * 1103515245u + 12345u;
        scene.reading[c] = (lcg >> 16) % TABLE[c].q.dom;
      }
      lcg = lcg * 1103515245u + 12345u;
      st = sl_pursue(&L, &GOAL[(lcg >> 16) % N_GOALS], &scene, &R);
      runs++;
      if (st == SM_ERR_INTERNAL_INVARIANT) invariant++;
      else if (st != SM_OK) other_err++;
      else if (R.outcome == SL_OUT_STUCK) stuck++;
    }
    sprintf(detail, "%u invariant, %u other errors", invariant, other_err);
    check("20,000 runs where every sensor reports noise: none reaches the "
          "unreachable branch, none errors",
          runs == 20000u && invariant == 0u && other_err == 0u);
    printf("        (%u of those ended STUCK, the honest outcome for noise)\n", stuck);
  }
  {
    /* The learner had only ever run on 32 situations, one 64-bit word.
       Here, 256: every word of the bitset. s = 16 * hi + lo. "hi" is read
       at a glance, "lo" must be measured, "odd" is a decoy that refines
       without enabling. The goal, s % 7 == 0, needs both digits. */
    static sl_capacity_t TB[3];
    static sr_query_t GB;
    static sl_learner_t LB;
    unsigned s, right = 0u, proved = 0u, derived = 0u;
    strcpy(TB[0].name, "odd");
    strcpy(TB[1].name, "hi");
    strcpy(TB[2].name, "lo");
    TB[0].immediate = 1;
    TB[1].immediate = 1;
    TB[2].immediate = 0;
    sr_query_init(&TB[0].q, 256u, 2u);
    sr_query_init(&TB[1].q, 256u, 16u);
    sr_query_init(&TB[2].q, 256u, 16u);
    sr_query_init(&GB, 256u, 2u);
    for (s = 0u; s < 256u; s++) {
      sr_query_set(&TB[0].q, s, s % 2u);
      sr_query_set(&TB[1].q, s, s / 16u);
      sr_query_set(&TB[2].q, s, s % 16u);
      sr_query_set(&GB, s, s % 7u == 0u);
    }
    for (s = 0u; s < 256u; s++) {
      sl_init(&LB, TB, 3u, 256u);
      sl_scene_truthful(TB, 3u, s, &scene);
      if (sl_pursue(&LB, &GB, &scene, &R) != SM_OK) continue;
      if (R.outcome != SL_OUT_DERIVED) continue;
      derived++;
      right += R.value == (s % 7u == 0u);
      proved += R.proved != 0;
    }
    sprintf(detail, "%u derived, %u right, %u proved", derived, right, proved);
    check("256 situations, all four bitset words: a newborn derives s % 7 == 0 "
          "correctly, with a checked witness, in every one of the 256 scenes",
          derived == 256u && right == 256u && proved == 256u);
  }

  /* ---- 6. no scoring anywhere in the learner ------------------------ */
  if (!load_code(argc > 1 ? argv[1] : "smarsh_learner.c")) {
    sprintf(detail, "could not open smarsh_learner.c; run from its directory");
    check("the learner source is readable for the audit", 0);
  } else {
    check("the learner never reads S at all: it decides on counts, not support",
          !reads_field_S(SRC));
    check("and never reads an intensity", strstr(SRC, "intensity") == 0);
    check("and never speculates: an autonomous loop that could guess would "
          "guess its way past every refusal", strstr(SRC, "speculate") == 0);
  }

  printf("\n");
  if (n_failed) {
    printf("%d of %d checks failed\n", n_failed, n_checks);
    return 1;
  }
  printf("all %d checks pass\n", n_checks);
  return 0;
}
