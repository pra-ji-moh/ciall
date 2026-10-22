/*
 * ob_one.c -- one brain: the grown brain, living. See ob_one.h.
 */

#include "ob_one.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- its anatomy, read once: who sends to whom, and what each rule is for ---- */

enum { C_OTHER, C_SENSE, C_PRED, C_ACTION, C_LEARN };   /* what a synapse is, by the rule that grew it */

static unsigned char CLASS[64];
static unsigned TARGET[OB_MAX_SYN];
static unsigned OUT_FIRST[OB_MAX_NEURONS + 1u], OUT_SYN[OB_MAX_SYN];
static unsigned char ZONE[OB_MAX_NEURONS];       /* brainstem zone of each neuron (OL_MOTORS: none) */
static unsigned char GROUP[OB_MAX_NEURONS];      /* the sense a thalamic neuron relays (OL_GROUPS: none) */
static unsigned ZONE_N[OL_MOTORS];
static unsigned ACTION_SYN[OB_MAX_SYN], N_ACTION;            /* the basal ganglia's synapses to the zones */
static unsigned BG[OB_MAX_NEURONS], N_BG;                    /* the basal ganglia's neurons */
static unsigned THAL[OB_MAX_NEURONS], N_THAL;                /* the thalamic neurons that relay a sense */
static unsigned STEM[OB_MAX_NEURONS], N_STEM;                /* the brainstem's neurons */
static unsigned HIP[OB_HIP_BYTES * 8], N_HIP;                /* the hippocampus's neurons */
static float DRIVE[OB_MAX_NEURONS], PRED[OB_MAX_NEURONS], INPUT[OB_MAX_NEURONS], PRED_CB[OB_MAX_NEURONS];
static float NEXT[OB_MAX_NEURONS];
static float RAW[OB_MAX_NEURONS];                 /* each neuron's drive before clipping: what ranks it */
static float BEFORE[OB_MAX_NEURONS];              /* activity before the last step: what predicted */
static unsigned FROM[OB_MAX_SYN];                 /* each synapse's source, and what it is for */
static unsigned char SCLASS[OB_MAX_SYN];
static unsigned char FIXED[OB_MAX_NEURONS];        /* held by the world or by itself, not by its inputs */
static unsigned CB_INDEX[OB_MAX_NEURONS], N_CB;    /* the cerebellum's neurons, numbered */

/* each zone acts of itself about one moment in fifty; the more neurons it
   has, the higher each one's threshold must be for that */
#define BASE_RATE 0.02
#define URGE_MIN -0.5f
#define URGE_MAX 0.6f
static float THETA[OL_MOTORS];

/* the point a standard normal exceeds with probability p (Acklam's approximation) */
static double normal_above(double p) {
  static const double c[6] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                              -2.549671010000035e+00, 4.374664141464968e+00, 2.938163982698783e+00};
  static const double d[4] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                              3.754408661907416e+00};
  double q = sqrt(-2.0 * log(p));   /* p is small: the upper tail */
  return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) /
         ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1.0);
}
/* the relays: the senses pass through them, with no loop to run away, so
   their activity is not held in check */
static const unsigned char RELAY[OB_REGIONS] = {[OB_THALAMUS] = 1, [OB_TECTUM] = 1, [OB_COLLICULUS_INF] = 1};

static int is_sensor(unsigned r) {
  return r == OB_RETINA_L || r == OB_RETINA_R || r == OB_COCHLEA_L || r == OB_COCHLEA_R || r == OB_VESTIBULE_L ||
         r == OB_VESTIBULE_R || r == OB_SPINAL_SENSORY;
}

unsigned ob_one_zone(const ob_neuron_t *n) {
  /* the brainstem, head to tail: the eyes' centres first (left side moves them
     left and up, the right side right and down), then the voice, then the body */
  if (n->region != OB_BRAINSTEM) return OL_MOTORS;
  if (n->sx < 0.12f) return n->side == OB_LEFT ? OL_LOOK_LEFT : OL_LOOK_RIGHT;
  if (n->sx < 0.24f) return n->side == OB_LEFT ? OL_LOOK_UP : OL_LOOK_DOWN;
  if (n->sx < 0.36f) return OL_VOICE;
  if (n->sx < 0.60f) return OL_BUZZ;
  return OL_MOTORS;
}

