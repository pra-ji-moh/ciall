/*
 * smarsh_law.c -- see smarsh_law.h.
 *
 * Evaluation recurses only into EARLIER laws, carrying a depth counter
 * capped at LW_MAX_LAWS, so the stack bound is (LW_MAX_LAWS + 1) frames;
 * the counting itself is a loop.
 */

#include "smarsh_law.h"

#include <string.h>

static sm_status_t eval_at(const lw_book_t *book, unsigned idx, uint64_t a, uint64_t b,
                           uint64_t *value, uint32_t *leaps, unsigned depth);

sm_status_t lw_init(lw_book_t *book, const sc_op_t *one_more_table, uint64_t seen_below,
                    const char *said) {
  lw_law_t *L;
  unsigned n;
  if (book == 0 || one_more_table == 0 || said == 0) return SM_ERR_NULL_ARGUMENT;
  memset(book, 0, sizeof *book);
  /* The counting sequence is the assumed representation; what is checked
     is that the observed "one more" really is the next number in it
     wherever it was witnessed. If it were not, nothing below would mean
     what it says. */
  for (n = 0u; n < SR_MAX_ANSWERS; n++) {
    if (n >= seen_below) break;
    if (!sc_grounded(one_more_table, n, 0u) || sc_value(one_more_table, n, 0u) != n + 1u) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  L = &book->law[0];
  strcpy(L->name, "one more");
  strncpy(L->said, said, sizeof L->said - 1u);
  L->kind = LW_ONE_MORE;
  L->arity = 1u;
  L->table = one_more_table;
  L->one_more_seen_below = seen_below;
  L->checked = (unsigned)seen_below;
  book->one_more = 0u;
  book->n = 1u;
  return SM_OK;
}

/* the step of law L applied to the value so far */
static sm_status_t step_of(const lw_book_t *book, const lw_law_t *L, uint64_t prev, uint64_t a,
                           uint64_t count, uint64_t *out, uint32_t *leaps, unsigned depth) {
  switch (L->step) {
    case LW_STEP_ONE_MORE:
      return eval_at(book, book->one_more, prev, 0u, out, leaps, depth + 1u);
    case LW_STEP_WITH_A:
      return eval_at(book, L->step_law, prev, a, out, leaps, depth + 1u);
    case LW_STEP_WITH_COUNT:
      return eval_at(book, L->step_law, prev, count, out, leaps, depth + 1u);
    default:
      return eval_at(book, L->step_law, prev, count + 1u, out, leaps, depth + 1u);
  }
}

static sm_status_t eval_at(const lw_book_t *book, unsigned idx, uint64_t a, uint64_t b,
                           uint64_t *value, uint32_t *leaps, unsigned depth) {
  const lw_law_t *L;
  uint64_t n, k, v, param;
  sm_status_t st;

  if (depth >= LW_MAX_LAWS) return SM_ERR_INTERNAL_INVARIANT;   /* laws only use earlier laws */
  if (idx >= book->n) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  L = &book->law[idx];

  if (L->kind == LW_ONE_MORE) {
    if (a >= L->one_more_seen_below) *leaps |= (uint32_t)1 << idx;
    *value = a + 1u;
    return SM_OK;
  }

  /* seen? then it is a fact, and no law is needed */
  if (L->table != 0 && a < SR_MAX_ANSWERS && (L->arity == 1u || b < SR_MAX_ANSWERS) &&
      sc_grounded(L->table, (unsigned)a, L->arity == 2u ? (unsigned)b : 0u)) {
    *value = sc_value(L->table, (unsigned)a, L->arity == 2u ? (unsigned)b : 0u);
    return SM_OK;
  }

  /* not seen: the law is taken on trust here, and that is recorded */
  *leaps |= (uint32_t)1 << idx;
  n = L->arity == 2u ? b : a;
  param = L->arity == 2u ? a : 0u;
  if (n > LW_MAX_STEPS) return SM_ERR_DOMAIN_TOO_LARGE;
  v = L->base == LW_BASE_A ? param : 0u;
  for (k = 0u; k < LW_MAX_STEPS; k++) {
    if (k >= n) break;
    st = step_of(book, L, v, param, k, &v, leaps, depth);
    if (st != SM_OK) return st;
  }
  *value = v;
  return SM_OK;
}

sm_status_t lw_eval(const lw_book_t *book, unsigned idx, uint64_t a, uint64_t b,
                    uint64_t *value, uint32_t *leaps) {
  if (book == 0 || value == 0 || leaps == 0) return SM_ERR_NULL_ARGUMENT;
  *leaps = 0u;
  return eval_at(book, idx, a, b, value, leaps, 0u);
}

/* Does this form agree with every witnessed step of the table? */
static int fits(const lw_book_t *book, const sc_op_t *t, lw_law_t *L) {
  unsigned a, b, checked = 0u;
  uint32_t leaps = 0u;
  unsigned amax = t->arity == 2u ? t->dom_a : 1u;
  unsigned bmax = t->arity == 2u ? t->dom_b : t->dom_a;

  /* the base */
  for (a = 0u; a < SR_MAX_ANSWERS; a++) {
    unsigned x = t->arity == 2u ? a : 0u, want;
    if (a >= amax) break;
    if (!sc_grounded(t, x, 0u)) continue;
    want = L->base == LW_BASE_A ? a : 0u;
    if (t->arity == 1u && L->base == LW_BASE_A) return 0;   /* no a to start from */
    if (sc_value(t, x, 0u) != want) return 0;
    checked++;
  }
  if (checked == 0u) return 0;

  /* every witnessed step */
  for (a = 0u; a < SR_MAX_ANSWERS; a++) {
    if (a >= amax) break;
    for (b = 0u; b + 1u < SR_MAX_ANSWERS; b++) {
      unsigned x0, y0, x1, y1;
      uint64_t got;
      if (b + 1u >= bmax) break;
      x0 = t->arity == 2u ? a : b;
      y0 = t->arity == 2u ? b : 0u;
      x1 = t->arity == 2u ? a : b + 1u;
      y1 = t->arity == 2u ? b + 1u : 0u;
      if (!sc_grounded(t, x0, y0) || !sc_grounded(t, x1, y1)) continue;
      if (step_of(book, L, sc_value(t, x0, y0), a, b, &got, &leaps, 0u) != SM_OK) return 0;
      if (got != sc_value(t, x1, y1)) return 0;
      checked++;
    }
  }
  L->checked = checked;
  L->check_leaps = leaps;
  return 1;
}

sm_status_t lw_discover(lw_book_t *book, const sc_op_t *table, const char *name,
                        const char *said, unsigned *idx) {
  lw_law_t L;
  unsigned base, j, form;
  if (book == 0 || table == 0 || name == 0 || said == 0 || idx == 0) return SM_ERR_NULL_ARGUMENT;
  if (book->n >= LW_MAX_LAWS) return SM_ERR_DOMAIN_TOO_LARGE;
  for (base = 0u; base < 2u; base++) {
    /* forms in order: one more, then each earlier two-argument law with a,
       with the count, with the count plus one */
    for (form = 0u; form < 1u + 3u * LW_MAX_LAWS; form++) {
      memset(&L, 0, sizeof L);
      L.kind = LW_RECURSIVE;
      L.arity = table->arity;
      L.base = base == 0u ? LW_BASE_A : LW_BASE_ZERO;
      if (form == 0u) {
        L.step = LW_STEP_ONE_MORE;
      } else {
        static const lw_step_t KIND[3] = {LW_STEP_WITH_A, LW_STEP_WITH_COUNT, LW_STEP_WITH_NEXT};
        j = (form - 1u) / 3u;
        if (j >= book->n || book->law[j].arity != 2u) continue;
        L.step = KIND[(form - 1u) % 3u];
        if (L.step == LW_STEP_WITH_A && table->arity != 2u) continue;
        L.step_law = j;
      }
      if (!fits(book, table, &L)) continue;
      strncpy(L.name, name, SC_NAME_LEN - 1u);
      strncpy(L.said, said, sizeof L.said - 1u);
      L.table = table;
      book->law[book->n] = L;
      *idx = book->n++;
      return SM_OK;
    }
  }
  return SM_ERR_EMPTY_DOMAIN;
}
