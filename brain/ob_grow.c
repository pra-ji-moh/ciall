/*
 * ob_grow.c -- the open brain growing itself. See ob.h.
 */

#include "ob.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* ---- the genes ----------------------------------------------------------- */

void ob_genome_default(ob_genome_t *g) {
  if (g == 0) return;
  memset(g, 0, sizeof(*g));
  g->seed = 1u;
  g->plate_w = 64u;
  g->plate_h = 40u;
  g->shh_reach = 0.35;
  g->bmp_reach = 0.35;
  g->olfactory_end = 0.04;
  g->tel_end = 0.32;
  g->die_end = 0.42;
  g->mes_end = 0.52;
  g->rho_end = 0.72;
  g->cerebellum_end = 0.58;
  g->frontal_end = 0.35;
  g->parietal_end = 0.72;
  g->temporal_end = 0.45;
  g->hippocampus_edge = 0.14;
  g->amygdala_band = 0.3;
  g->cortex_neurons = 10u;
  g->other_neurons = 3u;
  g->cerebellum_neurons = 12u;
  g->interneuron_fraction = 0.2;
  g->synapses_in = 40u;
  g->spread = 0.08;
  g->callosum = 0.15;
}

typedef struct {
  const char *name;
  int is_unsigned;
  size_t offset;
} ob_gene_t;

#define OB_GU(f) {#f, 1, offsetof(ob_genome_t, f)}
#define OB_GD(f) {#f, 0, offsetof(ob_genome_t, f)}

static const ob_gene_t GENES[] = {
    OB_GU(seed),          OB_GU(plate_w),        OB_GU(plate_h),           OB_GD(shh_reach),
    OB_GD(bmp_reach),     OB_GD(olfactory_end), OB_GD(tel_end),        OB_GD(die_end),           OB_GD(mes_end),
    OB_GD(rho_end),       OB_GD(cerebellum_end), OB_GD(frontal_end),       OB_GD(parietal_end),
    OB_GD(temporal_end),  OB_GD(hippocampus_edge), OB_GD(amygdala_band),   OB_GU(cortex_neurons),
    OB_GU(other_neurons), OB_GU(cerebellum_neurons), OB_GD(interneuron_fraction),
    OB_GU(synapses_in),   OB_GD(spread),         OB_GD(callosum),
};

int ob_genome_load(ob_genome_t *g, const char *path) {
  FILE *f;
  char line[256];
  if (g == 0 || path == 0) return 0;
  f = fopen(path, "r");
  if (f == 0) return 0;
  while (fgets(line, sizeof line, f) != 0) {
    char key[96];
    double v;
    size_t i;
    if (line[0] == '#' || sscanf(line, "%95s %lf", key, &v) != 2) continue;
    for (i = 0u; i < sizeof GENES / sizeof GENES[0]; i++) {
      if (strcmp(key, GENES[i].name) != 0) continue;
      if (GENES[i].is_unsigned) {
        unsigned u = (unsigned)v;
        memcpy((char *)g + GENES[i].offset, &u, sizeof u);
      } else {
        memcpy((char *)g + GENES[i].offset, &v, sizeof v);
      }
    }
  }
  fclose(f);
  if (g->plate_w > OB_PLATE_MAX_W) g->plate_w = OB_PLATE_MAX_W;
  if (g->plate_h > OB_PLATE_MAX_H) g->plate_h = OB_PLATE_MAX_H;
  if (g->plate_w < 8u) g->plate_w = 8u;
  if (g->plate_h < 8u) g->plate_h = 8u;
  return 1;
}

void ob_genome_save(const ob_genome_t *g, FILE *out) {
  size_t i;
  if (g == 0 || out == 0) return;
  for (i = 0u; i < sizeof GENES / sizeof GENES[0]; i++) {
    if (GENES[i].is_unsigned) {
      unsigned u;
      memcpy(&u, (const char *)g + GENES[i].offset, sizeof u);
      fprintf(out, "%s %u\n", GENES[i].name, u);
    } else {
      double v;
      memcpy(&v, (const char *)g + GENES[i].offset, sizeof v);
      fprintf(out, "%s %g\n", GENES[i].name, v);
    }
  }
}

/* ---- names --------------------------------------------------------------- */

