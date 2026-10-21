/*
 * ob_life.c -- a life of its own. See ob_life.h.
 *
 * Freestanding: no library, not even maths. The few functions it needs are
 * here, so the same code, bit for bit, runs in the tests and on a phone.
 */

#include "ob_life.h"

/* ---- what it needs instead of a library ---------------------------------------- */

static void zero(void *p, unsigned long n) {
  unsigned char *c = (unsigned char *)p;
  while (n--) *c++ = 0u;
}

static float ol_exp(float x) {
  /* e^x = 2^k e^r, |r| <= ln2 / 2 */
  float r, e = 1.0f, term = 1.0f;
  int k, i;
  if (x > 30.0f) x = 30.0f;
  if (x < -30.0f) x = -30.0f;
  k = (int)(x * 1.44269504f + (x >= 0.0f ? 0.5f : -0.5f));
  r = x - (float)k * 0.69314718f;
  for (i = 1; i <= 7; i++) {
    term *= r / (float)i;
    e += term;
  }
  for (; k > 0; k--) e *= 2.0f;
  for (; k < 0; k++) e *= 0.5f;
  return e;
}

static float sigm(float x) { return 1.0f / (1.0f + ol_exp(-x)); }

static unsigned rnd(ol_life_t *L) {
  unsigned x = L->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  L->rng = x;
  return x;
}

static float unit(ol_life_t *L) { return (float)(rnd(L) >> 8) / 16777216.0f; }

/* near enough to a normal deviate: four uniforms, centred, scaled to spread 1 */
static float gauss(ol_life_t *L) {
  return (unit(L) + unit(L) + unit(L) + unit(L) - 2.0f) * 1.7320508f;
}

/* ---- its body plan --------------------------------------------------------------- */

static const unsigned GROUP_FIRST[OL_GROUPS] = {0u, OL_VIS, OL_VIS + OL_EAR, OL_VIS + OL_EAR + OL_SKIN};
static const unsigned GROUP_SIZE[OL_GROUPS] = {OL_VIS, OL_EAR, OL_SKIN, OL_MOVE};

/* the brainstem's threshold: of itself, each centre fires about one moment in twenty */
#define THRESHOLD 1.65f
/* the basal ganglia lean on the brainstem, but never silence it, nor drive it
   all the time; and what they learned fades unless it keeps being worth it */
#define URGE_MIN -0.6f
#define URGE_MAX 1.4f
#define FORGET 0.0002f

/* one row of a sense's predictor: what it makes of that sense now, the ripple, a bias */
static float *row(ol_life_t *L, unsigned g, unsigned j) {
  switch (g) {
  case OL_SEE: return L->cb.see[L->gaze_move][j];
  case OL_HEAR: return L->cb.hear[j];
  case OL_FEEL: return L->cb.feel[j];
  default: return L->cb.moved[j];
  }
}

static float predict(const float *w, const float *own, unsigned n, const float *eff) {
  float sum = w[n + OL_EFF];
  unsigned i;
  for (i = 0u; i < n; i++) sum += w[i] * own[i];
  for (i = 0u; i < OL_EFF; i++) sum += w[n + i] * eff[i];
  return sum;
}

static unsigned gaze_bin(float x, float y) {
  int bx = (int)(x * 4.0f), by = (int)(y * 4.0f);
  if (bx < 0) bx = 0;
  if (bx > 3) bx = 3;
  if (by < 0) by = 0;
  if (by > 3) by = 3;
  return (unsigned)(by * 4 + bx);
}

/* what it did most recently, within the ripple (or OL_MOTORS: nothing) */
static unsigned recent(const ol_life_t *L) {
  unsigned age, m;
  for (age = 0u; age < OL_WAVE; age++) {
    for (m = 0u; m < OL_MOTORS; m++) {
      if (L->eff[m * OL_WAVE + age] > 0.0f) return m;
    }
  }
  return OL_MOTORS;
}

