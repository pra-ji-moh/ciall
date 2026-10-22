/*
 * ob_box.h -- growing up in the crib, and the blicket box (after Gopnik).
 *
 * The crib as before (ob_crib.c), lived by the one brain as it ages: every so
 * often its age moves on, and with it the myelination wave (ob_one_age). Now
 * and then a box is put in the crib. It chimes when the being does the right
 * acts with its eyes on the right quarter of the wall: one rule of 144 (one act
 * of its six, or both of two, or either of two, times the quarter). The chime
 * is high, on its cochlea's top band, and it is pleasant: dopamine, as the
 * mobile's answers were.
 *
 * Meeting a box, it reasons if it can: if its DLPFC holds anything, its frontal
 * cortex tests, by its own eyes and acts, and reads each outcome off its own
 * ear; once it knows the rule it uses it. If its DLPFC holds nothing (too
 * young, or a lesion), it lives as it would, and only its basal ganglia learn,
 * by the dopamine.
 */

#ifndef OB_BOX_H
#define OB_BOX_H

#include "ob_crib.h"
#include "ob_frontal.h"
#include "ob_one.h"

#define OB_BOX_RULES 144
#define OB_BOX_TESTS (63 * 4)

/* the world: the crib, and the box when it is there */
typedef struct {
  ob_crib_t crib;
  int box_on;
  unsigned rule;
  int chime;                          /* its chime reaching the ear now */
  unsigned long moments, chimes;
} ob_world_t;

void ob_world_make(ob_world_t *W, unsigned seed);

/* what rule h says a test brings */
int ob_box_says(unsigned h, const of_test_t *t);

/* one moment of its life (deliberately, if `intent_on`). Returns what its ear
   hears of the chime now */
float ob_world_moment(ob_world_t *W, ob_one_t *O, unsigned intent_on, unsigned intent);

/* it lives on, from its age now to `months`, `per_month` moments a month,
   its age moving on every tenth of a month */
void ob_grow_up(ob_world_t *W, ob_one_t *O, double months, unsigned long per_month);

enum { OB_LESION_ACC = 1, OB_LESION_DLPFC = 2, OB_LESION_FRONTAL = 4 };

typedef struct {
  unsigned room;                      /* how much its DLPFC could hold, when the box came */
  int found;                          /* it came to know the rule, and rightly */
  unsigned tries;                     /* the tries it took to know it */
  unsigned struck, left;              /* hypotheses ruled out; standing at the end */
  unsigned reads, reads_ok;           /* outcomes read off its ear, and rightly */
  double chime_rate;                  /* chimes per moment, in the last 300 moments with the box */
} ob_box_meeting_t;

/* a box with rule `rule` is in the crib for `moments` moments */
void ob_box_meet(ob_world_t *W, ob_one_t *O, unsigned rule, unsigned long moments, unsigned lesions,
                 ob_box_meeting_t *out);

#endif