const char *ob_region_name(unsigned r) {
  static const char *const N[OB_REGIONS] = {
      "none",          "frontal cortex", "parietal cortex", "temporal cortex", "occipital cortex",
      "hippocampus",   "amygdala",       "basal ganglia",   "thalamus",        "hypothalamus",
      "tectum",        "tegmentum",      "cerebellum",      "brainstem",       "spinal cord (sensory)",
      "spinal cord (motor)", "left retina", "right retina", "olfactory bulb", "left cochlea", "right cochlea"};
  return r < OB_REGIONS ? N[r] : "?";
}

static int is_cortex(unsigned r) { return r >= OB_FRONTAL && r <= OB_OCCIPITAL; }

/* ---- randomness, the same every time -------------------------------------- */

static unsigned RNG;

static unsigned rnd(void) {
  unsigned x = RNG ? RNG : 2463534242u;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  RNG = x;
  return x;
}

static double unit(void) { return (double)(rnd() >> 8) / 16777216.0; }

/* ---- stage 1: the plate folds, the morphogens, the fates ------------------ */

void ob_stage_plate(ob_brain_t *b) {
  const ob_genome_t *g = &b->genes;
  ob_plate_t *p = &b->plate;
  unsigned x, y;
  double half = ((double)g->plate_h - 1.0) / 2.0;
  memset(p, 0, sizeof(*p));
  p->w = g->plate_w;
  p->h = g->plate_h;
  for (x = 0u; x < p->w; x++) {
    double t = (double)x / ((double)p->w - 1.0);   /* Wnt rises from head to tail */
    p->wnt[x] = (float)t;
    for (y = 0u; y < p->h; y++) {
      /* folding: distance from the plate's midline becomes height in the tube,
         from the floor (0) to the roof (1) */
      double d = fabs((double)y - half) / half;
      double shh = exp(-d / g->shh_reach), bmp = exp(-(1.0 - d) / g->bmp_reach);
      int alar = bmp > shh;   /* the roof side: sensory; the floor side: motor */
      double a = alar ? (d - 0.5) / 0.5 : 0.0;   /* how far towards the roof, within the roof side */
      unsigned f;
      p->shh[x][y] = (float)shh;
      p->bmp[x][y] = (float)bmp;
      if (t < g->tel_end) {
        if (!alar) {
          f = OB_BASAL_GANGLIA;
        } else {
          double u = t / g->tel_end;   /* along the cortex, front to back */
          if (t < g->olfactory_end) f = OB_OLFACTORY;
          else if (a > 1.0 - g->hippocampus_edge) f = OB_HIPPOCAMPUS;
          else if (u < g->frontal_end) f = OB_FRONTAL;
          else if (u >= g->parietal_end) f = OB_OCCIPITAL;
          else if (a < g->amygdala_band && u < (g->frontal_end + g->parietal_end) / 2.0) f = OB_AMYGDALA;
          else if (a < g->temporal_end) f = OB_TEMPORAL;
          else f = OB_PARIETAL;
        }
      } else if (t < g->die_end) {
        f = alar ? OB_THALAMUS : OB_HYPOTHALAMUS;
      } else if (t < g->mes_end) {
        f = alar ? OB_TECTUM : OB_TEGMENTUM;
      } else if (t < g->rho_end) {
        f = (alar && t < g->cerebellum_end) ? OB_CEREBELLUM : OB_BRAINSTEM;
      } else {
        f = alar ? OB_SPINAL_SENSORY : OB_SPINAL_MOTOR;
      }
      p->fate[x][y] = (unsigned char)f;
    }
  }
}

/* ---- stage 2: neurons ------------------------------------------------------ */

/* Each region's sheet: the extent of its progenitors on the plate, per side. */
static unsigned BOX_X0[OB_REGIONS][2], BOX_X1[OB_REGIONS][2];
static double BOX_D0[OB_REGIONS][2], BOX_D1[OB_REGIONS][2];

static double depth_of(const ob_plate_t *p, unsigned y) {
  double half = ((double)p->h - 1.0) / 2.0;
  return fabs((double)y - half) / half;
}

static void boxes(const ob_plate_t *p) {
  unsigned r, s, x, y;
  for (r = 0u; r < OB_REGIONS; r++) {
    for (s = 0u; s < 2u; s++) {
      BOX_X0[r][s] = 0xFFFFu;
      BOX_X1[r][s] = 0u;
      BOX_D0[r][s] = 2.0;
      BOX_D1[r][s] = -1.0;
    }
  }
  for (x = 0u; x < p->w; x++) {
    for (y = 0u; y < p->h; y++) {
      unsigned f = p->fate[x][y], s = y < p->h / 2u ? OB_LEFT : OB_RIGHT;
      double d = depth_of(p, y);
      if (x < BOX_X0[f][s]) BOX_X0[f][s] = x;
      if (x > BOX_X1[f][s]) BOX_X1[f][s] = x;
      if (d < BOX_D0[f][s]) BOX_D0[f][s] = d;
      if (d > BOX_D1[f][s]) BOX_D1[f][s] = d;
    }
  }
}

