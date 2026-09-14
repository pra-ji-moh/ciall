/*
 * smarsh_reason.c -- implementation of the reasoning kernel.
 *
 * See smarsh_reason.h for the axiom, the correlation theorem, and the
 * status (compiled, run, checked by test_smarsh_reason.c).
 */

#include "smarsh_reason.h"

#include <math.h>

/* ------------------------------------------------------------------ */
/* bit helpers                                                          */
/* ------------------------------------------------------------------ */

static unsigned rpopcount64(uint64_t x) {
  /* SWAR, branchless and loop-free. Same reasoning as smarsh_core.c: a
     clear-lowest-bit loop varies its iteration count with the data, and a
     data-dependent loop is what makes WCET analysis hard. */
  x = x - ((x >> 1) & 0x5555555555555555ULL);
  x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
  x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
  return (unsigned)((x * 0x0101010101010101ULL) >> 56);
}

static uint64_t rsplitmix64(uint64_t *state) {
  uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

/* The live-word mask for word `w` given a universe of `n` worlds. Written
   with branches rather than a shift by (64 - k) alone because a shift of
   64 is undefined behaviour, and relying on it happening to give 0 is the
   kind of thing this file exists not to do. */
static uint64_t word_mask(unsigned w, unsigned n) {
  unsigned base = w * 64u;

  if (n >= base + 64u) {
    return ~(uint64_t)0;
  }
  if (n <= base) {
    return (uint64_t)0;
  }
  return (~(uint64_t)0) >> (64u - (n - base));
}

/* ------------------------------------------------------------------ */
/* states                                                               */
/* ------------------------------------------------------------------ */

sm_status_t sr_state_init(sr_state_t *s, unsigned n_worlds) {
  unsigned i;

  if (s == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_worlds == 0u) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (n_worlds > SR_MAX_WORLDS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  /* Fixed trip count: the loop runs SR_WORLD_WORDS times regardless of
     n_worlds, and the tail is masked rather than skipped. */
  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    s->live[i] = word_mask(i, n_worlds);
  }
  s->n_worlds = n_worlds;
  s->has_domain = 1;
  return SM_OK;
}

void sr_state_groundless(sr_state_t *s) {
  unsigned i;

  if (s == 0) {
    return;
  }
  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    s->live[i] = (uint64_t)0;
  }
  s->n_worlds = 0u;
  /* Not "everything eliminated". Never a question in the first place. */
  s->has_domain = 0;
}

