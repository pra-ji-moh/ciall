/*
 * ob_womb.c -- stage 1: the being in the womb, touch first. See ob.h.
 *
 * Its body: the spinal cord's sensory neurons are its skin and its motor
 * neurons its muscles, each at the place on the body its spinal segment
 * serves (along the body, head to tail; around it; on its own side).
 */

#include "ob.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float ACT0[OB_MAX_NEURONS], ACT1[OB_MAX_NEURONS], ACT2[OB_MAX_NEURONS], ACT3[OB_MAX_NEURONS];
static float INPUT[OB_MAX_NEURONS], DRIVE[OB_MAX_NEURONS], PRED[OB_MAX_NEURONS];
static float W[OB_MAX_SYN];                 /* weights while it lives; written back after */
static unsigned TARGET[OB_MAX_SYN];
static unsigned OUT_FIRST[OB_MAX_NEURONS + 1u], OUT_SYN[OB_MAX_SYN];
static unsigned PRED_RULE = 0xFFu, TIMING_RULE = 0xFFu;
static unsigned DRIVE_REGION = OB_SPINAL_SENSORY;   /* whose arrivals count as the sense's drive */

static unsigned RNG;

static unsigned rnd(void) {
  unsigned x = RNG ? RNG : 2463534242u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  RNG = x;
  return x;
}

static double unit(void) { return (double)(rnd() >> 8) / 16777216.0; }

/* near enough to a normal deviate */
static double gauss(void) { return (unit() + unit() + unit() + unit() - 2.0) * 1.7320508; }

/* ---- the brainstem, restless of itself ------------------------------------------
 * Nobody decides where it moves. Each brainstem neuron's activity wanders of
 * itself; when one's crosses threshold it fires, and its burst spreads through
 * its near neighbours on the same side, which is a twitch. Where and when the
 * twitches come is wherever the neurons' own activity takes them. */

static float RESTLESS[OB_MAX_NEURONS];
#define IGNITE 2.9

static unsigned ignite(const ob_brain_t *b, float *act) {
  unsigned i, j, n = 0u;
  memset(act, 0, sizeof(float) * b->n_neurons);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *a = &b->neuron[i];
    if (a->region != OB_BRAINSTEM) continue;
    RESTLESS[i] = (float)(0.8 * RESTLESS[i] + 0.6 * gauss());
    if (RESTLESS[i] < IGNITE) continue;
    for (j = 0u; j < b->n_neurons; j++) {
      const ob_neuron_t *c = &b->neuron[j];
      if (c->region == OB_BRAINSTEM && c->side == a->side && fabs(c->sx - a->sx) < 0.12 && fabs(c->sy - a->sy) < 0.3) {
        act[j] = 1.0f;
        RESTLESS[j] = 0.0f;   /* a burst spends it */
      }
    }
    n++;
  }
  return n;
}

/* for examining it afterwards only: a twitch set off at (tx, ty) on side ts,
   the way a neuroscientist stimulates a spot of the brainstem */
static void probe_twitch(const ob_brain_t *b, double tx, double ty, unsigned ts) {
  unsigned i;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    ACT0[i] = n->region == OB_BRAINSTEM && n->side == ts && fabs(n->sx - tx) < 0.12 && fabs(n->sy - ty) < 0.3;
  }
}

static int is_touch_thalamus(const ob_neuron_t *n) { return n->region == OB_THALAMUS && n->layer == 1u; }

/* where on the body a touch-thalamus neuron's skin is: along the body */
static double thal_body(const ob_neuron_t *n) { return n->sx; }

static void prepare(const ob_brain_t *b) {
  unsigned i, s, k;
  memset(OUT_FIRST, 0, sizeof OUT_FIRST);
  for (k = 0u; k < 64u; k++) {
    if (strncmp(ob_rule_name(k), "prediction:", 11) == 0) PRED_RULE = k;
    if (strncmp(ob_rule_name(k), "timing:", 7) == 0) TIMING_RULE = k;
  }
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      TARGET[s] = i;
      W[s] = (float)b->syn[s].weight / 1000.0f;
      OUT_FIRST[b->syn[s].from + 1u]++;
    }
  }
  for (i = 0u; i < b->n_neurons; i++) OUT_FIRST[i + 1u] += OUT_FIRST[i];
  {
    static unsigned fill[OB_MAX_NEURONS + 1u];
    memcpy(fill, OUT_FIRST, sizeof(unsigned) * (b->n_neurons + 1u));
    for (s = 0u; s < b->n_syn; s++) OUT_SYN[fill[b->syn[s].from]++] = s;
  }
}

/* One step of the whole brain: every neuron takes the sum of what its inputs
   carry, clipped to 0..1. Only active neurons send, so a quiet brain is cheap.
   For the touch thalamus, the skin's drive and the cerebellum's prediction are
   also kept apart, to measure and to learn from. */
static void step(const ob_brain_t *b, const float *in, float *out) {
  unsigned j, k, i;
  memset(INPUT, 0, sizeof(float) * b->n_neurons);
  memset(DRIVE, 0, sizeof(float) * b->n_neurons);
  memset(PRED, 0, sizeof(float) * b->n_neurons);
  for (j = 0u; j < b->n_neurons; j++) {
    float a = in[j];
    if (a <= 0.0f) continue;
    for (k = OUT_FIRST[j]; k < OUT_FIRST[j + 1u]; k++) {
      unsigned s = OUT_SYN[k], t = TARGET[s];
      float v = W[s] * a;
      INPUT[t] += v;
      if (b->syn[s].rule == PRED_RULE || b->syn[s].rule == TIMING_RULE) PRED[t] += v;
      else if (b->neuron[j].region == DRIVE_REGION) DRIVE[t] += v;
    }
  }
  for (i = 0u; i < b->n_neurons; i++) out[i] = INPUT[i] < 0.0f ? 0.0f : INPUT[i] > 1.0f ? 1.0f : INPUT[i];
}