static void read_anatomy(const ob_brain_t *b) {
  unsigned i, s, k;
  static unsigned fill[OB_MAX_NEURONS + 1u];
  for (k = 0u; k < 64u; k++) {
    const char *nm = ob_rule_name(k);
    CLASS[k] = C_OTHER;
    /* predictions: the cerebellum's (what its own acts will change) and the
       cortex's feedback from layer 6 (the steady world: what was there is still
       there), both learned from nothing */
    if (strncmp(nm, "prediction:", 11) == 0 || strncmp(nm, "timing:", 7) == 0 ||
        strcmp(nm, "cortex layer 6 back to thalamus") == 0)
      CLASS[k] = C_PRED;
    else if (strncmp(nm, "action:", 7) == 0) CLASS[k] = C_ACTION;
    /* the cortex's feedforward pathways learn, after birth: the thalamus into
       layer 4, layer 4 up to layers 2/3, and the visual cortex on into the
       temporal ("what") and parietal ("where") lobes */
    else if (strcmp(nm, "thalamus to cortex layer 4, by area") == 0 ||
             strcmp(nm, "column: layer 4 to layers 2/3") == 0 || strcmp(nm, "occipital to temporal") == 0 ||
             strcmp(nm, "occipital to parietal") == 0 || strcmp(nm, "to entorhinal from temporal") == 0 ||
             strcmp(nm, "to entorhinal from parietal") == 0 ||
             strcmp(nm, "perforant path: entorhinal to hippocampus") == 0)
      CLASS[k] = C_LEARN;
    else if (strcmp(nm, "retina to thalamus (visual nucleus)") == 0 ||
             strcmp(nm, "touch: spinal cord to thalamus (touch nucleus)") == 0 ||
             strcmp(nm, "hearing: inferior colliculus to thalamus (hearing nucleus)") == 0 ||
             strcmp(nm, "balance: vestibule to thalamus (touch nucleus)") == 0)
      CLASS[k] = C_SENSE;
  }
  memset(OUT_FIRST, 0, sizeof OUT_FIRST);
  memset(ZONE_N, 0, sizeof ZONE_N);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    unsigned skin = 0u, vest = 0u;
    ZONE[i] = (unsigned char)ob_one_zone(n);
    FIXED[i] = (unsigned char)(is_sensor(n->region) || n->region == OB_BRAINSTEM || n->region == OB_TEGMENTUM);
    if (ZONE[i] < OL_MOTORS) ZONE_N[ZONE[i]]++;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      unsigned r = b->neuron[b->syn[s].from].region;
      TARGET[s] = i;
      FROM[s] = b->syn[s].from;
      SCLASS[s] = CLASS[b->syn[s].rule];
      OUT_FIRST[b->syn[s].from + 1u]++;
      if (CLASS[b->syn[s].rule] == C_SENSE) {
        if (r == OB_VESTIBULE_L || r == OB_VESTIBULE_R) vest++;
        else skin++;
      }
    }
    GROUP[i] = OL_GROUPS;
    if (n->region == OB_THALAMUS) {
      if (n->layer == 3u) GROUP[i] = OL_SEE;
      else if (n->layer == 2u) GROUP[i] = OL_HEAR;
      else if (n->layer == 1u && (skin || vest)) GROUP[i] = vest >= skin ? OL_MOVED : OL_FEEL;
    }
  }
  for (k = 0u; k < OL_MOTORS; k++)
    THETA[k] = ZONE_N[k] ? (float)normal_above(1.0 - pow(1.0 - BASE_RATE, 1.0 / ZONE_N[k])) : 1e30f;
  N_ACTION = N_BG = N_THAL = N_STEM = N_CB = N_HIP = 0u;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (n->region == OB_BASAL_GANGLIA) BG[N_BG++] = i;
    if (n->region == OB_BRAINSTEM) STEM[N_STEM++] = i;
    if (n->region == OB_CEREBELLUM) CB_INDEX[i] = N_CB < 4096u ? N_CB++ : 4095u;
    if (n->region == OB_HIPPOCAMPUS && N_HIP < OB_HIP_BYTES * 8u) HIP[N_HIP++] = i;
    if (GROUP[i] < OL_GROUPS) THAL[N_THAL++] = i;
    if (ZONE[i] >= OL_MOTORS) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      if (CLASS[b->syn[s].rule] == C_ACTION) ACTION_SYN[N_ACTION++] = s;
    }
  }
  for (i = 0u; i < b->n_neurons; i++) OUT_FIRST[i + 1u] += OUT_FIRST[i];
  memcpy(fill, OUT_FIRST, sizeof(unsigned) * (b->n_neurons + 1u));
  for (s = 0u; s < b->n_syn; s++) OUT_SYN[fill[b->syn[s].from]++] = s;
}

/* ---- what it needs ------------------------------------------------------------- */

static unsigned rnd(ob_one_t *O) {
  unsigned x = O->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  O->rng = x;
  return x;
}
static float unit(ob_one_t *O) { return (float)(rnd(O) >> 8) / 16777216.0f; }
static float gauss(ob_one_t *O) { return (unit(O) + unit(O) + unit(O) + unit(O) - 2.0f) * 1.7320508f; }

#define FORGET 0.0002f
#define ACTOR 0.05f
#define PRED_RATE 0.1f   /* the delta rule's step, against all its sources' activity */

/* ---- the world arrives ----------------------------------------------------------- */

