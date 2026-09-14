/*
 * test_null_args.c -- every NULL-argument branch in the C, exercised.
 *
 * Each public function checks its pointer arguments first and returns
 * SM_ERR_NULL_ARGUMENT rather than crashing. Those branches had almost no
 * coverage (2 of 33). Here every pointer argument of every such function
 * is passed as NULL on its own, with the others valid, so every disjunct
 * of every `if (a == 0 || b == 0 ...)` is taken at least once.
 *
 * "Every" is checked, not remembered: the audit at the end reads every
 * reasoning source file (the list is in main), finds each
 * `return SM_ERR_NULL_ARGUMENT`, counts the `== 0` tests in the condition
 * guarding it, and fails unless this file exercised exactly that many
 * NULL cases for that function. A new pointer check without a test here
 * fails the build.
 *
 * Run from certifiable-c (it reads the sources).
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_analogy.h"
#include "smarsh_concept.h"
#include "smarsh_informant.h"
#include "smarsh_law.h"
#include "smarsh_proof.h"
#include "smarsh_abstract.h"
#include "smarsh_time.h"
#include "smarsh_space.h"
#include "smarsh_frame.h"
#include "smarsh_object.h"
#include "smarsh_play.h"
#include "smarsh_grow.h"
#include "smarsh_explore.h"
#include "smarsh_core.h"
#include "smarsh_learner.h"
#include "smarsh_reason.h"

#define MAX_FN 64u

static unsigned n_checks = 0u, n_fail = 0u;
static struct { const char *name; unsigned cases; } TESTED[MAX_FN];
static unsigned n_tested = 0u;

static void expect_null(const char *fn, sm_status_t got) {
  unsigned i;
  n_checks++;
  if (got != SM_ERR_NULL_ARGUMENT) {
    printf("  FAIL  %s: a NULL argument returned %d, not SM_ERR_NULL_ARGUMENT\n", fn, (int)got);
    n_fail++;
  }
  for (i = 0u; i < n_tested; i++) {
    if (strcmp(TESTED[i].name, fn) == 0) { TESTED[i].cases++; return; }
  }
  TESTED[n_tested].name = fn;
  TESTED[n_tested].cases = 1u;
  n_tested++;
}

/* Valid-looking objects. The null checks run before anything reads
   them, so zeroed contents are enough; the point is the pointer. */
static sm_possibility_t P;
static sm_result_t RES;
static sm_ancestry_t ANC;
static sr_state_t ST, ST2;
static sr_query_t Q, Q2, Q3;
static sr_op1_t OP1;
static sr_op2_t OP2;
static sr_result_t RR;
static sr_settle_t SET;
static sr_partition_t PART;
static sr_probe_t PROBE;
static sl_learner_t L;
static sl_capacity_t CAP[1];
static sl_scene_t SCENE;
static sl_obs_t OBS;
static sl_report_t REP;
static uint64_t U64[SR_WORLD_WORDS];
static unsigned IDX;

#define N 0   /* reads better than a bare 0 in the calls below */

static void core(void) {
  expect_null("sm_ancestry_add", sm_ancestry_add(N, 0u, 1u));
  expect_null("sm_init", sm_init(N, 2u));
  expect_null("sm_eliminate", sm_eliminate(N, 0u));
  expect_null("sm_restrict_to", sm_restrict_to(N, 0u));
  expect_null("sm_decide", sm_decide(N, 0.0, 1u, SM_INTENSITY_SUPPORT, 0u, &RES));
  expect_null("sm_decide", sm_decide(&P, 0.0, 1u, SM_INTENSITY_SUPPORT, 0u, N));
}

