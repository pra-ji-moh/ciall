/*
 * smarsh_grow.c -- the child growing by itself, through a map of worlds.
 * See smarsh_grow.h.
 */

#include "smarsh_grow.h"

#include <stddef.h>
#include <string.h>
#include <time.h>

#include "arc_worldgen.h"
#include "smarsh_play.h"

#define GW_BUDGET 4000u      /* actions it may spend on one world */
#define GW_STEP_UP 3u        /* finished well this many times in a row: harder */
#define GW_STEP_BACK 5u      /* not finished this many times in a row: easier */

/* The map: what each kind needs before it opens. */
typedef struct {
  unsigned kind;
  int needs_nothing;
  unsigned needs_kind, needs_frontier;
  int abstract;
  const char *why;
} gw_gate_t;

static const gw_gate_t MAP[GW_KINDS] = {
    {WG_MAZE, 1, 0u, 0u, 0, "where it began"},
    {WG_KEYS, 0, WG_MAZE, 3u, 0,
     "a door stands between it and the end, and only a cause it has never met opens it"},
    {WG_PUSH, 0, WG_MAZE, 3u, 0, "the end is about where something else is, not where it is"},
    {WG_ODD, 0, WG_MAZE, 0u, 1,
     "the end is the one thing that differs from the rest, and what differs changes every level"},
    {WG_MIRROR, 0, WG_MAZE, 0u, 1,
     "a twin moves whenever it moves, but transformed, and only the twin can end the level"},
    {WG_ORDER, 0, WG_MAZE, 0u, 1,
     "a legend shows an order, and the end is covering things in that order"},
    {WG_REVERSE, 0, WG_ORDER, 3u, 1,
     "the legend is to be read the other way, and a mark at one end says where to start"},
    {WG_ODDTWIN, 0, WG_MIRROR, 3u, 1,
     "the twin, not itself, must cover the one tile that differs"},
};

static const char *const KIND_KEY[GW_KINDS] = {"maze", "keys", "push", "odd",
                                                "mirror", "order", "reverse", "oddtwin"};

void gw_mind_init(gw_mind_t *m) {
  unsigned k;
  if (m == 0) return;
  memset(m, 0, sizeof(*m));
  m->next_seed = 1u;
  for (k = 0u; k < GW_KINDS; k++) m->track[k].unlocked = MAP[k].needs_nothing;
}

/* ---- memory on disk ----------------------------------------------------------- */

typedef struct {
  const char *name;
  size_t offset;
  int is_int;
} gw_field_t;

#define GW_TOP(f) {#f, offsetof(gw_mind_t, f), 0}
#define GW_TRK(f, is_int) {#f, offsetof(gw_track_t, f), is_int}

static const gw_field_t TOP[] = {
    GW_TOP(runs),        GW_TOP(worlds),      GW_TOP(finished), GW_TOP(finished_well),
    GW_TOP(next_seed),   GW_TOP(actions),     GW_TOP(shortest), GW_TOP(lost_itself),
    GW_TOP(mixed),       GW_TOP(unexplained), GW_TOP(cornered), GW_TOP(restarts),
    GW_TOP(predictions), GW_TOP(mispredicted), GW_TOP(taught),
};

static const gw_field_t TRACK[] = {
    GW_TRK(unlocked, 1), GW_TRK(has_frontier, 1), GW_TRK(difficulty, 0), GW_TRK(frontier, 0),
    GW_TRK(streak, 0),   GW_TRK(stuck, 0),        GW_TRK(worlds, 0),     GW_TRK(finished, 0),
    GW_TRK(finished_well, 0), GW_TRK(struggling, 0),
};

static void put(void *base, const gw_field_t *f, unsigned value) {
  if (f->is_int) {
    int v = value != 0u;
    memcpy((char *)base + f->offset, &v, sizeof v);
  } else {
    memcpy((char *)base + f->offset, &value, sizeof value);
  }
}

static unsigned get(const void *base, const gw_field_t *f) {
  if (f->is_int) {
    int v;
    memcpy(&v, (const char *)base + f->offset, sizeof v);
    return v ? 1u : 0u;
  } else {
    unsigned v;
    memcpy(&v, (const char *)base + f->offset, sizeof v);
    return v;
  }
}

