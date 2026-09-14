/*
 * demo_baby.c -- BABY -> RESEARCHER.
 *
 * A port of baby.py. Apart from its opening paragraph, which names the C
 * files instead of the Python ones, its output is byte-identical to the
 * Python version's (checked by diffing the two).
 *
 * A program that starts unable to tell any two situations apart and grows
 * by acquiring questions. Every capacity has to EARN its place by refining
 * the partition; a candidate that refines nothing is rejected as a
 * restatement however sophisticated it looks. The curriculum here is
 * scripted, to show the stages; smarsh_learner.c is the version that
 * chooses its own.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_reason.h"

#define N_SIT 32u
#define N_CAP 7u
#define N_BAT 6u
#define TRUE_SITUATION ((3u << 3) | (1u << 1) | 1u)

enum { MORE, CLOSE, NAME, SUM, EXACT_A, EXACT_B, AT_LEAST };
static const char *CAPNAME[N_CAP] = {"more", "close", "name", "sum",
                                     "exact_a", "exact_b", "at_least"};
static const char *BATNAME[N_BAT] = {"is a bigger than b", "how many altogether",
                                     "is the total four", "is it called a cup",
                                     "is a at least two", "is the total even"};

static sr_query_t CAP[N_CAP], BAT[N_BAT], BUF[N_CAP];

static unsigned a_of(unsigned s) { return (s >> 3) & 3u; }
static unsigned b_of(unsigned s) { return (s >> 1) & 3u; }
static unsigned name_of(unsigned s) { return s & 1u; }

static unsigned cap_ans(unsigned c, unsigned s) {
  unsigned a = a_of(s), b = b_of(s);
  switch (c) {
    case MORE: return a < b ? 0u : (a == b ? 1u : 2u);
    case CLOSE: return ((a > b ? a - b : b - a) <= 1u) ? 1u : 0u;
    case NAME: return name_of(s);
    case SUM: return a + b;
    case EXACT_A: return a;
    case EXACT_B: return b;
    default: return a >= b ? 1u : 0u;
  }
}

static unsigned bat_ans(unsigned g, unsigned s) {
  unsigned a = a_of(s), b = b_of(s);
  switch (g) {
    case 0: return a > b ? 1u : 0u;
    case 1: return a + b;
    case 2: return a + b == 4u ? 1u : 0u;
    case 3: return name_of(s);
    case 4: return a >= 2u ? 1u : 0u;
    default: return (a + b) % 2u == 0u ? 1u : 0u;
  }
}

static const char *word(sm_verdict_t v) {
  switch (v) {
    case SM_DERIVED: return "derived";
    case SM_UNDETERMINED: return "cannot say";
    case SM_CONTRADICTION: return "contradiction";
    default: return "no such question";
  }
}

static void partition_for(const unsigned *names, unsigned n, sr_partition_t *p) {
  unsigned i;
  for (i = 0u; i < n; i++) BUF[i] = CAP[names[i]];
  sr_refine(BUF, n, N_SIT, p);
}

static int immediate(unsigned c) { return c == MORE || c == CLOSE || c == NAME; }

typedef struct {
  sm_verdict_t verdict;
  unsigned value;
  double H;
} row_t;

typedef struct {
  const char *stage;
  const unsigned *names;
  unsigned n;
  sr_partition_t p;
  sr_state_t st;
  row_t rows[N_BAT];
  unsigned derived, expressible;
  double unheld;
} exam_t;

static void examine(exam_t *e) {
  unsigned i;
  sr_query_t pq, tq;
  partition_for(e->names, e->n, &e->p);
  sr_state_init(&e->st, e->p.n_cells);
  for (i = 0u; i < e->n; i++) {
    if (!immediate(e->names[i])) continue;
    sr_project(&e->p, &CAP[e->names[i]], &pq);
    sr_observe(&e->st, &pq, CAP[e->names[i]].ans[TRUE_SITUATION]);
  }
  e->derived = e->expressible = 0u;
  e->unheld = 0.0;
  for (i = 0u; i < N_BAT; i++) {
    sr_result_t r;
    if (sr_project(&e->p, &BAT[i], &tq) != SM_OK) {
      e->rows[i].verdict = SM_GROUNDLESS; e->rows[i].value = 0u; e->rows[i].H = 0.0;
      continue;
    }
    e->expressible++;
    sr_ask(&tq, &e->st, &r);
    if (r.verdict == SM_DERIVED) e->derived++;
    e->unheld += r.H;
    e->rows[i].verdict = r.verdict; e->rows[i].value = r.value; e->rows[i].H = r.H;
  }
}

static void dashes(unsigned n) { unsigned i; for (i = 0u; i < n; i++) putchar('-'); putchar('\n'); }

static const unsigned C0[] = {0};
static const unsigned C1[] = {MORE};
static const unsigned C2[] = {MORE, CLOSE};
static const unsigned C3[] = {MORE, CLOSE, NAME};
static const unsigned C4[] = {MORE, CLOSE, NAME, SUM, EXACT_A, EXACT_B};
static exam_t HIST[5];

int main(void) {
  static const unsigned cdom[N_CAP] = {3, 2, 2, 7, 4, 4, 2};
  static const unsigned bdom[N_BAT] = {2, 7, 2, 2, 2, 2};
  unsigned c, s, k, i;
  sr_partition_t p_cmp, after;

  for (c = 0u; c < N_CAP; c++) {
    sr_query_init(&CAP[c], N_SIT, cdom[c]);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&CAP[c], s, cap_ans(c, s));
  }
  for (c = 0u; c < N_BAT; c++) {
    sr_query_init(&BAT[c], N_SIT, bdom[c]);
    for (s = 0u; s < N_SIT; s++) sr_query_set(&BAT[c], s, bat_ans(c, s));
  }
  HIST[0].stage = "newborn";    HIST[0].names = C0; HIST[0].n = 0u;
  HIST[1].stage = "comparison"; HIST[1].names = C1; HIST[1].n = 1u;
  HIST[2].stage = "nearness";   HIST[2].names = C2; HIST[2].n = 2u;
  HIST[3].stage = "naming";     HIST[3].names = C3; HIST[3].n = 3u;
  HIST[4].stage = "counting";   HIST[4].names = C4; HIST[4].n = 6u;

  printf("BABY -> RESEARCHER.\n\n");
  printf("A program that starts unable to tell any two situations apart, and grows\n");
  printf("by acquiring questions. Nothing here is scripted as \"now it understands\n");
  printf("arithmetic\": every capacity has to EARN its place by refining the\n");
  printf("partition, and a candidate that refines nothing is rejected as a\n");
  printf("restatement however sophisticated it looks.\n\n");
  printf("Runs on smarsh_reason.c, the code test_smarsh_reason.c checks.\n\n");
  printf("the true situation: a=3, b=1, called a cup  (situation %u of %u)\n\n",
         TRUE_SITUATION, N_SIT);
  printf("%-20s%8s%9s%9s%13s\n", "stage", "worlds", "can ask", "derived", "bits unheld");
  dashes(60);
  for (k = 0u; k < 5u; k++) {
    examine(&HIST[k]);
    printf("%-20s%8u%9u/%u%8u/%u%13.2f\n", HIST[k].stage, HIST[k].p.n_cells,
           HIST[k].expressible, N_BAT, HIST[k].derived, N_BAT, HIST[k].unheld);
  }
  printf("\nIt starts unable to tell any two situations apart, so it can form no\n");
  printf("question at all, and it says so rather than producing an answer.\n");

  printf("\nEARNING A CAPACITY\n");
  dashes(60);
  partition_for(C1, 1, &p_cmp);
  printf("  after comparison, it can tell %u kinds of situation apart\n", p_cmp.n_cells);
  {
    unsigned cands[3] = {AT_LEAST, CLOSE, EXACT_A};
    for (i = 0u; i < 3u; i++) {
      if (sr_distinguishes(&p_cmp, &CAP[cands[i]])) {
        unsigned two[2];
        two[0] = MORE; two[1] = cands[i];
        partition_for(two, 2, &after);
        printf("  %-10s ACQUIRE  splits %u worlds into %u\n", CAPNAME[cands[i]],
               p_cmp.n_cells, after.n_cells);
      } else {
        printf("  %-10s reject   splits nothing; it is what it already knows, said again\n",
               CAPNAME[cands[i]]);
      }
    }
  }
  printf("\n  The rejection is not a judgement about sophistication. \"at_least\"\n");
  printf("  is a perfectly good question. It is simply already definable, so\n");
  printf("  acquiring it would add vocabulary and no capacity.\n");

  printf("\nWHAT THE ORDER OF THE LADDER IS, AND IS NOT\n");
  dashes(60);
  {
    unsigned best = 0u, best_cells = 0u;
    for (c = 0u; c < N_CAP; c++) {
      sr_partition_t one;
      partition_for(&c, 1, &one);
      if (one.n_cells > best_cells) { best = c; best_cells = one.n_cells; }
    }
    printf("  most refining single capacity: %s (%u worlds in one step)\n",
           CAPNAME[best], best_cells);
    printf("  the one it actually takes first: more (%u worlds)\n", p_cmp.n_cells);
  }
  printf("\n  So the ladder is NOT ordered by how much a step teaches. Greedy\n");
  printf("  would skip straight to exact counting. What orders it is what can\n");
  printf("  be FORMED yet: you cannot ask how many before you can tell apart.\n");
  printf("  That is a constructibility order, and it is not derivable from\n");
  printf("  information alone. Saying otherwise would be dressing up a\n");
  printf("  developmental fact as a theorem.\n");

  printf("\nARITHMETIC DOES NOT EXTEND STRUCTURE, IT SUBSUMES IT\n");
  dashes(60);
  {
    sr_partition_t p_struct, p_exact;
    sr_query_t tmp;
    unsigned sx[2] = {MORE, CLOSE}, ex[2] = {EXACT_A, EXACT_B};
    partition_for(sx, 2, &p_struct);
    partition_for(ex, 2, &p_exact);
    printf("  structure alone      %3u worlds\n", p_struct.n_cells);
    printf("  exact counts alone   %3u worlds\n", p_exact.n_cells);
    printf("  can structure be recovered from counts?  %s\n",
           sr_project(&p_exact, &CAP[MORE], &tmp) == SM_OK ? "yes" : "no");
    printf("  can counts be recovered from structure?  %s\n",
           sr_project(&p_struct, &CAP[EXACT_A], &tmp) == SM_OK ? "yes" : "no");
  }
  printf("\n  Once it can say \"three\", \"more\" was always derivable and stops\n");
  printf("  being a separate thing it knows. The earlier stage is not thrown\n");
  printf("  away, it becomes shorthand. That is checked, not asserted: the\n");
  printf("  projection succeeds one way and refuses the other.\n");

  printf("\nTHE EXAMINATION, STAGE BY STAGE\n");
  dashes(60);
  for (k = 0u; k < 5u; k++) {
    printf("\n  %s  (%u worlds, knows: ", HIST[k].stage, HIST[k].p.n_cells);
    if (HIST[k].n == 0u) printf("nothing");
    for (i = 0u; i < HIST[k].n; i++) printf("%s%s", i ? ", " : "", CAPNAME[HIST[k].names[i]]);
    printf(")\n");
    for (i = 0u; i < N_BAT; i++) {
      const row_t *r = &HIST[k].rows[i];
      char extra[32] = "", bits[48] = "";
      if (r->verdict == SM_DERIVED) sprintf(extra, "  = %u", r->value);
      if (r->H > 0) sprintf(bits, "   %.2f bits unheld", r->H);
      printf("    %-24s%-18s%s%s\n", BATNAME[i], word(r->verdict), extra, bits);
    }
  }

  printf("\nTHE RESEARCHER: AN ANSWER THAT CARRIES ITS OWN PROOF\n");
  dashes(60);
  {
    exam_t e;
    sr_query_t cq;
    sr_result_t r;
    static sr_query_t projected[3];
    const char *mlabel[3] = {"count the first pile", "count the second pile",
                             "count them together"};
    unsigned mkey[3] = {EXACT_A, EXACT_B, SUM};
    sr_probe_t pb;
    unsigned pick = 3u;
    e = HIST[4];
    examine(&e);
    sr_project(&e.p, &BAT[2], &cq);
    sr_ask(&cq, &e.st, &r);
    printf("  claim: the total is four\n");
    printf("  from a glance alone: %s, %u worlds still standing, %.2f bits it does not hold\n",
           word(r.verdict), sr_live_count(&e.st), r.H);
    printf("\n  It can phrase the question now and it still will not answer it.\n");
    printf("  So it works out what to measure. Not by ranking the measurements\n");
    printf("  as interesting: by counting what each one would leave standing.\n\n");
    for (i = 0u; i < 3u; i++) {
      sr_project(&e.p, &CAP[mkey[i]], &projected[i]);
      sr_probe(&e.st, &cq, &projected[i], &pb);
      printf("    %-24s%s\n", mlabel[i],
             pb.sufficient ? "SETTLES IT whichever way it comes back"
                           : (pb.irrelevant ? "cannot help on its own"
                                            : "narrows it, but may not finish"));
    }
    sr_choose(&e.st, &cq, projected, 3, SR_ASK_GUARANTEE, &pick, &pb);
    printf("\n  it chooses: %s\n", mlabel[pick]);
    sr_observe(&e.st, &projected[pick], CAP[mkey[pick]].ans[TRUE_SITUATION]);
    sr_ask(&cq, &e.st, &r);
    printf("  measures, and the total is four: %s, value %u, S = %.3f\n",
           word(r.verdict), r.value, r.S);
    printf("  worlds still standing: %u of %u\n", sr_live_count(&e.st), e.p.n_cells);
    printf("  a checker sharing none of the engine re-derives it from the witness alone: %s\n",
           sr_witness_check(&cq, &r.witness) ? "yes" : "NO");
  }
  printf("\n  At the first stage it could not form this question. In the middle\n");
  printf("  it could phrase it and refused to answer. At the end it worked out\n");
  printf("  which single measurement would settle it, took that one, answered,\n");
  printf("  and handed over a proof a stranger can check.\n\n");
  printf("  Nothing in between was a confidence rising. Every step was worlds\n");
  printf("  being ruled out, and the answer arrived when the survivors agreed.\n");
  return 0;
}
