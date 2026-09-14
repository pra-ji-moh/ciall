/*
 * smarsh_abstract.c -- see smarsh_abstract.h.
 */

#include "smarsh_abstract.h"

#include <string.h>

void ab_clear(ab_raw_t *raw, unsigned n_raw, unsigned dom_out) {
  if (raw == 0) return;
  memset(raw, 0, sizeof *raw);
  raw->n_raw = n_raw > AB_MAX_RAW ? AB_MAX_RAW : n_raw;
  raw->dom_out = dom_out;
}

sm_status_t ab_name(ab_raw_t *raw, unsigned attr, const char *name) {
  if (raw == 0 || name == 0) return SM_ERR_NULL_ARGUMENT;
  if (attr >= raw->n_raw) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  strncpy(raw->name[attr], name, AB_NAME - 1u);
  return SM_OK;
}

sm_status_t ab_observe(ab_raw_t *raw, unsigned pattern, unsigned outcome) {
  if (raw == 0) return SM_ERR_NULL_ARGUMENT;
  if (pattern >= (1u << raw->n_raw) || outcome >= raw->dom_out) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  if (raw->observed[pattern] && raw->outcome[pattern] != outcome) {
    /* the same raw situation with two outcomes: the raw attributes do not
       capture what decides it, and pretending otherwise would be false */
    return SM_ERR_INTERNAL_INVARIANT;
  }
  raw->observed[pattern] = 1u;
  raw->outcome[pattern] = (uint8_t)outcome;
  return SM_OK;
}

static unsigned swap_bits(unsigned s, unsigned i, unsigned j) {
  unsigned bi = (s >> i) & 1u, bj = (s >> j) & 1u;
  if (bi == bj) return s;
  return s ^ ((1u << i) | (1u << j));
}

static int dropped(ab_relevance_t r) {
  return r == AB_IRRELEVANT || r == AB_IRRELEVANT_CONJ;
}

static unsigned popcount8(unsigned x) {
  unsigned c = 0u;
  while (x != 0u) { c += x & 1u; x >>= 1; }
  return c;
}