sm_status_t gw_load(gw_mind_t *m, const char *path) {
  FILE *f;
  char line[160];
  unsigned i, k;
  if (m == 0 || path == 0) return SM_ERR_NULL_ARGUMENT;
  gw_mind_init(m);
  f = fopen(path, "r");
  if (f == 0) return SM_ERR_EMPTY_DOMAIN;
  while (fgets(line, sizeof line, f) != 0) {
    char key[96];
    unsigned value;
    char *dot;
    if (line[0] == '#') continue;
    if (mi_library_line(&m->lib, line)) continue;
    if (sscanf(line, "%95s %u", key, &value) != 2) continue;
    dot = strchr(key, '.');
    if (dot != 0) {
      *dot = '\0';
      for (k = 0u; k < GW_KINDS; k++) {
        if (strcmp(key, KIND_KEY[k]) != 0) continue;
        for (i = 0u; i < sizeof TRACK / sizeof TRACK[0]; i++) {
          if (strcmp(dot + 1, TRACK[i].name) == 0) put(&m->track[k], &TRACK[i], value);
        }
      }
      continue;
    }
    for (i = 0u; i < sizeof TOP / sizeof TOP[0]; i++) {
      if (strcmp(key, TOP[i].name) == 0) put(m, &TOP[i], value);
    }
    /* a mind from before the map: its progress was all in mazes */
    for (i = 0u; i < sizeof TRACK / sizeof TRACK[0]; i++) {
      if (strcmp(key, TRACK[i].name) == 0 && strcmp(key, "worlds") != 0 &&
          strcmp(key, "finished") != 0 && strcmp(key, "finished_well") != 0 &&
          strcmp(key, "predictions") != 0) {
        put(&m->track[WG_MAZE], &TRACK[i], value);
      }
    }
  }
  fclose(f);
  if (m->next_seed == 0u) m->next_seed = 1u;
  m->track[WG_MAZE].unlocked = 1;
  for (k = 0u; k < GW_KINDS; k++) {
    if (m->track[k].difficulty > WG_MAX_DIFFICULTY) m->track[k].difficulty = WG_MAX_DIFFICULTY;
  }
  return SM_OK;
}

sm_status_t gw_save(const gw_mind_t *m, const char *path) {
  FILE *f;
  unsigned i, k;
  if (m == 0 || path == 0) return SM_ERR_NULL_ARGUMENT;
  f = fopen(path, "w");
  if (f == 0) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  fprintf(f, "# where the child stands. Written by the child; read by the child.\n");
  for (i = 0u; i < sizeof TOP / sizeof TOP[0]; i++) {
    fprintf(f, "%s %u\n", TOP[i].name, get(m, &TOP[i]));
  }
  for (k = 0u; k < GW_KINDS; k++) {
    for (i = 0u; i < sizeof TRACK / sizeof TRACK[0]; i++) {
      fprintf(f, "%s.%s %u\n", KIND_KEY[k], TRACK[i].name, get(&m->track[k], &TRACK[i]));
    }
  }
  mi_library_save(&m->lib, f);
  fclose(f);
  return SM_OK;
}

/* ---- growing ------------------------------------------------------------------- */

static void stamp(FILE *j) {
  time_t now = time(0);
  struct tm *t = localtime(&now);
  char buf[32];
  if (t != 0 && strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", t) > 0u) {
    fprintf(j, "%s", buf);
  } else {
    fprintf(j, "(no clock)");
  }
}

static unsigned sum(const unsigned *v, unsigned n) {
  unsigned i, t = 0u;
  for (i = 0u; i < n && i < PL_MAX_LEVELS; i++) t += v[i];
  return t;
}

/* How far along a kind is: nothing shown yet counts below difficulty 0. */
static unsigned standing(const gw_track_t *t) {
  return t->has_frontier ? t->frontier + 1u : 0u;
}

/* The kind to practise. Once any abstract kind is open, worlds alternate
   between the abstract kinds and the concrete ones; within the side whose
   turn it is, the kind it is least far along in. */
