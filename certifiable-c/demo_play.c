/*
 * demo_play.c -- the child dropped into worlds it can only see and act in.
 *
 * It is given frames, numbered actions and a signal that a level ended.
 * Nobody tells it which cells are itself, which way anything moves, what a
 * wall is or what it is for. Watch what it works out, and how many actions
 * it spends against the shortest way through.
 */

#include <stdio.h>

#include "arc_standin.h"
#include "smarsh_play.h"

static void run(const char *title, void (*make)(pl_game_t *)) {
  pl_game_t g;
  pl_child_t ch;
  sm_status_t st;
  make(&g);
  pl_child_init(&ch);
  st = pl_play(&ch, &g, 600u);
  printf("\n%s\n", title);
  printf("  %s after %u actions\n",
         st == SM_OK ? "finished every level" : "ran out of actions", ch.actions_taken);
  pl_report(&ch, &g);
}

int main(void) {
  printf("THE CHILD AT PLAY\n");
  printf("=================\n");
  printf("Frames of 64 by 64 coloured cells, numbered actions, and a signal\n");
  printf("when a level ends. Nothing else reaches it.\n");

  run("ONE. A maze world.", standin_maze);
  run("TWO. The same mazes, actions shuffled, one action dead, a new ending colour.",
      standin_scrambled);
  return 0;
}
