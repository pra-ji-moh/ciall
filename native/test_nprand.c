/*
 * test_nprand.c -- nprand.c against numpy itself, stage by stage.
 *
 * The expected values were printed by numpy 2.4.3 and are pinned here, so
 * the check needs no Python. Each stage is checked separately, so a
 * mismatch says where: the SeedSequence pool, the PCG64 state after
 * seeding, the raw 64-bit output, the first normals, a CRC over a million
 * normals (which runs the rare tail and wedge paths thousands of times),
 * a seed above 2^32 (two entropy words), and finally the 256 initial
 * weights baseline.py draws, read from golden/numpy_init.txt.
 *
 * Run from native/.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nprand.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static uint32_t crc_step(uint32_t crc, const unsigned char *p, unsigned n) {
  unsigned i, k;
  for (i = 0u; i < n; i++) {
    crc ^= p[i];
    for (k = 0u; k < 8u; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return crc;
}

int main(void) {
  static const uint32_t POOL0[4] = {0xfe40eb07u, 0x4f363a36u, 0x4eb2009du, 0xc89a7aa7u};
  static const uint32_t POOL_BIG[4] = {0x65c2aa7eu, 0x92202145u, 0x2194a56fu, 0x0bf325ddu};
  static const uint64_t RAW0[3] = {0xa30febcfd9c2825fULL, 0x4510bdf882d9d721ULL,
                                   0x0a7d3da94ecde8b8ULL};
  static const double NORM0[4] = {0x1.017ed89db8441p-3, -0x1.0e8cfe9bd45ccp-3,
                                   0x1.47e57a468b06dp-1, 0x1.adabbec84d4f0p-4};
  static const double NORM_BIG[5] = {-0x1.91a5ac28a6757p-2, -0x1.858b1f09c6dfbp-2,
                                     0x1.1128d456005ddp+1, -0x1.4007c10f75866p-2,
                                     0x1.b96b2fcaf15b5p-3};
  uint32_t pool[4];
  nprand_t r;
  unsigned i;
  int ok;

  printf("numpy's default_rng, reproduced in C, checked against numpy's own output\n\n");

  nprand_seedseq_pool(0u, pool);
  check("SeedSequence(0) pool", memcmp(pool, POOL0, sizeof pool) == 0);
  nprand_seedseq_pool(12345678901234ULL, pool);
  check("SeedSequence(12345678901234) pool: a seed spanning two 32-bit words",
        memcmp(pool, POOL_BIG, sizeof pool) == 0);

  check("the ziggurat tables pass their checksum", nprand_seed(&r, 0u) == 1);
  check("PCG64 state after seeding",
        r.state_hi == 0x1aa1b5345996452dULL && r.state_lo == 0x09585eb7a69561e3ULL);
  check("PCG64 stream increment",
        r.inc_hi == 0x418ddadb3af71a82ULL && r.inc_lo == 0x588133bc447873a9ULL);
  ok = 1;
  for (i = 0u; i < 3u; i++) ok &= nprand_next64(&r) == RAW0[i];
  check("the first three raw 64-bit outputs", ok);

  nprand_seed(&r, 0u);
  ok = 1;
  for (i = 0u; i < 4u; i++) ok &= nprand_standard_normal(&r) == NORM0[i];
  check("the first four standard normals, bit for bit", ok);

  nprand_seed(&r, 12345678901234ULL);
  ok = 1;
  for (i = 0u; i < 5u; i++) ok &= nprand_standard_normal(&r) == NORM_BIG[i];
  check("five normals from the two-word seed", ok);

  {
    uint32_t crc = 0xFFFFFFFFu;
    nprand_seed(&r, 0u);
    for (i = 0u; i < 1000000u; i++) {
      double d = nprand_standard_normal(&r);
      unsigned char b[8];
      uint64_t u;
      unsigned k;
      memcpy(&u, &d, 8u);
      for (k = 0u; k < 8u; k++) b[k] = (unsigned char)(u >> (8u * k));
      crc = crc_step(crc, b, 8u);
    }
    {
      /*
       * numpy draws its normals with the C library's log1p and exp, and those
       * round differently in the last bit on different systems. So the check is
       * against numpy on this machine: NPRAND_NUMPY_CRC, when set, is numpy's own
       * CRC here (the cloud computes it); otherwise the value numpy gives at home.
       */
      const char *env = getenv("NPRAND_NUMPY_CRC");
      uint32_t want = env != 0 ? (uint32_t)strtoul(env, 0, 16) : 0x8A58B154u;
      check("a million normals: CRC-32 equals numpy's on this machine",
            (crc ^ 0xFFFFFFFFu) == want);
    }
  }

  {
    /* baseline.py: W1 = normal(0, sqrt(1/2), (2, 64)), then
       W2 = normal(0, sqrt(1/64), (64, 2)), from default_rng(0) */
    FILE *f = fopen("golden/numpy_init.txt", "r");
    unsigned same = 0u, total = 0u;
    if (f != NULL) {
      nprand_seed(&r, 0u);
      for (i = 0u; i < 256u; i++) {
        double want, got = nprand_normal(&r, 0.0, sqrt(i < 128u ? 1.0 / 2.0 : 1.0 / 64.0));
        if (fscanf(f, "%lf", &want) != 1) break;
        total++;
        same += got == want;
      }
      fclose(f);
    }
    printf("        (%u of %u initial weights identical)\n", same, total);
    check("baseline.py's 256 initial weights, every one bit for bit",
          total == 256u && same == 256u);
  }

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