static void senses_into(const ob_brain_t *b, const float *in, float *act) {
  unsigned i;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    switch (n->region) {
    case OB_RETINA_L:
    case OB_RETINA_R: {
      unsigned x = (unsigned)(n->sx * OL_EYE), y = (unsigned)(n->sy * OL_EYE);
      act[i] = in[y * OL_EYE + x];
      break;
    }
    case OB_COCHLEA_L:
    case OB_COCHLEA_R: act[i] = in[OL_VIS + (unsigned)(n->sx * OL_EAR)]; break;
    case OB_VESTIBULE_L:
    case OB_VESTIBULE_R: act[i] = in[OL_VIS + OL_EAR + OL_SKIN + (unsigned)(n->sx * OL_MOVE)]; break;
    case OB_SPINAL_SENSORY: {
      /* its skin: eight places, four down each side of its body (its face, the
         first tenth, is left to the womb's hand and mouth) */
      int place = n->sx < 0.1f ? -1 : (int)((n->sx - 0.1f) / 0.225f);
      if (place > 3) place = 3;
      act[i] = place < 0 ? 0.0f : in[OL_VIS + OL_EAR + (unsigned)place + 4u * n->side];
      break;
    }
    default: break;
    }
  }
}

static void senses(ob_one_t *O, const float *in) { senses_into(&O->brain, in, O->act); }

/* ---- the hippocampus --------------------------------------------------------------
 * The sketch's hippocampus (ob_life.c), the same code: each sense files every
 * moment under its own pattern and what it did lately. When now is a moment
 * it has lived, what came next then drives that sense's relays again, as
 * recall reactivates the senses' own areas: an expectation in each relay
 * neuron, followed where memory has earned trust.
 *
 * (It does not yet file moments under its own hippocampal neurons' activity:
 * its cortex's patterns do not repeat when the world does, not yet; with
 * adaptation, the visual cortex's correlation between two moments of the same
 * view is about 0.4, the hippocampus's 0.2.) */

static float SCRATCH[OB_MAX_NEURONS];

/* a remembered moment drives the relays again: its senses, through the
   midbrain, to what each relay neuron would receive */
static void reinstate(ob_one_t *O, const unsigned char *remembered, unsigned group) {
  const ob_brain_t *b = &O->brain;
  float in[OL_SENSE];
  unsigned i, s, k;
  for (i = 0u; i < OL_SENSE; i++) in[i] = (float)remembered[i] / 255.0f;
  memset(SCRATCH, 0, sizeof(float) * b->n_neurons);
  senses_into(b, in, SCRATCH);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    float x = 0.0f;
    if (n->region != OB_COLLICULUS_INF) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) x += O->w[s] * SCRATCH[FROM[s]];
    SCRATCH[i] = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
  }
  for (k = 0u; k < N_THAL; k++) {
    const ob_neuron_t *n = &b->neuron[THAL[k]];
    float x = 0.0f;
    if (GROUP[THAL[k]] != group) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++)
      if (SCLASS[s] == C_SENSE) x += O->w[s] * SCRATCH[FROM[s]];
    O->memdrive[THAL[k]] = x;
  }
}

/* ---- episodes: the hippocampus's own patterns ---- */

static unsigned ones(unsigned char x) {
  unsigned n = 0u;
  while (x) {
    n += x & 1u;
    x >>= 1;
  }
  return n;
}

static void hip_bits(const ob_one_t *O, unsigned char *bits) {
  unsigned k;
  memset(bits, 0, OB_HIP_BYTES);
  for (k = 0u; k < N_HIP; k++) {
    if (O->act[HIP[k]] > 0.0f) bits[k / 8u] |= (unsigned char)(1u << (k % 8u));
  }
}

#define EPISODE_REACH 20000u   /* how far back it looks for an episode like now */
#define EPISODE_NEAR 0.5f      /* how alike (shared over all fired) to be "this again" */

/* the lived moment whose hippocampal pattern is most like now's, if alike enough */
static long episode_like(const ob_one_t *O, const unsigned char *now) {
  static unsigned char ONES[256];
  unsigned long m, from;
  unsigned k, a = 0u;
  long best = -1;
  float best_j = EPISODE_NEAR;
  if (ONES[255] == 0u)
    for (k = 0u; k < 256u; k++) ONES[k] = (unsigned char)ones((unsigned char)k);
  for (k = 0u; k < OB_HIP_BYTES; k++) a += ONES[now[k]];
  if (a == 0u || O->moments < 4u) return -1;
  from = O->moments > EPISODE_REACH ? O->moments - EPISODE_REACH : 0u;
  for (m = from; m + 3u < O->moments; m++) {
    const unsigned char *p = O->pattern[m % OL_MEMORY];
    unsigned both = 0u, b = 0u;
    float j;
    if (!ol_hip_held(&O->hip, m) || !ol_hip_held(&O->hip, m + 1u)) continue;
    for (k = 0u; k < OB_HIP_BYTES; k++) {
      both += ONES[p[k] & now[k]];
      b += ONES[p[k]];
    }
    j = (float)both / (float)(a + b - both);
    if (j > best_j) {
      best_j = j;
      best = (long)m;
    }
  }
  return best;
}

int ob_one_light(const ob_one_t *O, unsigned long moment, float *out) {
  unsigned i, slot = (unsigned)(moment % OL_MEMORY);
  if (moment >= O->moments || !ol_hip_held(&O->hip, moment)) return 0;
  for (i = 0u; i < OL_SENSE; i++) out[i] = (float)O->hip.senses[slot][i] / 255.0f;
  return 1;
}

long ob_one_reminded(const ob_one_t *O, unsigned g) { return g < OL_GROUPS ? O->recalled_moment[g] : -1; }