static void reason(void) {
  expect_null("sr_state_init", sr_state_init(N, 4u));
  expect_null("sr_eliminate", sr_eliminate(N, 0u));
  expect_null("sr_query_init", sr_query_init(N, 4u, 2u));
  expect_null("sr_query_set", sr_query_set(N, 0u, 0u));

  expect_null("sr_map1", sr_map1(N, &Q, &Q2));
  expect_null("sr_map1", sr_map1(&OP1, N, &Q2));
  expect_null("sr_map1", sr_map1(&OP1, &Q, N));

  expect_null("sr_map2", sr_map2(N, &Q, &Q2, &Q3));
  expect_null("sr_map2", sr_map2(&OP2, N, &Q2, &Q3));
  expect_null("sr_map2", sr_map2(&OP2, &Q, N, &Q3));
  expect_null("sr_map2", sr_map2(&OP2, &Q, &Q2, N));

  expect_null("sr_image", sr_image(N, &ST, U64));
  expect_null("sr_image", sr_image(&Q, N, U64));
  expect_null("sr_image", sr_image(&Q, &ST, N));

  expect_null("sr_ask", sr_ask(N, &ST, &RR));
  expect_null("sr_ask", sr_ask(&Q, N, &RR));
  expect_null("sr_ask", sr_ask(&Q, &ST, N));

  expect_null("sr_range", sr_range(N, &ST, U64));
  expect_null("sr_range", sr_range(&Q, N, U64));
  expect_null("sr_range", sr_range(&Q, &ST, N));

  expect_null("sr_ask2", sr_ask2(N, &Q, &Q2, &ST, &Q3, &RR));
  expect_null("sr_ask2", sr_ask2(&OP2, N, &Q2, &ST, &Q3, &RR));
  expect_null("sr_ask2", sr_ask2(&OP2, &Q, N, &ST, &Q3, &RR));
  expect_null("sr_ask2", sr_ask2(&OP2, &Q, &Q2, N, &Q3, &RR));
  expect_null("sr_ask2", sr_ask2(&OP2, &Q, &Q2, &ST, N, &RR));
  expect_null("sr_ask2", sr_ask2(&OP2, &Q, &Q2, &ST, &Q3, N));

  expect_null("sr_observe", sr_observe(N, &Q, 0u));
  expect_null("sr_observe", sr_observe(&ST, N, 0u));

  expect_null("sr_settle_begin", sr_settle_begin(N, &ST, &Q));
  expect_null("sr_settle_begin", sr_settle_begin(&SET, N, &Q));
  expect_null("sr_settle_begin", sr_settle_begin(&SET, &ST, N));

  expect_null("sr_settle_step", sr_settle_step(N, &ST, &Q, &Q2, 0u));
  expect_null("sr_settle_step", sr_settle_step(&SET, N, &Q, &Q2, 0u));
  expect_null("sr_settle_step", sr_settle_step(&SET, &ST, N, &Q2, 0u));
  expect_null("sr_settle_step", sr_settle_step(&SET, &ST, &Q, N, 0u));

  expect_null("sr_settle_commit", sr_settle_commit(N, &Q, &ST, &RR));
  expect_null("sr_settle_commit", sr_settle_commit(&SET, N, &ST, &RR));
  expect_null("sr_settle_commit", sr_settle_commit(&SET, &Q, N, &RR));
  expect_null("sr_settle_commit", sr_settle_commit(&SET, &Q, &ST, N));

  expect_null("sr_refine", sr_refine(N, 1u, 4u, &PART));
  expect_null("sr_refine", sr_refine(&Q, 1u, 4u, N));

  expect_null("sr_project", sr_project(N, &Q, &Q2));
  expect_null("sr_project", sr_project(&PART, N, &Q2));
  expect_null("sr_project", sr_project(&PART, &Q, N));

  expect_null("sr_probe", sr_probe(N, &Q, &Q2, &PROBE));
  expect_null("sr_probe", sr_probe(&ST, N, &Q2, &PROBE));
  expect_null("sr_probe", sr_probe(&ST, &Q, N, &PROBE));
  expect_null("sr_probe", sr_probe(&ST, &Q, &Q2, N));

  expect_null("sr_choose", sr_choose(N, &Q, &Q2, 1u, SR_ASK_GUARANTEE, &IDX, &PROBE));
  expect_null("sr_choose", sr_choose(&ST, N, &Q2, 1u, SR_ASK_GUARANTEE, &IDX, &PROBE));
  expect_null("sr_choose", sr_choose(&ST, &Q, N, 1u, SR_ASK_GUARANTEE, &IDX, &PROBE));
  expect_null("sr_choose", sr_choose(&ST, &Q, &Q2, 1u, SR_ASK_GUARANTEE, N, &PROBE));
  expect_null("sr_choose", sr_choose(&ST, &Q, &Q2, 1u, SR_ASK_GUARANTEE, &IDX, N));

  expect_null("sr_retract", sr_retract(N, &ST2, &ANC, 0u));
  expect_null("sr_retract", sr_retract(&ST, N, &ANC, 0u));
  expect_null("sr_retract", sr_retract(&ST, &ST2, N, 0u));

  expect_null("sr_speculate", sr_speculate(N, &Q, 0.0, 1u, SM_INTENSITY_SUPPORT, 0u, &ANC, &RR));
  expect_null("sr_speculate", sr_speculate(&ST, N, 0.0, 1u, SM_INTENSITY_SUPPORT, 0u, &ANC, &RR));
  expect_null("sr_speculate", sr_speculate(&ST, &Q, 0.0, 1u, SM_INTENSITY_SUPPORT, 0u, N, &RR));
  expect_null("sr_speculate", sr_speculate(&ST, &Q, 0.0, 1u, SM_INTENSITY_SUPPORT, 0u, &ANC, N));
}

