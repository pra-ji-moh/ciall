/*
 * test_grow.c -- the child growing by itself, through a map of worlds.
 *
 * Checked:
 *   from nothing   with no memory, only mazes are open
 *   the map        the other kinds stay closed until mazes are handled at
 *                  difficulty 3, and then open without being asked for
 *   growing        by its own judgement it moves up in every kind it has
 *                  opened
 *   memory         where it stands survives being written and read exactly,
 *                  and a mind written before the map existed is read as
 *                  progress in mazes
 *   resuming       growing in two stretches with the memory saved between
 *                  them ends exactly where one long stretch does
 *   repeatable     the same growth twice gives the same child
 *   the hardest    every maze at the hardest difficulty, over many seeds,
 *                  is finished
 */

#include <stdio.h>
#include <string.h>

#include "arc_worldgen.h"
#include "smarsh_grow.h"
#include "smarsh_play.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static const char *TMP = "test_grow_mind.tmp";

static int same_but_runs(const gw_mind_t *a, const gw_mind_t *b) {
  gw_mind_t x = *a, y = *b;
  x.runs = 0u;
  y.runs = 0u;
  return memcmp(&x, &y, sizeof x) == 0;
}

int main(void) {
  gw_mind_t m, n, one, two;
  unsigned seed, finished = 0u, k;
  int closed_while_learning = 1;

  printf("THE CHILD GROWING BY ITSELF\n\n");

  remove(TMP);
  check("with no memory, only mazes are open",
        gw_load(&m, TMP) == SM_ERR_EMPTY_DOMAIN && m.track[WG_MAZE].unlocked &&
            !m.track[WG_KEYS].unlocked && !m.track[WG_PUSH].unlocked && m.worlds == 0u);

  /* grow one world at a time, and watch the gates */
  for (k = 0u; k < 80u; k++) {
    gw_grow(&m, 1u, 0);
    if ((m.track[WG_KEYS].unlocked || m.track[WG_PUSH].unlocked) &&
        !(m.track[WG_MAZE].has_frontier && m.track[WG_MAZE].frontier >= 3u))
      closed_while_learning = 0;
  }
  check("the other kinds stay closed until mazes are handled at difficulty 3",
        closed_while_learning);
  check("and then they open, without being asked for",
        m.track[WG_KEYS].unlocked && m.track[WG_PUSH].unlocked);
  check("it moves itself up in every kind it has opened",
        m.track[WG_MAZE].has_frontier && m.track[WG_KEYS].has_frontier &&
            m.track[WG_PUSH].has_frontier);

  check("where it stands is written down", gw_save(&m, TMP) == SM_OK);
  check("and read back exactly", gw_load(&n, TMP) == SM_OK && memcmp(&m, &n, sizeof m) == 0);

  {
    FILE *f = fopen(TMP, "w");
    if (f != 0) {
      fprintf(f, "has_frontier 1\ndifficulty 5\nfrontier 4\nworlds 24\nnext_seed 25\n");
      fclose(f);
    }
    check("a mind written before the map is read as progress in mazes",
          gw_load(&n, TMP) == SM_OK && n.track[WG_MAZE].difficulty == 5u &&
              n.track[WG_MAZE].frontier == 4u && n.track[WG_MAZE].has_frontier &&
              n.worlds == 24u && n.next_seed == 25u);
  }

  gw_mind_init(&one);
  gw_grow(&one, 30u, 0);
  gw_mind_init(&two);
  gw_grow(&two, 15u, 0);
  gw_save(&two, TMP);
  gw_load(&two, TMP);
  gw_grow(&two, 15u, 0);
  check("switched off halfway and started again, it ends where it would have anyway",
        same_but_runs(&one, &two));

  gw_mind_init(&n);
  gw_grow(&n, 30u, 0);
  check("the same growth twice gives the same child", memcmp(&one, &n, sizeof n) == 0);

  for (seed = 1u; seed <= 20u; seed++) {
    pl_game_t g;
    pl_child_t ch;
    wg_make(&g, WG_MAX_DIFFICULTY, 1000u + seed);
    pl_child_init(&ch);
    if (pl_play(&ch, &g, 4000u) == SM_OK && ch.levels_done == g.levels) finished++;
  }
  check("every one of twenty mazes at the hardest difficulty is finished", finished == 20u);

  check("NULL arguments are checked errors",
        gw_grow(0, 1u, 0) == SM_ERR_NULL_ARGUMENT && gw_load(0, TMP) == SM_ERR_NULL_ARGUMENT &&
            gw_save(&m, 0) == SM_ERR_NULL_ARGUMENT);

  remove(TMP);
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