static double norm(double v, double lo, double hi) {
  return hi > lo ? (v - lo) / (hi - lo) : 0.5;
}

static ob_neuron_t *new_neuron(ob_brain_t *b) {
  if (b->n_neurons >= OB_MAX_NEURONS) {
    b->dropped++;
    return 0;
  }
  memset(&b->neuron[b->n_neurons], 0, sizeof(ob_neuron_t));
  return &b->neuron[b->n_neurons++];
}

void ob_stage_neurons(ob_brain_t *b) {
  const ob_genome_t *g = &b->genes;
  const ob_plate_t *p = &b->plate;
  unsigned x, y, i, s, cortical[2] = {0u, 0u};
  b->n_neurons = 0u;
  memset(b->count, 0, sizeof b->count);
  boxes(p);
  for (x = 0u; x < p->w; x++) {
    for (y = 0u; y < p->h; y++) {
      unsigned f = p->fate[x][y], side = y < p->h / 2u ? OB_LEFT : OB_RIGHT, n;
      double sx = norm((double)x, (double)BOX_X0[f][side], (double)BOX_X1[f][side]);
      double sy = norm(depth_of(p, y), BOX_D0[f][side], BOX_D1[f][side]);
      if (f == OB_NONE) continue;
      n = is_cortex(f) ? g->cortex_neurons : f == OB_CEREBELLUM ? g->cerebellum_neurons : g->other_neurons;
      /* the thalamus grows four nuclei from every column, each with the whole
         map: one for each cortical area it relays to (frontal, parietal or touch,
         temporal or hearing, occipital or vision) */
      if (f == OB_THALAMUS) n = 4u * g->other_neurons;
      for (i = 0u; i < n; i++) {
        ob_neuron_t *nn = new_neuron(b);
        if (nn == 0) break;
        nn->region = (unsigned char)f;
        nn->side = (unsigned char)side;
        nn->birth = (unsigned short)i;
        nn->px = (unsigned short)x;
        nn->py = (unsigned short)y;
        nn->sx = (float)sx;
        nn->sy = (float)sy;
        /* inside out: the first born settle deepest, each later one climbs past */
        nn->layer = is_cortex(f) ? (unsigned char)((i * OB_LAYERS) / n) : f == OB_THALAMUS ? (unsigned char)(i % 4u) : 0u;
        b->count[f][side]++;
        if (is_cortex(f)) cortical[side]++;
      }
    }
  }
  /* inhibitory neurons: born in the ganglionic eminences (here, the basal
     ganglia's progenitors), they migrate sideways into the cortex of their side */
  RNG = g->seed * 2654435761u + 7u;
  for (s = 0u; s < 2u; s++) {
    unsigned want = (unsigned)(g->interneuron_fraction / (1.0 - g->interneuron_fraction) * cortical[s]);
    static unsigned origins[OB_PLATE_MAX_W * OB_PLATE_MAX_H][2], cols[OB_PLATE_MAX_W * OB_PLATE_MAX_H][2];
    unsigned n_orig = 0u, n_cols = 0u;
    for (x = 0u; x < p->w; x++) {
      for (y = 0u; y < p->h; y++) {
        unsigned side = y < p->h / 2u ? OB_LEFT : OB_RIGHT;
        if (side != s) continue;
        if (p->fate[x][y] == OB_BASAL_GANGLIA) {
          origins[n_orig][0] = x;
          origins[n_orig][1] = y;
          n_orig++;
        }
        if (is_cortex(p->fate[x][y])) {
          cols[n_cols][0] = x;
          cols[n_cols][1] = y;
          n_cols++;
        }
      }
    }
    for (i = 0u; i < want && n_orig > 0u && n_cols > 0u; i++) {
      unsigned o = rnd() % n_orig, c = rnd() % n_cols, cx = cols[c][0], cy = cols[c][1];
      unsigned f = p->fate[cx][cy];
      ob_neuron_t *nn = new_neuron(b);
      if (nn == 0) break;
      nn->region = (unsigned char)f;
      nn->side = (unsigned char)s;
      nn->inhibitory = 1u;
      nn->birth = (unsigned short)i;
      nn->px = (unsigned short)origins[o][0];   /* where it was born, far from where it ends up */
      nn->py = (unsigned short)origins[o][1];
      nn->sx = (float)norm((double)cx, (double)BOX_X0[f][s], (double)BOX_X1[f][s]);
      nn->sy = (float)norm(depth_of(p, cy), BOX_D0[f][s], BOX_D1[f][s]);
      nn->layer = (unsigned char)(rnd() % OB_LAYERS);
      b->count[f][s]++;
    }
  }
  /* the ears: two otic vesicles form beside the hindbrain; each cochlea's hair
     cells run from low pitch to high */
  for (s = 0u; s < 2u; s++) {
    unsigned r = s == OB_LEFT ? OB_COCHLEA_L : OB_COCHLEA_R;
    for (x = 0u; x < OB_COCHLEA; x++) {
      ob_neuron_t *nn = new_neuron(b);
      if (nn == 0) break;
      nn->region = (unsigned char)r;
      nn->side = (unsigned char)s;
      nn->birth = (unsigned short)x;
      nn->px = (unsigned short)(g->mes_end * (p->w - 1u));
      nn->py = (unsigned short)(s == OB_LEFT ? 0u : p->h - 1u);
      nn->sx = (float)((x + 0.5) / OB_COCHLEA);
      nn->sy = 0.5f;
      b->count[r][s]++;
    }
  }
  /* the eyes: two optic vesicles grow out of the diencephalon's sides */
  for (s = 0u; s < 2u; s++) {
    unsigned r = s == OB_LEFT ? OB_RETINA_L : OB_RETINA_R, ox = 0u, oy = 0u, best = 0u;
    for (x = 0u; x < p->w; x++) {
      for (y = 0u; y < p->h; y++) {
        unsigned side = y < p->h / 2u ? OB_LEFT : OB_RIGHT;
        unsigned lat = side == OB_LEFT ? p->h - y : y;
        if (side == s && p->fate[x][y] == OB_THALAMUS && lat >= best) {
          best = lat;
          ox = x;
          oy = y;
        }
      }
    }
    for (y = 0u; y < OB_RETINA; y++) {
      for (x = 0u; x < OB_RETINA; x++) {
        ob_neuron_t *nn = new_neuron(b);
        if (nn == 0) break;
        nn->region = (unsigned char)r;
        nn->side = (unsigned char)s;
        nn->birth = (unsigned short)(y * OB_RETINA + x);
        nn->px = (unsigned short)ox;
        nn->py = (unsigned short)oy;
        nn->sx = (float)((x + 0.5) / OB_RETINA);
        nn->sy = (float)((y + 0.5) / OB_RETINA);
        b->count[r][s]++;
      }
    }
  }
}

