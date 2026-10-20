/*
 * test_mind.c -- the child as a mind of parts.
 *
 * Checked:
 *   the concrete   mazes, keys and doors, and pushing are all finished
 *   mirror         it finds the twin, sees that it steps as itself mirrored,
 *                  and finishes the mirror worlds well, the easiest and the
 *                  hardest
 *   invention      it builds "the odd one out" and "the legend in order"
 *                  itself, from endings its first language cannot explain,
 *                  and finishes those worlds well
 *   prediction     it predicts every mover and every ending, and is mostly
 *                  right once it knows a world
 *   optogenetics   with the inventor switched off, the worlds that need an
 *                  invented idea are no longer finished well; and switching
 *                  its own parts off in pairs, it finds that its inventor and
 *                  packing stand in for each other
 *   harder         reverse order (the legend read the other way) and odd twin
 *                  (the twin must cover the odd tile) are finished well
 *   the score      ARC-AGI-3's way, every level counting
 *   intuition      a world like ones it has met has low novelty; after enough
 *                  worlds, intuition names what ends them (for worlds with a
 *                  legend, which look like nothing else)
 *   packing        ideas that keep explaining worlds become blocks it starts with
 *   sleep          worlds it could not explain are replayed, and an idea that
 *                  fits them is found
 *   memory         the library is written and read back exactly
 *   repeatable     the same library and world give the same play
 */

#include <stdio.h>
#include <string.h>

#include "arc_worldgen.h"
#include "smarsh_mind.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static int well(const mi_result_t *r, unsigned levels) {
  unsigned i, la = 0u, lb = 0u;
  if (r->levels_done < levels) return 0;
  for (i = 1u; i < r->levels_done; i++) {
    la += r->level_actions[i];
    lb += r->level_shortest[i];
  }
  return lb > 0u && la <= 2u * lb;
}

/* Play n worlds of one kind; how many finished well, and the last result. */
static unsigned play_kind(mi_library_t *lib, unsigned kind, unsigned diff, unsigned seed0, unsigned n,
                          unsigned *finished, mi_result_t *last) {
  unsigned i, w = 0u;
  *finished = 0u;
  for (i = 0u; i < n; i++) {
    pl_game_t g;
    mi_result_t r;
    wg_make_kind(&g, kind, diff, seed0 + i);
    if (mi_play(lib, &g, 4000u, MI_FAMILIES, &r) == SM_OK) (*finished)++;
    w += (unsigned)well(&r, g.levels);
    *last = r;
  }
  return w;
}

static const char *TMP = "test_mind_lib.tmp";

static unsigned PROBE_KIND_BASE;

/* the self-experiment's worlds: odd one out and in order, two of each, and two mirrors */
static void probe_world(pl_game_t *g, unsigned i) {
  static const unsigned kinds[3] = {WG_ODD, WG_ORDER, WG_MIRROR};
  wg_make_kind(g, kinds[i % 3u], 4u, PROBE_KIND_BASE + i);
}

