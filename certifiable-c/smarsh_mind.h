/*
 * smarsh_mind.h -- the child as a mind of parts, playing a world it can only
 * see and act in.
 *
 * ============================================================
 * WHY THIS EXISTS
 * ============================================================
 * smarsh_play.h reasons over cells: which cells moved together, which
 * colour was in the way, which colour it covered when a level ended. It
 * never says what it expects to happen, so when something it did not
 * expect happens it can count it but not learn from it, and it has no way
 * to hold an ending that is a relation or a sequence rather than a colour.
 *
 * This mind works on things, and it predicts. See research/ARCHITECTURE.md
 * for the whole design and where each part comes from.
 *
 * ============================================================
 * THE PARTS
 * ============================================================
 *   parietal map    the frame as things: connected patches of one colour,
 *                   the ground they stand on (remembered where hidden), and
 *                   the movers, found by moving together. Two relations are
 *                   read off the ground: the odd one out (among tiles of one
 *                   shape, the only one of its colour) and a legend (a row
 *                   of distinct colours, each also found elsewhere).
 *   beliefs         per mover, how it moves: by a fixed step per action
 *   (Hartley)       (and stopped by walls), or only when pushed. Which
 *                   colours block. Which conditions end a level. Every
 *                   belief is a set of what has not been ruled out; nothing
 *                   is weighed.
 *   prediction      before every action, where every mover will be and
 *                   whether the level will end. After it, the difference:
 *                   every mismatch corrects the belief that made it, and
 *                   what no belief can account for is kept as a puzzle.
 *   inventor        the language of endings starts with one block (a mover
 *                   covers a colour). When an ending happens that nothing in
 *                   the language explains, it builds the next blocks (the
 *                   odd one out, the legend in order, the legend reversed),
 *                   and keeps only those that agree with everything it has
 *                   seen in this world, replayed.
 *   imagination     plans are made inside its own beliefs, never by trying:
 *                   a search over where the movers could be, for a state in
 *                   which a surviving belief says the level ends. It acts on
 *                   the plan and replans only when a prediction fails.
 *   novelty         a fruit-fly style fingerprint of each new world (a
 *   (fly)           sparse random expansion, the strongest few units kept),
 *                   compared with the fingerprints of every world before.
 *   intuition       what ended the worlds that looked like this one, by how
 *   (Shannon)       often: the order in which to try what is still possible.
 *                   It orders; it never decides.
 *   controller      each move, from how much is still unknown (log2 of what
 *   (frontoparietal) is left), how surprised it is, and how reliable its
 *                   intuition has been: explore, trust intuition, imagine,
 *                   or ask.
 *   library         ideas that keep explaining worlds are packed into
 *                   blocks it starts with; ideas found only in sleep are
 *                   remembered as candidates.
 *   sleep           between stretches, it replays the worlds it could not
 *                   explain and tries every block on them, offline.
 *   self-correction when something it believes leads it wrong, it finds which
 *   (the leverage   belief, the way optogenetics finds which cells cause a
 *   of optogenetics) behaviour: precisely (one belief, one remembered moment),
 *                   in both directions (silence the evidence against an idea,
 *                   or switch the idea back on), reversibly, and in its head
 *                   before in the world. It corrects only the culprit, and the
 *                   world then confirms or refutes the correction.
 *   switches        any part can be switched off whole, so it can find out what
 *   (lesions)       each of its parts is worth by playing without it. This is
 *                   the coarse method optogenetics replaced in neuroscience; it
 *                   measures, it does not correct.
 *
 * Deterministic: the same library and the same world give the same play.
 */

#ifndef SMARSH_MIND_H
#define SMARSH_MIND_H

#include <stdio.h>

#include "smarsh_core.h"
#include "smarsh_play.h"

#define MI_MAX_MOVERS 3u
#define MI_MEMORY 256u      /* worlds remembered by fingerprint */
#define MI_FP_K 8u          /* units kept in a fingerprint */
#define MI_PACK_AT 3u       /* explained this many worlds: packed into a block */

typedef enum {
  MI_COVER = 0,     /* a mover covers a colour */
  MI_ODD = 1,       /* a mover covers the one tile that differs */
  MI_SEQ_LR = 2,    /* a mover covers the legend's colours, left to right */
  MI_SEQ_RL = 3,    /* the same, right to left */
  MI_FAMILIES = 4
} mi_family_t;

typedef enum {
  MI_EXPLORE = 0,
  MI_INTUITION = 1,
  MI_IMAGINE = 2,
  MI_ASK = 3,
  MI_MODES = 4
} mi_mode_t;

