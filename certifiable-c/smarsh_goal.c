/*
 * smarsh_goal.c -- what ends a level, said of the board, learned by elimination.
 * See smarsh_goal.h.
 */
#include "smarsh_goal.h"

#include <stdlib.h>
#include <string.h>

static unsigned fact_of(unsigned type, unsigned v, unsigned w) {
  return type * 256u + v * 16u + w;
}

static void put(uint64_t *b, unsigned i) {
  b[i >> 6] |= (uint64_t)1u << (i & 63u);
}

static int has(const uint64_t *b, unsigned i) {
  return (int)((b[i >> 6] >> (i & 63u)) & 1u);
}

static const char *GD_NAME[GD_DOMAINS] = {"space", "number", "body", "time", "pattern", "others"};

const char *go_domain_name(unsigned domain) {
  return domain < GD_DOMAINS ? GD_NAME[domain] : "?";
}

/* which kind of sense a fact belongs to */
static unsigned domain_of(unsigned f) {
  unsigned type = f / 256u;
  switch (type) {
    case GO_NONE:
    case GO_EQUAL:
    case GO_ABOVE_ONCE:
    case GO_ABOVE_TWICE:
    case GO_ABOVE_NEVER:
    case GO_LEFT_ONCE:
    case GO_LEFT_TWICE:
    case GO_LEFT_NEVER:
      return GD_NUMBER;
    case GO_AGAINST:
      return GD_BODY;
    case GO_ACT:
      return GD_TIME;
    case GO_MIRROR:
    case GO_ABOVE2:
    case GO_LEFT2:
    case GO_CABOVE2:
    case GO_CLEFT2:
      return GD_PATTERN;
    default:
      return GD_SPACE;
  }
}

unsigned go_domains(const go_goal_t *goal) {
  unsigned m = 1u << domain_of(goal->p);
  if (goal->q < GO_FACTS) m |= 1u << domain_of(goal->q);
  if (goal->act != GO_ANY_ACT) m |= 1u << GD_TIME;
  return m;
}

static unsigned popcount(const uint64_t *b) {
  unsigned i, n = 0u;
  for (i = 0u; i < GO_WORDS; i++) {
    uint64_t x = b[i];
    while (x) {
      x &= x - 1u;
      n++;
    }
  }
  return n;
}

/*
 * How the board is laid out, one colour against another: for each column, going down,
 * every place a colour gives way to another with only ground between is one instance of
 * "v above w"; the same place carried on across the next column is the same instance.
 * Likewise along each row for "v left of w". Counted up to 3.
 */
#define GO_CLOSE 2   /* cells of ground between, at most, for two things to be one arrangement */

static void layout(const pl_frame_t *f, unsigned char above[PL_COLOURS][PL_COLOURS],
                   unsigned char left[PL_COLOURS][PL_COLOURS], unsigned char cabove[PL_COLOURS][PL_COLOURS],
                   unsigned char cleft[PL_COLOURS][PL_COLOURS]) {
  static int last_line[PL_COLOURS][PL_COLOURS], last_a[PL_COLOURS][PL_COLOURS], last_b[PL_COLOURS][PL_COLOURS];
  unsigned count[PL_COLOURS], r, c, v, w, ground = 0u;
  memset(above, 0, PL_COLOURS * PL_COLOURS);
  memset(left, 0, PL_COLOURS * PL_COLOURS);
  memset(cabove, 0, PL_COLOURS * PL_COLOURS);
  memset(cleft, 0, PL_COLOURS * PL_COLOURS);
  memset(count, 0, sizeof count);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) count[f->c[r][c]]++;
  }
  for (v = 1u; v < PL_COLOURS; v++) {
    if (count[v] > count[ground]) ground = v;
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    for (w = 0u; w < PL_COLOURS; w++) last_line[v][w] = -10;
  }
  for (c = 0u; c < f->w; c++) {
    int prev = -1, prev_r = -1;
    for (r = 0u; r < f->h; r++) {
      unsigned here = f->c[r][c];
      if (here == ground) continue;
      if (prev >= 0 && (unsigned)prev != here) {
        unsigned a = (unsigned)prev;
        if (!(last_line[a][here] == (int)c - 1 && last_a[a][here] == prev_r && last_b[a][here] == (int)r)) {
          if (above[a][here] < 3u) above[a][here]++;
          if ((int)r - prev_r - 1 <= GO_CLOSE && cabove[a][here] < 3u) cabove[a][here]++;
        }
        last_line[a][here] = (int)c;
        last_a[a][here] = prev_r;
        last_b[a][here] = (int)r;
      }
      prev = (int)here;
      prev_r = (int)r;
    }
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    for (w = 0u; w < PL_COLOURS; w++) last_line[v][w] = -10;
  }
  for (r = 0u; r < f->h; r++) {
    int prev = -1, prev_c = -1;
    for (c = 0u; c < f->w; c++) {
      unsigned here = f->c[r][c];
      if (here == ground) continue;
      if (prev >= 0 && (unsigned)prev != here) {
        unsigned a = (unsigned)prev;
        if (!(last_line[a][here] == (int)r - 1 && last_a[a][here] == prev_c && last_b[a][here] == (int)c)) {
          if (left[a][here] < 3u) left[a][here]++;
          if ((int)c - prev_c - 1 <= GO_CLOSE && cleft[a][here] < 3u) cleft[a][here]++;
        }
        last_line[a][here] = (int)r;
        last_a[a][here] = prev_c;
        last_b[a][here] = (int)c;
      }
      prev = (int)here;
      prev_c = (int)c;
    }
  }
}