int main(void) {
  static mi_library_t lib, lib2, fresh;
  mi_result_t r, r2;
  unsigned fin, w, k;

  printf("THE CHILD AS A MIND OF PARTS\n\n");

  mi_library_init(&lib);
  fin = 0u;
  for (k = 0u; k < 3u; k++) {
    unsigned f2;
    play_kind(&lib, k, 0u, 100u, 6u, &f2, &r);
    fin += f2;
    play_kind(&lib, k, 9u, 200u, 6u, &f2, &r);
    fin += f2;
  }
  check("mazes, keys and doors, pushing: at least 34 of 36 finished, easiest and hardest", fin >= 34u);

  mi_library_init(&lib);
  w = play_kind(&lib, WG_MIRROR, 0u, 300u, 8u, &fin, &r);
  check("mirror twin, easiest: at least 7 of 8 finished well", w >= 7u);
  check("and it found the twin stepping as itself, mirrored", r.movers == 2u && r.transform != 0);
  w = play_kind(&lib, WG_MIRROR, 9u, 400u, 8u, &fin, &r);
  check("mirror twin, hardest (fully reversed): at least 7 of 8 finished well", w >= 7u);

  mi_library_init(&lib);
  {
    pl_game_t g;
    wg_make_kind(&g, WG_ORDER, 3u, 500u);
    mi_play(&lib, &g, 4000u, MI_FAMILIES, &r);
  }
  check("in order: with only 'a mover covers a colour', it invents the legend in order itself",
        (r.invented & ((1u << MI_SEQ_LR) | (1u << MI_SEQ_RL))) != 0u && r.winner >= MI_SEQ_LR);
  w = play_kind(&lib, WG_ORDER, 6u, 600u, 8u, &fin, &r);
  check("and finishes in-order worlds well: at least 7 of 8, difficulty 6", w >= 7u);

  mi_library_init(&lib);
  {
    pl_game_t g;
    wg_make_kind(&g, WG_ODD, 6u, 700u);
    mi_play(&lib, &g, 4000u, MI_FAMILIES, &r);
  }
  check("odd one out: it invents the odd one out itself", (r.invented & (1u << MI_ODD)) != 0u &&
                                                             r.winner == MI_ODD);
  w = play_kind(&lib, WG_ODD, 9u, 800u, 8u, &fin, &r);
  check("and finishes odd-one-out worlds well: at least 7 of 8, hardest", w >= 7u);

  {
    mi_result_t odd_last = r;
  mi_library_init(&lib);
  w = play_kind(&lib, WG_REVERSE, 5u, 1100u, 8u, &fin, &r);
  check("reverse order: it reads the legend the other way, at least 7 of 8 finished well",
        w >= 7u && lib.wins[MI_SEQ_RL] > 0u);
  mi_library_init(&lib);
  w = play_kind(&lib, WG_ODDTWIN, 5u, 1200u, 8u, &fin, &r);
  check("odd twin: two ideas at once, the twin on the odd tile, at least 6 of 8 finished well",
        w >= 6u && r.transform != 0 && r.winner == MI_ODD);
  {
    mi_result_t perfect;
    memset(&perfect, 0, sizeof perfect);
    perfect.levels_done = 3u;
    for (k = 0u; k < 3u; k++) perfect.level_actions[k] = perfect.level_shortest[k] = 10u;
    perfect.level_actions[2] = 20u;   /* the last level at twice the shortest: a quarter */
    check("scored as ARC-AGI-3 does: (1*1 + 2*1 + 3*0.25) / 6 = 62.5%", mi_score(&perfect, 3u) == 625u);
  }

  check("it predicts, and once it knows a world it is mostly right",
        odd_last.predictions > 0u && odd_last.errors * 4u < odd_last.predictions);
  }

  /* optogenetics */
  mi_library_init(&lib);
  mi_switch(MI_PART_INVENTOR, 1);
  w = play_kind(&lib, WG_ORDER, 6u, 600u, 8u, &fin, &r);
  {
    unsigned w2 = play_kind(&lib, WG_ODD, 9u, 800u, 8u, &fin, &r);
    mi_switch(MI_PART_INVENTOR, 0);
    check("with its inventor switched off, in-order and odd worlds are rarely finished well",
          w + w2 <= 4u && mi_switched_off(MI_PART_INVENTOR) == 0);
  }

  /* sleep: what it played with the inventor off is replayed with every block */
  mi_sleep(&lib, 0);
  check("in sleep, the worlds it could not explain are found to fit an idea",
        lib.dreamt[MI_ODD] > 0u || lib.dreamt[MI_SEQ_LR] > 0u);

  /* intuition and packing over a run of mixed worlds */
  mi_library_init(&lib);
  for (k = 0u; k < 4u; k++) {
    unsigned kind;
    for (kind = 0u; kind < WG_KINDS; kind++) play_kind(&lib, kind, 3u, 900u + 10u * k, 1u, &fin, &r);
  }
  {
    pl_game_t g;
    wg_make_kind(&g, WG_ORDER, 3u, 999u);
    mi_play(&lib, &g, 4000u, MI_FAMILIES, &r);
  }
  check("a world like ones it has met is not new to it", r.novelty <= 2u);
  /* (a world with a legend looks like nothing else; an odd-one-out world looks
     like a maze, whose exit is also the only tile of its colour, so intuition
     there fairly says "cover a colour") */
  check("and its intuition names what ended those: the legend in order", r.intuition == MI_SEQ_LR);
  check("ideas that kept explaining worlds are packed into blocks it starts with",
        lib.packed[MI_COVER] && lib.packed[MI_ODD] && lib.packed[MI_SEQ_LR]);

  /* optogenetics on itself, in pairs: a grown library, where packing gives what
     the inventor would build, so each alone costs little and both cost much */
  {
    static mi_library_t grown;
    mi_library_init(&grown);
    for (k = 0u; k < 4u; k++) {
      unsigned kind;
      for (kind = WG_ODD; kind <= WG_ORDER; kind++) play_kind(&grown, kind, 4u, 3000u + 10u * k, 1u, &fin, &r);
    }
    PROBE_KIND_BASE = 4000u;
    mi_probe(&grown, probe_world, 6u, 4000u, 0);
    check("switching its parts off in pairs, it finds the inventor and packing stand in for each other",
          grown.probe_pair[MI_PART_INVENTOR][MI_PART_PACKING] >
              grown.probe_delta[MI_PART_INVENTOR] + grown.probe_delta[MI_PART_PACKING] + 20 &&
              grown.probe_delta[MI_PART_IMAGINATION] > 200 && grown.probes == 1u);
  }

  /* memory */
  {
    FILE *f = fopen(TMP, "w");
    char line[256];
    if (f != 0) {
      mi_library_save(&lib, f);
      fclose(f);
    }
    mi_library_init(&lib2);
    f = fopen(TMP, "r");
    while (f != 0 && fgets(line, sizeof line, f) != 0) mi_library_line(&lib2, line);
    if (f != 0) fclose(f);
    remove(TMP);
    check("the library is written down and read back exactly", memcmp(&lib, &lib2, sizeof lib) == 0);
  }

  /* repeatable */
  {
    pl_game_t g;
    lib2 = lib;
    wg_make_kind(&g, WG_MIRROR, 5u, 4242u);
    mi_play(&lib, &g, 4000u, MI_FAMILIES, &r);
    wg_make_kind(&g, WG_MIRROR, 5u, 4242u);
    mi_play(&lib2, &g, 4000u, MI_FAMILIES, &r2);
    check("the same library and the same world give the same play",
          memcmp(&r, &r2, sizeof r) == 0 && memcmp(&lib, &lib2, sizeof lib) == 0);
  }

  mi_library_init(&fresh);
  check("NULL arguments are checked errors",
        mi_play(0, 0, 1u, MI_FAMILIES, &r) == SM_ERR_NULL_ARGUMENT &&
            mi_play(&fresh, 0, 1u, MI_FAMILIES, &r) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