typedef struct {
  double drive, left, face_drive, face_left, parietal;
  int double_touch;
} ob_moment_t;

/*
 * One moment of life. ACT0 holds what its brainstem did of itself (ignite).
 * poke: the mother presses at (px, py) on side ps. learn: the cerebellum
 * learns from it.
 */
static void moment(ob_brain_t *b, int poke, double px, double py, unsigned ps, int learn, ob_moment_t *m) {
  unsigned i;
  memset(m, 0, sizeof(*m));
  /* 1: the twitch reaches the muscles, and its copy the cerebellum */
  step(b, ACT0, ACT1);
  /* 2: the body: what the muscles press, what the arm brushes, what the mother presses */
  {
    static float touch[OB_MAX_NEURONS];
    unsigned j;
    memset(touch, 0, sizeof(float) * b->n_neurons);
    for (j = 0u; j < b->n_neurons; j++) {
      const ob_neuron_t *mus = &b->neuron[j];
      if (mus->region != OB_SPINAL_MOTOR || ACT1[j] < 0.3f) continue;
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *sk = &b->neuron[i];
        if (sk->region != OB_SPINAL_SENSORY || sk->side != mus->side) continue;
        if (fabs(sk->sx - mus->sx) < 0.07 && fabs(sk->sy - mus->sy) < 0.2) touch[i] = 1.0f;
        /* an arm moving (the front of the body) brushes the face */
        if (mus->sx > 0.15 && mus->sx < 0.35 && sk->sx < 0.1 && fabs(sk->sy - mus->sy) < 0.25) {
          touch[i] = 1.0f;
          m->double_touch = 1;
        }
      }
    }
    if (poke) {
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *sk = &b->neuron[i];
        if (sk->region == OB_SPINAL_SENSORY && sk->side == ps && fabs(sk->sx - px) < 0.1 &&
            fabs(sk->sy - py) < 0.3)
          touch[i] = 1.0f;
      }
    }
    for (i = 0u; i < b->n_neurons; i++) {
      if (b->neuron[i].region == OB_SPINAL_SENSORY) ACT1[i] = touch[i];
    }
  }
  /* the skin reaches the thalamus, and the cerebellum's prediction with it */
  step(b, ACT1, ACT2);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (!is_touch_thalamus(n) || DRIVE[i] <= 0.0f) continue;
    {
      /* against the touch's own ceiling: how much of what the skin sent is left */
      double sent = DRIVE[i] > 1.0f ? 1.0 : DRIVE[i], left = DRIVE[i] + PRED[i];
      if (left < 0.0) left = 0.0;
      if (left > 1.0) left = 1.0;
      m->drive += sent;
      m->left += left;
      if (thal_body(n) < 0.12) {
        m->face_drive += sent;
        m->face_left += left;
      }
    }
  }
  /* the cerebellum learns: each prediction connection moves to cancel what its
     neuron received from the skin, in proportion to how active its source was
     (the delta rule, normalised) */
  if (learn) {
    for (i = 0u; i < b->n_neurons; i++) {
      const ob_neuron_t *n = &b->neuron[i];
      unsigned s;
      double err, norm = 0.01;
      if (!is_touch_thalamus(n)) continue;
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        float a = ACT1[b->syn[s].from];
        if (b->syn[s].rule == PRED_RULE && a > 0.0f) norm += (double)a * a;
      }
      if (norm <= 0.01) continue;
      err = (double)DRIVE[i] + (double)PRED[i];
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        float a = ACT1[b->syn[s].from];
        if (b->syn[s].rule != PRED_RULE || a <= 0.0f) continue;
        W[s] -= (float)(0.3 * err * a / norm);
        if (W[s] < -5.0f) W[s] = -5.0f;
        if (W[s] > 1.0f) W[s] = 1.0f;
      }
    }
  }
  /* 3: the thalamus reaches the parietal cortex */
  step(b, ACT2, ACT3);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (n->region == OB_PARIETAL && n->layer == 2u) m->parietal += ACT3[i];
  }
}

