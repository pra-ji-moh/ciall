/*
 * demo_object.c -- worlds made of things, not of columns.
 *
 * Four kinds of thing that no table can hold: a set, a triangle, a ring,
 * and a word. Each arrives as items carrying numbers, with joins between
 * them, and nothing said about what any of it means. What comes back is
 * quantified: statements about every item, about every two items, about
 * every join. The elimination is the same one as everywhere else.
 */

#include <stdio.h>

#include "smarsh_object.h"

static ob_world_t W;

static void head(const char *t) { printf("\n%s\n", t); }

static void set_of(const int *v, unsigned n, double outcome) {
  double a[1];
  unsigned i, k;
  ob_scene(&W);
  for (k = 0u; k < n; k++) {
    a[0] = (double)v[k];
    ob_item(&W, a, &i);
  }
  ob_says(&W, outcome);
}

static void triangle(double p, double q, double r) {
  double a[1];
  unsigned i;
  ob_scene(&W);
  a[0] = p; ob_item(&W, a, &i);
  a[0] = q; ob_item(&W, a, &i);
  a[0] = r; ob_item(&W, a, &i);
}

static void ring(unsigned n) {
  double a[1];
  unsigned i, idx[OB_MAX_ITEMS];
  ob_scene(&W);
  for (i = 0u; i < n; i++) {
    a[0] = (double)(i % 2u);
    ob_item(&W, a, &idx[i]);
  }
  for (i = 0u; i < n; i++) {
    ob_join(&W, idx[i], idx[(i + 1u) % n]);
    ob_join(&W, idx[(i + 1u) % n], idx[i]);
  }
}

static void word(const int *letters, unsigned n) {
  double a[1];
  unsigned i, idx[OB_MAX_ITEMS];
  ob_scene(&W);
  for (i = 0u; i < n; i++) {
    a[0] = (double)letters[i];
    ob_item(&W, a, &idx[i]);
  }
  for (i = 0u; i + 1u < n; i++) ob_join(&W, idx[i], idx[i + 1u]);
}

int main(void) {
  unsigned k;
  ob_law_t L;
  ob_facts_t F;

  printf("THINGS, AND SAYING SOMETHING ABOUT ALL OF THEM\n");
  printf("=============================================\n");
  printf("Nothing below is a row of numbers. Each is a collection of things\n");
  printf("with joins between them, and nothing is said about what they are.\n");

  /* ---- a set, and something to predict about it -------------------------- */
  head("ONE. Sets of numbers, and a count nobody explains.");
  {
    int s1[3] = {2, 4, 7};
    int s2[4] = {1, 3, 5, 9};
    int s3[3] = {2, 2, 2};
    int s4[5] = {1, 2, 3, 4, 5};
    int s5[1] = {8};
    int s6[4] = {7, 7, 4, 4};
    ob_world_init(&W);
    ob_attr(&W, "value", 0.0, 9.0, 1, &k);
    ob_outcome_name(&W, "evens");
    set_of(s1, 3u, 2.0);
    set_of(s2, 4u, 0.0);
    set_of(s3, 3u, 3.0);
    set_of(s4, 5u, 2.0);
    set_of(s5, 1u, 1.0);
    set_of(s6, 4u, 2.0);
    ob_find_law(&W, &L);
    ob_report_law(&W, &L);
    printf("  it had to invent the idea of counting the things a condition\n");
    printf("  holds of, and the condition, before it could say that\n");
  }

  /* ---- triangles --------------------------------------------------------- */
  head("TWO. Five triangles, given as three things each carrying an angle.");
  ob_world_init(&W);
  ob_attr(&W, "angle", 0.0, 180.0, 1, &k);
  triangle(60, 60, 60);
  triangle(90, 45, 45);
  triangle(30, 60, 90);
  triangle(100, 40, 40);
  triangle(20, 70, 90);
  ob_facts(&W, &F);
  ob_report_facts(&W, &F);
  printf("  nothing was said about geometry, angles or straight lines.\n");
  printf("  \"for every triangle the angles come to 180\" is the description\n");
  printf("  that survived when the others did not\n");

  /* ---- a ring ------------------------------------------------------------ */
  head("THREE. Four rings of things joined in a loop, of different lengths.");
  ob_world_init(&W);
  ob_attr(&W, "mark", 0.0, 3.0, 1, &k);
  ring(4u);
  ring(5u);
  ring(6u);
  ring(7u);
  ob_facts(&W, &F);
  ob_report_facts(&W, &F);
  printf("  being a ring is not a word it was given. It is what is left\n");
  printf("  when every other account of those joins has been eliminated\n");

  /* ---- a word ------------------------------------------------------------ */
  head("FOUR. Words: things carrying a letter, joined in order.");
  {
    int w1[4] = {1, 2, 3, 4};
    int w2[3] = {5, 6, 7};
    int w3[5] = {2, 3, 4, 5, 6};
    int w4[3] = {7, 8, 9};
    ob_world_init(&W);
    ob_attr(&W, "letter", 0.0, 9.0, 1, &k);
    word(w1, 4u);
    word(w2, 3u);
    word(w3, 5u);
    word(w4, 3u);
    ob_facts(&W, &F);
    ob_report_facts(&W, &F);
    printf("  order is not built in anywhere. A word is things and joins,\n");
    printf("  and what holds across every join is found like anything else\n");
  }

  head("What this adds.");
  printf("  a set, a triangle, a ring and a word are one kind of situation,\n");
  printf("  and a description can now speak of all their parts at once.\n");
  return 0;
}
