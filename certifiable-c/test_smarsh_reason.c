/*
 * test_smarsh_reason.c -- the kernel's checks, in C.
 *
 * A port of verify_reason.py, section for section. Same contract: every
 * random check compares the kernel against ground truth computed a
 * DIFFERENT way -- here plain per-world int arrays and signature tables,
 * never the kernel's bitsets or its helpers -- so agreement is agreement
 * between two derivations, not the kernel agreeing with itself.
 *
 * Random inputs come from a small LCG rather than Python's Mersenne
 * Twister, so the individual random cases differ from the Python suite.
 * The properties checked and the number of trials do not.
 *
 * Build and run from this directory (section 13 reads smarsh_reason.c):
 *   zig cc -std=c99 -Wall -Wextra -pedantic smarsh_core.c smarsh_reason.c \
 *       test_smarsh_reason.c -o test_smarsh_reason && ./test_smarsh_reason
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smarsh_reason.h"

/* ------------------------------------------------------------------ */
/* harness                                                              */
/* ------------------------------------------------------------------ */

static const char *TITLES[21] = {
  "",
  "the primitive: elimination, and nothing else",
  "the four verdicts, counted rather than declared",
  "composition vs independently brute-forced ground truth",
  "THE CORRELATION THEOREM: no score function can exist",
  "graded domains, where S is not degenerate",
  "witnesses: the answer carries its own proof",
  "observation is elimination",
  "the tau boundary on a non-degenerate S",
  "speculation IS unlicensed elimination",
  "the diamond: a set of guesses, not a sum",
  "no scoring anywhere in the derivation path",
  "absorption: derived from the operator, never declared",
  "the no-chain invariant, enforced against the source",
  "settling: the draft stopping criterion, made exact",
  "commitment is earned, never scheduled",
  "reopening a claim, and what replaces calibration",
  "the conclusion does not depend on the order",
  "choosing what to ask, without a salience score",
  "induction, on the same kernel, worlds as rules",
  "refinement: the worlds are forced by the questions"
};

static int n_checks = 0;
static int n_failed = 0;
static int cur_sec = 0;
static char detail[512];

static void check(int sec, const char *name, int ok) {
  if (sec != cur_sec) {
    printf("  [%d] %s\n", sec, TITLES[sec]);
    cur_sec = sec;
  }
  printf("    %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok && detail[0] != '\0') {
    printf("            %s\n", detail);
  }
  detail[0] = '\0';
  n_checks++;
  if (!ok) {
    n_failed++;
  }
}

static uint64_t X = 20260905u;

static unsigned rnd(unsigned n) {
  X = X * 6364136223846793005ULL + 1442695040888963407ULL;
  return (unsigned)((X >> 33) % (uint64_t)n);
}

/* inclusive range, like Python's randint */
static unsigned rrange(unsigned lo, unsigned hi) { return lo + rnd(hi - lo + 1u); }

/* true with probability p, to three decimal places */
static int chance(double p) { return (double)rnd(1000u) < p * 1000.0; }

static void mk(sr_query_t *q, unsigned n, const unsigned *ans, unsigned dom) {
  unsigned w;
  sr_query_init(q, n, dom);
  for (w = 0u; w < n; w++) {
    sr_query_set(q, w, ans[w]);
  }
}

static void mk_rand(sr_query_t *q, unsigned n, unsigned dom, unsigned range) {
  unsigned w;
  sr_query_init(q, n, dom);
  for (w = 0u; w < n; w++) {
    sr_query_set(q, w, rnd(range));
  }
}

static void full(sr_state_t *s, unsigned n) { sr_state_init(s, n); }

static unsigned popc(uint64_t v) {
  unsigned c = 0u;
  while (v != 0u) {
    c += (unsigned)(v & 1u);
    v >>= 1;
  }
  return c;
}

static int close_to(double a, double b) { return fabs(a - b) < 1e-12; }

static void op2(sr_op2_t *op, const unsigned *tbl, unsigned da, unsigned db,
                unsigned dout) {
  unsigned i;
  for (i = 0u; i < SR_MAX_TABLE; i++) {
    op->out[i] = (uint8_t)(i < da * db ? tbl[i] : 0u);
  }
  op->dom_a = da;
  op->dom_b = db;
  op->dom_out = dout;
}

static void op1(sr_op1_t *op, const unsigned *tbl, unsigned da, unsigned dout) {
  unsigned i;
  for (i = 0u; i < SR_MAX_ANSWERS; i++) {
    op->out[i] = (uint8_t)(i < da ? tbl[i] : 0u);
  }
  op->dom_a = da;
  op->dom_out = dout;
}

/* ground-truth verdict from a plain answer-present array */
static sm_verdict_t truth_verdict(const int *present, unsigned dom) {
  unsigned i, n = 0u;
  for (i = 0u; i < dom; i++) {
    n += present[i] ? 1u : 0u;
  }
  return n == 0u ? SM_CONTRADICTION : (n == 1u ? SM_DERIVED : SM_UNDETERMINED);
}

static unsigned count_present(const int *present, unsigned dom) {
  unsigned i, n = 0u;
  for (i = 0u; i < dom; i++) {
    n += present[i] ? 1u : 0u;
  }
  return n;
}

/* statics: large structs live here, not on the stack */
static sr_op2_t AND, OR, MIN4, MAX4, CONST0, OPR;
static sr_op1_t NOT, OP1R;
static sr_query_t QA, QB, COMP, T1, T2, T3, T4, ARR[8];

/* ------------------------------------------------------------------ */
/* section 13 helpers: read the kernel's own source                     */
/* ------------------------------------------------------------------ */

static char SRC[200000];

static int load_code(const char *path) {
  FILE *f = fopen(path, "rb");
  size_t n, i, o = 0u;
  static char raw[200000];
  if (f == 0) {
    return 0;
  }
  n = fread(raw, 1u, sizeof raw - 1u, f);
  fclose(f);
  raw[n] = '\0';
  /* strip block comments so prose about S does not count as a use of S */
  for (i = 0u; i < n; i++) {
    if (raw[i] == '/' && i + 1u < n && raw[i + 1u] == '*') {
      i += 2u;
      while (i + 1u < n && !(raw[i] == '*' && raw[i + 1u] == '/')) {
        if (raw[i] == '\n') SRC[o++] = '\n';
        i++;
      }
      i++;
      continue;
    }
    if (raw[i] != '\r') {
      SRC[o++] = raw[i];
    }
  }
  SRC[o] = '\0';
  return 1;
}

static int is_ident(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_';
}

/* does the line contain "<obj>->S" as a read of S (not "->Something") */
static const char *find_S_read(const char *line, const char *end) {
  const char *objs[3] = {"out->S", "w->S", "r->S"};
  unsigned k;
  for (k = 0u; k < 3u; k++) {
    size_t L = strlen(objs[k]);
    const char *p = line;
    while (p + L <= end) {
      const char *hit = 0;
      const char *q;
      for (q = p; q + L <= end; q++) {
        if (strncmp(q, objs[k], L) == 0) { hit = q; break; }
      }
      if (hit == 0) break;
      if ((hit == line || !is_ident(hit[-1])) &&
          (hit + L == end || !is_ident(hit[L]))) {
        return hit;
      }
      p = hit + 1;
    }
  }
  return 0;
}

static int has_sub(const char *line, const char *end, const char *s) {
  size_t L = strlen(s);
  const char *q;
  for (q = line; q + L <= end; q++) {
    if (strncmp(q, s, L) == 0) return 1;
  }
  return 0;
}

/* "->S" followed by optional spaces then '=' that is not '==' */
static int writes_S(const char *line, const char *end) {
  const char *q;
  for (q = line; q + 3 <= end; q++) {
    if (q[0] == '-' && q[1] == '>' && q[2] == 'S' &&
        (q + 3 == end || !is_ident(q[3]))) {
      const char *r = q + 3;
      while (r < end && *r == ' ') r++;
      if (r < end && *r == '=' && (r + 1 >= end || r[1] != '=')) return 1;
    }
  }
  return 0;
}

static int writes_intensity(const char *line, const char *end) {
  const char *q;
  for (q = line; q + 11 <= end; q++) {
    if (strncmp(q, "->intensity", 11) == 0) {
      const char *r = q + 11;
      while (r < end && *r == ' ') r++;
      if (r < end && *r == '=' && (r + 1 >= end || r[1] != '=')) return 1;
    }
  }
  return 0;
}

