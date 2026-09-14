/*
 * smarsh_informant.c -- see smarsh_informant.h.
 */

#include "smarsh_informant.h"

#include <string.h>

static unsigned row_of(const sa_struct_t *s, const unsigned *v) {
  unsigned i, idx = 0u;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i < s->n_factors) idx = idx * s->dom[i] + v[i];
  }
  return idx;
}

static int in_range(const sa_struct_t *s, const unsigned *v) {
  unsigned i;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i < s->n_factors && v[i] >= s->dom[i]) return 0;
  }
  return 1;
}

static int same_question(const sa_struct_t *a, const sa_struct_t *b) {
  unsigned i;
  if (strcmp(a->name, b->name) != 0 || a->n_factors != b->n_factors || a->dom_out != b->dom_out) {
    return 0;
  }
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i < a->n_factors && a->dom[i] != b->dom[i]) return 0;
  }
  return 1;
}

void sk_init(sk_store_t *k) {
  if (k != 0) memset(k, 0, sizeof *k);
}

sm_status_t sk_source(sk_store_t *k, const char *name, unsigned *id) {
  if (k == 0 || name == 0 || id == 0) return SM_ERR_NULL_ARGUMENT;
  if (k->n_sources >= SK_MAX_SOURCES) return SM_ERR_DOMAIN_TOO_LARGE;
  strncpy(k->source_name[k->n_sources], name, SA_NAME - 1u);
  *id = k->n_sources++;
  return SM_OK;
}

sm_status_t sk_tell(sk_store_t *k, unsigned source, const sa_struct_t *claim) {
  unsigned i;
  if (k == 0 || claim == 0) return SM_ERR_NULL_ARGUMENT;
  if (source >= k->n_sources) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  if (k->n_claims >= SK_MAX_CLAIMS) return SM_ERR_DOMAIN_TOO_LARGE;
  /* a question must mean the same thing to everyone who talks about it */
  for (i = 0u; i < SK_MAX_CLAIMS; i++) {
    if (i >= k->n_claims) break;
    if (strcmp(k->claims[i].claim.name, claim->name) == 0 &&
        !same_question(&k->claims[i].claim, claim)) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
  }
  k->claims[k->n_claims].source = source;
  k->claims[k->n_claims].claim = *claim;
  /* rows already seen for this question apply to the new claim at once */
  for (i = 0u; i < SK_MAX_CLAIMS; i++) {
    unsigned e;
    if (i >= k->n_claims) break;
    if (strcmp(k->claims[i].claim.name, claim->name) != 0) continue;
    for (e = 0u; e < SA_MAX_ENTRIES; e++) {
      if (k->seen[i][e] != 0u) {
        k->seen[k->n_claims][e] = k->seen[i][e];
        if (claim->table[e] + 1u != k->seen[i][e] && !k->distrusted[source]) {
          k->distrusted[source] = 1;
          k->caught_on[source] = k->n_claims;
        }
      }
    }
    break;
  }
  k->n_claims++;
  return SM_OK;
}

sm_status_t sk_observe(sk_store_t *k, const char *name, const unsigned *values,
                       unsigned answer, unsigned *caught) {
  unsigned i, found = 0u;
  if (k == 0 || name == 0 || values == 0 || caught == 0) return SM_ERR_NULL_ARGUMENT;
  *caught = 0u;
  for (i = 0u; i < SK_MAX_CLAIMS; i++) {
    const sa_struct_t *c;
    unsigned r;
    if (i >= k->n_claims) break;
    c = &k->claims[i].claim;
    if (strcmp(c->name, name) != 0) continue;
    if (!in_range(c, values) || answer >= c->dom_out) return SM_ERR_INDEX_OUT_OF_DOMAIN;
    found++;
    r = row_of(c, values);
    k->seen[i][r] = (uint8_t)(answer + 1u);
    if (c->table[r] != answer && !k->distrusted[k->claims[i].source]) {
      k->distrusted[k->claims[i].source] = 1;
      k->caught_on[k->claims[i].source] = i;
      (*caught)++;
    }
  }
  /* seeing something nobody has claimed anything about is recorded
     nowhere: there is no question to file it under yet */
  return found > 0u ? SM_OK : SM_ERR_EMPTY_DOMAIN;
}

sk_standing_t sk_row(const sk_store_t *k, const char *name, const unsigned *values,
                     unsigned *answer, uint32_t *sources) {
  unsigned i, told = 0u, told_answer = 0u;
  int any = 0, conflict = 0;
  if (k == 0 || name == 0 || values == 0 || answer == 0 || sources == 0) {
    return SK_UNKNOWN;   /* nothing to stand on; the standing says so */
  }
  *answer = 0u;
  *sources = 0u;
  for (i = 0u; i < SK_MAX_CLAIMS; i++) {
    const sa_struct_t *c;
    unsigned r;
    if (i >= k->n_claims) break;
    c = &k->claims[i].claim;
    if (strcmp(c->name, name) != 0 || !in_range(c, values)) continue;
    r = row_of(c, values);
    any = 1;
    if (k->seen[i][r] != 0u) {
      *answer = k->seen[i][r] - 1u;
      *sources = 0u;
      return SK_CONFIRMED;
    }
    if (k->distrusted[k->claims[i].source]) continue;
    if (told == 0u) {
      told_answer = c->table[r];
    } else if (c->table[r] != told_answer) {
      conflict = 1;
    }
    told++;
    *sources |= (uint32_t)1 << k->claims[i].source;
  }
  if (!any || told == 0u) {
    *sources = 0u;
    return SK_UNKNOWN;
  }
  if (conflict) return SK_DISPUTED;
  *answer = told_answer;
  return SK_TOLD;
}

sm_status_t sk_best(const sk_store_t *k, const char *name, sa_struct_t *out,
                    uint32_t *sources, unsigned *n_open) {
  unsigned i, e, n, first = SK_MAX_CLAIMS;
  unsigned v[SA_MAX_FACTORS];
  if (k == 0 || name == 0 || out == 0 || sources == 0 || n_open == 0) return SM_ERR_NULL_ARGUMENT;
  *sources = 0u;
  *n_open = 0u;
  for (i = 0u; i < SK_MAX_CLAIMS; i++) {
    if (i < k->n_claims && strcmp(k->claims[i].claim.name, name) == 0) { first = i; break; }
  }
  if (first == SK_MAX_CLAIMS) return SM_ERR_EMPTY_DOMAIN;
  *out = k->claims[first].claim;
  n = sa_entries(out);
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    unsigned a, rest = e;
    uint32_t src;
    if (e >= n) break;
    for (i = SA_MAX_FACTORS; i-- > 0u;) {
      if (i < out->n_factors) { v[i] = rest % out->dom[i]; rest /= out->dom[i]; }
    }
    switch (sk_row(k, name, v, &a, &src)) {
      case SK_CONFIRMED:
      case SK_TOLD:
        out->table[e] = (uint8_t)a;
        *sources |= src;
        break;
      default:
        (*n_open)++;
    }
  }
  return *n_open == 0u ? SM_OK : SM_ERR_EMPTY_DOMAIN;
}
