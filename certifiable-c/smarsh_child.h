/*
 * smarsh_child.h -- one learner, one life: a loop with a drive of its
 * own, memory that lasts, and knowledge of what it knows.
 *
 * The other modules are faculties. A child is not faculties taking
 * turns; it is one thing that decides, acts, learns from what happened,
 * and knows where it stands. That is what this is.
 *
 * ============================================================
 * THE LOOP
 * ============================================================
 *   DECIDE   pick the experiment worth doing (below)
 *   ACT      do it: set out pebbles and watch the scale, or pour two
 *            trays together and count
 *   LEARN    fold the result into everything it knows: what can be told
 *            apart (smarsh_abstract), the rule behind it
 *            (smarsh_concept), the law behind the rule (smarsh_law), and
 *            what follows for every number (smarsh_proof)
 *   REFLECT  notice what it can now do that it could not before
 *
 * ============================================================
 * THE DRIVE: WHERE KNOWLEDGE IS SILENT, OR STICKS ITS NECK OUT
 * ============================================================
 * No reward, no score, no surprise counted statistically. It prefers, in
 * order:
 *
 *   1. what it CANNOT derive at all. Its knowledge says nothing there, and
 *      there is nothing to lose by looking.
 *   2. what it can only derive through a CONJECTURE: a symmetry it has not
 *      seen every case of, or a law used past where it was witnessed.
 *      Those experiments can prove it wrong, which is the only kind of
 *      experiment worth doing when you already have an answer.
 *   3. nothing else. What it can already derive from proved ground is not
 *      worth an experiment, and it says so rather than filling time.
 *
 * That is curiosity as self-correction: look where you are blind, or where
 * you have guessed.
 *
 * ============================================================
 * MEMORY
 * ============================================================
 * Everything it has seen and everything it has worked out stays in one
 * place and lasts across episodes: the raw record, the questions it
 * invented, the tables, the laws, the proofs, and which of those are
 * proved rather than conjectured. A later episode never relearns what an
 * earlier one settled, and an experiment that contradicts a conjecture
 * drops it.
 *
 * ============================================================
 * KNOWING WHERE IT STANDS
 * ============================================================
 * It can say what it can do, what rests on a guess, what it cannot answer
 * AND WHY (a table that only reaches three pebbles; a law never tested
 * that far), and what it wants to find out next. That report is built from
 * the same grounds the answers are, not written by hand.
 */

#ifndef SMARSH_CHILD_H
#define SMARSH_CHILD_H

#include "smarsh_abstract.h"
#include "smarsh_proof.h"

#define CH_MAX_POURS 512u
#define CH_TEXT 160u

/* The world it lives in: it can arrange pebbles in slots and watch the
   scale, or pour two trays together and count the pile. */
typedef struct {
  unsigned n_slots;                      /* slots it can fill, half per tray */
  unsigned max_count;                    /* pebbles it can put in one tray */
  unsigned (*tips)(unsigned pattern);    /* 0 left, 1 balanced, 2 right */
  unsigned (*pour)(unsigned a, unsigned b);
} ch_world_t;

typedef enum {
  CH_COUNT = 0,        /* invented "how many", and worlds of its own */
  CH_WHICH_WAY = 1,    /* the rule of the scale, over its own questions */
  CH_POUR_SEEN = 2,    /* what pouring gives, where it has poured */
  CH_POUR_LAW = 3,     /* and past that, by a law */
  CH_THEOREM = 4,      /* what holds for every number, proved */
  CH_N_ABILITIES = 5
} ch_ability_t;

typedef enum { CH_EXP_SCALE = 0, CH_EXP_POUR = 1, CH_EXP_NONE = 2 } ch_exp_kind_t;

typedef struct {
  ch_exp_kind_t kind;
  unsigned pattern;      /* CH_EXP_SCALE */
  unsigned a, b;         /* CH_EXP_POUR */
  unsigned value;        /* how much it is worth: 3 blind, 2 tests a guess */
  char why[CH_TEXT];
} ch_plan_t;

typedef struct {
  /* what it has seen */
  ab_raw_t scale_seen;
  unsigned n_scale;
  unsigned pour_a[CH_MAX_POURS], pour_b[CH_MAX_POURS], pour_t[CH_MAX_POURS];
  unsigned n_pours;

  /* what it has worked out */
  ab_result_t abstraction;
  int has_abstraction;
  sc_op_t which_way;
  int has_which_way;
  sc_op_t combine;
  int has_combine;
  lw_book_t laws;
  int has_law;
  unsigned law_combine;
  pf_book_t proofs;
  int has_closed_form;
  int commutative_proved;

  /* how it got here */
  int acquired[CH_N_ABILITIES];      /* has it now */
  int ever[CH_N_ABILITIES];          /* has had it at some point */
  unsigned acquired_at[CH_N_ABILITIES];
  unsigned episode, experiments, predictions, refutations, confirmations;
  char note[CH_TEXT];     /* what just happened, in words */
} ch_child_t;

void ch_init(ch_child_t *c);

/* What is worth doing next, and why. CH_EXP_NONE when everything it could
   learn from is already settled on proved ground. */
ch_plan_t ch_decide(const ch_child_t *c, const ch_world_t *w);

/* Do it, fold the result in, and notice what became possible. Returns 1
   if an experiment was run. */
int ch_step(ch_child_t *c, const ch_world_t *w);

/* Answers, each carrying what it rests on. */
typedef enum {
  CH_ANSWERED_SEEN = 0,
  CH_ANSWERED_PROVED = 1,
  CH_ANSWERED_CONJECTURE = 2,   /* rests on a symmetry or law not fully witnessed */
  CH_REFUSED = 3                /* no grounds: it says why */
} ch_standing_t;

ch_standing_t ch_which_way(const ch_child_t *c, unsigned left, unsigned right, unsigned *out,
                           char *why, unsigned cap);
ch_standing_t ch_pour(const ch_child_t *c, unsigned a, unsigned b, uint64_t *out, char *why,
                      unsigned cap);

/* Where it stands: abilities, what rests on guesses, what it cannot do
   and why, and what it wants next. Written into buf. */
void ch_report(const ch_child_t *c, const ch_world_t *w, char *buf, unsigned cap);

extern const char *const CH_ABILITY_NAME[CH_N_ABILITIES];

#endif /* SMARSH_CHILD_H */
