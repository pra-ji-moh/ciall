/*
 * smarsh_concept.c -- see smarsh_concept.h.
 *
 * Every loop runs over a compile-time constant (SR_MAX_SITUATIONS,
 * SR_MAX_ANSWERS, SR_MAX_QUESTIONS) with an early exit, like the kernel.
 */

#include "smarsh_concept.h"

#include <string.h>

static int valid_q(const sr_query_t *q, unsigned n) {
  return q->has_domain != 0 && q->n_worlds == n;
}

int sc_grounded(const sc_op_t *op, unsigned a, unsigned b) {
  unsigned idx;
  if (op == 0 || a >= op->dom_a || b >= op->dom_b) {
    return 0;
  }
  idx = a * op->dom_b + b;
  return (int)((op->grounded[idx >> 6] >> (idx & 63u)) & (uint64_t)1);
}

unsigned sc_value(const sc_op_t *op, unsigned a, unsigned b) {
  if (!sc_grounded(op, a, b)) {
    return 0u;
  }
  return (unsigned)op->table[a * op->dom_b + b];
}

/* Read the operator off a successful projection: every situation files
   its argument combination under the target's answer. The projection
   already proved that no combination gets two answers; this re-checks it
   rather than trusting that. */
static sm_status_t read_op(const sr_query_t *qa, const sr_query_t *qb,
                           const sr_query_t *target, unsigned n, sc_op_t *op) {
  unsigned s;
  memset(op, 0, sizeof *op);
  op->arity = qb != 0 ? 2u : 1u;
  op->dom_a = qa->dom;
  op->dom_b = qb != 0 ? qb->dom : 1u;
  op->dom_out = target->dom;
  op->witnessed_in = n;
  for (s = 0u; s < SR_MAX_SITUATIONS; s++) {
    unsigned idx;
    if (s >= n) {
      break;
    }
    idx = (unsigned)qa->ans[s] * op->dom_b + (qb != 0 ? (unsigned)qb->ans[s] : 0u);
    if ((op->grounded[idx >> 6] >> (idx & 63u)) & (uint64_t)1) {
      if (op->table[idx] != target->ans[s]) {
        return SM_ERR_INTERNAL_INVARIANT;   /* the projection lied */
      }
      continue;
    }
    op->grounded[idx >> 6] |= (uint64_t)1 << (idx & 63u);
    op->table[idx] = target->ans[s];
    op->n_grounded++;
  }
  return SM_OK;
}

