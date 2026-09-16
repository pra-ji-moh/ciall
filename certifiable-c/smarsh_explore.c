/*
 * smarsh_explore.c -- reasoning over the situations it can reach. See
 * smarsh_explore.h. One game at a time: the workspace is static and reused.
 */

#include "smarsh_explore.h"
#include <stdlib.h>
#include <string.h>
#include "smarsh_ending.h"
#include "smarsh_guess.h"
#include "smarsh_self.h"   /* the part of its source the child rewrites */

/*
 * Its own parts, by name, so it can study itself. Each mechanism below can be
 * switched off for a run (CIALL_OFF="panels,stood"): the child's self-study plays
 * a game with one part off and the same game with everything on, and rules out
 * what that part could be for there (needed, not needed, in the way). What is
 * proved in the way for a game is switched off for that game from then on.
 */
typedef enum {
  M_RESTLESS, M_CLOCK, M_STOOD, M_PANELS, M_NEAR, M_THEORY, M_SKIP,
  M_DEATHS, M_UNMASK, M_WINDOW, M_REDRAW, M_RECALL, M_REPLAY, M_REACH, M_CURIOUS, M_PLAN, M_COUNT
} ex_mech_t;
static const char *MECH_NAME[M_COUNT] = {
  "restless", "clock", "stood", "panels", "near", "theory", "skip",
  "deaths", "unmask", "window", "redraw", "recall", "replay", "reach", "curious", "plan"
};
static unsigned OFF_MASK;
#define ON(m) ((OFF_MASK & (1u << (m))) == 0u)

static void read_off(void) {
  const char *off = getenv("CIALL_OFF");
  unsigned m;
  static const unsigned by_self[M_COUNT] = {
    SELF_OFF_RESTLESS, SELF_OFF_CLOCK, SELF_OFF_STOOD, SELF_OFF_PANELS, SELF_OFF_NEAR, SELF_OFF_THEORY,
    SELF_OFF_SKIP, SELF_OFF_DEATHS, SELF_OFF_UNMASK, SELF_OFF_WINDOW, SELF_OFF_REDRAW, SELF_OFF_RECALL,
    SELF_OFF_REPLAY, SELF_OFF_REACH, SELF_OFF_CURIOUS, SELF_OFF_PLAN
  };
  OFF_MASK = 0u;
  for (m = 0u; m < M_COUNT; m++) {
    if (by_self[m]) OFF_MASK |= 1u << m;   /* parts it has switched off in its own source */
  }
  if (off == 0) return;
  for (m = 0u; m < M_COUNT; m++) {
    const char *hit;
    size_t len = strlen(MECH_NAME[m]);
    for (hit = strstr(off, MECH_NAME[m]); hit != 0; hit = strstr(hit + 1, MECH_NAME[m])) {
      if ((hit == off || hit[-1] == ',') && (hit[len] == '\0' || hit[len] == ',')) {
        OFF_MASK |= 1u << m;
        break;
      }
    }
  }
}

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define EX_TBL (1u << 17)
#define EX_TICK_MAX SELF_TICK_MAX          /* a change this small may be something ticking by itself */
#define EX_NONE (-1)
#define EX_THOUGHTS 14u         /* questions written down per level, at most */
#define EX_RESEMBLE 24.0        /* how many steps further it will go for a situation that
                                   looks entirely like winning */

/* ---- what it remembers of this level ----------------------------------------- */

static uint64_t N_HASH[EX_MAX_NODES];
static unsigned char N_NACT[EX_MAX_NODES];
static unsigned char N_UNTRIED[EX_MAX_NODES];
static unsigned short N_ACT[EX_MAX_NODES][EX_MAX_ACTS];
static int N_NEXT[EX_MAX_NODES][EX_MAX_ACTS];
static unsigned char N_FLAG[EX_MAX_NODES][EX_MAX_ACTS];   /* 1 died, 2 ended the level */
static unsigned N_COUNT;
/* how each situation differs from how the level began: cells of colour a now b */
static unsigned char N_CHG[EX_MAX_NODES][256];
/* for pointing: the colour pointed at, and which thing of that colour it was */
static unsigned char N_PCOL[EX_MAX_NODES][EX_MAX_ACTS];
static unsigned char N_PRANK[EX_MAX_NODES][EX_MAX_ACTS];

/*
 * What pointing at a place has done, whatever the situation: how often it
 * changed nothing, and how often it changed something. Pointing at the same
 * place, with the same colour there, is taken to be the same act.
 */
#define EX_PLACES (PL_SIZE * PL_SIZE * PL_COLOURS)
#define EX_FLAG_PREDICTED 4u   /* not spent: speculated, through the gate, to do nothing */

/*
 * What pointing at a place does, by elimination. Pointing at the same place,
 * with the same colour there, is one act. What it does in each situation it is
 * met in is unknown: one bit per situation. The first EX_PLACE_BITS situations
 * are the coordinates of a domain of 2^EX_PLACE_BITS possible answers. Each
 * situation where it changed nothing rules out every answer in which it changed
 * something there. Once it has changed something anywhere, "it does nothing"
 * is ruled out for good. Not spending it in a new situation is a speculation,
 * and speculation is licensed only by the gate: support (the fraction ruled
 * out) at or above EX_SKIP_TAU. That is a policy, named, not a count.
 */
#define EX_PLACE_BITS 4u
#define EX_SKIP_TAU SELF_SKIP_TAU
static sm_possibility_t PLACE_LAW[EX_PLACES];
static unsigned char PLACE_SEEN[EX_PLACES];     /* situations in which it did nothing, as coordinates */
static unsigned char PLACE_DOES[EX_PLACES];     /* it has changed something: never skipped */
static unsigned PLACES_OPEN;                     /* this level, laws start again */

static unsigned place_of(unsigned code, const pl_frame_t *f) {
  unsigned x = code & 63u, y = (code >> 6) & 63u;
  return (y * PL_SIZE + x) * PL_COLOURS + f->c[y][x];
}

static void place_nothing(unsigned pl) {
  unsigned j = PLACE_SEEN[pl], v;
  if (PLACE_LAW[pl].has_domain == 0) (void)sm_init(&PLACE_LAW[pl], 1u << EX_PLACE_BITS);
  if (j >= EX_PLACE_BITS) return;
  for (v = 0u; v < (1u << EX_PLACE_BITS); v++) {
    if (v & (1u << j)) (void)sm_eliminate(&PLACE_LAW[pl], v);   /* answers where it did something here */
  }
  PLACE_SEEN[pl] = (unsigned char)(j + 1u);
}

static int place_skip(unsigned pl) {
  if (PLACE_DOES[pl] || PLACE_LAW[pl].has_domain == 0) return 0;
  return sm_support(&PLACE_LAW[pl]) >= EX_SKIP_TAU;
}

static void forget_places(void) {
  memset(PLACE_LAW, 0, sizeof PLACE_LAW);
  memset(PLACE_SEEN, 0, sizeof PLACE_SEEN);
  memset(PLACE_DOES, 0, sizeof PLACE_DOES);
}

/*
 * Which kinds are still open. A kind is a plain action, or a thing of one
 * colour and size to point at. Its question, does it change anything, holds a
 * bit until it is first done; doing it answers for the kind. The unknowns that
 * matter most are situations: an act that changes something leads to a new
 * one, and every new situation is a new domain of unknowns. So kinds that have
 * changed something are tried first, then kinds not yet done, then kinds that
 * have only ever changed nothing.
 */
#define EX_KINDS 4096u
static uint32_t KIND_KEY[EX_KINDS];
static unsigned char KIND_DONE[EX_KINDS], KIND_DID[EX_KINDS];
static uint32_t N_AKEY[EX_MAX_NODES][EX_MAX_ACTS];

static unsigned TIE_MODE;   /* CIALL_TIE: how a tie is made uniform (see where the choice is made) */
static uint64_t TIE_SEED;   /* which uniform choices are made: CIALL_SEED, so a run can be replayed */
static unsigned kind_slot(uint32_t key);
/* how open an act is: 0 its kind has changed something, 1 not yet done, 2 only ever nothing */
static unsigned act_rank(int node, unsigned i) {
  unsigned k = kind_slot(N_AKEY[node][i]);
  return KIND_DID[k] ? 0u : (!KIND_DONE[k] ? 1u : 2u);
}

static unsigned kind_slot(uint32_t key) {
  unsigned i, slot = (key * 2654435761u) & (EX_KINDS - 1u);
  for (i = 0u; i < EX_KINDS; i++) {
    unsigned at = (slot + i) & (EX_KINDS - 1u);
    if (KIND_KEY[at] == key) return at;
    if (KIND_KEY[at] == 0u) {
      KIND_KEY[at] = key;
      return at;
    }
  }
  return 0u;
}
static int TBL[EX_TBL];

static unsigned char MASK[PL_SIZE][PL_SIZE];   /* cells that tick by themselves */
static unsigned WATCHING;   /* it is confused, and watching rather than getting on */
static unsigned short LAST_CODE;   /* what it did last, so it can do it again */
typedef enum { Q_STILL = 0, Q_AGAIN = 1, Q_AT = 2, Q_AWAY = 3, Q_WAYS = 4 } ex_ask_t;
static ex_ask_t ASK_NOW;          /* the way it is asking about what it cannot explain */
static unsigned ASK_R, ASK_C;     /* where the thing it is asking about is */
static pl_frame_t LEVEL_START;

/* the small changes seen, to tell a clock from a switch */
static unsigned PAIR_KINDS[PL_COLOURS][PL_COLOURS];   /* bit per action kind it came under */
static int PAIR_FIRST[PL_COLOURS][PL_COLOURS];        /* first cell it was seen at, -1 none */
static unsigned char PAIR_MOVED[PL_COLOURS][PL_COLOURS];
static unsigned char PAIR_CLOCK[PL_COLOURS][PL_COLOURS];
/* when each change was last seen, and how many steps running it has come,
   and under which actions in that run */
static unsigned PAIR_LAST[PL_COLOURS][PL_COLOURS];
static unsigned PAIR_RUN[PL_COLOURS][PL_COLOURS];
static unsigned PAIR_RUN_CODES[PL_COLOURS][PL_COLOURS];
static unsigned TICK_STEP;
#define EX_TICK_RUN 3u   /* steps running, under different actions, before it is a clock */

#define CODE(kind, x, y) ((unsigned short)(((kind) << 12) | ((y) << 6) | (x)))
#define KIND(code) ((unsigned)((code) >> 12))
#define PX(code) ((unsigned)((code) & 63u))
#define PY(code) ((unsigned)(((code) >> 6) & 63u))

static void think(ex_explorer_t *ex, const char *question, const char *answer) {
  if (ex->thinking == 0 || ex->thoughts >= EX_THOUGHTS) return;
  fprintf(ex->thinking, "    Q: %s%s    A: %s\n", question, "\n", answer);
  ex->thoughts++;
}

void ex_init(ex_explorer_t *ex) {
  if (ex == 0) return;
  memset(ex, 0, sizeof(*ex));
}

static uint64_t hash_frame(const pl_frame_t *f) {
  uint64_t h = 1469598103934665603ULL ^ ((uint64_t)f->h * 131u + (uint64_t)f->w);
  unsigned r, c;
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      unsigned v = MASK[r][c] ? 16u : f->c[r][c];
      h ^= (uint64_t)(v + 1u);
      h *= 1099511628211ULL;
    }
  }
  return h;
}

/* ---- what it moves, and where it has stood ------------------------------------- */

/*
 * Every colour's cells are summed: how many, and where on the whole. A colour
 * whose cells keep their number while their place shifts, step after step, is
 * something being moved. The one moved most often is what the child moves. Each
 * action's shift of it is learned from what happened, never given.
 */
typedef struct {
  long rows, cols;
  unsigned count;
} ex_sum_t;
static unsigned MOVED_VOTES[PL_COLOURS];
static long SHIFT_R[8][PL_COLOURS], SHIFT_C[8][PL_COLOURS];
static unsigned SHIFT_SEEN[8][PL_COLOURS];   /* times running this shift was seen */
#define EX_BODY_VOTES SELF_BODY_VOTES
#define EX_SHIFT_SURE SELF_SHIFT_SURE
#define EX_STOOD 8192u
static uint64_t STOOD[EX_STOOD];   /* places stood in, with the rest of the picture as it was */
static unsigned STOOD_OFF;   /* this level, where it stood no longer holds it back */

static void colour_sums(const pl_frame_t *f, ex_sum_t *sum) {
  unsigned r, c;
  memset(sum, 0, sizeof(ex_sum_t) * PL_COLOURS);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      unsigned v;
      if (MASK[r][c]) continue;
      v = f->c[r][c];
      sum[v].rows += (long)r;
      sum[v].cols += (long)c;
      sum[v].count++;
    }
  }
}

static int body_colour(void) {
  unsigned v, best = 0u;
  int who = -1;
  for (v = 0u; v < PL_COLOURS; v++) {
    if (MOVED_VOTES[v] >= EX_BODY_VOTES && MOVED_VOTES[v] > best) {
      best = MOVED_VOTES[v];
      who = (int)v;
    }
  }
  return who;
}

/* where the body stands, and how many cells of every other colour there are */
static uint64_t stood_key(const ex_sum_t *sum, int body, long dr, long dc) {
  uint64_t h = 88172645463325252ULL;
  unsigned v;
  for (v = 0u; v < PL_COLOURS; v++) {
    uint64_t x;
    if ((int)v == body) {
      x = ((uint64_t)(sum[v].rows + dr) << 24) ^ (uint64_t)(sum[v].cols + dc) ^ ((uint64_t)v << 56);
    } else {
      x = ((uint64_t)sum[v].count << 8) | v;
    }
    h ^= x + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
  }
  return h | 1u;
}

static int STOOD_NODE[EX_STOOD];   /* the situation it stood in there */

/* the situation where it stood so, or -1 */
static int stood_has(uint64_t k) {
  unsigned i, slot = (unsigned)(k & (EX_STOOD - 1u));
  for (i = 0u; i < EX_STOOD; i++) {
    unsigned at = (slot + i) & (EX_STOOD - 1u);
    if (STOOD[at] == 0u) return -1;
    if (STOOD[at] == k) return STOOD_NODE[at];
  }
  return -1;
}

static void stood_add(uint64_t k, int node) {
  unsigned i, slot = (unsigned)(k & (EX_STOOD - 1u));
  for (i = 0u; i < EX_STOOD / 2u; i++) {
    unsigned at = (slot + i) & (EX_STOOD - 1u);
    if (STOOD[at] == k) return;
    if (STOOD[at] == 0u) {
      STOOD[at] = k;
      STOOD_NODE[at] = node;
      return;
    }
  }
}

static unsigned stepped_onto(const pl_frame_t *f, const ex_sum_t *sum, int body, unsigned kind,
                             unsigned *onto);
static unsigned MOVE_ONTO[16];
static unsigned MOVES_ONTO;
static unsigned STEPPED_MASK;               /* colours the last stepped_onto saw ahead of the body */
static unsigned STEPPED_CELLS[PL_COLOURS];  /* and how many cells of each */
static unsigned BODY_STEP = 1u;
/* one step seen: which colours moved, and by how much under this action */
static unsigned char BLOCKER[PL_COLOURS];
static void learn_moving(unsigned kind, const pl_frame_t *a, const pl_frame_t *b) {
  static ex_sum_t sa[PL_COLOURS], sb[PL_COLOURS];
  unsigned v;
  if (kind >= 8u || kind == 6u) return;
  colour_sums(a, sa);
  colour_sums(b, sb);
  {
    int body = body_colour();
    if (body >= 0 && sa[body].count == sb[body].count &&
        (sa[body].rows != sb[body].rows || sa[body].cols != sb[body].cols)) {
      if (stepped_onto(a, sa, body, kind, MOVE_ONTO)) MOVES_ONTO++;
    } else if (body >= 0 && sa[body].count == sb[body].count && SHIFT_SEEN[kind][body] >= EX_SHIFT_SURE) {
      static unsigned refused[PL_COLOURS];
      if (stepped_onto(a, sa, body, kind, refused)) {
        unsigned v2, n2 = 0u, who = 0u;
        for (v2 = 0u; v2 < PL_COLOURS; v2++) {
          if (STEPPED_MASK & (1u << v2)) { n2++; who = v2; }
        }
        if (n2 == 1u) BLOCKER[who] = 1u;   /* only that lay ahead, and it would not be entered */
      }
    }
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    long dr = sb[v].rows - sa[v].rows, dc = sb[v].cols - sa[v].cols;
    if (sa[v].count == 0u || sa[v].count != sb[v].count || (dr == 0 && dc == 0)) continue;
    MOVED_VOTES[v]++;
    if ((int)v == body_colour()) {
      unsigned ar = (unsigned)labs(dr) / sa[v].count, ac = (unsigned)labs(dc) / sa[v].count;
      unsigned st = ar > ac ? ar : ac;
      if (st > 0u && st < 16u) BODY_STEP = st;
    }
    if (SHIFT_SEEN[kind][v] > 0u && SHIFT_R[kind][v] == dr && SHIFT_C[kind][v] == dc) {
      SHIFT_SEEN[kind][v]++;
    } else {
      SHIFT_R[kind][v] = dr;
      SHIFT_C[kind][v] = dc;
      SHIFT_SEEN[kind][v] = 1u;
    }
  }
}