/* ---- one step of the whole brain --------------------------------------------------
 * Every neuron takes what its inputs carry. The senses are held to what the
 * world brings, the brainstem to what it does, the tegmentum to its dopamine.
 * Elsewhere activity is clipped to 0..1, the cerebellum's lingers a little, and
 * each region's mean is held in check (inhibition, in effect), so the loops
 * through the cortex cannot run away. For the thalamus, what the senses drive
 * and what the cerebellum predicts are kept apart. */
static void step_only(ob_one_t *O, float gx, float gy, int only);

/* keep only the most active `share` of a region's neurons on each side (in NEXT) */
static int by_value(const void *a, const void *b) {
  float x = *(const float *)a, y = *(const float *)b;
  return x < y ? 1 : x > y ? -1 : 0;
}
static void sparse(const ob_brain_t *b, unsigned region, float share) {
  static float vals[OB_MAX_NEURONS];
  unsigned side, i, n;
  for (side = 0u; side < 2u; side++) {
    float cut;
    n = 0u;
    for (i = 0u; i < b->n_neurons; i++) {
      if (b->neuron[i].region == region && b->neuron[i].side == side) vals[n++] = RAW[i];
    }
    if (n == 0u) continue;
    qsort(vals, n, sizeof(float), by_value);
    cut = vals[(unsigned)(share * n)];
    for (i = 0u; i < b->n_neurons; i++) {
      if (b->neuron[i].region == region && b->neuron[i].side == side && (RAW[i] <= cut || RAW[i] <= 0.0f))
        NEXT[i] = 0.0f;
    }
  }
}

/* what a sense sends through a thalamic neuron: its drive, up to the neuron's
   ceiling (it cannot fire more than fully); what is predicted cancels that */
#define SENT(d) ((d) > 1.0f ? 1.0f : (d))

static void step(ob_one_t *O, float gx, float gy) { step_only(O, gx, gy, -1); }

/* one step, for the whole brain (only < 0) or for one region */
static void step_only(ob_one_t *O, float gx, float gy, int only) {
  const ob_brain_t *b = &O->brain;
  unsigned i, r, side;
  double sum[OB_REGIONS][2];
  unsigned cnt[OB_REGIONS][2];
  /* each neuron gathers what its inputs carry (its inputs lie together) */
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    float acc[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};   /* by class: other, sense, prediction, action, learned */
    unsigned s, end = n->syn_first + n->syn_n;
    if (FIXED[i] || (only >= 0 && n->region != (unsigned)only) || (only < 0 && n->region == OB_CEREBELLUM)) continue;
    for (s = n->syn_first; s < end; s++) {
      if (n->region == OB_HIPPOCAMPUS && O->brain.neuron[FROM[s]].region == OB_HIPPOCAMPUS) continue;
      acc[SCLASS[s]] += O->w[s] * O->act[FROM[s]];
    }
    if (n->region == OB_THALAMUS) {
      float cb = 0.0f;
      for (s = n->syn_first; s < end; s++)
        if (SCLASS[s] == C_PRED && O->brain.neuron[FROM[s]].region == OB_CEREBELLUM) cb += O->w[s] * O->act[FROM[s]];
      PRED_CB[i] = cb;
    }
    /* (the basal ganglia's say on the brainstem is read as an urge, not here) */
    INPUT[i] = acc[C_OTHER] + acc[C_SENSE] + acc[C_LEARN];
    DRIVE[i] = acc[C_SENSE];
    PRED[i] = acc[C_PRED];
  }
  memset(sum, 0, sizeof sum);
  memset(cnt, 0, sizeof cnt);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    float x;
    if (FIXED[i] || (only >= 0 && n->region != (unsigned)only) || (only < 0 && n->region == OB_CEREBELLUM)) {
      NEXT[i] = O->act[i];
      continue;
    }
    x = INPUT[i] + (n->region == OB_THALAMUS ? PRED[i] : 0.0f);
    if (n->region == OB_CEREBELLUM) {
      /* the cerebellum's granule cells are delay lines: cell k of each column
         gives what reached it k moments ago (its birth order), so together they
         hold the last OL_WAVE moments */
      unsigned d = n->birth % OL_WAVE;
      O->held[O->moments % OL_WAVE][CB_INDEX[i]] = x;
      x = O->held[(O->moments + OL_WAVE - d) % OL_WAVE][CB_INDEX[i]];
    }
    if (n->region == OB_TECTUM && fabsf(n->sx - gx) < 0.125f && fabsf(n->sy - gy) < 0.125f) x += 0.8f;
    /* adaptation: beyond the relays and the cerebellum, a neuron answers how
       its input differs from what it usually gets */
    if (!RELAY[n->region] && n->region != OB_CEREBELLUM) {
      float d = x - O->usual_in[i];
      O->usual_in[i] += (O->moments < 200u ? 0.05f : 0.0005f) * (x - O->usual_in[i]);
      x = d - O->threshold[i];   /* and a neuron that fires too often raises its threshold */
    }
    RAW[i] = x;
    NEXT[i] = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
  }
  /* the cerebellum's granule cells are sparse: of each side, only the most
     strongly driven fifth fire; the hippocampus's dentate gyrus, a tenth, to
     keep moments apart */
  if (only == OB_CEREBELLUM) sparse(b, OB_CEREBELLUM, 0.2f);
  if (only < 0) {
    sparse(b, OB_HIPPOCAMPUS, 0.1f);
    /* the cortex too: within each area, inhibition lets the most strongly
       driven tenth fire, so they compete */
    sparse(b, OB_FRONTAL, 0.1f);
    sparse(b, OB_PARIETAL, 0.1f);
    sparse(b, OB_TEMPORAL, 0.1f);
    sparse(b, OB_OCCIPITAL, 0.1f);
    sparse(b, OB_ENTORHINAL, 0.1f);
  }
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (FIXED[i] || (only >= 0 && n->region != (unsigned)only) || (only < 0 && n->region == OB_CEREBELLUM)) continue;
    sum[n->region][n->side] += NEXT[i];
    cnt[n->region][n->side]++;
  }
  for (r = 0u; r < OB_REGIONS; r++) {
    for (side = 0u; side < 2u; side++) {
      double mean = cnt[r][side] ? sum[r][side] / cnt[r][side] : 0.0;
      float scale = mean > 0.1 ? (float)(0.1 / mean) : 1.0f;
      sum[r][side] = scale;
    }
  }
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (!FIXED[i] && !RELAY[n->region] && (only < 0 ? n->region != OB_CEREBELLUM : n->region == (unsigned)only))
      NEXT[i] *= (float)sum[n->region][n->side];
    if (NEXT[i] < 0.05f) NEXT[i] = 0.0f;   /* too weak to fire */
  }
  memcpy(BEFORE, O->act, sizeof(float) * b->n_neurons);
  memcpy(O->act, NEXT, sizeof(float) * b->n_neurons);
}

