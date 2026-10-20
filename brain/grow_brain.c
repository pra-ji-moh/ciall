/*
 * grow_brain.c -- grow an open brain from a genome, and write it out to read.
 *
 *   grow_brain [genome.txt] [out_dir]
 *
 * Writes into out_dir: brain_map.txt (everything it is, in words and numbers),
 * plate.png (the folded plate's fates), morphogens.png (the three signals),
 * cortex.png (both hemispheres and the corpus callosum), retinotopy.png (the
 * retina's map, as it lands on the visual cortex before anything is seen).
 */

#include <stdio.h>
#include <string.h>

#include "ob.h"

static ob_brain_t BRAIN;

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
  sprintf(path, "%s/brain_map.txt", dir);
  f = fopen(path, "w");
  if (f == 0) {
    fprintf(stderr, "grow_brain: cannot write %s\n", path);
    return 2;
  }
  ob_write_map(&BRAIN, f);
  fclose(f);
  sprintf(path, "%s/plate.png", dir);
  ob_draw_plate(&BRAIN, path);
  sprintf(path, "%s/morphogens.png", dir);
  ob_draw_morphogens(&BRAIN, path);
  sprintf(path, "%s/cortex.png", dir);
  ob_draw_cortex(&BRAIN, path);
  sprintf(path, "%s/retinotopy.png", dir);
  ob_draw_retinotopy(&BRAIN, path);
  printf("grew %u neurons and %u connections; wrote %s/brain_map.txt and four pictures\n", BRAIN.n_neurons,
         BRAIN.n_syn, dir);
  return 0;
}