static unsigned choose_kind(const gw_mind_t *m) {
  unsigned k, best = WG_MAZE;
  int any_abstract = 0, abstract_turn;
  for (k = 0u; k < GW_KINDS; k++) {
    if (m->track[k].unlocked && MAP[k].abstract) any_abstract = 1;
  }
  abstract_turn = any_abstract && (m->worlds % 2u) == 1u;
  if (abstract_turn) {
    for (k = 0u; k < GW_KINDS; k++) {
      if (m->track[k].unlocked && MAP[k].abstract) {
        best = k;
        break;
      }
    }
  }
  for (k = 0u; k < GW_KINDS; k++) {
    if (!m->track[k].unlocked || MAP[k].abstract != abstract_turn) continue;
    if (standing(&m->track[k]) < standing(&m->track[best])) {
      best = k;
    } else if (standing(&m->track[k]) == standing(&m->track[best]) &&
               m->track[k].worlds < m->track[best].worlds) {
      best = k;   /* equally far along: the one it has practised least */
    }
  }
  return best;
}

/* Open whatever it has now earned. */
static void open_gates(gw_mind_t *m, FILE *journal) {
  unsigned k;
  for (k = 0u; k < GW_KINDS; k++) {
    const gw_track_t *need;
    if (m->track[k].unlocked || MAP[k].needs_nothing) continue;
    need = &m->track[MAP[k].needs_kind];
    if (!need->has_frontier || need->frontier < MAP[k].needs_frontier) continue;
    m->track[k].unlocked = 1;
    if (journal != 0) {
      fprintf(journal, "  a new kind of world opens: %s. It earned it by handling %s at difficulty"
                       " %u. What is new: %s. It is told nothing about how it works.\n",
              wg_kind_name(k), wg_kind_name(MAP[k].needs_kind), MAP[k].needs_frontier,
              MAP[k].why);
    }
  }
}

/* What the teacher knows and the child does not: what kind of idea ends a kind of world. */
static unsigned teacher_says(unsigned kind) {
  switch (kind) {
    case WG_ODD: return MI_ODD;
    case WG_ODDTWIN: return MI_ODD;
    case WG_ORDER: return MI_SEQ_LR;
    case WG_REVERSE: return MI_SEQ_RL;
    default: return MI_COVER;
  }
}

static void write_findings(FILE *j, const mi_result_t *r) {
  unsigned f, any = 0u;
  fprintf(j, "    it saw: %u thing%s that move%s", r->movers, r->movers == 1u ? "" : "s",
          r->movers == 1u ? "s" : "");
  if (r->transform) {
    fprintf(j, ", one stepping as another %s", r->transform == 1 ? "mirrored left to right"
                                                  : r->transform == 2 ? "mirrored up and down"
                                                                      : "reversed");
  }
  fprintf(j, "; this world was %s (novelty %u of %u)\n",
          r->novelty == 0u ? "like ones it has met" : r->novelty >= 6u ? "new to it" : "partly familiar",
          r->novelty, MI_FP_K);
  fprintf(j, "    it predicted %u things and %u were wrong; %u puzzle%s nothing it believed explained\n",
          r->predictions, r->errors, r->puzzles, r->puzzles == 1u ? "" : "s");
  for (f = 0u; f < MI_FAMILIES; f++) {
    if (r->invented & (1u << f)) {
      fprintf(j, "%s%s", any ? ", " : "    it invented: ", mi_family_name(f));
      any = 1u;
    }
  }
  if (any) fprintf(j, "\n");
  fprintf(j, "    what ended it: %s", r->winner >= 0 ? mi_family_name((unsigned)r->winner) : "not settled");
  if (r->intuition >= 0) {
    fprintf(j, "; its intuition said %s%s", mi_family_name((unsigned)r->intuition),
            r->trusted ? " (trusted)" : " (not yet trusted)");
  }
  fprintf(j, "\n    how it spent its moves: explore %u, intuition %u, imagine %u, ask %u%s\n",
          r->modes[MI_EXPLORE], r->modes[MI_INTUITION], r->modes[MI_IMAGINE], r->modes[MI_ASK],
          r->hinted ? " (it was taught first)" : "");
}

/* The worlds it experiments on itself with: two of each open kind, at what it practises. */
static gw_mind_t *PROBING;

static void probe_world(pl_game_t *g, unsigned i) {
  unsigned open[GW_KINDS], n = 0u, k;
  for (k = 0u; k < GW_KINDS; k++) {
    if (PROBING->track[k].unlocked) open[n++] = k;
  }
  k = open[(i / 2u) % n];
  wg_make_kind(g, k, PROBING->track[k].difficulty, 1000000u + i);
}

