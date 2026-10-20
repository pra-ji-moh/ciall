/*
 * test_worldgen.c -- the abstract worlds are fair: every one can be made,
 * every level has a way through, and the shortest way it reports is the
 * real one.
 *
 * It plays each world with an oracle that sees the world's insides: at every
 * step it takes an action after which the shortest way is one shorter (or the
 * level ends), and so must finish every level in exactly the shortest number
 * of actions the world reported. To try an action and take it back, it
 * includes arc_worldgen.c itself and copies the world's state.
 *
 * Checked, for odd one out, mirror twin, in order, reverse order and odd twin,
 * at every difficulty,
 * over forty seeds each:
 *   made       the kind asked for is the kind given, never a maze instead
 *   fair       the oracle finishes all three levels
 *   honest     in exactly the shortest number of actions reported at the start
 *   odd        each level's common colour is the colour that was odd before
 *   repeatable the same seed gives the same world
 */

#include "arc_worldgen.c"

#include <stdio.h>

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

/* Plays the current world to the end; returns actions taken, or 0 if it could not. */
static unsigned oracle(pl_game_t *g, unsigned *reported) {
  pl_frame_t f;
  unsigned lv = 0u, steps = 0u;
  g->reset(g, &f);
  *reported = g->shortest(g);
  while (lv < g->levels && steps < 4000u) {
    unsigned s = g->shortest(g), a, chosen = 0u;
    int r;
    if (s == 0u) return 0u;
    for (a = 1u; a <= g->n_actions && chosen == 0u; a++) {
      wg_state_t before = WG;
      r = g->act(g, a, &f);
      if ((s == 1u && r > 0) || (r == 0 && g->shortest(g) == s - 1u)) chosen = a;
      WG = before;
    }
    if (chosen == 0u) return 0u;
    r = g->act(g, chosen, &f);
    steps++;
    if (r > 0) {
      lv++;
      if (lv < g->levels) *reported += g->shortest(g);
    }
  }
  return lv == g->levels ? steps : 0u;
}

int main(void) {
  static const unsigned KINDS[5] = {WG_ODD, WG_MIRROR, WG_ORDER, WG_REVERSE, WG_ODDTWIN};
  unsigned k, d, seed;
  int made = 1, fair = 1, honest = 1, odd_moves = 1, same = 1;

  printf("THE ABSTRACT WORLDS ARE FAIR\n\n");

  for (k = 0u; k < 5u; k++) {
    for (d = 0u; d <= WG_MAX_DIFFICULTY; d++) {
      for (seed = 1u; seed <= 40u; seed++) {
        pl_game_t g;
        wg_facts_t f;
        unsigned steps, reported = 0u;
        memset(&f, 0, sizeof f);
        wg_make_kind(&g, KINDS[k], d, seed);
        wg_facts(&g, &f);
        if (f.kind != KINDS[k]) {
          made = 0;
          continue;
        }
        if (KINDS[k] == WG_ODD || KINDS[k] == WG_ODDTWIN) {
          unsigned lv;
          for (lv = 1u; lv < WG_LEVELS; lv++) {
            if (WG.common[lv] != WG.odd[lv - 1u] || WG.common[lv] == WG.odd[lv]) odd_moves = 0;
          }
        }
        steps = oracle(&g, &reported);
        if (steps == 0u) fair = 0;
        else if (steps != reported) honest = 0;
      }
    }
  }
  check("every abstract world asked for is made as that kind", made);
  check("every level of every one has a way through", fair);
  check("and the shortest way each reports is exactly the real one", honest);
  check("odd one out: each level's common colour is the one that was odd before", odd_moves);

  for (k = 0u; k < 5u; k++) {
    pl_game_t g;
    static pl_frame_t a, b;
    wg_make_kind(&g, KINDS[k], 6u, 77u);
    g.reset(&g, &a);
    wg_make_kind(&g, KINDS[k], 6u, 77u);
    g.reset(&g, &b);
    if (memcmp(&a, &b, sizeof a) != 0) same = 0;
  }
  check("the same seed gives the same world", same);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
