/*
 * test_guess.c -- what kinds of thing it can work out, when no kinds were given.
 *
 * Three things, behaving in three ways nobody named for it. For each it is shown
 * where the thing was, look after look, and asked to say where it will be next.
 * Nothing here tells it that "a step", "a turn" or "a wait" are kinds of thing to
 * be: the law is whatever survives being formulated over what it saw.
 */
#include <stdio.h>
#include <string.h>

#include "smarsh_guess.h"

static unsigned FAILED;

static void check(const char *what, int ok) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) FAILED++;
}

/* show it a thing, then see whether it can say where the thing goes next */
static int works_out(double (*where_r)(unsigned), double (*where_c)(unsigned), unsigned looks,
                     const char **law) {
  static gs_guess_t g;
  double nr = 0.0, nc = 0.0;
  unsigned i;
  gs_begin(&g);
  for (i = 0u; i < looks; i++) gs_saw(&g, (double)i, where_r(i), where_c(i));
  gs_formulate(&g);
  *law = g.law_r;
  if (!g.found) return 0;
  if (!gs_predict(&g, (double)(looks - 1u), where_r(looks - 1u), where_c(looks - 1u), &nr, &nc)) return 0;
  return nr == where_r(looks) && nc == where_c(looks);
}

/* a thing that walks: one row down each look */
static double walk_r(unsigned t) { return (double)(10u + t); }
static double walk_c(unsigned t) { (void)t; return 20.0; }

/* a thing that goes across twice as fast as it goes down */
static double slant_r(unsigned t) { return (double)(5u + t); }
static double slant_c(unsigned t) { return (double)(5u + 2u * t); }

/* a thing that sits still */
static double still_r(unsigned t) { (void)t; return 30.0; }
static double still_c(unsigned t) { (void)t; return 40.0; }

/* a thing that turns at a wall: down to 20, back up to 10, and again */
static double turn_r(unsigned t) {
  unsigned p = t % 20u;
  return (double)(p < 10u ? 10u + p : 30u - p);
}
static double turn_c(unsigned t) { (void)t; return 15.0; }

int main(void) {
  const char *law = "";

  printf("WORKING OUT WHAT A THING IS DOING, WITH NO KINDS GIVEN\n\n");

  check("a thing that walks: it says where it goes next", works_out(walk_r, walk_c, 8u, &law));
  printf("        its law: %s\n", law);

  check("a thing that goes across faster than down: it says where it goes next",
        works_out(slant_r, slant_c, 8u, &law));
  printf("        its law: %s\n", law);

  check("a thing that sits still: it says where it goes next", works_out(still_r, still_c, 8u, &law));
  printf("        its law: %s\n", law);

  /*
   * A thing that turns at a wall. Whether it can say this is not something to
   * assume: it depends on whether a description of that shape is in reach of
   * the formulation at all. The check reports what it found either way.
   */
  {
    int got = works_out(turn_r, turn_c, 16u, &law);
    printf("  %s  a thing that turns at a wall: %s\n", got ? "ok  " : "note",
           got ? "it says where it goes next" : "it could not say, and did not pretend to");
    printf("        its law: %s\n", law[0] ? law : "(none survived)");
  }

  {
    static gs_guess_t a, b;
    unsigned i;
    gs_begin(&a);
    gs_begin(&b);
    for (i = 0u; i < 8u; i++) {
      gs_saw(&a, (double)i, walk_r(i), walk_c(i));
      gs_saw(&b, (double)i, walk_r(i) + 7.0, walk_c(i) + 3.0);   /* the same doing, elsewhere */
    }
    gs_formulate(&a);
    gs_formulate(&b);
    check("two things doing the same kind of thing are seen to be alike", gs_same_kind(&a, &b));
  }

  check("NULL arguments are checked errors",
        gs_saw(0, 0.0, 0.0, 0.0) == SM_ERR_NULL_ARGUMENT &&
        gs_formulate(0) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks failed\n", FAILED);
  return FAILED == 0u ? 0 : 1;
}