static sc_discovery_t DISC;
static sc_op_t SCOP = {"x", 2u, 1u, 1u, 1u, {0}, {0}, 0u, 0u};

static void concept(void) {
  expect_null("sc_discover", sc_discover(N, 1u, &Q2, 4u, &DISC));
  expect_null("sc_discover", sc_discover(&Q, 1u, N, 4u, &DISC));
  expect_null("sc_discover", sc_discover(&Q, 1u, &Q2, 4u, N));
  expect_null("sc_apply", sc_apply(N, &Q, &Q2, 4u, &Q3, &IDX));
  expect_null("sc_apply", sc_apply(&SCOP, N, &Q2, 4u, &Q3, &IDX));
  expect_null("sc_apply", sc_apply(&SCOP, &Q, &Q2, 4u, N, &IDX));
  expect_null("sc_apply", sc_apply(&SCOP, &Q, &Q2, 4u, &Q3, N));
  expect_null("sc_apply", sc_apply(&SCOP, &Q, N, 4u, &Q3, &IDX));   /* arity 2 needs b */
}

static sa_struct_t SAS;
static sa_library_t SALIB;
static sa_found_t SAF;
static const char *const SANAMES[1] = {"f"};
static const unsigned SADOMS[1] = {2u};
static unsigned sa_fn(const unsigned *v) { return v[0]; }

static void analogy(void) {
  expect_null("sa_define", sa_define(N, "q", "d", 1u, SANAMES, SADOMS, 2u, sa_fn));
  expect_null("sa_define", sa_define(&SAS, N, "d", 1u, SANAMES, SADOMS, 2u, sa_fn));
  expect_null("sa_define", sa_define(&SAS, "q", N, 1u, SANAMES, SADOMS, 2u, sa_fn));
  expect_null("sa_define", sa_define(&SAS, "q", "d", 1u, N, SADOMS, 2u, sa_fn));
  expect_null("sa_define", sa_define(&SAS, "q", "d", 1u, SANAMES, N, 2u, sa_fn));
  expect_null("sa_define", sa_define(&SAS, "q", "d", 1u, SANAMES, SADOMS, 2u, N));
  expect_null("sa_add", sa_add(N, &SAS));
  expect_null("sa_add", sa_add(&SALIB, N));
  expect_null("sa_find", sa_find(N, &SAS, &SAF));
  expect_null("sa_find", sa_find(&SALIB, N, &SAF));
  expect_null("sa_find", sa_find(&SALIB, &SAS, N));
}

static sk_store_t SKS;
static unsigned SKID;
static const unsigned SKV[1] = {0u};

static void informant(void) {
  expect_null("sk_source", sk_source(N, "s", &SKID));
  expect_null("sk_source", sk_source(&SKS, N, &SKID));
  expect_null("sk_source", sk_source(&SKS, "s", N));
  expect_null("sk_tell", sk_tell(N, 0u, &SAS));
  expect_null("sk_tell", sk_tell(&SKS, 0u, N));
  expect_null("sk_observe", sk_observe(N, "q", SKV, 0u, &SKID));
  expect_null("sk_observe", sk_observe(&SKS, N, SKV, 0u, &SKID));
  expect_null("sk_observe", sk_observe(&SKS, "q", N, 0u, &SKID));
  expect_null("sk_observe", sk_observe(&SKS, "q", SKV, 0u, N));
  {
    uint32_t src;
    expect_null("sk_best", sk_best(N, "q", &SAS, &src, &SKID));
    expect_null("sk_best", sk_best(&SKS, N, &SAS, &src, &SKID));
    expect_null("sk_best", sk_best(&SKS, "q", N, &src, &SKID));
    expect_null("sk_best", sk_best(&SKS, "q", &SAS, N, &SKID));
    expect_null("sk_best", sk_best(&SKS, "q", &SAS, &src, N));
  }
}

