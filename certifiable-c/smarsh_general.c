/*
 * smarsh_general.c -- the core with no domain in it. See smarsh_general.h.
 */
#include "smarsh_general.h"

#include <math.h>
#include <string.h>

/* ---- SHA-256, for the chain ------------------------------------------------------- */

static const uint32_t K256[64] = {
  0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
  0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
  0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
  0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
  0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
  0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
  0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
  0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32u - (n))))

static void sha256(const unsigned char *msg, size_t len, unsigned char out[32]) {
  uint32_t h[8] = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                   0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
  unsigned char block[128];
  size_t full = len / 64u, rest = len % 64u, blocks, b, i;
  uint64_t bits = (uint64_t)len * 8u;
  memset(block, 0, sizeof block);
  memcpy(block, msg + full * 64u, rest);
  block[rest] = 0x80u;
  blocks = (rest < 56u) ? 1u : 2u;
  for (i = 0u; i < 8u; i++) block[blocks * 64u - 1u - i] = (unsigned char)(bits >> (8u * i));
  for (b = 0u; b < full + blocks; b++) {
    const unsigned char *p = (b < full) ? msg + b * 64u : block + (b - full) * 64u;
    uint32_t w[64], s[8], t1, t2;
    for (i = 0u; i < 16u; i++) {
      w[i] = ((uint32_t)p[i * 4u] << 24) | ((uint32_t)p[i * 4u + 1u] << 16) | ((uint32_t)p[i * 4u + 2u] << 8) |
             (uint32_t)p[i * 4u + 3u];
    }
    for (i = 16u; i < 64u; i++) {
      uint32_t s0 = ROR(w[i - 15u], 7u) ^ ROR(w[i - 15u], 18u) ^ (w[i - 15u] >> 3);
      uint32_t s1 = ROR(w[i - 2u], 17u) ^ ROR(w[i - 2u], 19u) ^ (w[i - 2u] >> 10);
      w[i] = w[i - 16u] + s0 + w[i - 7u] + s1;
    }
    memcpy(s, h, sizeof s);
    for (i = 0u; i < 64u; i++) {
      uint32_t e1 = ROR(s[4], 6u) ^ ROR(s[4], 11u) ^ ROR(s[4], 25u);
      uint32_t ch = (s[4] & s[5]) ^ (~s[4] & s[6]);
      uint32_t e0 = ROR(s[0], 2u) ^ ROR(s[0], 13u) ^ ROR(s[0], 22u);
      uint32_t mj = (s[0] & s[1]) ^ (s[0] & s[2]) ^ (s[1] & s[2]);
      t1 = s[7] + e1 + ch + K256[i] + w[i];
      t2 = e0 + mj;
      s[7] = s[6];
      s[6] = s[5];
      s[5] = s[4];
      s[4] = s[3] + t1;
      s[3] = s[2];
      s[2] = s[1];
      s[1] = s[0];
      s[0] = t1 + t2;
    }
    for (i = 0u; i < 8u; i++) h[i] += s[i];
  }
  for (i = 0u; i < 8u; i++) {
    out[i * 4u] = (unsigned char)(h[i] >> 24);
    out[i * 4u + 1u] = (unsigned char)(h[i] >> 16);
    out[i * 4u + 2u] = (unsigned char)(h[i] >> 8);
    out[i * 4u + 3u] = (unsigned char)h[i];
  }
}

/* one more link: the head so far, the test, what it showed, and how many were left */
static void chain(sg_core_t *c, unsigned test, int outcome, unsigned left) {
  unsigned char m[44];
  uint32_t v[3];
  unsigned i, j;
  v[0] = (uint32_t)test;
  v[1] = (uint32_t)outcome;
  v[2] = (uint32_t)left;
  memcpy(m, c->head, 32u);
  for (i = 0u; i < 3u; i++) {
    for (j = 0u; j < 4u; j++) m[32u + i * 4u + j] = (unsigned char)(v[i] >> (8u * j));
  }
  sha256(m, sizeof m, c->head);
}

/* ---- survivors, in the base's own domains ------------------------------------------ */

sm_status_t sg_begin(sg_core_t *c, const sg_adapter_t *a) {
  unsigned p, left;
  if (c == 0 || a == 0 || a->predict == 0) return SM_ERR_NULL_ARGUMENT;
  if (a->n_hyp == 0u) return SM_ERR_EMPTY_DOMAIN;
  if (a->n_hyp > SG_MAX_HYP) return SM_ERR_DOMAIN_TOO_LARGE;
  memset(c, 0, sizeof *c);
  c->a = a;
  c->n_parts = (a->n_hyp + SM_MAX_DOMAIN - 1u) / SM_MAX_DOMAIN;
  left = a->n_hyp;
  for (p = 0u; p < c->n_parts; p++) {
    unsigned size = left > SM_MAX_DOMAIN ? SM_MAX_DOMAIN : left;
    sm_status_t st = sm_init(&c->part[p], size);
    if (st != SM_OK) return st;
    left -= size;
  }
  return SM_OK;
}