/* ---- the hippocampus ---------------------------------------------------------------
 * Every moment is kept as it came. Each sense has its own index into them: a
 * key from the shape of what that sense brought (its sign against a few fixed
 * random patterns: similar moments, similar keys), and from the acts whose
 * copies reach that part of it, in the last three moments (the eyes'
 * movements for seeing, its voice for hearing, its jerks for being moved). When now matches a moment it has lived,
 * it expects what came next then. */

/* which acts' copies reach each sense's part of it; `lately` holds each act's
   last three moments, three bits per act (bit 3m + age) */
#define AGES(m) (7u << (3u * (m)))
static const unsigned REACHES[OL_GROUPS] = {AGES(OL_LOOK_LEFT) | AGES(OL_LOOK_RIGHT) | AGES(OL_LOOK_UP) | AGES(OL_LOOK_DOWN),
                                            AGES(OL_VOICE), 0u, AGES(OL_BUZZ)};

static unsigned mix(unsigned x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

/* a moment's shape in sense g: 12 bits, each its sign against a fixed random
   pattern; `weak` gets the three bits nearest to flipping, weakest first */
static unsigned shape(const float *s, unsigned g, unsigned weak[3]) {
  unsigned f = GROUP_FIRST[g], n = GROUP_SIZE[g], b, i, h = 0u;
  float mean = 0.0f, margin[3] = {1e30f, 1e30f, 1e30f};
  for (i = 0u; i < n; i++) mean += s[f + i];
  mean /= (float)n;
  for (b = 0u; b < 12u; b++) {
    float sum = 0.0f, a;
    for (i = 0u; i < n; i++)
      sum += (mix(b * 1315423911u + i * 2654435761u + g * 97u) & 1u ? 1.0f : -1.0f) * (s[f + i] - mean);
    if (sum > 0.0f) h |= 1u << b;
    a = sum < 0.0f ? -sum : sum;
    if (weak != 0) {
      unsigned k = 3u;
      while (k > 0u && a < margin[k - 1u]) {
        if (k < 3u) {
          margin[k] = margin[k - 1u];
          weak[k] = weak[k - 1u];
        }
        k--;
      }
      if (k < 3u) {
        margin[k] = a;
        weak[k] = b;
      }
    }
  }
  return h;
}

static unsigned file_under(unsigned h, unsigned g, unsigned acts) {
  return mix(mix(h + g * 0x9E3779B9u) ^ (acts & REACHES[g])) >> (32 - OL_INDEX_BITS);
}

static unsigned key(const float *s, unsigned g, unsigned acts) { return file_under(shape(s, g, 0), g, acts); }

static unsigned char quant(float v) {
  if (v < 0.0f) v = 0.0f;
  if (v > 1.0f) v = 1.0f;
  return (unsigned char)(v * 255.0f + 0.5f);
}

static int held(const ol_hippocampus_t *h, unsigned long moment) {
  return h->stamp[moment % OL_MEMORY] == (unsigned)(moment + 1u);
}

/* the moment it has lived that is nearest to now in sense g, if near enough:
   under now's key, and the keys a little noise would have given it */
static long recall(const ol_hippocampus_t *h, const float *s, unsigned g, unsigned acts, float *how_near) {
  unsigned f = GROUP_FIRST[g], n = GROUP_SIZE[g], weak[3] = {0u, 0u, 0u}, probe;
  unsigned bits = shape(s, g, weak);
  long best = -1;
  float nearest = OL_NEAR;
  for (probe = 0u; probe < 4u; probe++) {
    unsigned tries = 0u, m = h->head[g][file_under(probe ? bits ^ (1u << weak[probe - 1u]) : bits, g, acts)];
    while (m != 0u && tries < 16u) {
      unsigned long moment = m - 1u;
      unsigned slot = (unsigned)(moment % OL_MEMORY), i;
      if (h->stamp[slot] != m) break;   /* given way to a newer moment */
      if ((h->acts[slot] & REACHES[g]) == (acts & REACHES[g])) {
        float d = 0.0f;
        for (i = 0u; i < n; i++) {
          float v = (float)h->senses[slot][f + i] / 255.0f - s[f + i];
          d += v < 0.0f ? -v : v;
        }
        d /= (float)n;
        if (d < nearest) {
          nearest = d;
          best = (long)moment;
        }
      }
      m = h->next[g][slot];
      tries++;
    }
    if (best >= 0 && probe == 0u && nearest < OL_EXACT) break;   /* found it where it should be */
  }
  if (how_near != 0) *how_near = nearest;
  return best;
}

static void keep(ol_hippocampus_t *h, unsigned long moment, const float *s, unsigned acts) {
  unsigned slot = (unsigned)(moment % OL_MEMORY), g, i;
  h->stamp[slot] = (unsigned)(moment + 1u);
  for (i = 0u; i < OL_SENSE; i++) h->senses[slot][i] = quant(s[i]);
  h->acts[slot] = acts;
  for (g = 0u; g < OL_GROUPS; g++) {
    unsigned k = key(s, g, acts);
    h->next[g][slot] = h->head[g][k];
    h->head[g][k] = (unsigned)(moment + 1u);
  }
}

int ol_light(const ol_life_t *L, unsigned long moment, float *senses) {
  unsigned i, slot = (unsigned)(moment % OL_MEMORY);
  if (moment >= L->moments || !held(&L->hip, moment)) return 0;
  for (i = 0u; i < OL_SENSE; i++) senses[i] = (float)L->hip.senses[slot][i] / 255.0f;
  return 1;
}

long ol_complete(const ol_life_t *L, const float *cue, unsigned acts) { return recall(&L->hip, cue, OL_SEE, acts, 0); }

void ol_born(ol_life_t *L, unsigned seed, unsigned curiosity) {
  unsigned g;
  zero(L, sizeof(*L));
  L->seed = seed;
  L->rng = seed * 2654435761u + 0x9E3779B9u;
  if (L->rng == 0u) L->rng = 1u;
  L->curiosity = curiosity;
  L->learning = 1u;
  L->remembers = 1u;
  for (g = 0u; g < OL_GROUPS; g++) L->recalled[g] = -1;
}

float ol_progress(const ol_life_t *L, unsigned g, unsigned k) {
  if (g >= OL_GROUPS || k >= OL_PLACES) return 0.0f;
  return L->progress[g][k];
}

unsigned ol_live(ol_life_t *L, const float *in) {
  const float *s = in;
  float ctx[OL_CTX];
  unsigned g, j, i, m, bits = 0u, lately = 0u;

  /* where it is now, as its basal ganglia hear it: where its eyes point, what it
     did last, and a constant */
  zero(ctx, sizeof ctx);
  ctx[gaze_bin(in[OL_SENSE], in[OL_SENSE + 1])] = 1.0f;
  ctx[OL_GAZE_BINS + recent(L)] = 1.0f;
  ctx[OL_CTX - 1] = 1.0f;

  if (L->moments > 0u) {
    float reward = 0.0f;
    /* 1. the cerebellum: what came, against what it expected; each predictor
          moves to make it right (the delta rule, normalised by what it saw) */
    for (g = 0u; g < OL_GROUPS; g++) {
      unsigned f = GROUP_FIRST[g], n = GROUP_SIZE[g];
      float norm = 0.1f, e = 0.0f, en = 0.0f;
      for (i = 0u; i < n; i++) norm += L->x[f + i] * L->x[f + i];
      for (i = 0u; i < OL_EFF; i++) norm += L->x[OL_SENSE + i] * L->x[OL_SENSE + i];
      norm += 1.0f;
      for (j = 0u; j < n; j++) {
        float *w = row(L, g, j);
        float d = s[f + j] - L->expect[f + j], k = 0.2f * (s[f + j] - L->cb_expect[f + j]) / norm;
        L->surprise[f + j] = d;
        e += d * d;
        en += s[f + j] * s[f + j];
        if (!L->learning) continue;
        for (i = 0u; i < n; i++) w[i] += k * L->x[f + i];
        for (i = 0u; i < OL_EFF; i++) w[n + i] += k * L->x[OL_SENSE + i];
        w[n + OL_EFF] += k;
      }
      L->err[g] = e / (float)n;
      L->energy[g] = en / (float)n;
      /* the hippocampus learns where its memories can be trusted: did what came
         next last time predict this better than the cerebellum did, on average?
         (Where the world is random, memory loses: what came last time is one
         draw, the cerebellum's guess the average.) */
      if (L->recalled[g] >= 0 && L->learning) {
        float mem = 0.0f, cb = 0.0f, *t = &L->hip.trust[g][L->recalled[g]];
        for (j = 0u; j < n; j++) {
          float a = s[f + j] - L->remembered[f + j], b = s[f + j] - L->cb_expect[f + j];
          mem += a * a;
          cb += b * b;
        }
        *t += 0.2f * ((cb - mem) / (float)n - *t);
        if (*t == 0.0f) *t = cb >= mem ? 1e-9f : -1e-9f;   /* tried, now: never "untried" again */
      }
    }
    /* 2. the tegmentum: in the place each sense was, is its error coming down?
          Over each span of visits to a place, its mean error against the span
          before: how much it fell, against how big it was (no units, so the
          senses can be compared; and where it is random, it falls no more
          than it rises, so it is worth nothing). That is what it likes. */
    for (g = 0u; g < OL_GROUPS; g++) {
      unsigned k = L->place[g];
      float e = L->err[g];
      L->usual[g] += (L->moments < 1000u ? 0.01f : 0.0001f) * (e - L->usual[g]);
      L->fast[g][k] = L->visits[g][k] ? L->fast[g][k] + 0.05f * (e - L->fast[g][k]) : e;
      L->span[g][k] += e;
      L->visits[g][k]++;
      if (L->visits[g][k] % OL_SPAN == 0u) {
        float now = L->span[g][k] / (float)OL_SPAN, before = L->last[g][k];
        if (L->visits[g][k] > OL_SPAN) L->progress[g][k] = (before - now) / (before + now + L->usual[g] + 1e-6f);
        L->last[g][k] = now;
        L->span[g][k] = 0.0f;
      }
      if (L->curiosity == OL_PROGRESS) reward += L->progress[g][k];
      else if (L->curiosity == OL_ERROR) reward += L->fast[g][k] / (L->fast[g][k] + L->usual[g] + 1e-6f);
    }
    L->dopamine = reward;
    /* 3. the basal ganglia: was it better here than the critic expected? Then
          the urges that brought it here grow; worse, they shrink */
    if (L->curiosity != OL_NONE && L->learning) {
      float before = 0.0f, now = 0.0f, delta;
      for (i = 0u; i < OL_CTX; i++) {
        before += L->value[i] * L->ctx[i];
        now += L->value[i] * ctx[i];
      }
      delta = reward + 0.9f * now - before;
      for (i = 0u; i < OL_CTX; i++) {
        L->value[i] += 0.03f * delta * L->vtrace[i];
        for (m = 0u; m < OL_MOTORS; m++) {
          L->act[m][i] += 0.05f * delta * L->trace[m][i] - FORGET * L->act[m][i];
        }
      }
    }
  }
  for (i = 0u; i < OL_CTX; i++) L->ctx[i] = ctx[i];

  /* 4. the brainstem: each centre restless of itself; it fires when its own
        activity, with what the basal ganglia add, crosses threshold. The eyes'
        four centres inhibit one another: one movement at a time. */
  {
    float push[OL_MOTORS];
    int best = -1;
    for (m = 0u; m < OL_MOTORS; m++) {
      float u = 0.0f;
      for (i = 0u; i < OL_CTX; i++) u += L->act[m][i] * ctx[i];
      if (u > URGE_MAX) u = URGE_MAX;
      if (u < URGE_MIN) u = URGE_MIN;
      L->urge[m] = u;
      L->restless[m] = 0.8f * L->restless[m] + 0.6f * gauss(L);
      push[m] = L->restless[m] + u;
      L->fired[m] = push[m] > THRESHOLD;
    }
    for (m = OL_LOOK_LEFT; m <= OL_LOOK_DOWN; m++) {
      if (L->fired[m] && (best < 0 || push[m] > push[best])) best = (int)m;
    }
    for (m = OL_LOOK_LEFT; m <= OL_LOOK_DOWN; m++) L->fired[m] = (int)m == best;
    for (m = 0u; m < OL_MOTORS; m++) {
      float p = sigm(1.7f * (L->urge[m] - THRESHOLD));
      for (i = 0u; i < OL_CTX; i++) L->trace[m][i] = 0.8f * L->trace[m][i] + ((float)L->fired[m] - p) * ctx[i];
      if (L->fired[m]) {
        bits |= 1u << m;
        L->acts[m]++;
        L->restless[m] = 0.0f;   /* a burst spends it */
      }
    }
    for (i = 0u; i < OL_CTX; i++) L->vtrace[i] = 0.8f * L->vtrace[i] + ctx[i];
    L->gaze_move = best < 0 ? 0u : (unsigned)best + 1u;
  }

  /* 5. the efference copy ripples on: each command one moment older */
  for (m = 0u; m < OL_MOTORS; m++) {
    for (i = OL_WAVE - 1u; i > 0u; i--) L->eff[m * OL_WAVE + i] = L->eff[m * OL_WAVE + i - 1u];
    L->eff[m * OL_WAVE] = (float)L->fired[m];
  }

  /* 6. the cerebellum predicts what the senses will bring next, from what they
        bring now and what it has just done */
  for (i = 0u; i < OL_SENSE; i++) L->x[i] = s[i];
  for (i = 0u; i < OL_EFF; i++) L->x[OL_SENSE + i] = L->eff[i];
  L->x[OL_SENSE + OL_EFF] = 1.0f;
  /* what it has done lately (these three moments): the hippocampus files each
     moment under it, with what the senses brought */
  for (m = 0u; m < OL_MOTORS; m++) {
    for (i = 0u; i < 3u; i++) {
      if (L->eff[m * OL_WAVE + i] > 0.0f) lately |= 1u << (3u * m + i);
    }
  }
  for (g = 0u; g < OL_GROUPS; g++) {
    unsigned f = GROUP_FIRST[g], n = GROUP_SIZE[g];
    long then = -1;
    float near = 1.0f;
    for (j = 0u; j < n; j++) L->cb_expect[f + j] = L->expect[f + j] = predict(row(L, g, j), s + f, n, L->eff);
    /* 7. the hippocampus: has it lived this before? Then it remembers what came
          next, and expects it, where its memories have proved worth trusting */
    L->recalled[g] = -1;
    if (L->remembers) then = recall(&L->hip, s, g, lately, &near);
    if (then >= 0 && held(&L->hip, (unsigned long)then + 1u)) {
      const unsigned char *next = L->hip.senses[(unsigned long)(then + 1) % OL_MEMORY];
      unsigned k = key(s, g, lately);
      for (j = 0u; j < n; j++) L->remembered[f + j] = (float)next[f + j] / 255.0f;
      L->recalled[g] = (int)k;
      /* trust is earned: a memory is followed where it has predicted better
         than the cerebellum; never yet tried, only if now is that moment again,
         exactly */
      if (L->hip.trust[g][k] > 0.0f || (L->hip.trust[g][k] == 0.0f && near < OL_EXACT)) {
        for (j = 0u; j < n; j++) L->expect[f + j] = L->remembered[f + j];
        L->recalls[g]++;
      }
    }
  }
  if (L->remembers) keep(&L->hip, L->moments, s, lately);
  L->place[OL_SEE] = gaze_bin(in[OL_SENSE], in[OL_SENSE + 1]) * 2u + (L->gaze_move != 0u);
  L->place[OL_HEAR] = L->place[OL_FEEL] = L->place[OL_MOVED] = recent(L);
  L->moments++;
  return bits;
}