/* ---- being born ------------------------------------------------------------------- */

void ob_one_born(ob_one_t *O, const ob_brain_t *grown, unsigned seed, unsigned curiosity) {
  unsigned s, i;
  memset(O, 0, sizeof(*O));
  memcpy(&O->brain, grown, sizeof(O->brain));
  read_anatomy(&O->brain);
  for (s = 0u; s < O->brain.n_syn; s++) O->w[s] = (float)O->brain.syn[s].weight / 1000.0f;
  /* the cortex's feedback starts at nothing, like the cerebellum's: it is learned */
  for (s = 0u; s < O->brain.n_syn; s++) {
    if (SCLASS[s] == C_PRED && strcmp(ob_rule_name(O->brain.syn[s].rule), "cortex layer 6 back to thalamus") == 0)
      O->w[s] = 0.0f;
  }
  /* the relays' gain: each relay neuron's inputs from the senses (and the
     cerebellum's predictions of them) are scaled to sum to one, so it passes on
     a weighted average of what it hears from, between nothing and full */
  for (i = 0u; i < O->brain.n_neurons; i++) {
    const ob_neuron_t *n = &O->brain.neuron[i];
    float total = 0.0f;
    if (!RELAY[n->region]) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      unsigned r = O->brain.neuron[FROM[s]].region;
      if (SCLASS[s] == C_SENSE || (n->region != OB_THALAMUS && is_sensor(r))) total += O->w[s];
    }
    if (total <= 0.0f) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      unsigned r = O->brain.neuron[FROM[s]].region;
      if (SCLASS[s] == C_SENSE || SCLASS[s] == C_PRED || (n->region != OB_THALAMUS && is_sensor(r))) O->w[s] /= total;
    }
  }
  for (i = 0u; i < OL_MOTORS; i++) {
    O->rate[i] = 0.02f;
    O->ago[i] = 255u;
  }
  O->rng = seed * 2654435761u + 0x9E3779B9u;
  if (O->rng == 0u) O->rng = 1u;
  O->curiosity = curiosity;
  O->learning = 1u;
  O->remembers = 1u;
  O->episodes = 1u;
  O->cortex_learns = 1u;
  O->trace_rule = 0u;   /* Foldiak's trace rule: tried, and it did not help here (see test_cortex.c) */
  for (i = 0u; i < OL_GROUPS; i++) {
    O->recalled[i] = -1;
    O->recalled_moment[i] = -1;
  }
}

void ob_one_settle(ob_one_t *O) {
  unsigned s;
  for (s = 0u; s < O->brain.n_syn; s++) {
    float w = O->w[s] * 1000.0f;
    if (w > 32000.0f) w = 32000.0f;
    if (w < -32000.0f) w = -32000.0f;
    O->brain.syn[s].weight = (short)lrintf(w);
  }
}

/* ---- the cortex learns -------------------------------------------------------------- */

#define CORTEX_RATE 0.02f