sm_status_t ab_abstract(const ab_raw_t *raw, ab_result_t *out) {
  unsigned i, j, s, n, total, g;
  unsigned parent[AB_MAX_RAW];
  unsigned unproved = 0u;
  sr_partition_t p;
  sr_query_t proj;
  sm_status_t st;

  if (raw == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (raw->n_raw == 0u || raw->dom_out < 2u) return SM_ERR_EMPTY_DOMAIN;
  memset(out, 0, sizeof *out);
  n = raw->n_raw;
  total = 1u << n;

  /* 1. relevance, from pairs that differ in one attribute */
  for (i = 0u; i < n; i++) {
    int differs = 0, all_pairs = 1, some_pair = 0;
    for (s = 0u; s < AB_MAX_SIT; s++) {
      unsigned t;
      if (s >= total) break;
      t = s ^ (1u << i);
      if (!raw->observed[s]) continue;
      if (!raw->observed[t]) { all_pairs = 0; continue; }
      some_pair = 1;
      if (raw->outcome[s] != raw->outcome[t]) differs = 1;
    }
    if (differs) out->relevance[i] = AB_RELEVANT;
    else if (!some_pair) out->relevance[i] = AB_UNDETERMINED;
    else if (all_pairs) out->relevance[i] = AB_IRRELEVANT;
    else { out->relevance[i] = AB_IRRELEVANT_CONJ; out->leaps = 1; }
  }

  /* 2. symmetry: interchangeable pairs among the attributes kept */
  for (i = 0u; i < AB_MAX_RAW; i++) parent[i] = i;
  for (i = 0u; i < n; i++) {
    if (dropped(out->relevance[i])) continue;
    for (j = i + 1u; j < n; j++) {
      int ok = 1, seen_pair = 0, all_seen = 1;
      if (dropped(out->relevance[j])) continue;
      for (s = 0u; s < AB_MAX_SIT && ok; s++) {
        unsigned t;
        if (s >= total) break;
        if (!raw->observed[s]) continue;
        t = swap_bits(s, i, j);
        if (t == s) continue;
        if (!raw->observed[t]) { all_seen = 0; continue; }   /* a leap, if used */
        seen_pair = 1;
        if (raw->outcome[s] != raw->outcome[t]) ok = 0;
      }
      if (ok && seen_pair) {
        unsigned ri = i, rj = j;
        while (parent[ri] != ri) ri = parent[ri];
        while (parent[rj] != rj) rj = parent[rj];
        if (ri != rj) parent[rj] = ri;
        if (!all_seen) { unproved |= (1u << i) | (1u << j); out->leaps = 1; }
      }
    }
  }
  for (i = 0u; i < AB_MAX_RAW; i++) out->group_of[i] = AB_MAX_RAW;
  for (i = 0u; i < n; i++) {
    unsigned r = i;
    if (dropped(out->relevance[i])) continue;
    while (parent[r] != r) r = parent[r];
    if (out->group_of[r] == AB_MAX_RAW) out->group_of[r] = out->n_groups++;
    out->group_of[i] = out->group_of[r];
    g = out->group_of[i];
    out->group_size[g]++;
    out->group_members[g] |= (uint32_t)1 << i;
  }
  for (g = 0u; g < out->n_groups; g++) {
    if (out->group_size[g] > 1u) out->any_symmetry = 1;
    out->group_proved[g] = (out->group_members[g] & unproved) == 0u;
  }
  if (out->n_groups == 0u) return SM_ERR_EMPTY_DOMAIN;   /* nothing matters at all */

  /* 3. invent a question per group: how many of these are on */
  for (s = 0u; s < AB_MAX_SIT; s++) {
    if (s >= total) break;
    if (raw->observed[s]) out->situation[out->n_situations++] = s;
  }
  out->raw_worlds = out->n_situations;
  if (out->n_situations > SR_MAX_SITUATIONS) return SM_ERR_DOMAIN_TOO_LARGE;
  for (g = 0u; g < out->n_groups; g++) {
    st = sr_query_init(&out->question[g], out->n_situations, out->group_size[g] + 1u);
    if (st != SM_OK) return st;
    for (s = 0u; s < out->n_situations; s++) {
      st = sr_query_set(&out->question[g], s,
                        popcount8(out->situation[s] & out->group_members[g]));
      if (st != SM_OK) return st;
    }
  }
  st = sr_query_init(&out->outcome_q, out->n_situations, raw->dom_out);
  if (st != SM_OK) return st;
  for (s = 0u; s < out->n_situations; s++) {
    st = sr_query_set(&out->outcome_q, s, raw->outcome[out->situation[s]]);
    if (st != SM_OK) return st;
  }

  /* 4. the worlds it built, and whether they determine the outcome: the
     kernel's check, on the data, not the theorem's promise */
  st = sr_refine(out->question, out->n_groups, out->n_situations, &p);
  if (st != SM_OK) return st;
  out->built_worlds = p.n_cells;
  out->determined = sr_project(&p, &out->outcome_q, &proj) == SM_OK;
  return SM_OK;
}

ab_standing_t ab_predict(const ab_raw_t *raw, const ab_result_t *r, unsigned pattern,
                         unsigned *outcome) {
  unsigned s, g;
  if (raw == 0 || r == 0 || outcome == 0 || pattern >= (1u << raw->n_raw)) return AB_NO_GROUNDS;
  if (raw->observed[pattern]) {
    *outcome = raw->outcome[pattern];
    return AB_SEEN;
  }
  if (!r->determined) return AB_NO_GROUNDS;
  /* a seen situation in the same built world: every invented question
     gives the same answer on both */
  for (s = 0u; s < AB_MAX_SIT; s++) {
    int same = 1;
    if (s >= r->n_situations) break;
    for (g = 0u; g < AB_MAX_RAW; g++) {
      if (g >= r->n_groups) break;
      if (popcount8(pattern & r->group_members[g]) != popcount8(r->situation[s] & r->group_members[g])) {
        same = 0;
        break;
      }
    }
    if (same) {
      *outcome = raw->outcome[r->situation[s]];
      return AB_INFERRED;
    }
  }
  return AB_NO_GROUNDS;
}
