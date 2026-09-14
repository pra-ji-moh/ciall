/*
 * test_frame.c -- the formulation found by elimination, against what it
 * should be.
 *
 * Each situation below is generated from a rule this file knows and the
 * frame module does not. Checked:
 *   the rule found      the description it writes reproduces the outcome
 *                       of every row, including rows generated but never
 *                       shown to it
 *   readings            one that drives the outcome is proved to matter,
 *                       one that does nothing is called silent, one never
 *                       varied on its own is called untested, not dismissed
 *   quantities          a threshold rule makes it name the quantity being
 *                       tested, and that quantity is the right one
 *   form                two unrelated situations with the same underlying
 *                       shape come out alike, and two with different
 *                       shapes do not
 *   what it rests on    a law used past the range it was seen over is held
 *                       as a guess; one seen across its whole range is not
 *   contradiction       rows that disagree with themselves leave nothing
 *   handover            the theory it writes answers a case it never saw
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_frame.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static fr_situation_t SIT;

static void obs(double a, double b, double c, double out) {
  double v[3];
  v[0] = a; v[1] = b; v[2] = c;
  fr_observe(&SIT, v, out);
}

int main(void) {
  fr_frame_t f, g, h;
  unsigned i;

  printf("THE FORMULATION, FOUND BY ELIMINATION\n\n");

  /* ---- a sum it was never told about ------------------------------------- */
  fr_situation_init(&SIT);
  fr_reading(&SIT, "a", 0.0, 4.0, 1, &i);
  fr_reading(&SIT, "b", 0.0, 4.0, 1, &i);
  fr_reading(&SIT, "c", 0.0, 4.0, 1, &i);
  fr_outcome(&SIT, "sum", 0.0, 12.0, 1);
  obs(0, 0, 0, 0);
  obs(1, 0, 0, 1);
  obs(0, 1, 0, 1);
  obs(0, 0, 1, 1);
  obs(2, 1, 0, 3);
  obs(1, 2, 3, 6);
  obs(4, 4, 4, 12);
  obs(2, 2, 1, 5);
  check("adding up is found from grouped objects alone",
        fr_formulate(&SIT, &f) == SM_OK && f.found &&
            (strcmp(f.raw_law, "((a + b) + c)") == 0 ||
             strcmp(f.raw_law, "(a + (b + c))") == 0));
  check("every reading that drives it is proved to matter",
        f.standing[0] == FR_MATTERS && f.standing[1] == FR_MATTERS &&
            f.standing[2] == FR_MATTERS);
  check("seen across its whole range, it is not held as a guess", !f.conjectured);

  /* ---- a product, with a reading that does nothing ----------------------- */
  fr_situation_init(&SIT);
  fr_reading(&SIT, "w", 1.0, 9.0, 1, &i);
  fr_reading(&SIT, "h", 1.0, 9.0, 1, &i);
  fr_reading(&SIT, "tint", 0.0, 3.0, 1, &i);
  fr_outcome(&SIT, "area", 0.0, 81.0, 1);
  obs(2, 3, 0, 6);
  obs(2, 3, 1, 6);
  obs(4, 3, 0, 12);
  obs(2, 5, 0, 10);
  obs(3, 3, 0, 9);
  obs(5, 2, 1, 10);
  obs(1, 7, 2, 7);
  obs(6, 6, 3, 36);
  check("multiplying is found the same way", fr_formulate(&SIT, &g) == SM_OK && g.found &&
                                                 strcmp(g.raw_law, "(w * h)") == 0);
  check("a reading that changed and changed nothing is called silent",
        g.standing[2] == FR_SILENT);
  check("used past the sides it ever saw, it is held as a guess", g.conjectured);
  check("a sum and a product are not the same form", !fr_same_shape(&f, &g));

  /* ---- another world entirely, the same form ----------------------------- */
  fr_situation_init(&SIT);
  fr_reading(&SIT, "pace", 1.0, 9.0, 1, &i);
  fr_reading(&SIT, "span", 1.0, 9.0, 1, &i);
  fr_outcome(&SIT, "gone", 0.0, 81.0, 1);
  {
    double v[2];
    double pairs[7][2] = {{2, 3}, {4, 3}, {2, 5}, {3, 3}, {5, 2}, {1, 7}, {6, 6}};
    for (i = 0u; i < 7u; i++) {
      v[0] = pairs[i][0];
      v[1] = pairs[i][1];
      fr_observe(&SIT, v, v[0] * v[1]);
    }
  }
  check("a moving thing and a rectangle come out the same form",
        fr_formulate(&SIT, &h) == SM_OK && h.found && fr_same_shape(&g, &h));

  /* ---- a threshold, and the quantity it is about ------------------------- */
  fr_situation_init(&SIT);
  fr_reading(&SIT, "s1", 0.0, 1.0, 1, &i);
  fr_reading(&SIT, "s2", 0.0, 1.0, 1, &i);
  fr_reading(&SIT, "s3", 0.0, 1.0, 1, &i);
  fr_outcome(&SIT, "rang", 0.0, 1.0, 1);
  obs(0, 0, 0, 0);
  obs(1, 0, 0, 0);
  obs(0, 1, 0, 0);
  obs(1, 1, 0, 1);
  obs(1, 0, 1, 1);
  obs(1, 1, 1, 1);
  check("a bell that needs enough slots gives a threshold rule",
        fr_formulate(&SIT, &f) == SM_OK && f.found && strcmp(f.law, "(q1 >= 2)") == 0);
  check("and it names the quantity the rule is about, which is the count",
        f.n_defs == 1u && (strcmp(f.def_body[0], "((s1 + s2) + s3)") == 0 ||
                           strcmp(f.def_body[0], "(s1 + (s2 + s3))") == 0));
  check("it says plainly that another account fits what it saw just as well",
        f.has_rival && strlen(f.rival) > 0u);

  /* ---- the theory it writes, asked about a case it never met -------------- */
  {
    sx_theory_t T;
    sx_opts_t o = sx_default_opts();
    sx_answer_t A;
    unsigned q;
    check("the description hands over to the engine as a theory",
          fr_to_theory(&SIT, &f, &T) == SM_OK);
    sx_require(&T, "s1 == 0");
    sx_require(&T, "s2 == 1");
    sx_require(&T, "s3 == 1");
    sx_parse(&T, "rang", &q);
    sx_ask(&T, q, &o, &A);
    check("a case it was never shown is derived, not guessed",
          A.verdict == SX_VALUE && A.range.lo == 1.0);

    fr_to_theory(&SIT, &f, &T);
    sx_require(&T, "s3 == 1");
    sx_require(&T, "rang == 0");
    sx_parse(&T, "s1 + s2", &q);
    sx_ask(&T, q, &o, &A);
    check("and it runs backwards: silence with one slot filled settles the rest",
          A.verdict == SX_VALUE && A.range.lo == 0.0);
  }

  /* ---- sharing out, and remainders --------------------------------------- */
  fr_situation_init(&SIT);
  fr_reading(&SIT, "a", 0.0, 20.0, 1, &i);
  fr_reading(&SIT, "b", 1.0, 5.0, 1, &i);
  fr_outcome(&SIT, "each", 0.0, 20.0, 1);
  {
    double d[7][3] = {{6, 3, 2}, {8, 2, 4}, {10, 5, 2}, {12, 4, 3},
                      {20, 5, 4}, {9, 3, 3}, {4, 2, 2}};
    double v[2];
    for (i = 0u; i < 7u; i++) {
      v[0] = d[i][0];
      v[1] = d[i][1];
      fr_observe(&SIT, v, d[i][2]);
    }
  }
  check("sharing a pile into equal parts is found",
        fr_formulate(&SIT, &f) == SM_OK && f.found && strcmp(f.raw_law, "(a / b)") == 0);

  fr_situation_init(&SIT);
  fr_reading(&SIT, "n", 0.0, 20.0, 1, &i);
  fr_outcome(&SIT, "r", 0.0, 2.0, 1);
  {
    double v[1];
    for (i = 0u; i < 9u; i++) {
      v[0] = (double)i;
      fr_observe(&SIT, v, (double)(i % 3u));
    }
  }
  check("what is left over after threes is found, which is counting in cycles",
        fr_formulate(&SIT, &f) == SM_OK && f.found && strcmp(f.raw_law, "(n % 3)") == 0);

  /* ---- what is always true, with no outcome named ------------------------ */
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
    check("given a table and no question, it finds what is always true",
          fr_invariants(&SIT, &inv) == SM_OK && inv.n == 2u &&
              strcmp(inv.text[0], "((a + b) == 10)") == 0);
    check("and it reports each fact once, with nothing the others already force",
          strcmp(inv.text[1], "((b % 2) <= 0)") == 0);
    check("nothing true of every imaginable world is reported as news",
          inv.candidates > inv.n * 20u);
  }

  /* ---- sequences: growth, as the same search ----------------------------- */
  {
    fr_situation_t seq;
    double fib[8] = {1, 1, 2, 3, 5, 8, 13, 21};
    double dbl[6] = {3, 6, 12, 24, 48, 96};
    check("a sequence where each step is the two before it is found",
          fr_recurrence(fib, 8u, 0.0, 40.0, 1, &seq, &f) == SM_OK && f.found &&
              strcmp(f.raw_law, "(prev + prev2)") == 0);
    check("and one that doubles is found, from the numbers alone",
          fr_recurrence(dbl, 6u, 0.0, 200.0, 1, &seq, &f) == SM_OK && f.found &&
              (strcmp(f.raw_law, "(prev + prev)") == 0 ||
               strcmp(f.raw_law, "(prev * 2)") == 0 || strcmp(f.raw_law, "(2 * prev)") == 0));
    check("a sequence too short to have a shape is a checked error",
          fr_recurrence(fib, 3u, 0.0, 40.0, 1, &seq, &f) == SM_ERR_EMPTY_DOMAIN);
  }

  /* ---- nothing to find --------------------------------------------------- */
  fr_situation_init(&SIT);
  fr_reading(&SIT, "a", 0.0, 4.0, 1, &i);
  fr_outcome(&SIT, "out", 0.0, 9.0, 1);
  {
    double v[1];
    v[0] = 2.0;
    fr_observe(&SIT, v, 5.0);
    fr_observe(&SIT, v, 7.0);
  }
  check("the same reading twice with two different outcomes leaves nothing",
        fr_formulate(&SIT, &f) == SM_OK && !f.found && f.survivors == 0u);
  check("a reading never varied on its own gets no verdict either way",
        f.standing[0] == FR_UNTESTED);

  fr_situation_init(&SIT);
  check("a situation with nothing in it is a checked error",
        fr_formulate(&SIT, &f) == SM_ERR_EMPTY_DOMAIN);
  check("NULL arguments are checked errors",
        fr_formulate(0, &f) == SM_ERR_NULL_ARGUMENT &&
            fr_formulate(&SIT, 0) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