static void learn_cortex(ob_one_t *O) {
  const ob_brain_t *b = &O->brain;
  unsigned i, s;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    float y;
    if (!((n->region >= OB_FRONTAL && n->region <= OB_OCCIPITAL) || n->region == OB_ENTORHINAL ||
          n->region == OB_HIPPOCAMPUS))
      continue;
    O->trace[i] = 0.6f * O->trace[i] + 0.4f * O->act[i];
    /* homeostasis, where moments must be kept apart (the entorhinal cortex and
       the hippocampus): each aims to fire a tenth of the time, so the same few
       cannot answer everything */
    if (n->region == OB_TEMPORAL || n->region == OB_ENTORHINAL || n->region == OB_HIPPOCAMPUS)
      O->threshold[i] += 0.002f * ((O->act[i] > 0.0f ? 1.0f : 0.0f) - 0.1f);
    y = (n->region == OB_TEMPORAL || n->region == OB_ENTORHINAL) && O->trace_rule ? O->trace[i] : O->act[i];
    if (y <= 0.0f) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      if (SCLASS[s] != C_LEARN) continue;
      O->w[s] += CORTEX_RATE * y * (O->act[FROM[s]] - O->w[s]);
    }
  }
}

/* ---- living ------------------------------------------------------------------------ */

static unsigned gaze_bin(float x, float y) {
  int bx = (int)(x * 4.0f), by = (int)(y * 4.0f);
  bx = bx < 0 ? 0 : bx > 3 ? 3 : bx;
  by = by < 0 ? 0 : by > 3 ? 3 : by;
  return (unsigned)(by * 4 + bx);
}

