/*
 * smarsh_guess.c -- see smarsh_guess.h.
 */
#include "smarsh_guess.h"

#include <string.h>

#define GS_GRID 63.0
#define GS_WHEN 4096.0

void gs_begin(gs_guess_t *g) {
  if (g == 0) return;
  memset(g, 0, sizeof *g);
}

sm_status_t gs_saw(gs_guess_t *g, double when, double row, double col) {
  if (g == 0) return SM_ERR_NULL_ARGUMENT;
  if (g->n >= GS_MAX_ROWS) {   /* the oldest look is the one it can most afford to forget */
    unsigned i;
    for (i = 1u; i < g->n; i++) {
      g->t[i - 1u] = g->t[i];
      g->r[i - 1u] = g->r[i];
      g->c[i - 1u] = g->c[i];
    }
    g->n--;
  }
  g->t[g->n] = when;
  g->r[g->n] = row;
  g->c[g->n] = col;
  g->n++;
  return SM_OK;
}

/* the table it formulates over: when and where now, against where next */
static sm_status_t build(const gs_guess_t *g, int want_row, fr_situation_t *s) {
  unsigned i, idx;
  sm_status_t st;
  fr_situation_init(s);
  st = fr_reading(s, "when", 0.0, GS_WHEN, 1, &idx);
  if (st != SM_OK) return st;
  st = fr_reading(s, "row", 0.0, GS_GRID, 1, &idx);
  if (st != SM_OK) return st;
  st = fr_reading(s, "col", 0.0, GS_GRID, 1, &idx);
  if (st != SM_OK) return st;
  st = fr_outcome(s, want_row ? "next_row" : "next_col", 0.0, GS_GRID, 1);
  if (st != SM_OK) return st;
  for (i = 0u; i + 1u < g->n; i++) {
    double row[3];
    row[0] = g->t[i];
    row[1] = g->r[i];
    row[2] = g->c[i];
    st = fr_observe(s, row, want_row ? g->r[i + 1u] : g->c[i + 1u]);
    if (st != SM_OK) return st;
  }
  return SM_OK;
}

sm_status_t gs_formulate(gs_guess_t *g) {
  static fr_situation_t sit;
  fr_frame_t f;
  sm_status_t st;

  if (g == 0) return SM_ERR_NULL_ARGUMENT;
  g->found = 0;
  g->askable = 0;
  if (g->n < 4u) return SM_OK;   /* too few looks to rule much out */

  st = build(g, 1, &sit);
  if (st != SM_OK) return st;
  st = fr_formulate(&sit, &f);
  if (st != SM_OK || !f.found) return SM_OK;
  strncpy(g->law_r, f.law, FR_TEXT - 1u);
  g->support_r = f.support;
  g->shape_r = f.shape;
  if (fr_to_theory(&sit, &f, &g->th_r) != SM_OK) return SM_OK;

  st = build(g, 0, &sit);
  if (st != SM_OK) return st;
  st = fr_formulate(&sit, &f);
  if (st != SM_OK || !f.found) return SM_OK;
  strncpy(g->law_c, f.law, FR_TEXT - 1u);
  g->support_c = f.support;
  g->shape_c = f.shape;
  if (fr_to_theory(&sit, &f, &g->th_c) != SM_OK) return SM_OK;

  g->found = 1;
  g->askable = 1;
  return SM_OK;
}

/* ask one law: with when and where known, what must the answer be? */
static int ask(sx_theory_t *th, double when, double row, double col, double *out) {
  sx_box_t box;
  if (th->n_vars < 4u) return 0;
  box = sx_start(th);
  box.v[0] = iv_point(when);
  box.v[1] = iv_point(row);
  box.v[2] = iv_point(col);
  if (!sx_contract(th, &box)) return 0;   /* no world: the law does not allow this */
  if (box.v[3].hi - box.v[3].lo > 0.5) return 0;
  *out = (box.v[3].lo + box.v[3].hi) / 2.0;
  return 1;
}

int gs_predict(gs_guess_t *g, double when, double row, double col, double *next_row,
               double *next_col) {
  if (g == 0 || next_row == 0 || next_col == 0 || !g->askable) return 0;
  if (!ask(&g->th_r, when, row, col, next_row)) return 0;
  if (!ask(&g->th_c, when, row, col, next_col)) return 0;
  return 1;
}

int gs_same_kind(const gs_guess_t *a, const gs_guess_t *b) {
  if (a == 0 || b == 0 || !a->found || !b->found) return 0;
  return a->shape_r == b->shape_r && a->shape_c == b->shape_c;
}
