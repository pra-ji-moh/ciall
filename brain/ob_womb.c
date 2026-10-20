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
 * One moment of life. twitch: a twitch starts in the brainstem at (tx, ty) on
 * side ts. poke: the mother presses at (px, py) on side ps. learn: the
 * cerebellum learns from it.
 */
static void moment(ob_brain_t *b, int twitch, double tx, double ty, unsigned ts, int poke, double px, double py,
                   unsigned ps, int learn, ob_moment_t *m) {
  unsigned i;
  memset(m, 0, sizeof(*m));
  memset(ACT0, 0, sizeof(float) * b->n_neurons);
  if (twitch) {
    for (i = 0u; i < b->n_neurons; i++) {
      const ob_neuron_t *n = &b->neuron[i];
      if (n->region == OB_BRAINSTEM && n->side == ts && fabs(n->sx - tx) < 0.12 && fabs(n->sy - ty) < 0.3)
        ACT0[i] = 1.0f;
    }
  }
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
  prepare(b);
  DRIVE_REGION = OB_SPINAL_SENSORY;
  for (d = 0u; d < days; d++) {
    double sd = 0, sl = 0, od = 0, ol = 0, fd = 0, fl = 0, sp = 0, op = 0;
    for (t = 0u; t < moments_per_day; t++) {
      ob_moment_t m;
      double r = unit();
      int twitch = r < 0.6, poke = r >= 0.6 && r < 0.95;
      double tx = unit(), ty = unit(), px = unit(), py = unit();
      unsigned ts = rnd() & 1u, ps = rnd() & 1u;
      moment(b, twitch, tx, ty, ts, poke, px, py, ps, 1, &m);
      if (m.drive <= 0.0) continue;
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
      moment((ob_brain_t *)b, 1, pos, 0.5, side, 0, 0, 0, 0u, 0, &self);
      moment((ob_brain_t *)b, 0, 0, 0, 0u, 1, pos, 0.5, side, 0, &other);
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
