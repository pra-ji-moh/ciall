/*
 * smarsh_analogy.c -- see smarsh_analogy.h.
 *
 * No recursion: factor pairings and value relabelings are enumerated by
 * counting (the factorial number system), so the search is plain loops.
 * The search is exponential in the number of factors, which is why it
 * runs only after the invariant filter, and why it has a stated cap.
 */

#include "smarsh_analogy.h"

#include <string.h>

/* Past this many candidate renamings the search stops and says so,
   rather than claiming there is no match. Factor counts in the demo stay
   far below it. */
#define SA_MAX_CANDIDATES 20000000UL

unsigned sa_entries(const sa_struct_t *s) {
  unsigned i, n = 1u;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i < s->n_factors) n *= s->dom[i];
  }
  return n;
}

static unsigned index_of(const sa_struct_t *s, const unsigned *v) {
  unsigned i, idx = 0u;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i < s->n_factors) idx = idx * s->dom[i] + v[i];
  }
  return idx;
}

static void values_of(const sa_struct_t *s, unsigned idx, unsigned *v) {
  unsigned i;
  for (i = SA_MAX_FACTORS; i-- > 0u;) {
    if (i < s->n_factors) {
      v[i] = idx % s->dom[i];
      idx /= s->dom[i];
    }
  }
}

unsigned sa_at(const sa_struct_t *s, const unsigned *values) {
  return s->table[index_of(s, values)];
}

sm_status_t sa_define(sa_struct_t *s, const char *name, const char *domain,
                      unsigned n_factors, const char *const *factor_names,
                      const unsigned *doms, unsigned dom_out,
                      unsigned (*answer)(const unsigned *values)) {
  unsigned i, e, n;
  unsigned v[SA_MAX_FACTORS];
  if (s == 0 || name == 0 || domain == 0 || factor_names == 0 || doms == 0 || answer == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_factors == 0u || n_factors > SA_MAX_FACTORS || dom_out < 2u || dom_out > SA_MAX_VALUES) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  memset(s, 0, sizeof *s);
  strncpy(s->name, name, SA_NAME - 1u);
  strncpy(s->domain, domain, SA_NAME - 1u);
  s->n_factors = n_factors;
  s->dom_out = dom_out;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i >= n_factors) break;
    if (doms[i] < 2u || doms[i] > SA_MAX_VALUES) return SM_ERR_DOMAIN_TOO_LARGE;
    s->dom[i] = doms[i];
    strncpy(s->factor[i], factor_names[i], SA_NAME - 1u);
  }
  n = sa_entries(s);
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    unsigned a;
    if (e >= n) break;
    values_of(s, e, v);
    a = answer(v);
    if (a >= dom_out) return SM_ERR_INDEX_OUT_OF_DOMAIN;
    s->table[e] = (uint8_t)a;
  }
  return SM_OK;
}

sm_status_t sa_add(sa_library_t *lib, const sa_struct_t *s) {
  if (lib == 0 || s == 0) return SM_ERR_NULL_ARGUMENT;
  if (lib->n >= SA_MAX_LIBRARY) return SM_ERR_DOMAIN_TOO_LARGE;
  lib->s[lib->n++] = *s;
  return SM_OK;
}

/* ---- invariants ------------------------------------------------------ */

/* How many (combination, other value) pairs change the answer when this
   factor alone changes. Zero means the factor never matters. */
static unsigned influence(const sa_struct_t *s, unsigned f) {
  unsigned e, w, n = sa_entries(s), count = 0u;
  unsigned v[SA_MAX_FACTORS];
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    if (e >= n) break;
    values_of(s, e, v);
    for (w = 0u; w < SA_MAX_VALUES; w++) {
      unsigned keep = v[f];
      if (w >= s->dom[f] || w == keep) continue;
      v[f] = w;
      if (sa_at(s, v) != s->table[e]) count++;
      v[f] = keep;
    }
  }
  return count;
}

static void sort_u(unsigned *x, unsigned n) {
  unsigned i, j;
  for (i = 1u; i < n; i++) {
    for (j = i; j > 0u && x[j - 1u] > x[j]; j--) {
      unsigned t = x[j]; x[j] = x[j - 1u]; x[j - 1u] = t;
    }
  }
}

static void invariants(const sa_struct_t *s, unsigned *out, unsigned *n_out) {
  unsigned hist[SA_MAX_VALUES] = {0u, 0u, 0u, 0u};
  unsigned prof[SA_MAX_FACTORS];
  unsigned e, i, n = sa_entries(s), k = 0u;
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    if (e < n) hist[s->table[e]]++;
  }
  sort_u(hist, SA_MAX_VALUES);
  out[k++] = s->n_factors;
  out[k++] = s->dom_out;
  for (i = 0u; i < SA_MAX_VALUES; i++) out[k++] = hist[i];
  /* each factor as one number: its size and its influence, so the
     pairing of the two survives sorting */
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    prof[i] = i < s->n_factors ? s->dom[i] * 1000000u + influence(s, i) : 0u;
  }
  sort_u(prof, SA_MAX_FACTORS);
  for (i = 0u; i < SA_MAX_FACTORS; i++) out[k++] = prof[i];
  *n_out = k;
}