/*
 * What it walks into. Each move, the colours the body steps onto are counted;
 * so are the colours it stepped onto when a level ended. A colour stepped onto
 * far more often at an ending than on ordinary moves is what it goes to: the
 * goal, found from what happened, never named.
 */
static unsigned WIN_ONTO[PL_COLOURS];
static unsigned WINS_ONTO;
static unsigned short N_GOALD[EX_MAX_NODES];
static unsigned short N_GONEC[EX_MAX_NODES];   /* cells still there of colours an ending wants gone */
#define EX_GOAL_UNKNOWN 0xFFFFu
#define EX_GOAL_LIFT 2.0

static unsigned stepped_onto(const pl_frame_t *f, const ex_sum_t *sum, int body, unsigned kind,
                             unsigned *onto) {
  long dr, dc;
  unsigned r, c, v, any = 0u, n = sum[body].count;
  unsigned seen[PL_COLOURS];
  if (n == 0u || kind >= 8u || SHIFT_SEEN[kind][body] == 0u) return 0u;
  if (SHIFT_R[kind][body] % (long)n != 0 || SHIFT_C[kind][body] % (long)n != 0) return 0u;
  dr = SHIFT_R[kind][body] / (long)n;
  dc = SHIFT_C[kind][body] / (long)n;
  memset(seen, 0, sizeof seen);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      long tr = (long)r + dr, tc = (long)c + dc;
      if (MASK[r][c] || (int)f->c[r][c] != body) continue;
      if (tr < 0 || tc < 0 || tr >= (long)f->h || tc >= (long)f->w) continue;
      v = f->c[tr][tc];
      if ((int)v == body || MASK[tr][tc]) continue;
      if (!seen[v]) STEPPED_CELLS[v] = 0u;
      seen[v] = 1u;
      STEPPED_CELLS[v]++;
    }
  }
  STEPPED_MASK = 0u;
  for (v = 0u; v < PL_COLOURS; v++) {
    if (seen[v]) {
      onto[v]++;
      any = 1u;
      STEPPED_MASK |= 1u << v;
    }
  }
  return any;
}

/*
 * What to go towards comes from the theories of what ends a level
 * (smarsh_ending): formulated by elimination over every act, in a language the
 * child widens itself when every theory it can say is ruled out. Colours the body
 * was refused entry to are not places to go.
 */
static en_theory_t THEORY;
static en_obs_t OBS;
static uint16_t AIM_ONTO, AIM_GONE, AIM_POINT;
static int AIM_MATCH;

/*
 * Which colours hold which role, here and now. A role is what a thing does, not
 * which colour it is, so a theory said in roles can be true of a game it has
 * never seen. Nothing here is given: the body is what it found itself to be, a
 * blocker is what refused it entry, what is taken is what there is less of than
 * when the level began.
 */
static uint16_t ROLE_OF[PL_COLOURS];

static void work_out_roles(const pl_frame_t *f) {
  static ex_sum_t now_s[PL_COLOURS], start_s[PL_COLOURS];
  int body = body_colour();
  unsigned v, ground = 0u, bulk = 0u, rare = 0u;
  colour_sums(f, now_s);
  colour_sums(&LEVEL_START, start_s);
  memset(ROLE_OF, 0, sizeof ROLE_OF);
  for (v = 1u; v < PL_COLOURS; v++) {
    if (start_s[v].count > start_s[ground].count) ground = v;
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    if (v != ground && start_s[v].count > start_s[bulk].count) bulk = v;
    if (start_s[v].count > 0u && (start_s[rare].count == 0u || start_s[v].count < start_s[rare].count)) rare = v;
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    if ((int)v == body) ROLE_OF[v] |= 1u << EN_R_BODY;
    if (v == ground) ROLE_OF[v] |= 1u << EN_R_GROUND;
    if (v == bulk && start_s[v].count > 0u) ROLE_OF[v] |= 1u << EN_R_BULK;
    if (v == rare && start_s[v].count > 0u) ROLE_OF[v] |= 1u << EN_R_RARE;
    if (BLOCKER[v]) ROLE_OF[v] |= 1u << EN_R_BLOCK;
    if (start_s[v].count > 0u && now_s[v].count < start_s[v].count) ROLE_OF[v] |= 1u << EN_R_TAKEN;
    if (start_s[v].count > 0u && now_s[v].count == start_s[v].count && (int)v != body) {
      ROLE_OF[v] |= 1u << EN_R_STILL;
    }
    if (start_s[v].count == 0u && now_s[v].count > 0u) ROLE_OF[v] |= 1u << EN_R_NEW;
  }
}

/* the colours that hold any of these roles, here and now */
static unsigned colours_of_roles(uint16_t roles) {
  unsigned v, m = 0u;
  for (v = 0u; v < PL_COLOURS; v++) {
    if (ROLE_OF[v] & roles) m |= 1u << v;
  }
  return m;
}

static unsigned theories_left(void) {
  unsigned i, n = 0u;
  for (i = 0u; i < THEORY.n_fam; i++) n += sm_count(&THEORY.fam[i].dom);
  return n;
}

static void theory_aims(void) {
  uint16_t onto_r = 0u, gone_r = 0u, point_r = 0u;
  (void)en_aim(&THEORY, &AIM_ONTO, &AIM_GONE, &AIM_POINT, &AIM_MATCH, &onto_r, &gone_r, &point_r);
  /* what the theories say in roles becomes colours in the game it is in now */
  AIM_ONTO |= (uint16_t)colours_of_roles(onto_r);
  AIM_GONE |= (uint16_t)colours_of_roles(gone_r);
  AIM_POINT |= (uint16_t)colours_of_roles(point_r);
  {
    /*
     * An aim that takes in nearly everything there is rules nothing out, and
     * says nothing about where to go: following it would only cost it the
     * choice it would otherwise have made. So it is not an aim, and is dropped.
     */
    unsigned v, present = 0u, aimed = 0u;
    static ex_sum_t seen[PL_COLOURS];
    colour_sums(&LEVEL_START, seen);
    for (v = 0u; v < PL_COLOURS; v++) {
      if (seen[v].count == 0u) continue;
      present++;
      if (AIM_ONTO & (1u << v)) aimed++;
    }
    if (present > 0u && aimed * 2u > present) AIM_ONTO = 0u;
  }
  if (!ON(M_THEORY)) {
    AIM_ONTO = AIM_GONE = AIM_POINT = 0u;
    AIM_MATCH = 0;
  }
}

/*
 * Its ways of choosing what to do next, and whether each is getting it anywhere.
 * Each is a hypothesis about itself: "choosing this way gets me somewhere here",
 * over {it does, it does not}. An act chosen that way which brings nothing new --
 * no situation it had not met, no theory ruled out, no level ended -- is evidence
 * against it; EX_BARREN of them running rule it out, and it stops choosing that
 * way in this level. If every way is ruled out, that is a contradiction about
 * itself: they cannot all be hopeless, since it has to do something, so the
 * question was wrong and all of them are opened again.
 *
 * This is what it does to a game, done to itself: the answer to being stuck is to
 * give up the way of going, not to go that way harder.
 */
typedef enum { W_RECALL = 0, W_REPLAY = 1, W_AIM = 2, W_EXPLORE = 3, W_PLAN = 4, W_WAYS = 5 } ex_way_t;
static const char *WAY_NAME[W_WAYS] = {
  "walking the way I remember", "doing what won the last level", "going by what I think ends a level",
  "trying what I have not tried", "going by the way I reckoned over the board"
};
#define EX_BARREN 60u   /* acts chosen that way, bringing nothing new, before it is ruled out */
static unsigned WAY_BARREN[W_WAYS];
static unsigned char WAY_OUT[W_WAYS];
static ex_way_t WAY_NOW;

static void ways_begin(void) {
  memset(WAY_BARREN, 0, sizeof WAY_BARREN);
  memset(WAY_OUT, 0, sizeof WAY_OUT);
  WAY_NOW = W_EXPLORE;
}

/* what came of the act it just chose that way */
static int way_judge(ex_explorer_t *ex, int something_new) {
  if (something_new) {
    WAY_BARREN[WAY_NOW] = 0u;
    return 0;
  }
  WAY_BARREN[WAY_NOW]++;
  if (WAY_BARREN[WAY_NOW] < EX_BARREN || WAY_OUT[WAY_NOW]) return 0;
  WAY_OUT[WAY_NOW] = 1u;
  ex->ways_dropped++;
  {
    unsigned w, left = 0u;
    char a1[200];
    for (w = 0u; w < W_WAYS; w++) {
      if (!WAY_OUT[w]) left++;
    }
    sprintf(a1, "%s has brought me nothing for %u acts: I stop choosing that way here",
            WAY_NAME[WAY_NOW], (unsigned)EX_BARREN);
    think(ex, "is the way I am going getting me anywhere?", a1);
    if (left == 0u) {
      /* they cannot all be hopeless: it has to do something, so the question was wrong */
      memset(WAY_OUT, 0, sizeof WAY_OUT);
      memset(WAY_BARREN, 0, sizeof WAY_BARREN);
      ex->ways_reopened++;
      think(ex, "have I really no way of going at all?",
            "no: I must do something, so that cannot be right. Every way is open again");
    }
  }
  return 1;
}

/*
 * Where a thing may be sent. In some worlds pointing at a thing takes hold of it
 * and pointing again says where it goes -- a piece on a board, an end of a line
 * to be dragged. Whether it goes depends on what kind of thing it is: each kind
 * has its own reach, and nothing here says what that reach is.
 *
 * So for each colour and each distance, one question: can a thing of that colour
 * be sent that far? A pointing that moved it rules out "no"; a pointing that did
 * nothing rules out "yes". Distances never asked about are where the bits are, so
 * they are asked first; distances ruled out are not spent again.
 */
#define EX_REACH 15u          /* -7..7 in each direction */
#define EX_REACH_MID 7u
static unsigned char SENT_LAW[PL_COLOURS][EX_REACH][EX_REACH];   /* 0 unknown, 1 it goes, 2 it does not */
static unsigned HOLD_X, HOLD_Y, HOLD_COLOUR;
static int HOLDING;           /* the last act was a pointing that changed something */

static unsigned char *sent_law(unsigned colour, int dy, int dx) {
  if (dy < -7 || dy > 7 || dx < -7 || dx > 7 || colour >= PL_COLOURS) return 0;
  return &SENT_LAW[colour][(unsigned)(dy + 7)][(unsigned)(dx + 7)];
}


static unsigned aim_mask(void) {
  int body = body_colour();
  unsigned m = AIM_ONTO, v;
  if (body < 0) return 0u;
  m &= ~(1u << body);
  for (v = 0u; v < PL_COLOURS; v++) {
    if (BLOCKER[v]) m &= ~(1u << v);
  }
  return m;
}

/* how many body steps from the body, moved by (dr, dc) in sums, to the nearest goal cell */
static unsigned goal_distance(const pl_frame_t *f, const ex_sum_t *sum, int body, long dr, long dc) {
  unsigned aim = aim_mask();
  unsigned r, c, best = EX_GOAL_UNKNOWN;
  double cr, cc;
  if (aim == 0u || body < 0 || sum[body].count == 0u) return EX_GOAL_UNKNOWN;
  cr = (double)(sum[body].rows + dr) / (double)sum[body].count;
  cc = (double)(sum[body].cols + dc) / (double)sum[body].count;
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      double d;
      unsigned steps;
      if (MASK[r][c] || !(aim & (1u << f->c[r][c]))) continue;
      d = fabs((double)r - cr) + fabs((double)c - cc);
      steps = (unsigned)(d / (double)BODY_STEP + 0.5);
      if (steps < best) best = steps;
    }
  }
  return best;
}

/* ---- things that look alike ------------------------------------------------------ */

/*
 * A panel is a patch of one colour with something drawn inside it. What is
 * drawn is read as a pattern, shrunk to its smallest scale, so the same picture
 * drawn large and drawn small reads the same. Two panels whose patterns have
 * the same size can be compared: how many places differ. A situation where two
 * such panels differ in fewer places is nearer to making them alike, and making
 * things alike is worth going towards. Nothing says which panels matter.
 */
#define EX_PANELS 32u
#define EX_PAT 8u
#define EX_MATCH_NONE 255u
#define EX_MATCH_WEIGHT SELF_MATCH_WEIGHT
typedef struct {
  unsigned h, w;
  unsigned char bits[EX_PAT][EX_PAT];
} ex_panel_t;
static unsigned short PANEL_LABEL[PL_SIZE][PL_SIZE];
static unsigned char N_MATCHD[EX_MAX_NODES];
static unsigned MATCH_VARIES;   /* this level, situations differ in how alike their panels are */

static unsigned read_panels(const pl_frame_t *f, ex_panel_t *out) {
  unsigned r, c, n = 0u, label = 0u;
  static int qr[PL_SIZE * PL_SIZE], qc[PL_SIZE * PL_SIZE];
  memset(PANEL_LABEL, 0, sizeof PANEL_LABEL);
  for (r = 0u; r < f->h && n < EX_PANELS; r++) {
    for (c = 0u; c < f->w && n < EX_PANELS; c++) {
      unsigned head = 0u, tail = 0u, colour = f->c[r][c];
      int r0 = (int)r, r1 = (int)r, c0 = (int)c, c1 = (int)c, ir0, ir1, ic0, ic1, rr, cc;
      if (PANEL_LABEL[r][c] || MASK[r][c]) continue;
      label++;
      PANEL_LABEL[r][c] = (unsigned short)label;
      qr[tail] = (int)r;
      qc[tail] = (int)c;
      tail++;
      while (head < tail) {
        static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
        int k, y = qr[head], x = qc[head];
        head++;
        if (y < r0) r0 = y;
        if (y > r1) r1 = y;
        if (x < c0) c0 = x;
        if (x > c1) c1 = x;
        for (k = 0; k < 4; k++) {
          int ny = y + dr[k], nx = x + dc[k];
          if (ny < 0 || nx < 0 || ny >= (int)f->h || nx >= (int)f->w) continue;
          if (PANEL_LABEL[ny][nx] || f->c[ny][nx] != colour || MASK[ny][nx]) continue;
          PANEL_LABEL[ny][nx] = (unsigned short)label;
          qr[tail] = ny;
          qc[tail] = nx;
          tail++;
        }
      }
      if ((r1 - r0 + 1) * (c1 - c0 + 1) > 1024 || r1 - r0 < 3 || c1 - c0 < 3) continue;
      /* what is drawn inside: cells of the box that are not the panel's colour */
      ir0 = r1; ir1 = r0; ic0 = c1; ic1 = c0;
      for (rr = r0; rr <= r1; rr++) {
        for (cc = c0; cc <= c1; cc++) {
          if (f->c[rr][cc] == colour) continue;
          if (rr < ir0) ir0 = rr;
          if (rr > ir1) ir1 = rr;
          if (cc < ic0) ic0 = cc;
          if (cc > ic1) ic1 = cc;
        }
      }
      if (ir0 > ir1 || ir0 <= r0 || ir1 >= r1 || ic0 <= c0 || ic1 >= c1) continue;   /* nothing inside */
      {
        unsigned ph = (unsigned)(ir1 - ir0 + 1), pw = (unsigned)(ic1 - ic0 + 1), scale, sc2, ok;
        /* the smallest scale it is drawn at: the largest block size it is made of */
        for (scale = 8u; scale >= 1u; scale--) {
          if (ph % scale != 0u || pw % scale != 0u) continue;
          if (ph / scale > EX_PAT || pw / scale > EX_PAT) break;
          ok = 1u;
          for (rr = 0; rr < (int)ph && ok; rr++) {
            for (cc = 0; cc < (int)pw && ok; cc++) {
              int a = f->c[ir0 + rr][ic0 + cc] != colour;
              int b = f->c[ir0 + rr - rr % (int)scale][ic0 + cc - cc % (int)scale] != colour;
              if (a != b) ok = 0u;
            }
          }
          if (ok) break;
        }
        if (scale == 0u || ph / scale > EX_PAT || pw / scale > EX_PAT) continue;
        sc2 = scale;
        out[n].h = ph / sc2;
        out[n].w = pw / sc2;
        if (out[n].h < 2u || out[n].w < 2u) continue;
        {
          unsigned y, x, filled = 0u;
          for (y = 0u; y < out[n].h; y++) {
            for (x = 0u; x < out[n].w; x++) {
              out[n].bits[y][x] = f->c[ir0 + (int)(y * sc2)][ic0 + (int)(x * sc2)] != colour;
              filled += out[n].bits[y][x];
            }
          }
          if (filled == out[n].h * out[n].w) continue;   /* a solid block is not a picture */
        }
        n++;
      }
    }
  }
  return n;
}