int sg_possible(const sg_core_t *c, unsigned h) {
  if (h >= c->a->n_hyp) return 0;
  return sm_is_possible(&c->part[h / SM_MAX_DOMAIN], h % SM_MAX_DOMAIN);
}

unsigned sg_count(const sg_core_t *c) {
  unsigned p, n = 0u;
  for (p = 0u; p < c->n_parts; p++) n += sm_count(&c->part[p]);
  return n;
}

double sg_bits(const sg_core_t *c) {
  unsigned n = sg_count(c);
  return n > 1u ? log2((double)n) : 0.0;
}

double sg_ledger(const sg_core_t *c) {
  const sg_adapter_t *a = c->a;
  static unsigned char seen[SG_MAX_ROWS][SM_MAX_DOMAIN];
  unsigned h, r;
  double bits = 0.0;
  if (a->n_rows == 0u || a->n_rows > SG_MAX_ROWS) return sg_bits(c);
  for (r = 0u; r < a->n_rows; r++) {
    if (a->row_size[r] == 0u || a->row_size[r] > SM_MAX_DOMAIN) return sg_bits(c);
  }
  memset(seen, 0, sizeof seen);
  for (h = 0u; h < a->n_hyp; h++) {
    unsigned rest = h;
    if (!sg_possible(c, h)) continue;
    for (r = 0u; r < a->n_rows; r++) {
      seen[r][rest % a->row_size[r]] = 1u;
      rest /= a->row_size[r];
    }
  }
  for (r = 0u; r < a->n_rows; r++) {
    unsigned v, n = 0u;
    for (v = 0u; v < a->row_size[r]; v++) n += seen[r][v];
    if (n > 1u) bits += log2((double)n);
  }
  return bits;
}

unsigned sg_observe(sg_core_t *c, unsigned test, int outcome) {
  const sg_adapter_t *a = c->a;
  unsigned h, cut = 0u;
  for (h = 0u; h < a->n_hyp; h++) {
    if (!sg_possible(c, h)) continue;
    if (a->predict(a->ctx, h, test) != outcome) {
      (void)sm_eliminate(&c->part[h / SM_MAX_DOMAIN], h % SM_MAX_DOMAIN);   /* it said otherwise: ruled out */
      cut++;
    }
  }
  if (c->n_obs < SG_MAX_OBS) {
    c->obs_test[c->n_obs] = test;
    c->obs_out[c->n_obs] = outcome;
    c->n_obs++;
  }
  c->spent += a->test_cost ? a->test_cost(a->ctx, test) : 1u;
  chain(c, test, outcome, sg_count(c));
  return cut;
}

/* ---- the test that asks the most --------------------------------------------------- */

#define SG_OUTCOMES 64u

int sg_probe(const sg_core_t *c, unsigned *test, unsigned *worst_left) {
  const sg_adapter_t *a = c->a;
  unsigned t, n = sg_count(c), best_t = 0u, best_worst = n, best_cost = 0u;
  int found = 0;
  if (n < 2u) return 0;
  for (t = 0u; t < a->n_tests; t++) {
    int key[SG_OUTCOMES];
    unsigned cnt[SG_OUTCOMES], nk = 0u, h, k, worst = 0u, cost;
    int too_many = 0;
    for (h = 0u; h < a->n_hyp; h++) {
      int out;
      if (!sg_possible(c, h)) continue;
      out = a->predict(a->ctx, h, t);
      for (k = 0u; k < nk && key[k] != out; k++) {
      }
      if (k == nk) {
        if (nk == SG_OUTCOMES) {
          too_many = 1;   /* more answers than it counts: it splits at least that finely */
          continue;
        }
        key[nk] = out;
        cnt[nk] = 0u;
        nk++;
      }
      cnt[k]++;
    }
    for (k = 0u; k < nk; k++) {
      if (cnt[k] > worst) worst = cnt[k];
    }
    (void)too_many;
    if (worst >= n) continue;   /* every survivor says the same: this test can remove nothing */
    cost = a->test_cost ? a->test_cost(a->ctx, t) : 1u;
    if (!found || worst < best_worst || (worst == best_worst && cost < best_cost)) {
      found = 1;
      best_t = t;
      best_worst = worst;
      best_cost = cost;
    }
  }
  if (!found) return 0;
  *test = best_t;
  *worst_left = best_worst;
  return 1;
}

/* ---- when to stop ------------------------------------------------------------------- */