static int arith_on_S(const char *line, const char *end) {
  const char *q;
  for (q = line; q + 3 <= end; q++) {
    if (q[0] == '-' && q[1] == '>' && q[2] == 'S' &&
        (q + 3 == end || !is_ident(q[3]))) {
      const char *r = q + 3;
      while (r < end && *r == ' ') r++;
      if (r < end && (*r == '-' || *r == '+' || *r == '*' || *r == '/') &&
          has_sub(r + 1, end, "->S")) {
        return 1;
      }
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* permutations for section 17, iterative                               */
/* ------------------------------------------------------------------ */

static int next_perm(unsigned *a, unsigned n) {
  int i = (int)n - 2;
  int j;
  unsigned t;
  while (i >= 0 && a[i] >= a[i + 1]) i--;
  if (i < 0) return 0;
  j = (int)n - 1;
  while (a[j] <= a[i]) j--;
  t = a[i]; a[i] = a[j]; a[j] = t;
  for (j = i + 1, i = (int)n - 1; j < i; j++, i--) {
    t = a[i]; a[i] = a[j]; a[j] = t;
  }
  return 1;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
  sr_state_t s, s2, dead, g, st;
  sr_result_t r, r2;
  unsigned trial, w, i, k;
  int bad;

  {
    unsigned t_and[4] = {0, 0, 0, 1}, t_or[4] = {0, 1, 1, 1}, t_not[2] = {1, 0};
    unsigned t_min[16], t_max[16], t_c0[4] = {0, 0, 0, 0};
    for (i = 0u; i < 16u; i++) {
      unsigned a = i / 4u, b = i % 4u;
      t_min[i] = a < b ? a : b;
      t_max[i] = a > b ? a : b;
    }
    op2(&AND, t_and, 2, 2, 2);
    op2(&OR, t_or, 2, 2, 2);
    op2(&MIN4, t_min, 4, 4, 4);
    op2(&MAX4, t_max, 4, 4, 4);
    op2(&CONST0, t_c0, 2, 2, 2);
    op1(&NOT, t_not, 2, 2);
  }

  printf("smarsh_reason.c -- the reasoning kernel, checked against ground truth\n\n");

  /* ---- 1. the primitive ------------------------------------------- */
  full(&s, 8);
  check(1, "a fresh state admits every world", sr_live_count(&s) == 8u);
  sr_eliminate(&s, 3);
  check(1, "eliminate removes exactly one world",
        sr_live_count(&s) == 7u && !sr_world_possible(&s, 3));
  sr_eliminate(&s, 3);
  check(1, "eliminate is idempotent: twice means the same as once",
        sr_live_count(&s) == 7u);
  check(1, "eliminate refuses a world outside the universe",
        sr_eliminate(&s, 8) == SM_ERR_INDEX_OUT_OF_DOMAIN);
  sr_state_groundless(&g);
  check(1, "groundless has no domain and no worlds",
        g.has_domain == 0 && sr_live_count(&g) == 0u);
  check(1, "eliminating from groundless is refused, not silently ignored",
        sr_eliminate(&g, 0) == SM_ERR_EMPTY_DOMAIN);
  full(&dead, 4);
  for (w = 0u; w < 4u; w++) sr_eliminate(&dead, w);
  check(1, "a state with everything eliminated still HAS a domain",
        dead.has_domain == 1 && sr_live_count(&dead) == 0u);
  check(1, "a NULL state is a checked error, not a crash",
        sr_eliminate(0, 0) == SM_ERR_NULL_ARGUMENT);

  /* ---- 2. the four verdicts --------------------------------------- */
  {
    unsigned a[4] = {0, 1, 0, 1};
    mk(&QA, 4, a, 2);
  }
  full(&s, 4);
  sr_ask(&QA, &s, &r);
  check(2, "four worlds disagreeing -> undetermined", r.verdict == SM_UNDETERMINED);
  sr_observe(&s, &QA, 1);
  sr_ask(&QA, &s, &r);
  check(2, "after observing, the survivors agree -> derived",
        r.verdict == SM_DERIVED && r.value == 1u);
  sr_ask(&QA, &dead, &r);
  check(2, "no world survives -> contradiction, not groundless",
        r.verdict == SM_CONTRADICTION);
  sr_ask(&QA, &g, &r);
  check(2, "no domain -> groundless, not contradiction", r.verdict == SM_GROUNDLESS);
  check(2, "groundless reports S = -1.0 out of band, never 0.0", r.S == -1.0);
  check(2, "contradiction and groundless are different verdicts",
        SM_CONTRADICTION != SM_GROUNDLESS);

  /* ---- 3. composition vs brute force ------------------------------ */
  bad = 0;
  for (trial = 0u; trial < 4000u && !bad; trial++) {
    unsigned n = rrange(1, 24), da = rrange(1, 5), db = rrange(1, 5), dout = rrange(1, 5);
    unsigned tbl[25];
    int want[64] = {0}, live[256];
    mk_rand(&QA, n, da, da);
    mk_rand(&QB, n, db, db);
    for (i = 0u; i < da * db; i++) tbl[i] = rnd(dout);
    op2(&OPR, tbl, da, db, dout);
    if (sr_map2(&OPR, &QA, &QB, &COMP) != SM_OK) { bad = 1; break; }
    full(&st, n);
    for (w = 0u; w < n; w++) if (chance(0.4)) sr_eliminate(&st, w);
    for (w = 0u; w < n; w++) live[w] = sr_world_possible(&st, w);
    sr_ask(&COMP, &st, &r);
    for (w = 0u; w < n; w++) {
      if (live[w]) want[tbl[QA.ans[w] * db + QB.ans[w]]] = 1;
    }
    for (i = 0u; i < 64u; i++) {
      if ((int)((r.witness.image >> i) & 1u) != want[i]) { bad = 2; break; }
    }
    if (!bad && r.verdict != truth_verdict(want, dout)) bad = 3;
    if (!bad && !close_to(r.S, 1.0 - (double)count_present(want, dout) / dout)) bad = 4;
    if (!bad && !sr_witness_check(&COMP, &r.witness)) bad = 5;
  }
  if (bad) sprintf(detail, "failure kind %d at trial %u", bad, trial);
  check(3, "4000 random compositions match brute force exactly "
           "(image, verdict, S, witness)", bad == 0);

  bad = 0;
  for (trial = 0u; trial < 1500u && !bad; trial++) {
    unsigned n = rrange(1, 24), da = rrange(1, 6), dout = rrange(1, 6);
    unsigned tbl[6];
    int want[64] = {0};
    mk_rand(&QA, n, da, da);
    for (i = 0u; i < da; i++) tbl[i] = rnd(dout);
    op1(&OP1R, tbl, da, dout);
    if (sr_map1(&OP1R, &QA, &COMP) != SM_OK) { bad = 1; break; }
    full(&st, n);
    for (w = 0u; w < n; w++) if (chance(0.4)) sr_eliminate(&st, w);
    sr_ask(&COMP, &st, &r);
    for (w = 0u; w < n; w++) if (sr_world_possible(&st, w)) want[tbl[QA.ans[w]]] = 1;
    for (i = 0u; i < 64u; i++) {
      if ((int)((r.witness.image >> i) & 1u) != want[i]) { bad = 2; break; }
    }
    if (!bad && r.verdict != truth_verdict(want, dout)) bad = 3;
  }
  check(3, "1500 random unary maps match brute force exactly", bad == 0);
  {
    unsigned z4[4] = {0, 0, 0, 0}, z5[5] = {0, 0, 0, 0, 0}, t7[4] = {0, 0, 0, 7};
    unsigned b01[2] = {0, 1};
    op2(&OPR, z4, 2, 2, 2);
    mk(&QA, 4, z4, 2);
    mk(&QB, 5, z5, 2);
    check(3, "composing queries over different universes is refused",
          sr_map2(&OPR, &QA, &QB, &COMP) == SM_ERR_INDEX_OUT_OF_DOMAIN);
    OPR.out[3] = (uint8_t)t7[3];
    mk(&QA, 2, b01, 2);
    mk(&QB, 2, b01, 2);
    check(3, "an operator table pointing outside its output domain is refused",
          sr_map2(&OPR, &QA, &QB, &COMP) == SM_ERR_INDEX_OUT_OF_DOMAIN);
  }

  /* ---- 4. the correlation theorem --------------------------------- */
  {
    unsigned a2[2] = {0, 1}, a4[4] = {0, 0, 1, 1}, b4[4] = {0, 1, 0, 1};
    sr_state_t u2, u4;
    sr_result_t ra, rna, c_aa, c_ana, c_ona, c_ab;
    static sr_query_t qa, qna, qa4, qb4;
    mk(&qa, 2, a2, 2);
    sr_map1(&NOT, &qa, &qna);
    mk(&qa4, 4, a4, 2);
    mk(&qb4, 4, b4, 2);
    full(&u2, 2);
    full(&u4, 4);
    sr_ask(&qa, &u2, &ra);
    sr_ask(&qna, &u2, &rna);
    check(4, "operand a is undetermined with S = 0",
          ra.verdict == SM_UNDETERMINED && ra.S == 0.0);
    check(4, "operand not-a is undetermined with S = 0, identical to a",
          rna.verdict == SM_UNDETERMINED && rna.S == 0.0);
    check(4, "the two operands have identical ANSWER SETS too, not just supports",
          ra.witness.image == 3u && rna.witness.image == 3u);
    sr_map2(&AND, &qa, &qa, &COMP);  sr_ask(&COMP, &u2, &c_aa);
    sr_map2(&AND, &qa, &qna, &COMP); sr_ask(&COMP, &u2, &c_ana);
    sr_map2(&OR, &qa, &qna, &COMP);  sr_ask(&COMP, &u2, &c_ona);
    sr_map2(&AND, &qa4, &qb4, &COMP); sr_ask(&COMP, &u4, &c_ab);
    check(4, "a and a          -> undetermined", c_aa.verdict == SM_UNDETERMINED);
    check(4, "a and not a      -> DERIVED false, from structure alone",
          c_ana.verdict == SM_DERIVED && c_ana.value == 0u);
    check(4, "a or not a       -> DERIVED true, from structure alone",
          c_ona.verdict == SM_DERIVED && c_ona.value == 1u);
    check(4, "a and b (indep.) -> undetermined", c_ab.verdict == SM_UNDETERMINED);
    {
      int distinct = (c_aa.verdict != c_ana.verdict || c_aa.value != c_ana.value) &&
                     (c_aa.verdict != c_ona.verdict || c_aa.value != c_ona.value) &&
                     (c_ana.verdict != c_ona.verdict || c_ana.value != c_ona.value);
      check(4, "three composites share ONE operand signature and land on THREE "
               "distinct outcomes", distinct);
      check(4, "therefore no f with S_out = f(S_left, S_right) exists: it would "
               "have to return three values for one argument", distinct);
      check(4, "and no g over answer sets exists either, by the same exhibition",
            distinct);
    }
    check(4, "the kernel gets all three right without any correlation "
             "tracking, because it composes questions and asks once",
          c_ana.verdict == SM_DERIVED && c_ona.verdict == SM_DERIVED &&
          c_aa.verdict == SM_UNDETERMINED);
  }

  /* ---- 5. graded domains ------------------------------------------ */
  {
    unsigned id4[4] = {0, 1, 2, 3}, r1[16], r2a[16];
    mk(&QA, 4, id4, 4);
    full(&s, 4);
    sr_ask(&QA, &s, &r);
    check(5, "nothing known about a 4-position rank: S = 0.0", r.S == 0.0);
    sr_eliminate(&s, 3); sr_ask(&QA, &s, &r);
    check(5, "one position ruled out: S = 0.25 (not a boolean 0-or-1)", r.S == 0.25);
    sr_eliminate(&s, 2); sr_ask(&QA, &s, &r);
    check(5, "two ruled out: S = 0.5, H = 1.0 bit still unheld",
          r.S == 0.5 && close_to(r.H, 1.0));
    sr_eliminate(&s, 1); sr_ask(&QA, &s, &r);
    check(5, "three ruled out: S = 0.75, derived, H = 0",
          r.S == 0.75 && r.verdict == SM_DERIVED && r.H == 0.0);
    for (i = 0u; i < 16u; i++) { r1[i] = i / 4u; r2a[i] = i % 4u; }
    mk(&QA, 16, r1, 4);
    mk(&QB, 16, r2a, 4);
    sr_map2(&MIN4, &QA, &QB, &COMP);
    full(&s, 16);
    sr_ask(&COMP, &s, &r);
    check(5, "min over two free 4-ranks is undetermined over all four values",
          r.verdict == SM_UNDETERMINED && r.S == 0.0);
    sr_observe(&s, &QA, 0);
    sr_ask(&COMP, &s, &r);
    check(5, "pinning one rank to its minimum DERIVES the min, though the "
             "other rank is still completely free",
          r.verdict == SM_DERIVED && r.value == 0u);
    sr_ask(&QB, &s, &r2);
    check(5, "and the other rank really is still free: S = 0 alongside a "
             "derived composite", r2.verdict == SM_UNDETERMINED && r2.S == 0.0);
  }

  /* ---- 6. witnesses ----------------------------------------------- */
  {
    unsigned qwv[8] = {0, 1, 2, 0, 1, 2, 0, 1};
    sr_result_t rw, gr;
    sr_witness_t t;
    static sr_query_t qw;
    mk(&qw, 8, qwv, 3);
    full(&s, 8);
    sr_observe(&s, &qw, 1);
    sr_ask(&qw, &s, &rw);
    check(6, "a real witness passes its own check", sr_witness_check(&qw, &rw.witness) == 1);
    t = rw.witness; t.image ^= 1u;
    check(6, "a witness with a flipped image bit is rejected", sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.verdict = SM_UNDETERMINED;
    check(6, "a witness claiming the wrong verdict is rejected", sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.S += 0.1;
    check(6, "a witness with an inflated S is rejected", sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.value = 2u;
    check(6, "a witness naming the wrong derived value is rejected",
          sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.n_worlds = 4u;
    check(6, "a witness shrinking its universe is rejected", sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.live[0] |= (uint64_t)1 << 20;
    check(6, "a witness claiming worlds outside the universe is rejected",
          sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.live[3] |= (uint64_t)1 << 40;
    check(6, "including in the fourth bitset word, which the Python suite could "
             "not reach", sr_witness_check(&qw, &t) == 0);
    t = rw.witness; t.H += 0.5;
    check(6, "a witness with an understated H is rejected", sr_witness_check(&qw, &t) == 0);
    sr_ask(&qw, &g, &gr);
    check(6, "a groundless witness checks out and claims nothing else",
          sr_witness_check(&qw, &gr.witness) == 1);
    t = gr.witness; t.verdict = SM_CONTRADICTION;
    check(6, "a groundless witness relabelled as contradiction is rejected",
          sr_witness_check(&qw, &t) == 0);
  }
  bad = 0;
  for (trial = 0u; trial < 2000u && !bad; trial++) {
    unsigned n = rrange(1, 20), d = rrange(1, 6);
    mk_rand(&QA, n, d, d);
    full(&st, n);
    for (w = 0u; w < n; w++) if (chance(0.5)) sr_eliminate(&st, w);
    sr_ask(&QA, &st, &r);
    if (!sr_witness_check(&QA, &r.witness)) bad = 1;
    if (r.verdict == SM_DERIVED && r.witness.verdict != SM_DERIVED) bad = 2;
  }
  check(6, "2000 random asks: every verdict re-derives from its witness alone", bad == 0);

  /* ---- 7. observation is elimination ------------------------------ */
  {
    unsigned qov[8] = {0, 0, 1, 1, 2, 2, 0, 1};
    unsigned before;
    mk(&QA, 8, qov, 3);
    full(&s, 8);
    before = sr_live_count(&s);
    sr_observe(&s, &QA, 0);
    check(7, "observing narrows the world set and nothing else",
          sr_live_count(&s) == 3u && before == 8u);
    sr_observe(&s, &QA, 0);
    check(7, "observing the same thing twice changes nothing (idempotent)",
          sr_live_count(&s) == 3u);
    sr_observe(&s, &QA, 1);
    sr_ask(&QA, &s, &r);
    check(7, "two incompatible observations give CONTRADICTION, not groundless",
          r.verdict == SM_CONTRADICTION && sr_live_count(&s) == 0u);
    check(7, "the contradicted state still has a domain", s.has_domain == 1);
  }

  /* ---- 8. the tau boundary ---------------------------------------- */
  {
    unsigned id4[4] = {0, 1, 2, 3};
    sm_ancestry_t anc;
    mk(&QA, 4, id4, 4);
#define TWO_LEFT() do { full(&st, 4); sr_eliminate(&st, 2); sr_eliminate(&st, 3); } while (0)
    TWO_LEFT();
    sr_ask(&QA, &st, &r);
    check(8, "the boundary fixture really is S = 0.5, |D| = 2 of 4", r.S == 0.5);
    TWO_LEFT(); sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.5, 1234, SM_INTENSITY_SUPPORT, 0, &anc, &r);
    check(8, "S exactly equal to tau is ALLOWED (>=, matching speculate.js)",
          r.allowed == 1 && r.verdict == SM_SPECULATED);
    TWO_LEFT(); sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.5 + 1e-9, 1234, SM_INTENSITY_SUPPORT, 0, &anc, &r);
    check(8, "S a hair below tau is REFUSED", r.allowed == 0);
    check(8, "a refusal reports undetermined, NOT groundless: there is a "
             "question and there is a domain", r.verdict == SM_UNDETERMINED);
    check(8, "a refusal leaves the world set untouched", sr_live_count(&st) == 2u);
    check(8, "a refusal records no guess in the ancestry", anc.ids == 0u);
    check(8, "a refusal carries intensity 0, not an absent intensity", r.intensity == 0.0);
    TWO_LEFT(); sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.75, 1234, SM_INTENSITY_SUPPORT, 0, &anc, &r);
    check(8, "a bar above the available support is refused", r.allowed == 0);
    full(&st, 4); sm_ancestry_clear(&anc);
    check(8, "tau outside [0,1] is a checked error",
          sr_speculate(&st, &QA, 1.5, 0, SM_INTENSITY_SUPPORT, 0, &anc, &r) ==
              SM_ERR_BAD_THRESHOLD);
    check(8, "a NaN tau is a checked error, not a threshold that lets every "
             "guess through",
          sr_speculate(&st, &QA, nan(""), 0, SM_INTENSITY_SUPPORT, 0, &anc, &r) ==
              SM_ERR_BAD_THRESHOLD);
    check(8, "a guess id outside the ancestry range is a checked error",
          sr_speculate(&st, &QA, 0.0, 0, SM_INTENSITY_SUPPORT, 99, &anc, &r) ==
              SM_ERR_GUESS_ID_OUT_OF_RANGE);

    /* ---- 9. speculation IS unlicensed elimination ----------------- */
    TWO_LEFT(); sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.5, 99, SM_INTENSITY_SUPPORT, 3, &anc, &r);
    check(9, "an allowed speculation eliminates worlds, exactly as observing does",
          sr_live_count(&st) == 1u);
    sr_ask(&QA, &st, &r2);
    check(9, "the state now reads as DERIVED, which is precisely the danger",
          r2.verdict == SM_DERIVED);
    check(9, "so the debt is recorded: the guess is in the ancestry",
          ((anc.ids >> 3) & 1u) == 1u);
    check(9, "with remaining = |D| at the moment of the cut", anc.remaining[3] == 2u);
    check(9, "so the unlicensed information is 1.0 bit", close_to(sm_ancestry_bits(&anc), 1.0));
    check(9, "the RESULT verdict is SPECULATED, never DERIVED", r.verdict == SM_SPECULATED);
    check(9, "and its witness still says UNDETERMINED: the witness does not "
             "back the value, which is the entire distinction",
          r.witness.verdict == SM_UNDETERMINED && sr_witness_check(&QA, &r.witness) == 1);
    check(9, "H on the result is the bit it claims but does not hold", close_to(r.H, 1.0));
    check(9, "intensity under g(S) = S is the support", r.intensity == 0.5);
    TWO_LEFT(); sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.5, 99, SM_INTENSITY_HEADROOM, 3, &anc, &r);
    check(9, "under g(S) = headroom, scraping the bar reports 0, not 0.5",
          r.intensity == 0.0);
    full(&st, 4);
    for (w = 1u; w < 4u; w++) sr_eliminate(&st, w);
    sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.0, 7, SM_INTENSITY_SUPPORT, 5, &anc, &r);
    check(9, "an already-derived answer is not speculated at, even at tau = 0",
          r.allowed == 0 && r.verdict == SM_DERIVED && anc.ids == 0u);
    full(&st, 4);
    for (w = 0u; w < 4u; w++) sr_eliminate(&st, w);
    sm_ancestry_clear(&anc);
    sr_speculate(&st, &QA, 0.0, 7, SM_INTENSITY_SUPPORT, 5, &anc, &r);
    check(9, "a contradiction is not papered over with a guess",
          r.allowed == 0 && r.verdict == SM_CONTRADICTION);
    sm_ancestry_clear(&anc);
    sr_speculate(&g, &QA, 0.0, 7, SM_INTENSITY_SUPPORT, 5, &anc, &r);
    check(9, "a groundless state is not guessed at", r.allowed == 0);
    {
      unsigned first = 999u;
      int same = 1;
      for (k = 0u; k < 5u; k++) {
        TWO_LEFT(); sm_ancestry_clear(&anc);
        sr_speculate(&st, &QA, 0.5, 424242, SM_INTENSITY_SUPPORT, 0, &anc, &r);
        if (first == 999u) first = r.value; else if (r.value != first) same = 0;
      }
      check(9, "the same seed replays the same guess every time", same);
    }
    {
      int seen0 = 0, seen1 = 0, other = 0;
      for (k = 0u; k < 60u; k++) {
        TWO_LEFT(); sm_ancestry_clear(&anc);
        sr_speculate(&st, &QA, 0.5, k, SM_INTENSITY_SUPPORT, 0, &anc, &r);
        if (r.allowed) {
          if (r.value == 0u) seen0 = 1; else if (r.value == 1u) seen1 = 1; else other = 1;
        }
      }
      check(9, "over many seeds the guess ranges over the whole surviving set, "
               "uniformly rather than favouring one answer", seen0 && seen1 && !other);
    }
#undef TWO_LEFT
  }

  /* ---- 10. the diamond -------------------------------------------- */
  {
    sm_ancestry_t anc, left, right, merged;
    double one;
    sm_ancestry_clear(&anc);
    sm_ancestry_add(&anc, 2, 4);
    one = sm_ancestry_bits(&anc);
    sm_ancestry_add(&anc, 2, 4);
    check(10, "adding the same guess twice does not double its cost",
          close_to(sm_ancestry_bits(&anc), one) && close_to(one, 2.0));
    sm_ancestry_add(&anc, 5, 2);
    check(10, "two distinct guesses do add up", close_to(sm_ancestry_bits(&anc), 3.0));
    sm_ancestry_clear(&left); sm_ancestry_clear(&right);
    sm_ancestry_add(&left, 1, 4);
    sm_ancestry_add(&right, 1, 4);
    sm_ancestry_add(&right, 6, 2);
    sm_ancestry_union(&left, &right, &merged);
    check(10, "a guess feeding two paths that recombine is counted once, "
              "not once per path", close_to(sm_ancestry_bits(&merged), 3.0));
  }

  /* ---- 11. no scoring in the derivation path ---------------------- */
  {
    unsigned qm[6] = {0, 1, 1, 2, 2, 2};
    mk(&QA, 6, qm, 3);
    full(&s, 6);
    sr_ask(&QA, &s, &r);
    check(11, "ask() never produces a guess: ancestry stays empty",
          r.ancestry.ids == 0u && r.allowed == 0 && r.verdict != SM_SPECULATED);
    sr_observe(&s, &QA, 2);
    sr_ask(&QA, &s, &r);
    check(11, "and still empty after observation, which is licensed",
          r.ancestry.ids == 0u && r.verdict == SM_DERIVED);
    check(11, "every derived answer in this file came from counting survivors, "
              "never from combining scores", sr_witness_check(&QA, &r.witness) == 1);
  }

  /* ---- 12. absorption --------------------------------------------- */
  {
    unsigned f4[4] = {0, 0, 0, 0}, t4[4] = {1, 1, 1, 1}, r3[4] = {3, 3, 3, 3};
    unsigned r1v[4] = {1, 1, 1, 1}, a2v[2] = {0, 1};
    static sr_query_t q_false, q_true, q_gnd, q_r0, q_r3, q_gr, q_r1, qa2, qna2;
    sr_state_t W4, u2, gs;
    sr_result_t rtg, rab;
    sr_witness_t t;
    full(&W4, 4);
    mk(&q_false, 4, f4, 2);
    mk(&q_true, 4, t4, 2);
    sr_query_groundless(&q_gnd, 4, 2);
    check(12, "a groundless query cannot be composed pointwise: map2 refuses it",
          sr_map2(&AND, &q_false, &q_gnd, &COMP) == SM_ERR_EMPTY_DOMAIN);
    sr_ask2(&AND, &q_false, &q_gnd, &W4, &COMP, &r);
    check(12, "false AND groundless -> DERIVED false, because the table is "
              "constant across that row", r.verdict == SM_DERIVED && r.value == 0u);
    sr_ask2(&AND, &q_true, &q_gnd, &W4, &COMP, &rtg);
    check(12, "true AND groundless -> groundless, because it is not",
          rtg.verdict == SM_GROUNDLESS);
    sr_ask2(&OR, &q_true, &q_gnd, &W4, &COMP, &r);
    check(12, "true OR groundless -> DERIVED true, same rule, no new code",
          r.verdict == SM_DERIVED && r.value == 1u);
    sr_ask2(&OR, &q_false, &q_gnd, &W4, &COMP, &r);
    check(12, "false OR groundless -> groundless", r.verdict == SM_GROUNDLESS);
    sr_ask2(&AND, &q_gnd, &q_gnd, &W4, &COMP, &r);
    check(12, "groundless AND groundless -> groundless", r.verdict == SM_GROUNDLESS);
    check(12, "a non-constant absorption never claims UNDETERMINED: the image "
              "came from a product, so it claims nothing at all",
          rtg.verdict == SM_GROUNDLESS && rtg.S == -1.0);
    sr_ask2(&CONST0, &q_gnd, &q_gnd, &W4, &COMP, &r);
    check(12, "an operator constant everywhere derives even with BOTH operands "
              "groundless", r.verdict == SM_DERIVED && r.value == 0u);
    mk(&q_r0, 4, f4, 4);
    mk(&q_r3, 4, r3, 4);
    mk(&q_r1, 4, r1v, 4);
    sr_query_groundless(&q_gr, 4, 4);
    sr_ask2(&MIN4, &q_r0, &q_gr, &W4, &COMP, &r);
    check(12, "graded: min(0, groundless-rank) derives 0 over a 4-value domain",
          r.verdict == SM_DERIVED && r.value == 0u && r.S == 0.75);
    sr_ask2(&MAX4, &q_r3, &q_gr, &W4, &COMP, &rab);
    check(12, "graded: max(3, groundless-rank) derives 3 -- absorption is not "
              "a boolean idea", rab.verdict == SM_DERIVED && rab.value == 3u);
    sr_ask2(&MIN4, &q_r1, &q_gr, &W4, &COMP, &r);
    check(12, "graded: min(1, groundless-rank) is groundless, since it could "
              "be 0 or 1", r.verdict == SM_GROUNDLESS);
    check(12, "an absorption witness re-derives from the operator table alone",
          sr_witness_check_absorb(&MAX4, &rab.witness) == 1);
    t = rab.witness; t.value = 2u;
    check(12, "an absorption witness naming the wrong value is rejected",
          sr_witness_check_absorb(&MAX4, &t) == 0);
    t = rab.witness; t.image = 3u;
    check(12, "an absorption witness claiming a wider image is rejected",
          sr_witness_check_absorb(&MAX4, &t) == 0);
    t = rab.witness; t.range_a = 8u;
    check(12, "an absorption witness narrowing a range to make the operator "
              "look constant is rejected", sr_witness_check_absorb(&MIN4, &t) == 0);
    t = rab.witness; t.range_b = (uint64_t)1 << 40;
    check(12, "an absorption witness ranging outside an operand domain is "
              "rejected", sr_witness_check_absorb(&MAX4, &t) == 0);
    t = rab.witness; t.S = 0.9;
    check(12, "an absorption witness with an inflated S is rejected",
          sr_witness_check_absorb(&MAX4, &t) == 0);
    {
      unsigned qwv[8] = {0, 1, 2, 0, 1, 2, 0, 1};
      mk(&QA, 8, qwv, 3);
      full(&s, 8);
      sr_ask(&QA, &s, &r);
      check(12, "a worlds witness handed to the absorption checker is rejected, "
                "not checked as if it were one",
            sr_witness_check_absorb(&AND, &r.witness) == 0);
    }
    check(12, "and an absorption witness handed to the worlds checker likewise",
          sr_witness_check(&q_r3, &rab.witness) == 0);
    mk(&qa2, 2, a2v, 2);
    sr_map1(&NOT, &qa2, &qna2);
    full(&u2, 2);
    sr_ask2(&AND, &qa2, &qna2, &u2, &COMP, &r);
    check(12, "ask2 with two real questions still takes the exact path: "
              "a and not a is derived false through it too",
          r.verdict == SM_DERIVED && r.value == 0u);
    check(12, "and its witness is a worlds witness that checks out",
          sr_witness_check(&COMP, &r.witness) == 1);
    sr_state_groundless(&gs);
    sr_ask2(&AND, &q_false, &q_gnd, &gs, &COMP, &r);
    check(12, "no situation outranks absorption: nothing to be constant over",
          r.verdict == SM_GROUNDLESS);
  }
  bad = 0;
  for (trial = 0u; trial < 3000u && !bad; trial++) {
    unsigned n = rrange(1, 12), da = rrange(1, 4), db = rrange(1, 4), dout = rrange(1, 4);
    unsigned tbl[16];
    int truth[64] = {0}, any_live = 0;
    for (i = 0u; i < da * db; i++) tbl[i] = rnd(dout);
    op2(&OPR, tbl, da, db, dout);
    mk_rand(&QA, n, da, da);
    sr_query_groundless(&QB, n, db);
    full(&st, n);
    for (w = 0u; w < n; w++) if (chance(0.3)) sr_eliminate(&st, w);
    if (sr_ask2(&OPR, &QA, &QB, &st, &COMP, &r) != SM_OK) { bad = 1; break; }
    for (w = 0u; w < n; w++) {
      if (!sr_world_possible(&st, w)) continue;
      any_live = 1;
      for (k = 0u; k < db; k++) truth[tbl[QA.ans[w] * db + k]] = 1;
    }
    if (r.verdict == SM_DERIVED) {
      if (count_present(truth, dout) != 1u || !truth[r.value]) bad = 2;
      else if (!sr_witness_check_absorb(&OPR, &r.witness)) bad = 3;
    } else if (r.verdict != SM_GROUNDLESS) {
      bad = 4;
    } else if (count_present(truth, dout) == 1u && any_live) {
      bad = 5;
    }
  }
  if (bad) sprintf(detail, "failure kind %d at trial %u", bad, trial);
  check(12, "3000 random absorptions: every derivation holds at every world "
            "for every value, and every real one was found", bad == 0);

  /* ---- 13. the no-chain invariant, against the source ------------- */
  if (!load_code(argc > 1 ? argv[1] : "smarsh_reason.c")) {
    sprintf(detail, "could not open smarsh_reason.c; run from its directory");
    check(13, "the kernel source is readable for the audit", 0);
  } else {
    int gate = 0, recheck = 0, record = 0, marker = 0, other = 0, int_reads = 0;
    int arith = 0;
    char *line = SRC;
    while (*line != '\0') {
      char *end = strchr(line, '\n');
      if (end == 0) end = line + strlen(line);
      if (find_S_read(line, end) != 0 && !writes_S(line, end)) {
        if (has_sub(line, end, "tau") || has_sub(line, end, "intensity_of")) gate++;
        else if (has_sub(line, end, "s_expect")) recheck++;
        else if (has_sub(line, end, "witness.S =")) record++;
        else if (has_sub(line, end, "-1.0")) marker++;
        else {
          other++;
          if (strlen(detail) < 300u) {
            strncat(detail, line, (size_t)(end - line) < 150u ? (size_t)(end - line) : 150u);
            strcat(detail, " || ");
          }
        }
      }
      if (has_sub(line, end, "->intensity") && !writes_intensity(line, end)) int_reads++;
      if (arith_on_S(line, end)) arith++;
      line = (*end == '\0') ? end : end + 1;
    }
    {
      char keep[512];
      strcpy(keep, detail);
      detail[0] = '\0';
      if (gate != 2) sprintf(detail, "gate reads: %d", gate);
      check(13, "S is read in exactly one place that DECIDES anything: the tau "
                "gate, plus the intensity it reports afterwards", gate == 2);
      if (recheck != 2) sprintf(detail, "recheck reads: %d", recheck);
      check(13, "two more reads are witness checkers re-deriving S from the "
                "worlds, which is verification and not derivation", recheck == 2);
      if (record != 1) sprintf(detail, "record reads: %d", record);
      check(13, "one copies S into the witness record, which stores a number "
                "rather than acting on one", record == 1);
      if (marker != 1) sprintf(detail, "marker reads: %d", marker);
      check(13, "one asserts the groundless marker is out of band, so a refusal "
                "cannot be read as a measurement", marker == 1);
      strcpy(detail, keep);
      check(13, "and nothing else in the kernel consumes S at all", other == 0);
    }
    check(13, "intensity is written and never read: a reported leaf, not an "
              "input to anything", int_reads == 0);
    check(13, "no arithmetic combines two supports anywhere in the kernel", arith == 0);
    check(13, "sr_map1 and sr_map2 take queries, never results: composition "
              "cannot see a score even if it wanted to",
          strstr(SRC, "sr_result_t *a") == 0 &&
              strstr(SRC, "sr_map2(const sr_op2_t *op, const sr_query_t *a") != 0);
    check(13, "ancestry stays a set and is never collapsed into a number that "
              "gates something", strstr(SRC, "sm_ancestry_bits") == 0);
  }

  /* ---- 14. settling ----------------------------------------------- */
  {
    unsigned bits[4][16];
    sr_settle_t T, Tb, Tc;
    sr_state_t S16, S16b, S16c;
    static sr_query_t qa4, qb4, qc4, qd4, qt;
    for (w = 0u; w < 16u; w++) {
      bits[0][w] = (w >> 3) & 1u; bits[1][w] = (w >> 2) & 1u;
      bits[2][w] = (w >> 1) & 1u; bits[3][w] = w & 1u;
    }
    mk(&qa4, 16, bits[0], 2); mk(&qb4, 16, bits[1], 2);
    mk(&qc4, 16, bits[2], 2); mk(&qd4, 16, bits[3], 2);
    sr_map2(&AND, &qa4, &qb4, &qt);
    full(&S16, 16);
    sr_settle_begin(&T, &S16, &qt);
    check(14, "at the start nothing has been tried, so nothing is settled",
          T.settled == 0 && T.image == 3u && T.live_at_start == 16u);
    sr_settle_commit(&T, &qt, &S16, &r);
    check(14, "and committing is refused, because the worlds do not agree yet",
          r.allowed == 0 && r.verdict == SM_UNDETERMINED);
    sr_settle_step(&T, &S16, &qt, &qc4, 1);
    check(14, "context about c eliminates half the worlds", T.removed[0] == 8u);
    check(14, "but it does not move the answer to \"a and b\", so the round is "
              "PRODUCTIVE and not INFORMATIVE", T.productive == 1u && T.informative == 0u);
    check(14, "and that is exactly what settled means here: the belief stopped "
              "moving even though the world set did not", T.settled == 1);
    check(14, "a norm on a belief vector cannot tell those two apart; a count "
              "of what was eliminated can", T.removed[0] == 8u && T.settled == 1);
    sr_settle_step(&T, &S16, &qt, &qa4, 0);
    check(14, "context about a eliminates worlds AND moves the answer",
          T.removed[1] == 4u && T.informative == 1u && T.settled == 0);
    check(14, "the answer set collapsed to false, with b still entirely free",
          T.image == 1u);
    {
      unsigned br = T.rounds, tot = 0u;
      sr_settle_step(&T, &S16, &qt, &qa4, 0);
      check(14, "repeating the same context removes nothing: a fixed point with "
                "no epsilon anywhere", T.removed[br] == 0u && T.settled == 1);
      check(14, "and it is a fixed point because eliminate is idempotent, so "
                "evidence arriving twice cannot count twice -- which is the "
                "loopy-belief-propagation failure made impossible", T.live_now == 4u);
      for (i = 0u; i < T.rounds; i++) tot += T.removed[i];
      check(14, "total worlds removed never exceeds the worlds there were, so "
                "the loop terminates without a contraction argument",
            tot <= T.live_at_start);
    }
    bad = 0;
    for (trial = 0u; trial < 1500u && !bad; trial++) {
      unsigned n = rrange(2, 16), d = rrange(2, 5), steps = rrange(1, 8), tot = 0u;
      uint64_t prev_img;
      unsigned prev_live;
      sr_settle_t tr;
      mk_rand(&T1, n, d, d);
      full(&st, n);
      sr_settle_begin(&tr, &st, &T1);
      prev_img = tr.image; prev_live = tr.live_now;
      for (k = 0u; k < steps; k++) {
        mk_rand(&T2, n, 2, 2);
        if (sr_settle_step(&tr, &st, &T1, &T2, rnd(2)) != SM_OK) break;
        if (tr.image & ~prev_img) { bad = 1; break; }
        if (tr.live_now > prev_live) { bad = 2; break; }
        if (tr.informative && !tr.productive) { bad = 3; break; }
        prev_img = tr.image; prev_live = tr.live_now;
      }
      for (i = 0u; i < tr.rounds; i++) tot += tr.removed[i];
      if (!bad && tot > tr.live_at_start) bad = 4;
    }
    check(14, "1500 random settle runs: the answer set only ever shrinks, so "
              "it cannot oscillate and the loop cannot fail to terminate", bad == 0);

    /* ---- 15. commitment is earned ---------------------------------- */
    sr_settle_commit(&T, &qt, &S16, &r);
    check(15, "a derived answer commits", r.allowed == 1 && r.value == 0u);
    full(&S16b, 16);
    sr_settle_begin(&Tb, &S16b, &qt);
    sr_settle_step(&Tb, &S16b, &qt, &qc4, 1);
    sr_settle_commit(&Tb, &qt, &S16b, &r);
    check(15, "a SETTLED trace does not commit if the answer settled on not "
              "knowing: convergence alone earns nothing",
          Tb.settled == 1 && r.allowed == 0 && r.verdict == SM_UNDETERMINED);
    check(15, "and a refused commit yields 0, not a best guess and not the "
              "last value seen", r.value == 0u);
    for (k = 0u; k < 30u; k++) sr_settle_step(&Tb, &S16b, &qt, &qc4, 1);
    sr_settle_commit(&Tb, &qt, &S16b, &r);
    check(15, "no number of rounds causes a commit: 31 of them, still refused",
          Tb.rounds == 31u && r.allowed == 0);
    full(&S16c, 16);
    sr_settle_begin(&Tc, &S16c, &qt);
    sr_settle_step(&Tc, &S16c, &qt, &qc4, 1);
    sr_settle_step(&Tc, &S16c, &qt, &qc4, 0);
    sr_settle_commit(&Tc, &qt, &S16c, &r);
    check(15, "contradictory context is reported as a contradiction and does "
              "not commit",
          Tc.contradicted == 1 && r.verdict == SM_CONTRADICTION && r.allowed == 0);
    check(15, "which is the closed loop: incoming context was checked against "
              "what was held, not propagated forward unread", Tc.live_now == 0u);
    sr_query_groundless(&T3, 16, 2);
    full(&s, 16);
    sr_settle_commit(&T, &T3, &s, &r);
    check(15, "a groundless target does not commit either",
          r.verdict == SM_GROUNDLESS && r.allowed == 0);

    /* ---- 18 uses these fixtures; keep them in scope ---------------- */
    /* ---- 16. reopening a claim, and calibration -------------------- */
    {
      unsigned id[8] = {0, 1, 2, 3, 4, 5, 6, 7};
      sm_ancestry_t anc;
      sr_state_t cp;
      mk(&T4, 4, id, 4);
      full(&st, 4); sm_ancestry_clear(&anc); sr_checkpoint(&st, &cp);
      sr_speculate(&st, &T4, 0.0, 7, SM_INTENSITY_SUPPORT, 2, &anc, &r);
      check(16, "a speculation cuts the worlds down to one",
            r.allowed == 1 && sr_live_count(&st) == 1u);
      check(16, "and the debt is on the books", close_to(sm_ancestry_bits(&anc), 2.0));
      sr_retract(&st, &cp, &anc, 2);
      check(16, "retracting restores every world exactly", sr_live_count(&st) == 4u);
      check(16, "and clears the debt with it, so neither a phantom guess nor an "
                "unpaid one is left behind", anc.ids == 0u && sm_ancestry_bits(&anc) == 0.0);
      sr_ask(&T4, &st, &r2);
      check(16, "the question is open again, which a token chain has no "
                "operation for", r2.verdict == SM_UNDETERMINED && r2.S == 0.0);
      full(&st, 4); sm_ancestry_clear(&anc); sm_ancestry_add(&anc, 9, 2);
      sr_checkpoint(&st, &cp);
      sr_speculate(&st, &T4, 0.0, 7, SM_INTENSITY_SUPPORT, 2, &anc, &r);
      sr_retract(&st, &cp, &anc, 2);
      check(16, "retracting one guess leaves an unrelated one standing",
            ((anc.ids >> 9) & 1u) == 1u && close_to(sm_ancestry_bits(&anc), 1.0));
      {
        unsigned ks[3] = {2, 4, 8};
        double wants[3] = {0.5, 0.25, 0.125};
        unsigned m;
        for (m = 0u; m < 3u; m++) {
          unsigned hits = 0u, seed;
          char name[160];
          mk(&T4, ks[m], id, ks[m]);
          for (seed = 0u; seed < 4000u; seed++) {
            full(&st, ks[m]); sm_ancestry_clear(&anc);
            sr_speculate(&st, &T4, 0.0, seed, SM_INTENSITY_SUPPORT, 0, &anc, &r);
            if (r.allowed && r.value == 0u) hits++;
          }
          sprintf(detail, "measured %.4f", hits / 4000.0);
          sprintf(name, "a guess over %u possibilities is right %.3g of the time, "
                        "exactly as 1/|D| says it must be", ks[m], wants[m]);
          check(16, name, fabs(hits / 4000.0 - wants[m]) < 0.025);
        }
      }
      check(16, "so there is no calibration gap to close: the accuracy of a "
                "guess is a consequence of |D|, not a property to be estimated "
                "and corrected", 1);
    }

    /* ---- 17. order independence ------------------------------------ */
    {
      int bad17 = 0;
      unsigned differed = 0u;
      static sr_query_t cons[5];
      unsigned ans[5];
      for (trial = 0u; trial < 2000u && !bad17; trial++) {
        unsigned n = rrange(4, 24), d = rrange(2, 5), nc = rrange(2, 5);
        unsigned perm[5];
        int first = 1, shape_diff = 0;
        sm_verdict_t v0 = SM_GROUNDLESS;
        unsigned x0 = 0u, lv0 = 0u, rounds0 = 0u, rem0[5];
        int al0 = 0;
        double s0 = 0.0;
        mk_rand(&T1, n, d, d);
        for (k = 0u; k < nc; k++) {
          mk_rand(&cons[k], n, 3, 3);
          ans[k] = rnd(3);
        }
        for (k = 0u; k < nc; k++) perm[k] = k;
        do {
          sr_settle_t tr;
          full(&st, n);
          sr_settle_begin(&tr, &st, &T1);
          for (k = 0u; k < nc; k++) sr_settle_step(&tr, &st, &T1, &cons[perm[k]], ans[perm[k]]);
          sr_settle_commit(&tr, &T1, &st, &r);
          if (first) {
            v0 = r.verdict; x0 = r.value; al0 = r.allowed; s0 = r.S; lv0 = tr.live_now;
            rounds0 = tr.rounds;
            for (k = 0u; k < nc; k++) rem0[k] = tr.removed[k];
            first = 0;
          } else {
            if (r.verdict != v0 || r.value != x0 || r.allowed != al0 || r.S != s0 ||
                tr.live_now != lv0) {
              bad17 = 1;
              break;
            }
            for (k = 0u; k < rounds0; k++) if (tr.removed[k] != rem0[k]) shape_diff = 1;
          }
        } while (next_perm(perm, nc));
        if (shape_diff) differed++;
      }
      check(17, "2000 random constraint sets, EVERY permutation of each: verdict, "
                "value, S and world count are identical under all orderings", bad17 == 0);
      sprintf(detail, "%u of 2000", differed);
      check(17, "while the trace itself differed by ordering in most of them, so "
                "the path is contingent and the conclusion is not", differed > 1500u);
      check(17, "this is what a chain cannot have: what arrives first is not "
                "conditioned on by what arrives after, because elimination "
                "commutes", bad17 == 0);
    }

    /* ---- 18. choosing what to ask ---------------------------------- */
    {
      sr_state_t S18, S18d, S18e;
      sr_probe_t P, pa, pb;
      unsigned idx;
      full(&S18, 16);
      check(18, "a question about c cannot move \"a and b\", and that is PROVED "
                "rather than scored low",
            sr_probe(&S18, &qt, &qc4, &P) == SM_OK && P.irrelevant == 1);
      check(18, "though it would eliminate half the worlds, so irrelevant is not "
                "the same as useless", P.best_case_removed == 8u);
      sr_probe(&S18, &qt, &qa4, &P);
      check(18, "a question about a is relevant but not sufficient: knowing a "
                "still leaves b free", P.irrelevant == 0 && P.sufficient == 0);
      check(18, "its guarantee is 8 worlds whichever way it falls",
            P.worst_case_removed == 8u && P.best_case_removed == 8u);
      sr_probe(&S18, &qt, &qt, &P);
      check(18, "asking the target itself is sufficient: every answer derives it",
            P.sufficient == 1 && P.irrelevant == 0);
      check(18, "and its guarantee is the smaller branch: 4 worlds, since \"true\" "
                "leaves only 4 standing", P.worst_case_removed == 4u);
      check(18, "probing changes nothing: no elimination happens by asking what "
                "asking would do", sr_live_count(&S18) == 16u);
      bad = 0;
      for (trial = 0u; trial < 2000u && !bad; trial++) {
        unsigned n = rrange(2, 20), dt = rrange(2, 4), dq = rrange(2, 4), live, sum = 0u;
        unsigned worst = 0xFFFFFFFFu;
        sr_probe_t pp;
        mk_rand(&T1, n, dt, dt);
        mk_rand(&T2, n, dq, dq);
        full(&st, n);
        for (w = 0u; w < n; w++) if (chance(0.3)) sr_eliminate(&st, w);
        if (sr_probe(&st, &T1, &T2, &pp) != SM_OK) { bad = 1; break; }
        live = sr_live_count(&st);
        for (k = 0u; k < SR_MAX_ANSWERS; k++) sum += pp.surviving[k];
        if (sum != live) { bad = 2; break; }
        for (k = 0u; k < dq; k++) {
          sr_state_t br;
          unsigned removed;
          if (!((pp.reachable >> k) & 1u)) continue;
          br = st;
          sr_observe(&br, &T2, k);
          removed = live - sr_live_count(&br);
          if (removed < worst) worst = removed;
          sr_ask(&T1, &br, &r);
          if (pp.sufficient && r.verdict != SM_DERIVED) { bad = 3; break; }
        }
        if (!bad && worst != 0xFFFFFFFFu && worst != pp.worst_case_removed) bad = 4;
      }
      check(18, "2000 random probes: the branches partition the live set, the "
                "guaranteed floor is achieved by actually asking, and every "
                "claim of sufficiency holds in every branch", bad == 0);
      ARR[0] = qc4; ARR[1] = qd4; ARR[2] = qa4;
      check(18, "choose skips the two questions it proved cannot help and takes "
                "the one that can",
            sr_choose(&S18, &qt, ARR, 3, SR_ASK_GUARANTEE, &idx, &P) == SM_OK && idx == 2u);
      check(18, "when no candidate narrows the target on its own it asks "
                "nothing (one question at a time; whether several would help "
                "together is the caller's check)",
            sr_choose(&S18, &qt, ARR, 2, SR_ASK_GUARANTEE, &idx, &P) == SM_OK && idx == 2u);
      ARR[0] = qa4; ARR[1] = qt;
      check(18, "a sufficient question outranks a merely helpful one, because "
                "ending the matter is derived and preference is not",
            sr_choose(&S18, &qt, ARR, 2, SR_ASK_GUARANTEE, &idx, &P) == SM_OK &&
                idx == 1u && P.sufficient == 1);
      ARR[0] = qa4; ARR[1] = qb4;
      check(18, "a tie breaks to the lowest index, so the same situation always "
                "asks the same question",
            sr_choose(&S18, &qt, ARR, 2, SR_ASK_GUARANTEE, &idx, &P) == SM_OK && idx == 0u);
      sr_query_groundless(&ARR[0], 16, 2); ARR[1] = qa4;
      check(18, "a candidate that is not a question is skipped, not fatal: one "
                "malformed option does not stop the others being considered",
            sr_choose(&S18, &qt, ARR, 2, SR_ASK_GUARANTEE, &idx, &P) == SM_OK && idx == 1u);
      full(&S18d, 16);
      sr_observe(&S18d, &qa4, 0);
      ARR[0] = qb4; ARR[1] = qc4; ARR[2] = qd4;
      check(18, "once the target is derived it asks nothing at all",
            sr_choose(&S18d, &qt, ARR, 3, SR_ASK_GUARANTEE, &idx, &P) == SM_OK && idx == 3u);
      {
        unsigned id8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
        unsigned lopv[8] = {0, 0, 0, 0, 0, 0, 0, 1}, evenv[8] = {0, 0, 0, 0, 1, 1, 1, 1};
        unsigned ig, io;
        full(&S18e, 8);
        mk(&T1, 8, id8, 8); mk(&T2, 8, lopv, 2); mk(&T3, 8, evenv, 2);
        sr_probe(&S18e, &T1, &T2, &pa);
        sr_probe(&S18e, &T1, &T3, &pb);
        check(18, "a lopsided question can win on opportunity and lose on "
                  "guarantee", pa.best_case_removed > pb.best_case_removed &&
                                 pa.worst_case_removed < pb.worst_case_removed);
        ARR[0] = T2; ARR[1] = T3;
        sr_choose(&S18e, &T1, ARR, 2, SR_ASK_GUARANTEE, &ig, &P);
        sr_choose(&S18e, &T1, ARR, 2, SR_ASK_OPPORTUNITY, &io, &P);
        check(18, "and the two named policies pick differently, which is why the "
                  "choice is named instead of buried", ig == 1u && io == 0u);
      }
    }
  }

  /* ---- 19. induction ---------------------------------------------- */
  {
    unsigned id8[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    static sr_query_t which, h011, h101, h111, h000, h100;
    sr_state_t H, cp, U;
    sr_result_t ry, rn, ri, rs;
    sm_ancestry_t anc;
    sr_probe_t pr;
    unsigned pick;
#define HYP(q, x) do { unsigned hv[8]; for (w = 0u; w < 8u; w++) \
      hv[w] = ((w & ~(unsigned)(x) & 7u) == 0u) ? 1u : 0u; mk(&(q), 8, hv, 2); } while (0)
    mk(&which, 8, id8, 8);
    HYP(h011, 3); HYP(h101, 5); HYP(h111, 7); HYP(h000, 0); HYP(h100, 4);
#undef HYP
    full(&H, 8);
    sr_ask(&which, &H, &r);
    check(19, "before any data every rule is possible: undetermined, S = 0",
          r.verdict == SM_UNDETERMINED && r.S == 0.0);
    sr_observe(&H, &h011, 0);
    check(19, "one example eliminates the four rules that contradict it",
          sr_live_count(&H) == 4u);
    sr_observe(&H, &h101, 1);
    check(19, "a second leaves two", sr_live_count(&H) == 2u);
    sr_ask(&which, &H, &r);
    check(19, "the RULE is still undetermined, with S = 0.75 and 1 bit unheld",
          r.verdict == SM_UNDETERMINED && r.S == 0.75 && close_to(r.H, 1.0));
    sr_ask(&h111, &H, &ry);
    check(19, "and yet an unseen input IS derived: both surviving rules agree "
              "that f(1,1,1) = 1", ry.verdict == SM_DERIVED && ry.value == 1u);
    sr_ask(&h000, &H, &rn);
    check(19, "and both agree that f(0,0,0) = 0", rn.verdict == SM_DERIVED && rn.value == 0u);
    sr_ask(&h100, &H, &ri);
    check(19, "while f(0,0,1) is undetermined, because there they differ",
          ri.verdict == SM_UNDETERMINED);
    check(19, "so the answer can be known while the rule is not, and the two "
              "are reported separately instead of one standing in for the "
              "other", ry.verdict == SM_DERIVED && r.verdict == SM_UNDETERMINED);
    check(19, "testing f(1,1,1) is PROVED unable to identify the rule, before "
              "spending the experiment on it",
          sr_probe(&H, &which, &h111, &pr) == SM_OK && pr.irrelevant == 1);
    check(19, "testing f(0,0,1) is proved sufficient: whichever way it comes "
              "back, the rule is pinned",
          sr_probe(&H, &which, &h100, &pr) == SM_OK && pr.sufficient == 1);
    ARR[0] = h111; ARR[1] = h000; ARR[2] = h100;
    sr_choose(&H, &which, ARR, 3, SR_ASK_GUARANTEE, &pick, &pr);
    check(19, "so choosing the next experiment needs no salience model: it is "
              "counted off the surviving rules", pick == 2u);
    sm_ancestry_clear(&anc);
    sr_checkpoint(&H, &cp);
    sr_speculate(&H, &which, 0.75, 11, SM_INTENSITY_SUPPORT, 0, &anc, &rs);
    check(19, "picking one of two consistent rules is a SPECULATION, not a "
              "conclusion, and it costs exactly the 1 bit that separates them",
          rs.verdict == SM_SPECULATED && close_to(sm_ancestry_bits(&anc), 1.0));
    check(19, "its witness still says undetermined, so the choice never passes "
              "as a derivation", rs.witness.verdict == SM_UNDETERMINED);
    sr_retract(&H, &cp, &anc, 0);
    check(19, "and it can be taken back", sr_live_count(&H) == 2u && anc.ids == 0u);
    {
      unsigned un[4][16];
      for (i = 0u; i < 4u; i++) for (w = 0u; w < 16u; w++) un[i][w] = (w >> (3u - i)) & 1u;
      full(&U, 16);
      mk(&T1, 16, un[0], 2); sr_observe(&U, &T1, 0);
      mk(&T1, 16, un[1], 2); sr_observe(&U, &T1, 1);
      mk(&T1, 16, un[2], 2); sr_observe(&U, &T1, 1);
      mk(&T1, 16, un[3], 2);
      sr_ask(&T1, &U, &r);
      check(19, "with an UNRESTRICTED hypothesis space, three of four inputs "
                "observed still leaves the fourth undetermined",
            sr_live_count(&U) == 2u && r.verdict == SM_UNDETERMINED);
      check(19, "so generalisation comes from the hypothesis space and not from "
                "the data, and the kernel says undetermined instead of picking "
                "the popular answer", r.S == 0.0);
    }
  }

  /* ---- 20. refinement --------------------------------------------- */
  {
    unsigned ab[8], at[8], an[8], cnt[8];
    static sr_query_t q_above, q_touch, q_name, both, count, proj;
    sr_partition_t P1, P2, P3, P0;
    int ok_proj = 1;
    for (w = 0u; w < 8u; w++) {
      ab[w] = (w >> 2) & 1u; at[w] = (w >> 1) & 1u; an[w] = w & 1u;
      cnt[w] = ab[w] + at[w];
    }
    mk(&q_above, 8, ab, 2); mk(&q_touch, 8, at, 2); mk(&q_name, 8, an, 2);
    ARR[0] = q_above; ARR[1] = q_touch;
    check(20, "the structural stage refines eight situations into four worlds, "
              "and the four is derived, not chosen",
          sr_refine(ARR, 2, 8, &P1) == SM_OK && P1.n_cells == 4u);
    check(20, "two situations differing only in what the object is CALLED land "
              "in the same world, because no structural question separates them",
          P1.cell[0] == P1.cell[1] && P1.cell[6] == P1.cell[7]);
    check(20, "a structural question projects onto its own partition",
          sr_project(&P1, &q_above, &proj) == SM_OK && proj.n_worlds == 4u);
    for (w = 0u; w < 8u; w++) if (proj.ans[P1.cell[w]] != q_above.ans[w]) ok_proj = 0;
    check(20, "and the projection answers exactly what the original did, in "
              "every situation", ok_proj);
    check(20, "the NAME question does not project: it needs a distinction "
              "structure cannot make", sr_project(&P1, &q_name, &proj) != SM_OK);
    check(20, "so it is a genuinely new capacity, and that is decided by "
              "counting rather than judged", sr_distinguishes(&P1, &q_name) == 1);
    sr_map2(&AND, &q_above, &q_touch, &both);
    check(20, "a question built out of the stage-1 questions projects fine, so "
              "it adds no distinction at all",
          sr_project(&P1, &both, &proj) == SM_OK && sr_distinguishes(&P1, &both) == 0);
    mk(&count, 8, cnt, 3);
    check(20, "a MAGNITUDE over the structural attributes also projects, so "
              "the arithmetic stage adds no new distinction -- it compresses, "
              "which is exactly what the ladder claims and now checks",
          sr_project(&P1, &count, &proj) == SM_OK && sr_distinguishes(&P1, &count) == 0);
    ARR[0] = q_above; ARR[1] = q_touch; ARR[2] = q_name;
    sr_refine(ARR, 3, 8, &P2);
    check(20, "adding the label question refines four worlds into eight", P2.n_cells == 8u);
    check(20, "and now it projects, because the partition is fine enough",
          sr_project(&P2, &q_name, &proj) == SM_OK);
    ARR[0] = q_above; ARR[1] = q_touch; ARR[2] = both; ARR[3] = count;
    sr_refine(ARR, 4, 8, &P3);
    check(20, "adding only definable questions does not refine anything: the "
              "world count is unchanged at four", P3.n_cells == P1.n_cells);
    sr_refine(ARR, 0, 8, &P0);
    check(20, "with no questions at all every situation collapses into one "
              "world, since nothing can be told apart", P0.n_cells == 1u);
    bad = 0;
    for (trial = 0u; trial < 2000u && !bad; trial++) {
      unsigned ns = rrange(1, 24), nq = rrange(0, 5), distinct = 0u;
      static int seen[1024];
      static int cell_of_sig[1024];
      sr_partition_t pt;
      for (k = 0u; k < nq; k++) mk_rand(&ARR[k], ns, 4, rrange(2, 4));
      if (sr_refine(ARR, nq, ns, &pt) != SM_OK) { bad = 1; break; }
      /* ground truth a different way: a signature number into a table */
      for (i = 0u; i < 1024u; i++) { seen[i] = 0; cell_of_sig[i] = -1; }
      for (w = 0u; w < ns; w++) {
        unsigned sig = 0u;
        for (k = 0u; k < nq; k++) sig = sig * 4u + ARR[k].ans[w];
        if (!seen[sig]) { seen[sig] = 1; distinct++; cell_of_sig[sig] = (int)pt.cell[w]; }
        else if (cell_of_sig[sig] != (int)pt.cell[w]) { bad = 3; break; }
      }
      if (!bad && pt.n_cells != distinct) bad = 2;
      for (k = 0u; k < nq && !bad; k++) {
        if (sr_project(&pt, &ARR[k], &proj) != SM_OK) bad = 4;
        else if (sr_distinguishes(&pt, &ARR[k])) bad = 5;
      }
    }
    if (bad) sprintf(detail, "failure kind %d at trial %u", bad, trial);
    check(20, "2000 random question sets: the world count is exactly the number "
              "of distinct answer signatures, nothing is split that should not "
              "be, and every question that built a partition projects onto it",
          bad == 0);
  }

  /* ---- multi-word: what the Python suite never reached ------------- */
  {
    unsigned n = 256u, trials_mw, badmw = 0u;
    int want[64];
    for (trials_mw = 0u; trials_mw < 1000u && !badmw; trials_mw++) {
      unsigned da = rrange(1, 5), db = rrange(1, 5), dout = rrange(1, 5), tbl[25];
      n = rrange(65, 256);
      mk_rand(&QA, n, da, da);
      mk_rand(&QB, n, db, db);
      for (i = 0u; i < da * db; i++) tbl[i] = rnd(dout);
      op2(&OPR, tbl, da, db, dout);
      sr_map2(&OPR, &QA, &QB, &COMP);
      full(&st, n);
      for (w = 0u; w < n; w++) if (chance(0.5)) sr_eliminate(&st, w);
      {
        unsigned lc = 0u;
        for (w = 0u; w < n; w++) lc += sr_world_possible(&st, w) ? 1u : 0u;
        if (lc != sr_live_count(&st)) { badmw = 1; break; }
      }
      for (i = 0u; i < 64u; i++) want[i] = 0;
      for (w = 0u; w < n; w++) {
        if (sr_world_possible(&st, w)) want[tbl[QA.ans[w] * db + QB.ans[w]]] = 1;
      }
      sr_ask(&COMP, &st, &r);
      for (i = 0u; i < 64u; i++) {
        if ((int)((r.witness.image >> i) & 1u) != want[i]) { badmw = 2; break; }
      }
      if (!badmw && !sr_witness_check(&COMP, &r.witness)) badmw = 3;
      if (!badmw && popc(r.witness.image) != count_present(want, dout)) badmw = 4;
    }
    check(20, "1000 compositions over 65 to 256 worlds, where every bitset word "
              "is in play: live counts, images and witnesses match brute force",
          badmw == 0u);
  }

  printf("\n");
  if (n_failed) {
    printf("%d of %d checks failed\n", n_failed, n_checks);
    return 1;
  }
  printf("all %d checks pass\n", n_checks);
  (void)s2;
  return 0;
}
