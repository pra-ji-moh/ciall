/*
 * ob_life.h -- a life of its own: the being acting from its own neurons, led
 * by its own curiosity.
 *
 * Nothing here tells it what to do. Each moment its senses bring what the
 * world did; its brainstem's neurons, restless of themselves, fire when their
 * own activity crosses threshold, and what fires moves its eyes, makes a sound,
 * or buzzes its body. Copies of each command (the efference copy) ripple
 * through the cerebellum, which learns to predict what the senses will bring
 * next. The tegmentum's dopamine is not error, which would trap it in front of
 * anything random, but learning progress: how fast its predictions are
 * getting better, in each part of its world (Oudeyer & Kaplan 2007; Schmidhuber
 * 1991). The basal ganglia learn from that dopamine which of its own urges, in
 * which situation, lead to where it is learning, and lean on the brainstem
 * accordingly. What it does is whatever that makes of it.
 *
 * The same code runs in the simulated crib (ob_crib.c) and, compiled to
 * WebAssembly, on a phone, where its senses are the camera, the microphone,
 * the screen and the phone's motion sensors (phone/).
 *
 * Freestanding C99: no library at all, not even maths, so the phone runs
 * exactly what the tests test.
 */

#ifndef OB_LIFE_H
#define OB_LIFE_H

#define OL_EYE 8                      /* its retina: 8 by 8 */
#define OL_VIS (OL_EYE * OL_EYE)
#define OL_EAR 8                      /* its cochlea: 8 bands, low to high */
#define OL_SKIN 8                     /* its skin: 8 places */
#define OL_MOVE 3                     /* its inner ear: being moved, three axes */
#define OL_SENSE (OL_VIS + OL_EAR + OL_SKIN + OL_MOVE)
#define OL_INPUT (OL_SENSE + 2)       /* and where its eyes point, x and y, 0..1 */

/* its senses, as groups: each has its own predictor in the cerebellum */
enum { OL_SEE, OL_HEAR, OL_FEEL, OL_MOVED, OL_GROUPS };

/* what its brainstem can do */
enum { OL_LOOK_LEFT, OL_LOOK_RIGHT, OL_LOOK_UP, OL_LOOK_DOWN, OL_VOICE, OL_BUZZ, OL_MOTORS };

#define OL_WAVE 8                     /* the efference copy's ripple: how long ago, up to 8 moments */
#define OL_EFF (OL_MOTORS * OL_WAVE)
#define OL_GAZE_BINS 16               /* where it looks, 4 by 4: its places for seeing */
#define OL_RECENT (OL_MOTORS + 1)     /* what it did last (or nothing): its places for the other senses */
#define OL_CTX (OL_GAZE_BINS + OL_RECENT + 1)
#define OL_PLACES (2 * OL_GAZE_BINS)  /* the most places any sense has: for seeing, where it looks, eyes still or moving */
#define OL_SPAN 64                    /* learning progress: its errors over one span of visits against the span before */

/* its hippocampus: every moment, verbatim, as long as there is room (the
   oldest give way once it is full) */
#ifndef OL_MEMORY
#define OL_MEMORY 200000u

#endif
#define OL_INDEX_BITS 16
#define OL_NEAR 0.04f                 /* a memory this close to now (mean difference, 0..1) is "this again" */
#define OL_EXACT 0.006f               /* and this close, exactly this again */

typedef struct {
  unsigned char senses[OL_MEMORY][OL_SENSE];   /* what each moment brought, 0..255 */
  unsigned acts[OL_MEMORY];                    /* and what it had done lately (the last three moments) */
  unsigned stamp[OL_MEMORY];                   /* which moment each place holds (moment + 1; 0 none) */
  unsigned next[OL_GROUPS][OL_MEMORY];         /* each sense's index: the moment before with the same key */
  unsigned head[OL_GROUPS][1u << OL_INDEX_BITS];
  /* where memory can be trusted: for each sense and key, how much better what
     came next last time has predicted than the cerebellum, on average (0: never tried) */
  float trust[OL_GROUPS][1u << OL_INDEX_BITS];
} ol_hippocampus_t;

/* what drives its dopamine */
enum {
  OL_PROGRESS,   /* learning progress: the being */
  OL_ERROR,      /* plain prediction error: a twin that seeks surprise (for comparison) */
  OL_NONE        /* no dopamine: a twin whose basal ganglia never learn (for comparison) */
};

