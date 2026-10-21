/*
 * ob_crib.c -- the crib. See ob_crib.h.
 */

#include "ob_crib.h"

#include <math.h>
#include <string.h>

static unsigned rnd(ob_crib_t *c) {
  unsigned x = c->rng;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  c->rng = x;
  return x;
}

static double unit(ob_crib_t *c) { return (double)(rnd(c) >> 8) / 16777216.0; }

/* the window its eyes see: its corner on the wall */
static int corner(float g) { return (int)lrintf(g * (float)(OC_W - OL_EYE)); }

void ob_crib_make(ob_crib_t *c, unsigned seed) {
  int x, y;
  memset(c, 0, sizeof(*c));
  c->rng = seed * 747796405u + 2891336453u;
  if (c->rng == 0u) c->rng = 1u;
  c->gx = c->gy = 0.5f;
  c->answer_in = -1;
  c->voice_age = 99u;
  /* the wall: soft shapes, the same every day */
  for (y = 0; y < OC_W; y++) {
    for (x = 0; x < OC_W; x++)
      c->wall[y][x] = (float)(0.35 + 0.2 * sin(x * 0.7) * cos(y * 0.45) + 0.1 * sin((x + y) * 0.3));
  }
  /* its mother's face, to the left: a face, two eyes, a mouth */
  for (y = 10; y < 20; y++) {
    for (x = 0; x < 10; x++) {
      double dx = (x - 4.5) / 4.5, dy = (y - 14.5) / 5.0;
      if (dx * dx + dy * dy < 1.0) c->wall[y][x] = 0.75f;
    }
  }
  c->wall[13][3] = c->wall[13][6] = 0.1f;
}

int ob_crib_on_tv(const ob_crib_t *c) { return corner(c->gx) + 4 >= 23 && corner(c->gy) + 4 >= 23; }
int ob_crib_on_mobile(const ob_crib_t *c) {
  int x = corner(c->gx) + 4;
  return x >= 10 && x <= 22 && corner(c->gy) + 4 <= 8;
}
int ob_crib_on_mother(const ob_crib_t *c) {
  int y = corner(c->gy) + 4;
  return corner(c->gx) + 4 <= 10 && y >= 10 && y <= 20;
}

void ob_crib_sense(ob_crib_t *c, float *in) {
  float scene[OC_W][OC_W];
  int x, y, ox = corner(c->gx), oy = corner(c->gy), mx;
  unsigned i;
  memcpy(scene, c->wall, sizeof scene);
  /* the mobile, where its swing has taken it */
  mx = 16 + (int)lrintf(c->swing * 4.0f);
  for (y = 2; y < 5; y++) {
    for (x = mx - 1; x <= mx + 1; x++) {
      if (x >= 0 && x < OC_W) scene[y][x] = 1.0f;
    }
  }
  /* the television: snow */
  for (y = 23; y < OC_W; y++) {
    for (x = 23; x < OC_W; x++) scene[y][x] = (float)unit(c);
  }
  /* the mother's mouth moves while she talks */
  for (x = 3; x <= 6; x++) scene[17][x] = c->mother_talks && (c->t & 1u) ? 0.95f : 0.3f;

  for (y = 0; y < OL_EYE; y++) {
    for (x = 0; x < OL_EYE; x++) in[y * OL_EYE + x] = scene[oy + y][ox + x];
  }
  /* hearing: a low hum; its own voice; its mother's */
  for (i = 0u; i < OL_EAR; i++) in[OL_VIS + i] = 0.0f;
  in[OL_VIS] = (float)(0.1 + 0.05 * unit(c));
  c->self_heard = c->voice_age == 0u;
  if (c->self_heard) in[OL_VIS + 2] = in[OL_VIS + 3] = 0.8f;
  c->mother_heard = c->mother_talks > 0u;
  c->answer_heard = c->mother_heard && c->mother_answering;
  if (c->mother_heard) {
    for (i = 4u; i <= 6u; i++) in[OL_VIS + i] = (float)(0.5 + 0.3 * unit(c));
  }
  /* skin: where its mother touches it */
  for (i = 0u; i < OL_SKIN; i++) in[OL_VIS + OL_EAR + i] = 0.0f;
  c->touched = c->mother_touch > 0u;
  if (c->touched) in[OL_VIS + OL_EAR + c->touch_at] = 1.0f;
  /* the inner ear: its own jerks, and being rocked */
  for (i = 0u; i < OL_MOVE; i++) in[OL_VIS + OL_EAR + OL_SKIN + i] = 0.0f;
  c->self_moved = c->jerk > 0u;
  c->mother_moved = c->rocking > 0u;
  if (c->self_moved) in[OL_VIS + OL_EAR + OL_SKIN + 2] += c->jerk == 2u ? 0.9f : 0.4f;
  if (c->mother_moved) {
    in[OL_VIS + OL_EAR + OL_SKIN + 0] += c->rock[0];
    in[OL_VIS + OL_EAR + OL_SKIN + 1] += c->rock[1];
  }
  in[OL_SENSE] = c->gx;
  in[OL_SENSE + 1] = c->gy;
}

