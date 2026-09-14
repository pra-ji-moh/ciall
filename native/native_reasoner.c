/*
 * native_reasoner.c -- see native_reasoner.h.
 */

#include "native_reasoner.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define BIT(i) ((uint64_t)1 << (i))

/* ---- the World ------------------------------------------------------ */

void nr_world_init(nr_world_t *w, uint64_t known) {
  unsigned i;
  w->known = known;
  w->n_edges = 0u;
  for (i = 0u; i < NR_MAX_ENTITIES; i++) {
    w->adj[i] = 0u;
    w->reach[i] = 0u;
  }
}

int nr_world_add_edge(nr_world_t *w, unsigned a, unsigned b) {
  if (w->adj[a] & BIT(b)) return 0;
  w->adj[a] |= BIT(b);
  w->n_edges++;
  return 1;
}

void nr_world_close(nr_world_t *w) {
  /* Repeated relaxation, as reasoner.py: no parameters, nothing learned,
     the same facts always give the same closure. */
  unsigned a, b;
  int changed = 1;
  for (a = 0u; a < NR_MAX_ENTITIES; a++) w->reach[a] = w->adj[a];
  while (changed) {
    changed = 0;
    for (a = 0u; a < NR_MAX_ENTITIES; a++) {
      uint64_t extra = 0u;
      if (!(w->known & BIT(a))) continue;
      for (b = 0u; b < NR_MAX_ENTITIES; b++) {
        if (w->reach[a] & BIT(b)) extra |= w->reach[b];
      }
      if ((extra & ~w->reach[a]) != 0u) {
        w->reach[a] |= extra;
        changed = 1;
      }
    }
  }
}

int nr_known(const nr_world_t *w, unsigned e) {
  return e < NR_MAX_ENTITIES && (w->known & BIT(e)) != 0u;
}

unsigned nr_count_known(const nr_world_t *w) {
  unsigned c = 0u, i;
  for (i = 0u; i < NR_MAX_ENTITIES; i++) c += (w->known >> i) & 1u;
  return c;
}

unsigned nr_path(const nr_world_t *w, unsigned a, unsigned b,
                 unsigned *from, unsigned *to) {
  int prev[NR_MAX_ENTITIES];
  unsigned frontier[NR_MAX_ENTITIES], nxt[NR_MAX_ENTITIES];
  unsigned nf = 1u, i, v;
  for (i = 0u; i < NR_MAX_ENTITIES; i++) prev[i] = -1;
  prev[a] = (int)a;
  frontier[0] = a;
  while (nf > 0u) {
    unsigned nn = 0u;
    for (i = 0u; i < nf; i++) {
      unsigned u = frontier[i];
      for (v = 0u; v < NR_MAX_ENTITIES; v++) {
        if (!(w->adj[u] & BIT(v)) || prev[v] != -1) continue;
        prev[v] = (int)u;
        if (v == b) {
          unsigned steps = 0u, cur = b, k;
          unsigned rf[NR_MAX_PATH], rt[NR_MAX_PATH];
          while (cur != a) {
            rf[steps] = (unsigned)prev[cur];
            rt[steps] = cur;
            steps++;
            cur = (unsigned)prev[cur];
          }
          for (k = 0u; k < steps; k++) {
            from[k] = rf[steps - 1u - k];
            to[k] = rt[steps - 1u - k];
          }
          return steps;
        }
        nxt[nn++] = v;
      }
    }
    for (i = 0u; i < nn; i++) frontier[i] = nxt[i];
    nf = nn;
  }
  return 0u;
}

nr_answer_t nr_ask(const nr_world_t *w, unsigned a, unsigned b) {
  if (!nr_known(w, a) || !nr_known(w, b)) return NR_GROUNDLESS_Q;
  if (w->reach[a] & BIT(b)) return NR_TRUE;
  if (w->reach[b] & BIT(a)) return NR_FALSE;
  return NR_UNDET;
}

void nr_build(nr_world_t *w, unsigned n_entities, unsigned n_edges, uint64_t seed,
              unsigned *unknown, unsigned *n_unknown) {
  pyrand_t rng;
  int order[NR_MAX_ENTITIES], rank[NR_MAX_ENTITIES];
  uint64_t known = 0u;
  unsigned i;
  pyrand_seed(&rng, seed);
  for (i = 0u; i < n_entities; i++) {
    known |= BIT(i);
    order[i] = (int)i;   /* list(set(range(n))) iterates 0..n-1 for small ints */
  }
  pyrand_shuffle(&rng, order, n_entities);
  for (i = 0u; i < n_entities; i++) rank[order[i]] = (int)i;
  nr_world_init(w, known);
  while (w->n_edges < n_edges) {
    int pick[2];
    pyrand_sample(&rng, order, n_entities, 2u, pick);
    if (rank[pick[0]] < rank[pick[1]]) nr_world_add_edge(w, (unsigned)pick[0], (unsigned)pick[1]);
  }
  nr_world_close(w);
  for (i = 0u; i < 6u; i++) unknown[i] = n_entities + i;
  *n_unknown = 6u;
}

