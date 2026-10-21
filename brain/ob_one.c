/*
 * ob_one.c -- one brain: the grown brain, living. See ob_one.h.
 */

#include "ob_one.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---- its anatomy, read once: who sends to whom, and what each rule is for ---- */

enum { C_OTHER, C_SENSE, C_PRED, C_ACTION };   /* what a synapse is, by the rule that grew it */

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
static float DRIVE[OB_MAX_NEURONS], PRED[OB_MAX_NEURONS], INPUT[OB_MAX_NEURONS], PRED_CB[OB_MAX_NEURONS];
static float NEXT[OB_MAX_NEURONS];
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
  N_ACTION = N_BG = N_THAL = N_STEM = N_CB = 0u;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (n->region == OB_BASAL_GANGLIA) BG[N_BG++] = i;
    if (n->region == OB_BRAINSTEM) STEM[N_STEM++] = i;
    if (n->region == OB_CEREBELLUM) CB_INDEX[i] = N_CB < 4096u ? N_CB++ : 4095u;
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

static void senses(ob_one_t *O, const float *in) {
  const ob_brain_t *b = &O->brain;
  unsigned i;
  float gx = in[OL_SENSE], gy = in[OL_SENSE + 1];
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    switch (n->region) {
    case OB_RETINA_L:
    case OB_RETINA_R: {
      unsigned x = (unsigned)(n->sx * OL_EYE), y = (unsigned)(n->sy * OL_EYE);
      O->act[i] = in[y * OL_EYE + x];
      break;
    }
    case OB_COCHLEA_L:
    case OB_COCHLEA_R: O->act[i] = in[OL_VIS + (unsigned)(n->sx * OL_EAR)]; break;
    case OB_VESTIBULE_L:
    case OB_VESTIBULE_R: O->act[i] = in[OL_VIS + OL_EAR + OL_SKIN + (unsigned)(n->sx * OL_MOVE)]; break;
    case OB_SPINAL_SENSORY: {
      /* its skin: eight places, four down each side of its body (its face, the
         first tenth, is left to the womb's hand and mouth) */
      int place = n->sx < 0.1f ? -1 : (int)((n->sx - 0.1f) / 0.225f);
      if (place > 3) place = 3;
      O->act[i] = place < 0 ? 0.0f : in[OL_VIS + OL_EAR + (unsigned)place + 4u * n->side];
      break;
    }
    default: break;
    }
  }
  (void)gx;
  (void)gy;
}

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
      if (b->neuron[i].region == region && b->neuron[i].side == side) vals[n++] = NEXT[i];
    }
    if (n == 0u) continue;
    qsort(vals, n, sizeof(float), by_value);
    cut = vals[(unsigned)(share * n)];
    for (i = 0u; i < b->n_neurons; i++) {
      if (b->neuron[i].region == region && b->neuron[i].side == side && NEXT[i] <= cut) NEXT[i] = 0.0f;
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
    float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};   /* by class: other, sense, prediction, action */
    unsigned s, end = n->syn_first + n->syn_n;
    if (FIXED[i] || (only >= 0 && n->region != (unsigned)only) || (only < 0 && n->region == OB_CEREBELLUM)) continue;
    for (s = n->syn_first; s < end; s++) acc[SCLASS[s]] += O->w[s] * O->act[FROM[s]];
    if (n->region == OB_THALAMUS) {
      float cb = 0.0f;
      for (s = n->syn_first; s < end; s++)
        if (SCLASS[s] == C_PRED && O->brain.neuron[FROM[s]].region == OB_CEREBELLUM) cb += O->w[s] * O->act[FROM[s]];
      PRED_CB[i] = cb;
    }
    /* (the basal ganglia's say on the brainstem is read as an urge, not here) */
    INPUT[i] = acc[C_OTHER] + acc[C_SENSE];
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
    NEXT[i] = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
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
  /* the cerebellum's granule cells are sparse: of each side, only the most
     strongly driven fifth fire */
  if (only == OB_CEREBELLUM) sparse(b, OB_CEREBELLUM, 0.2f);
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
  for (k = 0u; k < N_THAL; k++) {
    float left;
    i = THAL[k];
    /* what it passes on is what came and was not expected: a neuron cannot fire
       below nothing (expected sounds that did not come are another signal, the
       womb's signal of absence) */
    left = SENT(DRIVE[i]) + PRED[i];
    if (left < 0.0f) left = 0.0f;
    err[GROUP[i]] += left * left;
    energy[GROUP[i]] += SENT(DRIVE[i]) * SENT(DRIVE[i]);
    n_err[GROUP[i]]++;
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
  O->moments++;
  return bits;
}

const float *ob_one_drive(void) { return DRIVE; }
const float *ob_one_pred(void) { return PRED; }
const float *ob_one_pred_cerebellum(void) { return PRED_CB; }