/* the fewest places in which two panels of the same size differ; EX_MATCH_NONE if none compare */
static unsigned match_distance(const pl_frame_t *f) {
  static ex_panel_t pan[EX_PANELS];
  unsigned n = read_panels(f, pan), i, j, best = EX_MATCH_NONE;
  for (i = 0u; i < n; i++) {
    for (j = i + 1u; j < n; j++) {
      unsigned y, x, d = 0u;
      if (pan[i].h != pan[j].h || pan[i].w != pan[j].w) continue;
      for (y = 0u; y < pan[i].h; y++) {
        for (x = 0u; x < pan[i].w; x++) d += pan[i].bits[y][x] != pan[j].bits[y][x];
      }
      if (d < best) best = d;
    }
  }
  return best;
}

/*
 * Like goes with like. Small things of one colour and of about one size, set
 * apart from each other, may belong together: a piece and the outline it fits,
 * a key and its lock. For each colour, how far apart its nearest such pair is;
 * added over colours. A situation where these are nearer is worth going to.
 */
#define EX_NEAR_THINGS 128u
#define EX_NEAR_NONE 0xFFFFu
#define EX_NEAR_WEIGHT 0.25   /* steps it will go for each cell nearer */
static unsigned short N_NEARD[EX_MAX_NODES];
static unsigned NEAR_VARIES;

static unsigned near_distance(const pl_frame_t *f) {
  static struct { unsigned colour, h, w, cells; int cr, cc; } th[EX_NEAR_THINGS];
  static int qr[PL_SIZE * PL_SIZE], qc[PL_SIZE * PL_SIZE];
  static unsigned char seen[PL_SIZE][PL_SIZE];
  unsigned count[PL_COLOURS], best[PL_COLOURS], r, c, n = 0u, back = 0u, i, j, total = 0u, any = 0u;
  memset(count, 0, sizeof count);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) count[f->c[r][c]]++;
  }
  for (i = 1u; i < PL_COLOURS; i++) {
    if (count[i] > count[back]) back = i;
  }
  memset(seen, 0, sizeof seen);
  for (r = 0u; r < f->h && n < EX_NEAR_THINGS; r++) {
    for (c = 0u; c < f->w && n < EX_NEAR_THINGS; c++) {
      unsigned head = 0u, tail = 0u, colour = f->c[r][c];
      unsigned r0 = r, r1 = r, c0 = c, c1 = c;
      if (seen[r][c] || MASK[r][c] || colour == back) continue;
      seen[r][c] = 1u;
      qr[tail] = (int)r;
      qc[tail] = (int)c;
      tail++;
      while (head < tail) {
        int y = qr[head], x = qc[head], dy, dx;
        head++;
        if ((unsigned)y < r0) r0 = (unsigned)y;
        if ((unsigned)y > r1) r1 = (unsigned)y;
        if ((unsigned)x < c0) c0 = (unsigned)x;
        if ((unsigned)x > c1) c1 = (unsigned)x;
        for (dy = -1; dy <= 1; dy++) {
          for (dx = -1; dx <= 1; dx++) {
            int ny = y + dy, nx = x + dx;
            if (ny < 0 || nx < 0 || ny >= (int)f->h || nx >= (int)f->w) continue;
            if (seen[ny][nx] || f->c[ny][nx] != colour || MASK[ny][nx]) continue;
            seen[ny][nx] = 1u;
            qr[tail] = ny;
            qc[tail] = nx;
            tail++;
          }
        }
      }
      if (r1 - r0 + 1u > 12u || c1 - c0 + 1u > 12u || r1 - r0 + 1u < 3u || c1 - c0 + 1u < 3u) continue;
      th[n].colour = colour;
      th[n].h = r1 - r0 + 1u;
      th[n].w = c1 - c0 + 1u;
      th[n].cells = tail;
      th[n].cr = (int)(r0 + r1);
      th[n].cc = (int)(c0 + c1);
      n++;
    }
  }
  for (i = 0u; i < PL_COLOURS; i++) best[i] = EX_NEAR_NONE;
  for (i = 0u; i < n; i++) {
    for (j = i + 1u; j < n; j++) {
      unsigned d;
      if (th[i].colour != th[j].colour) continue;
      if (th[i].h * 2u < th[j].h || th[j].h * 2u < th[i].h) continue;
      if (th[i].w * 2u < th[j].w || th[j].w * 2u < th[i].w) continue;
      d = (unsigned)(abs(th[i].cr - th[j].cr) + abs(th[i].cc - th[j].cc)) / 2u;
      if (d < best[th[i].colour]) best[th[i].colour] = d;
    }
  }
  for (i = 0u; i < PL_COLOURS; i++) {
    if (best[i] != EX_NEAR_NONE) {
      total += best[i];
      any = 1u;
    }
  }
  if (!any) return EX_NEAR_NONE;
  return total < EX_NEAR_NONE ? total : EX_NEAR_NONE - 1u;
}

static void forget_level(void) {
  N_COUNT = 0u;
  memset(TBL, 0, sizeof TBL);
  memset(STOOD, 0, sizeof STOOD);
  MATCH_VARIES = 0u;
  NEAR_VARIES = 0u;
}

/* ---- what it can do in a situation -------------------------------------------- */

static unsigned short LABEL[PL_SIZE][PL_SIZE];
static int QR[PL_SIZE * PL_SIZE], QC[PL_SIZE * PL_SIZE];

typedef struct {
  unsigned short code;
  unsigned size;
  double score;
  int simple;
  uint32_t key;
} ex_cand_t;

/* how likely this is to do nothing, from what it has seen: tried last when likely */
static uint32_t BUILT_KEY[EX_MAX_ACTS];
#define EX_KEEP_ACTS SELF_KEEP_ACTS
static unsigned ACT_WINDOW;       /* how far along the window of further acts is, this level */
static unsigned BUILT_OVERFLOW;   /* some situation had more acts than it held */
static unsigned build_actions(const ex_explorer_t *ex, const ex_game_t *g, const pl_frame_t *f,
                              unsigned short *acts) {
  (void)ex;
  static ex_cand_t cand[PL_SIZE * PL_SIZE + 8u];
  unsigned n = 0u, i, j, can_point = 0u, count[PL_COLOURS], background = 0u, r, c;

  for (i = 0u; i < g->n_available; i++) {
    unsigned a = g->available[i];
    if (a == 6u) {
      can_point = 1u;
      continue;
    }
    if (a == 0u || a > 7u) continue;
    cand[n].code = CODE(a, 0u, 0u);
    cand[n].size = 0u;
    cand[n].simple = 1;
    cand[n].key = (1u << 29) | a;
    cand[n].score = 0.0;
    n++;
  }

  if (can_point) {
    /* one place in each patch of one colour, leaving out the ground and what ticks */
    for (i = 0u; i < PL_COLOURS; i++) count[i] = 0u;
    for (r = 0u; r < f->h; r++) {
      for (c = 0u; c < f->w; c++) {
        if (!MASK[r][c]) count[f->c[r][c]]++;
      }
    }
    for (i = 1u; i < PL_COLOURS; i++) {
      if (count[i] > count[background]) background = i;
    }
    memset(LABEL, 0, sizeof LABEL);
    for (r = 0u; r < f->h; r++) {
      for (c = 0u; c < f->w; c++) {
        unsigned colour, head = 0u, tail = 0u, size = 0u, best = 0u;
        int pr0 = (int)r, pr1 = (int)r, pc0 = (int)c, pc1 = (int)c;
        long sr = 0, sc = 0;
        double best_d = 1e30, cr, cc;
        if (LABEL[r][c] || MASK[r][c] || f->c[r][c] == background) continue;
        colour = f->c[r][c];
        LABEL[r][c] = 1u;
        QR[tail] = (int)r;
        QC[tail] = (int)c;
        tail++;
        while (head < tail) {
          int rr = QR[head], ccc = QC[head], k;
          static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
          head++;
          size++;
          sr += rr;
          sc += ccc;
          if (rr < pr0) pr0 = rr;
          if (rr > pr1) pr1 = rr;
          if (ccc < pc0) pc0 = ccc;
          if (ccc > pc1) pc1 = ccc;
          for (k = 0; k < 4; k++) {
            int nr = rr + dr[k], nc = ccc + dc[k];
            if (nr < 0 || nc < 0 || nr >= (int)f->h || nc >= (int)f->w) continue;
            if (LABEL[nr][nc] || MASK[nr][nc] || f->c[nr][nc] != colour) continue;
            LABEL[nr][nc] = 1u;
            QR[tail] = nr;
            QC[tail] = nc;
            tail++;
          }
        }
        cr = (double)sr / (double)size;
        cc = (double)sc / (double)size;
        for (j = 0u; j < tail; j++) {
          double d = (QR[j] - cr) * (QR[j] - cr) + (QC[j] - cc) * (QC[j] - cc);
          if (d < best_d) {
            best_d = d;
            best = j;
          }
        }
        if (n < PL_SIZE * PL_SIZE) {
          cand[n].code = CODE(6u, (unsigned)QC[best], (unsigned)QR[best]);
          cand[n].size = size;
          cand[n].simple = 0;
          cand[n].key = 1u + colour + ((uint32_t)((pr1 - pr0) & 63) << 4) + ((uint32_t)((pc1 - pc0) & 63) << 10);
          cand[n].score = 0.0;
          n++;
        }
      }
    }
  }

  /*
   * Least likely to do nothing first. Among equals: plain actions, then things
   * the size of something one could press (not a speck of detail, and not a
   * great field that is more ground than thing), then the rest.
   */
  for (i = 0u; i < n; i++) {
    if (!cand[i].simple) {
      unsigned sz = cand[i].size, area = f->h * f->w;
      cand[i].size = (sz >= 9u && sz * 8u <= area) ? 0u : (sz < 9u ? 1u + (9u - sz) : 16u);
    }
    {
      unsigned k = kind_slot(cand[i].key);
      /* what opens new situations first: each new situation is a new domain of unknowns */
      cand[i].score = KIND_DID[k] ? 0.0 : (!KIND_DONE[k] ? 1.0 : 2.0);
      if (!cand[i].simple && (AIM_POINT & (1u << f->c[PY(cand[i].code)][PX(cand[i].code)]))) cand[i].score = -1.0;
      if (WATCHING) {
        /* confused: it acts the way it has chosen to ask */
        int near_it = !cand[i].simple &&
                      abs((int)PY(cand[i].code) - (int)ASK_R) + abs((int)PX(cand[i].code) - (int)ASK_C) <= 6;
        if (ASK_NOW == Q_STILL && KIND_DONE[k] && !KIND_DID[k]) cand[i].score = -3.0;
        else if (ASK_NOW == Q_AGAIN && cand[i].code == LAST_CODE) cand[i].score = -3.0;
        else if (ASK_NOW == Q_AT && near_it) cand[i].score = -3.0;
        else if (ASK_NOW == Q_AWAY && !cand[i].simple && !near_it) cand[i].score = -3.0;
      }
      if (!cand[i].simple && HOLDING && ON(M_REACH)) {
        /* holding something: a distance it has never asked about is where the bit
           is, one it knows the thing goes is worth using, one ruled out is not
           spent again */
        const unsigned char *law = sent_law(HOLD_COLOUR, (int)PY(cand[i].code) - (int)HOLD_Y,
                                            (int)PX(cand[i].code) - (int)HOLD_X);
        if (law != 0) cand[i].score = *law == 0u ? -2.0 : (*law == 1u ? -1.5 : 4.0);
      }
    }
  }
  for (i = 1u; i < n; i++) {
    ex_cand_t t = cand[i];
    j = i;
    while (j > 0u && (cand[j - 1u].score > t.score ||
                      (cand[j - 1u].score == t.score &&
                       (cand[j - 1u].simple < t.simple ||
                        (cand[j - 1u].simple == t.simple && cand[j - 1u].size > t.size))))) {
      cand[j] = cand[j - 1u];
      j--;
    }
    cand[j] = t;
  }
  /*
   * It can hold EX_MAX_ACTS acts to a situation. When there are more, it keeps
   * the first EX_KEEP_ACTS in order and a window further along; moving the
   * window is how "have I tried everything?" is answered honestly once what it
   * held has run out.
   */
  if (n > EX_MAX_ACTS) {
    unsigned out = 0u, start = EX_KEEP_ACTS + ACT_WINDOW;
    BUILT_OVERFLOW = start + (EX_MAX_ACTS - EX_KEEP_ACTS) < n;
    for (i = 0u; i < n && out < EX_MAX_ACTS; i++) {
      if (i < EX_KEEP_ACTS || cand[i].simple || i >= start) {
        acts[out] = cand[i].code;
        BUILT_KEY[out] = cand[i].key;
        out++;
      }
    }
    return out;
  }
  for (i = 0u; i < n; i++) {
    acts[i] = cand[i].code;
    BUILT_KEY[i] = cand[i].key;
  }
  return n;
}

/*
 * Which thing of its colour is at (x, y): the things of that colour counted
 * across and down, by the first cell of each. The same count on a new level
 * picks out the thing in the same place in the order, wherever it now is.
 */
static unsigned char RANK_LABEL[PL_SIZE][PL_SIZE];

static unsigned thing_rank(const pl_frame_t *f, unsigned x, unsigned y) {
  unsigned r, c, rank = 0u, colour = f->c[y][x];
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  memset(RANK_LABEL, 0, sizeof RANK_LABEL);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      unsigned head = 0u, tail = 0u;
      int holds = 0;
      if (RANK_LABEL[r][c] || f->c[r][c] != colour) continue;
      RANK_LABEL[r][c] = 1u;
      QR[tail] = (int)r;
      QC[tail] = (int)c;
      tail++;
      while (head < tail) {
        int rr = QR[head], cc = QC[head], k;
        head++;
        if ((unsigned)rr == y && (unsigned)cc == x) holds = 1;
        for (k = 0; k < 4; k++) {
          int nr = rr + dr[k], nc = cc + dc[k];
          if (nr < 0 || nc < 0 || nr >= (int)f->h || nc >= (int)f->w) continue;
          if (RANK_LABEL[nr][nc] || f->c[nr][nc] != colour) continue;
          RANK_LABEL[nr][nc] = 1u;
          QR[tail] = nr;
          QC[tail] = nc;
          tail++;
        }
      }
      if (holds) return rank > 255u ? 255u : rank;
      rank++;
    }
  }
  return 255u;
}

/* The situation this frame is, remembered if it is new. -1 when memory is full. */
static int node_of(ex_explorer_t *ex, const ex_game_t *g, const pl_frame_t *f) {
  uint64_t h = hash_frame(f);
  unsigned slot = (unsigned)(h & (uint64_t)(EX_TBL - 1u)), i, id;
  for (i = 0u; i < EX_TBL; i++) {
    int e = TBL[(slot + i) & (EX_TBL - 1u)];
    if (e == 0) break;
    if (N_HASH[e - 1] == h) return e - 1;
  }
  if (N_COUNT >= EX_MAX_NODES) return EX_NONE;
  id = N_COUNT++;
  TBL[(slot + i) & (EX_TBL - 1u)] = (int)id + 1;
  N_HASH[id] = h;
  {
    unsigned r, c;
    memset(N_CHG[id], 0, sizeof N_CHG[id]);
    for (r = 0u; r < f->h; r++) {
      for (c = 0u; c < f->w; c++) {
        unsigned k;
        if (MASK[r][c] || f->c[r][c] == LEVEL_START.c[r][c]) continue;
        k = ((unsigned)LEVEL_START.c[r][c] << 4) | f->c[r][c];
        if (N_CHG[id][k] < 255u) N_CHG[id][k]++;
      }
    }
  }
  N_NACT[id] = (unsigned char)build_actions(ex, g, f, N_ACT[id]);
  N_UNTRIED[id] = N_NACT[id];
  N_MATCHD[id] = (unsigned char)match_distance(f);
  if (ON(M_PANELS) && id > 0u && N_MATCHD[id] != N_MATCHD[0]) MATCH_VARIES = 1u;
  N_NEARD[id] = (unsigned short)near_distance(f);
  if (ON(M_NEAR) && id > 0u && N_NEARD[id] != N_NEARD[0]) NEAR_VARIES = 1u;
  for (i = 0u; i < EX_MAX_ACTS; i++) {
    N_NEXT[id][i] = EX_NONE;
    N_FLAG[id][i] = 0u;
  }
  N_GOALD[id] = (unsigned short)EX_GOAL_UNKNOWN;
  N_GONEC[id] = 0u;
  if (AIM_GONE != 0u) {
    unsigned r2, c2;
    for (r2 = 0u; r2 < f->h; r2++) {
      for (c2 = 0u; c2 < f->w; c2++) {
        if (!MASK[r2][c2] && (AIM_GONE & (1u << f->c[r2][c2])) && N_GONEC[id] < 60000u) N_GONEC[id]++;
      }
    }
  }
  {
    int body = body_colour();
    if (body >= 0) {
      static ex_sum_t sum[PL_COLOURS];
      colour_sums(f, sum);
      N_GOALD[id] = (unsigned short)goal_distance(f, sum, body, 0, 0);
      stood_add(stood_key(sum, body, 0, 0), (int)id);
      for (i = 0u; i < N_NACT[id] && !STOOD_OFF && ON(M_STOOD); i++) {
        unsigned kind = KIND(N_ACT[id][i]);
        if (kind >= 8u || kind == 6u || N_FLAG[id][i] != 0u) continue;
        if (SHIFT_SEEN[kind][body] < EX_SHIFT_SURE) continue;
        int there = stood_has(stood_key(sum, body, SHIFT_R[kind][body], SHIFT_C[kind][body]));
        if (there >= 0) {
          /* this would take it back where it has stood, with all else as it was: not
             worth spending to see, but still a way there for getting about */
          N_NEXT[id][i] = (int)id;   /* where exactly it leads is not known: the rest may differ */
          N_FLAG[id][i] = EX_FLAG_PREDICTED;
          if (N_UNTRIED[id] > 0u) N_UNTRIED[id]--;
          ex->predicted++;
          ex->stood_back++;
        }
      }
    }
  }
  /* not spent: pointing at a place the gate licenses speculating does nothing */
  for (i = 0u; i < N_NACT[id]; i++) N_AKEY[id][i] = BUILT_KEY[i];
  for (i = 0u; i < N_NACT[id]; i++) {
    unsigned code = N_ACT[id][i], pl;
    if (KIND(code) != 6u || N_FLAG[id][i] != 0u || PLACES_OPEN) continue;
    pl = place_of(code, f);
    if (ON(M_SKIP) && place_skip(pl)) {
      N_NEXT[id][i] = (int)id;
      N_FLAG[id][i] = EX_FLAG_PREDICTED;
      if (N_UNTRIED[id] > 0u) N_UNTRIED[id]--;
      ex->predicted++;
    }
  }
  return (int)id;
}

