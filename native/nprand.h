/*
 * nprand.h -- numpy's default_rng, reproduced exactly in C: SeedSequence
 * seeding, the PCG64 generator, and numpy's ziggurat standard normal.
 *
 * baseline.py initialises its network with numpy.random.default_rng(0).
 * With this, baseline.c draws the same initial weights numpy does, so its
 * default run is the Python run, not a different random start.
 *
 *   SeedSequence  hashes the seed's 32-bit words into a 4-word pool, then
 *                 expands the pool into PCG64's 128-bit state and stream
 *   PCG64         128-bit LCG, XSL-RR output; 128-bit arithmetic is done
 *                 in two 64-bit halves so this stays plain C99
 *   normal        numpy's 256-layer ziggurat (ziggurat_tables.h), with
 *                 its exact tail and wedge fallbacks
 *
 * Checked against numpy itself by check_nprand in test_nprand.c's golden
 * values, not trusted.
 */

#ifndef NPRAND_H
#define NPRAND_H

#include <stdint.h>

typedef struct {
  uint64_t state_hi, state_lo;
  uint64_t inc_hi, inc_lo;
} nprand_t;

/* default_rng(seed) for a non-negative seed below 2^64. Returns 0 if the
   embedded ziggurat tables fail their checksum. */
int nprand_seed(nprand_t *r, uint64_t seed);

uint64_t nprand_next64(nprand_t *r);
double nprand_double(nprand_t *r);             /* [0, 1), 53 bits */
double nprand_standard_normal(nprand_t *r);
double nprand_normal(nprand_t *r, double loc, double scale);

/* The 4-word SeedSequence pool, exposed so it can be checked on its own. */
void nprand_seedseq_pool(uint64_t seed, uint32_t pool[4]);

#endif /* NPRAND_H */