void ob_crib_act(ob_crib_t *c, unsigned bits) {
  const float step = 1.0f / 12.0f;
  /* its eyes */
  if (bits & (1u << OL_LOOK_LEFT)) c->gx -= step;
  if (bits & (1u << OL_LOOK_RIGHT)) c->gx += step;
  if (bits & (1u << OL_LOOK_UP)) c->gy -= step;
  if (bits & (1u << OL_LOOK_DOWN)) c->gy += step;
  if (c->gx < 0.0f) c->gx = 0.0f;
  if (c->gx > 1.0f) c->gx = 1.0f;
  if (c->gy < 0.0f) c->gy = 0.0f;
  if (c->gy > 1.0f) c->gy = 1.0f;
  /* its voice; and its mother, who often answers */
  c->voice_age = c->voice_age < 99u ? c->voice_age + 1u : 99u;
  if (bits & (1u << OL_VOICE)) {
    c->voice_age = 0u;
    if (c->answer_in < 0 && c->mother_talks == 0u && unit(c) < 0.7) c->answer_in = 2;
  }
  /* its jerk shakes the crib, and the mobile swings */
  if (c->jerk) c->jerk--;
  if (bits & (1u << OL_BUZZ)) {
    c->jerk = 2u;
    c->swing_v += 0.6f;
  }
  c->swing_v -= 0.15f * c->swing;
  c->swing_v *= 0.9f;
  c->swing += c->swing_v;
  if (c->swing > 1.5f) c->swing = 1.5f;
  if (c->swing < -1.5f) c->swing = -1.5f;
  /* the mother, of her own accord */
  if (c->mother_talks) c->mother_talks--;
  if (c->mother_talks == 0u) c->mother_answering = 0u;
  if (c->answer_in == 0) {
    c->mother_talks = 3u;
    c->mother_answering = 1u;
  }
  if (c->answer_in >= 0) c->answer_in--;
  if (c->mother_talks == 0u && unit(c) < 0.01) c->mother_talks = 3u;
  if (c->mother_touch) c->mother_touch--;
  else if (unit(c) < 0.015) {
    c->mother_touch = 2u;
    c->touch_at = rnd(c) % OL_SKIN;
  }
  /* her rocking: her hands push it this way and that, never quite regularly;
     its inner ear feels each push */
  if (c->rocking) {
    unsigned a;
    c->rocking--;
    for (a = 0u; a < 2u; a++) c->rock[a] = (float)(0.1 + 0.8 * unit(c));
  } else if (unit(c) < 0.003) {
    c->rocking = 30u;
  }
  c->t++;
}

/* ---- watching it -------------------------------------------------------------- */