void ob_womb_touch(ob_brain_t *b, unsigned days, unsigned moments_per_day, ob_womb_stats_t *out) {
  unsigned d, t, s;
  if (b == 0 || out == 0) return;
  memset(out, 0, sizeof(*out));
  if (days > 64u) days = 64u;
  RNG = b->genes.seed * 1597334677u + 3u;
  memset(RESTLESS, 0, sizeof RESTLESS);
  prepare(b);
  DRIVE_REGION = OB_SPINAL_SENSORY;
  for (d = 0u; d < days; d++) {
    double sd = 0, sl = 0, od = 0, ol = 0, fd = 0, fl = 0, sp = 0, op = 0;
    for (t = 0u; t < moments_per_day; t++) {
      ob_moment_t m;
      /* it twitches when its brainstem does; its mother presses when she does */
      int twitch = ignite(b, ACT0) > 0u, poke = unit() < 0.35;
      double px = unit(), py = unit();
      unsigned ps = rnd() & 1u;
      moment(b, poke, px, py, ps, 1, &m);
      if (m.drive <= 0.0 || (twitch && poke)) continue;   /* both at once: lived, not counted */
      if (twitch) {
        sd += m.drive;
        sl += m.left;
        sp += m.parietal;
        out->self_n[d]++;
        if (m.double_touch) {
          fd += m.face_drive;
          fl += m.face_left;
        }
      } else {
        od += m.drive;
        ol += m.left;
        op += m.parietal;
        out->other_n[d]++;
      }
    }
    out->self_left[d] = sd > 0 ? sl / sd : 0;
    out->other_left[d] = od > 0 ? ol / od : 0;
    out->double_left[d] = fd > 0 ? fl / fd : 0;
    out->parietal_self[d] = sd > 0 ? sp / sd : 0;
    out->parietal_other[d] = od > 0 ? op / od : 0;
  }
  out->days = days;
  /* what it learned is written into the brain, readable like any connection */
  for (s = 0u; s < b->n_syn; s++) {
    if (b->syn[s].rule == PRED_RULE) b->syn[s].weight = (short)lrint(W[s] * 1000.0f);
  }
}

void ob_write_womb(const ob_womb_stats_t *st, FILE *out) {
  unsigned d;
  if (st == 0 || out == 0) return;
  fprintf(out, "\n## the womb: touch, day by day\n");
  fprintf(out, "## share of touch still felt after the cerebellum's prediction: its own (from its twitches),\n");
  fprintf(out, "## the mother's (from outside), and hand-to-face; and the parietal response per unit of touch\n");
  fprintf(out, "   day   own    mother's  hand-to-face   parietal own / mother's\n");
  for (d = 0u; d < st->days; d++) {
    fprintf(out, "   %3u   %.3f  %.3f     %.3f          %.3f / %.3f\n", d, st->self_left[d], st->other_left[d],
            st->double_left[d], st->parietal_self[d], st->parietal_other[d]);
  }
  if (st->days > 0u) {
    fprintf(out, "   it now feels %.0f%% of its own touch and %.0f%% of the mother's: what it learned to cancel is \"me\"\n",
            100.0 * st->self_left[st->days - 1u], 100.0 * st->other_left[st->days - 1u]);
  }
}

/* ---- smell: good or bad for me ------------------------------------------------ */

static const char *const SMELL_NAME[OB_SMELLS] = {"the mother herself", "anise (in her meals)", "smell 2", "smell 3",
                                                  "smell 4", "stress (her cortisol)", "smell 6", "smell 7"};

/* the olfactory bulb's patch for a smell: its place across the bulb */
static int smells(const ob_neuron_t *n, unsigned k) {
  return n->region == OB_OLFACTORY && n->sy >= (float)k / OB_SMELLS && n->sy < (float)(k + 1u) / OB_SMELLS;
}

static int approach_neuron(const ob_neuron_t *n) { return n->region == OB_AMYGDALA && n->sy < 0.5f; }
static int avoid_neuron(const ob_neuron_t *n) { return n->region == OB_AMYGDALA && n->sy >= 0.5f; }

/* How the amygdala answers a smell: approach minus avoidance, by what reaches each */
static double valence(const ob_brain_t *b, unsigned k) {
  unsigned i, s;
  double ap = 0.0, av = 0.0;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    double in = 0.0;
    if (!approach_neuron(n) && !avoid_neuron(n)) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      if (smells(&b->neuron[b->syn[s].from], k)) in += W[s];
    }
    if (approach_neuron(n)) ap += in;
    else av += in;
  }
  return ap + av > 0.0 ? (ap - av) / (ap + av) : 0.0;
}

void ob_womb_smell(ob_brain_t *b, unsigned days, unsigned moments_per_day, ob_smell_stats_t *out) {
  unsigned d, t, k, i, s;
  if (b == 0 || out == 0) return;
  memset(out, 0, sizeof(*out));
  if (days > 64u) days = 64u;
  RNG = b->genes.seed * 3266489917u + 5u;
  prepare(b);
  for (k = 0u; k < OB_SMELLS; k++) out->before[k] = valence(b, k);
  for (d = 0u; d < days; d++) {
    for (t = 0u; t < moments_per_day; t++) {
      /* what the amniotic fluid carries this moment, and the taste that comes with it */
      double r = unit(), taste = 0.0;
      unsigned smell;
      if (r < 0.30) {
        smell = 0u;   /* the mother's own smell, with the sweetness of her milk-to-be */
        taste = 0.5;
      } else if (r < 0.50) {
        smell = 1u;   /* anise in her meal: sweet */
        taste = 1.0;
      } else if (r < 0.62) {
        smell = 5u;   /* her stress: bitter */
        taste = -1.0;
      } else {
        smell = 2u + rnd() % 6u;   /* everything else she eats or breathes: no taste of note */
        if (smell == 5u) smell = 7u;
      }
      if (taste == 0.0) continue;   /* only a taste's value teaches */
      /* three-factor learning: the smell's connections to the neurons of the
         taste's kind strengthen; to the other kind, weaken a little */
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *n = &b->neuron[i];
        int ap = approach_neuron(n), av = avoid_neuron(n);
        if (!ap && !av) continue;
        for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
          if (!smells(&b->neuron[b->syn[s].from], smell)) continue;
          if ((ap && taste > 0.0) || (av && taste < 0.0)) W[s] += (float)(0.02 * (taste < 0 ? -taste : taste));
          else W[s] -= (float)(0.01 * (taste < 0 ? -taste : taste));
          if (W[s] < 0.0f) W[s] = 0.0f;
          if (W[s] > 3.0f) W[s] = 3.0f;
        }
      }
    }
    out->day_mother[d] = valence(b, 0u);
    out->day_anise[d] = valence(b, 1u);
    out->day_stress[d] = valence(b, 5u);
  }
  out->days = days;
  for (k = 0u; k < OB_SMELLS; k++) out->after[k] = valence(b, k);
  for (s = 0u; s < b->n_syn; s++) {
    if (b->neuron[b->syn[s].from].region == OB_OLFACTORY) b->syn[s].weight = (short)lrint(W[s] * 1000.0f);
  }
}

