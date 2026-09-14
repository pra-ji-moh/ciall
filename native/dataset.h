/*
 * dataset.h -- ground truth by construction, in C. A port of dataset.py.
 *
 * 2D binary classification with an analytic boundary (the unit circle),
 * so the TRUE uncertainty of every sample is known and a model's estimate
 * can be scored against it. Three categories, and they are the whole
 * design:
 *
 *   CLEAN       inside the training support, away from the boundary:
 *               answer, confidently
 *   AMBIGUOUS   within `margin` of the boundary, where the generator flips
 *               a fair coin: answer, and be uncertain (abstaining is WRONG)
 *   NO_GROUNDS  from a wedge of angles training never sees: abstain
 *
 * Drawn through pyrand.c, so the samples are the ones dataset.py draws.
 */

#ifndef DATASET_H
#define DATASET_H

#include <stddef.h>
#include <stdint.h>

#define DS_HOLE_START 0.6
#define DS_HOLE_END 1.4

typedef enum { DS_CLEAN = 0, DS_AMBIGUOUS = 1, DS_NO_GROUNDS = 2 } ds_category_t;

typedef struct {
  double x, y;
  int label;
  ds_category_t category;
  double aleatoric;   /* 1.0 when the generator flipped a fair coin */
  double epistemic;   /* 1.0 when drawn from the excluded region */
} ds_sample_t;

double ds_boundary(double x, double y);
int ds_in_hole(double x, double y);

/* Fills out[0..n-1]. train != 0 excludes the hole entirely, which is what
   makes it a hole. */
void ds_generate(ds_sample_t *out, size_t n, uint64_t seed, double margin, int train);

extern const char *const DS_NAME[3];

#endif /* DATASET_H */