int sa_same_invariants(const sa_struct_t *a, const sa_struct_t *b) {
  unsigned ia[16], ib[16], na, nb, i;
  if (a == 0 || b == 0) return 0;
  invariants(a, ia, &na);
  invariants(b, ib, &nb);
  if (na != nb) return 0;
  for (i = 0u; i < na; i++) if (ia[i] != ib[i]) return 0;
  return 1;
}

/* ---- the exact search ------------------------------------------------ */

static unsigned factorial(unsigned n) {
  unsigned r = 1u, i;
  for (i = 2u; i <= SA_MAX_VALUES + 1u && i <= n; i++) r *= i;
  return r;
}

/* The k-th permutation of 0..n-1 in lexicographic order (n <= 5). */
static void nth_perm(unsigned n, unsigned k, unsigned *out) {
  unsigned pool[SA_MAX_FACTORS], i, j, m = n;
  for (i = 0u; i < SA_MAX_FACTORS; i++) pool[i] = i;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    unsigned f, pick;
    if (i >= n) break;
    f = factorial(m - 1u);
    pick = k / f;
    k %= f;
    out[i] = pool[pick];
    for (j = pick; j + 1u < m; j++) pool[j] = pool[j + 1u];
    m--;
  }
}

int sa_check_map(const sa_struct_t *a, const sa_struct_t *b, const sa_map_t *map) {
  unsigned e, i, n = sa_entries(a);
  unsigned va[SA_MAX_FACTORS], vb[SA_MAX_FACTORS];
  if (a == 0 || b == 0 || map == 0 || a->n_factors != b->n_factors || a->dom_out != b->dom_out) {
    return 0;
  }
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    if (e >= n) break;
    values_of(a, e, va);
    for (i = 0u; i < SA_MAX_FACTORS; i++) {
      if (i < a->n_factors) vb[map->factor_to[i]] = map->value_to[i][va[i]];
    }
    if (sa_at(b, vb) != map->out_to[a->table[e]]) return 0;
  }
  return 1;
}

/* Try one factor pairing and one choice of value relabelings; the answer
   relabeling is then forced, built as it goes, and must stay one-to-one. */
static int try_map(const sa_struct_t *a, const sa_struct_t *b, sa_map_t *m) {
  unsigned e, i, n = sa_entries(a);
  unsigned va[SA_MAX_FACTORS], vb[SA_MAX_FACTORS];
  int used[SA_MAX_VALUES] = {0, 0, 0, 0};
  for (i = 0u; i < SA_MAX_VALUES; i++) m->out_to[i] = 0xFFu;
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    unsigned ra, rb;
    if (e >= n) break;
    values_of(a, e, va);
    for (i = 0u; i < SA_MAX_FACTORS; i++) {
      if (i < a->n_factors) vb[m->factor_to[i]] = m->value_to[i][va[i]];
    }
    ra = a->table[e];
    rb = sa_at(b, vb);
    if (m->out_to[ra] == 0xFFu) {
      if (used[rb]) return 0;
      m->out_to[ra] = (uint8_t)rb;
      used[rb] = 1;
    } else if (m->out_to[ra] != rb) {
      return 0;
    }
  }
  /* answers a never gives still need somewhere to go, one-to-one */
  for (i = 0u; i < SA_MAX_VALUES; i++) {
    unsigned t;
    if (i >= a->dom_out || m->out_to[i] != 0xFFu) continue;
    for (t = 0u; t < b->dom_out && used[t]; t++) {}
    m->out_to[i] = (uint8_t)t;
    used[t] = 1;
  }
  return 1;
}

int sa_match(const sa_struct_t *a, const sa_struct_t *b, sa_map_t *map) {
  unsigned k = a->n_factors, pk, fp;
  unsigned long tried = 0ul;
  sa_map_t m;
  if (a == 0 || b == 0 || map == 0 || a->n_factors != b->n_factors || a->dom_out != b->dom_out) {
    return 0;
  }
  memset(&m, 0, sizeof m);
  pk = factorial(k);
  for (fp = 0u; fp < pk; fp++) {
    unsigned i, combos = 1u, c;
    int dims_ok = 1;
    nth_perm(k, fp, m.factor_to);
    for (i = 0u; i < k; i++) {
      if (a->dom[i] != b->dom[m.factor_to[i]]) dims_ok = 0;
      combos *= factorial(a->dom[i]);
    }
    if (!dims_ok) continue;
    /* every choice of value relabeling, counted in mixed radix */
    for (c = 0u; c < combos; c++) {
      unsigned rest = c, p[SA_MAX_VALUES];
      for (i = 0u; i < k; i++) {
        unsigned f = factorial(a->dom[i]), j;
        nth_perm(a->dom[i], rest % f, p);
        rest /= f;
        for (j = 0u; j < a->dom[i]; j++) m.value_to[i][j] = (uint8_t)p[j];
      }
      if (++tried > SA_MAX_CANDIDATES) return -1;
      if (try_map(a, b, &m)) {
        *map = m;
        return 1;
      }
    }
  }
  return 0;
}

