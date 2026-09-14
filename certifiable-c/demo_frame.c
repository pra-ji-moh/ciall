/*
 * demo_frame.c -- it is handed numbers off four unrelated worlds, with no
 * hint of what they mean, and writes its own description of each.
 *
 * Nothing here tells it which readings matter, what quantities exist, or
 * what shape the law has. It finds all of that by eliminating the
 * descriptions that disagree with what it saw. What is left it hands to
 * the engine, which then answers questions about situations it never met.
 */

#include <stdio.h>

#include "smarsh_frame.h"
#include "smarsh_space.h"

static fr_situation_t SIT;
static fr_frame_t RECT, TRAVEL;

static void head(const char *t) { printf("\n%s\n", t); }

static void row3(fr_situation_t *s, double a, double b, double c, double out) {
  double v[3];
  v[0] = a; v[1] = b; v[2] = c;
  fr_observe(s, v, out);
}

static void row2(fr_situation_t *s, double a, double b, double out) {
  double v[2];
  v[0] = a; v[1] = b;
  fr_observe(s, v, out);
}

int main(void) {
  fr_frame_t f;
  sx_theory_t T;
  sx_opts_t o = sx_default_opts();
  sx_answer_t A;
  unsigned q;
  unsigned i;

  printf("FINDING THE FORMULATION\n");
  printf("=======================\n");
  printf("Four worlds, given as bare numbers. It is told nothing else.\n");

  /* ---- pebbles in trays: where arithmetic comes from --------------------- */
  head("ONE. Three trays of pebbles, and how many there are altogether.");
  fr_situation_init(&SIT);
  fr_reading(&SIT, "t1", 0.0, 4.0, 1, &i);
  fr_reading(&SIT, "t2", 0.0, 4.0, 1, &i);
  fr_reading(&SIT, "t3", 0.0, 4.0, 1, &i);
  fr_outcome(&SIT, "total", 0.0, 12.0, 1);
  row3(&SIT, 0, 0, 0, 0);
  row3(&SIT, 1, 0, 0, 1);
  row3(&SIT, 0, 1, 0, 1);
  row3(&SIT, 0, 0, 1, 1);
  row3(&SIT, 2, 1, 0, 3);
  row3(&SIT, 1, 2, 3, 6);
  row3(&SIT, 4, 4, 4, 12);
  row3(&SIT, 2, 2, 1, 5);
  row3(&SIT, 3, 0, 2, 5);
  fr_formulate(&SIT, &f);
  fr_report(&SIT, &f);

  /* ---- rectangles, with a reading that means nothing --------------------- */
  head("TWO. Rectangles: two sides, a colour code, and the space inside.");
  fr_situation_init(&SIT);
  fr_reading(&SIT, "side_a", 1.0, 9.0, 1, &i);
  fr_reading(&SIT, "side_b", 1.0, 9.0, 1, &i);
  fr_reading(&SIT, "tint", 0.0, 3.0, 1, &i);
  fr_outcome(&SIT, "area", 0.0, 81.0, 1);
  row3(&SIT, 2, 3, 0, 6);
  row3(&SIT, 2, 3, 1, 6);
  row3(&SIT, 4, 3, 0, 12);
  row3(&SIT, 2, 5, 0, 10);
  row3(&SIT, 3, 3, 0, 9);
  row3(&SIT, 5, 2, 1, 10);
  row3(&SIT, 1, 7, 2, 7);
  row3(&SIT, 6, 6, 3, 36);
  fr_formulate(&SIT, &RECT);
  fr_report(&SIT, &RECT);

  /* ---- a moving thing: a different world entirely ------------------------ */
  head("THREE. Something moving: how fast, for how long, and how far it got.");
  fr_situation_init(&SIT);
  fr_reading(&SIT, "pace", 1.0, 9.0, 1, &i);
  fr_reading(&SIT, "span", 1.0, 9.0, 1, &i);
  fr_outcome(&SIT, "gone", 0.0, 81.0, 1);
  row2(&SIT, 2, 3, 6);
  row2(&SIT, 4, 3, 12);
  row2(&SIT, 2, 5, 10);
  row2(&SIT, 3, 3, 9);
  row2(&SIT, 5, 2, 10);
  row2(&SIT, 1, 7, 7);
  row2(&SIT, 6, 6, 36);
  fr_formulate(&SIT, &TRAVEL);
  fr_report(&SIT, &TRAVEL);

  head("The two above have nothing to do with each other.");
  if (fr_same_shape(&RECT, &TRAVEL)) {
    printf("  it wrote the same form for both: %s and %s\n", RECT.law, TRAVEL.law);
    printf("  that is the analogy, and no one built an analogy step:\n");
    printf("  both formulations were found by elimination and came out alike\n");
  } else {
    printf("  their forms differ\n");
  }

  /* ---- a bell that rings on enough: inventing the quantity --------------- */
  head("FOUR. Three slots and a bell. Nobody says what makes it ring.");
  fr_situation_init(&SIT);
  fr_reading(&SIT, "s1", 0.0, 1.0, 1, &i);
  fr_reading(&SIT, "s2", 0.0, 1.0, 1, &i);
  fr_reading(&SIT, "s3", 0.0, 1.0, 1, &i);
  fr_outcome(&SIT, "rang", 0.0, 1.0, 1);
  row3(&SIT, 0, 0, 0, 0);
  row3(&SIT, 1, 0, 0, 0);
  row3(&SIT, 0, 1, 0, 0);
  row3(&SIT, 1, 1, 0, 1);
  row3(&SIT, 1, 0, 1, 1);
  row3(&SIT, 1, 1, 1, 1);
  fr_formulate(&SIT, &f);
  fr_report(&SIT, &f);

  head("Now it is asked about the world it just described for itself.");
  if (fr_to_theory(&SIT, &f, &T) != SM_OK) {
    printf("  could not hand it over: %s\n", T.error);
    return 1;
  }
  printf("  the description it wrote, now the thing it thinks in\n");

  sx_require(&T, "s1 == 0");
  sx_require(&T, "s2 == 1");
  sx_require(&T, "s3 == 1");
  sx_parse(&T, "rang", &q);
  sx_ask(&T, q, &o, &A);
  printf("  a case it never saw, two of the other slots filled: rang = %.0f %s\n",
         A.range.lo, A.verdict == SX_VALUE ? "(derived)" : "(not settled)");

  sx_init(&T);
  fr_to_theory(&SIT, &f, &T);
  sx_require(&T, "s3 == 1");
  sx_require(&T, "rang == 0");
  sx_parse(&T, "s1 + s2", &q);
  sx_ask(&T, q, &o, &A);
  printf("  told only that it stayed silent with the third slot filled,\n");
  printf("  it works backwards: the other two hold %.0f %s\n",
         A.range.lo, A.verdict == SX_VALUE ? "(derived)" : "(not settled)");

  /* ---- dividing up, and what is left over -------------------------------- */
  head("FIVE. A pile shared out, and a count that goes round in cycles.");
  fr_situation_init(&SIT);
  fr_reading(&SIT, "pile", 0.0, 20.0, 1, &i);
  fr_reading(&SIT, "parts", 1.0, 5.0, 1, &i);
  fr_outcome(&SIT, "each", 0.0, 20.0, 1);
  {
    double d[7][3] = {{6, 3, 2}, {8, 2, 4}, {10, 5, 2}, {12, 4, 3},
                      {20, 5, 4}, {9, 3, 3}, {4, 2, 2}};
    for (i = 0u; i < 7u; i++) row2(&SIT, d[i][0], d[i][1], d[i][2]);
  }
  fr_formulate(&SIT, &f);
  printf("  %s is %s\n", "each", f.law);

  fr_situation_init(&SIT);
  fr_reading(&SIT, "n", 0.0, 20.0, 1, &i);
  fr_outcome(&SIT, "left", 0.0, 2.0, 1);
  {
    double v[1];
    for (i = 0u; i < 9u; i++) {
      v[0] = (double)i;
      fr_observe(&SIT, v, (double)(i % 3u));
    }
  }
  fr_formulate(&SIT, &f);
  printf("  %s is %s\n", "left", f.law);
  printf("  nobody mentioned division or remainders. They are just short\n");
  printf("  descriptions, and short descriptions are what survives\n");

  /* ---- a table with no question attached --------------------------------- */
  head("SIX. A table with no outcome column at all. What is always true here?");
  {
    fr_invariants_t inv;
    double v[2];
    int k;
    fr_situation_init(&SIT);
    fr_reading(&SIT, "a", 0.0, 10.0, 1, &i);
    fr_reading(&SIT, "b", 0.0, 10.0, 1, &i);
    for (k = 0; k <= 10; k += 2) {
      v[0] = (double)k;
      v[1] = 10.0 - (double)k;
      fr_observe(&SIT, v, 0.0);
    }
    fr_invariants(&SIT, &inv);
    printf("  out of %u statements it could make, %u stand on their own:\n",
           inv.candidates, inv.n);
    for (i = 0u; i < inv.n; i++) printf("    %s\n", inv.text[i]);
    printf("  everything true of any two numbers at all was thrown out as saying\n");
    printf("  nothing, and everything the others already force was thrown out too.\n");
    printf("  the engine proved both of those, so this is what it knows, not a list\n");
  }

  /* ---- sequences --------------------------------------------------------- */
  head("SEVEN. Numbers in a row, with no hint that order means anything.");
  {
    fr_situation_t seq;
    double fib[8] = {1, 1, 2, 3, 5, 8, 13, 21};
    double dbl[6] = {3, 6, 12, 24, 48, 96};
    fr_recurrence(fib, 8u, 0.0, 40.0, 1, &seq, &f);
    printf("  1 1 2 3 5 8 13 21   next is %s\n", f.law);
    fr_recurrence(dbl, 6u, 0.0, 200.0, 1, &seq, &f);
    printf("  3 6 12 24 48 96     next is %s\n", f.law);
    printf("  growth and recurrence are not a separate faculty either\n");
  }

  head("What this closes.");
  printf("  the description was the last place a person was still doing the work.\n");
  printf("  it is now found the same way everything else is: by elimination.\n");
  return 0;
}
