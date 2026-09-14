/*
 * test_dataset.c -- dataset.py's _self_check, in C.
 *
 * The generator's own claims, checked: training never reaches into the
 * hole, every hole point in the test set is no_grounds, each category
 * carries the uncertainties it claims, the ambiguous labels are a fair
 * coin, and a seed reproduces. The summary lines are compared with the
 * Python version's.
 *
 * With --dump SEED N TRAIN it instead prints every sample at full
 * precision, for diffing sample by sample against dataset.py.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dataset.h"

static ds_sample_t TRAIN[4000], TEST[4000], A[500], B[500];

static int fail(const char *msg) {
  printf("self-check FAILED: %s\n", msg);
  return 1;
}

static void summarise(const char *tag, const ds_sample_t *s, size_t n) {
  unsigned c[3] = {0u, 0u, 0u};
  size_t i;
  for (i = 0u; i < n; i++) c[s[i].category]++;
  printf("  %s {'clean': %u, 'ambiguous': %u, 'no_grounds': %u}\n", tag, c[0], c[1], c[2]);
}

int main(int argc, char **argv) {
  size_t i, n_amb = 0u, ones = 0u;
  double balance;

  if (argc == 5 && strcmp(argv[1], "--dump") == 0) {
    size_t n = (size_t)strtoul(argv[3], NULL, 10);
    ds_sample_t *s = (ds_sample_t *)malloc(n * sizeof *s);
    if (s == NULL) return 1;
    ds_generate(s, n, (uint64_t)strtoull(argv[2], NULL, 10), 0.08, atoi(argv[4]));
    for (i = 0u; i < n; i++) {
      printf("%.17g %.17g %d %s\n", s[i].x, s[i].y, s[i].label, DS_NAME[s[i].category]);
    }
    free(s);
    return 0;
  }

  ds_generate(TRAIN, 4000u, 1u, 0.08, 1);
  ds_generate(TEST, 4000u, 2u, 0.08, 0);

  for (i = 0u; i < 4000u; i++) {
    if (ds_in_hole(TRAIN[i].x, TRAIN[i].y))
      return fail("the training set reaches into the excluded region, so it is not excluded");
  }
  for (i = 0u; i < 4000u; i++) {
    const ds_sample_t *s = &TEST[i];
    if (ds_in_hole(s->x, s->y) && s->category != DS_NO_GROUNDS)
      return fail("a point in the excluded region was not labelled no_grounds");
    if (s->category == DS_AMBIGUOUS) {
      if (!(fabs(ds_boundary(s->x, s->y)) <= 0.08 + 1e-9)) return fail("ambiguous away from the boundary");
      if (!(s->aleatoric == 1.0 && s->epistemic == 0.0)) return fail("ambiguous uncertainties");
      n_amb++;
      ones += (size_t)s->label;
    }
    if (s->category == DS_NO_GROUNDS && !(s->epistemic == 1.0 && s->aleatoric == 0.0))
      return fail("no_grounds uncertainties");
    if (s->category == DS_CLEAN && !(s->aleatoric == 0.0 && s->epistemic == 0.0))
      return fail("clean uncertainties");
  }
  /* The ambiguous class must actually be near a coin flip, or it is not
     aleatoric and the experiment measures nothing. */
  balance = (double)ones / (double)(n_amb > 0u ? n_amb : 1u);
  if (!(0.4 < balance && balance < 0.6)) return fail("ambiguous labels are not a coin flip");

  /* Determinism, because a seed that does not reproduce proves nothing. */
  ds_generate(A, 500u, 7u, 0.08, 0);
  ds_generate(B, 500u, 7u, 0.08, 0);
  if (memcmp(A, B, sizeof A) != 0) return fail("not reproducible");

  printf("self-check passed\n");
  summarise("train", TRAIN, 4000u);
  summarise("test ", TEST, 4000u);
  printf("  ambiguous label balance: %.3f  (a fair coin, by construction)\n", balance);
  return 0;
}
