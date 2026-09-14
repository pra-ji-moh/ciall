/*
 * baseline.c -- the control, in C. A port of baseline.py.
 *
 * Softmax and cross-entropy, doing exactly what it is supposed to: a
 * predictive model on purpose, the thing to beat. Two layers, tanh,
 * full-batch gradient descent, backpropagation written out by hand so
 * every gradient in the experiment is visible in the file.
 *
 * What it should show: confident and right on CLEAN, confidence near 0.5
 * on AMBIGUOUS (correct, the world is undecided), and on NO_GROUNDS a
 * confident answer in a region it has never seen, because softmax has no
 * channel for "no grounds". The headline: one confidence number DOES
 * separate NO_GROUNDS from AMBIGUOUS (AUROC 0.946), but backwards. The
 * model is more confident where it has no grounds, so abstaining on low
 * confidence refuses exactly the wrong class.
 *
 * The initial weights come from nprand.c, which reproduces numpy's
 * default_rng(0) bit for bit (test_nprand.c), so this starts from exactly
 * the weights baseline.py starts from and its output is the Python's.
 * --init FILE still loads 256 weights from a file (W1 row-major, then W2),
 * for experiments from other starting points.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "dataset.h"
#include "nprand.h"

#define N_TRAIN 6000u
#define N_TEST 6000u
#define HIDDEN 64u
#define STEPS 4000u
#define LR 0.5
#define SEED 0u

static ds_sample_t TR[N_TRAIN], TE[N_TEST];
static double W1[2][HIDDEN], B1[HIDDEN], W2[HIDDEN][2], B2[2];
static double H[N_TRAIN][HIDDEN], DHP[N_TRAIN][HIDDEN], DL[N_TRAIN][2];

static int load_init(const char *path) {
  FILE *f = fopen(path, "r");
  unsigned i, j;
  if (f == NULL) return 0;
  for (i = 0u; i < 2u; i++)
    for (j = 0u; j < HIDDEN; j++)
      if (fscanf(f, "%lf", &W1[i][j]) != 1) { fclose(f); return 0; }
  for (i = 0u; i < HIDDEN; i++)
    for (j = 0u; j < 2u; j++)
      if (fscanf(f, "%lf", &W2[i][j]) != 1) { fclose(f); return 0; }
  fclose(f);
  return 1;
}

/* forward pass for one sample: hidden activations into h, probabilities into p */
static void forward(double x0, double x1, double *h, double *p) {
  double l0 = 0.0, l1 = 0.0, m, e0, e1;
  unsigned j;
  for (j = 0u; j < HIDDEN; j++) {
    h[j] = tanh(x0 * W1[0][j] + x1 * W1[1][j] + B1[j]);
    l0 += h[j] * W2[j][0];
    l1 += h[j] * W2[j][1];
  }
  l0 += B2[0];
  l1 += B2[1];
  m = l0 > l1 ? l0 : l1;
  e0 = exp(l0 - m);
  e1 = exp(l1 - m);
  p[0] = e0 / (e0 + e1);
  p[1] = e1 / (e0 + e1);
}

static void train(void) {
  unsigned step, i, j;
  for (step = 0u; step < STEPS; step++) {
    double dW1[2][HIDDEN], dB1[HIDDEN], dW2[HIDDEN][2], dB2[2] = {0.0, 0.0};
    memset(dW1, 0, sizeof dW1);
    memset(dB1, 0, sizeof dB1);
    memset(dW2, 0, sizeof dW2);
    for (i = 0u; i < N_TRAIN; i++) {
      double p[2];
      forward(TR[i].x, TR[i].y, H[i], p);
      /* dL/dlogits for softmax + cross-entropy is (p - y): the one piece
         of calculus this file relies on */
      DL[i][0] = (p[0] - (TR[i].label == 0 ? 1.0 : 0.0)) / (double)N_TRAIN;
      DL[i][1] = (p[1] - (TR[i].label == 1 ? 1.0 : 0.0)) / (double)N_TRAIN;
    }
    for (i = 0u; i < N_TRAIN; i++) {
      dB2[0] += DL[i][0];
      dB2[1] += DL[i][1];
      for (j = 0u; j < HIDDEN; j++) {
        double dh = DL[i][0] * W2[j][0] + DL[i][1] * W2[j][1];
        dW2[j][0] += H[i][j] * DL[i][0];
        dW2[j][1] += H[i][j] * DL[i][1];
        DHP[i][j] = dh * (1.0 - H[i][j] * H[i][j]);
        dW1[0][j] += TR[i].x * DHP[i][j];
        dW1[1][j] += TR[i].y * DHP[i][j];
        dB1[j] += DHP[i][j];
      }
    }
    for (j = 0u; j < HIDDEN; j++) {
      W1[0][j] -= LR * dW1[0][j];
      W1[1][j] -= LR * dW1[1][j];
      B1[j] -= LR * dB1[j];
      W2[j][0] -= LR * dW2[j][0];
      W2[j][1] -= LR * dW2[j][1];
    }
    B2[0] -= LR * dB2[0];
    B2[1] -= LR * dB2[1];
  }
}

/* Probability a random positive scores above a random negative, computed
   as baseline.py does: a STABLE sort, ranks 1..n with no tie averaging,
   positives listed first. Written out so the number cannot come from a
   library doing something subtly different. */
static double SC[N_TEST], TMP_S[N_TEST];
static int LB[N_TEST], TMP_L[N_TEST];