/* every fact of the goal language, true or false, of the board an act left */
static void truths(const go_world_t *g, unsigned act, const pl_frame_t *f, int body, uint64_t *t) {
  unsigned count[PL_COLOURS], was[PL_COLOURS], r, c, v, w;
  unsigned char beside[PL_COLOURS][PL_COLOURS];   /* some v cell has no w beside it: 0; every one has: 1 */
  unsigned char near_body[PL_COLOURS];
  memset(t, 0, GO_WORDS * sizeof t[0]);
  memset(count, 0, sizeof count);
  memset(was, 0, sizeof was);
  memset(near_body, 0, sizeof near_body);
  for (v = 0u; v < PL_COLOURS; v++) {
    for (w = 0u; w < PL_COLOURS; w++) beside[v][w] = 1u;
  }
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      unsigned here = f->c[r][c], nb = 0u;   /* colours beside this cell */
      count[here]++;
      if (r < g->start.h && c < g->start.w) was[g->start.c[r][c]]++;
      if (r > 0u) nb |= 1u << f->c[r - 1u][c];
      if (c > 0u) nb |= 1u << f->c[r][c - 1u];
      if (r + 1u < f->h) nb |= 1u << f->c[r + 1u][c];
      if (c + 1u < f->w) nb |= 1u << f->c[r][c + 1u];
      for (w = 0u; w < PL_COLOURS; w++) {
        if (!((nb >> w) & 1u)) beside[here][w] = 0u;
      }
      if (body >= 0 && here == (unsigned)body) {
        for (w = 0u; w < PL_COLOURS; w++) {
          if ((nb >> w) & 1u) near_body[w] = 1u;
        }
        if (r < g->start.h && c < g->start.w) near_body[g->start.c[r][c]] = 1u;   /* standing where it was */
      }
    }
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    if (was[v] > 0u && count[v] == 0u) put(t, fact_of(GO_NONE, v, 0u));
    if (body >= 0 && v != (unsigned)body && near_body[v]) put(t, fact_of(GO_AGAINST, v, 0u));
    for (w = v + 1u; w < PL_COLOURS; w++) {
      if (was[v] > 0u && was[w] > 0u && count[v] == count[w]) put(t, fact_of(GO_EQUAL, v, w));
    }
    for (w = 0u; w < PL_COLOURS; w++) {
      if (w != v && count[v] > 0u && beside[v][w]) put(t, fact_of(GO_BESIDE, v, w));
    }
  }
  {
    int lr = 1, tb = 1;
    for (r = 0u; r < f->h && (lr || tb); r++) {
      for (c = 0u; c < f->w; c++) {
        if (f->c[r][c] != f->c[r][f->w - 1u - c]) lr = 0;
        if (f->c[r][c] != f->c[f->h - 1u - r][c]) tb = 0;
      }
    }
    if (lr) put(t, fact_of(GO_MIRROR, 0u, 0u));
    if (tb) put(t, fact_of(GO_MIRROR, 1u, 0u));
  }
  if (act < 16u) put(t, fact_of(GO_ACT, act, 0u));
  {
    unsigned char above[PL_COLOURS][PL_COLOURS], left[PL_COLOURS][PL_COLOURS];
    unsigned char cabove[PL_COLOURS][PL_COLOURS], cleft[PL_COLOURS][PL_COLOURS];
    layout(f, above, left, cabove, cleft);
    for (v = 0u; v < PL_COLOURS; v++) {
      for (w = 0u; w < PL_COLOURS; w++) {
        if (v == w) continue;
        if (above[v][w] >= 1u) put(t, fact_of(GO_ABOVE, v, w));
        if (above[v][w] >= 2u) put(t, fact_of(GO_ABOVE2, v, w));
        if (left[v][w] >= 1u) put(t, fact_of(GO_LEFT, v, w));
        if (left[v][w] >= 2u) put(t, fact_of(GO_LEFT2, v, w));
        if (cabove[v][w] >= 1u) put(t, fact_of(GO_CABOVE, v, w));
        if (cabove[v][w] >= 2u) put(t, fact_of(GO_CABOVE2, v, w));
        if (cleft[v][w] >= 1u) put(t, fact_of(GO_CLEFT, v, w));
        if (cleft[v][w] >= 2u) put(t, fact_of(GO_CLEFT2, v, w));
        if (cabove[v][w] == 1u) put(t, fact_of(GO_ABOVE_ONCE, v, w));
        if (cabove[v][w] == 2u) put(t, fact_of(GO_ABOVE_TWICE, v, w));
        if (cabove[v][w] == 0u) put(t, fact_of(GO_ABOVE_NEVER, v, w));
        if (cleft[v][w] == 1u) put(t, fact_of(GO_LEFT_ONCE, v, w));
        if (cleft[v][w] == 2u) put(t, fact_of(GO_LEFT_TWICE, v, w));
        if (cleft[v][w] == 0u) put(t, fact_of(GO_LEFT_NEVER, v, w));
      }
    }
  }
}