void ob_write_smell(const ob_smell_stats_t *st, FILE *out) {
  unsigned k;
  if (st == 0 || out == 0) return;
  fprintf(out, "\n## the womb: smell, good or bad for me\n");
  fprintf(out, "## the amygdala's answer to each smell, approach minus avoidance (-1 to 1), before and after\n");
  for (k = 0u; k < OB_SMELLS; k++) {
    fprintf(out, "   %-24s %+.3f  ->  %+.3f\n", SMELL_NAME[k], st->before[k], st->after[k]);
  }
}

/* ---- hearing: the heartbeat ---------------------------------------------------- */

#define OB_WAVE 12u   /* how many moments back the wave across the cerebellum reaches */

static int hearing_thalamus(const ob_neuron_t *n) { return n->region == OB_THALAMUS && n->layer == 2u; }

/* One moment of hearing. heard: which sounds reach the ears now (bit 0 low,
   the heartbeat; bit 1 middle, the voice). ages: for each moment back, whether
   a low sound came then (the wave). Returns how much of what arrived is left
   after the prediction, how much arrived, and how much prediction met nothing. */
static void hear(ob_brain_t *b, unsigned heard, const unsigned char *ages, int learn, double *arrived,
                 double *left, double *unmet) {
  unsigned i, s;
  *arrived = *left = *unmet = 0.0;
  memset(ACT0, 0, sizeof(float) * b->n_neurons);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (n->region == OB_COCHLEA_L || n->region == OB_COCHLEA_R) {
      if ((heard & 1u) && n->sx < 0.15f) ACT0[i] = 1.0f;
      if ((heard & 2u) && n->sx > 0.35f && n->sx < 0.65f) ACT0[i] = 1.0f;
    }
  }
  /* the cochlea reaches the tectum; then the tectum, the thalamus. The wave is
     on the cerebellum throughout: granule cell k of each column fires k moments
     after a low sound (a column's cells are its birth order: one per moment) */
  DRIVE_REGION = OB_TECTUM;
  step(b, ACT0, ACT1);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    if (n->region == OB_CEREBELLUM) ACT1[i] = (n->birth < OB_WAVE && ages[n->birth]) ? 1.0f : 0.0f;
  }
  step(b, ACT1, ACT2);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    double sent, l;
    if (!hearing_thalamus(n)) continue;
    sent = DRIVE[i] > 1.0f ? 1.0 : DRIVE[i];
    l = DRIVE[i] + PRED[i];
    if (l < -1.0) l = -1.0;
    if (l < 0.0) *unmet += -l;
    *left += l < 0.0 ? 0.0 : l > 1.0 ? 1.0 : l;
    *arrived += sent;
  }
  if (learn) {
    for (i = 0u; i < b->n_neurons; i++) {
      const ob_neuron_t *n = &b->neuron[i];
      double err, norm = 0.01;
      if (!hearing_thalamus(n)) continue;
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        float a = ACT1[b->syn[s].from];
        if (b->syn[s].rule == TIMING_RULE && a > 0.0f) norm += (double)a * a;
      }
      if (norm <= 0.01) continue;
      err = (double)DRIVE[i] + (double)PRED[i];
      if (err > 1.0) err = 1.0;   /* a sound's drive counts up to its ceiling */
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        float a = ACT1[b->syn[s].from];
        if (b->syn[s].rule != TIMING_RULE || a <= 0.0f) continue;
        W[s] -= (float)(0.2 * err * a / norm);
        if (W[s] < -5.0f) W[s] = -5.0f;
        if (W[s] > 1.0f) W[s] = 1.0f;
      }
    }
  }
}