/* ---- telling what ticks by itself ----------------------------------------------- */

static void forget_changes(void) {
  unsigned a, b;
  for (a = 0u; a < PL_COLOURS; a++) {
    for (b = 0u; b < PL_COLOURS; b++) {
      PAIR_KINDS[a][b] = 0u;
      PAIR_FIRST[a][b] = -1;
      PAIR_MOVED[a][b] = 0u;
      PAIR_CLOCK[a][b] = 0u;
      PAIR_LAST[a][b] = 0u;
      PAIR_RUN[a][b] = 0u;
      PAIR_RUN_CODES[a][b] = 0u;
    }
  }
}

/* leave out the patch of this colour that the cell belongs to, as it was when the level began */
static unsigned mask_patch(unsigned r0, unsigned c0, unsigned colour) {
  unsigned head = 0u, tail = 0u, added = 0u;
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  if (!MASK[r0][c0]) {
    MASK[r0][c0] = 1u;
    added++;
  }
  if (LEVEL_START.c[r0][c0] != colour) return added;
  QR[tail] = (int)r0;
  QC[tail] = (int)c0;
  tail++;
  memset(LABEL, 0, sizeof LABEL);
  LABEL[r0][c0] = 1u;
  while (head < tail) {
    int r = QR[head], c = QC[head], k;
    head++;
    for (k = 0; k < 4; k++) {
      int nr = r + dr[k], nc = c + dc[k];
      if (nr < 0 || nc < 0 || nr >= (int)LEVEL_START.h || nc >= (int)LEVEL_START.w) continue;
      if (LABEL[nr][nc] || LEVEL_START.c[nr][nc] != colour) continue;
      LABEL[nr][nc] = 1u;
      if (!MASK[nr][nc]) {
        MASK[nr][nc] = 1u;
        added++;
      }
      QR[tail] = nr;
      QC[tail] = nc;
      tail++;
    }
  }
  return added;
}

/*
 * Each separate patch of change is judged on its own, so a counter ticking
 * beside a large change is still seen. A small patch changing one colour
 * into another, which has come under a different action, at a different
 * place, and has never been undone, is not something it did: it ticks by
 * itself. A switch is undone sooner or later; a clock is not. Returns how
 * many cells were newly left out.
 */
static unsigned char SEEN_CHANGE[PL_SIZE][PL_SIZE];
static unsigned char RESTLESS[PL_SIZE][PL_SIZE];
static unsigned RESTLESS_STEPS;
static unsigned char RESTLESS_KINDS[PL_SIZE][PL_SIZE];
static unsigned char FROM_RESTLESS[PL_SIZE][PL_SIZE];
static unsigned WINDOW_KINDS;
static unsigned popcount8(unsigned v) {
  unsigned n = 0u;
  for (; v != 0u; v &= v - 1u) n++;
  return n;
}
static unsigned NO_MASKING;   /* this level, nothing is left out */
#define EX_FEW_SITUATIONS SELF_FEW_SITUATIONS
#define EX_FRESH_STARTS SELF_FRESH_STARTS
#define EX_DEATH_RETRIES SELF_DEATH_RETRIES   /* what kills can depend on when: doubted this often anyway */
static unsigned any_restless(void) {
  unsigned r, c;
  for (r = 0u; r < PL_SIZE; r++) {
    for (c = 0u; c < PL_SIZE; c++) {
      if (FROM_RESTLESS[r][c] && MASK[r][c]) return 1u;
    }
  }
  return 0u;
}
static unsigned char MASK_KEPT[PL_SIZE][PL_SIZE];
#define EX_RESTLESS_WINDOW SELF_RESTLESS_WINDOW
static unsigned char PATCH_R[EX_TICK_MAX], PATCH_C[EX_TICK_MAX];

static unsigned notice_ticks(ex_explorer_t *ex, const pl_frame_t *a, const pl_frame_t *b,
                             unsigned code) {
  unsigned r, c, added = 0u, bit = 1u << (code % 31u);
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  TICK_STEP++;
  if (NO_MASKING) return 0u;
  /*
   * Restless cells: a place that changes on nearly every step, whatever is done,
   * is moving by itself (an animation, a flowing track), however large the patch.
   * A body passing over a cell changes it twice, not step after step.
   */
  RESTLESS_STEPS++;
  WINDOW_KINDS |= 1u << (KIND(code) & 7u);
  for (r = 0u; r < b->h; r++) {
    for (c = 0u; c < b->w; c++) {
      if (a->c[r][c] != b->c[r][c]) {
        if (RESTLESS[r][c] < 255u) RESTLESS[r][c]++;
        RESTLESS_KINDS[r][c] |= (unsigned char)(1u << (KIND(code) & 7u));
      }
    }
  }
  if (ON(M_RESTLESS) && RESTLESS_STEPS >= EX_RESTLESS_WINDOW) {
    for (r = 0u; r < b->h; r++) {
      for (c = 0u; c < b->w; c++) {
        /* nearly every step, and under every kind of action tried: a body going
           back and forth changes as often, but not under actions that block it */
        if (RESTLESS[r][c] * 4u >= RESTLESS_STEPS * 3u && !MASK[r][c] &&
            popcount8(WINDOW_KINDS) >= 3u && RESTLESS_KINDS[r][c] == WINDOW_KINDS) {
          MASK[r][c] = 1u;
          FROM_RESTLESS[r][c] = 1u;
          added++;
        }
      }
    }
    memset(RESTLESS, 0, sizeof RESTLESS);
    memset(RESTLESS_KINDS, 0, sizeof RESTLESS_KINDS);
    WINDOW_KINDS = 0u;
    RESTLESS_STEPS = 0u;
    if (added > 0u) {
      ex->clock_cells += added;
      return added;
    }
  }
  memset(SEEN_CHANGE, 0, sizeof SEEN_CHANGE);
  for (r = 0u; r < b->h; r++) {
    for (c = 0u; c < b->w; c++) {
      unsigned head = 0u, tail = 0u, j;
      if (SEEN_CHANGE[r][c] || MASK[r][c] || a->c[r][c] == b->c[r][c]) continue;
      /* the patch of change this cell belongs to */
      SEEN_CHANGE[r][c] = 1u;
      QR[tail] = (int)r;
      QC[tail] = (int)c;
      tail++;
      while (head < tail) {
        int rr = QR[head], cc = QC[head], k;
        head++;
        for (k = 0; k < 4; k++) {
          int nr = rr + dr[k], nc = cc + dc[k];
          if (nr < 0 || nc < 0 || nr >= (int)b->h || nc >= (int)b->w) continue;
          if (SEEN_CHANGE[nr][nc] || MASK[nr][nc] || a->c[nr][nc] == b->c[nr][nc]) continue;
          SEEN_CHANGE[nr][nc] = 1u;
          if (tail < PL_SIZE * PL_SIZE) {
            QR[tail] = nr;
            QC[tail] = nc;
            tail++;
          }
        }
      }
      if (tail > EX_TICK_MAX) continue;   /* a large change: something it did */
      if (KIND(code) == 6u) {
        /* it pointed here: a change where it pointed is its own doing, however
           often that happens, and never something ticking by itself */
        unsigned px = PX(code), py = PY(code), near = 0u;
        for (j = 0u; j < tail && !near; j++) {
          int dy = QR[j] - (int)py, dx = QC[j] - (int)px;
          if (dy >= -3 && dy <= 3 && dx >= -3 && dx <= 3) near = 1u;
        }
        if (near) continue;
      }
      for (j = 0u; j < tail; j++) {   /* kept aside: masking walks the same queue */
        PATCH_R[j] = (unsigned char)QR[j];
        PATCH_C[j] = (unsigned char)QC[j];
      }
      for (j = 0u; j < tail; j++) {
        unsigned rr = PATCH_R[j], cc = PATCH_C[j], from = a->c[rr][cc], to = b->c[rr][cc];
        int where = (int)(rr * PL_SIZE + cc);
        if (PAIR_KINDS[to][from] != 0u) PAIR_CLOCK[from][to] = 0u;   /* undone: a switch */
        PAIR_KINDS[from][to] |= bit;
        if (PAIR_FIRST[from][to] < 0) {
          PAIR_FIRST[from][to] = where;
        } else if (PAIR_FIRST[from][to] != where) {
          PAIR_MOVED[from][to] = 1u;
        }
        /*
         * A clock keeps changing step after step, whatever is done. Painting, a
         * one-way change that only happens where it acted, can also come under
         * different actions in different places and never be undone, so that
         * alone is not enough: the change must also have come on several steps
         * running, under more than one action.
         */
        if (PAIR_LAST[from][to] != TICK_STEP) {
          if (PAIR_LAST[from][to] + 1u == TICK_STEP) {
            PAIR_RUN[from][to]++;
            PAIR_RUN_CODES[from][to] |= bit;
          } else {
            PAIR_RUN[from][to] = 1u;
            PAIR_RUN_CODES[from][to] = bit;
          }
          PAIR_LAST[from][to] = TICK_STEP;
        }
        if (PAIR_KINDS[to][from] == 0u && PAIR_MOVED[from][to] &&
            (PAIR_KINDS[from][to] & (PAIR_KINDS[from][to] - 1u)) != 0u) {
          PAIR_CLOCK[from][to] = 1u;
        }
      }
      for (j = 0u; j < tail; j++) {
        unsigned rr = PATCH_R[j], cc = PATCH_C[j], from = a->c[rr][cc], to = b->c[rr][cc];
        if (ON(M_CLOCK) && PAIR_CLOCK[from][to] && !MASK[rr][cc]) added += mask_patch(rr, cc, from);
      }
      if (added > 0u) {
        ex->clock_cells += added;
        return added;
      }
    }
  }
  return added;
}

/* a change far from what it did: the first sign that something else is moving */
static int moved_by_itself(const pl_frame_t *a, const pl_frame_t *b, unsigned code,
                           unsigned *pr, unsigned *pc) {
  unsigned r, c;
  for (r = 0u; r < b->h; r++) {
    for (c = 0u; c < b->w; c++) {
      if (MASK[r][c] || a->c[r][c] == b->c[r][c]) continue;
      if (KIND(code) == 6u) {
        int dy = (int)r - (int)PY(code), dx = (int)c - (int)PX(code);
        if (dy > -6 && dy < 6 && dx > -6 && dx < 6) continue;   /* where it pointed: its doing */
      }
      if (body_colour() >= 0 && ((int)a->c[r][c] == body_colour() || (int)b->c[r][c] == body_colour())) {
        continue;   /* itself moving is not a puzzle */
      }
      *pr = r;
      *pc = c;
      return 1;
    }
  }
  return 0;
}

/*
 * Being confused, and being curious about it.
 *
 * When something changes that it cannot lay at the door of its own act, it does
 * not rub it out: it is confused, and says so. A thing that moves whatever it
 * does is the world acting back, and the world acting back is most of what it
 * fails at -- what kills it, what runs down, what plays against it.
 *
 * While confused it does the one experiment that tells "I did that" from "it
 * does that anyway": act in a way it has settled does nothing, and watch. If the
 * thing moves regardless, the thing moves itself, and then there is a second
 * question worth asking -- by how much, each time? A step that repeats is the
 * answer; once it can say where the thing will be next, the puzzle is explained,
 * and only then may it stop watching it. Leaving something out is earned by
 * understanding it, never a way of avoiding it.
 */
#define EX_PUZZLES 16u
#define EX_WATCH SELF_WATCH   /* acts it will spend on one puzzle before letting it be */
#define EX_STEP_SURE 2u     /* the same step seen this often: it can say where the thing goes */
/*
 * How to answer a question is itself a question.
 *
 * It has ways of asking: do something it knows changes nothing and watch; do
 * again what it did before; point straight at the thing; act far away from it.
 * Which way answers which puzzle is not given. A way that leaves it able to say
 * what the thing does has shown it can answer; a way that runs out of looking
 * with nothing to show has shown it does not always answer, and is asked for
 * last. It tries the ways it has never tried first, since that is where the
 * bits are.
 *
 * And saying it understands is not enough: to understand a thing is to say
 * where it will be. Every look tests the last claim, and a claim that fails
 * takes the explanation back and counts against the way of asking that made it.
 */
static const char *ASK_NAME[Q_WAYS] = {
  "doing something that changes nothing, and watching",
  "doing again what I did before",
  "pointing straight at it",
  "acting far away from it"
};
static unsigned char ASK_STATE[Q_WAYS];   /* 0 never tried, 1 it has answered, 2 it has failed */

#define EX_RHYTHM 8u   /* steps of a thing's rhythm it will keep */
typedef struct {
  unsigned char alive, explained, colour, ask;
  unsigned char r, c;       /* where the moving patch was last seen */
  int hr[EX_RHYTHM], hc[EX_RHYTHM];   /* the steps it has taken, latest last */
  unsigned n_steps, period;  /* the rhythm it keeps to, once one repeats */
  unsigned watched, barren, rhythms_before;   /* looks that ruled nothing out */
} ex_puzzle_t;

/*
 * A thing that goes up and comes back has no one step: it has a rhythm. So the
 * question "by how much, each time?" is too poor to answer with, and when it
 * cannot be answered the question is widened -- not one step, but a run of steps
 * that repeats. The shortest run that repeats twice is what it keeps.
 */
/* how many rhythms are still possible for it: what looking has not yet ruled out */
static unsigned rhythms_left(const ex_puzzle_t *p) {
  unsigned period, i, n = 0u;
  for (period = 1u; period <= p->n_steps / 2u; period++) {
    int same = 1;
    for (i = 0u; i + period < p->n_steps && same; i++) {
      if (p->hr[i] != p->hr[i + period] || p->hc[i] != p->hc[i + period]) same = 0;
    }
    if (same) n++;
  }
  return n;
}

static unsigned find_rhythm(const ex_puzzle_t *p) {
  unsigned period, i;
  for (period = 1u; period <= p->n_steps / 2u; period++) {
    int same = 1;
    for (i = 0u; i + period < p->n_steps && same; i++) {
      if (p->hr[i] != p->hr[i + period] || p->hc[i] != p->hc[i + period]) same = 0;
    }
    if (same) return period;
  }
  return 0u;
}
static ex_puzzle_t PUZZLE[EX_PUZZLES];
/*
 * The one thing it is attending to now, and what it has worked out about it. The
 * kind of law is not chosen here: smarsh_guess formulates over the looks by
 * elimination, so a walk, a stillness or something with no name are all just
 * whatever description survives.
 */
static gs_guess_t GUESS;
static int GUESS_OF = -1;   /* which puzzle the guess is about */
/*
 * How long it may spend on one thing it cannot explain is not fixed: what it
 * explains earns it more looking, and what it gives up on costs it. So looking
 * grows where looking pays, and shrinks where it does not, at the rate it is
 * actually learning rather than at a rate I chose.
 */
/*
 * How long it looks at one thing is not a rate of success and not a number I
 * chose: it looks while looking still rules something out. When several looks
 * running have ruled nothing out, staring longer will not help. SELF_WATCH is
 * only the ceiling, never the reason.
 */