sm_status_t sa_find(const sa_library_t *lib, const sa_struct_t *q, sa_found_t *out) {
  unsigned i;
  if (lib == 0 || q == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  memset(out, 0, sizeof *out);
  for (i = 0u; i < SA_MAX_LIBRARY; i++) {
    int r;
    if (i >= lib->n) break;
    if (!sa_same_invariants(q, &lib->s[i])) {
      out->filtered++;
      continue;
    }
    r = sa_match(q, &lib->s[i], &out->map[out->n_matches]);
    if (r == 1) {
      out->match[out->n_matches++] = i;
    } else if (r == 0) {
      out->searched_no_match++;
    } else {
      return SM_ERR_DOMAIN_TOO_LARGE;   /* the search cap: undecided, said so */
    }
  }
  return SM_OK;
}

/* ---- properties and transfer ----------------------------------------- */

int sa_holds(const sa_struct_t *s, const sa_prop_t *p) {
  unsigned e, n = sa_entries(s);
  unsigned v[SA_MAX_FACTORS];
  if (s == 0 || p == 0 || p->factor >= s->n_factors) return 0;
  if (p->kind == SA_IRRELEVANT) return influence(s, p->factor) == 0u;
  if (p->kind == SA_FORCES) {
    unsigned seen = 0u;
    if (p->value >= s->dom[p->factor]) return 0;
    for (e = 0u; e < SA_MAX_ENTRIES; e++) {
      if (e >= n) break;
      values_of(s, e, v);
      if (v[p->factor] != p->value) continue;
      seen++;
      if (s->table[e] != p->answer) return 0;
    }
    return seen > 0u;
  }
  if (p->other >= s->n_factors || p->other == p->factor || s->dom[p->other] != s->dom[p->factor]) {
    return 0;
  }
  for (e = 0u; e < SA_MAX_ENTRIES; e++) {
    unsigned t;
    if (e >= n) break;
    values_of(s, e, v);
    t = v[p->factor];
    v[p->factor] = v[p->other];
    v[p->other] = t;
    if (sa_at(s, v) != s->table[e]) return 0;
  }
  return 1;
}

unsigned sa_properties(const sa_struct_t *s, sa_prop_t *props, unsigned max) {
  unsigned f, g, v, a, n = 0u;
  if (s == 0 || props == 0) return 0u;
  for (f = 0u; f < SA_MAX_FACTORS; f++) {
    sa_prop_t p;
    if (f >= s->n_factors) break;
    memset(&p, 0, sizeof p);
    p.factor = f;
    p.kind = SA_IRRELEVANT;
    if (sa_holds(s, &p) && n < max) props[n++] = p;
    p.kind = SA_FORCES;
    for (v = 0u; v < SA_MAX_VALUES; v++) {
      if (v >= s->dom[f]) break;
      p.value = v;
      for (a = 0u; a < SA_MAX_VALUES; a++) {
        if (a >= s->dom_out) break;
        p.answer = a;
        if (sa_holds(s, &p) && n < max) props[n++] = p;
      }
    }
    p.kind = SA_INTERCHANGEABLE;
    for (g = f + 1u; g < SA_MAX_FACTORS; g++) {
      if (g >= s->n_factors) break;
      p.other = g;
      if (sa_holds(s, &p) && n < max) props[n++] = p;
    }
  }
  return n;
}

int sa_transfer(const sa_map_t *m, const sa_struct_t *a, const sa_struct_t *b,
                const sa_prop_t *on_b, sa_prop_t *on_a) {
  unsigned i, v, found_f = SA_MAX_FACTORS, found_o = SA_MAX_FACTORS;
  if (m == 0 || a == 0 || b == 0 || on_b == 0 || on_a == 0) return 0;
  *on_a = *on_b;
  for (i = 0u; i < SA_MAX_FACTORS; i++) {
    if (i >= a->n_factors) break;
    if (m->factor_to[i] == on_b->factor) found_f = i;
    if (on_b->kind == SA_INTERCHANGEABLE && m->factor_to[i] == on_b->other) found_o = i;
  }
  if (found_f == SA_MAX_FACTORS) return 0;
  on_a->factor = found_f;
  if (on_b->kind == SA_INTERCHANGEABLE) {
    if (found_o == SA_MAX_FACTORS) return 0;
    on_a->other = found_o;
  }
  if (on_b->kind == SA_FORCES) {
    int got_v = 0, got_a = 0;
    for (v = 0u; v < SA_MAX_VALUES; v++) {
      if (v < a->dom[found_f] && m->value_to[found_f][v] == on_b->value) { on_a->value = v; got_v = 1; }
      if (v < a->dom_out && m->out_to[v] == on_b->answer) { on_a->answer = v; got_a = 1; }
    }
    if (!got_v || !got_a) return 0;
  }
  return 1;
}
