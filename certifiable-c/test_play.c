/*
 * test_play.c -- the child in worlds it can only see and act in, against the
 * rules those worlds were built with.
 *
 * The rules live in arc_standin.c and nowhere the child can reach. Checked:
 *   itself        it finds the cells that are itself, both colours of them
 *   actions       it gets each action's displacement right, including when
 *                 they are shuffled and when one does nothing at all
 *   walls         it names the wall colour and nothing else
 *   the ending    it names the colour that ends a level, and not the decoy
 *                 it walked across on the way
 *   carried over  once it knows, it goes straight there on levels it has
 *                 never seen: those levels are done in the fewest actions
 *   efficiency    the whole game in no more than a quarter over the shortest
 */

#include <stdio.h>

#include "arc_standin.h"
#include "arc_worldgen.h"
#include "smarsh_play.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static int wall(const pl_child_t *ch, unsigned k) {
  return ch->failures > 0u && ch->wall_alive[k] && !ch->wall_ruled_out[k];
}

static int only_wall(const pl_child_t *ch, unsigned colour) {
  unsigned k;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (wall(ch, k) != (k == colour)) return 0;
  }
  return 1;
}

static int ending(const pl_child_t *ch, unsigned k) {
  return ch->endings > 0u && ch->end_alive[k] && ch->end_seen[k];
}

static int only_ending(const pl_child_t *ch, unsigned colour) {
  unsigned k;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (ending(ch, k) != (k == colour)) return 0;
  }
  return 1;
}

static int moves(const pl_child_t *ch, unsigned a, int dr, int dc) {
  return ch->effect[a] == PL_MOVES && ch->eff_dr[a] == dr && ch->eff_dc[a] == dc;
}

static int has_colour(const pl_child_t *ch, unsigned colour) {
  unsigned i;
  for (i = 0u; i < ch->body_n; i++) {
    if (ch->body_col[i] == colour) return 1;
  }
  return 0;
}

static unsigned total(const unsigned *v, unsigned n) {
  unsigned i, t = 0u;
  for (i = 0u; i < n; i++) t += v[i];
  return t;
}

int main(void) {
  pl_game_t g;
  pl_child_t ch;

  printf("THE CHILD AT PLAY\n\n");

  standin_maze(&g);
  pl_child_init(&ch);
  check("it finishes every level of a world it was told nothing about",
        pl_play(&ch, &g, 600u) == SM_OK && ch.levels_done == 3u);
  check("it finds itself: four cells that moved together, in two colours",
        ch.have_body && ch.body_n == 4u && has_colour(&ch, 3u) && has_colour(&ch, 6u));
  check("it gets what every action does, by two cells, not one",
        moves(&ch, 1u, -2, 0) && moves(&ch, 2u, 2, 0) && moves(&ch, 3u, 0, -2) &&
            moves(&ch, 4u, 0, 2));
  check("it names the wall colour, and nothing else as a wall", only_wall(&ch, 5u));
  check("it names what ends a level, and not the decoy it walked over", only_ending(&ch, 4u));
  check("carried to levels it has never seen, it takes the shortest way",
        ch.level_actions[1] == ch.level_shortest[1] &&
            ch.level_actions[2] == ch.level_shortest[2]);
  check("the whole game within a quarter of the shortest possible",
        4u * total(ch.level_actions, 3u) <= 5u * total(ch.level_shortest, 3u));

  standin_scrambled(&g);
  pl_child_init(&ch);
  check("with the actions shuffled it still finishes",
        pl_play(&ch, &g, 600u) == SM_OK && ch.levels_done == 3u);
  check("and works out the shuffle, action by action",
        moves(&ch, 1u, 0, 2) && moves(&ch, 2u, -2, 0) && moves(&ch, 4u, 0, -2) &&
            moves(&ch, 5u, 2, 0));
  check("and finds the action that does nothing", ch.effect[3] == PL_STILL);
  check("a different ending colour is found, not carried in", only_ending(&ch, 11u));

  /* ---- worlds that need a new kind of explanation --------------------------- */
  {
    static const unsigned diffs[3] = {0u, 3u, 9u};
    unsigned d, seed, all_keys = 1u, all_causes = 1u, push_ok = 1u, push_ending = 1u;
    for (d = 0u; d < 3u; d++) {
      unsigned finished = 0u, pushed_and_knew = 0u;
      for (seed = 1u; seed <= 16u; seed++) {
        wg_facts_t f;
        wg_make_kind(&g, WG_KEYS, diffs[d], seed);
        wg_facts(&g, &f);
        pl_child_init(&ch);
        if (!(pl_play(&ch, &g, 4000u) == SM_OK && ch.levels_done == 3u)) all_keys = 0u;
        if (f.kind != WG_KEYS || ch.cause_to[f.key][f.door] != (signed char)f.floor) all_causes = 0u;

        wg_make_kind(&g, WG_PUSH, diffs[d], seed);
        wg_facts(&g, &f);
        pl_child_init(&ch);
        if (pl_play(&ch, &g, 4000u) == SM_OK && ch.levels_done == 3u) {
          finished++;
          if (ch.push_known && ch.pend_alive[f.ending] && ch.pend_seen[f.ending]) pushed_and_knew++;
        }
      }
      if (finished < 13u) push_ok = 0u;
      if (pushed_and_knew != finished) push_ending = 0u;
    }
    check("keys and doors: every one of 48 worlds finished, at three difficulties", all_keys);
    check("and in every one it found the cause for itself: covering the key turns the door to floor",
          all_causes);
    check("pushing: at least 13 of 16 worlds finished at each of three difficulties", push_ok);
    check("and in every one it finished, it knew the end is the pushed thing on the target",
          push_ending);
  }

  check("NULL arguments are checked errors",
        pl_play(0, &g, 1u) == SM_ERR_NULL_ARGUMENT && pl_play(&ch, 0, 1u) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