sm_status_t sr_eliminate(sr_state_t *s, unsigned world) {
  if (s == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (s->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (world >= s->n_worlds) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  /* Idempotent by construction: clearing a clear bit is a no-op, so
     applying the same constraint twice says the same thing as once. */
  s->live[world >> 6] &= ~((uint64_t)1 << (world & 63u));
  return SM_OK;
}

int sr_world_possible(const sr_state_t *s, unsigned world) {
  if (s == 0 || s->has_domain == 0 || world >= s->n_worlds) {
    return 0;
  }
  return (s->live[world >> 6] >> (world & 63u)) & (uint64_t)1 ? 1 : 0;
}

unsigned sr_live_count(const sr_state_t *s) {
  unsigned i;
  unsigned total = 0u;

  if (s == 0 || s->has_domain == 0) {
    return 0u;
  }
  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    total += rpopcount64(s->live[i] & word_mask(i, s->n_worlds));
  }
  return total;
}

/* ------------------------------------------------------------------ */
/* queries                                                              */
/* ------------------------------------------------------------------ */

sm_status_t sr_query_init(sr_query_t *q, unsigned n_worlds, unsigned dom) {
  unsigned i;

  if (q == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_worlds == 0u || dom == 0u) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (n_worlds > SR_MAX_WORLDS || dom > SR_MAX_ANSWERS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    q->ans[i] = (uint8_t)0;
  }
  q->n_worlds = n_worlds;
  q->dom = dom;
  q->has_domain = 1;
  return SM_OK;
}

sm_status_t sr_query_groundless(sr_query_t *q, unsigned n_worlds,
                                unsigned dom) {
  sm_status_t st = sr_query_init(q, n_worlds, dom);

  if (st != SM_OK) {
    return st;
  }
  /* dom survives on purpose: absorption has to know what the answer
     space would have been in order to check the operator is constant
     over it. ans[] is left zeroed and means nothing. */
  q->has_domain = 0;
  return SM_OK;
}

sm_status_t sr_query_set(sr_query_t *q, unsigned world, unsigned answer) {
  if (q == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (q->has_domain == 0) {
    /* Setting an answer on a question that does not exist would quietly
       turn it back into one. */
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (world >= q->n_worlds || answer >= q->dom) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  q->ans[world] = (uint8_t)answer;
  return SM_OK;
}

sm_status_t sr_map1(const sr_op1_t *op, const sr_query_t *a, sr_query_t *out) {
  unsigned i;
  sm_status_t st;

  if (op == 0 || a == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (a->has_domain == 0) {
    /* Pointwise composition needs an answer at every world and a
       groundless query has none. Absorption is the route for these --
       sr_ask2 -- and it is a different computation, not this one with a
       fallback. */
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (op->dom_a != a->dom) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (op->dom_a > SR_MAX_ANSWERS || op->dom_out > SR_MAX_ANSWERS ||
      op->dom_out == 0u) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  /* A table entry outside the stated output domain would let a query
     report an answer its own dom says is impossible, and every count
     downstream would then be measuring the wrong space. Checked here
     rather than trusted. */
  for (i = 0u; i < SR_MAX_ANSWERS; i++) {
    if (i < op->dom_a && (unsigned)op->out[i] >= op->dom_out) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  st = sr_query_init(out, a->n_worlds, op->dom_out);
  if (st != SM_OK) {
    return st;
  }
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    unsigned src = (unsigned)a->ans[i];
    out->ans[i] = (src < op->dom_a) ? op->out[src] : (uint8_t)0;
  }
  return SM_OK;
}

sm_status_t sr_map2(const sr_op2_t *op, const sr_query_t *a,
                    const sr_query_t *b, sr_query_t *out) {
  unsigned i;
  sm_status_t st;

  if (op == 0 || a == 0 || b == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  /* Two queries over different universes are not comparable world by
     world, and composing them would silently line up world 3 of one
     question with world 3 of a different question. Refused. */
  if (a->has_domain == 0 || b->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;   /* see sr_map1 */
  }
  if (a->n_worlds != b->n_worlds) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (op->dom_a != a->dom || op->dom_b != b->dom) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (op->dom_out == 0u || op->dom_out > SR_MAX_ANSWERS ||
      op->dom_a > SR_MAX_ANSWERS || op->dom_b > SR_MAX_ANSWERS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  if (op->dom_a * op->dom_b > SR_MAX_TABLE) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  for (i = 0u; i < SR_MAX_TABLE; i++) {
    if (i < op->dom_a * op->dom_b && (unsigned)op->out[i] >= op->dom_out) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  st = sr_query_init(out, a->n_worlds, op->dom_out);
  if (st != SM_OK) {
    return st;
  }
  /* THE composition. Pointwise, over worlds. Not over answer sets, not
     over supports -- see the correlation theorem in the header. Two
     operands that share a variable share every world here, so
     "a and not a" lands on false at every world with no special case. */
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    unsigned va = (unsigned)a->ans[i];
    unsigned vb = (unsigned)b->ans[i];
    if (va < op->dom_a && vb < op->dom_b) {
      out->ans[i] = op->out[va * op->dom_b + vb];
    } else {
      out->ans[i] = (uint8_t)0;
    }
  }
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* asking                                                               */
/* ------------------------------------------------------------------ */

sm_status_t sr_image(const sr_query_t *q, const sr_state_t *s, uint64_t *out) {
  unsigned w;
  uint64_t img = (uint64_t)0;

  if (q == 0 || s == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  /* Groundless is not "an image of nothing" -- an image of nothing is a
     contradiction. The caller must handle the distinction before asking
     for a count, so this refuses rather than returning 0. */
  if (s->has_domain == 0 || q->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (q->n_worlds != s->n_worlds) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  for (w = 0u; w < SR_MAX_WORLDS; w++) {
    if (w < s->n_worlds &&
        ((s->live[w >> 6] >> (w & 63u)) & (uint64_t)1) != (uint64_t)0) {
      unsigned a = (unsigned)q->ans[w];
      if (a >= q->dom || a >= SR_MAX_ANSWERS) {
        return SM_ERR_INDEX_OUT_OF_DOMAIN;
      }
      img |= (uint64_t)1 << a;
    }
  }
  *out = img;
  return SM_OK;
}

/* The four verdicts, COUNTED. Nothing here decides anything; it reads a
   population count and reports which of the four cases that lands in. */
static sm_verdict_t verdict_of(uint64_t image, int has_domain) {
  unsigned n;

  if (has_domain == 0) {
    return SM_GROUNDLESS;
  }
  n = rpopcount64(image);
  if (n == 0u) {
    return SM_CONTRADICTION;
  }
  if (n == 1u) {
    return SM_DERIVED;
  }
  return SM_UNDETERMINED;
}

static double support_of(uint64_t image, unsigned dom, int has_domain) {
  if (has_domain == 0) {
    /* Out of band on purpose. 0.0 would read as a real measurement of
       "nothing eliminated", and there was nothing to eliminate from. */
    return -1.0;
  }
  /* (dom - |D|) / dom: one correctly rounded division of exact integers.
     1 - |D|/dom rounds twice and can be a bit off; see sm_support. */
  return (double)(dom - rpopcount64(image)) / (double)dom;
}

static double hartley_of(uint64_t image) {
  unsigned n = rpopcount64(image);

  if (n <= 1u) {
    return 0.0;
  }
  return log2((double)n);
}

static unsigned lowest_answer(uint64_t image) {
  unsigned i;

  for (i = 0u; i < SR_MAX_ANSWERS; i++) {
    if (((image >> i) & (uint64_t)1) != (uint64_t)0) {
      return i;
    }
  }
  return 0u;
}

static void copy_live(const sr_state_t *s, uint64_t *dst) {
  unsigned i;

  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    dst[i] = s->live[i];
  }
}

sm_status_t sr_ask(const sr_query_t *q, const sr_state_t *s, sr_result_t *out) {
  uint64_t img = (uint64_t)0;
  sm_status_t st;

  if (q == 0 || s == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  sm_ancestry_clear(&out->ancestry);
  out->intensity = 0.0;
  out->allowed = 0;
  out->value = 0u;

  /* No situation, or no question. Either way there is nothing to count,
     and the two are kept apart by which flag was clear, not merged into
     one condition. */
  if (s->has_domain == 0 || q->has_domain == 0) {
    unsigned i;
    for (i = 0u; i < SR_WORLD_WORDS; i++) {
      out->witness.live[i] = (uint64_t)0;
    }
    out->witness.kind = SR_W_WORLDS;
    out->witness.range_a = (uint64_t)0;
    out->witness.range_b = (uint64_t)0;
    out->witness.n_worlds = 0u;
    out->witness.image = (uint64_t)0;
    out->witness.dom = q->dom;
    out->witness.has_domain = 0;
    out->witness.verdict = SM_GROUNDLESS;
    out->witness.value = 0u;
    out->witness.S = -1.0;
    out->witness.H = 0.0;
    out->verdict = SM_GROUNDLESS;
    out->S = -1.0;
    out->H = 0.0;
    return SM_OK;
  }

  st = sr_image(q, s, &img);
  if (st != SM_OK) {
    return st;
  }

  out->verdict = verdict_of(img, 1);
  out->S = support_of(img, q->dom, 1);
  out->H = hartley_of(img);
  out->value = (out->verdict == SM_DERIVED) ? lowest_answer(img) : 0u;

  copy_live(s, out->witness.live);
  out->witness.kind = SR_W_WORLDS;
  out->witness.range_a = (uint64_t)0;
  out->witness.range_b = (uint64_t)0;
  out->witness.n_worlds = s->n_worlds;
  out->witness.image = img;
  out->witness.dom = q->dom;
  out->witness.has_domain = 1;
  out->witness.verdict = out->verdict;
  out->witness.value = out->value;
  out->witness.S = out->S;
  out->witness.H = out->H;
  return SM_OK;
}

/*
 * The checker. Written as a separate dumb loop on purpose: it recomputes
 * the image from the recorded worlds and the query, recounts, and
 * compares every reported field. It calls no engine function that
 * produces a verdict, so agreement here is agreement between two
 * derivations rather than a function confirming its own output.
 */
int sr_witness_check(const sr_query_t *q, const sr_witness_t *w) {
  uint64_t img = (uint64_t)0;
  unsigned n;
  unsigned i;
  double s_expect;
  double h_expect;
  sm_verdict_t v_expect;

  if (q == 0 || w == 0) {
    return 0;
  }
  /* A witness of the other shape is rejected, not checked as if it were
     this one. Checking the wrong thing and passing is worse than not
     checking. */
  if (w->kind != SR_W_WORLDS) {
    return 0;
  }
  if (q->dom != w->dom) {
    return 0;
  }
  if (w->has_domain == 0) {
    /* A groundless witness must claim nothing else. */
    return (w->verdict == SM_GROUNDLESS && w->image == (uint64_t)0 &&
            w->n_worlds == 0u && w->S == -1.0 && w->H == 0.0) ? 1 : 0;
  }
  if (q->n_worlds != w->n_worlds || w->n_worlds > SR_MAX_WORLDS) {
    return 0;
  }
  /* A witness claiming worlds outside the universe would inflate the
     image with answers no real world holds. */
  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    if ((w->live[i] & ~word_mask(i, w->n_worlds)) != (uint64_t)0) {
      return 0;
    }
  }
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    if (i < w->n_worlds &&
        ((w->live[i >> 6] >> (i & 63u)) & (uint64_t)1) != (uint64_t)0) {
      unsigned a = (unsigned)q->ans[i];
      if (a >= q->dom) {
        return 0;
      }
      img |= (uint64_t)1 << a;
    }
  }
  if (img != w->image) {
    return 0;
  }
  n = rpopcount64(img);
  if (n == 0u) {
    v_expect = SM_CONTRADICTION;
  } else if (n == 1u) {
    v_expect = SM_DERIVED;
  } else {
    v_expect = SM_UNDETERMINED;
  }
  if (v_expect != w->verdict) {
    return 0;
  }
  if (v_expect == SM_DERIVED && w->value != lowest_answer(img)) {
    return 0;
  }
  s_expect = (double)(w->dom - n) / (double)w->dom;
  h_expect = (n <= 1u) ? 0.0 : log2((double)n);
  if (w->S < s_expect - 1e-12 || w->S > s_expect + 1e-12) {
    return 0;
  }
  if (w->H < h_expect - 1e-12 || w->H > h_expect + 1e-12) {
    return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* absorption: an answer forced whatever the operands turn out to be    */
/* ------------------------------------------------------------------ */

sm_status_t sr_range(const sr_query_t *q, const sr_state_t *s, uint64_t *out) {
  unsigned i;
  uint64_t all = (uint64_t)0;

  if (q == 0 || s == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (q->has_domain != 0) {
    return sr_image(q, s, out);
  }
  /* A groundless operand could have been anything its domain allows. It
     is not "unknown" in the sense of a value waiting to be found -- there
     is no value -- but for the purpose of asking whether an operator is
     constant regardless, ranging over the whole domain is exactly the
     right question and gives exactly the right answer. */
  if (q->dom == 0u || q->dom > SR_MAX_ANSWERS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  for (i = 0u; i < SR_MAX_ANSWERS; i++) {
    if (i < q->dom) {
      all |= (uint64_t)1 << i;
    }
  }
  *out = all;
  return SM_OK;
}

sm_status_t sr_ask2(const sr_op2_t *op, const sr_query_t *a,
                    const sr_query_t *b, const sr_state_t *s,
                    sr_query_t *composed, sr_result_t *out) {
  uint64_t ra = (uint64_t)0;
  uint64_t rb = (uint64_t)0;
  uint64_t img = (uint64_t)0;
  unsigned x;
  unsigned y;
  unsigned i;
  sm_status_t st;

  if (op == 0 || a == 0 || b == 0 || s == 0 || composed == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (op->dom_a != a->dom || op->dom_b != b->dom) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (a->n_worlds != b->n_worlds) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }

  /* Both are questions: the exact path. Compose pointwise and ask once,
     so shared structure is preserved rather than approximated. */
  if (a->has_domain != 0 && b->has_domain != 0) {
    st = sr_map2(op, a, b, composed);
    if (st != SM_OK) {
      return st;
    }
    return sr_ask(composed, s, out);
  }

  /* At least one operand is not a question. Mark the composed query
     groundless so a caller cannot mistake it for something checkable
     against worlds. */
  st = sr_query_groundless(composed, a->n_worlds, op->dom_out);
  if (st != SM_OK) {
    return st;
  }

  sm_ancestry_clear(&out->ancestry);
  out->intensity = 0.0;
  out->allowed = 0;
  out->value = 0u;

  /* No situation at all outranks everything: there is nothing for the
     operator to be constant over. */
  if (s->has_domain == 0) {
    for (i = 0u; i < SR_WORLD_WORDS; i++) {
      out->witness.live[i] = (uint64_t)0;
    }
    out->witness.kind = SR_W_WORLDS;
    out->witness.n_worlds = 0u;
    out->witness.image = (uint64_t)0;
    out->witness.dom = op->dom_out;
    out->witness.has_domain = 0;
    out->witness.verdict = SM_GROUNDLESS;
    out->witness.value = 0u;
    out->witness.S = -1.0;
    out->witness.H = 0.0;
    out->witness.range_a = (uint64_t)0;
    out->witness.range_b = (uint64_t)0;
    out->verdict = SM_GROUNDLESS;
    out->S = -1.0;
    out->H = 0.0;
    return SM_OK;
  }

  st = sr_range(a, s, &ra);
  if (st != SM_OK) {
    return st;
  }
  st = sr_range(b, s, &rb);
  if (st != SM_OK) {
    return st;
  }

  /* Scan the operator over everything the two operands could still be.
     Both loops are bounded by SR_MAX_ANSWERS, not by the data. */
  for (x = 0u; x < SR_MAX_ANSWERS; x++) {
    if (x >= op->dom_a || ((ra >> x) & (uint64_t)1) == (uint64_t)0) {
      continue;
    }
    for (y = 0u; y < SR_MAX_ANSWERS; y++) {
      unsigned v;
      if (y >= op->dom_b || ((rb >> y) & (uint64_t)1) == (uint64_t)0) {
        continue;
      }
      v = (unsigned)op->out[x * op->dom_b + y];
      if (v >= op->dom_out) {
        return SM_ERR_INDEX_OUT_OF_DOMAIN;
      }
      img |= (uint64_t)1 << v;
    }
  }

  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    out->witness.live[i] = (uint64_t)0;
  }
  out->witness.kind = SR_W_ABSORB;
  out->witness.n_worlds = 0u;
  out->witness.image = img;
  out->witness.dom = op->dom_out;
  out->witness.range_a = ra;
  out->witness.range_b = rb;

  if (rpopcount64(img) == 1u) {
    /* Constant across the whole product: forced, whatever the operands
       are, INCLUDING the one that is not a question. This is where
       "false and groundless" comes out false -- not by a rule about AND
       and false, but because the AND table holds one value across that
       whole row. Any operator with an absorbing element gets this for
       free, over any domain, and none of them are named in this file. */
    out->witness.has_domain = 1;
    out->witness.verdict = SM_DERIVED;
    out->witness.value = lowest_answer(img);
    out->witness.S = (double)(op->dom_out - 1u) / (double)op->dom_out;
    out->witness.H = 0.0;
    out->verdict = SM_DERIVED;
    out->value = out->witness.value;
    out->S = out->witness.S;
    out->H = 0.0;
    return SM_OK;
  }

  /* Not constant, so the answer really does depend on a question that
     does not exist. GROUNDLESS, deliberately not SM_UNDETERMINED:
     undetermined would assert that the answer set IS this image, and this
     image came from a product, which is an over-approximation. Claiming
     nothing is the only sound thing left. */
  out->witness.has_domain = 0;
  out->witness.image = (uint64_t)0;
  out->witness.verdict = SM_GROUNDLESS;
  out->witness.value = 0u;
  out->witness.S = -1.0;
  out->witness.H = 0.0;
  out->verdict = SM_GROUNDLESS;
  out->S = -1.0;
  out->H = 0.0;
  return SM_OK;
}

int sr_witness_check_absorb(const sr_op2_t *op, const sr_witness_t *w) {
  uint64_t img = (uint64_t)0;
  unsigned x;
  unsigned y;
  double s_expect;

  if (op == 0 || w == 0) {
    return 0;
  }
  if (w->kind != SR_W_ABSORB) {
    return 0;
  }
  if (w->dom != op->dom_out || op->dom_out == 0u ||
      op->dom_out > SR_MAX_ANSWERS) {
    return 0;
  }
  if (op->dom_a == 0u || op->dom_b == 0u ||
      op->dom_a > SR_MAX_ANSWERS || op->dom_b > SR_MAX_ANSWERS) {
    return 0;
  }
  /* A range claiming answers outside an operand domain would let a
     witness make the product look different from what it is. */
  for (x = 0u; x < SR_MAX_ANSWERS; x++) {
    if (x >= op->dom_a && ((w->range_a >> x) & (uint64_t)1) != (uint64_t)0) {
      return 0;
    }
    if (x >= op->dom_b && ((w->range_b >> x) & (uint64_t)1) != (uint64_t)0) {
      return 0;
    }
  }
  if (w->range_a == (uint64_t)0 || w->range_b == (uint64_t)0) {
    return 0;
  }
  /* Recompute the product from the table alone. Separate loop, sharing
     no verdict-producing code with sr_ask2. */
  for (x = 0u; x < SR_MAX_ANSWERS; x++) {
    if (x >= op->dom_a || ((w->range_a >> x) & (uint64_t)1) == (uint64_t)0) {
      continue;
    }
    for (y = 0u; y < SR_MAX_ANSWERS; y++) {
      if (y >= op->dom_b || ((w->range_b >> y) & (uint64_t)1) == (uint64_t)0) {
        continue;
      }
      if ((unsigned)op->out[x * op->dom_b + y] >= op->dom_out) {
        return 0;
      }
      img |= (uint64_t)1 << (unsigned)op->out[x * op->dom_b + y];
    }
  }
  if (rpopcount64(img) != 1u) {
    /* The only absorption witness worth checking is one claiming a
       derivation. Anything else should have been recorded groundless. */
    return 0;
  }
  if (w->has_domain != 1 || w->verdict != SM_DERIVED) {
    return 0;
  }
  if (w->image != img || w->value != lowest_answer(img)) {
    return 0;
  }
  s_expect = (double)(w->dom - 1u) / (double)w->dom;
  if (w->S < s_expect - 1e-12 || w->S > s_expect + 1e-12) {
    return 0;
  }
  if (w->H != 0.0) {
    return 0;
  }
  return 1;
}

/* ------------------------------------------------------------------ */
/* eliminating: licensed, and then not                                  */
/* ------------------------------------------------------------------ */

sm_status_t sr_observe(sr_state_t *s, const sr_query_t *q, unsigned answer) {
  unsigned w;

  if (s == 0 || q == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (s->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (q->n_worlds != s->n_worlds) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (answer >= q->dom) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  for (w = 0u; w < SR_MAX_WORLDS; w++) {
    if (w < s->n_worlds && (unsigned)q->ans[w] != answer) {
      s->live[w >> 6] &= ~((uint64_t)1 << (w & 63u));
    }
  }
  /* Emptying the state is allowed and is not an error: it means the
     constraints admit no world, which is a contradiction. Reported by
     sr_ask, not repaired here. */
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* settling: commitment earned by convergence, not scheduled            */
/* ------------------------------------------------------------------ */

sm_status_t sr_settle_begin(sr_settle_t *t, const sr_state_t *s,
                            const sr_query_t *target) {
  unsigned i;
  sm_status_t st;

  if (t == 0 || s == 0 || target == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  for (i = 0u; i < SR_MAX_ROUNDS; i++) {
    t->removed[i] = 0u;
  }
  t->rounds = 0u;
  t->productive = 0u;
  t->informative = 0u;
  t->live_at_start = sr_live_count(s);
  t->live_now = t->live_at_start;
  t->image = (uint64_t)0;
  if (s->has_domain != 0 && target->has_domain != 0) {
    st = sr_image(target, s, &t->image);
    if (st != SM_OK) {
      return st;
    }
  }
  /* Not settled at the start. Nothing has been tried, so nothing has
     failed to change anything -- reporting settled here would let a
     caller commit having applied no context at all. */
  t->settled = 0;
  t->contradicted = (s->has_domain != 0 && t->live_at_start == 0u) ? 1 : 0;
  return SM_OK;
}

sm_status_t sr_settle_step(sr_settle_t *t, sr_state_t *s,
                           const sr_query_t *target,
                           const sr_query_t *constraint, unsigned answer) {
  unsigned before;
  unsigned after;
  uint64_t img_before;
  uint64_t img_after = (uint64_t)0;
  sm_status_t st;

  if (t == 0 || s == 0 || target == 0 || constraint == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (t->rounds >= SR_MAX_ROUNDS) {
    /* Cannot happen while every productive round removes a world and
       there are at most SR_MAX_WORLDS of them, but a caller is free to
       keep feeding redundant context forever, and silently overwriting
       the trace would lose the record of what was applied. */
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  before = sr_live_count(s);
  img_before = t->image;

  st = sr_observe(s, constraint, answer);
  if (st != SM_OK) {
    return st;
  }
  after = sr_live_count(s);
  if (s->has_domain != 0 && target->has_domain != 0) {
    st = sr_image(target, s, &img_after);
    if (st != SM_OK) {
      return st;
    }
  }

  t->removed[t->rounds] = before - after;
  t->rounds++;
  t->live_now = after;
  t->image = img_after;

  if (before != after) {
    t->productive++;
  }
  if (img_after != img_before) {
    /* The answer moved. Keep going. */
    t->informative++;
    t->settled = 0;
  } else {
    /* The exact form of "belief stopped changing": this context ruled out
       no answer that was not already ruled out. No epsilon, no norm, and
       no way for it to be nearly true. Note this can hold while the round
       was still productive -- worlds went, but none the target could
       tell apart. */
    t->settled = 1;
  }
  if (after == 0u && s->has_domain != 0) {
    t->contradicted = 1;
  }
  return SM_OK;
}

sm_status_t sr_settle_commit(const sr_settle_t *t, const sr_query_t *target,
                             const sr_state_t *s, sr_result_t *out) {
  sm_status_t st;

  if (t == 0 || target == 0 || s == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  st = sr_ask(target, s, out);
  if (st != SM_OK) {
    return st;
  }
  /* The whole rule, in one line. Commitment is not scheduled by having
     run enough rounds, or by the trace having settled, or by anything
     else the caller can arrange: it happens when the surviving worlds
     agree, and otherwise it does not happen. */
  out->allowed = (out->verdict == SM_DERIVED) ? 1 : 0;
  if (out->allowed == 0) {
    /* A refused commit yields no value. Not a best guess, not the last
       value seen: zero, so a caller that reads it anyway gets something
       obviously unusable rather than something plausible. */
    out->value = 0u;
  }
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* refinement: deriving the worlds from the questions                   */
/* ------------------------------------------------------------------ */

sm_status_t sr_refine(const sr_query_t *questions, unsigned n_questions,
                      unsigned n_situations, sr_partition_t *out) {
  unsigned i;
  unsigned j;
  unsigned k;

  if (questions == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_situations == 0u) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (n_situations > SR_MAX_SITUATIONS || n_questions > SR_MAX_QUESTIONS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  for (k = 0u; k < n_questions; k++) {
    if (questions[k].has_domain == 0) {
      return SM_ERR_EMPTY_DOMAIN;
    }
    if (questions[k].n_worlds != n_situations) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  for (i = 0u; i < SR_MAX_SITUATIONS; i++) {
    out->cell[i] = (uint8_t)0;
  }
  out->n_situations = n_situations;
  out->n_cells = 0u;

  /* Two situations share a cell exactly when NO question tells them
     apart. Pairwise rather than hashed, because a hash table is a dynamic
     structure and the bound here is already static: SR_MAX_SITUATIONS
     squared times SR_MAX_QUESTIONS, all compile-time known. */
  for (i = 0u; i < SR_MAX_SITUATIONS; i++) {
    int placed = 0;
    if (i >= n_situations) {
      continue;
    }
    for (j = 0u; j < SR_MAX_SITUATIONS; j++) {
      int same = 1;
      if (j >= i) {
        break;
      }
      for (k = 0u; k < SR_MAX_QUESTIONS; k++) {
        if (k >= n_questions) {
          break;
        }
        if (questions[k].ans[i] != questions[k].ans[j]) {
          same = 0;
          break;
        }
      }
      if (same != 0) {
        out->cell[i] = out->cell[j];
        placed = 1;
        break;
      }
    }
    if (placed == 0) {
      /* Numbered in order of first appearance, so the same input always
         yields the same numbering and a replay is byte-identical. */
      if (out->n_cells >= SR_MAX_WORLDS) {
        return SM_ERR_DOMAIN_TOO_LARGE;
      }
      out->cell[i] = (uint8_t)out->n_cells;
      out->n_cells++;
    }
  }
  /* With no questions at all, every situation collapses into one cell:
     nothing can be told apart, which is the correct answer and not a
     degenerate case to special-case away. */
  return SM_OK;
}

sm_status_t sr_project(const sr_partition_t *p, const sr_query_t *situation_q,
                       sr_query_t *out) {
  unsigned answer_of[SR_MAX_WORLDS];
  int seen[SR_MAX_WORLDS];
  unsigned i;
  sm_status_t st;

  if (p == 0 || situation_q == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (situation_q->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (situation_q->n_worlds != p->n_situations || p->n_cells == 0u) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    seen[i] = 0;
    answer_of[i] = 0u;
  }
  for (i = 0u; i < SR_MAX_SITUATIONS; i++) {
    unsigned c;
    unsigned a;
    if (i >= p->n_situations) {
      continue;
    }
    c = (unsigned)p->cell[i];
    a = (unsigned)situation_q->ans[i];
    if (c >= p->n_cells || a >= situation_q->dom) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
    if (seen[c] == 0) {
      seen[c] = 1;
      answer_of[c] = a;
    } else if (answer_of[c] != a) {
      /* Two situations in one cell, two different answers. The partition
         cannot express this question. Refused rather than collapsed to
         one of the two, because collapsing would invent a distinction
         that was never made. */
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  st = sr_query_init(out, p->n_cells, situation_q->dom);
  if (st != SM_OK) {
    return st;
  }
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    if (i < p->n_cells) {
      out->ans[i] = (uint8_t)answer_of[i];
    }
  }
  return SM_OK;
}

int sr_distinguishes(const sr_partition_t *p, const sr_query_t *situation_q) {
  unsigned answer_of[SR_MAX_WORLDS];
  int seen[SR_MAX_WORLDS];
  unsigned i;

  if (p == 0 || situation_q == 0 || situation_q->has_domain == 0) {
    return 0;
  }
  if (situation_q->n_worlds != p->n_situations) {
    return 0;
  }
  for (i = 0u; i < SR_MAX_WORLDS; i++) {
    seen[i] = 0;
    answer_of[i] = 0u;
  }
  for (i = 0u; i < SR_MAX_SITUATIONS; i++) {
    unsigned c;
    unsigned a;
    if (i >= p->n_situations) {
      continue;
    }
    c = (unsigned)p->cell[i];
    a = (unsigned)situation_q->ans[i];
    if (c >= p->n_cells) {
      return 0;
    }
    if (seen[c] == 0) {
      seen[c] = 1;
      answer_of[c] = a;
    } else if (answer_of[c] != a) {
      return 1;   /* a distinction this partition cannot make */
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* choosing what to ask                                                 */
/* ------------------------------------------------------------------ */

sm_status_t sr_probe(const sr_state_t *s, const sr_query_t *target,
                     const sr_query_t *question, sr_probe_t *out) {
  uint64_t img[SR_MAX_ANSWERS];
  uint64_t whole = (uint64_t)0;
  unsigned live = 0u;
  unsigned a;
  unsigned w;
  unsigned most = 0u;
  unsigned fewest = 0xFFFFFFFFu;
  int seen = 0;

  if (s == 0 || target == 0 || question == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (s->has_domain == 0 || target->has_domain == 0 ||
      question->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (question->n_worlds != s->n_worlds || target->n_worlds != s->n_worlds) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }

  for (a = 0u; a < SR_MAX_ANSWERS; a++) {
    img[a] = (uint64_t)0;
    out->surviving[a] = 0u;
  }
  out->reachable = (uint64_t)0;

  /* One pass. For each live world, file it under the answer this question
     gives there, and record what the TARGET says in that world. That is
     the whole computation: no sampling, no scoring, no lookahead beyond
     the one branch, because one branch is all a question has. */
  for (w = 0u; w < SR_MAX_WORLDS; w++) {
    unsigned qa;
    unsigned ta;
    if (w >= s->n_worlds) {
      continue;
    }
    if (((s->live[w >> 6] >> (w & 63u)) & (uint64_t)1) == (uint64_t)0) {
      continue;
    }
    qa = (unsigned)question->ans[w];
    ta = (unsigned)target->ans[w];
    if (qa >= question->dom || ta >= target->dom) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
    live++;
    out->surviving[qa]++;
    out->reachable |= (uint64_t)1 << qa;
    img[qa] |= (uint64_t)1 << ta;
    whole |= (uint64_t)1 << ta;
  }

  out->sufficient = 1;
  out->irrelevant = 1;
  for (a = 0u; a < SR_MAX_ANSWERS; a++) {
    if (((out->reachable >> a) & (uint64_t)1) == (uint64_t)0) {
      continue;
    }
    seen = 1;
    if (out->surviving[a] > most) {
      most = out->surviving[a];
    }
    if (out->surviving[a] < fewest) {
      fewest = out->surviving[a];
    }
    if (rpopcount64(img[a]) != 1u) {
      out->sufficient = 0;
    }
    if (img[a] != whole) {
      /* Some answer narrows the target. The question is relevant, and it
         is relevant because of what it would rule out, not because
         something judged it interesting. */
      out->irrelevant = 0;
    }
  }

  if (seen == 0) {
    /* No live worlds at all: a contradiction. Nothing to ask, and
       reporting a removal count here would be inventing one. */
    out->worst_case_removed = 0u;
    out->best_case_removed = 0u;
    out->sufficient = 0;
    out->irrelevant = 1;
    return SM_OK;
  }
  /* The floor is the branch that leaves the most standing. */
  out->worst_case_removed = live - most;
  out->best_case_removed = live - fewest;
  return SM_OK;
}

sm_status_t sr_choose(const sr_state_t *s, const sr_query_t *target,
                      const sr_query_t *questions, unsigned n_questions,
                      sr_ask_policy_t policy, unsigned *out_index,
                      sr_probe_t *out_probe) {
  sr_probe_t p;
  sr_probe_t best;
  unsigned best_i;
  unsigned i;
  unsigned best_score = 0u;
  int have = 0;
  uint64_t img = (uint64_t)0;
  sm_status_t st;

  if (s == 0 || target == 0 || questions == 0 || out_index == 0 ||
      out_probe == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_questions > SR_MAX_WORLDS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  *out_index = n_questions;

  if (s->has_domain == 0 || target->has_domain == 0) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  st = sr_image(target, s, &img);
  if (st != SM_OK) {
    return st;
  }
  if (rpopcount64(img) <= 1u) {
    /* Already derived, or a contradiction. Asking anything would be
       spending a question on a settled matter. */
    return SM_OK;
  }

  for (i = 0u; i < n_questions; i++) {
    if (questions[i].has_domain == 0) {
      /* Not a question, so it cannot help. Skipped as irrelevant rather
         than failing the whole choice: one malformed candidate on the
         menu should not stop the others being considered. */
      continue;
    }
    st = sr_probe(s, target, &questions[i], &p);
    if (st != SM_OK) {
      return st;
    }
    if (p.irrelevant != 0) {
      /* Proved unable to narrow the target BY ITSELF; not ranked. It may
         still help in combination with others (see jointly_relevant in
         smarsh_learner.c), which a one-question choice cannot see. */
      continue;
    }
    if (p.sufficient != 0) {
      /* Ends it whatever the answer. Derived, so it outranks any policy,
         and the first such question wins so the choice stays replayable. */
      *out_index = i;
      *out_probe = p;
      return SM_OK;
    }
    {
      unsigned score = (policy == SR_ASK_OPPORTUNITY) ? p.best_case_removed
                                                      : p.worst_case_removed;
      if (have == 0 || score > best_score) {
        have = 1;
        best_score = score;
        best_i = i;
        best = p;
      }
    }
  }

  if (have != 0) {
    *out_index = best_i;
    *out_probe = best;
  }
  /* Otherwise out_index stays n_questions: no candidate narrows the
     target on its own. That does NOT prove the set useless: two questions
     can each be irrelevant alone and settle the target together. A caller
     that must know checks the joint refinement, as the learner does. */
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* reopening a claim                                                    */
/* ------------------------------------------------------------------ */

void sr_checkpoint(const sr_state_t *s, sr_state_t *out) {
  unsigned i;

  if (s == 0 || out == 0) {
    return;
  }
  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    out->live[i] = s->live[i];
  }
  out->n_worlds = s->n_worlds;
  out->has_domain = s->has_domain;
}

sm_status_t sr_retract(sr_state_t *s, const sr_state_t *checkpoint,
                       sm_ancestry_t *ancestry, unsigned guess_id) {
  unsigned i;

  if (s == 0 || checkpoint == 0 || ancestry == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (guess_id >= SM_MAX_GUESSES) {
    return SM_ERR_GUESS_ID_OUT_OF_RANGE;
  }
  for (i = 0u; i < SR_WORLD_WORDS; i++) {
    s->live[i] = checkpoint->live[i];
  }
  s->n_worlds = checkpoint->n_worlds;
  s->has_domain = checkpoint->has_domain;

  /* The guess and what it bought go together. Restoring the worlds while
     leaving the debt would report information the state no longer rests
     on; clearing the debt while leaving the worlds would hide a cut
     nothing licensed. */
  ancestry->ids &= ~((uint64_t)1 << guess_id);
  ancestry->remaining[guess_id] = (uint16_t)0;
  return SM_OK;
}

static double intensity_of(double s, double tau, sm_intensity_policy_t policy) {
  if (policy == SM_INTENSITY_HEADROOM) {
    if (tau >= 1.0) {
      return 0.0;
    }
    return (s - tau) / (1.0 - tau);
  }
  return s;   /* SM_INTENSITY_SUPPORT: g(S) = S, what Smarsh ships. */
}

sm_status_t sr_speculate(sr_state_t *s, const sr_query_t *q, double tau,
                         uint64_t seed, sm_intensity_policy_t policy,
                         unsigned guess_id, sm_ancestry_t *ancestry,
                         sr_result_t *out) {
  sm_status_t st;
  unsigned n;
  unsigned k;
  unsigned pick = 0u;
  unsigned seen = 0u;
  unsigned i;
  unsigned w;
  uint64_t rng_state;

  if (s == 0 || q == 0 || ancestry == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  /* Written as !(>=) || !(<=), not as a range test, so NaN is rejected.
     The range test this replaced let NaN through, and after that S < tau
     is also false for NaN -- so a NaN threshold meant "always allowed to
     guess", the most permissive setting reached by accident. smarsh_core
     already guarded this; the guard was not carried over when this kernel
     was written, and nothing caught it until the C test suite tried NaN. */
  if (!(tau >= 0.0) || !(tau <= 1.0)) {
    return SM_ERR_BAD_THRESHOLD;
  }
  if (guess_id >= SM_MAX_GUESSES) {
    return SM_ERR_GUESS_ID_OUT_OF_RANGE;
  }

  /* Ask first. The witness recorded on `out` is the situation BEFORE any
     cut, because that is what is true: a speculation derives nothing, and
     a witness that showed the post-cut state would read as a derivation
     of the guessed answer. So the witness says UNDETERMINED while the
     result says SM_SPECULATED, and that gap is the whole point -- it is
     the value the witness does not back. */
  st = sr_ask(q, s, out);
  if (st != SM_OK) {
    return st;
  }
  out->ancestry = *ancestry;
  out->allowed = 0;
  out->intensity = 0.0;

  /* Nothing to speculate about in three of the four cases: there is no
     question, or the constraints admit nothing, or they already decided
     it. Guessing at a derived answer would be adding a debt for
     information already held. */
  if (out->verdict != SM_UNDETERMINED) {
    return SM_OK;
  }
  if (out->S < tau) {
    /* Refused. The state is untouched -- verify by inspection: nothing
       above this line writes to `s`. Undetermined, not groundless: there
       IS a question here and there IS a domain, and the language surface
       renders this as the groundless value without the kernel having to
       pretend the two are the same thing. */
    return SM_OK;
  }

  n = rpopcount64(out->witness.image);
  if (n < 2u) {
    return SM_ERR_INTERNAL_INVARIANT;
  }
  rng_state = seed;
  k = (unsigned)(rsplitmix64(&rng_state) % (uint64_t)n);
  /* Uniform over what remains. It is not trying to be right more often
     than chance; that is exactly what keeps this from being prediction. */
  for (i = 0u; i < SR_MAX_ANSWERS; i++) {
    if (((out->witness.image >> i) & (uint64_t)1) != (uint64_t)0) {
      if (seen == k) {
        pick = i;
      }
      seen++;
    }
  }

  /* The unlicensed elimination. Mechanically identical to sr_observe --
     that identity is the claim, not a coincidence of implementation. */
  for (w = 0u; w < SR_MAX_WORLDS; w++) {
    if (w < s->n_worlds && (unsigned)q->ans[w] != pick) {
      s->live[w >> 6] &= ~((uint64_t)1 << (w & 63u));
    }
  }

  /* remaining = |D| at the moment of the cut, so the debt is log2(n)
     bits. Added to a SET, so the same guess reached twice is one guess. */
  st = sm_ancestry_add(ancestry, guess_id, n);
  if (st != SM_OK) {
    return st;
  }
  out->ancestry = *ancestry;
  out->verdict = SM_SPECULATED;
  out->value = pick;
  out->intensity = intensity_of(out->S, tau, policy);
  out->allowed = 1;
  return SM_OK;
}