/* ---- stage 3: the first wiring, by chemoaffinity ------------------------- */

/*
 * Each rule says: neurons of this region (and layers, and side) grow inputs
 * from neurons of that region (and layers, on the same side or the other),
 * landing where the position labels match. The thalamus is divided by its
 * position into four bands, one for each cortical area it serves (front to
 * back: frontal, parietal, temporal, occipital), the way its nuclei are.
 */
enum { OB_SAME = 0xF0, OB_ANY_CORTEX = 0xF1, OB_AREA_OF_BAND = 0xF2, OB_BAND_OF_AREA = 0xF3 };
enum { MAP_TOPO = 0, MAP_WIDE = 1 };
enum { SIDE_SAME = 0, SIDE_OTHER = 1 };

#define L6 (1u << 0)
#define L5 (1u << 1)
#define L4 (1u << 2)
#define L23 ((1u << 3) | (1u << 4))
#define ANY 0u

typedef struct {
  const char *name;
  unsigned char dst, src;    /* regions (or the special codes above) */
  unsigned dst_layers, src_layers;
  int dst_band;              /* for the thalamus as target: which band, -1 any */
  unsigned char side, map;
  double share;              /* of a target's inputs, relative to its other rules */
} ob_rule_t;

static const ob_rule_t RULES[] = {
    /* the column: layer 4 feeds the upper layers, they feed layer 5, which feeds 6, which feeds back to 4 */
    {"column: layer 4 to layers 2/3", OB_ANY_CORTEX, OB_SAME, L23, L4, -1, SIDE_SAME, MAP_TOPO, 3.0},
    {"column: layers 2/3 to layer 5", OB_ANY_CORTEX, OB_SAME, L5, L23, -1, SIDE_SAME, MAP_TOPO, 3.0},
    {"column: layer 5 to layer 6", OB_ANY_CORTEX, OB_SAME, L6, L5, -1, SIDE_SAME, MAP_TOPO, 3.0},
    {"column: layer 6 to layer 4", OB_ANY_CORTEX, OB_SAME, L4, L6, -1, SIDE_SAME, MAP_TOPO, 1.0},
    {"column: within the upper layers", OB_ANY_CORTEX, OB_SAME, L23, L23, -1, SIDE_SAME, MAP_TOPO, 2.0},
    /* the senses arrive */
    {"retina to thalamus (visual nucleus)", OB_THALAMUS, OB_RETINA_L, ANY, ANY, 3, SIDE_OTHER, MAP_TOPO, 6.0},
    {"retina to thalamus (visual nucleus)", OB_THALAMUS, OB_RETINA_R, ANY, ANY, 3, SIDE_OTHER, MAP_TOPO, 6.0},
    {"retina to tectum", OB_TECTUM, OB_RETINA_L, ANY, ANY, -1, SIDE_OTHER, MAP_TOPO, 6.0},
    {"retina to tectum", OB_TECTUM, OB_RETINA_R, ANY, ANY, -1, SIDE_OTHER, MAP_TOPO, 6.0},
    {"touch: spinal cord to thalamus (touch nucleus)", OB_THALAMUS, OB_SPINAL_SENSORY, ANY, ANY, 1, SIDE_OTHER,
     MAP_TOPO, 6.0},
    {"thalamus to cortex layer 4, by area", OB_ANY_CORTEX, OB_BAND_OF_AREA, L4, ANY, -1, SIDE_SAME, MAP_TOPO, 6.0},
    {"cortex layer 6 back to thalamus", OB_THALAMUS, OB_AREA_OF_BAND, ANY, L6, -1, SIDE_SAME, MAP_TOPO, 3.0},
    /* the areas talk */
    {"occipital to parietal", OB_PARIETAL, OB_OCCIPITAL, L4, L23, -1, SIDE_SAME, MAP_WIDE, 2.0},
    {"occipital to temporal", OB_TEMPORAL, OB_OCCIPITAL, L4, L23, -1, SIDE_SAME, MAP_WIDE, 2.0},
    {"frontoparietal: parietal to frontal", OB_FRONTAL, OB_PARIETAL, L23, L23, -1, SIDE_SAME, MAP_WIDE, 2.0},
    {"frontoparietal: frontal to parietal", OB_PARIETAL, OB_FRONTAL, L23, L23, -1, SIDE_SAME, MAP_WIDE, 1.5},
    {"temporal to frontal", OB_FRONTAL, OB_TEMPORAL, L23, L23, -1, SIDE_SAME, MAP_WIDE, 1.0},
    {"parietal and temporal", OB_TEMPORAL, OB_PARIETAL, L23, L23, -1, SIDE_SAME, MAP_WIDE, 1.0},
    /* the corpus callosum: the same area on the other side (share set by the genes) */
    {"corpus callosum", OB_ANY_CORTEX, OB_SAME, L23, L23, -1, SIDE_OTHER, MAP_TOPO, -1.0},
    /* out to the body and the loops below */
    {"motor: frontal layer 5 to spinal cord", OB_SPINAL_MOTOR, OB_FRONTAL, ANY, L5, -1, SIDE_OTHER, MAP_TOPO, 4.0},
    {"frontal layer 5 to brainstem", OB_BRAINSTEM, OB_FRONTAL, ANY, L5, -1, SIDE_SAME, MAP_WIDE, 2.0},
    {"cortex layer 5 to basal ganglia", OB_BASAL_GANGLIA, OB_FRONTAL, ANY, L5, -1, SIDE_SAME, MAP_TOPO, 3.0},
    {"cortex layer 5 to basal ganglia", OB_BASAL_GANGLIA, OB_PARIETAL, ANY, L5, -1, SIDE_SAME, MAP_WIDE, 1.5},
    {"basal ganglia to thalamus (frontal nucleus)", OB_THALAMUS, OB_BASAL_GANGLIA, ANY, ANY, 0, SIDE_SAME, MAP_TOPO,
     4.0},
    {"cortex layer 5 to cerebellum (crossing)", OB_CEREBELLUM, OB_FRONTAL, ANY, L5, -1, SIDE_OTHER, MAP_WIDE, 2.0},
    {"cortex layer 5 to cerebellum (crossing)", OB_CEREBELLUM, OB_PARIETAL, ANY, L5, -1, SIDE_OTHER, MAP_WIDE, 2.0},
    {"touch to cerebellum", OB_CEREBELLUM, OB_SPINAL_SENSORY, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 1.0},
    {"cerebellum to thalamus (frontal band, crossing)", OB_THALAMUS, OB_CEREBELLUM, ANY, ANY, 0, SIDE_OTHER,
     MAP_WIDE, 3.0},
    /* the limbic loop */
    {"to hippocampus from temporal", OB_HIPPOCAMPUS, OB_TEMPORAL, ANY, L23, -1, SIDE_SAME, MAP_WIDE, 3.0},
    {"to hippocampus from parietal", OB_HIPPOCAMPUS, OB_PARIETAL, ANY, L23, -1, SIDE_SAME, MAP_WIDE, 2.0},
    {"hippocampus within (pairs)", OB_HIPPOCAMPUS, OB_HIPPOCAMPUS, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 3.0},
    {"hippocampus to frontal", OB_FRONTAL, OB_HIPPOCAMPUS, L23, ANY, -1, SIDE_SAME, MAP_WIDE, 1.0},
    {"hippocampus to amygdala", OB_AMYGDALA, OB_HIPPOCAMPUS, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 1.5},
    {"to amygdala from temporal", OB_AMYGDALA, OB_TEMPORAL, ANY, L23, -1, SIDE_SAME, MAP_WIDE, 2.0},
    {"to amygdala from thalamus (fast path)", OB_AMYGDALA, OB_THALAMUS, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 1.5},
    {"amygdala to frontal", OB_FRONTAL, OB_AMYGDALA, L23, ANY, -1, SIDE_SAME, MAP_WIDE, 1.0},
    {"frontal to amygdala (regulation)", OB_AMYGDALA, OB_FRONTAL, ANY, L5, -1, SIDE_SAME, MAP_WIDE, 1.0},
    {"amygdala to hypothalamus", OB_HYPOTHALAMUS, OB_AMYGDALA, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 3.0},
    {"hypothalamus to brainstem", OB_BRAINSTEM, OB_HYPOTHALAMUS, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 2.0},
    /* smell: the olfactory bulb reaches the amygdala and hippocampus directly,
       without passing the thalamus, as smell alone among the senses does */
    {"smell: olfactory bulb to amygdala", OB_AMYGDALA, OB_OLFACTORY, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 4.0},
    {"smell: olfactory bulb to hippocampus", OB_HIPPOCAMPUS, OB_OLFACTORY, ANY, ANY, -1, SIDE_SAME, MAP_WIDE, 1.0},
    /* hearing: each cochlea reaches the midbrain (the inferior colliculus, in the
       tectum) on both sides, and from there the thalamus's hearing nucleus, which
       relays to the temporal cortex. Pitch keeps its order all the way */
    {"hearing: cochlea to tectum", OB_TECTUM, OB_COCHLEA_L, ANY, ANY, -1, SIDE_OTHER, MAP_TOPO, 3.0},
    {"hearing: cochlea to tectum", OB_TECTUM, OB_COCHLEA_R, ANY, ANY, -1, SIDE_OTHER, MAP_TOPO, 3.0},
    {"hearing: cochlea to tectum", OB_TECTUM, OB_COCHLEA_L, ANY, ANY, -1, SIDE_SAME, MAP_TOPO, 1.5},
    {"hearing: cochlea to tectum", OB_TECTUM, OB_COCHLEA_R, ANY, ANY, -1, SIDE_SAME, MAP_TOPO, 1.5},
    {"hearing: tectum to thalamus (hearing nucleus)", OB_THALAMUS, OB_TECTUM, ANY, ANY, 2, SIDE_SAME, MAP_TOPO, 6.0},
    /* the midbrain */
    {"tectum to thalamus (visual nucleus)", OB_THALAMUS, OB_TECTUM, ANY, ANY, 3, SIDE_SAME, MAP_TOPO, 2.0},
    {"tegmentum to basal ganglia (dopamine)", OB_BASAL_GANGLIA, OB_TEGMENTUM, ANY, ANY, -1, SIDE_SAME, MAP_WIDE,
     1.0},
    {"tegmentum to frontal (dopamine)", OB_FRONTAL, OB_TEGMENTUM, L23, ANY, -1, SIDE_SAME, MAP_WIDE, 0.5},
    /* the brainstem drives the body's muscles in a map of the body, and sends a
       copy of every command to the cerebellum (the efference copy), which can
       learn what touch that movement will bring, and cancel it in the thalamus.
       The prediction's connections start at nothing: they are learned. */
    {"brainstem to spinal cord (motor)", OB_SPINAL_MOTOR, OB_BRAINSTEM, ANY, ANY, -1, SIDE_SAME, MAP_TOPO, 2.0},
    {"efference copy: brainstem to cerebellum", OB_CEREBELLUM, OB_BRAINSTEM, ANY, ANY, -1, SIDE_SAME, MAP_TOPO, 3.0},
    /* the cerebellum serves its own side of the body and projects across, to the
       thalamus of the other side, where that side's touch arrives */
    {"prediction: cerebellum to thalamus (touch nucleus)", OB_THALAMUS, OB_CEREBELLUM, ANY, ANY, 1, SIDE_OTHER, MAP_TOPO,
     4.0},
    {"spinal reflex: sensory to motor", OB_SPINAL_MOTOR, OB_SPINAL_SENSORY, ANY, ANY, -1, SIDE_SAME, MAP_TOPO, 2.0},
};

