/*
 * smarsh_core.c -- implementation. See smarsh_core.h for its status
 * (compiled and run; differential not yet done) and for why a possibility
 * set rather than a probability.
 */

#include "smarsh_core.h"

#include <math.h>

/* ------------------------------------------------------------------ */
/* bit helpers. Constant-time by construction: no loop here depends on
   the data, only on SM_DOMAIN_WORDS, so worst case equals typical case. */
/* ------------------------------------------------------------------ */

static unsigned popcount64(uint64_t x) {
  /* SWAR, branchless and loop-free -- chosen over the clear-lowest-bit
     loop because that loop's iteration count varies with the data, and a
     data-dependent loop is exactly what makes WCET analysis hard. */
  x = x - ((x >> 1) & 0x5555555555555555ULL);
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return (unsigned)((x * 0x0101010101010101ULL) >> 56);
}

static uint64_t splitmix64(uint64_t *state) {
  uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

/* ------------------------------------------------------------------ */
/* ancestry: a set of guesses, not a running total                      */
/* ------------------------------------------------------------------ */

void sm_ancestry_clear(sm_ancestry_t *a) {
  unsigned i;

  if (a == 0) {
    return;
  }
  a->ids = 0u;
  for (i = 0u; i < SM_MAX_GUESSES; i++) {
    a->remaining[i] = 0u;
  }
}

sm_status_t sm_ancestry_add(sm_ancestry_t *a, unsigned guess_id,
                            unsigned remaining) {
  if (a == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (guess_id >= SM_MAX_GUESSES) {
    return SM_ERR_GUESS_ID_OUT_OF_RANGE;
  }
  a->ids |= ((uint64_t)1u << guess_id);
  a->remaining[guess_id] = (uint16_t)remaining;
  return SM_OK;
}

void sm_ancestry_union(const sm_ancestry_t *a, const sm_ancestry_t *b,
                       sm_ancestry_t *out) {
  unsigned i;

  if (a == 0 || b == 0 || out == 0) {
    return;
  }
  out->ids = a->ids | b->ids;
  for (i = 0u; i < SM_MAX_GUESSES; i++) {
    /* Same id means the same guess, so the two sides agree on its size;
       taking either is correct. `a` wins when both are set. */
    if ((a->ids & ((uint64_t)1u << i)) != 0u) {
      out->remaining[i] = a->remaining[i];
    } else if ((b->ids & ((uint64_t)1u << i)) != 0u) {
      out->remaining[i] = b->remaining[i];
    } else {
      out->remaining[i] = 0u;
    }
  }
}

double sm_ancestry_bits(const sm_ancestry_t *a) {
  unsigned i;
  double total = 0.0;

  if (a == 0) {
    return 0.0;
  }
  for (i = 0u; i < SM_MAX_GUESSES; i++) {
    if ((a->ids & ((uint64_t)1u << i)) != 0u && a->remaining[i] > 1u) {
      total += log2((double)a->remaining[i]);
    }
  }
  return total;
}

/* ------------------------------------------------------------------ */
/* constructing and constraining                                        */
/* ------------------------------------------------------------------ */

sm_status_t sm_init(sm_possibility_t *p, unsigned size) {
  unsigned w;
  unsigned i;

  if (p == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (size == 0u) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (size > SM_MAX_DOMAIN) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }

  for (w = 0u; w < SM_DOMAIN_WORDS; w++) {
    p->bits[w] = 0u;
  }
  /* Everything in 0..size-1 starts possible: nothing has been reasoned
     about yet, so nothing has been eliminated. */
  for (i = 0u; i < size; i++) {
    p->bits[i >> 6] |= ((uint64_t)1u << (i & 63u));
  }
  p->size = size;
  p->has_domain = 1;
  return SM_OK;
}

void sm_init_groundless(sm_possibility_t *p) {
  unsigned w;

  if (p == 0) {
    return;
  }
  for (w = 0u; w < SM_DOMAIN_WORDS; w++) {
    p->bits[w] = 0u;
  }
  p->size = 0u;
  /* The distinction that matters: no domain, as opposed to a domain with
     nothing left in it. The second is a contradiction. */
  p->has_domain = 0;
}

sm_status_t sm_eliminate(sm_possibility_t *p, unsigned index) {
  if (p == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (p->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (index >= p->size) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  /* Idempotent on purpose: applying the same constraint twice must not
     mean anything different from applying it once. */
  p->bits[index >> 6] &= ~((uint64_t)1u << (index & 63u));
  return SM_OK;
}

sm_status_t sm_restrict_to(sm_possibility_t *p, unsigned index) {
  unsigned w;

  if (p == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (p->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (index >= p->size) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  for (w = 0u; w < SM_DOMAIN_WORDS; w++) {
    p->bits[w] = 0u;
  }
  p->bits[index >> 6] |= ((uint64_t)1u << (index & 63u));
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* reading                                                              */
/* ------------------------------------------------------------------ */

int sm_is_possible(const sm_possibility_t *p, unsigned index) {
  if (p == 0 || p->has_domain == 0 || index >= p->size) {
    return 0;
  }
  return (p->bits[index >> 6] & ((uint64_t)1u << (index & 63u))) != 0u;
}

unsigned sm_count(const sm_possibility_t *p) {
  unsigned w;
  unsigned n = 0u;

  if (p == 0 || p->has_domain == 0) {
    return 0u;
  }
  for (w = 0u; w < SM_DOMAIN_WORDS; w++) {
    n += popcount64(p->bits[w]);
  }
  return n;
}

sm_verdict_t sm_verdict(const sm_possibility_t *p) {
  unsigned n;

  if (p == 0 || p->has_domain == 0) {
    return SM_GROUNDLESS;
  }
  n = sm_count(p);
  if (n == 0u) {
    return SM_CONTRADICTION;
  }
  if (n == 1u) {
    return SM_DERIVED;
  }
  return SM_UNDETERMINED;
}

double sm_support(const sm_possibility_t *p) {
  if (p == 0 || p->has_domain == 0) {
    /* Out of band rather than 0.0: there is no domain to take a fraction
       of, and 0.0 would read as a real measurement of "nothing was
       eliminated", which is a different and checkable claim. */
    return -1.0;
  }
  if (p->size == 0u) {
    return -1.0;
  }
  /* (size - count) / size, not 1 - count / size. The numerator is an
     exact integer, so this is ONE correctly rounded division: the double
     nearest the true support. 1 - count/size rounds twice and can land one
     bit away, which diff_core.c caught against Smarsh's JS gate (it
     computes used/available the exact way): 659 of 3,000 cases disagreed,
     and where the bar sat exactly on S the decision itself flipped. */
  return (double)(p->size - sm_count(p)) / (double)p->size;
}

double sm_hartley(const sm_possibility_t *p) {
  unsigned n;

  if (p == 0 || p->has_domain == 0) {
    return 0.0;
  }
  n = sm_count(p);
  if (n <= 1u) {
    /* Forced, not guessed: emitting the only remaining value claims
       nothing the constraints did not already establish. */
    return 0.0;
  }
  return log2((double)n);
}

/* ------------------------------------------------------------------ */
/* the gate                                                             */
/* ------------------------------------------------------------------ */

static unsigned nth_possible(const sm_possibility_t *p, unsigned k) {
  unsigned i;
  unsigned seen = 0u;

  /* Bounded by SM_MAX_DOMAIN, a compile-time constant, not by anything
     the caller supplies. */
  for (i = 0u; i < SM_MAX_DOMAIN; i++) {
    if (i >= p->size) {
      break;
    }
    if ((p->bits[i >> 6] & ((uint64_t)1u << (i & 63u))) != 0u) {
      if (seen == k) {
        return i;
      }
      seen++;
    }
  }
  return 0u; /* unreachable when k < sm_count(p); callers guarantee that */
}

/* g(S). A policy, not a derivation -- see the note in smarsh_core.h. */
static double intensity_of(double s, double tau, sm_intensity_policy_t policy) {
  if (policy == SM_INTENSITY_HEADROOM) {
    if (tau >= 1.0) {
      /* The bar is at the ceiling; there is no headroom to measure and
         dividing would be by zero. Nothing can clear it anyway except
         S == 1, which is a contradiction rather than a guess. */
      return 0.0;
    }
    return (s - tau) / (1.0 - tau);
  }
  return s; /* SM_INTENSITY_SUPPORT: what speculate.js ships */
}

sm_status_t sm_decide(const sm_possibility_t *p, double tau, uint64_t seed,
                      sm_intensity_policy_t policy, unsigned guess_id,
                      sm_result_t *out) {
  sm_verdict_t v;
  unsigned n;
  double s;
  uint64_t state;
  unsigned k;

  if (p == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (!(tau >= 0.0) || !(tau <= 1.0)) {
    /* Written as two >= / <= checks rather than a range test so that a
       NaN tau is rejected rather than silently passing a comparison. */
    return SM_ERR_BAD_THRESHOLD;
  }

  if (guess_id >= SM_MAX_GUESSES) {
    return SM_ERR_GUESS_ID_OUT_OF_RANGE;
  }
  sm_ancestry_clear(&out->ancestry);
  /* Set once here so no branch below can leave it stale. Refusing carries
     an intensity of zero rather than an undefined value. */
  out->intensity = 0.0;
  v = sm_verdict(p);

  if (v == SM_GROUNDLESS) {
    out->verdict = SM_GROUNDLESS;
    out->value = 0u;
    out->S = -1.0;
    out->H = 0.0;
    return SM_OK;
  }

  s = sm_support(p);
  out->S = s;
  out->H = sm_hartley(p);

  if (v == SM_CONTRADICTION) {
    out->verdict = SM_CONTRADICTION;
    out->value = 0u;
    return SM_OK;
  }

  if (v == SM_DERIVED) {
    out->verdict = SM_DERIVED;
    out->value = nth_possible(p, 0u);
    /* H is already 0.0 here: the emission was forced. */
    return SM_OK;
  }

  /* Undetermined. Whether to collapse anyway is the whole decision. */
  if (s < tau) {
    /* Not grounded enough to venture a guess. Report the honest state
       rather than inventing a value: no unlicensed elimination happens. */
    out->verdict = SM_UNDETERMINED;
    out->value = 0u;
    return SM_OK;
  }

  /* Cleared the bar. Collapsing to a point now eliminates |D|-1 values
     that no constraint eliminated -- unlicensed, and H records exactly
     how much was claimed without being held.

     Uniform over what remains: this does not try to be right more often
     than chance. The modulo introduces bias bounded by count/2^64, which
     is below 1e-17 for any domain this module accepts; rejection
     sampling would remove it entirely at the cost of a data-dependent
     loop, which is a worse trade for WCET than a bias that small. */
  n = sm_count(p);
  state = seed;
  k = (unsigned)(splitmix64(&state) % (uint64_t)n);

  out->verdict = SM_SPECULATED;
  out->value = nth_possible(p, k);
  out->intensity = intensity_of(s, tau, policy);
  /* n possibilities remained when this guess was made; that is what makes
     it worth log2(n) bits of unlicensed elimination. */
  return sm_ancestry_add(&out->ancestry, guess_id, n);
}

/* ------------------------------------------------------------------ */
/* composition: removed. See the note in smarsh_core.h and sr_ask2() in  */
/* smarsh_reason.h. Composing two results cannot be correct; composing   */
/* two questions can.                                                    */
/* ------------------------------------------------------------------ */
