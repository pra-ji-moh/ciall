/*
 * nprand.c -- see nprand.h. Follows numpy's bit_generator.pyx
 * (SeedSequence), pcg64.h and distributions.c (random_standard_normal).
 */

#include "nprand.h"

#include <math.h>

#include "ziggurat_tables.h"

/* ---- SeedSequence --------------------------------------------------- */

#define INIT_A 0x43b0d7e5u
#define MULT_A 0x931e8875u
#define INIT_B 0x8b51f9ddu
#define MULT_B 0x58f38dedu
#define MIX_MULT_L 0xca01f9ddu
#define MIX_MULT_R 0x4973f715u
#define XSHIFT 16u
#define POOL 4u

static uint32_t hashmix(uint32_t value, uint32_t *hash_const) {
  value ^= *hash_const;
  *hash_const *= MULT_A;
  value *= *hash_const;
  value ^= value >> XSHIFT;
  return value;
}

static uint32_t mix(uint32_t x, uint32_t y) {
  uint32_t result = MIX_MULT_L * x - MIX_MULT_R * y;
  result ^= result >> XSHIFT;
  return result;
}

void nprand_seedseq_pool(uint64_t seed, uint32_t pool[4]) {
  /* numpy turns the integer into little-endian 32-bit words; 0 is [0] */
  uint32_t entropy[2];
  unsigned n = 1u, i, s, d;
  uint32_t hc = INIT_A;
  entropy[0] = (uint32_t)(seed & 0xFFFFFFFFu);
  entropy[1] = (uint32_t)(seed >> 32);
  if (entropy[1] != 0u) n = 2u;
  for (i = 0u; i < POOL; i++) pool[i] = hashmix(i < n ? entropy[i] : 0u, &hc);
  for (s = 0u; s < POOL; s++) {
    for (d = 0u; d < POOL; d++) {
      if (s != d) pool[d] = mix(pool[d], hashmix(pool[s], &hc));
    }
  }
  /* entropy words beyond the pool size would be mixed in here; a 64-bit
     seed has at most two, so there are none */
}

/* generate_state(4, uint64): 8 words from the pool, paired low first */
static void seedseq_state(const uint32_t pool[4], uint64_t out[4]) {
  uint32_t w[8];
  uint32_t hc = INIT_B;
  unsigned i;
  for (i = 0u; i < 8u; i++) {
    uint32_t v = pool[i % POOL];
    v ^= hc;
    hc *= MULT_B;
    v *= hc;
    v ^= v >> XSHIFT;
    w[i] = v;
  }
  for (i = 0u; i < 4u; i++) out[i] = (uint64_t)w[2u * i] | ((uint64_t)w[2u * i + 1u] << 32);
}

/* ---- 128-bit arithmetic in halves ----------------------------------- */

static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo) {
  uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
  uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
  uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
  *lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
  *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

#define MULT_HI 2549297995355413924ULL
#define MULT_LO 4865540595714422341ULL

/* state = state * MULT + inc (mod 2^128) */
static void step(nprand_t *r) {
  uint64_t hi, lo, carry;
  mul64(r->state_lo, MULT_LO, &hi, &lo);
  hi += r->state_hi * MULT_LO + r->state_lo * MULT_HI;
  lo += r->inc_lo;
  carry = lo < r->inc_lo;
  r->state_hi = hi + r->inc_hi + carry;
  r->state_lo = lo;
}

/* ---- checksum for the copied tables --------------------------------- */

static uint32_t crc_bytes(uint32_t crc, const unsigned char *p, unsigned n) {
  unsigned i, k;
  for (i = 0u; i < n; i++) {
    crc ^= p[i];
    for (k = 0u; k < 8u; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

static uint32_t crc_u64(uint32_t crc, uint64_t v) {
  unsigned char b[8];
  unsigned i;
  for (i = 0u; i < 8u; i++) b[i] = (unsigned char)(v >> (8u * i));
  return crc_bytes(crc, b, 8u);
}

static uint64_t bits_of(double d) {
  union { double d; uint64_t u; } x;
  x.d = d;
  return x.u;
}

static int tables_ok(void) {
  uint32_t crc = 0xFFFFFFFFu;
  unsigned i;
  for (i = 0u; i < 256u; i++) crc = crc_u64(crc, ZIG_KI[i]);
  for (i = 0u; i < 256u; i++) crc = crc_u64(crc, bits_of(ZIG_WI[i]));
  for (i = 0u; i < 256u; i++) crc = crc_u64(crc, bits_of(ZIG_FI[i]));
  return (crc ^ 0xFFFFFFFFu) == (uint32_t)ZIG_TABLES_CRC32;
}

/* ---- the generator -------------------------------------------------- */

int nprand_seed(nprand_t *r, uint64_t seed) {
  uint32_t pool[4];
  uint64_t v[4];
  if (!tables_ok()) return 0;
  nprand_seedseq_pool(seed, pool);
  seedseq_state(pool, v);
  /* pcg64_set_seed: state seed = (v[0] high, v[1] low), stream = (v[2], v[3]);
     then srandom: state 0, inc = stream << 1 | 1, step, add seed, step */
  r->inc_hi = (v[2] << 1) | (v[3] >> 63);
  r->inc_lo = (v[3] << 1) | 1u;
  r->state_hi = 0u;
  r->state_lo = 0u;
  step(r);
  r->state_lo += v[1];
  r->state_hi += v[0] + (r->state_lo < v[1]);
  step(r);
  return 1;
}

uint64_t nprand_next64(nprand_t *r) {
  uint64_t x;
  unsigned rot;
  step(r);
  x = r->state_hi ^ r->state_lo;   /* XSL: fold the halves */
  rot = (unsigned)(r->state_hi >> 58);
  return (x >> rot) | (x << ((64u - rot) & 63u));
}

double nprand_double(nprand_t *r) {
  return (double)(nprand_next64(r) >> 11) * (1.0 / 9007199254740992.0);
}

#define ZIG_R 3.6541528853610087963519472518
#define ZIG_INV_R 0.27366123732975827203338247596

double nprand_standard_normal(nprand_t *r) {
  for (;;) {
    uint64_t v = nprand_next64(r), rabs;
    unsigned idx = (unsigned)(v & 0xFFu);
    int sign;
    double x;
    v >>= 8;
    sign = (int)(v & 1u);
    rabs = (v >> 1) & 0x000FFFFFFFFFFFFFULL;
    x = (double)rabs * ZIG_WI[idx];
    if (sign) x = -x;
    if (rabs < ZIG_KI[idx]) return x;   /* the common case, ~99% */
    if (idx == 0u) {
      for (;;) {
        double xx = -ZIG_INV_R * log1p(-nprand_double(r));
        double yy = -log1p(-nprand_double(r));
        if (yy + yy > xx * xx) return ((rabs >> 8) & 1u) ? -(ZIG_R + xx) : ZIG_R + xx;
      }
    } else if ((ZIG_FI[idx - 1u] - ZIG_FI[idx]) * nprand_double(r) + ZIG_FI[idx] <
               exp(-0.5 * x * x)) {
      return x;
    }
  }
}

double nprand_normal(nprand_t *r, double loc, double scale) {
  return loc + scale * nprand_standard_normal(r);
}
