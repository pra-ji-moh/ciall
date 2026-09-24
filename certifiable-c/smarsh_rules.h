/*
 * smarsh_rules.h -- what each act does to each kind of thing on the board, learned
 * by elimination, and used to say what the next picture will be.
 *
 * A THING is a patch of one colour, joined side to side. What the child can say
 * about it without naming it, three ways at once, from the narrowest to the widest:
 * its colour and exact shape; its colour and how big it is, in classes; its colour.
 * It learns a rule at all three, and says what will happen by the narrowest that has
 * settled to one rule: as wide a rule as the evidence allows, and no wider.
 * Things of a kind behave alike, so what is learned of one is learned of all.
 *
 * For each (kind, act) it holds every rule its language can say:
 *
 *     nothing            the thing is where it was
 *     go(dr, dc)         every cell of it moves by that much
 *     gone               it is no longer there
 *     grew / shrank      it has more or fewer cells
 *     recoloured(v)      its cells are colour v now
 *
 * and at the start every one of them is possible. An act is done; what actually
 * happened to each thing rules out every rule that says otherwise. What is left is
 * what it knows, and log2 of how many are left is how much it still does not know.
 * Nothing is counted and nothing is weighed: a rule is ruled out or it is not.
 *
 * Where one rule is left for a (kind, act), it can say what that act will do before
 * doing it. Where more than one is left, it cannot, and says so. Everything it says
 * is checked against what happens, and being wrong rules more out.
 */
#ifndef SMARSH_RULES_H
#define SMARSH_RULES_H

#include <stdint.h>
#include <stdio.h>

#include "smarsh_play.h"

#define RU_KINDS 1024u
#define RU_WAYS 3u                     /* colour and shape; colour and size; colour */
#define RU_ACTS 8u
#define RU_REACH 8                     /* go(dr, dc), each -8..8: a thing in these games steps by a whole block */
#define RU_SIDE (2u * RU_REACH + 1u)
#define RU_MOVES (RU_SIDE * RU_SIDE)   /* go(0, 0) is "nothing" */
/* the moves; gone; changed size; recoloured; then each move again as "unless something is in the way" */
#define RU_WORDS ((2u * RU_MOVES + 3u + 63u) / 64u)
#define RU_MAX_THINGS 256u
#define RU_LOG 320u                    /* pictures kept: a whole game at the usual budget */
#define RU_NO_ACT 255u                 /* this picture does not follow from the one before */
#define RU_EV 65536u                   /* sightings kept: what each thing could have done, and what it touched */
#define RU_CONDS 4096u                 /* accounts of where a rule breaks, held at once */
#define RU_CUT_MAX 1024u               /* sightings looked back over when a rule dies */

/*
 * Where a rule breaks.
 *
 * When every rule for a kind and an act has been ruled out, it does not take the
 * world's word for it and stop. It looks back over every time it saw that kind do
 * that act, and asks what was different about the times that went one way and the
 * times that went the other. Each single fact that splits them, so that some rule
 * still stands on each side, is an account of where the rule breaks, and every such
 * account is held at once until the world rules it out:
 *
 *   TOUCH v    it does one thing when touching colour v, another when not
 *              (one thing's rule depending on another thing)
 *   NTH m      it did one thing the first times and another from the m-th time on
 *              (something it cannot see changed: a switch, a count)
 *   LEVEL L    it changed when the level changed
 *   MISREAD    every other time agrees; that one sighting was read wrong
 *              (it does not trust its own eyes over everything else it has seen)
 *   AHEAD d v  what lies directly beside it on side d is colour v
 *              (blocked by v, pushed by v, let through by v)
 *   CYCLE p r  it is the r-th of every p times
 *              (something unseen that goes round: alternates, counts in threes)
 *
 * Which of these families it may use is itself something it sets, from what its
 * study finds between games (ru_families).
 */
enum { RU_F_TOUCH = 0, RU_F_NTH = 1, RU_F_LEVEL = 2, RU_F_MISREAD = 3, RU_F_AHEAD = 4, RU_F_CYCLE = 5,
       RU_FACTS = 6 };
#define RU_FAMILIES_FIRST ((1u << RU_F_TOUCH) | (1u << RU_F_NTH) | (1u << RU_F_LEVEL) | (1u << RU_F_MISREAD))
#define RU_CROP 12u                    /* the picture around a sighting, kept for study: RU_CROP square */

typedef struct {
  unsigned colour, size, cells;
  unsigned top, left, bottom, right;
  unsigned row, col;                   /* where its middle is */
} ru_thing_t;

typedef struct {
  uint16_t k[RU_WAYS];                 /* its kind, each way of saying it */
  uint16_t nth[RU_WAYS];               /* how many times before this that act met that kind */
  uint16_t touch;                      /* the colours it was touching */
  uint16_t ahead[4];                   /* the colours beside it: above, below, left, right */
  uint8_t crop[RU_CROP][RU_CROP];      /* the picture around it, centred on it; 16 is off the board */
  uint16_t colour, cells;              /* to say which it was, in words */
  uint8_t act, level;
  uint64_t could[RU_WORDS];            /* every rule this sighting allowed */
} ru_ev_t;