sm_status_t gw_grow(gw_mind_t *m, unsigned worlds, FILE *journal) {
  unsigned w;
  if (m == 0) return SM_ERR_NULL_ARGUMENT;
  m->runs++;
  if (journal != 0) {
    unsigned k;
    fprintf(journal, "\n");
    stamp(journal);
    fprintf(journal, "  a stretch of growth begins. Where it stands:");
    for (k = 0u; k < GW_KINDS; k++) {
      const gw_track_t *t = &m->track[k];
      if (!t->unlocked) {
        fprintf(journal, " %s closed;", wg_kind_name(k));
      } else if (t->has_frontier) {
        fprintf(journal, " %s frontier %u;", wg_kind_name(k), t->frontier);
      } else {
        fprintf(journal, " %s open, nothing shown yet;", wg_kind_name(k));
      }
    }
    fprintf(journal, "\n");
  }
  open_gates(m, journal);

  for (w = 0u; w < worlds; w++) {
    pl_game_t g;
    mi_result_t res;
    sm_status_t st;
    unsigned kind = choose_kind(m), done_actions, best, later, later_best, seed = m->next_seed++;
    unsigned hint = MI_FAMILIES;
    gw_track_t *t = &m->track[kind];
    wg_facts_t facts;
    int finished, well;

    wg_make_kind(&g, kind, t->difficulty, seed);
    wg_facts(&g, &facts);
    /* struggled long enough: the teacher names what kind of idea ends these */
    if (t->struggling >= GW_STEP_BACK) {
      hint = teacher_says(facts.kind);
      t->struggling = 0u;
      m->taught++;
      if (journal != 0) {
        fprintf(journal, "  a teacher, after five %s worlds not done well, says what kind of idea"
                         " ends them: %s\n", wg_kind_name(kind), mi_family_name(hint));
      }
    }
    st = mi_play(&m->lib, &g, GW_BUDGET, hint, &res);

    finished = (st == SM_OK && res.levels_done >= g.levels);
    done_actions = sum(res.level_actions, res.levels_done);
    best = sum(res.level_shortest, res.levels_done);
    /*
     * The first level of a world is where it finds out how that world works,
     * and that costs what it costs. Whether it learned shows on the levels
     * after: finished well means every level ended, and the later ones
     * within twice their shortest way.
     */
    later = done_actions - (res.levels_done > 0u ? res.level_actions[0] : 0u);
    later_best = best - (res.levels_done > 0u ? res.level_shortest[0] : 0u);
    well = finished && later_best > 0u && later <= 2u * later_best;

    m->worlds++;
    t->worlds++;
    m->unexplained += res.puzzles;
    m->predictions += res.predictions;
    m->mispredicted += res.errors;
    if (finished) {
      m->finished++;
      t->finished++;
      m->actions += done_actions;
      m->shortest += best;
    }
    t->struggling = well ? 0u : t->struggling + 1u;

    if (journal != 0) {
      fprintf(journal, "  world %u  %s  difficulty %u  seed %u\n", m->worlds, wg_kind_name(kind),
              t->difficulty, seed);
      if (facts.kind != kind) {
        fprintf(journal, "    (that kind could not be made at this size; it was given a maze)\n");
      }
      if (finished) {
        fprintf(journal, "    finished all %u levels in %u actions, shortest %u", g.levels,
                done_actions, best);
        if (later_best > 0u) {
          fprintf(journal, "; after the first level %u.%02u times the shortest", later / later_best,
                  (unsigned)((100u * (later % later_best)) / later_best));
        }
        fprintf(journal, "%s\n", well ? "" : ", too wasteful to count as learned");
        fprintf(journal, "    scored as ARC-AGI-3 would, every level counting: %u.%u%%\n",
                mi_score(&res, g.levels) / 10u, mi_score(&res, g.levels) % 10u);
      } else {
        fprintf(journal, "    did not finish: %u of %u levels in %u actions\n", res.levels_done,
                g.levels, res.actions);
      }
      write_findings(journal, &res);
    }

    /* judging itself, and choosing what to practise next in this kind */
    if (well) {
      m->finished_well++;
      t->finished_well++;
      t->stuck = 0u;
      t->streak++;
      if (t->streak >= GW_STEP_UP) {
        t->streak = 0u;
        if (!t->has_frontier || t->difficulty > t->frontier) {
          t->frontier = t->difficulty;
          t->has_frontier = 1;
        }
        if (t->difficulty < WG_MAX_DIFFICULTY) {
          t->difficulty++;
          if (journal != 0) {
            fprintf(journal,
                    "  handled %s at difficulty %u well three times running: moving to %u\n",
                    wg_kind_name(kind), t->frontier, t->difficulty);
          }
        } else if (journal != 0) {
          fprintf(journal, "  handled the hardest %s it can be given, three times running\n",
                  wg_kind_name(kind));
        }
        open_gates(m, journal);
      }
    } else {
      t->streak = 0u;
      if (!finished) {
        t->stuck++;
        if (t->stuck >= GW_STEP_BACK) {
          t->stuck = 0u;
          if (journal != 0) {
            fprintf(journal,
                    "  five %s worlds in a row at difficulty %u it could not finish: this is its"
                    " edge there for now\n",
                    wg_kind_name(kind), t->difficulty);
          }
          if (t->difficulty > 0u) {
            t->difficulty--;
            if (journal != 0) {
              fprintf(journal, "  stepping back to %u to consolidate\n", t->difficulty);
            }
          }
        }
      }
    }
  }

  mi_sleep(&m->lib, journal);
  {
    unsigned k, open = 0u;
    for (k = 0u; k < GW_KINDS; k++) open += (unsigned)m->track[k].unlocked;
    PROBING = m;
    mi_probe(&m->lib, probe_world, 2u * open, GW_BUDGET, journal);
  }
  if (journal != 0) {
    stamp(journal);
    fprintf(journal, "  the stretch ends: %u worlds in all\n", m->worlds);
    fflush(journal);
  }
  return SM_OK;
}