#define EX_BARREN_LOOKS 6u


/* the way of asking to try now: one never tried, else one that has answered */
static ex_ask_t choose_ask(unsigned seed) {
  unsigned w, n = 0u, pick[Q_WAYS];
  for (w = 0u; w < Q_WAYS; w++) {
    if (ASK_STATE[w] == 0u) pick[n++] = w;
  }
  if (n == 0u) {
    for (w = 0u; w < Q_WAYS; w++) {
      if (ASK_STATE[w] == 1u) pick[n++] = w;
    }
  }
  if (n == 0u) {
    for (w = 0u; w < Q_WAYS; w++) pick[n++] = w;
  }
  return (ex_ask_t)pick[seed % n];
}

static void puzzles_begin(void) {
  memset(PUZZLE, 0, sizeof PUZZLE);
  memset(ASK_STATE, 0, sizeof ASK_STATE);
  ASK_NOW = Q_STILL;
  WATCHING = 0u;
}

/* something changed that it did not do: a puzzle, unless it is one already */
static void be_confused(ex_explorer_t *ex, unsigned colour, unsigned r, unsigned c) {
  unsigned i;
  for (i = 0u; i < EX_PUZZLES; i++) {
    if (PUZZLE[i].alive && PUZZLE[i].colour == colour) return;
  }
  for (i = 0u; i < EX_PUZZLES; i++) {
    if (PUZZLE[i].alive) continue;
    memset(&PUZZLE[i], 0, sizeof PUZZLE[i]);
    PUZZLE[i].alive = 1u;
    PUZZLE[i].colour = (unsigned char)colour;
    PUZZLE[i].r = (unsigned char)r;
    PUZZLE[i].c = (unsigned char)c;
    PUZZLE[i].ask = (unsigned char)choose_ask(ex->actions + i);
    ex->puzzles++;
    WATCHING = 1u;
    ASK_NOW = (ex_ask_t)PUZZLE[i].ask;
    ASK_R = r;
    ASK_C = c;
    {
      char a1[200];
      sprintf(a1, "something moved that I did not move. I ask by %s", ASK_NAME[PUZZLE[i].ask]);
      think(ex, "what made that happen, if I did not?", a1);
    }
    return;
  }
}

/*
 * Where the thing has got to: not the nearest cell of its colour, which on a busy
 * board jumps about, but the middle of the patch it belongs to. A thing is a patch,
 * and following the thing means following the patch.
 */
static unsigned char PATCH_SEEN[PL_SIZE][PL_SIZE];

static int find_patch(const pl_frame_t *f, unsigned colour, unsigned r0, unsigned c0,
                      unsigned *rr, unsigned *cc) {
  unsigned r, c, best = 0xFFFFu, sr = 0u, sc = 0u;
  int found = 0;
  static const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
  memset(PATCH_SEEN, 0, sizeof PATCH_SEEN);
  for (r = 0u; r < f->h; r++) {
    for (c = 0u; c < f->w; c++) {
      unsigned head = 0u, tail = 0u, n = 0u, mid_r, mid_c, d;
      long sum_r = 0, sum_c = 0;
      if (PATCH_SEEN[r][c] || f->c[r][c] != colour) continue;
      PATCH_SEEN[r][c] = 1u;
      QR[tail] = (int)r;
      QC[tail] = (int)c;
      tail++;
      while (head < tail) {
        int y = QR[head], x = QC[head], k;
        head++;
        n++;
        sum_r += y;
        sum_c += x;
        for (k = 0; k < 4; k++) {
          int ny = y + dr[k], nx = x + dc[k];
          if (ny < 0 || nx < 0 || ny >= (int)f->h || nx >= (int)f->w) continue;
          if (PATCH_SEEN[ny][nx] || f->c[ny][nx] != colour) continue;
          PATCH_SEEN[ny][nx] = 1u;
          QR[tail] = ny;
          QC[tail] = nx;
          tail++;
        }
      }
      mid_r = (unsigned)(sum_r / (long)n);
      mid_c = (unsigned)(sum_c / (long)n);
      d = (unsigned)(abs((int)mid_r - (int)r0) + abs((int)mid_c - (int)c0));
      if (d < best) {
        best = d;
        sr = mid_r;
        sc = mid_c;
        found = 1;
      }
    }
  }
  if (found) {
    *rr = sr;
    *cc = sc;
  }
  return found;
}

/* one more look at what it cannot explain: has it a step that repeats? */
static void watch_puzzles(ex_explorer_t *ex, const pl_frame_t *f) {
  unsigned i;
  WATCHING = 0u;
  for (i = 0u; i < EX_PUZZLES; i++) {
    unsigned r = 0u, c = 0u;
    int dr, dc;
    ex_puzzle_t *p = &PUZZLE[i];
    if (!p->alive) continue;
    if (p->explained) {
      /* to understand it is to say where it will be: so say, and see */
      unsigned at = p->period > 0u ? (p->n_steps % p->period) : 0u;
      unsigned want_r = (unsigned)((int)p->r + p->hr[at]), want_c = (unsigned)((int)p->c + p->hc[at]);
      if (!find_patch(f, p->colour, want_r, want_c, &r, &c)) {
        p->alive = 0u;
        continue;
      }
      if (r != want_r || c != want_c) {
        p->explained = 0u;
        p->period = 0u;
        ex->predictions_broken++;
        if (ASK_STATE[p->ask] != 2u) ASK_STATE[p->ask] = 2u;
        think(ex, "was I right about where that thing would be?",
              "no. So I did not understand it, and the way I asked did not answer");
        WATCHING = 1u;
      }
      else {
        unsigned k = p->n_steps < EX_RHYTHM ? p->n_steps : EX_RHYTHM - 1u;
        p->hr[k] = (int)r - (int)p->r;
        p->hc[k] = (int)c - (int)p->c;
        if (p->n_steps < EX_RHYTHM) p->n_steps++;
      }
      p->r = (unsigned char)r;
      p->c = (unsigned char)c;
      continue;
    }
    p->watched++;
    /* it attends to one question at a time, and works out the law of that one */
    if (GUESS_OF != (int)i) {
      gs_begin(&GUESS);
      GUESS_OF = (int)i;
    }
    ASK_NOW = (ex_ask_t)p->ask;
    ASK_R = p->r;
    ASK_C = p->c;
    if (!find_patch(f, p->colour, p->r, p->c, &r, &c)) {
      p->alive = 0u;
      continue;
    }
    dr = (int)r - (int)p->r;
    dc = (int)c - (int)p->c;
    if (p->n_steps >= EX_RHYTHM) {
      unsigned k;
      for (k = 1u; k < EX_RHYTHM; k++) {
        p->hr[k - 1u] = p->hr[k];
        p->hc[k - 1u] = p->hc[k];
      }
      p->n_steps = EX_RHYTHM - 1u;
    }
    p->hr[p->n_steps] = dr;
    p->hc[p->n_steps] = dc;
    p->n_steps++;
    {
      /* did this look rule anything out? If not, looking again will not either */
      unsigned left = rhythms_left(p);
      if (p->n_steps >= 4u) {
        if (p->rhythms_before != 0u && left >= p->rhythms_before) p->barren++;
        else p->barren = 0u;
      }
      p->rhythms_before = left;
    }
    if (GUESS_OF == (int)i) {
      /* the law of the thing, whatever shape it turns out to have */
      double nr = 0.0, nc = 0.0;
      (void)gs_saw(&GUESS, (double)p->watched, (double)r, (double)c);
      if (GUESS.n >= 5u && (p->watched % 4u) == 0u) {
        (void)gs_formulate(&GUESS);
        if (GUESS.found && gs_predict(&GUESS, (double)p->watched, (double)r, (double)c, &nr, &nc)) {
          char a1[220];
          p->explained = 1u;
          p->period = 1u;
          p->hr[0] = (int)nr - (int)r;
          p->hc[0] = (int)nc - (int)c;
          p->n_steps = 1u;
          ex->laws_found++;
          ex->puzzles_explained++;
          if (ASK_STATE[p->ask] == 0u) {
            ASK_STATE[p->ask] = 1u;
            ex->asks_that_answer++;
          }
          sprintf(a1, "I can say it: the next row is %s and the next column is %s",
                  GUESS.law_r, GUESS.law_c);
          think(ex, "what is that thing doing?", a1);
          continue;
        }
      }
    }
    {
      unsigned period = find_rhythm(p);
      if (period > 0u && p->n_steps >= period * 2u) {
        char a1[200];
        p->period = period;
        p->explained = 1u;
        ex->puzzles_explained++;
        if (ASK_STATE[p->ask] == 0u) {
          ASK_STATE[p->ask] = 1u;   /* that way of asking has answered something */
          ex->asks_that_answer++;
        }
        if (period == 1u) {
          sprintf(a1, "it goes %d down and %d across every time: now I can say where it will be",
                  p->hr[0], p->hc[0]);
        } else {
          sprintf(a1, "it keeps to a rhythm of %u steps and then does the same again: now I can say where it will be",
                  period);
        }
        think(ex, "what is that thing doing?", a1);
      }
    }
    p->r = (unsigned char)r;
    p->c = (unsigned char)c;
    if ((p->barren >= EX_BARREN_LOOKS || p->watched >= EX_WATCH) && !p->explained) {
      p->alive = 0u;   /* looked long enough and still no step it can name */
      ex->puzzles_given_up++;
      if (ASK_STATE[p->ask] == 0u) {
        ASK_STATE[p->ask] = 2u;   /* it does not always answer: asked for last from now on */
        ex->asks_ruled_out++;
        think(ex, "did asking that way answer?",
              "no: I looked as long as I could and it told me nothing. I ask another way next time");
      }
      think(ex, "can I say what that thing does?",
            "no: I have watched it and found no step it keeps to. I leave it be, and get on");
    }
    if (p->alive && !p->explained) WATCHING = 1u;
  }
}

/* where an explained thing will be after one more step */
static int mover_next(unsigned i, unsigned *r, unsigned *c) {
  unsigned at;
  if (i >= EX_PUZZLES || !PUZZLE[i].alive || !PUZZLE[i].explained || PUZZLE[i].period == 0u) return 0;
  at = PUZZLE[i].n_steps % PUZZLE[i].period;
  *r = (unsigned)((int)PUZZLE[i].r + PUZZLE[i].hr[at]);
  *c = (unsigned)((int)PUZZLE[i].c + PUZZLE[i].hc[at]);
  return 1;
}

/*
 * Where an act would put it, set against where the things it has explained will
 * be. Having been killed at least once, it will not walk into a thing whose step
 * it can say: that is what understanding the thing was for.
 */
static int would_meet_mover(const ex_sum_t *sum, int body, unsigned kind) {
  unsigned i, mr, mc;
  double br, bc;
  if (body < 0 || sum[body].count == 0u || SHIFT_SEEN[kind][body] < EX_SHIFT_SURE) return 0;
  br = (double)(sum[body].rows + SHIFT_R[kind][body]) / (double)sum[body].count;
  bc = (double)(sum[body].cols + SHIFT_C[kind][body]) / (double)sum[body].count;
  for (i = 0u; i < EX_PUZZLES; i++) {
    if (!mover_next(i, &mr, &mc)) continue;
    if (fabs(br - (double)mr) + fabs(bc - (double)mc) <= 1.5) return 1;
  }
  return 0;
}

/*
 * Going by what it has worked out.
 *
 * It can say what ends a level -- step onto this, once there is none of that --
 * and until now it still chose its acts by searching the situations it happened
 * to have met. That is looking for the answer among things it has already seen.
 *
 * Here it goes at the world instead. It knows where its body is, what each act
 * does to it (SHIFT, worked out), and what has refused it entry (BLOCKER, worked
 * out). That is enough to lay a way from where it stands to the nearest thing its
 * theories say it needs, over the board itself rather than over its memory: what
 * must be true first (have none of this) before what must be true last (be on
 * that).
 *
 * Every step of the way is a claim -- the body will be there next -- so every step
 * is checked. A body that does not arrive where its own law said says the way was
 * wrong, and the way is dropped rather than pushed on with.
 */
#define EX_PLAN_MAX 64u
static unsigned char PLAN_ACT[EX_PLAN_MAX];
static unsigned PLAN_LEN, PLAN_POS;
static unsigned PLAN_WANT_R, PLAN_WANT_C;   /* where the body should be after the next step */

/* the middle cell of the body */
static int body_cell(const pl_frame_t *f, unsigned *br, unsigned *bc) {
  static ex_sum_t sum[PL_COLOURS];
  int body = body_colour();
  if (body < 0) return 0;
  colour_sums(f, sum);
  if (sum[body].count == 0u) return 0;
  *br = (unsigned)(sum[body].rows / (long)sum[body].count);
  *bc = (unsigned)(sum[body].cols / (long)sum[body].count);
  return 1;
}

/* what one act does to the body, in cells; 0 if it cannot say */
static int act_step(unsigned kind, int *dr, int *dc) {
  static ex_sum_t sum[PL_COLOURS];
  int body = body_colour();
  unsigned n;
  if (body < 0 || kind >= 8u || kind == 6u || SHIFT_SEEN[kind][body] < EX_SHIFT_SURE) return 0;
  colour_sums(&LEVEL_START, sum);
  n = sum[body].count;
  if (n == 0u || SHIFT_R[kind][body] % (long)n != 0 || SHIFT_C[kind][body] % (long)n != 0) return 0;
  *dr = (int)(SHIFT_R[kind][body] / (long)n);
  *dc = (int)(SHIFT_C[kind][body] / (long)n);
  return (*dr != 0 || *dc != 0);
}

/*
 * A way to the nearest cell of any colour in `want`, over the board: each act it
 * can say the effect of is a move, a colour that has refused it entry is a wall.
 * Length of the way, or 0.
 */
static unsigned plan_over_world(ex_explorer_t *ex, const pl_frame_t *f, unsigned want) {
  static unsigned char seen[PL_SIZE][PL_SIZE];
  static unsigned char came_act[PL_SIZE][PL_SIZE];
  static int came_r[PL_SIZE][PL_SIZE], came_c[PL_SIZE][PL_SIZE];
  static int qr[PL_SIZE * PL_SIZE], qc[PL_SIZE * PL_SIZE];
  unsigned head = 0u, tail = 0u, br, bc, k;
  int found_r = -1, found_c = -1;

  if (want == 0u || !body_cell(f, &br, &bc)) return 0u;
  memset(seen, 0, sizeof seen);
  seen[br][bc] = 1u;
  qr[tail] = (int)br;
  qc[tail] = (int)bc;
  tail++;
  while (head < tail && found_r < 0) {
    int r = qr[head], c = qc[head];
    head++;
    for (k = 1u; k < 8u; k++) {
      int dr, dc, nr, nc;
      if (!act_step(k, &dr, &dc)) continue;
      nr = r + dr;
      nc = c + dc;
      if (nr < 0 || nc < 0 || nr >= (int)f->h || nc >= (int)f->w) continue;
      if (seen[nr][nc] || BLOCKER[f->c[nr][nc]]) continue;
      seen[nr][nc] = 1u;
      came_act[nr][nc] = (unsigned char)k;
      came_r[nr][nc] = r;
      came_c[nr][nc] = c;
      if (want & (1u << f->c[nr][nc])) {
        found_r = nr;
        found_c = nc;
        break;
      }
      qr[tail] = nr;
      qc[tail] = nc;
      tail++;
    }
  }
  if (found_r < 0) return 0u;
  {
    unsigned n = 0u, i;
    int r = found_r, c = found_c;
    while (!(r == (int)br && c == (int)bc) && n < EX_PLAN_MAX) {
      int pr = came_r[r][c], pc = came_c[r][c];
      PLAN_ACT[n++] = came_act[r][c];
      r = pr;
      c = pc;
    }
    for (i = 0u; i < n / 2u; i++) {   /* the way back, turned round */
      unsigned char t = PLAN_ACT[i];
      PLAN_ACT[i] = PLAN_ACT[n - 1u - i];
      PLAN_ACT[n - 1u - i] = t;
    }
    PLAN_LEN = n;
    PLAN_POS = 0u;
    ex->plans_laid++;
    return n;
  }
}

/* what its theories say to make true, nearest first: have none of this, then be on that */
static unsigned plan_from_theory(ex_explorer_t *ex, const pl_frame_t *f) {
  unsigned want = 0u, v;
  static ex_sum_t sum[PL_COLOURS];
  if (!ON(M_PLAN)) return 0u;
  colour_sums(f, sum);
  for (v = 0u; v < PL_COLOURS; v++) {   /* first, what must be gone but is still here */
    if ((AIM_GONE & (1u << v)) && sum[v].count > 0u) want |= 1u << v;
  }
  if (want == 0u) want = aim_mask();    /* then, what it must be standing on */
  return plan_over_world(ex, f, want);
}

/* ---- finding the nearest thing untried -------------------------------------------- */

