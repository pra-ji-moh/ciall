/*
 * pyrand.h -- CPython's `random.Random`, reproduced exactly in C.
 *
 * The native/ experiments were written in Python and seeded. Porting them
 * to C and comparing outputs byte for byte only works if the C draws the
 * SAME random numbers, so this reproduces the generator and the specific
 * algorithms CPython layers on it:
 *
 *   seeding    Random(int): init_by_array over the integer's 32-bit words
 *   random()   53-bit float from two 32-bit draws: (a >> 5, b >> 6)
 *   randbelow  getrandbits(n.bit_length()) with rejection, as CPython does
 *   choice     seq[randbelow(len)]
 *   shuffle    Fisher-Yates from the END, randbelow(i + 1)
 *   sample     the pool method CPython uses for small populations
 *   randint    a + randbelow(b - a + 1)
 *   uniform    a + (b - a) * random()
 *
 * Checked against the real `random` module by check_pyrand.py rather than
 * trusted: a generator that is almost right produces experiments that are
 * quietly different.
 */

#ifndef PYRAND_H
#define PYRAND_H

#include <stdint.h>

typedef struct {
  uint32_t mt[624];
  int mti;
} pyrand_t;

void pyrand_seed(pyrand_t *r, uint64_t seed);
uint32_t pyrand_u32(pyrand_t *r);
double pyrand_random(pyrand_t *r);
uint32_t pyrand_getrandbits(pyrand_t *r, unsigned k);      /* 1 <= k <= 32 */
unsigned pyrand_randbelow(pyrand_t *r, unsigned n);         /* n >= 1 */
int pyrand_randint(pyrand_t *r, int a, int b);
double pyrand_uniform(pyrand_t *r, double a, double b);

/* In-place on an array of ints, exactly as random.shuffle. */
void pyrand_shuffle(pyrand_t *r, int *x, unsigned n);

/* random.sample(population, k), for populations small enough that CPython
   uses its pool method (n <= 21 when k <= 5), which covers every call in
   native/. Returns 0 if asked for a case it does not reproduce. */
int pyrand_sample(pyrand_t *r, const int *pop, unsigned n, unsigned k, int *out);

#endif /* PYRAND_H */