void gw_report(const gw_mind_t *m) {
  unsigned k;
  if (m == 0) return;
  printf("  stretches of growth: %u; worlds played: %u; finished: %u; finished well: %u\n",
         m->runs, m->worlds, m->finished, m->finished_well);
  for (k = 0u; k < GW_KINDS; k++) {
    const gw_track_t *t = &m->track[k];
    printf("  %-15s ", wg_kind_name(k));
    if (!t->unlocked) {
      printf("closed: not yet earned\n");
      continue;
    }
    printf("practising %u, ", t->difficulty);
    if (t->has_frontier) {
      printf("frontier %u", t->frontier);
    } else {
      printf("nothing shown yet");
    }
    printf(" (%u worlds, %u finished well)\n", t->worlds, t->finished_well);
  }
  if (m->shortest > 0u) {
    printf("  over finished worlds: %u actions against a shortest %u\n", m->actions, m->shortest);
  }
  printf("  surprises so far: lost itself %u, two-way actions %u, unexplained endings %u,",
         m->lost_itself, m->mixed, m->unexplained);
  printf(" cornered %u, restarts %u\n", m->cornered, m->restarts);
  printf("  predictions: %u, of which wrong %u; taught %u time%s\n", m->predictions, m->mispredicted,
         m->taught, m->taught == 1u ? "" : "s");
  printf("  its library:");
  for (k = 0u; k < MI_FAMILIES; k++) {
    printf(" [%s: explained %u, in sleep %u%s]", mi_family_name(k), m->lib.wins[k], m->lib.dreamt[k],
           m->lib.packed[k] ? ", packed" : "");
  }
  printf("\n  intuition right %u, wrong %u; worlds remembered %u\n", m->lib.intuition_right,
         m->lib.intuition_wrong, m->lib.n_mem);
  if (m->lib.probes > 0u) {
    unsigned q;
    printf("  what its parts are worth (score points with it, minus without):");
    for (k = 0u; k < MI_PARTS; k++) printf(" %s %+.1f", mi_part_name(k), m->lib.probe_delta[k] / 10.0);
    printf("\n  and in pairs (both off together):");
    for (k = 0u; k < MI_PARTS; k++) {
      for (q = k + 1u; q < MI_PARTS; q++) {
        printf(" %s+%s %+.1f", mi_part_name(k), mi_part_name(q), m->lib.probe_pair[k][q] / 10.0);
      }
    }
    printf("\n");
  }
}
