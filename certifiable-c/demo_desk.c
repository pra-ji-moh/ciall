/*
 * demo_desk.c -- a trade approval, run through the reasoning kernel.
 *
 * A port of demo_desk.py whose output is byte-identical to it (checked by
 * diffing the two). Five unknowns, so thirty-two worlds:
 *
 *   kyc    counterparty KYC is current
 *   sanc   counterparty appears on a sanctions list
 *   lim    trade is inside the position limit
 *   mkt    market is open
 *   col    collateral is posted
 *
 *   approve = kyc and (not sanc) and lim and mkt and col
 *
 * Nothing below tells the kernel what approve means beyond that formula.
 * It is not scored, ranked, or predicted. Every line of output is a count
 * of what survived.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_reason.h"

#define N 32u

static const char *VNAME[] = {"derived", "undetermined", "contradiction",
                              "groundless", "speculated"};

static sr_op2_t AND, OR;
static sr_op1_t NOT;
static sr_query_t kyc, sanc, lim, mkt, col, nsanc, t1, t2, t3, approve, tmp, tmp2;

static const char *boolname(unsigned v) { return v == 0u ? "false" : (v == 1u ? "true" : "-"); }
static const char *pybool(int b) { return b ? "True" : "False"; }

static void var(sr_query_t *q, unsigned pos) {
  unsigned w;
  sr_query_init(q, N, 2);
  for (w = 0u; w < N; w++) sr_query_set(q, w, (w >> pos) & 1u);
}

static void full(sr_state_t *s) { sr_state_init(s, N); }

static void show(const char *label, const sr_query_t *q, const sr_state_t *s,
                 const char *indent) {
  sr_result_t r;
  char sv[16];
  const char *val;
  sr_ask(q, s, &r);
  if (r.S < 0) strcpy(sv, " n/a "); else sprintf(sv, "%5.2f", r.S);
  val = (r.verdict == SM_DERIVED) ? boolname(r.value) : "-";
  printf("%s%-26s %-13s S=%s  answer=%-5s  witness %s\n", indent, label,
         VNAME[r.verdict], sv, val, sr_witness_check(q, &r.witness) ? "checks" : "FAILS");
}

static void rule(const char *title) {
  size_t i;
  printf("\n%s\n", title);
  for (i = 0u; i < strlen(title); i++) putchar('-');
  putchar('\n');
}

static sr_settle_t T;
static sr_state_t S;

static void feed(const char *label, const sr_query_t *constraint, unsigned answer) {
  unsigned before = T.rounds, gone, i;
  char img[32] = "{";
  int first = 1;
  sr_settle_step(&T, &S, &approve, constraint, answer);
  gone = T.removed[before];
  for (i = 0u; i < 2u; i++) {
    if ((T.image >> i) & 1u) {
      if (!first) strcat(img, ", ");
      strcat(img, boolname(i));
      first = 0;
    }
  }
  strcat(img, "}");
  printf("  %-28s%12u%12s%13s   %s\n", label, gone, gone ? "yes" : "no",
         T.settled ? "no" : "yes", img);
}

int main(void) {
  unsigned t_and[4] = {0, 0, 0, 1}, t_or[4] = {0, 1, 1, 1};
  unsigned i;
  sr_result_t r, r2;

  for (i = 0u; i < SR_MAX_TABLE; i++) { AND.out[i] = 0u; OR.out[i] = 0u; }
  for (i = 0u; i < 4u; i++) { AND.out[i] = (uint8_t)t_and[i]; OR.out[i] = (uint8_t)t_or[i]; }
  AND.dom_a = AND.dom_b = AND.dom_out = 2u;
  OR.dom_a = OR.dom_b = OR.dom_out = 2u;
  for (i = 0u; i < SR_MAX_ANSWERS; i++) NOT.out[i] = 0u;
  NOT.out[0] = 1u; NOT.out[1] = 0u; NOT.dom_a = 2u; NOT.dom_out = 2u;

  var(&kyc, 4); var(&sanc, 3); var(&lim, 2); var(&mkt, 1); var(&col, 0);
  sr_map1(&NOT, &sanc, &nsanc);
  sr_map2(&AND, &kyc, &nsanc, &t1);
  sr_map2(&AND, &t1, &lim, &t2);
  sr_map2(&AND, &t2, &mkt, &t3);
  sr_map2(&AND, &t3, &col, &approve);

  /* ---- A ------------------------------------------------------------ */
  rule("A. what is known before anything arrives");
  full(&S);
  printf("  %u worlds possible, nothing eliminated\n", sr_live_count(&S));
  show("approve", &approve, &S, "  ");
  show("kyc", &kyc, &S, "  ");
  show("sanctioned", &sanc, &S, "  ");
  show("within limit", &lim, &S, "  ");
  show("market open", &mkt, &S, "  ");
  show("collateral", &col, &S, "  ");

  /* ---- B ------------------------------------------------------------ */
  rule("B. context arrives, one piece at a time");
  sr_settle_begin(&T, &S, &approve);
  printf("  target: approve\n\n");
  printf("  %-28s%12s%12s%13s   answer set\n", "context", "worlds gone",
         "productive", "informative");
  printf("  ");
  for (i = 0u; i < 76u; i++) putchar('-');
  putchar('\n');
  feed("market is open", &mkt, 1);
  feed("counterparty IS sanctioned", &sanc, 1);
  feed("market is open (again)", &mkt, 1);
  printf("\n  rounds=%u  productive=%u  informative=%u  settled=%s  live=%u\n",
         T.rounds, T.productive, T.informative, pybool(T.settled), T.live_now);

  /* ---- C ------------------------------------------------------------ */
  rule("C. commit");
  sr_settle_commit(&T, &approve, &S, &r);
  printf("  allowed=%s   verdict=%s   answer=%s\n", pybool(r.allowed),
         VNAME[r.verdict], boolname(r.value));
  printf("\n  and the four inputs it never learned:\n");
  show("kyc", &kyc, &S, "    ");
  show("within limit", &lim, &S, "    ");
  show("collateral", &col, &S, "    ");
  printf("\n");
  printf("  One fact decided it. Four of the five inputs are still entirely\n");
  printf("  unknown, S = 0 on each, and the answer is derived anyway. Nothing\n");
  printf("  had to be assumed about them, because the operator table holds one\n");
  printf("  value across that whole row.\n");

  /* ---- D ------------------------------------------------------------ */
  rule("D. the same desk, with nothing disqualifying");
  {
    sr_state_t s2;
    sr_settle_t t2s;
    full(&s2);
    sr_settle_begin(&t2s, &s2, &approve);
    sr_settle_step(&t2s, &s2, &approve, &kyc, 1);  printf("  applied: kyc is current\n");
    sr_settle_step(&t2s, &s2, &approve, &sanc, 0); printf("  applied: not sanctioned\n");
    sr_settle_step(&t2s, &s2, &approve, &mkt, 1);  printf("  applied: market is open\n");
    printf("\n");
    show("approve", &approve, &s2, "  ");
    sr_settle_commit(&t2s, &approve, &s2, &r2);
    printf("  commit allowed=%s   value returned=%u  (0 is \"no value\", not \"false\")\n",
           pybool(r2.allowed), r2.value);
    printf("\n");
    printf("  Three facts in, and it will not answer. Limit and collateral are\n");
    printf("  unknown, so the worlds disagree, so there is nothing to pass on.\n");
  }

  /* ---- E ------------------------------------------------------------ */
  rule("E. asked to guess anyway");
  {
    double taus[3] = {0.50, 0.25, 0.00};
    unsigned k;
    for (k = 0u; k < 3u; k++) {
      sr_state_t probe, cp;
      sm_ancestry_t anc;
      sr_result_t rr, back;
      full(&probe);
      sr_observe(&probe, &kyc, 1);
      sr_observe(&probe, &sanc, 0);
      sr_observe(&probe, &mkt, 1);
      sm_ancestry_clear(&anc);
      sr_checkpoint(&probe, &cp);
      sr_speculate(&probe, &approve, taus[k], 20260905u, SM_INTENSITY_SUPPORT, 3, &anc, &rr);
      if (rr.allowed) {
        printf("  tau=%.2f  GUESSED %-5s debt=%.1f bit   witness still says %s\n",
               taus[k], boolname(rr.value), sm_ancestry_bits(&anc),
               VNAME[rr.witness.verdict]);
        sr_retract(&probe, &cp, &anc, 3);
        sr_ask(&approve, &probe, &back);
        printf("           retracted -> %s, debt=%.1f\n", VNAME[back.verdict],
               sm_ancestry_bits(&anc));
      } else {
        printf("  tau=%.2f  refused   verdict=%s  S=%.2f < tau, worlds untouched (%u)\n",
               taus[k], VNAME[rr.verdict], rr.S, sr_live_count(&probe));
      }
    }
  }
  printf("\n");
  printf("  S is 0 here because nothing about approve has been ruled out. So\n");
  printf("  any bar above zero refuses. At tau=0 it will guess, and the guess\n");
  printf("  is uniform, carries a recorded debt, and its own witness declines\n");
  printf("  to back it.\n");

  /* ---- F ------------------------------------------------------------ */
  rule("F. structure alone, where a score cannot follow");
  {
    sr_state_t s3;
    full(&s3);
    show("within limit", &lim, &s3, "  ");
    show("collateral", &col, &s3, "  ");
    printf("  both operands: undetermined, S = 0.00, answer set {false, true}\n\n");
    sr_map1(&NOT, &lim, &tmp);
    sr_map2(&OR, &lim, &tmp, &tmp2);  show("limit or not limit", &tmp2, &s3, "  ");
    sr_map2(&AND, &lim, &tmp, &tmp2); show("limit and not limit", &tmp2, &s3, "  ");
    sr_map2(&OR, &lim, &col, &tmp2);  show("limit or collateral", &tmp2, &s3, "  ");
  }
  printf("\n");
  printf("  Three composites. Identical operand answer sets and identical\n");
  printf("  operand supports. Derived true, derived false, and undetermined.\n");
  printf("  No function of the operands returns three values for one input,\n");
  printf("  so nothing that combines per-claim confidences reproduces this\n");
  printf("  row. The kernel gets it by composing the questions and asking\n");
  printf("  once.\n");

  /* ---- G ------------------------------------------------------------ */
  rule("G. context that conflicts");
  {
    sr_state_t s4;
    sr_settle_t t4;
    sr_result_t r4;
    full(&s4);
    sr_settle_begin(&t4, &s4, &approve);
    sr_settle_step(&t4, &s4, &approve, &sanc, 1);
    printf("  applied: counterparty IS sanctioned\n");
    sr_settle_step(&t4, &s4, &approve, &sanc, 0);
    printf("  applied: counterparty is NOT sanctioned\n");
    sr_settle_commit(&t4, &approve, &s4, &r4);
    printf("\n  verdict=%s  live=%u  contradicted=%s  commit allowed=%s\n\n",
           VNAME[r4.verdict], t4.live_now, pybool(t4.contradicted), pybool(r4.allowed));
    printf("  Not low confidence, and not a tie to be broken. A fourth verdict:\n");
    printf("  the constraints admit no world at all. The incoming claim was\n");
    printf("  checked against what was already held instead of overwriting it.\n");
  }

  printf("\n");
  for (i = 0u; i < 78u; i++) putchar('=');
  printf("\nEvery answer above came from counting the worlds that survived.\n");
  printf("No score was combined with another score at any point.\n");
  return 0;
}