#define N_RULES (sizeof RULES / sizeof RULES[0])
typedef char ob_rules_fit[(N_RULES <= 64u) ? 1 : -1];   /* rule_count has room for every rule */

const char *ob_rule_name(unsigned rule) { return rule < N_RULES ? RULES[rule].name : "?"; }


/* Sources, grouped by region, side and layer, and by an 8x8 grid of their sheet. */
#define OB_GRID 8u
#define OB_GROUPS (OB_REGIONS * 2u * OB_LAYERS)
static unsigned START[OB_GROUPS * OB_GRID * OB_GRID + 1u];
static unsigned BY_CELL[OB_MAX_NEURONS];

static unsigned group_of(unsigned region, unsigned side, unsigned layer) {
  return (region * 2u + side) * OB_LAYERS + layer;
}

static unsigned cell_of(double sx, double sy) {
  unsigned gx = (unsigned)(sx * OB_GRID), gy = (unsigned)(sy * OB_GRID);
  if (gx >= OB_GRID) gx = OB_GRID - 1u;
  if (gy >= OB_GRID) gy = OB_GRID - 1u;
  return gy * OB_GRID + gx;
}

static void index_sources(const ob_brain_t *b) {
  static unsigned fill[OB_GROUPS * OB_GRID * OB_GRID];
  unsigned i, k, n = OB_GROUPS * OB_GRID * OB_GRID;
  memset(START, 0, sizeof START);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *nn = &b->neuron[i];
    START[group_of(nn->region, nn->side, nn->layer) * OB_GRID * OB_GRID + cell_of(nn->sx, nn->sy) + 1u]++;
  }
  for (k = 0u; k < n; k++) START[k + 1u] += START[k];
  memcpy(fill, START, sizeof fill);
  for (i = 0u; i < b->n_neurons; i++) {
    const ob_neuron_t *nn = &b->neuron[i];
    BY_CELL[fill[group_of(nn->region, nn->side, nn->layer) * OB_GRID * OB_GRID + cell_of(nn->sx, nn->sy)]++] = i;
  }
}

