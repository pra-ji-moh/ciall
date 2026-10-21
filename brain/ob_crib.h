/*
 * ob_crib.h -- the crib: a world for ob_life.c, and nothing more than a world.
 *
 * It has physics and people, never a plan for the being: it does not choose
 * what the being looks at, says or does. It only answers what the being did,
 * the way the world would.
 *
 *   A wall it can look over (32 by 32), its eyes seeing 8 by 8 of it at a time.
 *   A mobile hanging above, tied to the crib: when the being jerks, the crib
 *     shakes and the mobile swings.
 *   A television in the corner, showing nothing but snow: never the same,
 *     never predictable.
 *   Its mother's face to one side. She talks now and then of her own accord;
 *     when the being makes a sound she often answers, a moment later; she
 *     touches it now and then, and sometimes picks it up and rocks it.
 *   Its own voice, which it hears; its own jerks, which it feels.
 */

#ifndef OB_CRIB_H
#define OB_CRIB_H

#include "ob_life.h"

#define OC_W 32

typedef struct {
  unsigned rng;
  float gx, gy;                       /* where its eyes point, 0..1 */
  float swing, swing_v;               /* the mobile */
  unsigned voice_age;                 /* moments since its own voice (0: this moment) */
  int answer_in;                      /* moments until the mother answers (-1: she will not) */
  unsigned mother_talks, mother_answering, mother_touch, touch_at, rocking, jerk;
  float rock[2];                      /* how she rocks it, this moment */
  unsigned long t;
  float wall[OC_W][OC_W];
  /* what caused what the senses bring now (for measuring, never shown to it) */
  int self_heard, mother_heard, answer_heard, self_moved, mother_moved, touched;
} ob_crib_t;

void ob_crib_make(ob_crib_t *c, unsigned seed);
/* what its senses bring now (OL_INPUT floats) */
void ob_crib_sense(ob_crib_t *c, float *in);
/* the world answers what it did */
void ob_crib_act(ob_crib_t *c, unsigned bits);
/* whether its eyes are on the television */
int ob_crib_on_tv(const ob_crib_t *c);
int ob_crib_on_mobile(const ob_crib_t *c);
int ob_crib_on_mother(const ob_crib_t *c);

/* a life in the crib, watched (the watching changes nothing) */
#define OC_TENTHS 10
typedef struct {
  unsigned long moments;
  unsigned long acts[OC_TENTHS][OL_MOTORS];   /* what it did, in each tenth of its life */
  /* in the second half of its life: the share of moments its eyes were on the
     television, on each other corner (the mean of the three), the mobile, its mother */
  double tv, corner, mobile, mother;
} ob_crib_life_t;

void ob_crib_live(ol_life_t *L, unsigned seed, unsigned long moments, ob_crib_life_t *out);

/* what it has come to expect: afterwards, learning stopped, how much of what
   its senses bring still surprises it, by what caused it (error against what
   came: 0 all expected, 1 nothing expected) */
enum { OC_EYES, OC_VOICE, OC_JERK, OC_ROCKED, OC_ANSWER, OC_TALK, OC_MOBILE, OC_TOUCHED, OC_EXAMS };
extern const char *const OC_EXAM_NAME[OC_EXAMS];
void ob_crib_examine(const ol_life_t *L, unsigned seed, unsigned long moments, double out[OC_EXAMS]);

#endif
