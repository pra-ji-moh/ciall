/*
 * smarsh_core.h -- the Smarsh value core in C: possibility sets, the four
 * verdicts, support, unlicensed elimination, and the speculation gate.
 *
 * ============================================================
 * STATUS: COMPILED AND RUN
 * ============================================================
 * This was written with no compiler present and said so. It has since
 * been compiled (zig cc 0.16.0, clang 21, strict C99, -Wall -Wextra
 * -pedantic: zero warnings) and test_smarsh_core.c passes. ../build_c.sh
 * rebuilds and reruns everything in this directory.
 *
 * Differential: diff_core.c runs the speculation gate here against
 * Smarsh's JavaScript one (src/speculate.js via the `speculate` builtin)
 * on 3,000 undetermined cases, half with the bar exactly on S. They agree
 * on every decision and every intensity, to the bit. The first run did
 * not: 659 disagreed, because sm_support computed 1 - |D|/|Dom| (two
 * roundings) where JS computes the correctly rounded (|Dom| - |D|)/|Dom|,
 * and at S == tau that flipped decisions. Fixed here and in the kernel.
 *
 * Two cases differ by design and are not diffed. One value left: this
 * answers it as derived, JS still gates it. No value left: this reports a
 * contradiction, while JS computes S = 1 and speculates at full intensity,
 * which is a defect on the JS side, not a design choice.
 *
 * Scope: that is the gate, not every function in this file, and not yet
 * the 3,065-program differential Smarsh's two JS engines run.
 *
 * ============================================================
 * WHY A POSSIBILITY SET AND NOT A PROBABILITY
 * ============================================================
 * One axiom: to reason is to eliminate possibility. A node's state is
 * therefore a SET -- the only thing elimination can act on -- and the
 * primitive operation is sm_eliminate(), which removes a value that a
 * constraint has ruled out. Everything else here is counted off that:
 *
 *   S  = fraction of the domain eliminated      (grounding, not likelihood)
 *   H  = log2|D|, the information a single-value emission CLAIMS
 *        but does not hold -- an unlicensed elimination, since it
 *        removes |D|-1 possibilities that no constraint removed
 *
 * The four verdicts are counted, not declared:
 *   |D| == 1   derived        forced by the constraints
 *   |D|  > 1   undetermined   the constraints do not decide it
 *   |D| == 0   contradiction  the constraints admit nothing
 *   no domain  groundless     there was never a question to decide
 *
 * ============================================================
 * CERTIFICATION PROPERTIES (claimed, not certified)
 * ============================================================
 *   - No malloc/free/realloc anywhere in this file or smarsh_core.c.
 *     Every buffer is a fixed-size array sized by a compile-time
 *     constant (SM_MAX_DOMAIN).
 *   - Every loop is bounded by a compile-time constant. Two are bounded
 *     directly (SM_DOMAIN_WORDS, SM_MAX_DOMAIN); sm_init's fill loop is
 *     bounded by the caller's `size`, which is itself rejected before
 *     the loop if it exceeds SM_MAX_DOMAIN -- so the worst case is
 *     still statically known, but saying "never bounded by caller data"
 *     would have been inaccurate, and this file's whole premise is that
 *     the difference between a careful claim and a checked one matters.
 *   - No recursion.
 *   - No function pointers or dynamic dispatch.
 *   - Overlong input is a checked error return, never silent truncation.
 *   - The one floating-point dependency is log2() from <math.h>, used
 *     only for H. Callers who cannot link libm can use sm_count() and
 *     compute H themselves; nothing else here needs it.
 *
 * This demonstrates the ALGORITHM has no dependency on garbage
 * collection or dynamic allocation. It is NOT DO-178C evidence. See
 * ../CERTIFICATION-GAPS.md for the honest accounting of what would be.
 */

#ifndef SMARSH_CORE_H
#define SMARSH_CORE_H

#include <stdint.h>

/* Bounded so every loop below is bounded by a compile-time constant. A
   domain larger than this is a checked error, not a truncation. */
#define SM_MAX_DOMAIN 256u
#define SM_DOMAIN_WORDS ((SM_MAX_DOMAIN + 63u) / 64u)

typedef enum {
  SM_DERIVED = 0,
  SM_UNDETERMINED = 1,
  SM_CONTRADICTION = 2,
  SM_GROUNDLESS = 3,
  SM_SPECULATED = 4
} sm_verdict_t;