/* which facts can mean anything at all here: the language as this game offers it */
static void meaningful(uint64_t *m) {
  unsigned v, w;
  memset(m, 0, GO_WORDS * sizeof m[0]);
  for (v = 0u; v < PL_COLOURS; v++) {
    put(m, fact_of(GO_NONE, v, 0u));
    put(m, fact_of(GO_AGAINST, v, 0u));
    for (w = 0u; w < PL_COLOURS; w++) {
      if (w > v) put(m, fact_of(GO_EQUAL, v, w));
      if (w != v) {
        put(m, fact_of(GO_BESIDE, v, w));
        put(m, fact_of(GO_ABOVE, v, w));
        put(m, fact_of(GO_LEFT, v, w));
        put(m, fact_of(GO_ABOVE2, v, w));
        put(m, fact_of(GO_LEFT2, v, w));
        put(m, fact_of(GO_CABOVE, v, w));
        put(m, fact_of(GO_CLEFT, v, w));
        put(m, fact_of(GO_CABOVE2, v, w));
        put(m, fact_of(GO_CLEFT2, v, w));
        put(m, fact_of(GO_ABOVE_ONCE, v, w));
        put(m, fact_of(GO_ABOVE_TWICE, v, w));
        put(m, fact_of(GO_ABOVE_NEVER, v, w));
        put(m, fact_of(GO_LEFT_ONCE, v, w));
        put(m, fact_of(GO_LEFT_TWICE, v, w));
        put(m, fact_of(GO_LEFT_NEVER, v, w));
      }
    }
  }
  put(m, fact_of(GO_MIRROR, 0u, 0u));
  put(m, fact_of(GO_MIRROR, 1u, 0u));
  for (v = 0u; v < 8u; v++) put(m, fact_of(GO_ACT, v, 0u));
}

void go_begin(go_world_t *g) {
  memset(g, 0, sizeof *g);
  meaningful(g->alive);
  memcpy(g->ever, g->alive, sizeof g->ever);
}

