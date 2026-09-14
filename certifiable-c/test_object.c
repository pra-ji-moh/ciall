/*
 * test_object.c -- worlds made of things, against what should come out.
 *
 * Each world below is built from a rule this file knows and the module
 * does not. Checked:
 *   quantified facts   the angle sum of a triangle, and that every thing in
 *                      a ring is joined to exactly two, come out top
 *   counting           a law that needs "how many things a condition holds
 *                      of" is found, condition and all
 *   joins              what holds across every join is found in a word
 *   nothing said       a statement true of every scene it can imagine is
 *                      never reported as something it knows
 *   ordering           what it reports is ordered by how much it rules out
 *   errors             empty worlds and NULL arguments are checked
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_object.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static ob_world_t W;

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

static int mentions(const ob_facts_t *f, const char *what) {
  unsigned i;
  for (i = 0u; i < f->n; i++) {
    if (strstr(f->text[i], what) != 0) return 1;
  }
  return 0;
}

int main(void) {
  unsigned k, i;
  ob_facts_t F;
  ob_law_t L;

  printf("THINGS, AND SAYING SOMETHING ABOUT ALL OF THEM\n\n");

  /* ---- triangles --------------------------------------------------------- */
  ob_world_init(&W);
  ob_attr(&W, "angle", 0.0, 180.0, 1, &k);
  triangle(60, 60, 60);
  triangle(90, 45, 45);
  triangle(30, 60, 90);
  triangle(100, 40, 40);
  triangle(20, 70, 90);
  check("the angle sum of a triangle is found, with no geometry given",
        ob_facts(&W, &F) == SM_OK && mentions(&F, "180 == (total angle") &&
            mentions(&F, "(total angle == (60 * how many things))"));
  check("and that a triangle has three parts is pinned down with it",
        mentions(&F, "(3 =="));
  {
    int ordered = 1;
    for (i = 1u; i < F.n; i++) {
      if (F.ruled_out[i] > F.ruled_out[i - 1u]) ordered = 0;
    }
    check("what it reports is ordered by how much each fact rules out", ordered);
  }
  check("nothing true of every scene it can imagine is reported as known",
        !mentions(&F, "(0 <= how many things)") && !mentions(&F, "(0 <= total angle)"));

  /* ---- a ring ------------------------------------------------------------ */
  ob_world_init(&W);
  ob_attr(&W, "mark", 0.0, 3.0, 1, &k);
  ring(4u);
  ring(5u);
  ring(6u);
  ring(7u);
  check("what makes a ring a ring is found: every thing joined to two others",
        ob_facts(&W, &F) == SM_OK && mentions(&F, "every x: (2 == x's joins)"));
  check("and what it settles that against is every scene there is, written out",
        F.universe > 10000u && F.universe_items == 3u && F.ruled_out[0] > F.universe / 2u);

  /* ---- a word ------------------------------------------------------------ */
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
    check("in a word, what holds across every join is found",
          ob_facts(&W, &F) == SM_OK && mentions(&F, "every joined x and y: (x.letter <= y.letter)"));
    check("including that the joins run one way only",
          mentions(&F, "every joined x and y: not (y joined to x)"));
  }

  /* ---- counting, which it has to invent ---------------------------------- */
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
    check("a count of the things a condition holds of is found, condition and all",
          ob_find_law(&W, &L) == SM_OK && L.found &&
              strcmp(L.law, "how many x with ((0 == (x.value % 2)))") == 0);
    check("it says how much of what it could have said was eliminated",
          L.support > 0.99 && L.candidates > 100u);
    check("and names another account that fits what it saw just as well",
          L.has_rival && strlen(L.rival) > 0u);
  }

  /* ---- nothing to find --------------------------------------------------- */
  {
    double a[1];
    unsigned idx;
    ob_world_init(&W);
    ob_attr(&W, "v", 0.0, 4.0, 1, &k);
    ob_scene(&W);
    a[0] = 1.0;
    ob_item(&W, a, &idx);
    ob_says(&W, 5.0);
    ob_scene(&W);
    a[0] = 1.0;
    ob_item(&W, a, &idx);
    ob_says(&W, 7.0);
    check("two scenes alike with different outcomes leave no description",
          ob_find_law(&W, &L) == SM_OK && !L.found);
  }

  ob_world_init(&W);
  check("a world with nothing in it is a checked error",
        ob_facts(&W, &F) == SM_ERR_EMPTY_DOMAIN && ob_find_law(&W, &L) == SM_ERR_EMPTY_DOMAIN);
  {
    double a[1];
    unsigned idx;
    a[0] = 0.0;
    check("an item outside any scene, and a join to nothing, are checked errors",
          ob_item(&W, a, &idx) == SM_ERR_EMPTY_DOMAIN && ob_says(&W, 1.0) == SM_ERR_EMPTY_DOMAIN &&
              ob_join(&W, 0u, 1u) == SM_ERR_EMPTY_DOMAIN);
  }
  check("NULL arguments are checked errors",
        ob_facts(0, &F) == SM_ERR_NULL_ARGUMENT && ob_find_law(&W, 0) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