static void merge_sort(unsigned lo, unsigned hi) {
  unsigned mid, a, b, k;
  if (hi - lo < 2u) return;
  mid = lo + (hi - lo) / 2u;
  merge_sort(lo, mid);
  merge_sort(mid, hi);
  for (a = lo, b = mid, k = lo; k < hi; k++) {
    if (a < mid && (b >= hi || SC[a] <= SC[b])) { TMP_S[k] = SC[a]; TMP_L[k] = LB[a]; a++; }
    else { TMP_S[k] = SC[b]; TMP_L[k] = LB[b]; b++; }
  }
  for (k = lo; k < hi; k++) { SC[k] = TMP_S[k]; LB[k] = TMP_L[k]; }
}

static double auroc(const double *pos, unsigned n_pos, const double *neg, unsigned n_neg) {
  unsigned i, n = n_pos + n_neg;
  double rank_sum = 0.0;
  if (n_pos == 0u || n_neg == 0u) return NAN;
  for (i = 0u; i < n_pos; i++) { SC[i] = pos[i]; LB[i] = 1; }
  for (i = 0u; i < n_neg; i++) { SC[n_pos + i] = neg[i]; LB[n_pos + i] = 0; }
  merge_sort(0u, n);
  for (i = 0u; i < n; i++) if (LB[i]) rank_sum += (double)(i + 1u);
  return (rank_sum - (double)n_pos * ((double)n_pos + 1.0) / 2.0) / ((double)n_pos * (double)n_neg);
}

int main(int argc, char **argv) {
  static double conf[N_TEST], amb[N_TEST], ng[N_TEST];
  static int pred[N_TEST];
  static const char *EXPECT[3] = {"confident, right", "conf near 0.50", "should abstain"};
  unsigned i, j, c, n_amb = 0u, n_ng = 0u;
  double h[HIDDEN], mean_amb = 0.0, mean_ng = 0.0, sep;
  const char *source = "seed 0";

  ds_generate(TR, N_TRAIN, 1u, 0.08, 1);
  ds_generate(TE, N_TEST, 2u, 0.08, 0);

  if (argc == 3 && strcmp(argv[1], "--init") == 0) {
    if (!load_init(argv[2])) { printf("could not read 256 weights from %s\n", argv[2]); return 1; }
    source = "weights loaded from a file";
  } else {
    nprand_t rng;
    if (!nprand_seed(&rng, SEED)) { printf("ziggurat tables failed their checksum\n"); return 1; }
    /* Xavier, so the tanh does not saturate at initialisation. Drawn in
       numpy's order: W1 (2 x 64) row-major, then W2 (64 x 2). */
    for (i = 0u; i < 2u; i++) for (j = 0u; j < HIDDEN; j++) W1[i][j] = nprand_normal(&rng, 0.0, sqrt(1.0 / 2.0));
    for (i = 0u; i < HIDDEN; i++) for (j = 0u; j < 2u; j++) W2[i][j] = nprand_normal(&rng, 0.0, sqrt(1.0 / HIDDEN));
  }

  train();
  for (i = 0u; i < N_TEST; i++) {
    double p[2];
    forward(TE[i].x, TE[i].y, h, p);
    conf[i] = p[0] >= p[1] ? p[0] : p[1];
    pred[i] = p[1] > p[0] ? 1 : 0;   /* argmax: the first on a tie */
  }

  printf("baseline: softmax + cross-entropy, %u hidden, %u steps, %s\n\n", HIDDEN, STEPS, source);
  printf("%-12s%6s%11s%12s%22s\n", "category", "n", "accuracy", "mean conf", "correct behaviour");
  for (c = 0u; c < 3u; c++) {
    unsigned n = 0u, right = 0u;
    double sum = 0.0;
    for (i = 0u; i < N_TEST; i++) {
      if (TE[i].category != (ds_category_t)c) continue;
      n++;
      right += pred[i] == TE[i].label;
      sum += conf[i];
    }
    printf("%-12s%6u%11.3f%12.3f%22s\n", DS_NAME[c], n, (double)right / n, sum / n, EXPECT[c]);
  }
  for (i = 0u; i < N_TEST; i++) {
    if (TE[i].category == DS_AMBIGUOUS) { amb[n_amb++] = conf[i]; mean_amb += conf[i]; }
    if (TE[i].category == DS_NO_GROUNDS) { ng[n_ng++] = conf[i]; mean_ng += conf[i]; }
  }
  mean_amb /= n_amb;
  mean_ng /= n_ng;
  sep = auroc(ng, n_ng, amb, n_amb);

  printf("\nCan one confidence number tell \"no grounds\" from \"genuinely ambiguous\"?\n");
  printf("  mean confidence, no_grounds : %.3f\n", mean_ng);
  printf("  mean confidence, ambiguous  : %.3f\n", mean_amb);
  printf("  AUROC separating them       : %.3f   (0.5 = cannot tell at all)\n\n", sep);
  if (mean_ng > 0.8) {
    printf("  The model is confident in a region it has never seen. It has no\n");
    printf("  channel for \"no grounds\", so it extrapolates and asserts.\n");
  }
  if (mean_ng > mean_amb) {
    printf("  It does tell them apart, but BACKWARDS: more confident with no\n");
    printf("  grounds than on a coin flip. Abstaining on low confidence would\n");
    printf("  refuse the ambiguous cases, which it should answer, and answer\n");
    printf("  the no-grounds ones, which it should refuse.\n");
  }
  printf("  Whatever replaces this has to abstain on no grounds and answer,\n");
  printf("  uncertainly, on the ambiguous class. Abstaining there is wrong.\n");
  return 0;
}
