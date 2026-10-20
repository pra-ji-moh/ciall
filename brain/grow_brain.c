/*
 * grow_brain.c -- grow an open brain from a genome, and write it out to read.
 *
 *   grow_brain [genome.txt] [out_dir]
 *
 * Writes into out_dir: brain_map.txt (everything it is, in words and numbers),
 * plate.png (the folded plate's fates), morphogens.png (the three signals),
 * cortex.png (both hemispheres and the corpus callosum), retinotopy.png (the
 * retina's map, as it lands on the visual cortex before anything is seen),
 * and then, after twenty days in the womb learning touch, self.png (its body,
 * by how much of each place's touch it has learned is its own, and the days).
 */

#include <stdio.h>
#include <string.h>

#include "ob.h"

static ob_brain_t BRAIN;
static ob_womb_stats_t WOMB;
static ob_smell_stats_t SMELL;
static ob_heart_stats_t HEART;
static ob_vision_stats_t VISION;
static ob_reflex_stats_t REFLEX;

int main(int argc, char **argv) {
  ob_genome_t g;
  const char *dir = argc > 2 ? argv[2] : ".";
  char path[1024];
  FILE *f;
  ob_genome_default(&g);
  if (argc > 1 && !ob_genome_load(&g, argv[1])) {
    fprintf(stderr, "grow_brain: cannot read %s\n", argv[1]);
    return 2;
  }
  if (strlen(dir) > 900u) return 2;
  ob_grow(&BRAIN, &g);
  ob_womb_touch(&BRAIN, 20u, 300u, &WOMB);
  ob_womb_smell(&BRAIN, 20u, 50u, &SMELL);
  ob_womb_heartbeat(&BRAIN, 20u, 400u, &HEART);
  ob_womb_vision(&BRAIN, 20u, 200u, 1, &VISION);
  ob_womb_reflex(&BRAIN, 20u, 100u, &REFLEX);
  sprintf(path, "%s/brain_map.txt", dir);
  f = fopen(path, "w");
  if (f == 0) {
    fprintf(stderr, "grow_brain: cannot write %s\n", path);
    return 2;
  }
  ob_write_map(&BRAIN, f);
  ob_write_womb(&WOMB, f);
  ob_write_smell(&SMELL, f);
  ob_write_heart(&HEART, f);
  ob_write_vision(&VISION, f);
  ob_write_reflex(&REFLEX, f);
  fclose(f);
  sprintf(path, "%s/plate.png", dir);
  ob_draw_plate(&BRAIN, path);
  sprintf(path, "%s/morphogens.png", dir);
  ob_draw_morphogens(&BRAIN, path);
  sprintf(path, "%s/cortex.png", dir);
  ob_draw_cortex(&BRAIN, path);
  sprintf(path, "%s/retinotopy.png", dir);
  ob_draw_retinotopy(&BRAIN, path);
  sprintf(path, "%s/self.png", dir);
  ob_draw_self(&BRAIN, &WOMB, path);
  printf("grew %u neurons and %u connections, lived %u days in the womb (now feels %.0f%% of its own touch,"
         " %.0f%% of the mother's); wrote %s/brain_map.txt and five pictures\n",
         BRAIN.n_neurons, BRAIN.n_syn, WOMB.days, 100.0 * WOMB.self_left[WOMB.days - 1u],
         100.0 * WOMB.other_left[WOMB.days - 1u], dir);
  return 0;
}
