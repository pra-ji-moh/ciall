/*
 * test_brain.c -- the open brain grows itself, and grows as development says.
 *
 * Checked:
 *   regions        every region forms, on both sides, from the gradients alone
 *   symmetry       the two sides grow alike
 *   inside out     in every cortical column the first born lie deepest
 *   migration      inhibitory neurons are born below the cortex and end up in it
 *   the senses     the retina wires only into the thalamus's visual band and the
 *                  tectum, on the other side; touch only into the touch band
 *   retinotopy     before anything is seen, the retina's map lands in order on
 *                  the visual cortex: where a neuron of layer 4 hears from on
 *                  the retina follows where it sits, closely
 *   the genes      a wider parietal gene grows more parietal cortex; a thicker
 *                  callosum gene grows more connections that cross
 *   repeatable     the same genome grows the same brain, connection for
 *                  connection
 *   readable       the genome written out reads back the same
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ob.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static ob_brain_t A, B;

static unsigned rule_total(const ob_brain_t *b, const char *name) {
  unsigned k, n = 0u;
  for (k = 0u; k < 64u; k++) {
    if (b->rule_count[k] && strcmp(ob_rule_name(k), name) == 0) n += b->rule_count[k];
  }
  return n;
}

/* The correlation between where occipital layer-4 neurons sit and where on the
   retina (through the thalamus) they hear from, across the sheet. */
static double retinotopy(const ob_brain_t *b) {
  static double rx[OB_MAX_NEURONS];
  static unsigned rh[OB_MAX_NEURONS];
  unsigned k, s;
  double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, n = 0;
  memset(rh, 0, sizeof rh);
  memset(rx, 0, sizeof rx);
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *t = &b->neuron[k];
    if (t->region != OB_THALAMUS) continue;
    for (s = t->syn_first; s < t->syn_first + t->syn_n; s++) {
      const ob_neuron_t *src = &b->neuron[b->syn[s].from];
      if (src->region == OB_RETINA_L || src->region == OB_RETINA_R) {
        rx[k] += src->sy;
        rh[k]++;
      }
    }
  }
  for (k = 0u; k < b->n_neurons; k++) {
    const ob_neuron_t *c = &b->neuron[k];
    double acc = 0.0;
    unsigned h = 0u;
    if (c->region != OB_OCCIPITAL || c->layer != 2u || c->inhibitory) continue;
    for (s = c->syn_first; s < c->syn_first + c->syn_n; s++) {
      unsigned from = b->syn[s].from;
      if (b->neuron[from].region == OB_THALAMUS && rh[from] > 0u) {
        acc += rx[from] / rh[from];
        h++;
      }
    }
    if (h == 0u) continue;
    acc /= h;
    sx += c->sy;
    sy += acc;
    sxx += c->sy * c->sy;
    syy += acc * acc;
    sxy += c->sy * acc;
    n += 1.0;
  }
  if (n < 10.0) return 0.0;
  return (n * sxy - sx * sy) / sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
}

int main(void) {
  ob_genome_t g, g2;
  unsigned r, k, s;
  int all = 1, sym = 1, inside_out = 1, migrated = 1, senses = 1;

  printf("THE OPEN BRAIN GROWS ITSELF\n\n");

  ob_genome_default(&g);
  ob_grow(&A, &g);

  for (r = 1u; r < OB_REGIONS; r++) {
    if (r == OB_RETINA_L) all &= A.count[r][OB_LEFT] > 0u;
    else if (r == OB_RETINA_R) all &= A.count[r][OB_RIGHT] > 0u;
    else all &= A.count[r][OB_LEFT] > 0u && A.count[r][OB_RIGHT] > 0u;
  }
  check("every region forms, on both sides, from the gradients alone", all && A.dropped == 0u);

  for (r = 1u; r < OB_RETINA_L; r++) {
    unsigned l = A.count[r][OB_LEFT], rr = A.count[r][OB_RIGHT];
    if ((l > rr ? l - rr : rr - l) * 10u > (l + rr)) sym = 0;
  }
  check("the two sides grow alike (within a tenth)", sym);

  for (k = 0u; k + 1u < A.n_neurons; k++) {
    const ob_neuron_t *a = &A.neuron[k], *c = &A.neuron[k + 1u];
    if (a->inhibitory || c->inhibitory || a->px != c->px || a->py != c->py) continue;
    if (a->region < OB_FRONTAL || a->region > OB_OCCIPITAL) continue;
    if (c->birth > a->birth && c->layer < a->layer) inside_out = 0;
  }
  check("inside out: in every cortical column the first born lie deepest", inside_out);

  {
    unsigned n_in = 0u;
    for (k = 0u; k < A.n_neurons; k++) {
      const ob_neuron_t *n = &A.neuron[k];
      if (!n->inhibitory) continue;
      n_in++;
      if (A.plate.fate[n->px][n->py] != OB_BASAL_GANGLIA || n->region < OB_FRONTAL || n->region > OB_OCCIPITAL)
        migrated = 0;
    }
    check("inhibitory neurons are born below the cortex and migrate into it", migrated && n_in > 100u);
  }

  for (k = 0u; k < A.n_neurons; k++) {
    const ob_neuron_t *n = &A.neuron[k];
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      const ob_neuron_t *src = &A.neuron[A.syn[s].from];
      int retina = src->region == OB_RETINA_L || src->region == OB_RETINA_R;
      if (retina && !((n->region == OB_THALAMUS && n->layer == 3u) || n->region == OB_TECTUM)) senses = 0;
      if (retina && n->side == src->side) senses = 0;
      if (src->region == OB_SPINAL_SENSORY && n->region == OB_THALAMUS && n->layer != 1u) senses = 0;
    }
  }
  check("the retina wires only to the visual nucleus and the tectum, crossing; touch only to the touch nucleus",
        senses);

  {
    double rho = retinotopy(&A);
    printf("        (retinotopy: correlation %.3f)\n", rho);
    check("before anything is seen, the retina's map lands in order on the visual cortex (r > 0.9)", rho > 0.9);
  }

  {
    unsigned par0 = A.count[OB_PARIETAL][0] + A.count[OB_PARIETAL][1];
    unsigned cc0 = rule_total(&A, "corpus callosum");
    g2 = g;
    g2.parietal_end = 0.80;
    g2.temporal_end = 0.25;
    ob_grow(&B, &g2);
    check("a wider parietal gene grows more parietal cortex",
          B.count[OB_PARIETAL][0] + B.count[OB_PARIETAL][1] > par0 * 3u / 2u);
    g2 = g;
    g2.callosum = 0.30;
    ob_grow(&B, &g2);
    check("a thicker callosum gene grows more connections that cross",
          rule_total(&B, "corpus callosum") > cc0 * 3u / 2u);
  }

  ob_grow(&B, &g);
  check("the same genome grows the same brain, connection for connection",
        A.n_neurons == B.n_neurons && A.n_syn == B.n_syn &&
            memcmp(A.neuron, B.neuron, sizeof(ob_neuron_t) * A.n_neurons) == 0 &&
            memcmp(A.syn, B.syn, sizeof(ob_synapse_t) * A.n_syn) == 0);

  {
    FILE *f = fopen("test_brain_genome.tmp", "w");
    if (f != 0) {
      ob_genome_save(&g, f);
      fclose(f);
    }
    memset(&g2, 0, sizeof g2);
    check("the genome written out reads back the same",
          ob_genome_load(&g2, "test_brain_genome.tmp") && memcmp(&g, &g2, sizeof g) == 0);
    remove("test_brain_genome.tmp");
  }

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