void ob_womb_heartbeat(ob_brain_t *b, unsigned days, unsigned moments_per_day, ob_heart_stats_t *out) {
  unsigned char ages[OB_WAVE];
  unsigned d, t, k, s, clock = 0u;
  if (b == 0 || out == 0) return;
  memset(out, 0, sizeof(*out));
  if (days > 64u) days = 64u;
  RNG = b->genes.seed * 2654435789u + 9u;
  prepare(b);
  memset(ages, 0, sizeof ages);
  for (d = 0u; d < days; d++) {
    double ha = 0, hl = 0, va = 0, vl = 0;
    for (t = 0u; t < moments_per_day; t++, clock++) {
      /* the mother's heart: lub, then dub two moments later, every eight moments */
      unsigned beat = (clock % 8u == 0u || clock % 8u == 2u) ? 1u : 0u;
      unsigned voice = unit() < 0.15 ? 2u : 0u;
      double ar, le, un;
      hear(b, beat | voice, ages, 1, &ar, &le, &un);
      if (beat && !voice) {
        ha += ar;
        hl += le;
      } else if (voice && !beat) {
        va += ar;
        vl += le;
      }
      /* the wave moves on one step; a low sound now starts it again at the beginning */
      for (k = OB_WAVE - 1u; k > 0u; k--) ages[k] = ages[k - 1u];
      ages[0] = (unsigned char)(beat ? 1u : 0u);
    }
    out->beat_left[d] = ha > 0 ? hl / ha : 0;
    out->voice_left[d] = va > 0 ? vl / va : 0;
  }
  out->days = days;
  /* after: a beat that should come and does not; and the same moment with the beat */
  {
    double ar, le, un, ar2, le2, un2;
    unsigned char a2[OB_WAVE];
    /* ages as they are just before a lub: the last lub 8 moments ago, the dub 6 */
    memset(a2, 0, sizeof a2);
    a2[7] = 1u;
    a2[5] = 1u;
    hear(b, 0u, a2, 0, &ar, &le, &un);
    hear(b, 1u, a2, 0, &ar2, &le2, &un2);
    /* both as a share of what a real beat brings */
    out->missed_beat = ar2 > 0 ? un / ar2 : 0;
    out->expected_beat_left = ar2 > 0 ? le2 / ar2 : 0;
    memset(a2, 0, sizeof a2);
    hear(b, 0u, a2, 0, &ar, &le, &un);
    out->silence = ar2 > 0 ? un / ar2 : 0;
  }
  for (s = 0u; s < b->n_syn; s++) {
    if (b->syn[s].rule == TIMING_RULE) b->syn[s].weight = (short)lrint(W[s] * 1000.0f);
  }
  DRIVE_REGION = OB_SPINAL_SENSORY;
}

void ob_write_heart(const ob_heart_stats_t *st, FILE *out) {
  if (st == 0 || out == 0 || st->days == 0u) return;
  fprintf(out, "\n## the womb: hearing the heartbeat\n");
  fprintf(out, "   share of the heartbeat still heard after the cerebellum's timing: day 0 %.3f, last %.3f\n",
          st->beat_left[0], st->beat_left[st->days - 1u]);
  fprintf(out, "   share of the mother's voice (irregular) still heard: day 0 %.3f, last %.3f\n", st->voice_left[0],
          st->voice_left[st->days - 1u]);
  fprintf(out, "   a beat that should come and does not: a signal %.0f%% as strong as a beat, where there is nothing"
               " (in plain silence: %.0f%%)\n",
          100.0 * st->missed_beat, 100.0 * st->silence);
}

/* ---- vision in the dark: retinal waves ------------------------------------------ */

static int retina(const ob_neuron_t *n) { return n->region == OB_RETINA_L || n->region == OB_RETINA_R; }
static int visual_thalamus(const ob_neuron_t *n) { return n->region == OB_THALAMUS && n->layer == 3u; }

/* Mean receptive field size of the visual thalamus: the weighted spread of the
   retina cells each one hears from, in retina cells. */
static double field_size(const ob_brain_t *b) {
  unsigned i, s, n_cells = 0u;
  double total = 0.0;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    double w = 0, mx = 0, my = 0, vx = 0, vy = 0;
    if (!visual_thalamus(n)) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      const ob_neuron_t *src = &b->neuron[b->syn[s].from];
      if (!retina(src) || W[s] <= 0.0f) continue;
      w += W[s];
      mx += W[s] * src->sx;
      my += W[s] * src->sy;
    }
    if (w <= 0.0) continue;
    mx /= w;
    my /= w;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      const ob_neuron_t *src = &b->neuron[b->syn[s].from];
      if (!retina(src) || W[s] <= 0.0f) continue;
      vx += W[s] * (src->sx - mx) * (src->sx - mx);
      vy += W[s] * (src->sy - my) * (src->sy - my);
    }
    total += sqrt((vx + vy) / w) * OB_RETINA;
    n_cells++;
  }
  return n_cells ? total / n_cells : 0.0;
}