typedef struct {
  /* inputs of each sense's predictor: that sense now, the efference ripple, and a bias */
  float see[5][OL_VIS][OL_VIS + OL_EFF + 1];   /* one per eye movement: still, left, right, up, down */
  float hear[OL_EAR][OL_EAR + OL_EFF + 1];
  float feel[OL_SKIN][OL_SKIN + OL_EFF + 1];
  float moved[OL_MOVE][OL_MOVE + OL_EFF + 1];
} ol_cerebellum_t;

typedef struct {
  unsigned seed, rng;
  unsigned curiosity;                 /* OL_PROGRESS, OL_ERROR or OL_NONE */
  unsigned learning;                  /* 1 while it learns; 0 to examine it as it is */
  unsigned long moments;

  /* brainstem: each motor centre's own restless activity */
  float restless[OL_MOTORS];
  unsigned fired[OL_MOTORS];          /* what it did this moment */
  float urge[OL_MOTORS];              /* what the basal ganglia added to it */
  float eff[OL_EFF];                  /* the ripple: eff[m * OL_WAVE + age] */

  /* cerebellum */
  ol_cerebellum_t cb;
  float x[OL_SENSE + OL_EFF + 1];     /* what the last prediction was made from */
  float cb_expect[OL_SENSE];          /* what the cerebellum alone expects (what it learns from) */
  float expect[OL_SENSE];             /* what it expects the senses to bring next: the cerebellum's
                                         guess, or, where now is a moment it has lived, what came next then */
  unsigned gaze_move;                 /* which eye-movement predictor made it */
  float surprise[OL_SENSE];           /* what came, minus what it expected: what it notices */
  float err[OL_GROUPS];               /* each sense's error this moment */
  float energy[OL_GROUPS];            /* and how much came */

  /* tegmentum: in each sense, in each place, its errors over this span of
     visits and the last; how much they came down (its learning progress); a
     fast memory of its error, for the twin that seeks surprise; and each
     sense's usual error, its scale */
  float span[OL_GROUPS][OL_PLACES], last[OL_GROUPS][OL_PLACES], progress[OL_GROUPS][OL_PLACES];
  float fast[OL_GROUPS][OL_PLACES], usual[OL_GROUPS];
  unsigned visits[OL_GROUPS][OL_PLACES];
  unsigned place[OL_GROUPS];          /* where each sense was, the last moment */
  float dopamine;

  /* basal ganglia: an urge for each act in each situation, a critic, their traces */
  float ctx[OL_CTX];
  float act[OL_MOTORS][OL_CTX], trace[OL_MOTORS][OL_CTX];
  float value[OL_CTX], vtrace[OL_CTX];

  /* hippocampus */
  unsigned remembers;                 /* 1: it keeps every moment (0: a twin without, for comparison) */
  unsigned long recalls[OL_GROUPS];   /* moments each sense was predicted from memory */
  float remembered[OL_SENSE];         /* what came next, the last time it lived this */
  int recalled[OL_GROUPS];            /* the key it was recalled under (-1: nothing recalled) */

  /* what it has done, for the diary */
  unsigned long acts[OL_MOTORS];

  ol_hippocampus_t hip;               /* last: it is most of its size */
} ol_life_t;

/* a newborn: knows nothing, predicts nothing, wants nothing in particular */
void ol_born(ol_life_t *L, unsigned seed, unsigned curiosity);

/*
 * One moment. `in` is what its senses bring now (OL_INPUT floats: the retina,
 * the cochlea's bands, the skin, being moved, all 0..1, then where its eyes
 * point). Returns what it does: bit m set if motor centre m fired.
 */
unsigned ol_live(ol_life_t *L, const float *in);

/* where in each sense it is learning fastest now (its curiosity, as it stands) */
float ol_progress(const ol_life_t *L, unsigned group, unsigned place);

/*
 * Coded optogenetics: for us, not for it. Its memories are tagged by the
 * moment that made them, the way active neurons are tagged with a
 * light-sensitive channel (Liu, Ramirez & Tonegawa 2012). Shining light on
 * one brings back exactly what that moment brought. It changes nothing in it,
 * and it never knows.
 */
/* what moment `moment` brought it (OL_SENSE floats); 0 if it no longer holds it */
int ol_light(const ol_life_t *L, unsigned long moment, float *senses);
/* which moment a glimpse (OL_SENSE floats; only its seeing is used) brings back
   to it, its acts lately as `acts` says (bit 3m + age: act m, age moments
   ago, for the last three); -1 if none */
long ol_complete(const ol_life_t *L, const float *cue, unsigned acts);

#endif