/* ---- node results --------------------------------------------------- */

static void blank(nr_result_t *r) {
  memset(r, 0, sizeof *r);
}

double nr_ancestry_H(const nr_result_t *r) {
  double t = 0.0;
  unsigned i;
  for (i = 0u; i < r->n_anc; i++) t += r->anc_H[i];
  return t;
}

void nr_node_query(const nr_world_t *w, unsigned a, unsigned b, nr_result_t *out) {
  int ab, ba;
  unsigned nD;
  blank(out);
  out->is_bool = 1;
  if (!nr_known(w, a) || !nr_known(w, b)) {
    out->verdict = NR_GROUNDLESS;
    out->H = 0.0;
    return;
  }
  out->has_dom = 1;
  out->dom = 2u;
  ab = (w->reach[a] & BIT(b)) != 0u;
  ba = (w->reach[b] & BIT(a)) != 0u;
  if (ab && ba) {
    out->verdict = NR_CONTRADICTION;
    out->has_S = 1;
    out->S = 1.0;
    out->H = INFINITY;
    return;
  }
  if (ab) out->D = BIT(1);
  else if (ba) out->D = BIT(0);
  else out->D = BIT(0) | BIT(1);
  nD = (out->D == 3u) ? 2u : 1u;
  out->has_S = 1;
  out->S = 1.0 - (double)nD / 2.0;
  out->H = nD <= 1u ? 0.0 : log2((double)nD);
  if (nD == 1u) {
    out->verdict = NR_DERIVED;
    out->has_value = 1;
    out->value = ab ? 1 : 0;
    out->witness_len = ab ? nr_path(w, a, b, out->wfrom, out->wto)
                          : nr_path(w, b, a, out->wfrom, out->wto);
    return;
  }
  out->verdict = NR_UNDETERMINED;
}

/* the values of D in the order Python's sorted(D, key=str) gives */
static unsigned sorted_by_str(const nr_result_t *r, int *vals) {
  unsigned n = 0u, i, j;
  char si[24], sj[24];
  for (i = 0u; i < 64u; i++) if (r->D & BIT(i)) vals[n++] = (int)i;
  if (r->is_bool) return n;   /* 'False' < 'True', which is bit order */
  for (i = 1u; i < n; i++) {
    for (j = i; j > 0u; j--) {
      sprintf(si, "%d", vals[j - 1u]);
      sprintf(sj, "%d", vals[j]);
      if (strcmp(si, sj) > 0) { int t = vals[j]; vals[j] = vals[j - 1u]; vals[j - 1u] = t; }
      else break;
    }
  }
  return n;
}

void nr_speculate(const nr_result_t *in, double tau, pyrand_t *rng, long guess_id,
                  nr_result_t *out) {
  int vals[64];
  unsigned n;
  *out = *in;
  if (in->verdict != NR_UNDETERMINED) return;
  if (!in->has_S || in->S < tau) return;   /* not grounded enough; stay honest */
  n = sorted_by_str(in, vals);
  out->verdict = NR_SPECULATED;
  out->has_value = 1;
  out->value = vals[pyrand_randbelow(rng, n)];
  out->witness_len = 0u;
  out->n_anc = 1u;
  out->anc_id[0] = guess_id;
  out->anc_H[0] = in->H;
}

/* ---- rank domain ---------------------------------------------------- */

void nr_rank_query(const nr_world_t *w, unsigned e, nr_result_t *out) {
  unsigned k, pred = 0u, succ = 0u, x, n;
  int lo, hi;
  blank(out);
  out->is_bool = 0;
  if (!nr_known(w, e)) {
    out->verdict = NR_GROUNDLESS;
    return;
  }
  k = nr_count_known(w);
  for (x = 0u; x < NR_MAX_ENTITIES; x++) {
    if (x != e && nr_known(w, x) && (w->reach[x] & BIT(e))) pred++;
  }
  for (x = 0u; x < NR_MAX_ENTITIES; x++) {
    if ((w->reach[e] & w->known) & BIT(x)) succ++;
  }
  out->has_dom = 1;
  out->dom = k;
  lo = (int)pred;
  hi = (int)k - 1 - (int)succ;
  if (lo > hi) {
    out->verdict = NR_CONTRADICTION;
    out->has_S = 1;
    out->S = 1.0;
    out->H = INFINITY;
    return;
  }
  for (x = (unsigned)lo; x <= (unsigned)hi; x++) out->D |= BIT(x);
  n = (unsigned)(hi - lo + 1);
  out->has_S = 1;
  out->S = 1.0 - (double)n / (double)k;
  out->H = n <= 1u ? 0.0 : log2((double)n);
  if (n == 1u) {
    out->verdict = NR_DERIVED;
    out->has_value = 1;
    out->value = lo;
    return;
  }
  out->verdict = NR_UNDETERMINED;
}