typedef enum {
  SM_OK = 0,
  SM_ERR_DOMAIN_TOO_LARGE = 1,
  SM_ERR_EMPTY_DOMAIN = 2,
  SM_ERR_INDEX_OUT_OF_DOMAIN = 3,
  SM_ERR_BAD_THRESHOLD = 4,
  SM_ERR_NULL_ARGUMENT = 5,
  SM_ERR_GUESS_ID_OUT_OF_RANGE = 6,
  /* For a condition the code proves cannot happen. Returned rather than
     assumed away, so that if the proof is wrong it fails loudly instead
     of continuing on a false premise. Used by smarsh_reason.c. */
  SM_ERR_INTERNAL_INVARIANT = 7
} sm_status_t;

/*
 * A possibility set. `size` fixes the domain as indices 0..size-1;
 * `bits` marks which of those remain possible. `has_domain` is the
 * groundless distinction and is deliberately NOT the same as "bits are
 * all clear": a set with nothing left is a contradiction (the
 * constraints ruled everything out), while no domain at all means there
 * was never a well-formed question. Collapsing those two is the mistake
 * this whole design exists to avoid.
 */
typedef struct {
  uint64_t bits[SM_DOMAIN_WORDS];
  unsigned size;
  int has_domain;
} sm_possibility_t;

/*
 * WHICH guesses a result rests on, not how many bits they came to.
 *
 * This was a single running double, and that was wrong: one guess feeding
 * two paths that later recombine got added once per path, so a 1-bit guess
 * reported as 2 bits and a diamond of depth n reported 2^n. The quantity
 * is a property of a SET of guesses, so the set travels and composition
 * unions it; a guess reached twice collapses because a set says it is the
 * same guess. Found by a diamond test, not by rereading the derivation.
 *
 * `ids` is a bitset over guess identities the caller assigns -- the caller
 * is what knows which node a guess belongs to, and the runtime cannot
 * invent an identity without either breaking replay or colliding.
 * `remaining[i]` is |D| at the moment guess i was made, so its magnitude
 * is log2(remaining[i]); storing the count rather than the double keeps
 * this struct small enough to pass by value.
 */
#define SM_MAX_GUESSES 64u

typedef struct {
  uint64_t ids;
  uint16_t remaining[SM_MAX_GUESSES];
} sm_ancestry_t;

void sm_ancestry_clear(sm_ancestry_t *a);
sm_status_t sm_ancestry_add(sm_ancestry_t *a, unsigned guess_id, unsigned remaining);
void sm_ancestry_union(const sm_ancestry_t *a, const sm_ancestry_t *b,
                       sm_ancestry_t *out);
/* Summed over the DISTINCT guesses, so a shared ancestor counts once. */
double sm_ancestry_bits(const sm_ancestry_t *a);

typedef struct {
  sm_verdict_t verdict;
  /* Meaningful only when verdict is SM_DERIVED or SM_SPECULATED. */
  unsigned value;
  /* S and H are filled for every verdict except SM_GROUNDLESS, where
     there is no domain to take a fraction of; S is then -1.0 as an
     out-of-band marker rather than 0.0, which would read as a real
     measurement of "nothing eliminated". */
  double S;
  double H;
  /* Which upstream guesses this rests on. Empty unless composed from, or
     produced by, a speculation. Read its magnitude with
     sm_ancestry_bits() rather than keeping a running total, for the
     reason given above the struct. */
  sm_ancestry_t ancestry;
  /* g(S): how hard to speculate, once the bar is cleared. Zero for every
     verdict except SM_SPECULATED -- refusing is a behaviour, not an
     absence, so it carries an intensity of zero rather than nothing. */
  double intensity;
} sm_result_t;

/*
 * g() is a POLICY, not a derivation. The elimination axiom says what may
 * be concluded; it says nothing about how hard to guess once you have
 * already decided to guess without a licence. So this is a named choice
 * rather than a constant buried in the arithmetic, and changing it is
 * meant to be a visible act.
 *
 * SM_INTENSITY_SUPPORT   g(S) = S
 *   What Smarsh ships today (speculate.js `intensityOf`). The C core
 *   defaults to it so the two implementations cannot silently disagree --
 *   a divergence here would be caught by the differential oracle, and
 *   introducing one on purpose while porting would be a bad trade.
 *
 * SM_INTENSITY_HEADROOM  g(S) = (S - tau) / (1 - tau)
 *   Zero at the bar, rising as support does. The argument for it: under
 *   the identity, a claim that only just scraped past a high bar
 *   (S = tau = 0.9) reports intensity 0.9, which reads as near-total
 *   confidence for what was in fact a near-miss. Headroom measures how
 *   far past the bar you got, not how high the bar was. Note it can
 *   never reach 1.0 in practice: S = 1 means nothing remains possible,
 *   which is a contradiction, not a confident guess.
 *
 * Both are implemented and tested. Which one is right is a decision
 * about what intensity is FOR, and that is not the porter's to make.
 */