void go_level(go_world_t *g, const pl_frame_t *start, unsigned level) {
  g->start = *start;
  g->level = level;
}

static int pair_holds(const uint64_t *t, unsigned p, unsigned q) {
  return has(t, p) && (q >= GO_FACTS || has(t, q));
}

/* a stored goal on a board after an act: its facts hold, and the act was its act if it has one */
static int stored_holds(const go_world_t *g, unsigned i, const uint64_t *t) {
  if (!pair_holds(t, g->pair_p[i], g->pair_q[i])) return 0;
  return g->pair_act[i] == GO_ANY_ACT || has(t, fact_of(GO_ACT, g->pair_act[i], 0u));
}

static void add_goal(go_world_t *g, unsigned p, unsigned q, unsigned act) {
  if (g->n_pairs >= GO_PAIRS) {
    g->pairs_dropped++;
    return;
  }
  g->pair_p[g->n_pairs] = p;
  g->pair_q[g->n_pairs] = q;
  g->pair_act[g->n_pairs] = act;
  g->pair_alive[g->n_pairs] = 1u;
  g->n_pairs++;
}

void go_saw(go_world_t *g, unsigned act, const pl_frame_t *after, int body) {
  uint64_t t[GO_WORDS];
  unsigned i;
  truths(g, act, after, body, t);
  /* it did not end: nothing that holds here is what ends it */
  for (i = 0u; i < GO_WORDS; i++) g->alive[i] &= ~t[i];
  for (i = 0u; i < g->n_pairs; i++) {
    if (g->pair_alive[i] && stored_holds(g, i, t)) g->pair_alive[i] = 0u;
  }
  memcpy(g->seen[g->seen_next], t, sizeof t);
  g->seen_level[g->seen_next] = (unsigned char)(g->level > 255u ? 255u : g->level);
  g->seen_next = (g->seen_next + 1u) % GO_SEEN;
  if (g->seen_count < GO_SEEN) g->seen_count++;
}

/*
 * Did this pair never both hold after an act that ended nothing? Only in this level,
 * if asked; only on the boards where act `act` was done, if one is named.
 */
static int never_both(const go_world_t *g, unsigned p, unsigned q, int this_level_only, unsigned act) {
  unsigned i;
  for (i = 0u; i < g->seen_count; i++) {
    if (this_level_only && g->seen_level[i] != (unsigned char)g->level) continue;
    if (act != GO_ANY_ACT && !has(g->seen[i], fact_of(GO_ACT, act, 0u))) continue;
    if (has(g->seen[i], p) && (q >= GO_FACTS || has(g->seen[i], q))) return 0;
  }
  return 1;
}

/* was this fact ever false on a board that counts (so that it could split anything)? */
static int ever_false(const go_world_t *g, unsigned f, unsigned act) {
  unsigned i;
  for (i = 0u; i < g->seen_count; i++) {
    if (act != GO_ANY_ACT && !has(g->seen[i], fact_of(GO_ACT, act, 0u))) continue;
    if (!has(g->seen[i], f)) return 1;
  }
  return 0;
}

static int held_at_every_end(const go_world_t *g, unsigned p, unsigned q) {
  unsigned e;
  for (e = 0u; e < g->n_ends; e++) {
    if (!pair_holds(g->ends[e], p, q)) return 0;
  }
  return 1;
}