static int next_perm(unsigned *a, unsigned n) {
  int i = (int)n - 2, j;
  unsigned t;
  while (i >= 0 && a[i] >= a[i + 1]) i--;
  if (i < 0) return 0;
  j = (int)n - 1;
  while (a[j] <= a[i]) j--;
  t = a[i]; a[i] = a[j]; a[j] = t;
  for (j = i + 1, i = (int)n - 1; j < i; j++, i--) { t = a[i]; a[i] = a[j]; a[j] = t; }
  return 1;
}

uint64_t nr_brute_force_positions(const nr_world_t *w, unsigned e) {
  unsigned ents[16], n = 0u, i, a, b;
  uint64_t achieved = 0u;
  for (i = 0u; i < NR_MAX_ENTITIES && n < 16u; i++) if (nr_known(w, i)) ents[n++] = i;
  do {
    int pos[NR_MAX_ENTITIES];
    int ok = 1;
    for (i = 0u; i < NR_MAX_ENTITIES; i++) pos[i] = -1;
    for (i = 0u; i < n; i++) pos[ents[i]] = (int)i;
    for (a = 0u; a < NR_MAX_ENTITIES && ok; a++) {
      for (b = 0u; b < NR_MAX_ENTITIES; b++) {
        if ((w->adj[a] & BIT(b)) && pos[a] >= 0 && pos[b] >= 0 && pos[a] >= pos[b]) { ok = 0; break; }
      }
    }
    if (ok) achieved |= BIT((unsigned)pos[e]);
  } while (next_perm(ents, n));
  return achieved;
}

/* ---- composition (superseded) --------------------------------------- */

static int definitely_false(const nr_result_t *r) {
  if (r->has_value) return r->value == 0;
  return r->verdict != NR_GROUNDLESS && r->D == BIT(0);
}

static uint64_t values_of(const nr_result_t *r) {
  return r->has_value ? BIT((unsigned)r->value) : r->D;
}

static void union_ancestry(const nr_result_t *a, const nr_result_t *b, nr_result_t *out) {
  unsigned i, j;
  out->n_anc = 0u;
  for (i = 0u; i < a->n_anc; i++) {
    out->anc_id[out->n_anc] = a->anc_id[i];
    out->anc_H[out->n_anc] = a->anc_H[i];
    out->n_anc++;
  }
  for (i = 0u; i < b->n_anc; i++) {
    int dup = 0;
    for (j = 0u; j < out->n_anc; j++) {
      if (out->anc_id[j] == b->anc_id[i] && out->anc_H[j] == b->anc_H[i]) { dup = 1; break; }
    }
    if (!dup && out->n_anc < NR_MAX_ANCESTRY) {
      out->anc_id[out->n_anc] = b->anc_id[i];
      out->anc_H[out->n_anc] = b->anc_H[i];
      out->n_anc++;
    }
  }
}

void nr_compose_and(const nr_result_t *r1, const nr_result_t *r2, nr_result_t *out) {
  nr_result_t anc;
  uint64_t D1, D2, D = 0u;
  int true_ok, false_ok;
  unsigned nD;
  blank(&anc);
  union_ancestry(r1, r2, &anc);   /* computed before any branch returns */
  blank(out);
  out->is_bool = 1;
  out->n_anc = anc.n_anc;
  memcpy(out->anc_id, anc.anc_id, sizeof anc.anc_id);
  memcpy(out->anc_H, anc.anc_H, sizeof anc.anc_H);

  if (r1->verdict == NR_CONTRADICTION || r2->verdict == NR_CONTRADICTION) {
    out->verdict = NR_CONTRADICTION;
    out->has_dom = 1; out->dom = 2u;
    out->has_S = 1; out->S = 1.0; out->H = INFINITY;
    return;
  }
  if (definitely_false(r1) || definitely_false(r2)) {
    out->verdict = NR_DERIVED;
    out->has_value = 1; out->value = 0;
    out->D = BIT(0); out->has_dom = 1; out->dom = 2u;
    out->has_S = 1; out->S = 0.5; out->H = 0.0;
    return;
  }
  if (r1->verdict == NR_GROUNDLESS || r2->verdict == NR_GROUNDLESS) {
    out->verdict = NR_GROUNDLESS;
    return;
  }
  D1 = values_of(r1);
  D2 = values_of(r2);
  true_ok = (D1 & BIT(1)) && (D2 & BIT(1));
  false_ok = (D1 & BIT(0)) || (D2 & BIT(0));
  if (true_ok) D |= BIT(1);
  if (false_ok) D |= BIT(0);
  out->has_dom = 1; out->dom = 2u;
  if (D == 0u) {
    out->verdict = NR_CONTRADICTION;
    out->has_S = 1; out->S = 1.0; out->H = INFINITY;
    return;
  }
  nD = (D == 3u) ? 2u : 1u;
  out->D = D;
  out->has_S = 1;
  out->S = 1.0 - (double)nD / 2.0;
  out->H = nD <= 1u ? 0.0 : 1.0;
  if (nD == 1u) {
    out->verdict = NR_DERIVED;
    out->has_value = 1;
    out->value = (D & BIT(1)) ? 1 : 0;
    return;
  }
  out->verdict = NR_UNDETERMINED;
}