typedef enum {
  SM_INTENSITY_SUPPORT = 0,
  SM_INTENSITY_HEADROOM = 1
} sm_intensity_policy_t;

/* ---- constructing and constraining ------------------------------- */

/* A domain of `size` values, all still possible: the state before any
   reasoning has happened. */
sm_status_t sm_init(sm_possibility_t *p, unsigned size);

/* The groundless state: no domain at all. */
void sm_init_groundless(sm_possibility_t *p);

/* THE primitive operation. A constraint has ruled `index` out. Idempotent:
   eliminating something already eliminated is not an error, because
   applying the same constraint twice should not mean anything different
   from applying it once. */
sm_status_t sm_eliminate(sm_possibility_t *p, unsigned index);

/* Keep only `index`, eliminating everything else -- a constraint that
   pins the value. Distinct from sm_speculate(): this is licensed. */
sm_status_t sm_restrict_to(sm_possibility_t *p, unsigned index);

/* ---- reading -------------------------------------------------------- */

int sm_is_possible(const sm_possibility_t *p, unsigned index);
unsigned sm_count(const sm_possibility_t *p);
sm_verdict_t sm_verdict(const sm_possibility_t *p);
double sm_support(const sm_possibility_t *p);   /* S; -1.0 if groundless */
double sm_hartley(const sm_possibility_t *p);   /* H; 0.0 if |D| <= 1 */

/* ---- the gate ------------------------------------------------------- */

/*
 * Decide what to emit. `tau` must be in [0,1] or SM_ERR_BAD_THRESHOLD.
 *
 * Direction matches Smarsh's shipped speculate.js (`allowed = s >= tau`):
 * S >= tau means grounded enough to venture a guess. An implementation
 * of this in Python had the comparison backwards and it survived two
 * test runs, because a two-valued domain makes S constant -- so the
 * boundary is tested explicitly in test_smarsh_core.c rather than
 * assumed from the sign in this comment.
 *
 * When it does guess, the choice is UNIFORM over what remains. It does
 * not try to be right more often than chance; that is what keeps this
 * from being prediction. `seed` makes the choice reproducible, because
 * a decision that cannot be replayed is not evidence.
 */
/*
 * `guess_id` identifies the speculation this call may make, so that the
 * same guess reached by two paths is recognised as one. It must be less
 * than SM_MAX_GUESSES. There is deliberately no anonymous fallback: an
 * identity derived from an address does not survive a restart, is reused
 * after free, and collides outright for interned values -- and a guess
 * that cannot be told from another cannot be counted correctly, so
 * refusing beats silently miscounting.
 */
sm_status_t sm_decide(const sm_possibility_t *p, double tau, uint64_t seed,
                      sm_intensity_policy_t policy, unsigned guess_id,
                      sm_result_t *out);

/* ---- composition: REMOVED, see smarsh_reason.h ---------------------- */

/*
 * sm_compose_and() used to live here. It took two sm_result_t and
 * combined them into a third, three-valued, over booleans.
 *
 * It is gone because it cannot be right. Composing two RESULTS means the
 * inputs are the two answer sets, and verify_reason.py section 4 exhibits
 * three composites -- a AND a, a AND NOT a, a OR NOT a -- whose operands
 * have identical answer sets and identical supports, and whose verdicts
 * are undetermined, derived false, and derived true. No function of the
 * operands can return three different things for one argument. So the
 * old signature described a function that does not exist, and what it
 * actually computed was the independent case, silently, whether or not
 * the operands were independent.
 *
 * sr_ask2() in smarsh_reason.h replaces it and is strictly more: exact
 * when the operands share structure, general over any operator table and
 * any domain size rather than boolean AND alone, and it derives
 * absorption -- what used to be the hand-written rule that false AND
 * groundless is false -- from the operator table being constant, so it
 * holds for OR, for min and max over ranks, and for anything else with an
 * absorbing element, none of which are named anywhere.
 *
 * What survives from here is sm_ancestry_union(), declared above: a set
 * of guesses, unioned rather than summed. That part was right.
 */

#endif /* SMARSH_CORE_H */
