/*
 * test_cortex.c -- the cortex learns what it sees: the "what" stream.
 *
 * After birth its cortex's feedforward pathways learn (thalamus to layer 4,
 * layer 4 to layers 2/3, the visual cortex on to the temporal and parietal
 * lobes): within each area the most strongly driven tenth fire, and each that
 * fires moves its synapses towards what drove it. Each temporal neuron pools
 * widely over the visual cortex. (Foldiak's trace rule, learning from a
 * neuron's recent firing so that what stays while the eyes move is answered as
 * one thing, was tried: it looked decisive in one brain and did not hold up
 * over six, where it did worse than none. It is off, and still measured here.)
 *
 * Three brains (three seeds of the genome), each living three ways: a cortex
 * that does not learn; one that learns; one that learns with the trace rule.
 * In the last part of each life, its
 * visual cortex's and temporal lobe's patterns are compared between moments
 * well apart in time: when the eyes were on the same view, one eye-step away,
 * or far. Selectivity: how much more alike the same view is than a far one.
 * Invariance: how close one eye-step away comes to the same view.
 *
 * Checked:
 *   stable        learning makes the temporal lobe's pattern for a view repeat
 *   selective     learning makes both areas more selective for what is seen
 *   the stream    on average, invariance grows from the visual cortex to the
 *                 temporal lobe, as along the real ventral stream; without
 *                 learning it falls
 * (all averaged over the three brains, so no one lucky wiring decides)
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ob_crib.h"
#include "ob_one.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

#define LIFE 15000ul
#define KEPT 3000
#define MAXN 1300

static ob_brain_t GROWN;
static ob_one_t ONE;
static float SEEN[2][KEPT][MAXN];
static float GX[KEPT], GY[KEPT];

typedef struct {
  double same, step, far, selectivity, invariance;
} areal_t;

static double corr(const float *a, const float *b, unsigned n) {
  double ma = 0.0, mb = 0.0, x = 0.0, y = 0.0, xy = 0.0;
  unsigned k;
  for (k = 0u; k < n; k++) {
    ma += a[k];
    mb += b[k];
  }
  ma /= n;
  mb /= n;
  for (k = 0u; k < n; k++) {
    double p = a[k] - ma, q = b[k] - mb;
    x += p * p;
    y += q * q;
    xy += p * q;
  }
  return xy / sqrt(x * y + 1e-12);
}

/* a life of one twin; its visual cortex [0] and temporal lobe [1] measured */
static void live(unsigned seed, unsigned learns, unsigned trace, areal_t out[2]) {
  static const unsigned REG[2] = {OB_OCCIPITAL, OB_TEMPORAL};
  unsigned idx[2][MAXN], n[2] = {0u, 0u}, r, i;
  ob_crib_t c;
  float in[OL_INPUT];
  unsigned long t;
  int k, u;
  ob_genome_t g;
  ob_genome_default(&g);
  g.seed = seed;
  ob_grow(&GROWN, &g);
  ob_one_born(&ONE, &GROWN, 1u, OL_NONE);
  ONE.cortex_learns = learns;
  ONE.trace_rule = trace;
  for (r = 0u; r < 2u; r++)
    for (i = 0u; i < GROWN.n_neurons && n[r] < MAXN; i++)
      if (GROWN.neuron[i].region == REG[r]) idx[r][n[r]++] = i;
  ob_crib_make(&c, 1u);
  for (t = 0u; t < LIFE; t++) {
    long kept = (long)t - (long)(LIFE - KEPT);
    unsigned bits;
    ob_crib_sense(&c, in);
    bits = ob_one_live(&ONE, in);
    if (kept >= 0) {
      for (r = 0u; r < 2u; r++)
        for (i = 0u; i < n[r]; i++) SEEN[r][kept][i] = ONE.act[idx[r][i]];
      GX[kept] = c.gx;
      GY[kept] = c.gy;
    }
    ob_crib_act(&c, bits);
  }
  for (r = 0u; r < 2u; r++) {
    double sm = 0.0, st = 0.0, fa = 0.0;
    unsigned ns = 0u, nt = 0u, nf = 0u;
    for (k = 200; k < KEPT; k += 2) {
      for (u = k - 200; u < k - 4; u += 3) {
        float d = fabsf(GX[k] - GX[u]) + fabsf(GY[k] - GY[u]);
        if (d < 0.01f) {
          sm += corr(SEEN[r][k], SEEN[r][u], n[r]);
          ns++;
        } else if (d < 0.1f) {
          st += corr(SEEN[r][k], SEEN[r][u], n[r]);
          nt++;
        } else if (d > 0.24f) {
          fa += corr(SEEN[r][k], SEEN[r][u], n[r]);
          nf++;
        }
      }
    }
    out[r].same = ns ? sm / ns : 0.0;
    out[r].step = nt ? st / nt : 0.0;
    out[r].far = nf ? fa / nf : 0.0;
    out[r].selectivity = out[r].same - out[r].far;
    out[r].invariance = (out[r].step - out[r].far) / (out[r].selectivity + 1e-9);
  }
}

int main(void) {
  static const char *const TWIN[3] = {"its cortex does not learn", "it learns", "it learns, with the trace rule"};
  static const char *const AREA[2] = {"visual cortex", "temporal lobe"};
  areal_t a[3][2], one[2];
  unsigned twin, r, seed;

  printf("THE CORTEX LEARNS WHAT IT SEES (three brains, averaged)\n\n");
  memset(a, 0, sizeof a);
  for (seed = 1u; seed <= 3u; seed++) {
    for (twin = 0u; twin < 3u; twin++) {
      live(seed, twin > 0u, twin == 2u, one);
      for (r = 0u; r < 2u; r++) {
        a[twin][r].same += one[r].same / 3.0;
        a[twin][r].step += one[r].step / 3.0;
        a[twin][r].far += one[r].far / 3.0;
        a[twin][r].selectivity += one[r].selectivity / 3.0;
        a[twin][r].invariance += one[r].invariance / 3.0;
      }
    }
  }
  for (twin = 0u; twin < 3u; twin++) {
    for (r = 0u; r < 2u; r++)
      printf("        %-31s %s: same view %.2f, one step away %.2f, far %.2f; selectivity %.2f, invariance %.2f\n",
             r ? "" : TWIN[twin], AREA[r], a[twin][r].same, a[twin][r].step, a[twin][r].far, a[twin][r].selectivity,
             a[twin][r].invariance);
  }
  check("selective: learning makes the visual cortex more selective for what it sees (1.2x)",
        a[1][0].selectivity > 1.2 * a[0][0].selectivity);
  check("selective: and the temporal lobe (1.5x)", a[1][1].selectivity > 1.5 * a[0][1].selectivity);
  check("stable: learning makes the temporal lobe's pattern for a view repeat more (by 0.2 or more)",
        a[1][1].same > a[0][1].same + 0.2);
  check("the stream: with learning, invariance grows from the visual cortex to the temporal lobe (by 0.05 or more)",
        a[1][1].invariance > a[1][0].invariance + 0.05);
  check("the stream: without learning it falls", a[0][1].invariance < a[0][0].invariance);
  printf("        (the trace rule: temporal invariance %.2f with it, %.2f without)\n", a[2][1].invariance,
         a[1][1].invariance);
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
