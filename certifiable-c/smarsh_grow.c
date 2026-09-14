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
  const char *why;
} gw_gate_t;

static const gw_gate_t MAP[GW_KINDS] = {
    {WG_MAZE, 1, 0u, 0u, "where it began"},
    {WG_KEYS, 0, WG_MAZE, 3u,
     "a door stands between it and the end, and only a cause it has never met opens it"},
    {WG_PUSH, 0, WG_MAZE, 3u, "the end is about where something else is, not where it is"},
};

static const char *const KIND_KEY[GW_KINDS] = {"maze", "keys", "push"};

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
};

static const gw_field_t TRACK[] = {
    GW_TRK(unlocked, 1), GW_TRK(has_frontier, 1), GW_TRK(difficulty, 0), GW_TRK(frontier, 0),
    GW_TRK(streak, 0),   GW_TRK(stuck, 0),        GW_TRK(worlds, 0),     GW_TRK(finished, 0),
    GW_TRK(finished_well, 0),
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
          strcmp(key, "finished") != 0 && strcmp(key, "finished_well") != 0) {
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

/* The kind to practise: of those open, the one it is least far along in. */
static unsigned choose_kind(const gw_mind_t *m) {
  unsigned k, best = WG_MAZE;
  for (k = 0u; k < GW_KINDS; k++) {
    if (!m->track[k].unlocked) continue;
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

static void write_findings(FILE *j, const pl_child_t *ch, const pl_game_t *g) {
  unsigned k, a, dead = 0u, moving = 0u, t;
  int any;
  fprintf(j, "    itself: ");
  if (ch->have_body) {
    fprintf(j, "%u cell%s that moved together", ch->body_n, ch->body_n == 1u ? "" : "s");
  } else {
    fprintf(j, "not held at the end");
  }
  for (a = 1u; a <= g->n_actions; a++) {
    if (ch->effect[a] == PL_MOVES) moving++;
    if (ch->effect[a] == PL_STILL) dead++;
  }
  fprintf(j, "; %u action%s that move it, %u that do nothing\n", moving, moving == 1u ? "" : "s",
          dead);
  fprintf(j, "    in the way:");
  any = 0;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (ch->wall_alive[k] && !ch->wall_ruled_out[k]) {
      fprintf(j, " colour %u", k);
      any = 1;
    }
  }
  if (!any) fprintf(j, " nothing it can name");
  fprintf(j, "; it ends a level by covering:");
  any = 0;
  for (k = 0u; k < PL_COLOURS; k++) {
    if (ch->endings > 0u && ch->end_alive[k] && ch->end_seen[k]) {
      fprintf(j, " colour %u", k);
      any = 1;
    }
  }
  if (!any) fprintf(j, " nothing settled");
  fprintf(j, "\n");
  if (ch->causes_seen > 0u) {
    fprintf(j, "    causes it found:");
    any = 0;
    for (k = 0u; k < PL_COLOURS; k++) {
      for (t = 0u; t < PL_COLOURS; t++) {
        if (ch->cause_to[k][t] >= 0 && t != k) {
          fprintf(j, " covering colour %u turns colour %u into colour %d;", k, t,
                  (int)ch->cause_to[k][t]);
          any = 1;
        }
      }
    }
    if (!any) fprintf(j, " changes happened, but no single cause survived");
    fprintf(j, "\n");
  }
  if (ch->push_known) {
    fprintf(j, "    it pushes a thing of %u cell%s, %u times", ch->push_n,
            ch->push_n == 1u ? "" : "s", ch->pushes);
    any = 0;
    for (k = 0u; k < PL_COLOURS; k++) {
      if (ch->push_endings > 0u && ch->pend_alive[k] && ch->pend_seen[k]) {
        if (!any) fprintf(j, "; a level ends when that thing covers");
        fprintf(j, " colour %u", k);
        any = 1;
      }
    }
    fprintf(j, "\n");
  }
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
    pl_child_t ch;
    sm_status_t st;
    unsigned kind = choose_kind(m), done_actions, best, later, later_best, seed = m->next_seed++;
    gw_track_t *t = &m->track[kind];
    wg_facts_t facts;
    int finished, well;
    unsigned surprised;

    wg_make_kind(&g, kind, t->difficulty, seed);
    wg_facts(&g, &facts);
    pl_child_init(&ch);   /* a new world: nothing about the last one applies to this one */
    st = pl_play(&ch, &g, GW_BUDGET);

    finished = (st == SM_OK && ch.levels_done >= g.levels);
    done_actions = sum(ch.level_actions, ch.levels_done);
    best = sum(ch.level_shortest, ch.levels_done);
    /*
     * The first level of a world is where it finds out how that world works,
     * and that costs what it costs. Whether it learned shows on the levels
     * after: finished well means every level ended, and the later ones
     * within twice their shortest way.
     */
    later = done_actions - (ch.levels_done > 0u ? ch.level_actions[0] : 0u);
    later_best = best - (ch.levels_done > 0u ? ch.level_shortest[0] : 0u);
    well = finished && later_best > 0u && later <= 2u * later_best;
    surprised = ch.lost_itself + ch.mixed + ch.unexplained_endings + ch.cornered;

    m->worlds++;
    t->worlds++;
    m->lost_itself += ch.lost_itself;
    m->mixed += ch.mixed;
    m->unexplained += ch.unexplained_endings;
    m->cornered += ch.cornered;
    m->restarts += ch.restarts;
    if (finished) {
      m->finished++;
      t->finished++;
      m->actions += done_actions;
      m->shortest += best;
    }

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
      } else {
        fprintf(journal, "    did not finish: %u of %u levels in %u actions\n", ch.levels_done,
                g.levels, ch.actions_taken);
      }
      write_findings(journal, &ch, &g);
      if (ch.restarts > 0u) {
        fprintf(journal, "    it stranded itself and started a level again %u time%s\n",
                ch.restarts, ch.restarts == 1u ? "" : "s");
      }
      if (surprised == 0u) {
        fprintf(journal, "    nothing it could not explain\n");
      } else {
        fprintf(journal, "    what it could not explain:");
        if (ch.lost_itself) {
          fprintf(journal, " it lost its account of itself %u times;", ch.lost_itself);
        }
        if (ch.mixed) fprintf(journal, " an action moved it two different ways;");
        if (ch.cornered) {
          fprintf(journal, " %u time%s it had nowhere left to go, so something it had", ch.cornered,
                  ch.cornered == 1u ? "" : "s");
          fprintf(journal, " concluded was wrong;");
        }
        if (ch.unexplained_endings) {
          fprintf(journal, " %u level%s ended for no reason it suspected;", ch.unexplained_endings,
                  ch.unexplained_endings == 1u ? "" : "s");
        }
        fprintf(journal, "\n");
      }
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
}