/* A source neuron in this region, on this side, in one of these layers, near
   (sx, sy) in its sheet (or anywhere, if wide). 0 tries left: none found. */
static int pick_source(unsigned region, unsigned side, unsigned layers, double sx, double sy, double spread,
                       int wide, unsigned *out) {
  unsigned tries;
  for (tries = 0u; tries < 24u; tries++) {
    unsigned layer = 0u, g, c, n;
    double x = wide ? unit() : sx + (unit() * 2.0 - 1.0) * spread;
    double y = wide ? unit() : sy + (unit() * 2.0 - 1.0) * spread;
    if (layers != ANY) {
      unsigned want[OB_LAYERS], nl = 0u, l;
      for (l = 0u; l < OB_LAYERS; l++) {
        if (layers & (1u << l)) want[nl++] = l;
      }
      layer = want[rnd() % nl];
    }
    if (x < 0.0) x = 0.0;
    if (y < 0.0) y = 0.0;
    if (x > 0.999) x = 0.999;
    if (y > 0.999) y = 0.999;
    g = group_of(region, side, layer);
    c = g * OB_GRID * OB_GRID + cell_of(x, y);
    n = START[c + 1u] - START[c];
    if (n == 0u) continue;
    *out = BY_CELL[START[c] + rnd() % n];
    return 1;
  }
  return 0;
}