static int BFS_PREV[EX_MAX_NODES];
static unsigned char BFS_ACT[EX_MAX_NODES];
static unsigned BFS_STAMP[EX_MAX_NODES];
static unsigned STAMP;
static int BFS_Q[EX_MAX_NODES];
static unsigned BFS_DIST[EX_MAX_NODES];
static unsigned char PATH[EX_MAX_NODES];
static int PATH_NODE[EX_MAX_NODES];

/*
 * What used to be here: how much a situation resembled the ones it had won from,
 * as a cosine between two tallies of changes, worth EX_RESEMBLE steps of walking.
 * That is a likeness, and a likeness is a guess about what is probable. It is out.
 * What a situation is worth is now counted in acts still to be done (see below),
 * which is a fact about the work, not a bet on the answer.
 */

/*
 * The way to the untried situation most worth going to. With no win seen yet,
 * the nearest. Once it knows what winning changed, the one that best trades
 * resembling winning against distance. Length of the way, or 0.
 */
static unsigned plan_to_untried(ex_explorer_t *ex, int cur, double *how_like, unsigned *how_far) {
  int head = 0, tail = 0, best = EX_NONE;
  double best_score = -1e30;
  unsigned seen_untried = 0u;
  STAMP++;
  BFS_STAMP[cur] = STAMP;
  BFS_PREV[cur] = EX_NONE;
  BFS_DIST[cur] = 0u;
  BFS_Q[tail++] = cur;
  while (head < tail) {
    int n = BFS_Q[head++];
    unsigned i;
    if (best != EX_NONE && ((aim_mask() == 0u && AIM_GONE == 0u && !MATCH_VARIES && !NEAR_VARIES) || seen_untried >= 128u)) break;   /* nearest is enough */
    if (ex->wins_seen > 0u && seen_untried >= 256u) break;
    for (i = 0u; i < N_NACT[n]; i++) {
      int m = N_NEXT[n][i];
      if (m < 0 || (N_FLAG[n][i] != 0u && N_FLAG[n][i] != EX_FLAG_PREDICTED) || BFS_STAMP[m] == STAMP) continue;
      BFS_STAMP[m] = STAMP;
      BFS_PREV[m] = n;
      BFS_ACT[m] = (unsigned char)i;
      BFS_DIST[m] = BFS_DIST[n] + 1u;
      BFS_Q[tail++] = m;
      if (N_UNTRIED[m] > 0u) {
        /*
         * How far off a situation is, counted in acts that must still be done:
         * the steps to reach it, plus the steps its own reckoning says remain
         * from there -- cells of a colour that must be gone, places still to
         * cross, cells of a picture still unlike its twin. Every term is a count
         * of work, never a weight and never a likeness: nothing here says one
         * situation is more probable than another, only that one demands fewer
         * acts before what it has settled can hold.
         */
        double like = 0.0, left = (double)BFS_DIST[m];
        if (aim_mask() != 0u && N_GOALD[m] != EX_GOAL_UNKNOWN) left += (double)N_GOALD[m];
        if (AIM_GONE != 0u) left += (double)N_GONEC[m];
        if (MATCH_VARIES && N_MATCHD[m] != EX_MATCH_NONE) left += (double)N_MATCHD[m];
        if (NEAR_VARIES && N_NEARD[m] != EX_NEAR_NONE) left += (double)N_NEARD[m];
        {
          double score = -left;
          seen_untried++;
          if (best == EX_NONE || score > best_score) {
            best = m;
            best_score = score;
            *how_like = like;
            *how_far = BFS_DIST[m];
          }
        }
        break;
      }
    }
  }
  if (best == EX_NONE) return 0u;
  {
    unsigned len = 0u, i;
    int n = best;
    while (n != cur) {
      PATH[len] = BFS_ACT[n];
      PATH_NODE[len] = BFS_PREV[n];
      len++;
      n = BFS_PREV[n];
    }
    for (i = 0u; i < len / 2u; i++) {   /* the walk back, turned round */
      unsigned char ta = PATH[i];
      int tn = PATH_NODE[i];
      PATH[i] = PATH[len - 1u - i];
      PATH_NODE[i] = PATH_NODE[len - 1u - i];
      PATH[len - 1u - i] = ta;
      PATH_NODE[len - 1u - i] = tn;
    }
    return len;
  }
}

/* The shortest route it knows from start to goal; its length, or 0 if none. */
static unsigned route(int start, int goal) {
  int head = 0, tail = 0, found = 0;
  if (start == goal) return 0u;
  STAMP++;
  BFS_STAMP[start] = STAMP;
  BFS_Q[tail++] = start;
  while (head < tail && !found) {
    int n = BFS_Q[head++];
    unsigned i;
    for (i = 0u; i < N_NACT[n]; i++) {
      int m = N_NEXT[n][i];
      if (m < 0 || N_FLAG[n][i] != 0u || BFS_STAMP[m] == STAMP) continue;
      BFS_STAMP[m] = STAMP;
      BFS_PREV[m] = n;
      BFS_ACT[m] = (unsigned char)i;
      if (m == goal) { found = 1; break; }
      BFS_Q[tail++] = m;
    }
  }
  if (!found) return 0u;
  {
    unsigned len = 0u, i;
    int n = goal;
    while (n != start) {
      PATH[len] = BFS_ACT[n];
      PATH_NODE[len] = BFS_PREV[n];
      len++;
      n = BFS_PREV[n];
    }
    for (i = 0u; i < len / 2u; i++) {
      unsigned char ta = PATH[i];
      int tn = PATH_NODE[i];
      PATH[i] = PATH[len - 1u - i];
      PATH_NODE[i] = PATH_NODE[len - 1u - i];
      PATH[len - 1u - i] = ta;
      PATH_NODE[len - 1u - i] = tn;
    }
    return len;
  }
}

/*
 * The step of the last level's winning plan that fits here: the same kind of
 * action, and for pointing, a thing of the same colour, the same one in the
 * order if there is one, else the nearest in the order. EX_MAX_ACTS if none.
 */
static unsigned fit_step(const ex_explorer_t *ex, int cur, const pl_frame_t *f, unsigned step) {
  unsigned i, best = EX_MAX_ACTS, best_gap = 1000u;
  unsigned kind = ex->plan_kind[step];
  for (i = 0u; i < N_NACT[cur]; i++) {
    unsigned code = N_ACT[cur][i];
    if (KIND(code) != kind) continue;
    if (kind != 6u) return i;
    if (f->c[PY(code)][PX(code)] != ex->plan_colour[step]) continue;
    {
      unsigned rank = thing_rank(f, PX(code), PY(code));
      unsigned gap = rank > ex->plan_rank[step] ? rank - ex->plan_rank[step] : ex->plan_rank[step] - rank;
      if (gap < best_gap) {
        best_gap = gap;
        best = i;
      }
    }
  }
  return best;
}

/* ---- playing -------------------------------------------------------------------- */

/* A new level: the layout is new, so where it has been is forgotten. What ticks
   by itself is kept: a clock usually stays where it was from level to level. */
static void new_level(ex_explorer_t *ex, const pl_frame_t *f) {
  (void)ex;
  forget_level();
  LEVEL_START = *f;
}

/*
 * What it carries between runs of one game, when told to: what it settled, not
 * what it merely did. What ticks by itself; what it moves and how each action
 * shifts it; what it stepped onto at endings; what each kind of act does; and,
 * for each level it ended, the shortest way it knows from the level's start to
 * its end. On the next run a level with a known way is walked by that way;
 * if the world does not answer as it did, the way is dropped and it explores.
 */
#define EX_ROUTE_MAX 512u
static unsigned short KNOWN_ROUTE[EX_MAX_LEVELS][EX_ROUTE_MAX];
static unsigned KNOWN_LEN[EX_MAX_LEVELS];
static unsigned CARRIED;   /* what it knows came from before: not wiped at the start */
static unsigned RUNS_BEFORE, BEST_BEFORE;