void go_ended(go_world_t *g, unsigned act, const pl_frame_t *after, int body, int seen) {
  uint64_t t[GO_WORDS], was[GO_WORDS];
  unsigned i, j, standing = 0u;
  truths(g, act, after, body, t);
  g->endings++;
  if (seen) g->from_board++;
  else g->from_guess++;
  if (g->n_ends < GO_ENDS) memcpy(g->ends[g->n_ends++], t, sizeof t);
  memcpy(was, g->alive, sizeof was);
  /* it ended: whatever does not hold here is not what ends it */
  for (i = 0u; i < GO_WORDS; i++) g->alive[i] &= t[i];
  for (i = 0u; i < g->n_pairs; i++) {
    if (g->pair_alive[i] && !stored_holds(g, i, t)) g->pair_alive[i] = 0u;
  }
  {
    /* which kinds of sense this ending left with nothing standing */
    unsigned d, f;
    unsigned left[GD_DOMAINS];
    memset(left, 0, sizeof left);
    for (f = 0u; f < GO_FACTS; f++) {
      if (has(g->alive, f)) left[domain_of(f)]++;
    }
    for (d = 0u; d < GD_DOMAINS; d++) {
      if (left[d] == 0u) g->domain_blank[d]++;
    }
  }
  standing = go_count(g);
  if (standing > 0u) return;
  /*
   * Everything it held is ruled out. It does not take that as the end. Two accounts,
   * held together: the goal changed with the level (judge single facts on this level's
   * evidence alone), and it takes two facts together (pairs that held at every ending
   * and were never both true after an act that ended nothing).
   */
  g->blanks++;
  {
    uint64_t m[GO_WORDS];
    unsigned f;
    meaningful(m);
    for (f = 0u; f < GO_FACTS; f++) {
      if (!has(m, f) || !has(t, f)) continue;
      if (never_both(g, f, GO_FACTS, 1, GO_ANY_ACT)) {
        put(g->alive, f);
        g->relevelled++;
      }
    }
  }
  {
    static unsigned cand[GO_FACTS];
    unsigned nc = 0u, f, trig;
    uint64_t m[GO_WORDS];
    meaningful(m);
    /* pairs, over every board: only facts that held here and were ever false elsewhere */
    for (f = 0u; f < GO_FACTS; f++) {
      if (has(m, f) && has(t, f) && f / 256u != GO_ACT && ever_false(g, f, GO_ANY_ACT)) cand[nc++] = f;
    }
    for (i = 0u; i < nc; i++) {
      for (j = i + 1u; j < nc; j++) {
        if (!held_at_every_end(g, cand[i], cand[j])) continue;
        if (!never_both(g, cand[i], cand[j], 0, GO_ANY_ACT)) continue;
        add_goal(g, cand[i], cand[j], GO_ANY_ACT);
        g->widened++;
      }
    }
    /*
     * It ends only on the act that ended it: judge one fact or two on the boards where
     * that act was done and nothing ended. A board where it was not done says nothing.
     */
    trig = act;
    if (trig < 16u) {
      nc = 0u;
      for (f = 0u; f < GO_FACTS; f++) {
        if (has(m, f) && has(t, f) && f / 256u != GO_ACT && ever_false(g, f, trig)) cand[nc++] = f;
      }
      for (i = 0u; i < nc; i++) {
        if (never_both(g, cand[i], GO_FACTS, 0, trig)) {
          add_goal(g, cand[i], GO_FACTS, trig);
          g->triggered++;
        }
      }
      for (i = 0u; i < nc; i++) {
        for (j = i + 1u; j < nc; j++) {
          if (!never_both(g, cand[i], cand[j], 0, trig)) continue;
          add_goal(g, cand[i], cand[j], trig);
          g->triggered++;
        }
      }
    }
  }
  if (go_count(g) == 0u) g->empty_after++;
  (void)was;
}

unsigned go_count(const go_world_t *g) {
  unsigned i, n = popcount(g->alive);
  for (i = 0u; i < g->n_pairs; i++) n += g->pair_alive[i];
  return n;
}

/* the i-th goal standing, in a fixed order: singles, then the rest */
static int nth_goal(const go_world_t *g, unsigned k, go_goal_t *out);

int go_nth(const go_world_t *g, unsigned k, go_goal_t *out) {
  return nth_goal(g, k, out);
}

unsigned go_list(const go_world_t *g, go_goal_t *out, unsigned cap) {
  unsigned n = 0u, w, i;
  for (w = 0u; w < GO_WORDS && n < cap; w++) {
    uint64_t x = g->alive[w];
    while (x != 0u && n < cap) {
      unsigned b = 0u;
      while (!((x >> b) & 1u)) b++;
      x &= x - 1u;
      out[n].p = w * 64u + b;
      out[n].q = GO_FACTS;
      out[n].act = GO_ANY_ACT;
      n++;
    }
  }
  for (i = 0u; i < g->n_pairs && n < cap; i++) {
    if (!g->pair_alive[i]) continue;
    out[n].p = g->pair_p[i];
    out[n].q = g->pair_q[i];
    out[n].act = g->pair_act[i];
    n++;
  }
  return n;
}