static lw_book_t LWB;
static sc_op_t LWOP;

static void law(void) {
  uint64_t v;
  uint32_t lp;
  unsigned ix;
  expect_null("lw_init", lw_init(N, &LWOP, 1u, "s"));
  expect_null("lw_init", lw_init(&LWB, N, 1u, "s"));
  expect_null("lw_init", lw_init(&LWB, &LWOP, 1u, N));
  expect_null("lw_discover", lw_discover(N, &LWOP, "n", "s", &ix));
  expect_null("lw_discover", lw_discover(&LWB, N, "n", "s", &ix));
  expect_null("lw_discover", lw_discover(&LWB, &LWOP, N, "s", &ix));
  expect_null("lw_discover", lw_discover(&LWB, &LWOP, "n", N, &ix));
  expect_null("lw_discover", lw_discover(&LWB, &LWOP, "n", "s", N));
  expect_null("lw_eval", lw_eval(N, 0u, 1u, 1u, &v, &lp));
  expect_null("lw_eval", lw_eval(&LWB, 0u, 1u, 1u, N, &lp));
  expect_null("lw_eval", lw_eval(&LWB, 0u, 1u, 1u, &v, N));
}

static pf_book_t PFB;
static pf_expr_t PFE;
static pf_poly_t PFP;
static pf_verdict_t PFV;

static void proof(void) {
  expect_null("pf_laws", pf_laws(N, &PFB));
  expect_null("pf_laws", pf_laws(&LWB, N));
  expect_null("pf_normal", pf_normal(N, &PFB, &PFE, &PFP));
  expect_null("pf_normal", pf_normal(&LWB, N, &PFE, &PFP));
  expect_null("pf_normal", pf_normal(&LWB, &PFB, N, &PFP));
  expect_null("pf_normal", pf_normal(&LWB, &PFB, &PFE, N));
  expect_null("pf_prove", pf_prove(N, &PFB, &PFE, &PFE, &PFV));
  expect_null("pf_prove", pf_prove(&LWB, N, &PFE, &PFE, &PFV));
  expect_null("pf_prove", pf_prove(&LWB, &PFB, N, &PFE, &PFV));
  expect_null("pf_prove", pf_prove(&LWB, &PFB, &PFE, N, &PFV));
  expect_null("pf_prove", pf_prove(&LWB, &PFB, &PFE, &PFE, N));
}

static ab_raw_t ABR;
static ab_result_t ABRES;

static void abstraction(void) {
  expect_null("ab_name", ab_name(N, 0u, "a"));
  expect_null("ab_name", ab_name(&ABR, 0u, N));
  expect_null("ab_observe", ab_observe(N, 0u, 0u));
  expect_null("ab_abstract", ab_abstract(N, &ABRES));
  expect_null("ab_abstract", ab_abstract(&ABR, N));
}

static tm_model_t TMM;
static tm_plan_t TMP;
static unsigned TMC[TM_MAX_STATES];

static void timing(void) {
  expect_null("tm_init", tm_init(N, 2u, 1u));
  expect_null("tm_watch", tm_watch(N, 0u, 0u, 0u));
  expect_null("tm_plan", tm_plan(N, 1u, 1u, &TMP));
  expect_null("tm_plan", tm_plan(&TMM, 1u, 1u, N));
  expect_null("tm_merge", tm_merge(N, TMC, &IDX));
  expect_null("tm_merge", tm_merge(&TMM, N, &IDX));
  expect_null("tm_merge", tm_merge(&TMM, TMC, N));
}

static sx_theory_t SXT;
static sx_answer_t SXA;
static sx_roots_t SXR;

