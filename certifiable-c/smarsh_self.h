/*
 * smarsh_self.h -- the part of the child's own source that the child reads and rewrites.
 *
 * Every line below is a real setting of how it reasons (used by smarsh_explore.c),
 * written in one form so the child can read it:
 *
 *     #define NAME value   (a comment beginning "self:" listing the values it may take)
 *
 * self_write reads this file, chooses one change it has not tried (uniformly), and
 * writes a new version of it. A new version is kept only if the whole program still
 * compiles, every check in build_c.sh still passes (the checks are not in this file
 * and it cannot touch them), and it ends more levels on the development games and no
 * fewer on the held-back games. Every change tried, kept or ruled out, and why, is
 * written to child/self_edits.txt.
 *
 * Nothing outside this file is rewritten by the child.
 */
#ifndef SMARSH_SELF_H
#define SMARSH_SELF_H

/* what ticks by itself: a change patch this small may be a clock */
#define SELF_TICK_MAX 8u   /* self: 4u 8u 16u */
/* how many steps a cell is watched before it is judged restless */
#define SELF_RESTLESS_WINDOW 12u   /* self: 8u 12u 16u 24u */
/* how often a colour must move before it is taken as the body */
#define SELF_BODY_VOTES 3u   /* self: 2u 3u 5u */
/* how often a shift must repeat before a step is predicted by it */
#define SELF_SHIFT_SURE 2u   /* self: 1u 2u 3u */
/* the gate for not spending a pointing: support needed (above 1 never skips) */
#define SELF_SKIP_TAU 0.75   /* self: 0.5 0.625 0.75 0.875 1.01 */
/* steps worth one cell nearer to two panels matching */
#define SELF_MATCH_WEIGHT 8.0   /* self: 0.0 4.0 8.0 16.0 */
/* acts always kept, in order, when a situation offers more than it can hold */
#define SELF_KEEP_ACTS 24u   /* self: 12u 24u 40u */
/* fewer situations than this, with much left out: watch everything again */
#define SELF_FEW_SITUATIONS 64u   /* self: 16u 32u 64u 128u */
/* times it redraws its map of a level before giving it up */
#define SELF_FRESH_STARTS 3u   /* self: 0u 1u 3u 6u */
/* times per level it doubts that a death was the last step's doing */
#define SELF_DEATH_RETRIES 10u   /* self: 0u 3u 10u 20u */
/* how a tie is made uniform: 0 plain acts only, 1 every act, 2 kinds then acts */
#define SELF_TIE_MODE 1u   /* self: 0u 1u 2u */
/* acts it will spend on one thing it cannot explain before letting it be */
#define SELF_WATCH 48u   /* self: 0u 16u 48u 96u */
/* whether families kept by other games are formulated from the first act */
#define SELF_USE_LIVE 0   /* self: 0 1 */

/* its own parts, each on (0) or off (1) */
#define SELF_OFF_RESTLESS 0   /* self: 0 1 */
#define SELF_OFF_CLOCK 0   /* self: 0 1 */
#define SELF_OFF_STOOD 0   /* self: 0 1 */
#define SELF_OFF_PANELS 0   /* self: 0 1 */
#define SELF_OFF_NEAR 1   /* self: 0 1 */
#define SELF_OFF_THEORY 1   /* self: 0 1 */
#define SELF_OFF_SKIP 0   /* self: 0 1 */
#define SELF_OFF_DEATHS 0   /* self: 0 1 */
#define SELF_OFF_UNMASK 0   /* self: 0 1 */
#define SELF_OFF_WINDOW 0   /* self: 0 1 */
#define SELF_OFF_REDRAW 0   /* self: 0 1 */
#define SELF_OFF_RECALL 0   /* self: 0 1 */
#define SELF_OFF_REPLAY 1   /* self: 0 1 */
#define SELF_OFF_REACH 0   /* self: 0 1 */
#define SELF_OFF_CURIOUS 0   /* self: 0 1 */
#define SELF_OFF_PLAN 0   /* self: 0 1 */
/* act on the best theory it has, before an ending has proved it */
#define SELF_BOLD 1   /* self: 0 1 */

#endif
