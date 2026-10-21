/*
 * ob_one.h -- one brain: the grown brain, living.
 *
 * The brain grown from the genome (ob_grow.c), with what the womb taught it
 * (ob_womb.c), is the one that lives. Nothing is added beside it: the world
 * arrives on its own sensory neurons, its acts are zones of its own brainstem,
 * its predictions are learned on its own cerebellum's synapses to the thalamus,
 * its choices on its own basal ganglia's synapses to the brainstem. Every
 * number that changes is a neuron's activity or a synapse's weight, and can be
 * read off the brain like any other.
 *
 *   the world        its retinas (each pixel of the 8 by 8 view on 3 by 3 of
 *                    their cells), its cochleas (each band on 4 hair cells), its
 *                    skin (eight places along its body), its vestibules (being
 *                    moved), and where its eyes point, held in the tectum's map
 *   its acts         zones of its brainstem: eyes left (the left side's front),
 *                    eyes right (the right's), up, down, its voice, a jerk of
 *                    the body. Each neuron is restless of itself; a zone acts
 *                    when one of its neurons, with what the basal ganglia add,
 *                    crosses threshold, and its burst takes the whole zone
 *   expecting        copies of its acts, and the senses, reach the cerebellum
 *                    and linger there a few moments; its synapses onto the
 *                    thalamus (touch, hearing and vision nuclei, from the genome)
 *                    learn by the delta rule to cancel what each sense will
 *                    bring. What is left in the thalamus is its surprise
 *   wanting          the tegmentum's dopamine is its learning progress, in each
 *                    situation; the basal ganglia, hearing the cortex, learn on
 *                    their synapses to the brainstem (from nothing) which urges
 *                    lead to it; a critic in them, how good a situation is
 *
 * It takes the same senses as ob_life.c (OL_INPUT floats, the same layout) and
 * returns the same acts, so the same worlds and the same checks serve both.
 */

#ifndef OB_ONE_H
#define OB_ONE_H

#include "ob.h"
#include "ob_life.h"

#define OB_HIP_BYTES 32            /* the hippocampus's neurons, a bit each (up to 256) */

typedef struct {
  ob_brain_t brain;                   /* its brain: anatomy, and every weight */
  unsigned curiosity;                 /* OL_PROGRESS, OL_ERROR or OL_NONE */
  unsigned learning;
  unsigned rng;
  unsigned long moments;

  float act[OB_MAX_NEURONS];          /* every neuron's activity, now */
  float usual_in[OB_MAX_NEURONS];     /* each neuron's usual input: it adapts to it, and answers what differs */
  float trace[OB_MAX_NEURONS];        /* each cortical neuron's recent firing */
  float threshold[OB_MAX_NEURONS];    /* and its threshold, raised when it fires too often (homeostasis) */
  unsigned cortex_learns;             /* 1: its cortex learns (after birth); 0: a twin whose cortex does not */
  unsigned trace_rule;                /* 1: its temporal lobe learns from its recent firing (0: from now's only) */
  float held[OL_WAVE][4096];          /* the cerebellum's delay lines: what reached each of its cells, lately */
  float w[OB_MAX_SYN];                /* every synapse's weight, while it lives */
  float restless[OB_MAX_NEURONS];     /* the brainstem's own activity */
  float elig[OB_MAX_SYN];             /* the basal ganglia's synapses: what each did lately */
  float value[OB_MAX_NEURONS];        /* the critic, on the basal ganglia's neurons */
  float vtrace[OB_MAX_NEURONS];

  unsigned fired[OL_MOTORS];
  float rate[OL_MOTORS];              /* how often each zone acts, of late */
  unsigned gaze_move;
  unsigned ago[OL_MOTORS];            /* moments since each zone last acted */

  /* the thalamus, this moment: each sense's error and energy, as in ob_life */
  float err[OL_GROUPS], energy[OL_GROUPS];
  /* tegmentum: learning progress, as in ob_life */
  float span[OL_GROUPS][OL_PLACES], last[OL_GROUPS][OL_PLACES], progress[OL_GROUPS][OL_PLACES];
  float fast[OL_GROUPS][OL_PLACES], usual[OL_GROUPS];
  unsigned visits[OL_GROUPS][OL_PLACES];
  unsigned place[OL_GROUPS];
  float dopamine, before;             /* this moment's, and the critic's last estimate */
  unsigned long acts[OL_MOTORS];

  /* hippocampus: the sketch's (ob_life.c), the same code; each sense files
     its moments under its own pattern, and a recalled moment drives that
     sense's relays again */
  unsigned remembers;                 /* 1: it keeps every moment (0: a twin without) */
  int recalled[OL_GROUPS];            /* the key each sense recalled under, for the coming moment (-1 none) */
  int trusted[OL_GROUPS];             /* and whether it follows memory there */
  long recalled_moment[OL_GROUPS];
  float memdrive[OB_MAX_NEURONS];     /* what a recalled moment drives into each relay */
  unsigned long recalls[OL_GROUPS];
  /* and the hippocampus's own: whole moments filed under which of its own
     neurons fired (an episode), recalled from the nearest pattern */
  unsigned episodes;                  /* 1: it recalls episodes (0: a twin without) */
  int via[OL_GROUPS];                 /* how each sense's expectation was recalled: 0 none, 1 as a thing, 2 as an episode */
  float episode_trust[OL_GROUPS];     /* how much better episodes have predicted, on average */
  unsigned long episode_recalls[OL_GROUPS];
  unsigned char pattern[OL_MEMORY][OB_HIP_BYTES];
  ol_hippocampus_t hip;               /* last: it is much of its size */
} ob_one_t;

/* Its life begins from a grown brain (copied in, with whatever the womb taught it). */
void ob_one_born(ob_one_t *O, const ob_brain_t *grown, unsigned seed, unsigned curiosity);

/* One moment: `in` as for ol_live. Returns what it does. */
unsigned ob_one_live(ob_one_t *O, const float *in);

/* What it learned, written into its brain's synapses (in thousandths). */
void ob_one_settle(ob_one_t *O);

/* Which zone of the brainstem a neuron belongs to (OL_MOTORS: none). */
unsigned ob_one_zone(const ob_neuron_t *n);

/* to watch it: what the senses drove into each neuron at the last step, and
   what the cerebellum predicted there (meaningful in the thalamus) */
const float *ob_one_drive(void);
const float *ob_one_pred(void);
const float *ob_one_pred_cerebellum(void);   /* the cerebellum's part of it (the rest is the cortex's) */

/*
 * Coded optogenetics, for us (as for the sketch): what moment `moment` brought
 * it (OL_SENSE floats), 0 if it no longer holds it. And which moment sense g
 * is reminded of now (-1: none).
 */
int ob_one_light(const ob_one_t *O, unsigned long moment, float *senses);
long ob_one_reminded(const ob_one_t *O, unsigned g);

#endif