#define GO_LIST_CAP (GO_FACTS + GO_PAIRS)
static go_goal_t LIST[GO_LIST_CAP];

void go_kind(const go_goal_t *goal, char *out) {
  unsigned a = goal->p / 256u, b = goal->q < GO_FACTS ? goal->q / 256u : 99u, t;
  if (b != 99u && b < a) {
    t = a;
    a = b;
    b = t;
  }
  if (b == 99u) sprintf(out, "%u/-/", a);
  else sprintf(out, "%u/%u/", a, b);
  if (goal->act == GO_ANY_ACT) strcat(out, "-");
  else sprintf(out + strlen(out), "%u", goal->act);
}

int go_pick(const go_world_t *g, uint64_t z, go_goal_t *out) {
  /*
   * First a kind of sense, uniformly among the combinations of senses that still hold
   * a goal; then a goal, uniformly among those of that combination.
   */
  unsigned n = go_list(g, LIST, GO_LIST_CAP), keys[64], per[64], nk = 0u, k, j, pick_key, pick_n;
  if (n == 0u) return 0;
  memset(per, 0, sizeof per);
  for (k = 0u; k < n; k++) {
    unsigned m = go_domains(&LIST[k]) & 63u;
    for (j = 0u; j < nk && keys[j] != m; j++) {
    }
    if (j == nk) keys[nk++] = m;
    per[j]++;
  }
  pick_key = keys[(unsigned)(z % nk)];
  pick_n = (unsigned)((z / nk) % per[(unsigned)(z % nk)]);
  for (k = 0u; k < n; k++) {
    if ((go_domains(&LIST[k]) & 63u) != pick_key) continue;
    if (pick_n-- == 0u) {
      *out = LIST[k];
      return 1;
    }
  }
  return 0;
}

unsigned go_domain_count(const go_world_t *g, unsigned domain) {
  unsigned n = go_list(g, LIST, GO_LIST_CAP), k, c = 0u;
  for (k = 0u; k < n; k++) {
    if (go_domains(&LIST[k]) & (1u << domain)) c++;
  }
  return c;
}

static int nth_goal(const go_world_t *g, unsigned k, go_goal_t *out) {
  unsigned f, i;
  for (f = 0u; f < GO_FACTS; f++) {
    if (!has(g->alive, f)) continue;
    if (k-- == 0u) {
      out->p = f;
      out->q = GO_FACTS;
      out->act = GO_ANY_ACT;
      return 1;
    }
  }
  for (i = 0u; i < g->n_pairs; i++) {
    if (!g->pair_alive[i]) continue;
    if (k-- == 0u) {
      out->p = g->pair_p[i];
      out->q = g->pair_q[i];
      out->act = g->pair_act[i];
      return 1;
    }
  }
  return 0;
}

int go_standing(const go_world_t *g, const go_goal_t *goal) {
  unsigned i;
  if (goal->q >= GO_FACTS && goal->act == GO_ANY_ACT) return has(g->alive, goal->p);
  for (i = 0u; i < g->n_pairs; i++) {
    if (g->pair_alive[i] && g->pair_p[i] == goal->p && g->pair_q[i] == goal->q && g->pair_act[i] == goal->act) return 1;
  }
  return 0;
}

int go_holds(const go_world_t *g, const go_goal_t *goal, const pl_frame_t *f, int body) {
  uint64_t t[GO_WORDS];
  unsigned a;
  truths(g, 0xFFu, f, body, t);
  for (a = 0u; a < 16u; a++) put(t, fact_of(GO_ACT, a, 0u));   /* the act is the planner's to choose */
  return pair_holds(t, goal->p, goal->q);
}

