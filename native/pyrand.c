/*
 * pyrand.c -- see pyrand.h. MT19937 as in CPython's _randommodule.c.
 */

#include "pyrand.h"

static void init_genrand(pyrand_t *r, uint32_t s) {
  int i;
  r->mt[0] = s;
  for (i = 1; i < 624; i++) {
    r->mt[i] = 1812433253u * (r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) + (uint32_t)i;
  }
  r->mti = 624;
}

static void init_by_array(pyrand_t *r, const uint32_t *key, unsigned len) {
  unsigned i = 1u, j = 0u, k;
  init_genrand(r, 19650218u);
  k = 624u > len ? 624u : len;
  for (; k; k--) {
    r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1664525u)) + key[j] + j;
    i++;
    j++;
    if (i >= 624u) { r->mt[0] = r->mt[623]; i = 1u; }
    if (j >= len) j = 0u;
  }
  for (k = 623u; k; k--) {
    r->mt[i] = (r->mt[i] ^ ((r->mt[i - 1] ^ (r->mt[i - 1] >> 30)) * 1566083941u)) - i;
    i++;
    if (i >= 624u) { r->mt[0] = r->mt[623]; i = 1u; }
  }
  r->mt[0] = 0x80000000u;
}

void pyrand_seed(pyrand_t *r, uint64_t seed) {
  /* CPython splits abs(seed) into 32-bit words, least significant first;
     zero seeds as the single word 0. */
  uint32_t key[2];
  unsigned len = 1u;
  key[0] = (uint32_t)(seed & 0xFFFFFFFFu);
  key[1] = (uint32_t)(seed >> 32);
  if (key[1] != 0u) len = 2u;
  init_by_array(r, key, len);
}

uint32_t pyrand_u32(pyrand_t *r) {
  uint32_t y;
  static const uint32_t mag01[2] = {0u, 0x9908b0dfu};
  if (r->mti >= 624) {
    int kk;
    for (kk = 0; kk < 624 - 397; kk++) {
      y = (r->mt[kk] & 0x80000000u) | (r->mt[kk + 1] & 0x7fffffffu);
      r->mt[kk] = r->mt[kk + 397] ^ (y >> 1) ^ mag01[y & 1u];
    }
    for (; kk < 623; kk++) {
      y = (r->mt[kk] & 0x80000000u) | (r->mt[kk + 1] & 0x7fffffffu);
      r->mt[kk] = r->mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 1u];
    }
    y = (r->mt[623] & 0x80000000u) | (r->mt[0] & 0x7fffffffu);
    r->mt[623] = r->mt[396] ^ (y >> 1) ^ mag01[y & 1u];
    r->mti = 0;
  }
  y = r->mt[r->mti++];
  y ^= (y >> 11);
  y ^= (y << 7) & 0x9d2c5680u;
  y ^= (y << 15) & 0xefc60000u;
  y ^= (y >> 18);
  return y;
}

double pyrand_random(pyrand_t *r) {
  uint32_t a = pyrand_u32(r) >> 5, b = pyrand_u32(r) >> 6;
  return ((double)a * 67108864.0 + (double)b) * (1.0 / 9007199254740992.0);
}

uint32_t pyrand_getrandbits(pyrand_t *r, unsigned k) {
  return pyrand_u32(r) >> (32u - k);
}

static unsigned bit_length(unsigned n) {
  unsigned b = 0u;
  while (n) { b++; n >>= 1; }
  return b;
}

unsigned pyrand_randbelow(pyrand_t *r, unsigned n) {
  unsigned k = bit_length(n);
  uint32_t v = pyrand_getrandbits(r, k);
  while (v >= n) v = pyrand_getrandbits(r, k);
  return (unsigned)v;
}

int pyrand_randint(pyrand_t *r, int a, int b) {
  return a + (int)pyrand_randbelow(r, (unsigned)(b - a + 1));
}

double pyrand_uniform(pyrand_t *r, double a, double b) {
  return a + (b - a) * pyrand_random(r);
}

void pyrand_shuffle(pyrand_t *r, int *x, unsigned n) {
  unsigned i;
  if (n < 2u) return;
  for (i = n - 1u; i >= 1u; i--) {
    unsigned j = pyrand_randbelow(r, i + 1u);
    int t = x[i];
    x[i] = x[j];
    x[j] = t;
  }
}

int pyrand_sample(pyrand_t *r, const int *pop, unsigned n, unsigned k, int *out) {
  int pool[64];
  unsigned i;
  if (k > 5u || n > 21u || n > 64u || k > n) return 0;
  for (i = 0u; i < n; i++) pool[i] = pop[i];
  for (i = 0u; i < k; i++) {
    unsigned j = pyrand_randbelow(r, n - i);
    out[i] = pool[j];
    pool[j] = pool[n - i - 1u];
  }
  return 1;
}
