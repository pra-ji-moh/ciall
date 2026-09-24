/*
 * smarsh_goal.h -- what ends a level, said of the board, learned by elimination.
 *
 * What ends a level is held as a set of goals, every one possible at the start,
 * each a fact about the board as it stands after an act:
 *
 *     NONE v        no cell of colour v is left (there was some when the level began)
 *     AGAINST v     the body is against colour v, or stands where v was
 *     EQUAL v w     there are as many cells of v as of w
 *     BESIDE v w    every cell of v has a cell of w right beside it
 *     MIRROR k      the board is its own mirror image, side to side (0) or top to bottom (1)
 *     ACT k         the act just done was k
 *     ABOVE v w     somewhere, colour v sits directly above colour w, only ground between
 *     LEFT v w      somewhere, colour v sits directly left of colour w, only ground between
 *     ABOVE2 v w    that holds in two places or more: an arrangement shown, and a copy of it
 *     LEFT2 v w     the same, side by side
 *     CLOSE_... v w the same four, but only where at most two cells of ground lie between:
 *                   near enough to be one arrangement, not two things far apart
 *     ONCE, TWICE, NEVER v w
 *                   the close arrangement in exactly one place, exactly two (the example
 *                   and one copy, no more), or nowhere: what is not there says as much
 *                   as what is
 *
 * The last four are how the board is laid out, one part against another: the goal
 * of many a level is to make the work look like an example drawn beside it.
 *
 * and, when no single one of them will do, a pair of them both holding.
 *
 * An act that does not end the level rules out every goal that holds on the board
 * it left: had that been the goal, the level would have ended. An act that ends the
 * level rules out every goal that does not hold on the board it left. Nothing is
 * weighed: a goal is ruled out or it is not.
 *
 * The board an ending is judged on is the board as the winning act left it, before
 * the next level is drawn over it (the bridge sends it apart: END). Before that was
 * sent, no ending had ever been seen by the child at all: only the next level.
 *
 * When an ending rules out every goal it holds, it does not stop there. It holds, at
 * once, every account of why:
 *   - the goal changed with the level: goals judged on this level's evidence alone
 *   - it takes two facts together: pairs that held at every ending and never both at once
 *     after an act that ended nothing
 *   - it ends only on the act that ended it: the board has to be right AND that act done
 *     (a submit, a check). Goals, one fact or two, judged only on the boards where that
 *     act was done and nothing ended; a board where it was not done says nothing
 * and goes on ruling those out as it plays. Only facts that were ever false where it
 * matters are put into pairs: a fact true everywhere can split nothing.
 *
 * What survives is carried from level to level. It chooses among what survives
 * uniformly, and plans towards it: reaching a goal without the level ending rules it
 * out, and so every plan is also an experiment.
 *
 * Not one kind of sense, several (after Gardner: intelligence is not one thing). Each
 * fact belongs to a domain, and each domain is its own account of what a level wants:
 *
 *     space     where things are, one against another   BESIDE ABOVE LEFT and the close forms
 *     number    how many                                 NONE EQUAL and ONCE TWICE NEVER
 *     body      where the thing it moves is              AGAINST
 *     time      what is done, and when                   ACT, and goals that end only on an act
 *     pattern   a shape shown and a copy of it           MIRROR and the two-place forms
 *     others    things that act by themselves            (no words yet)
 *
 * It chooses a goal uniformly over the kinds of sense that still hold one (a goal of
 * two facts is of both its kinds), then uniformly within: a sense with few words is
 * not drowned by one with many. And at each ending it says which senses still had an
 * account and which were left with none: which kind of sense the level found wanting.
 */
#ifndef SMARSH_GOAL_H
#define SMARSH_GOAL_H

#include <stdint.h>
#include <stdio.h>

#include "smarsh_play.h"