static void space(void) {
  sx_opts_t o = sx_default_opts();
  unsigned r;
  expect_null("sx_var", sx_var(N, "x", SX_REAL, 0.0, 1.0, &r));
  expect_null("sx_var", sx_var(&SXT, N, SX_REAL, 0.0, 1.0, &r));
  expect_null("sx_var", sx_var(&SXT, "x", SX_REAL, 0.0, 1.0, N));
  expect_null("sx_parse", sx_parse(N, "1", &r));
  expect_null("sx_parse", sx_parse(&SXT, N, &r));
  expect_null("sx_parse", sx_parse(&SXT, "1", N));
  expect_null("sx_require", sx_require(N, "1 < 2"));
  expect_null("sx_require", sx_require(&SXT, N));
  expect_null("sx_ask", sx_ask(N, SX_NONE, &o, &SXA));
  expect_null("sx_ask", sx_ask(&SXT, SX_NONE, N, &SXA));
  expect_null("sx_ask", sx_ask(&SXT, SX_NONE, &o, N));
  expect_null("sx_ask_plain", sx_ask_plain(N, SX_NONE, &o, &SXA));
  expect_null("sx_ask_plain", sx_ask_plain(&SXT, SX_NONE, N, &SXA));
  expect_null("sx_ask_plain", sx_ask_plain(&SXT, SX_NONE, &o, N));
  expect_null("sx_diff", sx_diff(N, 0u, 0u, &r));
  expect_null("sx_diff", sx_diff(&SXT, 0u, 0u, N));
  expect_null("sx_roots", sx_roots(N, 0u, 0u, 0.0, 1.0, 1e-6, &SXR));
  expect_null("sx_roots", sx_roots(&SXT, 0u, 0u, 0.0, 1.0, 1e-6, N));
  expect_null("sx_define", sx_define(N, "d", "1"));
  expect_null("sx_define", sx_define(&SXT, N, "1"));
  expect_null("sx_define", sx_define(&SXT, "d", N));
  expect_null("sx_source", sx_source(N, "s", &r));
  expect_null("sx_source", sx_source(&SXT, N, &r));
  expect_null("sx_source", sx_source(&SXT, "s", N));
  expect_null("sx_claim", sx_claim(N, "1 < 2", 0u, 0));
  expect_null("sx_claim", sx_claim(&SXT, N, 0u, 0));
  {
    uint32_t rests;
    int proved;
    expect_null("sx_ask_grounded", sx_ask_grounded(N, SX_NONE, &o, &SXA, &rests, &proved));
    expect_null("sx_ask_grounded", sx_ask_grounded(&SXT, SX_NONE, N, &SXA, &rests, &proved));
    expect_null("sx_ask_grounded", sx_ask_grounded(&SXT, SX_NONE, &o, N, &rests, &proved));
    expect_null("sx_ask_grounded", sx_ask_grounded(&SXT, SX_NONE, &o, &SXA, N, &proved));
    expect_null("sx_ask_grounded", sx_ask_grounded(&SXT, SX_NONE, &o, &SXA, &rests, N));
  }
  {
    static sx_solutions_t SXS;
    expect_null("sx_solve", sx_solve(N, &r, 1u, 1e-6, &SXS));
    expect_null("sx_solve", sx_solve(&SXT, N, 1u, 1e-6, &SXS));
    expect_null("sx_solve", sx_solve(&SXT, &r, 1u, 1e-6, N));
  }
}

static void learner(void) {
  expect_null("sl_init", sl_init(N, CAP, 1u, 4u));
  expect_null("sl_init", sl_init(&L, N, 1u, 4u));
  expect_null("sl_scene_truthful", sl_scene_truthful(N, 1u, 0u, &SCENE));
  expect_null("sl_scene_truthful", sl_scene_truthful(CAP, 1u, 0u, N));
  expect_null("sl_grant", sl_grant(N, 0u));
  expect_null("sl_partition", sl_partition(N, SL_NONE, &PART));
  expect_null("sl_partition", sl_partition(&L, SL_NONE, N));
  expect_null("sl_state", sl_state(N, &PART, &ST));
  expect_null("sl_state", sl_state(&L, N, &ST));
  expect_null("sl_state", sl_state(&L, &PART, N));
  expect_null("sl_glance", sl_glance(N, &SCENE));
  expect_null("sl_glance", sl_glance(&L, N));
  expect_null("sl_recover", sl_recover(N, &PART, &OBS));
  expect_null("sl_recover", sl_recover(&L, N, &OBS));
  expect_null("sl_recover", sl_recover(&L, &PART, N));
  expect_null("sl_pursue", sl_pursue(N, &Q, &SCENE, &REP));
  expect_null("sl_pursue", sl_pursue(&L, N, &SCENE, &REP));
  expect_null("sl_pursue", sl_pursue(&L, &Q, N, &REP));
  expect_null("sl_pursue", sl_pursue(&L, &Q, &SCENE, N));
}