unsigned ob_one_live(ob_one_t *O, const float *in) {
  ob_brain_t *b = &O->brain;
  float gx = in[OL_SENSE], gy = in[OL_SENSE + 1], err[OL_GROUPS], energy[OL_GROUPS], urge[OL_MOTORS];
  unsigned g, i, k, s, m, bits = 0u, n_err[OL_GROUPS];

  senses(O, in);
  /* 1. the senses reach the thalamus (hearing a step later, through the
        midbrain), and the cerebellum's prediction with them: what is left is
        its surprise; the cerebellum's synapses learn from it */
  step(O, gx, gy);
  step(O, gx, gy);
  memset(err, 0, sizeof err);
  memset(energy, 0, sizeof energy);
  memset(n_err, 0, sizeof n_err);
  {
    float by_cb[OL_GROUPS], by_mem[OL_GROUPS];
    memset(by_cb, 0, sizeof by_cb);
    memset(by_mem, 0, sizeof by_mem);
    for (k = 0u; k < N_THAL; k++) {
      float left, cb, mem = 0.0f;
      i = THAL[k];
      g = GROUP[i];
      /* what it passes on is what came and was not expected: a neuron cannot
         fire below nothing (expected sounds that did not come are another
         signal, the womb's signal of absence). Expected by the cerebellum and
         the cortex, or, where it follows memory, by what came next last time */
      cb = SENT(DRIVE[i]) + PRED[i];
      if (O->via[g]) mem = SENT(DRIVE[i]) - SENT(O->memdrive[i]);
      by_cb[g] += cb * cb;
      by_mem[g] += mem * mem;
      left = O->via[g] && O->trusted[g] ? mem : cb;
      if (left < 0.0f) left = 0.0f;
      err[g] += left * left;
      energy[g] += SENT(DRIVE[i]) * SENT(DRIVE[i]);
      n_err[g]++;
    }
    /* where memory can be trusted: has what came next last time predicted this
       better than the cerebellum and the cortex, on average? */
    if (O->learning) {
      for (g = 0u; g < OL_GROUPS; g++) {
        float *t;
        if (n_err[g] == 0u || O->via[g] == 0) continue;
        t = O->via[g] == 2 ? &O->episode_trust[g][O->episode_key[g]] : &O->hip.trust[g][O->recalled[g]];
        *t += 0.2f * ((by_cb[g] - by_mem[g]) / (float)n_err[g] - *t);
        if (*t == 0.0f) *t = by_cb[g] >= by_mem[g] ? 1e-9f : -1e-9f;
      }
    }
  }
  /* the delta rule: each prediction synapse moves to cancel what its neuron
     was left with, in proportion to the activity of its source when the
     prediction was made */
  if (O->learning) {
    for (k = 0u; k < N_THAL; k++) {
      const ob_neuron_t *n = &b->neuron[THAL[k]];
      float left, norm = 0.1f;
      i = THAL[k];
      left = SENT(DRIVE[i]) + PRED[i];
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        float a;
        if (SCLASS[s] != C_PRED) continue;
        a = BEFORE[FROM[s]];
        norm += a * a;
      }
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        float a;
        if (SCLASS[s] != C_PRED) continue;
        a = BEFORE[FROM[s]];
        if (a <= 0.0f) continue;
        O->w[s] -= PRED_RATE * left * a / norm;
        if (O->w[s] < -2.0f) O->w[s] = -2.0f;
        if (O->w[s] > 1.0f) O->w[s] = 1.0f;
      }
    }
  }
  for (g = 0u; g < OL_GROUPS; g++) {
    O->err[g] = n_err[g] ? err[g] / (float)n_err[g] : 0.0f;
    O->energy[g] = n_err[g] ? energy[g] / (float)n_err[g] : 0.0f;
  }
  /* 2b. the cortex learns: each neuron that fired moves its learned synapses
         towards what its inputs carried (competitive Hebbian learning: the
         winners become templates of what drove them). In the temporal lobe the
         rule uses the neuron's recent firing, not just now's (Foldiak's trace
         rule): what stays while the eyes move is one thing, and its neurons
         learn to keep answering it */
  if (O->learning && O->cortex_learns) learn_cortex(O);

  /* 2. (activity flows on through the cortex to the basal ganglia from moment
        to moment, so what reaches them is a moment or two old) */

  /* 3. the tegmentum: learning progress where each sense was */
  {
    float reward = 0.0f;
    for (g = 0u; g < OL_GROUPS && O->moments > 0u; g++) {
      unsigned k = O->place[g];
      float e = O->err[g];
      O->usual[g] += (O->moments < 1000u ? 0.01f : 0.0001f) * (e - O->usual[g]);
      O->fast[g][k] = O->visits[g][k] ? O->fast[g][k] + 0.05f * (e - O->fast[g][k]) : e;
      O->span[g][k] += e;
      O->visits[g][k]++;
      if (O->visits[g][k] % OL_SPAN == 0u) {
        float now = O->span[g][k] / (float)OL_SPAN, before = O->last[g][k];
        if (O->visits[g][k] > OL_SPAN) O->progress[g][k] = (before - now) / (before + now + O->usual[g] + 1e-6f);
        O->last[g][k] = now;
        O->span[g][k] = 0.0f;
      }
      if (O->curiosity == OL_PROGRESS) reward += O->progress[g][k];
      else if (O->curiosity == OL_ERROR) reward += O->fast[g][k] / (O->fast[g][k] + O->usual[g] + 1e-6f);
    }
    /* and what the world gives (a light, a voice answering), when it does */
    reward += O->reward_now;
    O->reward_now = 0.0f;
    O->dopamine = reward;
    for (i = 0u; i < b->n_neurons; i++) {
      if (b->neuron[i].region == OB_TEGMENTUM) O->act[i] = reward < 0.0f ? 0.0f : reward > 1.0f ? 1.0f : reward;
    }
    /* 4. the basal ganglia: better than the critic expected? the urges that
          brought it here grow on their synapses; worse, they shrink */
    if (O->curiosity != OL_NONE && O->learning) {
      float now = 0.0f, delta, norm = 1.0f;
      for (k = 0u; k < N_BG; k++) {
        i = BG[k];
        now += O->value[i] * O->act[i];
        norm += O->vtrace[i] * O->vtrace[i];
      }
      delta = O->moments > 0u ? reward + 0.9f * now - O->before : 0.0f;
      for (k = 0u; k < N_BG; k++) O->value[BG[k]] += 0.03f * delta * O->vtrace[BG[k]] / norm * 8.0f;
      for (k = 0u; k < N_ACTION; k++) {
        s = ACTION_SYN[k];
        O->w[s] += ACTOR * delta * O->elig[s] - FORGET * O->w[s];
        if (O->w[s] > 3.0f) O->w[s] = 3.0f;
        if (O->w[s] < -3.0f) O->w[s] = -3.0f;
      }
      O->before = 0.0f;
      for (k = 0u; k < N_BG; k++) O->before += O->value[BG[k]] * O->act[BG[k]];
    }
  }

  /* 5. the brainstem: each zone's urge, from the basal ganglia's synapses */
  memset(urge, 0, sizeof urge);
  for (k = 0u; k < N_ACTION; k++) {
    s = ACTION_SYN[k];
    urge[ZONE[TARGET[s]]] += O->w[s] * O->act[b->syn[s].from];
  }
  {
    float best_push = -1e30f, push[OL_MOTORS];
    int best = -1;
    for (m = 0u; m < OL_MOTORS; m++) {
      urge[m] = ZONE_N[m] ? urge[m] / (float)ZONE_N[m] * 10.0f : 0.0f;
      urge[m] = urge[m] < URGE_MIN ? URGE_MIN : urge[m] > URGE_MAX ? URGE_MAX : urge[m];
      O->fired[m] = 0u;
      push[m] = -1e30f;
    }
    /* each neuron restless of itself; a zone acts when one of its neurons,
       with the zone's urge, crosses threshold */
    for (k = 0u; k < N_STEM; k++) {
      float p;
      i = STEM[k];
      O->restless[i] = 0.8f * O->restless[i] + 0.6f * gauss(O);
      if (ZONE[i] >= OL_MOTORS) continue;
      p = O->restless[i] + urge[ZONE[i]];
      if (p > push[ZONE[i]]) push[ZONE[i]] = p;
      if (p > THETA[ZONE[i]]) O->fired[ZONE[i]] = 1u;
    }
    for (m = OL_LOOK_LEFT; m <= OL_LOOK_DOWN; m++) {
      if (O->fired[m] && push[m] > best_push) {
        best_push = push[m];
        best = (int)m;
      }
    }
    for (m = OL_LOOK_LEFT; m <= OL_LOOK_DOWN; m++) O->fired[m] = (int)m == best;
    /* deliberate acts: when the frontal cortex has an intention, its layer 5
       drives those zones and holds the restless others back */
    if (O->intent_on) {
      for (m = 0u; m < OL_MOTORS; m++) O->fired[m] = O->intent >> m & 1u;
      best = -1;
      for (m = OL_LOOK_LEFT; m <= OL_LOOK_DOWN; m++)
        if (O->fired[m]) best = (int)m;
    }
    O->gaze_move = best < 0 ? 0u : (unsigned)best + 1u;
    for (m = 0u; m < OL_MOTORS; m++) {
      O->rate[m] += 0.002f * ((float)O->fired[m] - O->rate[m]);
      if (O->fired[m]) {
        bits |= 1u << m;
        O->acts[m]++;
      }
    }
    /* the burst takes the whole zone; it spends their restlessness */
    for (k = 0u; k < N_STEM; k++) {
      i = STEM[k];
      O->act[i] = ZONE[i] < OL_MOTORS && O->fired[ZONE[i]] ? 1.0f : 0.0f;
      if (O->act[i] > 0.0f) O->restless[i] = 0.0f;
    }
    /* what each of the basal ganglia's synapses did, for dopamine to judge */
    for (k = 0u; k < N_ACTION; k++) {
      unsigned z;
      s = ACTION_SYN[k];
      z = ZONE[TARGET[s]];
      O->elig[s] = 0.8f * O->elig[s] + ((float)O->fired[z] - O->rate[z]) * O->act[b->syn[s].from];
    }
    for (k = 0u; k < N_BG; k++) O->vtrace[BG[k]] = 0.8f * O->vtrace[BG[k]] + O->act[BG[k]];
  }

  /* 6. the copy of what it just did reaches the cerebellum before the senses
        bring back what it did: the cerebellum's next prediction is made with it */
  step_only(O, gx, gy, OB_CEREBELLUM);

  /* where each sense will be, for the tegmentum: where it looks (and whether its
     eyes moved), and what it did last, within the last eight moments */
  {
    unsigned last_act = OL_MOTORS, nearest = OL_WAVE;
    for (m = 0u; m < OL_MOTORS; m++) {
      O->ago[m] = O->fired[m] ? 0u : O->ago[m] < 255u ? O->ago[m] + 1u : 255u;
      if (O->ago[m] < nearest) {
        nearest = O->ago[m];
        last_act = m;
      }
    }
    O->place[OL_SEE] = gaze_bin(gx, gy) * 2u + (O->gaze_move != 0u);
    O->place[OL_HEAR] = O->place[OL_FEEL] = O->place[OL_MOVED] = last_act;
  }

  /* 7. the hippocampus: has each sense lived this before? Then what came next
        then drives its relays as an expectation for the coming moment; and
        this moment is kept */
  {
    unsigned lately = 0u;
    for (m = 0u; m < OL_MOTORS; m++) {
      if (O->ago[m] < 3u) lately |= 1u << (3u * m + O->ago[m]);
    }
    unsigned char now[OB_HIP_BYTES];
    long episode = -1;
    hip_bits(O, now);
    if (O->episodes) episode = episode_like(O, now);
    for (g = 0u; g < OL_GROUPS; g++) {
      long then = -1;
      float near = 1.0f;
      O->recalled[g] = -1;
      O->recalled_moment[g] = -1;
      O->via[g] = 0;
      O->trusted[g] = 0;
      /* as a thing: this sense's pattern, lived before (the perirhinal way) */
      if (O->remembers) then = ol_hip_recall(&O->hip, in, g, lately, &near);
      if (then >= 0 && ol_hip_held(&O->hip, (unsigned long)then + 1u)) {
        unsigned key = ol_hip_key(in, g, lately);
        float t = O->hip.trust[g][key];
        reinstate(O, O->hip.senses[((unsigned long)then + 1u) % OL_MEMORY], g);
        O->recalled[g] = (int)key;
        O->recalled_moment[g] = then;
        O->via[g] = 1;
        O->trusted[g] = t > 0.0f || (t == 0.0f && near < OL_EXACT);
        if (O->trusted[g]) O->recalls[g]++;
      }
      /* or as an episode: the whole moment its hippocampus is reminded of */
      if (!O->trusted[g] && episode >= 0) {
        /* trust in episodes is earned, in each situation (what this sense
           brings and what it did lately), never given */
        unsigned key = ol_hip_key(in, g, lately);
        reinstate(O, O->hip.senses[((unsigned long)episode + 1u) % OL_MEMORY], g);
        O->recalled_moment[g] = episode;
        O->via[g] = 2;
        O->episode_key[g] = key;
        O->trusted[g] = O->episode_trust[g][key] > 0.0f;
        if (O->trusted[g]) O->episode_recalls[g]++;
      }
    }
    if (O->remembers || O->episodes) {
      ol_hip_keep(&O->hip, O->moments, in, lately);
      memcpy(O->pattern[O->moments % OL_MEMORY], now, OB_HIP_BYTES);
    }
  }
  O->moments++;
  return bits;
}

const float *ob_one_drive(void) { return DRIVE; }
const float *ob_one_pred(void) { return PRED; }
const float *ob_one_pred_cerebellum(void) { return PRED_CB; }