sm_status_t ex_save(const ex_explorer_t *ex, FILE *out) {
  unsigned r, c, v, k, i;
  if (ex == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  fprintf(out, "ciall-explorer-mind 1\n");
  fprintf(out, "runs %u\n", RUNS_BEFORE + 1u);
  fprintf(out, "best %u\n", ex->levels_done > BEST_BEFORE ? ex->levels_done : BEST_BEFORE);
  for (r = 0u; r < PL_SIZE; r++) {
    for (c = 0u; c < PL_SIZE; c++) {
      if (MASK[r][c]) fprintf(out, "mask %u %u %u\n", r, c, (unsigned)FROM_RESTLESS[r][c]);
    }
  }
  for (v = 0u; v < PL_COLOURS; v++) {
    fprintf(out, "colour %u %u %u %u\n", v, MOVED_VOTES[v], MOVE_ONTO[v], WIN_ONTO[v]);
    for (k = 0u; k < 8u; k++) {
      if (SHIFT_SEEN[k][v] > 0u) fprintf(out, "shift %u %u %ld %ld %u\n", k, v, SHIFT_R[k][v], SHIFT_C[k][v], SHIFT_SEEN[k][v]);
    }
  }
  fprintf(out, "onto %u %u %u\n", MOVES_ONTO, WINS_ONTO, BODY_STEP);
  {
    unsigned f2;
    char name[32];
    for (f2 = 0u; f2 < THEORY.n_fam; f2++) {
      const en_family_t *fam = &THEORY.fam[f2];
      if (fam->survived_ending && sm_count(&fam->dom) > 0u) {
        /* a theory in colours is about this game only; one in roles can be true of
           a game it has never seen, and only those are offered to other games */
        int carries = strstr(en_family_name(fam, name, sizeof name), "_R") != 0;
        fprintf(out, "%s %s 1\n", carries ? "family" : "family-here",
                en_family_name(fam, name, sizeof name));
      }
    }
    if (THEORY.widenings > 0u) fprintf(out, "widened %u\n", THEORY.widenings);
  }
  for (i = 0u; i < EX_KINDS; i++) {
    if (KIND_KEY[i] != 0u && KIND_DONE[i]) fprintf(out, "kind %lu %u %u\n", (unsigned long)KIND_KEY[i], (unsigned)KIND_DONE[i], (unsigned)KIND_DID[i]);
  }
  for (k = 0u; k < EX_MAX_LEVELS; k++) {
    if (KNOWN_LEN[k] == 0u) continue;
    fprintf(out, "route %u %u", k, KNOWN_LEN[k]);
    for (i = 0u; i < KNOWN_LEN[k]; i++) fprintf(out, " %u", (unsigned)KNOWN_ROUTE[k][i]);
    fprintf(out, "\n");
  }
  return SM_OK;
}

sm_status_t ex_load(ex_explorer_t *ex, FILE *in) {
  char word[32];
  if (ex == 0 || in == 0) return SM_ERR_NULL_ARGUMENT;
  memset(MASK, 0, sizeof MASK);
  memset(FROM_RESTLESS, 0, sizeof FROM_RESTLESS);
  memset(MOVED_VOTES, 0, sizeof MOVED_VOTES);
  memset(MOVE_ONTO, 0, sizeof MOVE_ONTO);
  memset(WIN_ONTO, 0, sizeof WIN_ONTO);
  memset(SHIFT_SEEN, 0, sizeof SHIFT_SEEN);
  memset(KIND_KEY, 0, sizeof KIND_KEY);
  memset(KIND_DONE, 0, sizeof KIND_DONE);
  memset(KIND_DID, 0, sizeof KIND_DID);
  memset(KNOWN_LEN, 0, sizeof KNOWN_LEN);
  MOVES_ONTO = WINS_ONTO = 0u;
  BODY_STEP = 1u;
  RUNS_BEFORE = BEST_BEFORE = 0u;
  while (fscanf(in, "%31s", word) == 1) {
    unsigned a, b, c2, d;
    long lr, lc;
    unsigned long key;
    if (strcmp(word, "runs") == 0 && fscanf(in, "%u", &a) == 1) {
      RUNS_BEFORE = a;
    } else if (strcmp(word, "best") == 0 && fscanf(in, "%u", &a) == 1) {
      BEST_BEFORE = a;
    } else if (strcmp(word, "mask") == 0 && fscanf(in, "%u %u %u", &a, &b, &c2) == 3) {
      if (a < PL_SIZE && b < PL_SIZE) {
        MASK[a][b] = 1u;
        FROM_RESTLESS[a][b] = (unsigned char)(c2 != 0u);
      }
    } else if (strcmp(word, "colour") == 0 && fscanf(in, "%u %u %u %u", &a, &b, &c2, &d) == 4) {
      if (a < PL_COLOURS) {
        MOVED_VOTES[a] = b;
        MOVE_ONTO[a] = c2;
        WIN_ONTO[a] = d;
      }
    } else if (strcmp(word, "shift") == 0 && fscanf(in, "%u %u %ld %ld %u", &a, &b, &lr, &lc, &c2) == 5) {
      if (a < 8u && b < PL_COLOURS) {
        SHIFT_R[a][b] = lr;
        SHIFT_C[a][b] = lc;
        SHIFT_SEEN[a][b] = c2;
      }
    } else if (strcmp(word, "onto") == 0 && fscanf(in, "%u %u %u", &a, &b, &c2) == 3) {
      MOVES_ONTO = a;
      WINS_ONTO = b;
      BODY_STEP = c2 > 0u ? c2 : 1u;
    } else if (strcmp(word, "kind") == 0 && fscanf(in, "%lu %u %u", &key, &a, &b) == 3) {
      unsigned at = kind_slot((uint32_t)key);
      KIND_DONE[at] = (unsigned char)(a != 0u);
      KIND_DID[at] = (unsigned char)(b != 0u);
    } else if (strcmp(word, "route") == 0 && fscanf(in, "%u %u", &a, &b) == 2) {
      unsigned i, code;
      for (i = 0u; i < b; i++) {
        if (fscanf(in, "%u", &code) != 1) break;
        if (a < EX_MAX_LEVELS && i < EX_ROUTE_MAX) KNOWN_ROUTE[a][i] = (unsigned short)code;
      }
      if (a < EX_MAX_LEVELS && i == b && b <= EX_ROUTE_MAX) KNOWN_LEN[a] = b;
    }
  }
  CARRIED = 1u;
  ex->runs_before = RUNS_BEFORE;
  ex->best_before = BEST_BEFORE;
  return SM_OK;
}

/* the act here most like a remembered one: the same act, or pointing at the same colour nearest the place */
static unsigned recall_step(int cur, const pl_frame_t *f, unsigned code) {
  unsigned i, best = EX_MAX_ACTS, best_d = 1u << 30;
  for (i = 0u; i < N_NACT[cur]; i++) {
    unsigned a = N_ACT[cur][i], d;
    if (a == code) return i;
    if (KIND(a) != 6u || KIND(code) != 6u) continue;
    if (f->c[PY(a)][PX(a)] != f->c[PY(code)][PX(code)]) continue;
    d = (unsigned)(abs((int)PX(a) - (int)PX(code)) + abs((int)PY(a) - (int)PY(code)));
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

sm_status_t ex_play(ex_explorer_t *ex, ex_game_t *g, const pl_frame_t *first, unsigned budget) {
  static pl_frame_t now, next;
  unsigned level_began = 0u, path_len = 0u, path_pos = 0u, plan_pos = 0u;
  int replaying = 0, took_back = 0, unmasked = 0, recalling = 0;
  unsigned recall_pos = 0u;
  unsigned took_back_at = 0u, death_retry_at = 0u, fresh_starts = 0u, death_retries_here = 0u;
  int start, cur;

  if (ex == 0 || g == 0 || first == 0) return SM_ERR_NULL_ARGUMENT;
  now = *first;
  ex->thoughts = 0u;
  forget_places();
  PLACES_OPEN = 0u;
  ACT_WINDOW = 0u;
  BUILT_OVERFLOW = 0u;
  {
    const char *seed = getenv("CIALL_SEED");
    TIE_SEED = seed != 0 ? (uint64_t)strtoull(seed, 0, 10) : 0u;
    seed = getenv("CIALL_TIE");
    TIE_MODE = seed != 0 ? (unsigned)atoi(seed) : SELF_TIE_MODE;   /* uniform over kinds: best over seeds */
  }
  if (!CARRIED) {   /* a new game with nothing brought from before */
    memset(KIND_KEY, 0, sizeof KIND_KEY);
    memset(KIND_DONE, 0, sizeof KIND_DONE);
    memset(KIND_DID, 0, sizeof KIND_DID);
    memset(MASK, 0, sizeof MASK);
    memset(FROM_RESTLESS, 0, sizeof FROM_RESTLESS);
    memset(MOVED_VOTES, 0, sizeof MOVED_VOTES);
    memset(MOVE_ONTO, 0, sizeof MOVE_ONTO);
    memset(WIN_ONTO, 0, sizeof WIN_ONTO);
    MOVES_ONTO = 0u;
    WINS_ONTO = 0u;
    BODY_STEP = 1u;
    memset(SHIFT_SEEN, 0, sizeof SHIFT_SEEN);
    memset(KNOWN_LEN, 0, sizeof KNOWN_LEN);
  }
  read_off();
  (void)en_begin(&THEORY, SELF_USE_LIVE ? getenv("CIALL_LIVE") : 0);
  ways_begin();
  puzzles_begin();
  PLAN_LEN = 0u;
  memset(SENT_LAW, 0, sizeof SENT_LAW);   /* a new world: how far a thing goes here is unknown */
  HOLDING = 0;
  memset(BLOCKER, 0, sizeof BLOCKER);
  theory_aims();
  memset(RESTLESS, 0, sizeof RESTLESS);
  RESTLESS_STEPS = 0u;
  memset(RESTLESS_KINDS, 0, sizeof RESTLESS_KINDS);
  WINDOW_KINDS = 0u;
  NO_MASKING = 0u;
  STOOD_OFF = 0u;
  forget_changes();
  new_level(ex, &now);
  start = node_of(ex, g, &now);
  cur = start;
  if (ON(M_RECALL) && KNOWN_LEN[0] > 0u) {
    recalling = 1;
    recall_pos = 0u;
    think(ex, "have I been here before?", "yes: I remember a way through this level, and walk it");
  }
  {
    char a1[160];
    unsigned k, simple = 0u, point = 0u;
    for (k = 0u; k < N_NACT[start]; k++) {
      if (KIND(N_ACT[start][k]) == 6u) point++; else simple++;
    }
    sprintf(a1, "%u action%s, and %u thing%s I could point at", simple, simple == 1u ? "" : "s",
            point, point == 1u ? "" : "s");
    think(ex, "what can I do here?", a1);
  }

  while (ex->actions < budget) {
    unsigned idx = EX_MAX_ACTS, code, kind, i;
    int outcome, nb;

    if (cur < 0) {
      /* memory for this level is full: what it has cannot hold more situations */
      ex->exhausted = 1;
      break;
    }

    /*
     * The way it has laid from what it worked out. Every step says where the body
     * will be; a body that is not there says the way was wrong.
     */
    if (PLAN_LEN > 0u && idx == EX_MAX_ACTS) {
      unsigned br, bc;
      if (PLAN_POS >= PLAN_LEN || !body_cell(&now, &br, &bc) ||
          (PLAN_POS > 0u && (br != PLAN_WANT_R || bc != PLAN_WANT_C))) {
        if (PLAN_POS > 0u && PLAN_POS < PLAN_LEN) {
          ex->plans_broke++;
          think(ex, "did I get where my own reckoning said I would?",
                "no, so the way I laid was wrong. I lay it again from where I am");
        }
        PLAN_LEN = 0u;
      } else {
        unsigned k = PLAN_ACT[PLAN_POS], w;
        int dr = 0, dc = 0;
        for (w = 0u; w < N_NACT[cur]; w++) {
          if (KIND(N_ACT[cur][w]) == k && N_FLAG[cur][w] != 1u) {
            idx = w;
            break;
          }
        }
        if (idx == EX_MAX_ACTS) {
          PLAN_LEN = 0u;
        } else if (act_step(k, &dr, &dc)) {
          PLAN_WANT_R = (unsigned)((int)br + dr);
          PLAN_WANT_C = (unsigned)((int)bc + dc);
          PLAN_POS++;
          WAY_NOW = W_PLAN;
          ex->plan_steps++;
          path_len = 0u;
        }
      }
    }
    /* a way remembered from an earlier run, step by step, while the world answers as it did */
    if (recalling && !WAY_OUT[W_RECALL]) {
      unsigned L = ex->levels_done;
      WAY_NOW = W_RECALL;
      if (L < EX_MAX_LEVELS && recall_pos < KNOWN_LEN[L]) {
        idx = recall_step(cur, &now, KNOWN_ROUTE[L][recall_pos]);
        if (idx == EX_MAX_ACTS) {
          recalling = 0;
          think(ex, "does the way I remember still fit?", "no: what it needs is not here. I explore");
        } else {
          recall_pos++;
          path_len = 0u;
          ex->recalled++;
        }
      } else {
        recalling = 0;
        think(ex, "did the way I remember end the level?", "no: it has changed, or I was wrong. I explore");
      }
    }
    /* the plan that won the last level, step by step, while it fits */
    if (replaying && !recalling && !WAY_OUT[W_REPLAY]) {
      WAY_NOW = W_REPLAY;
      if (plan_pos < ex->plan_len) {
        idx = fit_step(ex, cur, &now, plan_pos);
        if (idx == EX_MAX_ACTS) {
          replaying = 0;
          think(ex, "does what won last time fit here?",
                "not at this step: there is nothing like it to do. I go back to exploring");
        } else {
          plan_pos++;
          path_len = 0u;
        }
      } else {
        replaying = 0;
        think(ex, "did doing what won last time end this level?",
              "no. What I saw on the way is kept; I go back to exploring");
      }
    }
    /* nothing else in hand: if its theories say where to be, lay a way there */
    if (idx == EX_MAX_ACTS && PLAN_LEN == 0u && !WATCHING && !WAY_OUT[W_PLAN] &&
        (aim_mask() != 0u || AIM_GONE != 0u)) {
      if (plan_from_theory(ex, &now) > 0u) {
        char a1[160];
        sprintf(a1, "%u steps, over the board, by what each act does to me and what will not let me in",
                PLAN_LEN);
        think(ex, "my theories say where I must be: how do I get there?", a1);
        continue;   /* take the first step of it on the next turn round */
      }
    }
    /* something untried right here */
    if (idx == EX_MAX_ACTS && N_UNTRIED[cur] > 0u) {
      int body = body_colour();
      unsigned best_d = EX_GOAL_UNKNOWN + 1u;
      static ex_sum_t cs[PL_COLOURS];
      int aim = body >= 0 && aim_mask() != 0u && N_GOALD[cur] != EX_GOAL_UNKNOWN && !WAY_OUT[W_AIM];
      WAY_NOW = aim ? W_AIM : W_EXPLORE;
      if (aim) colour_sums(&now, cs);
      for (i = 0u; i < N_NACT[cur]; i++) {
        if (N_NEXT[cur][i] == EX_NONE && N_FLAG[cur][i] == 0u) {
          unsigned d = EX_GOAL_UNKNOWN, k2 = KIND(N_ACT[cur][i]);
          if (!aim) {
            idx = i;
            break;
          }
          /* with a goal in sight: the untried step that brings it nearest */
          if (k2 < 8u && k2 != 6u && SHIFT_SEEN[k2][body] >= EX_SHIFT_SURE) {
            d = goal_distance(&now, cs, body, SHIFT_R[k2][body], SHIFT_C[k2][body]);
            if (ex->deaths > 0u && would_meet_mover(cs, body, k2)) {
              d = EX_GOAL_UNKNOWN;   /* it knows where that thing will be, and it kills */
              ex->stepped_clear++;
            }
          }
          if (d < best_d) {
            best_d = d;
            idx = i;
          }
        }
      }
      if (aim && idx != EX_MAX_ACTS && best_d == EX_GOAL_UNKNOWN && ex->deaths > 0u) {
        /*
         * Every way on from here walks into something whose step it can say. It
         * does not have to walk into it: a thing that moves will move on. So it
         * does something it knows changes nothing, and lets the thing pass. That
         * is what explaining the thing was for.
         */
        unsigned w;
        for (w = 0u; w < N_NACT[cur]; w++) {
          unsigned k3 = kind_slot(N_AKEY[cur][w]);
          if (N_FLAG[cur][w] != 0u || KIND_DID[k3] || !KIND_DONE[k3]) continue;
          idx = w;
          ex->waited++;
          think(ex, "must I walk into that thing?",
                "no: it moves, so it will move on. I do something that changes nothing and let it pass");
          break;
        }
      }
      if (!aim && idx != EX_MAX_ACTS) {
        /*
         * Among the acts equally open here (the lowest rank), nothing known tells
         * them apart, so the choice is uniform, as sm_decide guesses; the seed makes
         * it replayable. TIE_MODE 0: uniform among plain acts only, pointing in
         * order; 1: uniform over every act; 2: uniform over kinds, then over the
         * acts of the chosen kind.
         */
        unsigned tie[EX_MAX_ACTS], nt = 0u, rank0 = 3u, pick;
        uint64_t z = ((uint64_t)ex->actions + TIE_SEED) * 0x9E3779B97F4A7C15ULL + (uint64_t)cur;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z ^= z >> 31;
        for (i = 0u; i < N_NACT[cur]; i++) {
          unsigned rk;
          if (N_NEXT[cur][i] != EX_NONE || N_FLAG[cur][i] != 0u) continue;
          if (TIE_MODE == 0u && KIND(N_ACT[cur][i]) == 6u) continue;
          rk = act_rank(cur, i);
          if (rk < rank0) { rank0 = rk; nt = 0u; }
          if (rk == rank0) tie[nt++] = i;
        }
        if (TIE_MODE == 0u && KIND(N_ACT[cur][idx]) == 6u) nt = 0u;
        if (nt > 1u && TIE_MODE == 2u) {
          uint32_t kinds[EX_MAX_ACTS];
          unsigned nk = 0u, a, b, inkind[EX_MAX_ACTS], ni = 0u;
          for (a = 0u; a < nt; a++) {
            for (b = 0u; b < nk && kinds[b] != N_AKEY[cur][tie[a]]; b++) {}
            if (b == nk) kinds[nk++] = N_AKEY[cur][tie[a]];
          }
          pick = (unsigned)(z % nk);
          for (a = 0u; a < nt; a++) {
            if (N_AKEY[cur][tie[a]] == kinds[pick]) inkind[ni++] = tie[a];
          }
          idx = inkind[(z >> 16) % ni];
        } else if (nt > 1u) {
          idx = tie[z % nt];
        }
      }
      path_len = 0u;
    }
    /* or the way it is already following */
    if (idx == EX_MAX_ACTS && path_pos < path_len && PATH_NODE[path_pos] == cur) {
      idx = PATH[path_pos];
    }
    /* or a fresh way to the nearest untried thing */
    if (idx == EX_MAX_ACTS) {
      double like = 0.0;
      unsigned far = 0u;
      path_len = plan_to_untried(ex, cur, &like, &far);
      path_pos = 0u;
      if (path_len > 0u) idx = PATH[0];
      if (path_len > 0u && ex->wins_seen > 0u && like > 0.0) {
        char a1[160];
        ex->guided++;
        sprintf(a1, "one %u step%s away, %.0f%% like what winning changed; I go there first",
                far, far == 1u ? "" : "s", like * 100.0);
        think(ex, "which untried situation is most like winning?", a1);
      }
    }
    /* nothing untried can be reached from here: begin the level again */
    if (idx == EX_MAX_ACTS) {
      if (cur != start && g->reset != 0) {
        g->reset(g, &now);
        ex->actions++;
        ex->resets++;
        cur = node_of(ex, g, &now);
        path_len = 0u;
        continue;
      }
      if (ex->predicted > 0u && (!took_back || N_COUNT > took_back_at)) {
        /* nothing untried is left, but some things were only predicted to do
           nothing. Predictions are not proofs: they are taken back, and tried */
        unsigned n, i2;
        for (n = 0u; n < N_COUNT; n++) {
          for (i2 = 0u; i2 < N_NACT[n]; i2++) {
            if (N_FLAG[n][i2] == EX_FLAG_PREDICTED) {
              N_FLAG[n][i2] = 0u;
              N_NEXT[n][i2] = EX_NONE;
              N_UNTRIED[n]++;
            }
          }
        }
        PLACES_OPEN = 1u;   /* the speculations were not borne out: none again this level */
        took_back = 1;
        took_back_at = N_COUNT;   /* again only once something new has been found */
        STOOD_OFF = 1u;
        ex->unpredicted++;
        think(ex, "have I really tried everything?",
              "no: some places I only predicted would do nothing. I try them now");
        continue;
      }
      if (ON(M_DEATHS) && (N_COUNT > death_retry_at || death_retries_here < EX_DEATH_RETRIES)) {
        /* a death is pinned on the last step taken, but running out of time kills
           whatever step came last. Before giving up, those steps are tried again */
        unsigned n, i2, freed = 0u;
        for (n = 0u; n < N_COUNT; n++) {
          for (i2 = 0u; i2 < N_NACT[n]; i2++) {
            if (N_FLAG[n][i2] == 1u) {
              N_FLAG[n][i2] = 0u;
              N_NEXT[n][i2] = EX_NONE;
              N_UNTRIED[n]++;
              freed++;
            }
          }
        }
        death_retry_at = N_COUNT;
        death_retries_here++;
        if (freed > 0u) {
          ex->death_retries++;
          think(ex, "was it really that step that killed me?",
                "perhaps time ran out instead. I try those steps again");
          continue;
        }
      }
      if (ON(M_UNMASK) && !unmasked && N_COUNT < EX_FEW_SITUATIONS && any_restless()) {
        /* hardly anywhere to go, yet much of the picture was left out as moving by
           itself: perhaps what was left out was the very thing it moves. Everything
           counts again, for the rest of this level */
        memcpy(MASK_KEPT, MASK, sizeof MASK);
        {
          unsigned rr, cc;
          for (rr = 0u; rr < PL_SIZE; rr++) {
            for (cc = 0u; cc < PL_SIZE; cc++) {
              if (FROM_RESTLESS[rr][cc]) MASK[rr][cc] = 0u;
            }
          }
        }
        forget_changes();
        unmasked = 1;
        NO_MASKING = 1u;
        ex->rebuilds++;
        forget_level();
        start = node_of(ex, g, &LEVEL_START);
        cur = node_of(ex, g, &now);
        path_len = 0u;
        think(ex, "why is there nowhere to go?",
              "perhaps what I stopped watching was what I move. I watch everything again");
        continue;
      }
      if (ON(M_WINDOW) && BUILT_OVERFLOW && ACT_WINDOW < 32u * (EX_MAX_ACTS - EX_KEEP_ACTS)) {
        /* it has not tried everything: there were more things than it held */
        ACT_WINDOW += EX_MAX_ACTS - EX_KEEP_ACTS;
        BUILT_OVERFLOW = 0u;
        ex->windows++;
        ex->rebuilds++;
        forget_level();
        start = node_of(ex, g, &LEVEL_START);
        cur = node_of(ex, g, &now);
        path_len = 0u;
        took_back = 0;
        think(ex, "have I tried everything there was?",
              "no: there were more things than I held. I look further along");
        continue;
      }
      if (ON(M_REDRAW) && fresh_starts < EX_FRESH_STARTS) {
        /* its map says everything is tried, but a map drawn with parts of the picture
           left out can join situations that differ. The map is drawn again */
        fresh_starts++;
        ex->fresh_starts++;
        STOOD_OFF = 1u;
        ex->rebuilds++;
        forget_level();
        start = node_of(ex, g, &LEVEL_START);
        cur = node_of(ex, g, &now);
        path_len = 0u;
        took_back = 0;
        death_retry_at = 0u;
        think(ex, "is my map of this level wrong?",
              "perhaps: I draw it again from what I see");
        continue;
      }
      ex->exhausted = 1;   /* from the beginning too: what it can do here does not end it */
      think(ex, "is there anything left I have not tried?",
            "no: nothing I can do from the beginning of this level ends it");
      break;
    }

    code = N_ACT[cur][idx];
    kind = KIND(code);
    if (kind == 6u) {
      N_PCOL[cur][idx] = now.c[PY(code)][PX(code)];
      N_PRANK[cur][idx] = (unsigned char)thing_rank(&now, PX(code), PY(code));
    }
    {
      /* what was true of this act, before it: the facts the theories are made of */
      static ex_sum_t os[PL_COLOURS], ls[PL_COLOURS];
      int body = body_colour();
      unsigned v;
      memset(&OBS, 0, sizeof OBS);
      colour_sums(&now, os);
      colour_sums(&LEVEL_START, ls);
      for (v = 0u; v < PL_COLOURS; v++) {
        if (os[v].count == 0u && ls[v].count > 0u) OBS.gone |= (uint16_t)(1u << v);
      }
      if (body >= 0 && kind != 6u) {
        static unsigned tmp[PL_COLOURS];
        if (stepped_onto(&now, os, body, kind, tmp)) {
          OBS.onto = (uint16_t)STEPPED_MASK;
          for (v = 0u; v < PL_COLOURS; v++) {
            if ((STEPPED_MASK & (1u << v)) && STEPPED_CELLS[v] >= os[v].count) OBS.last |= (uint16_t)(1u << v);
          }
        }
      }
      if (kind == 6u) OBS.point = (uint16_t)(1u << now.c[PY(code)][PX(code)]);
      OBS.match = (unsigned char)(MATCH_VARIES && N_MATCHD[cur] == 0u);
      /* the same act, said in roles: this is what can carry to another game */
      work_out_roles(&now);
      for (v = 0u; v < PL_COLOURS; v++) {
        if (OBS.onto & (1u << v)) OBS.onto_r |= ROLE_OF[v];
        if (OBS.last & (1u << v)) OBS.last_r |= ROLE_OF[v];
        if (OBS.gone & (1u << v)) OBS.gone_r |= ROLE_OF[v];
        if (OBS.point & (1u << v)) OBS.point_r |= ROLE_OF[v];
      }
    }
    LAST_CODE = (unsigned short)code;
    outcome = g->act(g, kind, PX(code), PY(code), &next);
    OBS.ended = (unsigned char)(outcome == 1 || outcome == 2);
    {
      unsigned before = THEORY.widenings;
      unsigned nodes_before = N_COUNT, theories_before = theories_left();
      (void)en_observe(&THEORY, &OBS);
      /* something new: a situation it had not met, a theory ruled out, or an ending */
      (void)way_judge(ex, N_COUNT > nodes_before || theories_left() < theories_before ||
                          outcome == 1 || outcome == 2);
      theory_aims();
      if (THEORY.widenings > before) {
        think(ex, "why did that end the level, when nothing I can say explains it?",
              "every single fact I have was ruled out, so my language was too poor: I now think in pairs of facts");
      }
    }
    ex->actions++;
    if (kind < 8u) ex->kind_tried[kind]++;
    if (kind == 6u) ex->point_tried[now.c[PY(code)][PX(code)]]++;

    if (outcome == 2 || outcome == 1) {
      {
        /* what did winning change? The situation it won from, set against how the
           level began, is added to what it knows winning looks like */
        unsigned k, top[3] = {0u, 0u, 0u}, tv[3] = {0u, 0u, 0u};
        char a1[200];
        for (k = 0u; k < 256u; k++) {
          unsigned v = N_CHG[cur][k], t;
          ex->win_change[k] += (double)v;
          for (t = 0u; t < 3u; t++) {
            if (v > tv[t]) {
              unsigned u;
              for (u = 2u; u > t; u--) { tv[u] = tv[u - 1u]; top[u] = top[u - 1u]; }
              tv[t] = v;
              top[t] = k;
              break;
            }
          }
        }
        ex->wins_seen++;
        {
          static ex_sum_t ws[PL_COLOURS];
          int body = body_colour();
          if (body >= 0 && kind != 6u) {
            colour_sums(&now, ws);
            if (stepped_onto(&now, ws, body, kind, WIN_ONTO)) WINS_ONTO++;
          }
        }
        if (tv[0] == 0u) {
          sprintf(a1, "the ending came from how the level began: the last thing I did was the key");
        } else {
          int at = sprintf(a1, "colour %u became %u in %u cells", top[0] >> 4, top[0] & 15u, tv[0]);
          if (tv[1]) at += sprintf(a1 + at, "; %u became %u in %u", top[1] >> 4, top[1] & 15u, tv[1]);
          if (tv[2]) sprintf(a1 + at, "; %u became %u in %u", top[2] >> 4, top[2] & 15u, tv[2]);
        }
        think(ex, "what did winning change?", a1);
      }
      N_FLAG[cur][idx] = 2u;
      if (N_UNTRIED[cur] > 0u) N_UNTRIED[cur]--;
      if (replaying) ex->plans_won++;   /* it won while following the plan */
      {
        /* how did I win? The shortest way I know from where the level began to
           where I won from, then the winning step: that is the plan to try next */
        unsigned len = route(start, cur), i, n = 0u;
        char a1[160];
        if (len + 1u <= 256u && (len > 0u || cur == start)) {
          for (i = 0u; i < len; i++) {
            int node = PATH_NODE[i];
            unsigned ai = PATH[i];
            ex->plan_kind[n] = (unsigned char)KIND(N_ACT[node][ai]);
            ex->plan_colour[n] = N_PCOL[node][ai];
            ex->plan_rank[n] = N_PRANK[node][ai];
            n++;
          }
          ex->plan_kind[n] = (unsigned char)kind;
          ex->plan_colour[n] = N_PCOL[cur][idx];
          ex->plan_rank[n] = N_PRANK[cur][idx];
          n++;
          ex->plan_len = n;
          sprintf(a1, "in %u step%s, by the shortest way I know; I keep that as a plan", n,
                  n == 1u ? "" : "s");
          think(ex, "what is the least I needed to win?", a1);
        }
      }
      {
        /* the shortest way it knows through this level, kept for the next run */
        unsigned L = ex->levels_done, len = route(start, cur), i2;
        if (L < EX_MAX_LEVELS && len + 1u <= EX_ROUTE_MAX && (len > 0u || cur == start) &&
            (KNOWN_LEN[L] == 0u || len + 1u < KNOWN_LEN[L])) {
          for (i2 = 0u; i2 < len; i2++) KNOWN_ROUTE[L][i2] = N_ACT[PATH_NODE[i2]][PATH[i2]];
          KNOWN_ROUTE[L][len] = (unsigned short)code;
          KNOWN_LEN[L] = len + 1u;
        }
      }
      if (ex->levels_done < EX_MAX_LEVELS) {
        ex->level_actions[ex->levels_done] = ex->actions - level_began;
        ex->situations[ex->levels_done] = N_COUNT;
      }
      ex->levels_done++;
      level_began = ex->actions;
      if (outcome == 2) {
        ex->won = 1;
        break;
      }
      now = next;
      new_level(ex, &now);
      start = node_of(ex, g, &now);
      cur = start;
      path_len = 0u;
      ex->thoughts = 0u;
      took_back = 0;
      death_retry_at = 0u;
      STOOD_OFF = 0u;
      ways_begin();   /* a new level: every way of going is worth trying again */
      PLAN_LEN = 0u;
      puzzles_begin();
      recalling = ON(M_RECALL) && ex->levels_done < EX_MAX_LEVELS && KNOWN_LEN[ex->levels_done] > 0u;
      recall_pos = 0u;
      if (recalling) replaying = 0;
      fresh_starts = 0u;
      death_retries_here = 0u;
      unmasked = 0;
      NO_MASKING = 0u;
      /* a new level is a new layout: what a place did before says nothing about it now */
      forget_places();
      PLACES_OPEN = 0u;
      ACT_WINDOW = 0u;
      BUILT_OVERFLOW = 0u;
      if (ex->thinking != 0) fprintf(ex->thinking, "    -- level %u --\n", ex->levels_done + 1u);
      if (ON(M_REPLAY) && ex->plan_len > 0u) {
        char a1[160];
        replaying = 1;
        plan_pos = 0u;
        ex->plans_tried++;
        sprintf(a1, "maybe: I try the %u steps that won last time first, matched to what is here",
                ex->plan_len);
        think(ex, "this level is new. Can I do what won the last one?", a1);
      }
      continue;
    }
    if (outcome == 3) {
      /* it died and the level began again: that way is marked, not walked again */
      if (N_NEXT[cur][idx] == EX_NONE && N_UNTRIED[cur] > 0u) N_UNTRIED[cur]--;
      N_FLAG[cur][idx] = 1u;
      N_NEXT[cur][idx] = start;
      ex->deaths++;
      replaying = 0;
      recalling = 0;
      now = next;
      cur = node_of(ex, g, &now);
      path_len = 0u;
      continue;
    }

    if (unmasked == 1 && N_COUNT > EX_FEW_SITUATIONS * 4u) {
      /* watching everything only multiplied the situations: what was left out
         really did move by itself. It is left out again, and that is final */
      memcpy(MASK, MASK_KEPT, sizeof MASK);
      unmasked = 2;
      NO_MASKING = 0u;
      ex->rebuilds++;
      forget_level();
      start = node_of(ex, g, &LEVEL_START);
      now = next;
      cur = node_of(ex, g, &now);
      path_len = 0u;
      think(ex, "did watching everything help?",
            "no: it only moved by itself. I leave it out again");
      continue;
    }
    /* an ordinary step: what ticks by itself is left out, then the step is remembered */
    {
      /* whatever moved without it having done so is a puzzle before it is a nuisance */
      unsigned pr, pc;
      if (ON(M_CURIOUS) && moved_by_itself(&now, &next, code, &pr, &pc)) {
        be_confused(ex, next.c[pr][pc], pr, pc);
      }
      watch_puzzles(ex, &next);
    }
    if (notice_ticks(ex, &now, &next, code) > 0u) {
      char a1[160];
      sprintf(a1, "it ticks by itself (%u cells so far); I stop counting it as a difference",
              ex->clock_cells);
      think(ex, "why does part of the picture change whatever I do?", a1);
      ex->rebuilds++;
      forget_level();
      start = node_of(ex, g, &LEVEL_START);
      now = next;
      cur = node_of(ex, g, &now);
      path_len = 0u;
      continue;
    }
    learn_moving(kind, &now, &next);
    nb = node_of(ex, g, &next);
    if (kind < 8u && ex->kind_tried[kind] == 1u) {
      char q[64], a1[160];
      unsigned r, c, n_changed = 0u;
      for (r = 0u; r < next.h; r++) {
        for (c = 0u; c < next.w; c++) n_changed += (!MASK[r][c] && now.c[r][c] != next.c[r][c]) ? 1u : 0u;
      }
      if (kind == 6u) sprintf(q, "what happens when I point at something?");
      else sprintf(q, "what does action %u do?", kind);
      if (n_changed == 0u) sprintf(a1, "nothing I can see, here");
      else sprintf(a1, "it changed %u cell%s", n_changed, n_changed == 1u ? "" : "s");
      think(ex, q, a1);
    }
    if (N_NEXT[cur][idx] == EX_NONE && N_UNTRIED[cur] > 0u) N_UNTRIED[cur]--;
    N_NEXT[cur][idx] = nb;
    if (nb == cur) {
      if (kind < 8u) ex->kind_nothing[kind]++;
      if (kind == 6u) ex->point_nothing[now.c[PY(code)][PX(code)]]++;
    }
    {
      unsigned k = kind_slot(N_AKEY[cur][idx]);
      KIND_DONE[k] = 1u;
      if (nb != cur) KIND_DID[k] = 1u;
    }
    if (kind == 6u) {
      /* it had hold of something and has just said where: that settles the reach */
      if (HOLDING) {
        unsigned char *law = sent_law(HOLD_COLOUR, (int)PY(code) - (int)HOLD_Y,
                                      (int)PX(code) - (int)HOLD_X);
        if (law != 0 && (PX(code) != HOLD_X || PY(code) != HOLD_Y)) {
          *law = (unsigned char)(nb != cur ? 1u : 2u);
          if (nb != cur) ex->sent_learned++;
        }
      }
      /* a pointing that changed something, where nothing moved far, is taking hold */
      HOLDING = (nb != cur);
      HOLD_X = PX(code);
      HOLD_Y = PY(code);
      HOLD_COLOUR = now.c[PY(code)][PX(code)];
    }
    if (kind == 6u) {
      unsigned pl = place_of(code, &now);
      if (nb == cur) {
        place_nothing(pl);
      } else {
        /*
         * It had judged this place to do nothing, having seen it do nothing more
         * than once, and here it has done something. So the question behind that
         * judgement -- does pointing here do the same thing every time? -- is
         * answered no for this world: what a place does can depend on how things
         * stand. It stops speaking for places it has not tried, this level.
         */
        if (PLACE_SEEN[pl] >= 2u && !PLACES_OPEN) {
          PLACES_OPEN = 1u;
          ex->places_reopened++;
          think(ex, "does pointing at a place do the same thing every time?",
                "not here: one I had taken to do nothing has just done something. I stop leaving places unspent");
        }
        PLACE_DOES[pl] = 1u;
      }
    }
    if (path_len > 0u && path_pos < path_len && PATH_NODE[path_pos] == cur) {
      path_pos++;
      if (path_pos < path_len && PATH_NODE[path_pos] != nb) path_len = 0u;   /* not as it was */
    }
    now = next;
    cur = nb;
  }

  if (!ex->won && ex->levels_done < EX_MAX_LEVELS) ex->situations[ex->levels_done] = N_COUNT;
  return SM_OK;
}

void ex_report(FILE *out, const ex_explorer_t *ex) {
  unsigned k;
  if (out == 0 || ex == 0) return;
  fprintf(out, "  levels ended: %u; actions %u; %s\n", ex->levels_done, ex->actions,
          ex->won ? "the game is won" : "not won");
  for (k = 0u; k < ex->levels_done && k < EX_MAX_LEVELS; k++) {
    fprintf(out, "    level %u: %u actions, %u situations met\n", k + 1u, ex->level_actions[k],
            ex->situations[k]);
  }
  if (!ex->won && ex->levels_done < EX_MAX_LEVELS) {
    fprintf(out, "    in the level it stopped in: %u situations met\n",
            ex->situations[ex->levels_done]);
  }
  if (ex->exhausted) {
    fprintf(out, "  it ran out of anything untried it could reach, from the beginning too\n");
  }
  fprintf(out, "  died %u times; began a level again itself %u times\n", ex->deaths, ex->resets);
  (void)en_report(&THEORY, out);
  if (OFF_MASK != 0u) {
    unsigned m;
    fprintf(out, "  played with parts of itself switched off:");
    for (m = 0u; m < M_COUNT; m++) {
      if (!ON(m)) fprintf(out, " %s", MECH_NAME[m]);
    }
    fprintf(out, "\n");
  }
  if (ex->runs_before > 0u || ex->recalled > 0u) {
    fprintf(out, "  remembered from %u earlier run%s (best before: %u levels); walked %u steps it remembered\n",
            ex->runs_before, ex->runs_before == 1u ? "" : "s", ex->best_before, ex->recalled);
  }
  if (ex->plans_laid > 0u) {
    fprintf(out, "  ways it reckoned over the board from what it worked out: %u laid, %u steps walked, %u dropped when it was not where it said\n",
            ex->plans_laid, ex->plan_steps, ex->plans_broke);
  }
  if (ex->puzzles > 0u) {
    unsigned w;
    fprintf(out, "  how I ask about what I cannot explain:");
    for (w = 0u; w < Q_WAYS; w++) {
      fprintf(out, " %s: %s;", ASK_NAME[w],
              ASK_STATE[w] == 0u ? "never tried" : (ASK_STATE[w] == 1u ? "has answered" : "did not answer"));
    }
    fprintf(out, "\n  I said where a thing would be and was wrong %u times, and took the explaining back\n",
            ex->predictions_broken);
  }
  if (ex->puzzles > 0u) {
    fprintf(out, "  things that moved when I had not moved them: %u; I watched them and can say what %u of them do; %u I could not\n",
            ex->puzzles, ex->puzzles_explained, ex->puzzles_given_up);
  }
  if (ex->sent_learned > 0u) {
    unsigned v, goes = 0u, cannot = 0u, dy, dx;
    for (v = 0u; v < PL_COLOURS; v++) {
      for (dy = 0u; dy < EX_REACH; dy++) {
        for (dx = 0u; dx < EX_REACH; dx++) {
          if (SENT_LAW[v][dy][dx] == 1u) goes++;
          else if (SENT_LAW[v][dy][dx] == 2u) cannot++;
        }
      }
    }
    fprintf(out, "  where a thing it holds may be sent: %u ways it found it goes, %u it found it does not\n",
            goes, cannot);
  }
  if (ex->ways_dropped > 0u) {
    fprintf(out, "  gave up a way of going that was getting it nowhere %u times, and opened them all again %u times\n",
            ex->ways_dropped, ex->ways_reopened);
  }
  if (ex->fresh_starts > 0u) {
    fprintf(out, "  drew its map of a level again %u times\n", ex->fresh_starts);
  }
  if (ex->death_retries > 0u) {
    fprintf(out, "  doubted %u times that a death was the last step's doing\n", ex->death_retries);
  }
  if (ex->stood_back > 0u) {
    int body = body_colour();
    fprintf(out, "  found what it moves (colour %d); held back %u steps back to where it had stood\n",
            body, ex->stood_back);
  }
  if (ex->predicted > 0u) {
    fprintf(out, "  did not spend %u pointings it predicted would do nothing; took that back %u times\n",
            ex->predicted, ex->unpredicted);
  }
  if (ex->plans_tried > 0u) {
    fprintf(out, "  began %u level%s by doing what won the last one; that won %u of them\n",
            ex->plans_tried, ex->plans_tried == 1u ? "" : "s", ex->plans_won);
  }
  if (ex->clock_cells > 0u) {
    fprintf(out, "  found %u cells that tick by themselves, and left them out\n", ex->clock_cells);
  }
  fprintf(out, "  what never did anything:");
  for (k = 1u; k < 8u; k++) {
    if (ex->kind_tried[k] >= 4u && ex->kind_nothing[k] == ex->kind_tried[k]) {
      fprintf(out, " action %u;", k);
    }
  }
  for (k = 0u; k < PL_COLOURS; k++) {
    if (ex->point_tried[k] >= 4u && ex->point_nothing[k] == ex->point_tried[k]) {
      fprintf(out, " pointing at colour %u;", k);
    }
  }
  fprintf(out, "\n");
}