/* The parts that can be switched off. */
typedef enum {
  MI_PART_INTUITION = 0,
  MI_PART_INVENTOR = 1,
  MI_PART_IMAGINATION = 2,
  MI_PART_PACKING = 3,
  MI_PART_CORRECTION = 4,
  MI_PARTS = 5
} mi_part_t;

/* What it carries from world to world. */
typedef struct {
  unsigned worlds;
  unsigned wins[MI_FAMILIES];      /* worlds this idea was the simplest explanation of */
  unsigned dreamt[MI_FAMILIES];    /* worlds sleep found it explains */
  int packed[MI_FAMILIES];         /* a block it starts every world with */
  unsigned mirror_wins;            /* worlds where one mover's steps were another's, transformed */
  unsigned n_mem;
  unsigned char mem_fp[MI_MEMORY][MI_FP_K];
  unsigned char mem_family[MI_MEMORY];   /* winning family + 1; 0 when none was found */
  unsigned intuition_right, intuition_wrong;
  /* per idea: how often intuition named it, and was right. It is trusted
     about an idea it has been right about, not about every idea at once */
  unsigned intuition_named[MI_FAMILIES], intuition_hit[MI_FAMILIES];
  unsigned puzzles, inventions, corrections;
  /* what it found out about itself by switching parts off: its score (see
     mi_score) over the same worlds with the part on, minus with it off; and
     with each pair of parts off together, which shows parts that stand in for
     each other (each alone costs little, both together cost much) */
  int probe_delta[MI_PARTS];
  int probe_pair[MI_PARTS][MI_PARTS];
  unsigned probes;
} mi_library_t;

/* How one world went. */
typedef struct {
  unsigned levels_done;
  unsigned level_actions[PL_MAX_LEVELS];
  unsigned level_shortest[PL_MAX_LEVELS];
  unsigned actions;
  unsigned modes[MI_MODES];        /* actions taken in each mode */
  unsigned predictions, errors;    /* things predicted, and how many were wrong */
  unsigned puzzles;                /* endings nothing it believed explained */
  unsigned invented;               /* families it built during this world (bit per family) */
  unsigned corrected_endings;      /* ideas about endings brought back: the moment that ruled them out was a guess */
  unsigned corrected_walls;        /* colours it had concluded block it, doubted one at a time */
  unsigned corrected_actions;      /* actions it had concluded do nothing, doubted one at a time */
  int winner;                      /* simplest family alive at the end, or -1 */
  unsigned movers;
  int transform;                   /* a mover moved as another, transformed: 0 none, else 1..3 */
  unsigned novelty;                /* 0 (seen its like) .. MI_FP_K (nothing like it) */
  int intuition;                   /* family intuition put first, or -1 */
  int trusted;                     /* intuition was reliable enough to act on */
  int asked;                       /* it was stuck enough to ask */
  int hinted;                      /* a teacher named a family before it began */
} mi_result_t;

void mi_library_init(mi_library_t *lib);

/*
 * Play the whole game, spending at most budget actions. hint is a family a
 * teacher named for this world, or MI_FAMILIES for none: it is added to the
 * language from the start, and still has to survive everything seen.
 * Returns SM_OK when every level ended within the budget.
 */
sm_status_t mi_play(mi_library_t *lib, pl_game_t *g, unsigned budget, unsigned hint,
                    mi_result_t *out);

/*
 * How well a world went, the way ARC-AGI-3 scores it, in thousandths: each
 * level scores (shortest / actions taken) squared, at most 1, and 0 if not
 * finished; later levels weigh more (level i weighs i). Unlike "finished
 * well", the first level counts: that is where knowing what to try first
 * saves moves.
 */
unsigned mi_score(const mi_result_t *r, unsigned levels);

/* Between stretches: try every block on the worlds it could not explain. */
void mi_sleep(mi_library_t *lib, FILE *journal);

/* Switch a part off (or on). Applies to every mind in this process. */
void mi_switch(mi_part_t part, int off);
int mi_switched_off(mi_part_t part);

/*
 * Optogenetics on itself: play the same worlds with each part switched off
 * in turn, on a copy of the library, and record what each part is worth.
 * `make` builds world number i (0 <= i < n) into g.
 */
void mi_probe(mi_library_t *lib, void (*make)(pl_game_t *g, unsigned i), unsigned n,
              unsigned budget, FILE *journal);

const char *mi_family_name(unsigned family);
const char *mi_mode_name(unsigned mode);
const char *mi_part_name(unsigned part);

/* Write and read the library as "lib.<name> <values>" lines. */
void mi_library_save(const mi_library_t *lib, FILE *out);
/* One line; returns 1 when it was a library line. */
int mi_library_line(mi_library_t *lib, const char *line);

#endif /* SMARSH_MIND_H */
