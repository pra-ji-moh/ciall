/*
 * smarsh_time.h -- time and change: what actions do, where things might
 * be now, what to do without knowing, and which states are really the same.
 *
 * Every world before this was a still picture. Here things change: the
 * learner acts, and the state moves. Nothing new is needed in kind. What
 * is possible NOW is a set of states; an action carries every possible
 * state forward; a sensor eliminates the ones that disagree with it. That
 * is the whole of reasoning over time, and it is elimination again, with
 * no probabilities anywhere.
 *
 *   LEARNING WHAT ACTIONS DO. Watched transitions (state, action, next
 *   state) fill in a table of possible successors. A transition never
 *   watched is not guessed: from there, anything is possible.
 *
 *   TRACKING. step: the set of states that could follow; sense: keep only
 *   the states the sensor reading allows. After a few of each, a learner
 *   that did not know where it started can know exactly where it is.
 *
 *   PLANNING WITHOUT KNOWING. A plan that GUARANTEES the goal, from every
 *   state still possible, using only transitions it has seen. Found by
 *   breadth-first search over sets of possible states, so the first plan
 *   found is a shortest one. If the search runs out of new sets to try,
 *   that is a PROOF that no plan of any length exists, given what it knows.
 *   Every plan is re-checked by simulating it.
 *
 *   WHICH STATES ARE REALLY DIFFERENT. Two states that look the same to
 *   every sensor and lead, under every action, to states that are again
 *   the same, can never be told apart by acting and looking. Merging them
 *   gives the smallest model with the same behaviour (bisimulation, by
 *   partition refinement). This is refinement again (sr_refine), but the
 *   distinctions that survive are the ones TIME can reveal.
 */

#ifndef SMARSH_TIME_H
#define SMARSH_TIME_H

#include "smarsh_core.h"

#define TM_MAX_STATES 64u
#define TM_MAX_ACTIONS 8u
#define TM_MAX_PLAN 32u
#define TM_MAX_BELIEFS 4096u
#define TM_NAME 20u

typedef uint64_t tm_set_t;   /* bit s: state s is possible */

typedef struct {
  unsigned n_states;
  unsigned n_actions;
  char state_name[TM_MAX_STATES][TM_NAME];
  char action_name[TM_MAX_ACTIONS][TM_NAME];
  tm_set_t next[TM_MAX_ACTIONS][TM_MAX_STATES];   /* successors seen */
  tm_set_t seen[TM_MAX_ACTIONS];                  /* bit s: (s, a) watched */
  uint8_t sensor[TM_MAX_STATES];                  /* what looking shows */
} tm_model_t;

sm_status_t tm_init(tm_model_t *m, unsigned n_states, unsigned n_actions);
tm_set_t tm_all(const tm_model_t *m);

/* One watched transition. */
sm_status_t tm_watch(tm_model_t *m, unsigned state, unsigned action, unsigned next);

/* Everything that could follow from any state in `now` under `action`.
   *unseen: how many states in `now` whose outcome under this action was
   never watched; each of those makes every state possible. */
tm_set_t tm_step(const tm_model_t *m, tm_set_t now, unsigned action, unsigned *unseen);

/* Keep the states whose sensor shows `reading`. */
tm_set_t tm_sense(const tm_model_t *m, tm_set_t now, unsigned reading);

typedef struct {
  int found;
  int proved_impossible;   /* the search ran out: no plan exists */
  unsigned length;
  unsigned action[TM_MAX_PLAN];
  unsigned explored;       /* sets of states considered */
} tm_plan_t;

/* A shortest plan that takes every state in `from` into `goal`, using
   only watched transitions. */
sm_status_t tm_plan(const tm_model_t *m, tm_set_t from, tm_set_t goal, tm_plan_t *out);

/* Re-check a plan by running it. 1 if every possible end state is a goal. */
int tm_check_plan(const tm_model_t *m, tm_set_t from, tm_set_t goal, const tm_plan_t *plan);

/* The smallest model with the same behaviour: class[s] for every state,
   *n_classes of them. Only watched transitions count as behaviour. */
sm_status_t tm_merge(const tm_model_t *m, unsigned *cls, unsigned *n_classes);

#endif /* SMARSH_TIME_H */
