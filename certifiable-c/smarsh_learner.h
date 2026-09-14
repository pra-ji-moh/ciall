/*
 * smarsh_learner.h -- the autonomous learner, in C.
 *
 * COMPILED AND RUN (strict C99, zero warnings). test_learner.c passes its
 * 30 checks, and diff_learner.c ran 3,734 learner runs identical to the
 * Python original, python-reference/baby_auto.py. The C found two real
 * bugs in the original, both fixed in both languages:
 *   - with a lying sensor, recovery could loop forever (fix: per-scene
 *     distrust);
 *   - the learner gave up as STUCK when no single question narrowed the
 *     goal, even when several together would (s % 7 over 16 * hi + lo:
 *     neither digit alone helps). Found on the first run with 256
 *     situations. Fix: an exact joint-relevance test before giving up.
 * Every outcome but CONTRADICTED is reached by some test; CONTRADICTED is
 * provably unreachable (see the enum below).
 *
 * ============================================================
 * WHAT IT DOES
 * ============================================================
 * No curriculum. Given a goal and a scene, it works out the rest:
 *
 *   cannot even PHRASE the goal   acquire the smallest capacity that
 *                                 would let it, and try again
 *   can phrase it, cannot settle  work out which single measurement
 *                                 would settle it, and take that one
 *   settled                       answer, with a witness
 *   the evidence contradicts      drop the most recent claim, DISTRUST
 *                                 its source for the scene, carry on
 *
 * ============================================================
 * THE SENSOR IS DATA, NOT A CALLBACK
 * ============================================================
 * The Python version read the truth directly. Here the caller hands in a
 * scene: what each capacity REPORTS. A truthful caller fills it from the
 * real situation; a test fills it with a lie. The learner cannot tell the
 * difference, which is exactly the property under test, and it keeps the
 * file free of function pointers.
 *
 * ============================================================
 * WHY THE LOOP TERMINATES -- AND THE VERSION OF THIS THAT WAS WRONG
 * ============================================================
 * Every pass does one of four things, each bounded independently of what
 * the world reports:
 *
 *   acquire   strictly refines the partition. <= n_table passes.
 *   measure   takes a capacity neither in memory nor distrusted.
 *             <= n_table per scene.
 *   recover   pops at least one claim and distrusts its source, which is
 *             then never re-measured this scene. Memory holds at most one
 *             entry per capacity from the glance and one from measuring,
 *             so <= 2 * n_table passes.
 *   return    once.
 *
 * Total <= 4 * n_table + 1, which is SL_MAX_PASSES at the table limit.
 * The loop is bounded by that constant; hitting it returns
 * SM_ERR_INTERNAL_INVARIANT, because the argument above says it cannot
 * happen and a proof that is wrong should fail loudly.
 *
 * The Python version once claimed instead that every pass "makes progress
 * that cannot be undone". Recovery IS an undo: with no distrust, a sensor
 * returning an impossible value for the one measurement that settles the
 * goal was measured, retracted and measured again forever. Writing this
 * port forced the bound to be stated, and stating it exposed the gap.
 *
 * ============================================================
 * WHAT A WITNESS HERE DOES AND DOES NOT MEAN
 * ============================================================
 * It certifies ENTAILMENT, not truth. A lying sensor whose lie stays
 * consistent with everything else the learner saw produces an answer that
 * genuinely follows from what it was told, and the witness checks. That is
 * measured, not supposed: 2.4% of 576 lying-sensor runs end that way, and
 * verify_auto.py pins it as an expected limit. The claim that survives is
 * that every wrong answer rests on a false input the witness names.
 *
 * ============================================================
 * MEMORY
 * ============================================================
 * No malloc. The capacity table is caller-owned. The one large buffer, a
 * contiguous copy of the held questions that sr_refine needs, lives in the
 * learner struct rather than on the stack: sizeof(sr_query_t) is 268
 * bytes, and a 17-entry gather on the stack would be about 4.5KB per
 * call, which is the undocumented-frame problem the audit flagged.
 */

#ifndef SMARSH_LEARNER_H
#define SMARSH_LEARNER_H

#include "smarsh_reason.h"

#define SL_MAX_CAPS 16u
#define SL_MAX_MEMORY (2u * SL_MAX_CAPS)
#define SL_MAX_LOG 64u
#define SL_NAME_LEN 16u
#define SL_NONE 0xFFFFu
#define SL_MAX_PASSES (4u * SL_MAX_CAPS + 1u)