void ob_womb_vision(ob_brain_t *b, unsigned days, unsigned moments_per_day, int waves, ob_vision_stats_t *out) {
  static double total_in[OB_MAX_NEURONS];
  unsigned d, t, i, s;
  double cx[2] = {0, 0}, cy[2] = {0, 0}, vx[2] = {0, 0}, vy[2] = {0, 0};
  unsigned life[2] = {0u, 0u};
  if (b == 0 || out == 0) return;
  memset(out, 0, sizeof(*out));
  if (days > 64u) days = 64u;
  RNG = b->genes.seed * 668265263u + 13u;
  prepare(b);
  DRIVE_REGION = OB_RETINA_L;
  /* each visual-thalamus neuron's total input from the eye, kept constant */
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *n = &b->neuron[i];
    total_in[i] = 0.0;
    if (!visual_thalamus(n)) continue;
    for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
      if (retina(&b->neuron[b->syn[s].from])) total_in[i] += W[s];
    }
  }
  out->field_before = field_size(b);
  for (d = 0u; d < days; d++) {
    for (t = 0u; t < moments_per_day; t++) {
      unsigned e;
      memset(ACT0, 0, sizeof(float) * b->n_neurons);
      for (e = 0u; e < 2u; e++) {
        /* each eye has its own waves: a patch that starts somewhere and travels */
        if (life[e] == 0u) {
          double a = unit() * 6.2831853;
          cx[e] = unit();
          cy[e] = unit();
          vx[e] = cos(a) * 0.045;
          vy[e] = sin(a) * 0.045;
          life[e] = 6u + rnd() % 6u;
        }
        cx[e] += vx[e];
        cy[e] += vy[e];
        life[e]--;
      }
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *n = &b->neuron[i];
        unsigned e2;
        if (!retina(n)) continue;
        e2 = n->region == OB_RETINA_L ? 0u : 1u;
        if (waves) {
          double dx = n->sx - cx[e2], dy = n->sy - cy[e2];
          if (dx * dx + dy * dy < 0.011) ACT0[i] = 1.0f;   /* a patch about five cells across */
        } else if (unit() < 0.06) {
          ACT0[i] = 1.0f;   /* the control: the same amount of activity, no neighbour with its neighbour */
        }
      }
      step(b, ACT0, ACT1);
      /* Hebb, then each neuron's total from the eye scaled back to what it was */
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *n = &b->neuron[i];
        double post = ACT1[i], sum = 0.0;
        if (!visual_thalamus(n) || post <= 0.0) continue;
        for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
          if (!retina(&b->neuron[b->syn[s].from]) || W[s] <= 0.0f) continue;
          W[s] += (float)(0.2 * post * ACT0[b->syn[s].from]);
          sum += W[s];
        }
        if (sum <= 0.0) continue;
        for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
          if (!retina(&b->neuron[b->syn[s].from]) || W[s] <= 0.0f) continue;
          W[s] = (float)(W[s] * total_in[i] / sum);
        }
      }
    }
    /* what has fallen below a tenth of its neuron's average is pruned: gone */
    for (i = 0u; i < b->n_neurons; i++) {
      const ob_neuron_t *n = &b->neuron[i];
      unsigned n_in = 0u;
      if (!visual_thalamus(n)) continue;
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        if (retina(&b->neuron[b->syn[s].from]) && W[s] > 0.0f) n_in++;
      }
      for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
        if (retina(&b->neuron[b->syn[s].from]) && W[s] > 0.0f && n_in > 0u && W[s] < 0.25 * total_in[i] / n_in)
          W[s] = 0.0f;
      }
    }
    out->field_day[d] = field_size(b);
  }
  out->days = days;
  out->field_after = field_size(b);
  for (s = 0u; s < b->n_syn; s++) {
    if (!retina(&b->neuron[b->syn[s].from]) || !visual_thalamus(&b->neuron[TARGET[s]])) continue;
    b->syn[s].weight = (short)lrint(W[s] * 1000.0f);
    if (b->syn[s].weight == 0) out->pruned++;
    else out->kept++;
  }
  DRIVE_REGION = OB_SPINAL_SENSORY;
}

void ob_write_vision(const ob_vision_stats_t *st, FILE *out) {
  if (st == 0 || out == 0 || st->days == 0u) return;
  fprintf(out, "\n## the womb: vision in the dark, retinal waves\n");
  fprintf(out, "   receptive field of the visual thalamus (retina cells across): %.2f before the waves, %.2f after\n",
          st->field_before, st->field_after);
  fprintf(out, "   the eyes' connections to the thalamus: %u kept, %u pruned\n", st->kept, st->pruned);
}

/* ---- the reflex loop: practised before birth ------------------------------------ */

static unsigned REFLEX_RULE = 0xFFu;

/* The drive that reaches muscles near `at` (along the body) when the skin near
   `at` is touched, through the reflex connections alone. */
static double reflex_drive(const ob_brain_t *b, double at) {
  unsigned i, s, n = 0u;
  double total = 0.0;
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *m = &b->neuron[i];
    if (m->region != OB_SPINAL_MOTOR || fabs(m->sx - at) > 0.06) continue;
    for (s = m->syn_first; s < m->syn_first + m->syn_n; s++) {
      const ob_neuron_t *src = &b->neuron[b->syn[s].from];
      if (b->syn[s].rule == REFLEX_RULE && src->region == OB_SPINAL_SENSORY && fabs(src->sx - at) < 0.06)
        total += W[s];
    }
    n++;
  }
  return n ? total / n : 0.0;
}

