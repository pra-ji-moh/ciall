/*
 * dataset.c -- see dataset.h.
 */

#include "dataset.h"

#include <math.h>

#include "pyrand.h"

const char *const DS_NAME[3] = {"clean", "ambiguous", "no_grounds"};

static const double TWO_PI = 6.283185307179586;   /* 2 * math.pi, as a double */

double ds_boundary(double x, double y) {
  return hypot(x, y) - 1.0;
}

/* Python's float %: fmod, then shifted into the sign of the divisor. */
static double pymod(double v, double w) {
  double m = fmod(v, w);
  if (m != 0.0 && ((w < 0.0) != (m < 0.0))) m += w;
  return m;
}

int ds_in_hole(double x, double y) {
  double angle = pymod(atan2(y, x), TWO_PI);
  return DS_HOLE_START <= angle && angle <= DS_HOLE_END;
}

void ds_generate(ds_sample_t *out, size_t n, uint64_t seed, double margin, int train) {
  pyrand_t rng;
  size_t k = 0u;
  pyrand_seed(&rng, seed);
  while (k < n) {
    /* a point in the disc of radius 2, uniform by area */
    double r = 2.0 * sqrt(pyrand_random(&rng));
    double theta = pyrand_uniform(&rng, 0.0, TWO_PI);
    double x = r * cos(theta), y = r * sin(theta);
    double d = ds_boundary(x, y);
    ds_sample_t *s = &out[k];
    if (ds_in_hole(x, y)) {
      if (train) continue;   /* the model must never see this region */
      s->x = x; s->y = y; s->label = d > 0.0;
      s->category = DS_NO_GROUNDS; s->aleatoric = 0.0; s->epistemic = 1.0;
      k++;
      continue;
    }
    if (fabs(d) <= margin) {
      /* genuinely undecided: a fair coin, no evidence could resolve it */
      s->x = x; s->y = y; s->label = (int)pyrand_randbelow(&rng, 2u);
      s->category = DS_AMBIGUOUS; s->aleatoric = 1.0; s->epistemic = 0.0;
      k++;
      continue;
    }
    s->x = x; s->y = y; s->label = d > 0.0;
    s->category = DS_CLEAN; s->aleatoric = 0.0; s->epistemic = 0.0;
    k++;
  }
}
