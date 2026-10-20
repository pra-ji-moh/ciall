/*
 * ob.h -- the open brain: a brain that grows itself from a flat sheet of
 * cells, and can be read like a map at every moment.
 *
 * ============================================================
 * WHAT IT IS
 * ============================================================
 * Not a network drawn by hand. A genome (a short list of developmental
 * settings, ob_genome_t) and a few rules taken from developmental biology:
 *
 *   the neural plate   a flat sheet of progenitor cells, head end to tail end
 *                      along x, midline to edge along y (both sides)
 *   folding            the plate rolls into a tube: its midline becomes the
 *                      floor of the tube, its two edges meet as the roof, and
 *                      left and right become the two sides of the brain
 *   morphogens         three signals, each falling off with distance: SHH from
 *                      the floor, BMP from the roof, Wnt rising towards the
 *                      tail. Every cell reads the three where it sits and takes
 *                      a fate by thresholds (Wolpert's "French flag"). Regions
 *                      are not placed; they are where the thresholds fall
 *   areas              within the cortex, two opposed gradients along the head
 *                      to tail axis (as Pax6 and Emx2 are) divide it into
 *                      frontal, parietal, temporal and occipital areas; the
 *                      edge of the cortex nearest the roof becomes hippocampus
 *   eyes               two optic vesicles grow out of the diencephalon and
 *                      become the retinas
 *   neurons            each progenitor makes a column of neurons (Rakic's
 *                      radial units). In the cortex they are placed inside
 *                      out: the first born deepest (layer 6), each later one
 *                      climbing past the ones before it. Inhibitory neurons
 *                      are born below the cortex (in the ganglionic eminences)
 *                      and migrate into it sideways, as real ones do
 *   wiring             connections first grow by matching chemical labels
 *                      (Sperry's chemoaffinity): a source neuron's position in
 *                      its sheet is a label, and it finds targets whose
 *                      position label matches, with a spread. So the retina's
 *                      map lands on the thalamus and on the visual cortex in
 *                      order before anything has been seen. Many more are made
 *                      than will be kept: experience prunes them later
 *
 * ============================================================
 * WHAT "OPEN" MEANS
 * ============================================================
 * Everything is readable: the genome is a text file of named numbers; the
 * grown brain writes itself out as a text map (regions, layers, counts, and
 * every projection with its rule); and it draws itself as pictures. Nothing
 * is hidden in weights nobody can read: each neuron knows where it came
 * from, when it was born, and which rule made each of its connections.
 *
 * Deterministic: the same genome gives the same brain, connection for
 * connection.
 *
 * What it is not: a model of 86 billion neurons, or of any one animal. The
 * rules are biology's principles, simplified; the regions and projections are
 * a scaffold for a being to grow on, and say so.
 */

#ifndef OB_H
#define OB_H

#include <stdio.h>

#define OB_PLATE_MAX_W 96
#define OB_PLATE_MAX_H 64
#define OB_MAX_NEURONS 60000
#define OB_MAX_SYN 2400000
#define OB_RETINA 24          /* each retina is OB_RETINA x OB_RETINA */
#define OB_COCHLEA 32         /* each cochlea: hair cells from low pitch to high */
#define OB_LAYERS 5           /* cortex layers 6, 5, 4, 3, 2, deepest first */

typedef enum {
  OB_NONE = 0,
  /* forebrain: telencephalon */
  OB_FRONTAL, OB_PARIETAL, OB_TEMPORAL, OB_OCCIPITAL, OB_HIPPOCAMPUS, OB_AMYGDALA,
  OB_BASAL_GANGLIA,
  /* forebrain: diencephalon */
  OB_THALAMUS, OB_HYPOTHALAMUS,
  /* midbrain */
  OB_TECTUM, OB_TEGMENTUM,
  /* hindbrain */
  OB_CEREBELLUM, OB_BRAINSTEM,
  /* spinal cord */
  OB_SPINAL_SENSORY, OB_SPINAL_MOTOR,
  /* outside the tube */
  OB_RETINA_L, OB_RETINA_R,
  /* added for the womb's senses */
  OB_OLFACTORY,             /* the olfactory bulb, at the very front of the forebrain */
  OB_COCHLEA_L, OB_COCHLEA_R,   /* the ears, outside the tube like the retinas */
  OB_REGIONS
} ob_region_t;

typedef enum { OB_LEFT = 0, OB_RIGHT = 1 } ob_side_t;