/* ---- the audit ------------------------------------------------------ */

static char SRC[200000];

static unsigned tested_cases(const char *fn, size_t len) {
  unsigned i;
  for (i = 0u; i < n_tested; i++) {
    if (strlen(TESTED[i].name) == len && strncmp(TESTED[i].name, fn, len) == 0) return TESTED[i].cases;
  }
  return 0u;
}

static unsigned audit(const char *path, unsigned *n_branches) {
  FILE *f = fopen(path, "rb");
  size_t n;
  const char *p, *ret;
  unsigned bad = 0u;
  if (f == NULL) { printf("  FAIL  cannot open %s (run from certifiable-c)\n", path); return 1u; }
  n = fread(SRC, 1u, sizeof SRC - 1u, f);
  fclose(f);
  SRC[n] = '\0';
  for (p = SRC; (ret = strstr(p, "return SM_ERR_NULL_ARGUMENT")) != NULL; p = ret + 1) {
    const char *fn_start = NULL, *q, *cond;
    size_t fn_len = 0u;
    unsigned zeros = 0u, have;
    /* enclosing function: the last line before this starting "sm_status_t " */
    for (q = ret; q > SRC; q--) {
      if (q[-1] == '\n' && strncmp(q, "sm_status_t ", 12u) == 0) {
        fn_start = q + 12;
        fn_len = strcspn(fn_start, "(");
        break;
      }
    }
    /* the guarding condition: from the last "if (" before the return */
    for (cond = ret; cond > SRC && strncmp(cond, "if (", 4u) != 0; cond--) {}
    for (q = cond; q < ret; q++) if (strncmp(q, "== 0", 4u) == 0) zeros++;
    (*n_branches)++;
    if (fn_start == NULL) { printf("  FAIL  %s: a null branch outside any function?\n", path); bad++; continue; }
    have = tested_cases(fn_start, fn_len);
    if (have != zeros) {
      printf("  FAIL  %.*s checks %u pointer(s) for NULL; this file tests %u\n", (int)fn_len,
             fn_start, zeros, have);
      bad++;
    }
  }
  return bad;
}


static fr_situation_t FRS;
static fr_frame_t FRF;

static void frame(void) {
  double row[2];
  unsigned r;
  row[0] = 1.0;
  row[1] = 2.0;
  expect_null("fr_reading", fr_reading(N, "a", 0.0, 1.0, 1, &r));
  expect_null("fr_reading", fr_reading(&FRS, N, 0.0, 1.0, 1, &r));
  expect_null("fr_reading", fr_reading(&FRS, "a", 0.0, 1.0, 1, N));
  expect_null("fr_outcome", fr_outcome(N, "y", 0.0, 1.0, 1));
  expect_null("fr_outcome", fr_outcome(&FRS, N, 0.0, 1.0, 1));
  expect_null("fr_observe", fr_observe(N, row, 0.0));
  expect_null("fr_observe", fr_observe(&FRS, N, 0.0));
  expect_null("fr_formulate", fr_formulate(N, &FRF));
  expect_null("fr_formulate", fr_formulate(&FRS, N));
  expect_null("fr_to_theory", fr_to_theory(N, &FRF, &SXT));
  expect_null("fr_to_theory", fr_to_theory(&FRS, N, &SXT));
  expect_null("fr_to_theory", fr_to_theory(&FRS, &FRF, N));
  {
    fr_invariants_t inv;
    double xs[4];
    xs[0] = 1.0; xs[1] = 2.0; xs[2] = 3.0; xs[3] = 4.0;
    expect_null("fr_invariants", fr_invariants(N, &inv));
    expect_null("fr_invariants", fr_invariants(&FRS, N));
    expect_null("fr_recurrence", fr_recurrence(N, 4u, 0.0, 9.0, 1, &FRS, &FRF));
    expect_null("fr_recurrence", fr_recurrence(xs, 4u, 0.0, 9.0, 1, N, &FRF));
    expect_null("fr_recurrence", fr_recurrence(xs, 4u, 0.0, 9.0, 1, &FRS, N));
  }
}


static ob_world_t OBW;
static ob_law_t OBL;
static ob_facts_t OBF;

