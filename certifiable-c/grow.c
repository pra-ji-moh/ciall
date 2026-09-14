/*
 * grow.c -- one stretch of the child growing by itself.
 *
 *   grow [worlds] [directory]
 *
 * Reads where the child stands from <directory>/mind.txt (starting fresh if
 * there is none), plays that many worlds it has never seen, writes where it
 * now stands back, and adds what happened to <directory>/journal.txt. It
 * needs nobody present: run it on a schedule and the child keeps growing,
 * and the journal is how anyone finds out what it did.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smarsh_grow.h"

int main(int argc, char **argv) {
  unsigned worlds = 12u;
  const char *dir = "child";
  char mind_path[512], journal_path[512];
  gw_mind_t m;
  FILE *journal;
  sm_status_t st;

  if (argc > 1) worlds = (unsigned)strtoul(argv[1], 0, 10);
  if (argc > 2) dir = argv[2];
  if (worlds == 0u) worlds = 1u;
  if (strlen(dir) + 16u > sizeof mind_path) {
    fprintf(stderr, "grow: directory name too long\n");
    return 2;
  }
  sprintf(mind_path, "%s/mind.txt", dir);
  sprintf(journal_path, "%s/journal.txt", dir);

  st = gw_load(&m, mind_path);
  printf("the child %s\n", st == SM_OK ? "remembers where it stood" : "starts with nothing");

  journal = fopen(journal_path, "a");
  if (journal == 0) {
    fprintf(stderr, "grow: cannot write %s (does the directory exist?)\n", journal_path);
    return 2;
  }
  gw_grow(&m, worlds, journal);
  fclose(journal);

  if (gw_save(&m, mind_path) != SM_OK) {
    fprintf(stderr, "grow: cannot write %s\n", mind_path);
    return 2;
  }
  gw_report(&m);
  printf("  journal: %s\n", journal_path);
  return 0;
}