/* Does rule r grow inputs into neuron n? Which region do its inputs come from? */
static int applies(const ob_brain_t *b, const ob_rule_t *r, const ob_neuron_t *n, unsigned *src) {
  unsigned dst = n->region;
  if (r->dst == OB_ANY_CORTEX) {
    if (!is_cortex(dst)) return 0;
  } else if (r->dst != dst) {
    return 0;
  }
  if (r->dst_layers != ANY && is_cortex(dst) && !(r->dst_layers & (1u << n->layer))) return 0;
  if (r->dst_band >= 0 && dst == OB_THALAMUS && n->layer != (unsigned)r->dst_band) return 0;
  if (r->src == OB_SAME) {
    *src = dst;
  } else if (r->src == OB_BAND_OF_AREA) {
    *src = OB_THALAMUS;
  } else if (r->src == OB_AREA_OF_BAND) {
    *src = OB_FRONTAL + n->layer;   /* the thalamic nucleus: which area it serves */
  } else {
    *src = r->src;
  }
  (void)b;
  return 1;
}

void ob_stage_wiring(ob_brain_t *b) {
  const ob_genome_t *g = &b->genes;
  unsigned i;
  RNG = g->seed * 2246822519u + 11u;
  b->n_syn = 0u;
  memset(b->rule_count, 0, sizeof b->rule_count);
  index_sources(b);
  for (i = 0u; i < b->n_neurons; i++) {
    ob_neuron_t *n = &b->neuron[i];
    double share[N_RULES], total = 0.0;
    unsigned src_of[N_RULES], r, k, made = 0u;
    n->syn_first = b->n_syn;
    n->syn_n = 0u;
    for (r = 0u; r < N_RULES; r++) {
      share[r] = 0.0;
      if (!applies(b, &RULES[r], n, &src_of[r])) continue;
      if (n->inhibitory && RULES[r].src != OB_SAME) continue;   /* interneurons listen locally */
      share[r] = RULES[r].share < 0.0 ? 0.0 : RULES[r].share;
      total += share[r];
    }
    if (total <= 0.0) continue;
    /* the corpus callosum takes its share of the upper layers' inputs, set by the genes */
    for (k = 0u; k < g->synapses_in; k++) {
      double pick = unit() * total, acc = 0.0;
      unsigned chosen = N_RULES, from, side;
      const ob_rule_t *rule;
      int callosal = is_cortex(n->region) && (n->layer >= 3u) && unit() < g->callosum;
      if (callosal) {
        for (r = 0u; r < N_RULES; r++) {
          if (RULES[r].share < 0.0) chosen = r;
        }
      } else {
        for (r = 0u; r < N_RULES; r++) {
          if (share[r] <= 0.0) continue;
          acc += share[r];
          if (pick < acc) {
            chosen = r;
            break;
          }
        }
        if (chosen == N_RULES) continue;
      }
      rule = &RULES[chosen];
      if (callosal) src_of[chosen] = n->region;
      side = rule->side == SIDE_OTHER ? 1u - n->side : n->side;
      {
        /* where to look: the same place in the source's sheet. Each nucleus of
           the thalamus has the whole sheet, and serves one cortical area */
        unsigned layers = rule->src_layers;
        if (rule->src == OB_BAND_OF_AREA) layers = 1u << (n->region - OB_FRONTAL);
        if (!pick_source(src_of[chosen], side, layers, n->sx, n->sy, g->spread, rule->map == MAP_WIDE, &from))
          continue;
      }
      if (from == i) continue;
      if (b->n_syn >= OB_MAX_SYN) {
        b->dropped++;
        break;
      }
      b->syn[b->n_syn].from = from;
      b->syn[b->n_syn].rule = (unsigned char)chosen;
      b->syn[b->n_syn].weight =
          (short)((100 + (int)(rnd() % 900u)) * (b->neuron[from].inhibitory ? -1 : 1));
      if (strncmp(rule->name, "prediction:", 11) == 0) b->syn[b->n_syn].weight = 0;   /* learned, from nothing */
      b->n_syn++;
      b->rule_count[chosen]++;
      made++;
    }
    n->syn_n = made;
  }
  /* (the local inhibitory loop needs no rule of its own: interneurons sit in the
     column's layers, so the column rules reach them, and they reach the column) */
}

/* ---- all of it ------------------------------------------------------------- */

void ob_grow(ob_brain_t *b, const ob_genome_t *g) {
  if (b == 0 || g == 0) return;
  b->genes = *g;
  b->dropped = 0u;
  ob_stage_plate(b);
  ob_stage_neurons(b);
  ob_stage_wiring(b);
}