void ob_womb_reflex(ob_brain_t *b, unsigned days, unsigned moments_per_day, int arms_free, ob_reflex_stats_t *out) {
  static float touch[OB_MAX_NEURONS], muscle[OB_MAX_NEURONS];
  unsigned d, t, i, j, s, k;
  if (b == 0 || out == 0) return;
  memset(out, 0, sizeof(*out));
  if (days > 64u) days = 64u;
  RNG = b->genes.seed * 374761393u + 17u;
  memset(RESTLESS, 0, sizeof RESTLESS);
  prepare(b);
  for (k = 0u; k < 64u; k++) {
    if (strncmp(ob_rule_name(k), "spinal reflex", 13) == 0) REFLEX_RULE = k;
  }
  out->mouth_before = reflex_drive(b, 0.04);
  out->body_before = reflex_drive(b, 0.5);
  for (d = 0u; d < days; d++) {
    for (t = 0u; t < moments_per_day; t++) {
      /* its brainstem twitches, of itself, and the twitch reaches its muscles */
      if (ignite(b, ACT0) == 0u) continue;
      step(b, ACT0, ACT1);
      /* the body: a contracting muscle presses the wall there; a moving arm
         (if free) brushes the face */
      memset(touch, 0, sizeof(float) * b->n_neurons);
      for (j = 0u; j < b->n_neurons; j++) {
        const ob_neuron_t *mus = &b->neuron[j];
        if (mus->region != OB_SPINAL_MOTOR || ACT1[j] < 0.3f) continue;
        for (i = 0u; i < b->n_neurons; i++) {
          const ob_neuron_t *sk = &b->neuron[i];
          if (sk->region != OB_SPINAL_SENSORY || sk->side != mus->side) continue;
          if (fabs(sk->sx - mus->sx) < 0.07 && fabs(sk->sy - mus->sy) < 0.2) touch[i] = 1.0f;
          if (arms_free && mus->sx > 0.15 && mus->sx < 0.35 && sk->sx < 0.1 && fabs(sk->sy - mus->sy) < 0.25)
            touch[i] = 1.0f;
        }
      }
      /* the touched skin drives the muscles back, through the reflex: each
         muscle is what its twitch and its reflex bring together */
      step(b, touch, ACT2);
      for (i = 0u; i < b->n_neurons; i++) {
        float m = ACT1[i] + ACT2[i];
        muscle[i] = b->neuron[i].region == OB_SPINAL_MOTOR ? (m > 1.0f ? 1.0f : m) : 0.0f;
      }
      /* Hebb: skin and muscle active together, the reflex between them grows */
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *m = &b->neuron[i];
        if (m->region != OB_SPINAL_MOTOR || muscle[i] < 0.3f) continue;
        for (s = m->syn_first; s < m->syn_first + m->syn_n; s++) {
          if (b->syn[s].rule != REFLEX_RULE || touch[b->syn[s].from] <= 0.0f) continue;
          W[s] += 0.01f * muscle[i];
          if (W[s] > 3.0f) W[s] = 3.0f;
        }
      }
    }
  }
  out->days = days;
  out->mouth_after = reflex_drive(b, 0.04);
  out->body_after = reflex_drive(b, 0.5);
  for (s = 0u; s < b->n_syn; s++) {
    if (b->syn[s].rule == REFLEX_RULE) b->syn[s].weight = (short)lrint(W[s] * 1000.0f);
  }
}

void ob_write_reflex(const ob_reflex_stats_t *st, FILE *out) {
  if (st == 0 || out == 0 || st->days == 0u) return;
  fprintf(out, "\n## the womb: the reflex loop, practised\n");
  fprintf(out, "   lips touched, mouth muscles' drive: %.2f before, %.2f after (its own hand to its mouth, and its twitches)\n",
          st->mouth_before, st->mouth_after);
  fprintf(out, "   middle of the body touched, its muscles' drive: %.2f before, %.2f after (its twitches pressing the womb wall)\n",
          st->body_before, st->body_after);
}

/* ---- infancy: serve and return, the mobile -------------------------------------- */

static unsigned ACTION_RULE = 0xFFu;

static int leg_kick_centre(const ob_neuron_t *n) {
  return n->region == OB_BRAINSTEM && n->side == OB_LEFT && n->sx >= 0.75f && n->sx < 0.95f;
}

void ob_infant_mobile(ob_brain_t *b, unsigned sessions, unsigned moments_per_session, int contingent,
                      unsigned char *schedule, ob_mobile_stats_t *out) {
  unsigned se, t, i, s, k, moment = 0u;
  double place = 0.0, adds_kick = 0.0, adds_still = 0.0;   /* the critic, and what each choice adds */
  double centre = 0.0;                                      /* the kick centre's own restless activity */
  if (b == 0 || out == 0) return;
  memset(out, 0, sizeof(*out));
  if (sessions > 32u) sessions = 32u;
  RNG = b->genes.seed * 2246822507u + (contingent ? 19u : 23u);
  prepare(b);
  for (k = 0u; k < 64u; k++) {
    if (strncmp(ob_rule_name(k), "action:", 7) == 0) ACTION_RULE = k;
  }
  for (se = 0u; se < sessions; se++) {
    int answering = se > 0u && se + 1u < sessions;
    for (t = 0u; t < moments_per_session; t++, moment++) {
      double drive = 0.0, reward;
      int kick, answer;
      unsigned n_centre = 0u;
      /* the scene: the basal ganglia hear it (here, a context of their neurons
         that the room with the mobile lights up) */
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *n = &b->neuron[i];
        ACT1[i] = (n->region == OB_BASAL_GANGLIA && n->side == OB_LEFT && n->sy < 0.5f) ? 1.0f : 0.0f;
      }
      /* what reaches the kick centre from the basal ganglia makes a kick likelier */
      for (i = 0u; i < b->n_neurons; i++) {
        const ob_neuron_t *n = &b->neuron[i];
        if (!leg_kick_centre(n)) continue;
        n_centre++;
        for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
          if (b->syn[s].rule == ACTION_RULE) drive += W[s] * ACT1[b->syn[s].from];
        }
      }
      drive = n_centre ? drive / n_centre : 0.0;
      /* the kick centre's neurons, coupled, wander together of themselves; the
         leg kicks when their activity, with what the basal ganglia add (or
         take away), crosses threshold */
      centre = 0.8 * centre + 0.6 * gauss();
      kick = centre + drive > 1.4;
      if (kick) centre = 0.0;   /* a kick spends it */
      if (kick) out->kicks[se]++;
      /* the world answers: the mobile moves, the mother's voice */
      if (contingent) answer = answering && kick;
      else answer = answering && schedule != 0 && (schedule[moment / 8u] >> (moment % 8u) & 1u);
      if (contingent && schedule != 0 && answer) schedule[moment / 8u] |= (unsigned char)(1u << (moment % 8u));
      if (answer) out->answered[se]++;
      /* dopamine: what this moment brought, against what is expected of this
         choice here: how good this place is anyway (the critic, learned every
         moment), plus what this choice adds to it (learned only from moments
         of this choice: one for kicking, one for holding still). Where the world
         answers whatever it does, choices add nothing and nothing is learned;
         where it answers kicks, kicks add; where it stops, kicks disappoint. */
      reward = answer ? 1.0 : 0.0;
      {
        double delta = reward - place - (kick ? adds_kick : adds_still);
        if (kick) adds_kick += 0.05 * delta;
        else adds_still += 0.05 * delta;
        place += 0.1 * (reward - place);
        for (i = 0u; i < b->n_neurons; i++) {
          const ob_neuron_t *n = &b->neuron[i];
          if (!leg_kick_centre(n)) continue;
          for (s = n->syn_first; s < n->syn_first + n->syn_n; s++) {
            if (b->syn[s].rule != ACTION_RULE || ACT1[b->syn[s].from] <= 0.0f) continue;
            W[s] += (float)((kick ? 0.02 : -0.02) * delta * ACT1[b->syn[s].from]);
            /* the basal ganglia's output can hold a movement back as well as let
               it go: below its spontaneous rate as well as above */
            if (W[s] < -3.0f) W[s] = -3.0f;
            if (W[s] > 3.0f) W[s] = 3.0f;
          }
        }
      }
    }
  }
  out->sessions = sessions;
  for (s = 0u; s < b->n_syn; s++) {
    if (b->syn[s].rule == ACTION_RULE) b->syn[s].weight = (short)lrint(W[s] * 1000.0f);
  }
}

