/*
 * smarsh_abstract.h -- building its own worlds from raw observation.
 *
 * ============================================================
 * THE WALL THIS ADDRESSES
 * ============================================================
 * Everything before this took its questions as given: "count pile 1",
 * "count pile 2". Given the questions, the worlds are forced (see
 * sr_refine), so the real open problem was always: where do the
 * questions come from? A child is not handed "count the left side of the
 * balance". They see pegs, some with weights, and what the beam does.
 *
 * ============================================================
 * WHAT IT DOES
 * ============================================================
 * Input: raw binary attributes (is there a weight on peg 3, is lamp 5
 * lit), with no meaning attached, and the outcome observed in each
 * situation. From that alone:
 *
 *   1. RELEVANCE. An attribute is proved relevant by two observed
 *      situations that differ only in it and have different outcomes. It is
 *      proved irrelevant when every such pair was observed and never
 *      differed. When the pairs seen never differ but some were never seen,
 *      it is CONJECTURED irrelevant: dropped, and the leap recorded. With no
 *      pair seen at all it is undetermined, and kept.
 *
 *   2. SYMMETRY. Two attributes are interchangeable when swapping their
 *      values never changes the outcome. PROVED when every swap was seen;
 *      CONJECTURED when every swap seen agrees and some were never seen,
 *      exactly as a law used past its witnessed range is a labeled leap
 *      (smarsh_law.h). One contradicting swap refuses it outright.
 *      Interchangeable attributes form groups.
 *
 *   3. COUNTING, FROM SYMMETRY. If the outcome is unchanged by every swap
 *      within a group, it can depend on nothing about that group but how
 *      many of its members are on (the orbits of all rearrangements of k
 *      switches are exactly "how many are on"). So for each group a new
 *      question is INVENTED: how many of these are on. That this new
 *      question, with the ungrouped attributes, determines the outcome is
 *      then checked by the kernel on the data, not assumed from the
 *      theorem.
 *
 *   4. THE WORLDS IT BUILT. The invented questions refine the observations
 *      into far fewer worlds than the raw attributes do. That world set is
 *      its own: nobody named a single question in it.
 *
 * ============================================================
 * THE SCOPE, STATED
 * ============================================================
 * This finds abstractions that come from symmetry: counting, and anything
 * the outcome treats as interchangeable. It does not find abstractions of
 * other kinds (position as a weight, as in reading binary numbers; order;
 * shape). When there is no symmetry it says so rather than inventing a
 * grouping. Attributes are yes/no, at most AB_MAX_RAW of them.
 */

#ifndef SMARSH_ABSTRACT_H
#define SMARSH_ABSTRACT_H

#include "smarsh_concept.h"

#define AB_MAX_RAW 8u
#define AB_MAX_SIT (1u << AB_MAX_RAW)
#define AB_NAME 24u

typedef enum {
  AB_UNDETERMINED = 0,
  AB_RELEVANT = 1,
  AB_IRRELEVANT = 2,        /* proved */
  AB_IRRELEVANT_CONJ = 3    /* never seen to matter, not every pair seen */
} ab_relevance_t;

typedef struct {
  unsigned n_raw;
  char name[AB_MAX_RAW][AB_NAME];
  unsigned dom_out;
  uint8_t outcome[AB_MAX_SIT];      /* by situation: bit i = attribute i */
  uint8_t observed[AB_MAX_SIT];     /* 1 if that situation was seen */
} ab_raw_t;

typedef struct {
  ab_relevance_t relevance[AB_MAX_RAW];
  unsigned group_of[AB_MAX_RAW];    /* group index, or AB_MAX_RAW if dropped */
  unsigned n_groups;
  unsigned group_size[AB_MAX_RAW];
  uint32_t group_members[AB_MAX_RAW];
  int group_proved[AB_MAX_RAW];     /* every swap within it was seen */

  /* The invented questions, one per group, over the observed situations
     in order: group g asks "how many of these are on". */
  unsigned n_situations;            /* observed */
  unsigned situation[AB_MAX_SIT];   /* observed situation -> raw pattern */
  sr_query_t question[AB_MAX_RAW];
  sr_query_t outcome_q;

  unsigned raw_worlds;              /* distinct observed raw patterns */
  unsigned built_worlds;            /* cells of the invented questions */
  int determined;                   /* the invented questions determine the outcome */
  int any_symmetry;                 /* some group has more than one member */
  int leaps;                        /* any conjectured symmetry or irrelevance */
} ab_result_t;

typedef enum {
  AB_SEEN = 0,          /* this very situation was observed */
  AB_INFERRED = 1,      /* from an observed situation in the same built world */
  AB_NO_GROUNDS = 2     /* no observed situation shares its built world */
} ab_standing_t;

void ab_clear(ab_raw_t *raw, unsigned n_raw, unsigned dom_out);
sm_status_t ab_name(ab_raw_t *raw, unsigned attr, const char *name);
/* One observation: the pattern (bit i is attribute i) and what happened. */
sm_status_t ab_observe(ab_raw_t *raw, unsigned pattern, unsigned outcome);

sm_status_t ab_abstract(const ab_raw_t *raw, ab_result_t *out);

/*
 * What happens in a raw situation, seen or not: through the worlds it
 * built. An unseen situation gets the outcome of a seen one in the same
 * built world, and that inference rests on the abstraction (and on its
 * conjectures, if result->leaps). No seen situation in that world: no
 * grounds, and no answer.
 */
ab_standing_t ab_predict(const ab_raw_t *raw, const ab_result_t *result, unsigned pattern,
                         unsigned *outcome);

#endif /* SMARSH_ABSTRACT_H */