typedef struct {
  char name[SL_NAME_LEN];
  sr_query_t q;       /* over situations */
  int immediate;      /* a glance reports it; otherwise it must be measured */
} sl_capacity_t;

/* What each capacity reports in the scene in front of the learner. */
typedef struct {
  unsigned reading[SL_MAX_CAPS];
} sl_scene_t;

typedef struct {
  unsigned cap;
  unsigned answer;
} sl_obs_t;

typedef enum {
  SL_EV_ACQUIRE = 0,   /* cap, worlds before, worlds after */
  SL_EV_MEASURE = 1,   /* cap, value reported */
  SL_EV_RETRACT = 2    /* cap, value dropped; the source is now distrusted */
} sl_ev_kind_t;

typedef struct {
  sl_ev_kind_t kind;
  unsigned cap;
  unsigned a;
  unsigned b;
} sl_event_t;

typedef enum {
  SL_OUT_DERIVED = 0,
  SL_OUT_CANNOT_PHRASE = 1,   /* no capacity left that could let it ask */
  SL_OUT_STUCK = 2,           /* can ask, nothing available settles it */
  /* Never produced, and provably so: sl_init rejects zero situations, so
     a partition has at least one cell, and with every claim dropped every
     cell is live. Recovery therefore always succeeds before memory runs
     out. Kept so the outcome set matches the Python reference; sl_pursue
     returns SM_ERR_INTERNAL_INVARIANT, loudly, if the proof is ever wrong. */
  SL_OUT_CONTRADICTED = 3
} sl_outcome_t;

typedef struct {
  sl_outcome_t outcome;
  unsigned value;      /* meaningful only when outcome is SL_OUT_DERIVED */
  double H;
  unsigned worlds;
  unsigned measured;
  unsigned acquired;
  int proved;          /* the witness re-derives the verdict */
  sl_event_t log[SL_MAX_LOG];
  unsigned n_log;
} sl_report_t;

typedef struct {
  const sl_capacity_t *table;
  unsigned n_table;
  unsigned n_situations;

  unsigned held[SL_MAX_CAPS];     /* acquired capacities, in order acquired */
  unsigned n_held;

  sl_obs_t memory[SL_MAX_MEMORY];
  unsigned n_memory;
  int distrusted[SL_MAX_CAPS];    /* per scene */

  unsigned measurements;
  unsigned acquisitions;

  /* Two large buffers, kept here rather than on the stack (see MEMORY).
     scratch: the held questions gathered contiguously for sr_refine.
     measure_buf: candidate measurements projected for sr_choose. They are
     separate on purpose. Sharing one would be safe only because of the
     order the loop happens to call things in, and an invariant that rests
     on call order is the kind that breaks silently on the next edit. */
  sr_query_t scratch[SL_MAX_CAPS + 1u];
  sr_query_t measure_buf[SL_MAX_CAPS];
  unsigned measure_key[SL_MAX_CAPS];
} sl_learner_t;

sm_status_t sl_init(sl_learner_t *L, const sl_capacity_t *table,
                    unsigned n_table, unsigned n_situations);

/* Fill a scene from the real situation: every capacity tells the truth. */
sm_status_t sl_scene_truthful(const sl_capacity_t *table, unsigned n_table,
                              unsigned situation, sl_scene_t *out);

/* Give the learner a capacity without it having earned one, for setting
   up a grown learner. Refuses a duplicate. */
sm_status_t sl_grant(sl_learner_t *L, unsigned cap);

/* The partition over what it holds, optionally with one more capacity.
   Pass SL_NONE for none. */
sm_status_t sl_partition(sl_learner_t *L, unsigned extra, sr_partition_t *out);

/* Rebuilt from what it OBSERVED, never from what it concluded, so a newly
   acquired distinction pays off against memory already in hand. */
sm_status_t sl_state(const sl_learner_t *L, const sr_partition_t *p,
                     sr_state_t *out);

/* The glance: resets memory to the immediate capacities' readings, and
   resets trust along with the scene. */
sm_status_t sl_glance(sl_learner_t *L, const sl_scene_t *scene);

/* Drop the most recent claim and distrust its source, until the worlds
   come back. Writes the dropped claim to *out, or returns
   SM_ERR_EMPTY_DOMAIN if even an empty memory is contradicted. */
sm_status_t sl_recover(sl_learner_t *L, const sr_partition_t *p,
                       sl_obs_t *out);

sm_status_t sl_pursue(sl_learner_t *L, const sr_query_t *goal,
                      const sl_scene_t *scene, sl_report_t *out);

#endif /* SMARSH_LEARNER_H */