int sg_certificate(const sg_core_t *c, unsigned *decision, unsigned *gap) {
  const sg_adapter_t *a = c->a;
  unsigned d, h, best_d = 0u, best_gap = 0u;
  int have = 0;
  if (a->decision_cost == 0 || a->n_decisions == 0u || sg_count(c) == 0u) return 0;
  for (d = 0u; d < a->n_decisions; d++) {
    unsigned regret = 0u;
    for (h = 0u; h < a->n_hyp; h++) {
      unsigned mine, least, d2;
      if (!sg_possible(c, h)) continue;
      mine = a->decision_cost(a->ctx, h, d);
      least = mine;
      for (d2 = 0u; d2 < a->n_decisions; d2++) {
        unsigned other = a->decision_cost(a->ctx, h, d2);
        if (other < least) least = other;
      }
      if (mine - least > regret) regret = mine - least;
    }
    if (!have || regret < best_gap) {
      have = 1;
      best_gap = regret;
      best_d = d;
    }
  }
  *decision = best_d;
  *gap = best_gap;
  return best_gap == 0u;
}

/* ---- when nothing survives ---------------------------------------------------------- */

/* is there any hypothesis that agrees with every observation kept? */
static int coexist(const sg_core_t *c, const unsigned char *keep) {
  const sg_adapter_t *a = c->a;
  unsigned h, i;
  for (h = 0u; h < a->n_hyp; h++) {
    int ok = 1;
    for (i = 0u; i < c->n_obs && ok; i++) {
      if (keep[i] && a->predict(a->ctx, h, c->obs_test[i]) != c->obs_out[i]) ok = 0;
    }
    if (ok) return 1;
  }
  return 0;
}

unsigned sg_conflict(const sg_core_t *c, unsigned *which, unsigned cap) {
  static unsigned char keep[SG_MAX_OBS];
  unsigned i, n = 0u;
  for (i = 0u; i < c->n_obs; i++) keep[i] = 1u;
  if (coexist(c, keep)) return 0u;
  /* leave each one out in turn: if the rest still cannot coexist, it was not needed */
  for (i = 0u; i < c->n_obs; i++) {
    keep[i] = 0u;
    if (coexist(c, keep)) keep[i] = 1u;
  }
  for (i = 0u; i < c->n_obs; i++) {
    if (keep[i] && n < cap) which[n++] = i;
  }
  return n;
}

/* ---- when the system has changed ---------------------------------------------------- */

static unsigned apart(const sg_adapter_t *a, unsigned h1, unsigned h2) {
  if (a->distance) return a->distance(a->ctx, h1, h2);
  return h1 > h2 ? h1 - h2 : h2 - h1;
}

unsigned sg_carry(sg_core_t *c, unsigned old, unsigned since, unsigned *nearest, unsigned *dist,
                  unsigned *ball) {
  const sg_adapter_t *a = c->a;
  unsigned p, left = a->n_hyp, h, i, n = 0u, best = 0u, best_d = 0u, in_ball = 0u;
  int have = 0;
  /* the survivors of the evidence since the change: exact, as always */
  for (p = 0u; p < c->n_parts; p++) {
    unsigned size = left > SM_MAX_DOMAIN ? SM_MAX_DOMAIN : left;
    (void)sm_init(&c->part[p], size);
    left -= size;
  }
  for (h = 0u; h < a->n_hyp; h++) {
    int ok = 1;
    for (i = since; i < c->n_obs && ok; i++) {
      if (a->predict(a->ctx, h, c->obs_test[i]) != c->obs_out[i]) ok = 0;
    }
    if (!ok) {
      (void)sm_eliminate(&c->part[h / SM_MAX_DOMAIN], h % SM_MAX_DOMAIN);
      continue;
    }
    n++;
    if (!have || apart(a, h, old) < best_d) {
      have = 1;
      best = h;
      best_d = apart(a, h, old);
    }
  }
  /* nearness orders where to look; it removes nothing. How big the ball it had to open is: */
  for (h = 0u; h < a->n_hyp && have; h++) {
    if (apart(a, h, old) <= best_d) in_ball++;
  }
  if (have) {
    *nearest = best;
    *dist = best_d;
    *ball = in_ball;
  }
  return n;
}

/* ---- the trace ---------------------------------------------------------------------- */

void sg_head(const sg_core_t *c, char out[65]) {
  static const char HEX[] = "0123456789abcdef";
  unsigned i;
  for (i = 0u; i < 32u; i++) {
    out[i * 2u] = HEX[c->head[i] >> 4];
    out[i * 2u + 1u] = HEX[c->head[i] & 15u];
  }
  out[64] = '\0';
}

int sg_replay(const sg_adapter_t *a, const unsigned *tests, const int *outs, unsigned n,
              const unsigned char head[32]) {
  static sg_core_t again;
  unsigned i;
  if (sg_begin(&again, a) != SM_OK) return 0;
  for (i = 0u; i < n; i++) (void)sg_observe(&again, tests[i], outs[i]);
  return memcmp(again.head, head, 32u) == 0;
}