/* The genes: every number development reads, by name. */
typedef struct {
  unsigned seed;
  unsigned plate_w, plate_h;      /* plate size: head to tail, and across (both sides) */
  double shh_reach, bmp_reach;    /* how far the floor and roof signals reach, as a fraction of the half-width */
  /* along the head to tail axis, where the Wnt level divides the tube */
  double olfactory_end;           /* the front tip of the forebrain's roof side becomes the olfactory bulb */
  double tel_end, die_end, mes_end, rho_end;
  double cerebellum_end;          /* the front part of the hindbrain's roof side */
  /* the cortex's areas: where the two opposed gradients cross, fractions of the cortex's length */
  double frontal_end, parietal_end, temporal_end;
  double hippocampus_edge;        /* how much of the cortex's roof-side edge is hippocampus */
  double amygdala_band;           /* the band at the cortex's floor-side edge, at the temporal pole */
  unsigned cortex_neurons;        /* per progenitor column in the cortex (the layers) */
  unsigned other_neurons;         /* per progenitor elsewhere */
  unsigned cerebellum_neurons;    /* granule cells are the most numerous in any brain */
  double interneuron_fraction;    /* of cortical neurons, inhibitory, migrating in from below */
  unsigned synapses_in;           /* inputs grown per neuron, before pruning */
  double spread;                  /* how widely a projection lands, as a fraction of its target sheet */
  double callosum;                /* fraction of upper-layer cortical outputs that cross to the other side */
} ob_genome_t;

typedef struct {
  unsigned char region, side, layer;   /* layer 0..4 = cortex 6..2; in the thalamus its nucleus 0..3; 0 elsewhere */
  unsigned char inhibitory;
  unsigned short birth;                /* birth order within its column */
  unsigned short px, py;               /* where its progenitor sat on the plate */
  float sx, sy;                        /* position in its region's sheet, 0..1 each way */
  unsigned syn_first, syn_n;           /* its inputs */
} ob_neuron_t;

typedef struct {
  unsigned from;                       /* the neuron the input comes from */
  short weight;                        /* in thousandths; negative for inhibitory sources */
  unsigned char rule;                  /* which wiring rule grew it (ob_rule_name) */
} ob_synapse_t;

/* The plate, after folding and the morphogens: each progenitor's fate. */
typedef struct {
  unsigned w, h;
  unsigned char fate[OB_PLATE_MAX_W][OB_PLATE_MAX_H];
  float shh[OB_PLATE_MAX_W][OB_PLATE_MAX_H], bmp[OB_PLATE_MAX_W][OB_PLATE_MAX_H];
  float wnt[OB_PLATE_MAX_W];
} ob_plate_t;

typedef struct {
  ob_genome_t genes;
  ob_plate_t plate;
  unsigned n_neurons, n_syn;
  ob_neuron_t neuron[OB_MAX_NEURONS];
  ob_synapse_t syn[OB_MAX_SYN];
  unsigned count[OB_REGIONS][2];       /* neurons per region and side */
  unsigned rule_count[64];             /* synapses grown by each rule */
  unsigned dropped;                    /* neurons or synapses that did not fit */
} ob_brain_t;

void ob_genome_default(ob_genome_t *g);
/* Read "name value" lines over the defaults. 0 when the file could not be read. */
int ob_genome_load(ob_genome_t *g, const char *path);
void ob_genome_save(const ob_genome_t *g, FILE *out);

/* Grow a whole brain from the genome: plate, folding, fates, neurons, wiring. */
void ob_grow(ob_brain_t *b, const ob_genome_t *g);

/* The stages, separately, for watching it grow. */
void ob_stage_plate(ob_brain_t *b);      /* the plate folds; morphogens; fates */
void ob_stage_neurons(ob_brain_t *b);    /* columns of neurons, inside out; interneurons migrate */
void ob_stage_wiring(ob_brain_t *b);     /* first connections, by chemoaffinity */

const char *ob_region_name(unsigned r);
const char *ob_rule_name(unsigned rule);

/* The brain as a readable map: regions, layers, counts, projections. */
void ob_write_map(const ob_brain_t *b, FILE *out);

/* Pictures (PNG). Each returns 0 on success. */
int ob_draw_plate(const ob_brain_t *b, const char *path);        /* fates on the plate */
int ob_draw_morphogens(const ob_brain_t *b, const char *path);   /* SHH, BMP, Wnt */
int ob_draw_cortex(const ob_brain_t *b, const char *path);       /* both hemispheres, by area and layer */
int ob_draw_retinotopy(const ob_brain_t *b, const char *path);   /* the retina's map, as it lands on the visual cortex */
/* Any picture: w*h*3 bytes of red, green, blue. */
int ob_write_png_rgb(const char *path, const unsigned char *rgb, unsigned w, unsigned h);