void ob_write_mobile(const ob_mobile_stats_t *real, const ob_mobile_stats_t *yoked, FILE *out) {
  unsigned se;
  if (real == 0 || out == 0 || real->sessions == 0u) return;
  fprintf(out, "\n## infancy: serve and return (the mobile)\n");
  fprintf(out, "## kicks in each session: first a baseline, last an extinction; between, the world answers\n");
  fprintf(out, "   session   its own kicks answered   a yoked twin (answered, not by its kicks)\n");
  for (se = 0u; se < real->sessions; se++) {
    fprintf(out, "   %4u      %5u                    %5u\n", se, real->kicks[se], yoked ? yoked->kicks[se] : 0u);
  }
}

/* ---- the picture: its body, and how much of each place's touch is its own ---- */

int ob_draw_self(const ob_brain_t *b, const ob_womb_stats_t *st, const char *path) {
  const unsigned W2 = 640u, H2 = 360u, BINS = 24u;
  unsigned char *img = (unsigned char *)calloc((size_t)W2 * H2 * 3u, 1u);
  unsigned d, x, y, k, side;
  int rc;
  if (img == 0 || b == 0 || st == 0) {
    free(img);
    return 1;
  }
  /* the curves: share of touch left after prediction, day by day */
  for (d = 0u; d < st->days; d++) {
    unsigned px = 30u + d * (W2 - 60u) / (st->days > 1u ? st->days - 1u : 1u);
    double vals[3];
    static const unsigned char COL[3][3] = {{90, 160, 255}, {255, 110, 90}, {250, 210, 80}};
    vals[0] = st->self_left[d];
    vals[1] = st->other_left[d];
    vals[2] = st->double_left[d];
    for (k = 0u; k < 3u; k++) {
      double v = vals[k] > 1.2 ? 1.2 : vals[k];
      unsigned py = 20u + (unsigned)((1.2 - v) / 1.2 * 140.0);
      for (y = py; y < py + 4u && y < H2; y++) {
        for (x = px; x < px + 4u && x < W2; x++) memcpy(img + 3u * ((size_t)y * W2 + x), COL[k], 3);
      }
    }
  }
  /* the body: probe each place with its own twitch and with the mother's touch
     (without learning) and colour it by how much of its own touch is cancelled */
  prepare(b);
  for (side = 0u; side < 2u; side++) {
    for (k = 0u; k < BINS; k++) {
      ob_moment_t self, other;
      double pos = (k + 0.5) / BINS, own;
      unsigned char c[3];
      probe_twitch(b, pos, 0.5, side);
      moment((ob_brain_t *)b, 0, 0, 0, 0u, 0, &self);
      memset(ACT0, 0, sizeof(float) * b->n_neurons);
      moment((ob_brain_t *)b, 1, pos, 0.5, side, 0, &other);
      own = self.drive > 0 ? 1.0 - self.left / self.drive : 0.0;
      if (own < 0.0) own = 0.0;
      if (own > 1.0) own = 1.0;
      c[0] = (unsigned char)(255.0 * (1.0 - own));
      c[1] = (unsigned char)(60.0 + 80.0 * own);
      c[2] = (unsigned char)(255.0 * own);
      for (y = 200u + side * 70u; y < 260u + side * 70u; y++) {
        for (x = 30u + k * (W2 - 60u) / BINS; x < 28u + (k + 1u) * (W2 - 60u) / BINS; x++)
          memcpy(img + 3u * ((size_t)y * W2 + x), c, 3);
      }
      (void)other;
    }
  }
  rc = ob_write_png_rgb(path, img, W2, H2);
  free(img);
  return rc;
}