static int in_corner(const ob_crib_t *c, int right, int bottom) {
  int ox = corner(c->gx), oy = corner(c->gy), lo = 5, hi = OC_W - OL_EYE - 5;
  return (right ? ox >= hi : ox <= lo) && (bottom ? oy >= hi : oy <= lo);
}

void ob_crib_live(ol_life_t *L, unsigned seed, unsigned long moments, ob_crib_life_t *out) {
  static ob_crib_t c;
  float in[OL_INPUT];
  unsigned long t, half = moments / 2u;
  memset(out, 0, sizeof(*out));
  ob_crib_make(&c, seed);
  for (t = 0u; t < moments; t++) {
    unsigned bits, m;
    ob_crib_sense(&c, in);
    bits = ol_live(L, in);
    for (m = 0u; m < OL_MOTORS; m++) out->acts[t * OC_TENTHS / moments][m] += bits >> m & 1u;
    if (t >= half) {
      out->tv += ob_crib_on_tv(&c);
      out->corner += (in_corner(&c, 0, 0) + in_corner(&c, 1, 0) + in_corner(&c, 0, 1)) / 3.0;
      out->mobile += ob_crib_on_mobile(&c);
      out->mother += ob_crib_on_mother(&c);
    }
    ob_crib_act(&c, bits);
  }
  out->moments = moments;
  if (moments > half) {
    double n = (double)(moments - half);
    out->tv /= n;
    out->corner /= n;
    out->mobile /= n;
    out->mother /= n;
  }
}

const char *const OC_EXAM_NAME[OC_EXAMS] = {
  "the wall, as its eyes move", "its own voice", "its own jerks", "being rocked",
  "its mother answering it", "its mother talking of herself", "the mobile swinging", "its mother's touch"};

void ob_crib_examine(const ol_life_t *L, unsigned seed, unsigned long moments, double out[OC_EXAMS]) {
  static ol_life_t me;
  static ob_crib_t c;
  double err[OC_EXAMS], came[OC_EXAMS];
  float in[OL_INPUT];
  unsigned long t;
  unsigned k, last = 0u;
  memcpy(&me, L, sizeof me);
  me.learning = 0u;
  memset(err, 0, sizeof err);
  memset(came, 0, sizeof came);
  ob_crib_make(&c, seed);
  for (t = 0u; t < moments; t++) {
    ob_crib_sense(&c, in);
    last = ol_live(&me, in);
#define SEEN(k, g) do { err[k] += me.err[g]; came[k] += me.energy[g]; } while (0)
    if (c.self_heard && !c.mother_heard) SEEN(OC_VOICE, OL_HEAR);
    if (c.answer_heard && !c.self_heard) SEEN(OC_ANSWER, OL_HEAR);
    if (c.mother_heard && !c.answer_heard && !c.self_heard) SEEN(OC_TALK, OL_HEAR);
    if (c.self_moved && !c.mother_moved) SEEN(OC_JERK, OL_MOVED);
    if (c.mother_moved && !c.self_moved) SEEN(OC_ROCKED, OL_MOVED);
    if (c.touched) SEEN(OC_TOUCHED, OL_FEEL);
    if (ob_crib_on_mobile(&c) && fabsf(c.swing) > 0.2f) SEEN(OC_MOBILE, OL_SEE);
    if (me.moments > 1u && (me.eff[OL_LOOK_LEFT * OL_WAVE + 1u] + me.eff[OL_LOOK_RIGHT * OL_WAVE + 1u] +
                            me.eff[OL_LOOK_UP * OL_WAVE + 1u] + me.eff[OL_LOOK_DOWN * OL_WAVE + 1u]) > 0.0f &&
        !ob_crib_on_tv(&c))
      SEEN(OC_EYES, OL_SEE);
#undef SEEN
    ob_crib_act(&c, last);
  }
  for (k = 0u; k < OC_EXAMS; k++) out[k] = came[k] > 0.0 ? err[k] / came[k] : -1.0;
}