/* ============================================================
 * STAGE 1: LIFE IN THE WOMB (ob_womb.c)
 * ============================================================
 * Neurons fire: each neuron's activity is between 0 and 1, the sum of what its
 * inputs carry (activity times weight), clipped. One moment of life is three
 * steps of the whole brain at once:
 *
 *   1. what started this moment acts: a twitch, generated in the brainstem
 *      (as fetal twitches are, especially in sleep), drives the muscles it maps
 *      to, and its copy (the efference copy) reaches the cerebellum
 *   2. the body moves: a muscle that contracts presses the womb wall there; an
 *      arm that moves brushes the face (hand to mouth); and now and then the
 *      mother presses from outside, which no twitch caused. The skin's touch
 *      reaches the thalamus, and with it the cerebellum's prediction
 *   3. the thalamus reaches the parietal cortex
 *
 * The cerebellum learns, by the delta rule (as its climbing fibres teach it),
 * to make its prediction cancel the touch that arrives with it: what is left in
 * the thalamus is touch nothing predicted. Nobody tells it which touch was its
 * own. Touch it learns to cancel is "me"; touch that stays is "not me".
 */

typedef struct {
  unsigned days;                       /* epochs of womb life measured */
  double self_left[64], other_left[64];      /* share of touch left after prediction */
  double double_left[64];                    /* the same, for hand-to-face touch */
  double parietal_self[64], parietal_other[64];   /* parietal response per unit of touch */
  unsigned self_n[64], other_n[64];
} ob_womb_stats_t;

/*
 * Live in the womb for `days` epochs of `moments_per_day` moments each,
 * learning the touch-prediction connections in place (they are written back
 * into the brain's synapse weights, readable like any other).
 */
void ob_womb_touch(ob_brain_t *b, unsigned days, unsigned moments_per_day, ob_womb_stats_t *out);

/*
 * Smell, and "is this good or bad for me". The amniotic fluid carries the
 * mother's meals: smells arrive, and with some of them sweet (her meal) or
 * bitter (her stress), whose value is innate (newborns smile at sweet and
 * grimace at bitter before they have tasted anything). The olfactory bulb maps
 * each smell to its own patch, as glomeruli do; the amygdala keeps two kinds of
 * neuron, for approach and for avoidance (as it does: Namburi et al. 2015). A
 * smell's connections to one kind strengthen when it comes with a value of
 * that kind. Nobody labels the smells; only taste has a value of its own.
 */
#define OB_SMELLS 8u

typedef struct {
  unsigned days;
  /* for each smell: approach minus avoidance in the amygdala, -1..1, before
     the womb and after */
  double before[OB_SMELLS], after[OB_SMELLS];
  double day_mother[64], day_anise[64], day_stress[64];   /* three smells, day by day */
} ob_smell_stats_t;

void ob_womb_smell(ob_brain_t *b, unsigned days, unsigned moments_per_day, ob_smell_stats_t *out);
void ob_write_smell(const ob_smell_stats_t *st, FILE *out);

/*
 * Hearing, and the heartbeat. Each low sound starts a wave that crosses the
 * cerebellum one step a moment, so its cells hold how long ago the sound came
 * (a travelling wave as a memory of the recent past). From the wave, the
 * cerebellum learns when the next sound comes, and cancels it in the thalamus's
 * hearing nucleus. The mother's heart, lub then dub, every eight moments, comes
 * to be expected; her voice, irregular, cannot be. A beat that should come and
 * does not leaves a prediction with nothing to meet it: a signal of absence.
 */
typedef struct {
  unsigned days;
  double beat_left[64], voice_left[64];   /* share still heard, day by day */
  double expected_beat_left;              /* after: a beat that comes when expected */
  double missed_beat;                     /* after: the signal when an expected beat does not come, as a share of a beat */
  double silence;                         /* after: the same, in silence with nothing expected */
} ob_heart_stats_t;

void ob_womb_heartbeat(ob_brain_t *b, unsigned days, unsigned moments_per_day, ob_heart_stats_t *out);
void ob_write_heart(const ob_heart_stats_t *st, FILE *out);

/* What the womb did, in words and numbers, for the map. */
void ob_write_womb(const ob_womb_stats_t *st, FILE *out);

/* After the womb: a map of the body on the parietal cortex, each place
   coloured by how much of its own touch it has learned to predict. */
int ob_draw_self(const ob_brain_t *b, const ob_womb_stats_t *st, const char *path);

#endif /* OB_H */