enum { GO_NONE = 0, GO_AGAINST = 1, GO_EQUAL = 2, GO_BESIDE = 3, GO_MIRROR = 4, GO_ACT = 5,
       GO_ABOVE = 6, GO_LEFT = 7, GO_ABOVE2 = 8, GO_LEFT2 = 9,
       GO_CABOVE = 10, GO_CLEFT = 11, GO_CABOVE2 = 12, GO_CLEFT2 = 13,
       GO_ABOVE_ONCE = 14, GO_ABOVE_TWICE = 15, GO_ABOVE_NEVER = 16,
       GO_LEFT_ONCE = 17, GO_LEFT_TWICE = 18, GO_LEFT_NEVER = 19, GO_TYPES = 20 };

#define GO_FACTS (GO_TYPES * 256u)         /* type * 256 + v * 16 + w */
#define GO_WORDS (GO_FACTS / 64u)
#define GO_PAIRS 4096u
#define GO_SEEN 4096u                      /* boards after acts that ended nothing, kept */
#define GO_ENDS 64u                        /* boards at endings, kept */

typedef struct {
  unsigned p, q;                           /* one fact, or two; q == GO_FACTS when one */
  unsigned act;                            /* and the act that must be done, or GO_ANY_ACT */
} go_goal_t;

#define GO_ANY_ACT 0xFFu

enum { GD_SPACE = 0, GD_NUMBER = 1, GD_BODY = 2, GD_TIME = 3, GD_PATTERN = 4, GD_OTHERS = 5, GD_DOMAINS = 6 };

typedef struct {
  uint64_t alive[GO_WORDS];                /* single facts still possible as what ends a level */
  uint64_t ever[GO_WORDS];                 /* facts that have meant something here at all */
  unsigned pair_p[GO_PAIRS], pair_q[GO_PAIRS], pair_act[GO_PAIRS];
  unsigned char pair_alive[GO_PAIRS];
  unsigned n_pairs, pairs_dropped;
  uint64_t seen[GO_SEEN][GO_WORDS];        /* what held after each act that ended nothing */
  unsigned char seen_level[GO_SEEN];
  unsigned seen_next, seen_count;
  uint64_t ends[GO_ENDS][GO_WORDS];        /* what held at each ending */
  unsigned n_ends;
  pl_frame_t start;                        /* the level as it began */
  unsigned level;
  unsigned endings, from_board, from_guess, blanks, relevelled, widened, empty_after, triggered;
  unsigned domain_blank[GD_DOMAINS];       /* endings that left a sense with no single fact standing */
} go_world_t;

void go_begin(go_world_t *g);
/* a level begins: the board it begins with */
void go_level(go_world_t *g, const pl_frame_t *start, unsigned level);
/* an act that ended nothing, and the board it left */
void go_saw(go_world_t *g, unsigned act, const pl_frame_t *after, int body);
/* an act that ended the level; seen: 1 if the board is the one the engine drew, 0 if imagined */
void go_ended(go_world_t *g, unsigned act, const pl_frame_t *after, int body, int seen);

unsigned go_count(const go_world_t *g);    /* goals still standing, singles and pairs */
/* one of the goals still standing, chosen uniformly by z */
int go_pick(const go_world_t *g, uint64_t z, go_goal_t *out);
/* does the goal's board part hold on this board (the act, if it has one, is the planner's to do) */
int go_holds(const go_world_t *g, const go_goal_t *goal, const pl_frame_t *f, int body);
int go_standing(const go_world_t *g, const go_goal_t *goal);
void go_words(const go_goal_t *goal, char *out);
void go_report(const go_world_t *g, FILE *out);
/* which kinds of sense a goal draws on: a mask of 1 << GD_... */
unsigned go_domains(const go_goal_t *goal);
/* goals still standing that draw on this kind of sense */
unsigned go_domain_count(const go_world_t *g, unsigned domain);
const char *go_domain_name(unsigned domain);
/* the k-th goal still standing, in a fixed order */
int go_nth(const go_world_t *g, unsigned k, go_goal_t *out);
/* every goal still standing, in that order, in one pass; returns how many (at most cap) */
unsigned go_list(const go_world_t *g, go_goal_t *out, unsigned cap);
/*
 * A goal's kind, without its colours: the types of its facts and its act, e.g. "15/16/5"
 * (exactly twice, and never, then act 5). Colours mean nothing in another game; kinds
 * can: this is what is carried from game to game.
 */
void go_kind(const go_goal_t *goal, char *out);

#endif