sm_status_t sc_discover(const sr_query_t *cands, unsigned n_cands,
                        const sr_query_t *target, unsigned n_situations,
                        sc_discovery_t *out) {
  sr_partition_t p;
  sr_query_t pair[2];
  sr_query_t proj;
  unsigned i;
  unsigned j;
  sm_status_t st;

  if (cands == 0 || target == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_cands == 0u || n_situations == 0u) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (n_cands > SR_MAX_QUESTIONS || n_situations > SR_MAX_SITUATIONS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  if (!valid_q(target, n_situations)) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  for (i = 0u; i < SR_MAX_QUESTIONS; i++) {
    if (i < n_cands && !valid_q(&cands[i], n_situations)) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  out->found = 0;
  out->needs_more = 0;
  out->arity = 0u;
  out->arg[0] = out->arg[1] = 0u;
  out->counter_s = out->counter_t = 0u;

  /* One argument, then two: explain with as little as works. */
  for (i = 0u; i < SR_MAX_QUESTIONS; i++) {
    if (i >= n_cands) {
      break;
    }
    st = sr_refine(&cands[i], 1u, n_situations, &p);
    if (st != SM_OK) {
      return st;
    }
    if (sr_project(&p, target, &proj) == SM_OK) {
      out->found = 1;
      out->arity = 1u;
      out->arg[0] = i;
      return read_op(&cands[i], 0, target, n_situations, &out->op);
    }
  }
  for (i = 0u; i < SR_MAX_QUESTIONS; i++) {
    if (i >= n_cands) {
      break;
    }
    for (j = 0u; j < SR_MAX_QUESTIONS; j++) {
      if (j >= n_cands) {
        break;
      }
      if (j <= i) {
        continue;
      }
      pair[0] = cands[i];
      pair[1] = cands[j];
      st = sr_refine(pair, 2u, n_situations, &p);
      if (st != SM_OK) {
        return st;
      }
      if (sr_project(&p, target, &proj) == SM_OK) {
        out->found = 1;
        out->arity = 2u;
        out->arg[0] = i;
        out->arg[1] = j;
        return read_op(&cands[i], &cands[j], target, n_situations, &out->op);
      }
    }
  }

  /* Nothing small enough. Either all of them together would do it (it
     needs more at once than this search tries), or not even all of them,
     and then there is a pair of situations that proves it. */
  st = sr_refine(cands, n_cands, n_situations, &p);
  if (st != SM_OK) {
    return st;
  }
  if (sr_project(&p, target, &proj) == SM_OK) {
    out->needs_more = 1;
    return SM_OK;
  }
  for (i = 0u; i < SR_MAX_SITUATIONS; i++) {
    if (i >= n_situations) {
      break;
    }
    for (j = 0u; j < SR_MAX_SITUATIONS; j++) {
      if (j >= i) {
        break;
      }
      if (p.cell[i] == p.cell[j] && target->ans[i] != target->ans[j]) {
        out->counter_s = j;
        out->counter_t = i;
        return SM_OK;
      }
    }
  }
  return SM_ERR_INTERNAL_INVARIANT;   /* projection refused with no witness */
}

int sc_check(const sc_op_t *op, const sr_query_t *a, const sr_query_t *b,
             const sr_query_t *target, unsigned n_situations) {
  unsigned s;
  if (op == 0 || a == 0 || target == 0 || (op->arity == 2u && b == 0)) {
    return 0;
  }
  if (!valid_q(a, n_situations) || !valid_q(target, n_situations) ||
      (op->arity == 2u && !valid_q(b, n_situations))) {
    return 0;
  }
  for (s = 0u; s < SR_MAX_SITUATIONS; s++) {
    unsigned bv;
    if (s >= n_situations) {
      break;
    }
    bv = op->arity == 2u ? (unsigned)b->ans[s] : 0u;
    if (!sc_grounded(op, a->ans[s], bv) || sc_value(op, a->ans[s], bv) != target->ans[s]) {
      return 0;
    }
  }
  return 1;
}

sm_status_t sc_apply(const sc_op_t *op, const sr_query_t *a, const sr_query_t *b,
                     unsigned n_situations, sr_query_t *out,
                     unsigned *n_ungrounded) {
  unsigned s;
  unsigned missing = 0u;
  sm_status_t st;

  if (op == 0 || a == 0 || out == 0 || n_ungrounded == 0 || (op->arity == 2u && b == 0)) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (!valid_q(a, n_situations) || (op->arity == 2u && !valid_q(b, n_situations))) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  for (s = 0u; s < SR_MAX_SITUATIONS; s++) {
    if (s >= n_situations) {
      break;
    }
    if (!sc_grounded(op, a->ans[s], op->arity == 2u ? (unsigned)b->ans[s] : 0u)) {
      missing++;
    }
  }
  *n_ungrounded = missing;
  if (missing > 0u) {
    return sr_query_groundless(out, n_situations, op->dom_out);
  }
  st = sr_query_init(out, n_situations, op->dom_out);
  if (st != SM_OK) {
    return st;
  }
  for (s = 0u; s < SR_MAX_SITUATIONS; s++) {
    if (s >= n_situations) {
      break;
    }
    st = sr_query_set(out, s, sc_value(op, a->ans[s],
                                       op->arity == 2u ? (unsigned)b->ans[s] : 0u));
    if (st != SM_OK) {
      return st;
    }
  }
  return SM_OK;
}

int sc_same(const sc_op_t *p, const sc_op_t *q, int swapped,
            unsigned *first_diff_a, unsigned *first_diff_b) {
  unsigned a;
  unsigned b;
  unsigned shared = 0u;
  if (p == 0 || q == 0 || p->arity != q->arity) {
    return 0;
  }
  for (a = 0u; a < SR_MAX_ANSWERS; a++) {
    for (b = 0u; b < SR_MAX_ANSWERS; b++) {
      unsigned qa = swapped ? b : a;
      unsigned qb = swapped ? a : b;
      if (!sc_grounded(p, a, b) || !sc_grounded(q, qa, qb)) {
        continue;
      }
      shared++;
      if (sc_value(p, a, b) != sc_value(q, qa, qb)) {
        if (first_diff_a != 0) *first_diff_a = a;
        if (first_diff_b != 0) *first_diff_b = b;
        return 0;
      }
    }
  }
  return shared > 0u;
}

int sc_commutative(const sc_op_t *op) {
  return op != 0 && op->arity == 2u && sc_same(op, op, 1, 0, 0);
}

int sc_identity(const sc_op_t *op, unsigned *e) {
  unsigned c;
  unsigned a;
  if (op == 0 || op->arity != 2u || e == 0) {
    return 0;
  }
  for (c = 0u; c < SR_MAX_ANSWERS; c++) {
    unsigned checked = 0u;
    int ok = 1;
    for (a = 0u; a < SR_MAX_ANSWERS && ok; a++) {
      if (sc_grounded(op, a, c)) {
        checked++;
        ok = sc_value(op, a, c) == a;
      }
      if (ok && sc_grounded(op, c, a)) {
        checked++;
        ok = sc_value(op, c, a) == a;
      }
    }
    if (ok && checked > 0u) {
      *e = c;
      return 1;
    }
  }
  return 0;
}