static void fact_words(unsigned f, char *out) {
  unsigned type = f / 256u, v = (f / 16u) % 16u, w = f % 16u;
  if (type == GO_NONE) sprintf(out, "no colour %u left", v);
  else if (type == GO_AGAINST) sprintf(out, "the body against colour %u", v);
  else if (type == GO_EQUAL) sprintf(out, "as many cells of colour %u as of %u", v, w);
  else if (type == GO_BESIDE) sprintf(out, "every cell of colour %u beside colour %u", v, w);
  else if (type == GO_MIRROR) sprintf(out, "the board its own mirror, %s", v == 0u ? "side to side" : "top to bottom");
  else if (type == GO_ACT) sprintf(out, "act %u done", v);
  else if (type == GO_ABOVE) sprintf(out, "colour %u directly above colour %u somewhere", v, w);
  else if (type == GO_LEFT) sprintf(out, "colour %u directly left of colour %u somewhere", v, w);
  else if (type == GO_ABOVE2) sprintf(out, "colour %u directly above colour %u in two places (a copy of it)", v, w);
  else if (type == GO_LEFT2) sprintf(out, "colour %u directly left of colour %u in two places (a copy of it)", v, w);
  else if (type == GO_CABOVE) sprintf(out, "colour %u right above colour %u, close", v, w);
  else if (type == GO_CLEFT) sprintf(out, "colour %u right left of colour %u, close", v, w);
  else if (type == GO_CABOVE2) sprintf(out, "colour %u right above colour %u, close, in two places (a copy of it)", v, w);
  else if (type == GO_CLEFT2) sprintf(out, "colour %u right left of colour %u, close, in two places (a copy of it)", v, w);
  else if (type == GO_ABOVE_ONCE) sprintf(out, "colour %u right above colour %u in exactly one place", v, w);
  else if (type == GO_ABOVE_TWICE) sprintf(out, "colour %u right above colour %u in exactly two places (the example and one copy)", v, w);
  else if (type == GO_ABOVE_NEVER) sprintf(out, "colour %u right above colour %u nowhere", v, w);
  else if (type == GO_LEFT_ONCE) sprintf(out, "colour %u right left of colour %u in exactly one place", v, w);
  else if (type == GO_LEFT_TWICE) sprintf(out, "colour %u right left of colour %u in exactly two places (the example and one copy)", v, w);
  else sprintf(out, "colour %u right left of colour %u nowhere", v, w);
}

void go_words(const go_goal_t *goal, char *out) {
  char a[120], b[120];
  fact_words(goal->p, a);
  if (goal->q >= GO_FACTS) sprintf(out, "%s", a);
  else {
    fact_words(goal->q, b);
    sprintf(out, "%s, and %s", a, b);
  }
  if (goal->act != GO_ANY_ACT) sprintf(out + strlen(out), ", then act %u", goal->act);
}

void go_report(const go_world_t *g, FILE *out) {
  unsigned n = go_count(g);
  if (g->endings == 0u) {
    fprintf(out, "  what ends a level, said of the board: no ending seen yet; %u goals still possible\n", n);
    return;
  }
  fprintf(out, "  what ends a level, said of the board: %u endings seen (%u on the board the engine drew, "
               "%u imagined); %u goals still standing\n", g->endings, g->from_board, g->from_guess, n);
  if (g->blanks > 0u) {
    fprintf(out, "  %u times an ending ruled out every goal it held; it did not stop: %u goals taken up again "
                 "as the goal having changed with the level, %u pairs of facts formulated, %u goals that hold "
                 "only when the act that ended it is done%s%s\n",
            g->blanks, g->relevelled, g->widened, g->triggered, g->pairs_dropped ? " (some let go for room)" : "",
            g->empty_after ? "; and still nothing left, after some of those" : "");
  }
  {
    unsigned d;
    fprintf(out, "  by kind of sense, goals still standing:");
    for (d = 0u; d < GD_DOMAINS; d++) fprintf(out, " %s %u", GD_NAME[d], go_domain_count(g, d));
    fprintf(out, "\n  endings that left a sense with nothing standing:");
    for (d = 0u; d < GD_DOMAINS; d++) fprintf(out, " %s %u", GD_NAME[d], g->domain_blank[d]);
    fprintf(out, "\n");
  }
  if (n > 0u && n <= 6u) {
    go_goal_t goal;
    unsigned k;
    for (k = 0u; k < n; k++) {
      char words[260];
      if (!nth_goal(g, k, &goal)) break;
      go_words(&goal, words);
      fprintf(out, "    it could be: %s\n", words);
    }
  }
}