static void object(void) {
  double a[1];
  unsigned r;
  a[0] = 0.0;
  expect_null("ob_attr", ob_attr(N, "v", 0.0, 1.0, 1, &r));
  expect_null("ob_attr", ob_attr(&OBW, N, 0.0, 1.0, 1, &r));
  expect_null("ob_attr", ob_attr(&OBW, "v", 0.0, 1.0, 1, N));
  expect_null("ob_outcome_name", ob_outcome_name(N, "y"));
  expect_null("ob_outcome_name", ob_outcome_name(&OBW, N));
  expect_null("ob_scene", ob_scene(N));
  expect_null("ob_item", ob_item(N, a, &r));
  expect_null("ob_item", ob_item(&OBW, N, &r));
  expect_null("ob_item", ob_item(&OBW, a, N));
  expect_null("ob_join", ob_join(N, 0u, 0u));
  expect_null("ob_says", ob_says(N, 0.0));
  expect_null("ob_find_law", ob_find_law(N, &OBL));
  expect_null("ob_find_law", ob_find_law(&OBW, N));
  expect_null("ob_facts", ob_facts(N, &OBF));
  expect_null("ob_facts", ob_facts(&OBW, N));
}


static pl_child_t PLC;
static pl_game_t PLG;

static void play(void) {
  expect_null("pl_play", pl_play(N, &PLG, 1u));
  expect_null("pl_play", pl_play(&PLC, N, 1u));
  {
    static pl_frame_t first;
    expect_null("pl_play_from", pl_play_from(N, &PLG, 1u, &first));
    expect_null("pl_play_from", pl_play_from(&PLC, N, 1u, &first));
    expect_null("pl_play_from", pl_play_from(&PLC, &PLG, 1u, N));
  }
}


static gw_mind_t GWM;

static void grow(void) {
  expect_null("gw_load", gw_load(N, "x"));
  expect_null("gw_load", gw_load(&GWM, N));
  expect_null("gw_save", gw_save(N, "x"));
  expect_null("gw_save", gw_save(&GWM, N));
  expect_null("gw_grow", gw_grow(N, 1u, N));
}


static ex_explorer_t EXX;
static ex_game_t EXG;

static void explore(void) {
  static pl_frame_t first;
  expect_null("ex_play", ex_play(N, &EXG, &first, 1u));
  expect_null("ex_play", ex_play(&EXX, N, &first, 1u));
  expect_null("ex_play", ex_play(&EXX, &EXG, N, 1u));
  expect_null("ex_save", ex_save(N, stdout));
  expect_null("ex_save", ex_save(&EXX, N));
  expect_null("ex_load", ex_load(N, stdin));
  expect_null("ex_load", ex_load(&EXX, N));
}

int main(void) {
  unsigned bad = 0u, branches = 0u;
  core();
  reason();
  learner();
  concept();
  analogy();
  informant();
  law();
  proof();
  abstraction();
  timing();
  space();
  frame();
  object();
  play();
  grow();
  explore();
  printf("  %u NULL cases across %u functions, %u returned the wrong status\n", n_checks, n_tested,
         n_fail);
  bad += audit("smarsh_core.c", &branches);
  bad += audit("smarsh_reason.c", &branches);
  bad += audit("smarsh_learner.c", &branches);
  bad += audit("smarsh_concept.c", &branches);
  bad += audit("smarsh_analogy.c", &branches);
  bad += audit("smarsh_informant.c", &branches);
  bad += audit("smarsh_law.c", &branches);
  bad += audit("smarsh_proof.c", &branches);
  bad += audit("smarsh_abstract.c", &branches);
  bad += audit("smarsh_time.c", &branches);
  bad += audit("smarsh_space.c", &branches);
  bad += audit("smarsh_frame.c", &branches);
  bad += audit("smarsh_object.c", &branches);
  bad += audit("smarsh_play.c", &branches);
  bad += audit("smarsh_grow.c", &branches);
  bad += audit("smarsh_explore.c", &branches);
  bad += audit("smarsh_child.c", &branches);
  printf("  audit: %u null branches in the source, %u not fully exercised here\n", branches, bad);
  if (branches != n_tested) {
    printf("  FAIL  %u null branches in the source but %u functions tested\n", branches, n_tested);
    bad++;
  }
  if (n_fail == 0u && bad == 0u) {
    printf("all %u null branches exercised, every pointer of each (%u cases)\n", branches, n_checks);
    return 0;
  }
  printf("%u failure(s)\n", n_fail + bad);
  return 1;
}