typedef struct {
  uint16_t k, arg;
  uint16_t next;                       /* the next account of the same (way, kind, act), plus one */
  uint8_t way, act, fact, alive;
  uint64_t yes[RU_WORDS], no[RU_WORDS];   /* the rules still standing where the fact holds, and where not */
  unsigned said, right;
} ru_cond_t;

typedef struct {
  uint64_t left[RU_WAYS][RU_KINDS][RU_ACTS][RU_WORDS];   /* a bit per rule still possible */
  unsigned seen[RU_WAYS][RU_KINDS][RU_ACTS];             /* times an act was done with such a thing there */
  unsigned said, said_right, said_wrong, cannot_say, spared;
  unsigned unsettled;                  /* times more than one rule was still possible: wait, and watch */
  unsigned no_words;                   /* times the last rule went: the language itself is too small */
  unsigned mute;                       /* acts met afterwards with nothing left to say of them */
  /*
   * The evidence, kept as it was seen.
   *
   * Ruling out is an AND, and an AND can only take bits away, so a word added to the
   * language later has nothing to answer for unless the past is still here to answer
   * to. These are the pictures themselves, not facts phrased in today's words, so a
   * wider language can be put to them exactly as this one was.
   */
  pl_frame_t log[RU_LOG];
  unsigned char log_act[RU_LOG];       /* the act leading from log[i] to log[i+1] */
  unsigned logged, lost;
  /* every sighting, the newest kept, and the accounts of where rules break */
  ru_ev_t ev[RU_EV];
  unsigned ev_next, ev_count;
  ru_cond_t cond[RU_CONDS];
  uint16_t chain[RU_WAYS][RU_KINDS][RU_ACTS];   /* the first account of each, plus one: found without a search */
  unsigned n_cond, cond_full;
  unsigned level;                      /* which level of the game it is on */
  int conds_on;                        /* whether it looks for where rules break */
  unsigned families;                   /* which kinds of fact it may say a rule turns on */
  unsigned made[RU_FACTS], killed[RU_FACTS];
  unsigned repaired, unrepaired;       /* deaths it found an account for, and deaths it could not */
  unsigned cond_said, cond_right;      /* what it said from those accounts, and how often rightly */
  uint16_t passable;                   /* colours a thing has been seen to move into */
  unsigned long long ruled_out;
} ru_world_t;

/* every rule possible again: a new game */
void ru_begin(ru_world_t *w);

/* the things in a picture; how many, up to RU_MAX_THINGS */
unsigned ru_things(const pl_frame_t *f, ru_thing_t *out, unsigned cap);

/* what kind a thing is: its colour and how big it is, in classes */
unsigned ru_kind(const ru_thing_t *t);      /* the narrowest: colour and shape */
unsigned ru_kind_way(const ru_thing_t *t, unsigned way);

/*
 * An act, and the picture before and after it: every rule that says something else
 * happened to a thing of that kind is ruled out. Returns how many were ruled out.
 */
unsigned ru_saw(ru_world_t *w, unsigned act, const pl_frame_t *before, const pl_frame_t *after);

/*
 * What it says this act will do to the things now, where one rule is left for each
 * kind it can see: 1 if it could say and `out` holds its picture of what follows,
 * 0 if more than one rule is still possible for something the act touches.
 */
int ru_say(ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out);

/*
 * 1 when every thing it can see has settled to "nothing happens to it" for this act:
 * the act is known to change nothing, and spending one on it would buy nothing.
 */
int ru_changes_nothing(const ru_world_t *w, unsigned act, const pl_frame_t *now);

/*
 * Imagining: the picture this act would leave, by its rules -- each thing whose kind
 * has settled moves or goes as its one rule says; a thing whose kind has not settled
 * is imagined where it is. That last is a hypothesis, not knowledge, so a plan made
 * in imagination is checked step by step against what really happens.
 */
void ru_imagine(const ru_world_t *w, unsigned act, const pl_frame_t *now, pl_frame_t *out);

/* how much it still does not know about acts and kinds, in bits */
double ru_bits(const ru_world_t *w);

/*
 * The bits an act is guaranteed to win, whatever the world answers: the sharpest
 * question it can ask from here. A worst case over outcomes, not an average.
 */
double ru_worst_bits(const ru_world_t *w, unsigned act, const pl_frame_t *now);

/* times the language could not account for what happened at all */
unsigned ru_no_words(const ru_world_t *w);

/* forget what was ruled out and put the kept evidence to the language afresh */
void ru_replay(ru_world_t *w);

/* which level it is on now: a level is one of the facts a rule can break at */
void ru_level(ru_world_t *w, unsigned level);

/* look for where rules break when they die (on), or let them die (off) */
void ru_conds(ru_world_t *w, int on);

/* where to write, in words, why it believes what it does and why it stopped */
void ru_tell(FILE *f);

/* which kinds of fact a rule may turn on: a mask of 1 << RU_F_... */
void ru_families(ru_world_t *w, unsigned mask);

/*
 * Where to put, for study between games, each death it could not account for: every
 * sighting of that kind under that act, with the picture around it. One JSON line each.
 */
void ru_dump(FILE *f);

sm_status_t ru_report(const ru_world_t *w, FILE *out);

#endif
