/*
 * smarsh_reason.h -- the foundational reasoning kernel.
 *
 * COMPILED AND RUN (strict C99, zero warnings; see smarsh_core.h). It is
 * checked by test_smarsh_reason.c, 194 checks against independently
 * brute-forced ground truth, and by diff_kernel.c, which ran 3,000 random
 * trials identical to the Python reference (python-reference/kernel_py.py).
 * Compiling it found one real bug the Python had too: a NaN threshold
 * passed the speculation gate. Fixed in both.
 *
 * ============================================================
 * WHAT THIS FILE IS, AND WHAT IT REPLACES
 * ============================================================
 * smarsh_core.h models ONE node: a possibility set, eliminated down. That
 * is right as far as it goes, and it goes exactly one node. The moment
 * two nodes had to be combined, sm_compose_and() had to be written by
 * hand, over booleans only, and it is WRONG in the way that hand-written
 * composition is always wrong -- see THE CORRELATION FAILURE below.
 *
 * This file replaces per-node possibility sets with the object those sets
 * are shadows of: a set of WORLDS. Everything else falls out of that.
 *
 * ============================================================
 * THE AXIOM, UNCHANGED
 * ============================================================
 * To reason is to eliminate possibility.
 *
 * The only primitive is sr_eliminate(): a world is struck out. There is
 * no inference rule, no scoring function, no propagation step, and no
 * confidence arithmetic anywhere in this kernel. A verdict is never
 * DECLARED; it is COUNTED off what survives.
 *
 * ============================================================
 * THE THREE OBJECTS
 * ============================================================
 * 1. A STATE is the set of worlds still possible. It starts as
 *    everything and only ever shrinks. sr_eliminate is the only thing
 *    that shrinks it, so the entire history of a computation is a list of
 *    eliminations and nothing else.
 *
 * 2. A QUERY is a total function from worlds to answers: q(w) = a. It is
 *    stored as a table, one answer per world. A query is NOT a value and
 *    carries no state -- it is the question, and it means the same thing
 *    before and after any elimination.
 *
 * 3. An ANSWER SET is what a query still admits: D(q) = { q(w) : w live }.
 *    Read by sr_image(). Everything reported comes off |D|:
 *
 *      |D| == 1   derived        the surviving worlds all agree
 *      |D|  > 1   undetermined   they do not
 *      |D| == 0   contradiction  no world survives at all
 *      no domain  groundless     there was never a question
 *
 *      S = 1 - |D| / dom     how much of the answer space is gone
 *      H = log2 |D|          what a single-answer emission would CLAIM
 *                            but does not hold
 *
 * ============================================================
 * THE CORRELATION FAILURE, WHICH IS THE POINT
 * ============================================================
 * Let a be an unknown boolean. Then:
 *
 *      D(a)      = {false, true}   S = 0   undetermined
 *      D(not a)  = {false, true}   S = 0   undetermined
 *
 * Now ask for "a and not a". The truth is that it is FALSE -- derived,
 * with no evidence at all, purely from structure. But any composition
 * rule whose inputs are the two operands ANSWER SETS must give the same
 * result for "a and not a" as for "a and b" with b independent, because
 * those two have identical operand answer sets. The first is derived
 * false and the second is undetermined. So no such rule exists.
 *
 * The same three queries make the sharper statement. With identical
 * operand supports (S = 0 and S = 0) the composite lands on all three
 * live verdicts:
 *
 *      a and a       -> {false, true}   undetermined
 *      a and not a   -> {false}         derived FALSE
 *      a or  not a   -> {true}          derived TRUE
 *
 * THEREFORE: S_out = f(S_left, S_right) is not merely a bad idea, it is
 * unsatisfiable. There is no such f. This is proved by exhibition in
 * verify_reason.py rather than asserted here, because the whole argument
 * for this project is that a stated property and a checked one are
 * different things.
 *
 * At correct scope the result is: NO COMPOSITIONAL CONFIDENCE SEMANTICS
 * EXISTS. It reaches anything that attaches a number to a claim and then
 * combines claims, any interface whose contract is a per-answer
 * confidence, and any chain of reasoning that commits an intermediate
 * conclusion and then reuses it, since committing is what turns a step
 * into an operand. It does NOT reach the internals of a system that
 * composes representations rather than scores. The narrower claim is the
 * true one and it is the one worth making. This kernel never forms such
 * a composition at all. It composes the QUESTIONS (sr_map1, sr_map2,
 * pointwise over worlds) and evaluates the result ONCE against the
 * surviving worlds. Correlation is therefore not
 * handled; it never arises. There is nothing to handle, because the
 * operands were never separated in the first place.
 *
 * ============================================================
 * OBSERVATION AND SPECULATION ARE THE SAME OPERATION
 * ============================================================
 * This is what the world model buys that per-node sets could not say.
 *
 *   sr_observe(state, q, a)     eliminate every world where q(w) != a
 *   sr_speculate(state, q, ...) eliminate every world where q(w) != a,
 *                               for an a that nothing licensed
 *
 * Identical mechanics. The ONLY difference is whether a constraint
 * authorised the cut. So "unlicensed elimination" stops being a metaphor
 * for H and becomes literally the operation performed: a speculation cuts
 * log2|D| bits worth of worlds that no constraint cut, and that debt is
 * recorded in the ancestry rather than lost into the state.
 *
 * This is the one and only place in the kernel where something happens
 * that was not forced. It is gated on S >= tau, it requires a caller
 * assigned guess_id, and it taints the ancestry of everything downstream.
 * Every other function here is derivation, or it is counting.
 *
 * ============================================================
 * WITNESSES: THE ANSWER CARRIES ITS OWN PROOF
 * ============================================================
 * sr_ask() returns the surviving world set alongside the verdict, and
 * sr_witness_check() recomputes the verdict from that world set and the
 * query alone. The checker shares no code with the engine and trusts
 * nothing it reports -- it is a separate dumb loop, and it is meant to
 * be. A verdict that cannot be re-derived by something which does not
 * trust the deriver is a claim, not a result.
 *
 * ============================================================
 * WHAT THIS COSTS, STATED PLAINLY
 * ============================================================
 * Worlds are ENUMERATED. SR_MAX_WORLDS is 256, which is 8 independent
 * booleans, or one variable over 256 values, or any mix multiplying out
 * to 256. That is small, and it is a hard ceiling rather than a default.
 *
 * Exactness is what is being bought. Enumeration is sound AND complete
 * over the finite universe: no heuristic, no approximation, no search
 * order, nothing to tune, and a result a fifteen-line checker can
 * confirm. A symbolic representation would scale further and would not
 * have that property without a great deal more work to earn it back.
 *
 * Scaling past 256 worlds is a real, unsolved, separate problem. It is
 * not solved here, and nothing in this file should be read as claiming
 * otherwise.
 *
 * ============================================================
 * CERTIFICATION PROPERTIES (claimed, not certified)
 * ============================================================
 *   - No malloc/free/realloc. Every buffer is fixed size.
 *   - No recursion.
 *   - No function pointers and no dynamic dispatch. Operators are passed
 *     as TABLES (sr_op1_t, sr_op2_t), which is data; that keeps not, and,
 *     or, min and any other operator uniform without a call through a
 *     pointer.
 *   - Almost every loop is bounded by a compile-time constant --
 *     SR_MAX_WORLDS, SR_WORLD_WORDS or SR_MAX_ANSWERS -- and loops that
 *     could have run to a caller supplied n_worlds instead run the full
 *     fixed range and mask, so worst case equals typical case.
 *
 *     TWO EXCEPTIONS, stated rather than glossed. sr_choose iterates
 *     n_questions, and sr_refine's inner loops break early on j and k.
 *     Both have a statically known worst case (n_questions is rejected
 *     above SR_MAX_QUESTIONS), but their trip count does vary with caller
 *     data, and writing "every loop" would have been false. smarsh_core.h
 *     made the same distinction about sm_init and it is kept here for the
 *     same reason: a careful claim and a checked one are different, and
 *     so are a careful claim and a slightly-too-broad one.
 *   - Overlong input is a checked error, never a truncation.
 *   - The only floating point is log2() for H and the division for S.
 */

#ifndef SMARSH_REASON_H
#define SMARSH_REASON_H

#include <stdint.h>

#include "smarsh_core.h"   /* sm_verdict_t, sm_status_t, sm_ancestry_t,
                              sm_intensity_policy_t -- shared on purpose,
                              so the four verdicts have ONE definition. */

#define SR_MAX_WORLDS 256u
#define SR_WORLD_WORDS ((SR_MAX_WORLDS + 63u) / 64u)
#define SR_MAX_ANSWERS 64u
#define SR_MAX_TABLE (SR_MAX_ANSWERS * SR_MAX_ANSWERS)
/* One per world: a round that removes nothing ends the loop, and a round
   that removes something removes at least one world, so there can never
   be more productive rounds than there are worlds. */
#define SR_MAX_ROUNDS SR_MAX_WORLDS

/*
 * The state: which worlds are still possible.
 *
 * has_domain is groundless, and it is NOT the same as "no bits set". No
 * bits set means every world was eliminated -- a contradiction, which is
 * a strong and informative result. No domain means there was never a
 * question. Collapsing those two is the mistake the whole design exists
 * to avoid, and it is why this flag is separate from the bitset.
 */
typedef struct {
  uint64_t live[SR_WORLD_WORDS];
  unsigned n_worlds;
  int has_domain;
} sr_state_t;

/*
 * A query: the answer at every world. dom fixes the answer space as
 * 0..dom-1, so a boolean query has dom 2 (0 false, 1 true) and a rank
 * query over k positions has dom k.
 *
 * Note what is NOT in here: no support, no answer set, no cached verdict.
 * A query is the question. Everything else is a function of the question
 * AND the state, and storing it on the query would let the two drift.
 */
typedef struct {
  uint8_t ans[SR_MAX_WORLDS];
  unsigned n_worlds;
  unsigned dom;
  /* A GROUNDLESS query: not a well-formed question over these worlds, so
     ans[] means nothing. Distinct from a state having no domain -- that
     says there is no situation, this says there is no question, and a
     program can hold one of each at the same time. `dom` is still
     meaningful, because composition needs to know what the answer space
     WOULD have been. See sr_ask2 and absorption. */
  int has_domain;
} sr_query_t;

/* Unary operator as a table: out[a]. */
typedef struct {
  uint8_t out[SR_MAX_ANSWERS];
  unsigned dom_a;
  unsigned dom_out;
} sr_op1_t;

/* Binary operator as a table: out[a * dom_b + b]. */
typedef struct {
  uint8_t out[SR_MAX_TABLE];
  unsigned dom_a;
  unsigned dom_b;
  unsigned dom_out;
} sr_op2_t;

/*
 * A witness: everything sr_witness_check() needs to re-derive the verdict
 * without trusting anything else in this file.
 *
 * live is a COPY of the surviving worlds, not a pointer to them, so a
 * witness stays valid after further elimination. Otherwise checking one
 * would mean checking the present rather than the moment it was made.
 */
/*
 * There are two ways an answer can be forced, so there are two shapes of
 * proof, and a checker must be told which one it is looking at rather
 * than guessing.
 *
 * SR_W_WORLDS   the surviving worlds all agree. Re-derived by replaying
 *               the query over the recorded world set.
 * SR_W_ABSORB   the operator is constant over the entire range the two
 *               operands could take, so the answer holds whatever they
 *               are -- including when one of them is not a question at
 *               all. Re-derived by scanning the operator table over the
 *               two recorded ranges. No worlds involved.
 */
typedef enum {
  SR_W_WORLDS = 0,
  SR_W_ABSORB = 1
} sr_witness_kind_t;

typedef struct {
  sr_witness_kind_t kind;
  uint64_t live[SR_WORLD_WORDS];
  unsigned n_worlds;
  uint64_t image;      /* bitset over answers 0..dom-1 */
  unsigned dom;
  int has_domain;
  sm_verdict_t verdict;
  unsigned value;      /* meaningful only when verdict is SM_DERIVED */
  double S;
  double H;
  /* SR_W_ABSORB only: what each operand could still have been. */
  uint64_t range_a;
  uint64_t range_b;
} sr_witness_t;

typedef struct {
  sm_verdict_t verdict;
  unsigned value;
  double S;
  double H;
  double intensity;      /* g(S); zero for every verdict but SM_SPECULATED */
  sm_ancestry_t ancestry;
  sr_witness_t witness;
  /* Set only by sr_speculate. 0 means the bar was not cleared and NOTHING
     was eliminated: a refusal leaves the state exactly as it found it,
     which is what makes it a refusal rather than a quiet default. */
  int allowed;
} sr_result_t;

/* ---- states: the only thing that ever changes ---------------------- */

sm_status_t sr_state_init(sr_state_t *s, unsigned n_worlds);
void sr_state_groundless(sr_state_t *s);

/*
 * THE primitive. A world is struck out. Idempotent, because applying the
 * same constraint twice must not mean anything different from applying it
 * once.
 */
sm_status_t sr_eliminate(sr_state_t *s, unsigned world);

int sr_world_possible(const sr_state_t *s, unsigned world);
unsigned sr_live_count(const sr_state_t *s);

/* ---- queries -------------------------------------------------------- */

sm_status_t sr_query_init(sr_query_t *q, unsigned n_worlds, unsigned dom);
sm_status_t sr_query_set(sr_query_t *q, unsigned world, unsigned answer);

/* A question that was never well formed. `dom` is what the answer space
   would have been, which is what absorption needs in order to check that
   an operator is constant across it. */
sm_status_t sr_query_groundless(sr_query_t *q, unsigned n_worlds, unsigned dom);

/*
 * Compose the QUESTIONS, pointwise over worlds. This is the operation the
 * correlation argument above is about: "a and not a" composes to a query
 * that answers false at every single world, so asking it gives derived
 * FALSE, with no special case for the shared operand and no dependency
 * tracking anywhere. There is nothing to track. The worlds were never
 * split apart.
 */
sm_status_t sr_map1(const sr_op1_t *op, const sr_query_t *a, sr_query_t *out);
sm_status_t sr_map2(const sr_op2_t *op, const sr_query_t *a,
                    const sr_query_t *b, sr_query_t *out);

/* ---- asking --------------------------------------------------------- */

/*
 * The answer set: which answers the surviving worlds still admit. Returns
 * a bitset over 0..dom-1, which is why SR_MAX_ANSWERS is 64.
 */
sm_status_t sr_image(const sr_query_t *q, const sr_state_t *s, uint64_t *out);

/* Ask, and get back the proof. Never speculates: if the worlds do not
   decide it, the verdict is SM_UNDETERMINED and that IS the answer. */
sm_status_t sr_ask(const sr_query_t *q, const sr_state_t *s, sr_result_t *out);

/*
 * Re-derive the verdict from the witness alone and report whether it
 * agrees. Deliberately duplicates the counting rather than calling into
 * it: a checker that shares the engine's code checks only that the engine
 * agrees with itself.
 */
int sr_witness_check(const sr_query_t *q, const sr_witness_t *w);

/*
 * The other proof shape. Re-derives an absorption from the operator table
 * and the two recorded ranges alone -- no worlds, no state, no query.
 * Rejects a witness of the wrong kind rather than checking the wrong
 * thing and passing.
 */
int sr_witness_check_absorb(const sr_op2_t *op, const sr_witness_t *w);

/*
 * What an operand could still be: its answer set if it is a question, or
 * the whole of `dom` if it is groundless. The second case is where a
 * groundless operand stops being poison and becomes a range.
 */
sm_status_t sr_range(const sr_query_t *q, const sr_state_t *s, uint64_t *out);

/*
 * Ask a composite, handling operands that are not questions.
 *
 * BOTH OPERANDS ARE QUESTIONS -- the ordinary case. Composes pointwise
 * (sr_map2) and asks once. Exact, correlation included, per the theorem
 * at the top of this file. `composed` receives the composite query so the
 * caller can hand it to sr_witness_check.
 *
 * EITHER OPERAND IS GROUNDLESS -- absorption. The composite is scanned
 * over the full product of what the two operands could be: the answer set
 * for a real question, the whole domain for a groundless one. If the
 * operator is CONSTANT across that product, the answer is forced no
 * matter what, and the verdict is SM_DERIVED. Otherwise the verdict is
 * SM_GROUNDLESS -- never SM_UNDETERMINED, because "undetermined" would
 * claim the answer set is narrower than this can justify.
 *
 * That is the general rule. `false and groundless = false` is one
 * instance of it, and so is `true or groundless = true`, and so is any
 * operator with an absorbing element over any domain. None of them are
 * special-cased anywhere; they fall out of the table being constant.
 *
 * WHY THE PRODUCT IS SOUND HERE AND NOT IN GENERAL. Ranging over a
 * product is exactly the over-approximation the correlation theorem
 * rules out -- when both operands are functions of the same worlds. A
 * groundless operand is not a function of the worlds at all; there is no
 * shared variable to lose, because there is no variable. And the
 * over-approximation can only ever be an over-approximation: if it comes
 * out constant, the true answer set is that same singleton, so DERIVED is
 * right; if it does not, nothing is claimed. Sound in both branches.
 */
sm_status_t sr_ask2(const sr_op2_t *op, const sr_query_t *a,
                    const sr_query_t *b, const sr_state_t *s,
                    sr_query_t *composed, sr_result_t *out);

/* ---- eliminating: licensed, and then not ---------------------------- */

/*
 * A constraint says q = answer. Every world that disagrees is struck out.
 * This can empty the state, and an empty state is a contradiction, which
 * is reported rather than repaired.
 */
sm_status_t sr_observe(sr_state_t *s, const sr_query_t *q, unsigned answer);

/*
 * The unlicensed elimination, and the only one in the kernel.
 *
 * Computes S over the current worlds. If S < tau it refuses: allowed is
 * 0, the state is untouched, intensity is 0, and the verdict stays
 * SM_UNDETERMINED. Undetermined is deliberately NOT reported as
 * groundless here, even though the language surface renders a refused
 * speculation as the groundless VALUE -- at this level there is a domain
 * and there is a question, and saying otherwise would erase the very
 * difference the state's has_domain flag exists to keep.
 *
 * If S >= tau it picks UNIFORMLY from the surviving answers -- it does
 * not try to be right more often than chance, which is precisely what
 * keeps it from being prediction -- eliminates every world that disagrees
 * with the pick, and records the debt in ancestry: guess_id, with
 * remaining = |D| at the moment of the cut, so its size is the log2|D|
 * bits that no constraint licensed.
 *
 * seed makes the pick replayable, because a decision that cannot be
 * replayed is not evidence. guess_id must be under SM_MAX_GUESSES and has
 * no anonymous fallback: two guesses that cannot be told apart cannot be
 * counted, and refusing beats miscounting.
 */
sm_status_t sr_speculate(sr_state_t *s, const sr_query_t *q, double tau,
                         uint64_t seed, sm_intensity_policy_t policy,
                         unsigned guess_id, sm_ancestry_t *ancestry,
                         sr_result_t *out);

/* ==================================================================== */
/* SETTLING: commitment earned by convergence, not scheduled by position */
/* ==================================================================== */

/*
 * This implements the stopping criterion from the Native Reasoning Model
 * draft, section 2 -- with one change, which is the point of putting it
 * here rather than transcribing it.
 *
 * THE DRAFT SAYS: a node keeps pulling context while each additional
 * piece is still changing its belief, and commits only once
 *
 *     || belief(t) - belief(t-1) || < epsilon
 *
 * THE PROBLEM WITH THAT AS WRITTEN. It is a norm over per-node beliefs
 * exchanged between nodes, which is f(belief_left, belief_right) at every
 * message. The correlation theorem at the top of this file says that
 * function does not exist. This is not a hypothetical objection: loopy
 * belief propagation, cited in the draft as the model to follow, is known
 * to be wrong on graphs with cycles for exactly this reason -- it
 * double-counts evidence that arrived by two paths from one source. It is
 * the same defect the draft is trying to escape, one level up.
 *
 * WHAT REPLACES IT. Over a world set the same criterion has an exact,
 * discrete form and needs no norm and no epsilon at all:
 *
 *     a piece of context has stopped changing the answer
 *     exactly when applying it eliminates NO world.
 *
 * That is a fixed point in the strict sense, and it is strictly better
 * than a numeric convergence test on four counts:
 *
 *   1. NO EPSILON. There is nothing to tune and nothing to get wrong.
 *      Zero worlds removed is zero, not nearly zero.
 *   2. TERMINATION IS PROVED, NOT ASSUMED. The state only ever shrinks,
 *      and a productive round removes at least one world, so there can be
 *      at most n_worlds productive rounds. A numeric fixed-point
 *      iteration needs a contraction argument to rule out oscillation;
 *      this needs none, because monotone plus finite is enough.
 *   3. IT IS DECIDABLE. "Did live change" is a comparison of bitsets.
 *      Whether two belief vectors are within epsilon is a judgement call
 *      about epsilon.
 *   4. IT CANNOT DOUBLE-COUNT. Eliminating a world twice is eliminating
 *      it once (sr_eliminate is idempotent), so the same evidence
 *      arriving by two paths does nothing the second time. That is the
 *      loopy-BP failure, made structurally impossible rather than
 *      corrected for.
 *
 * THE COMMIT RULE. The draft's principle -- "a token's commitment must be
 * earned by convergence, not scheduled by position; passing forward is
 * the thing that needs justification, not the default" -- is enforced by
 * sr_settle_commit(), which refuses to yield a value unless the target
 * query is DERIVED. Undetermined does not commit. Contradiction does not
 * commit. Groundless does not commit. There is no path through this API
 * that emits a value because it ran out of rounds.
 *
 * OPEN LOOP VERSUS CLOSED LOOP (draft section 3). Incoming context is not
 * accepted as axiomatic here: it is applied as an elimination, and it can
 * empty the state. That is reported as a contradiction on the trace,
 * which is the incoming claim being checked against what is already held
 * rather than propagated forward unread.
 *
 * WHAT THIS DOES NOT DO. It does not tell you WHICH context to pull next,
 * and it does not build the world set. Those are the hard parts and they
 * are not solved here. See the note at the end of this header.
 */

/*
 * TWO KINDS OF STANDING STILL, WHICH THE DRAFT DOES NOT SEPARATE.
 *
 * The draft says to keep pulling context while it is still changing the
 * BELIEF. Over a world set that splits into two different things, and
 * keeping them apart is free:
 *
 *   productive   the round eliminated worlds. Something was learned.
 *   informative  the round moved the target's ANSWER SET. Something was
 *                learned THAT MATTERED TO THIS QUESTION.
 *
 * A round can be productive without being informative: context that rules
 * out worlds the target could not tell apart anyway. That is the exact
 * shape of a relevant-but-useless retrieval, and a norm on a belief vector
 * cannot see it, because both look like "belief did not move".
 *
 * `settled` follows the draft and tracks the second one: the loop stops
 * when the answer stops moving, not when the world set stops shrinking.
 */
typedef struct {
  unsigned rounds;                    /* pieces of context applied */
  unsigned productive;                /* rounds that eliminated worlds */
  unsigned informative;               /* rounds that moved the answer */
  unsigned removed[SR_MAX_ROUNDS];    /* worlds each round eliminated */
  unsigned live_at_start;
  unsigned live_now;
  uint64_t image;                     /* the target's answer set, now */
  /* The last round did not move the answer. Says the piece just applied
     was redundant FOR THIS QUESTION given what is already held -- NOT
     that no other context could ever help. The draft's criterion has the
     same scope; this states it out loud rather than leaving it to be
     assumed away. */
  int settled;
  /* Applying context emptied the state. The incoming claim conflicts with
     what was already held, which is a result and not a failure. */
  int contradicted;
} sr_settle_t;

/*
 * `target` is passed to begin and step rather than stored on the trace: a
 * query is 264 bytes and the trace is meant to be cheap to copy. The
 * caller has to pass the same one both times, and passing a different one
 * mid-loop makes `settled` meaningless, so step re-reads it rather than
 * trusting a cached image.
 */
sm_status_t sr_settle_begin(sr_settle_t *t, const sr_state_t *s,
                            const sr_query_t *target);

/*
 * Apply one piece of context: the constraint says `constraint` = `answer`.
 * Records what it eliminated and whether the target's answer moved. The
 * answer not moving is the stopping condition.
 */
sm_status_t sr_settle_step(sr_settle_t *t, sr_state_t *s,
                           const sr_query_t *target,
                           const sr_query_t *constraint, unsigned answer);

/*
 * Commit, or refuse to. Fills `out` with the honest verdict either way.
 * out->allowed is 1 only when the target is DERIVED -- that is the whole
 * of "passing forward needs justification". A caller that ignores
 * out->allowed and reads out->value anyway gets 0, not a guess.
 */
sm_status_t sr_settle_commit(const sr_settle_t *t, const sr_query_t *target,
                             const sr_state_t *s, sr_result_t *out);

/* ==================================================================== */
/* CHOOSING WHAT TO ASK, WITHOUT A SALIENCE SCORE                        */
/* ==================================================================== */

/*
 * sr_settle applies the context it is handed. Deciding WHICH context to
 * pull next is the other half of reasoning, and the usual way to do it is
 * a relevance or salience score -- which is a learned number consumed to
 * produce another number, which is the chain again, wearing a different
 * hat.
 *
 * It does not have to be. Over a world set, what a question would do is
 * not estimated. It is counted, exactly, before the question is asked:
 * partition the surviving worlds by what the candidate question answers
 * in each of them, and look at what the target's answer set becomes in
 * every branch.
 *
 * TWO OF THE RESULTS ARE FACTS, NOT PREFERENCES.
 *
 *   IRRELEVANT   No possible answer to this question, BY ITSELF, moves
 *                the target's answer set. This is DERIVED, and stronger
 *                than a score: a score can rank a question low, it cannot
 *                prove anything about it. But it is a fact about one
 *                question asked now, not about the question in company.
 *                For "is s divisible by 7" over s = 16 * hi + lo, "hi"
 *                and "lo" are each irrelevant and together sufficient:
 *                the worlds a question eliminates can make a later
 *                question decisive. The learner checks that case exactly
 *                (jointly_relevant, smarsh_learner.c); it used to miss it.
 *
 *   SUFFICIENT   EVERY possible answer leaves the target derived. Asking
 *                it settles the matter whatever comes back. Also derived,
 *                and knowable before asking.
 *
 * THE ORDERING AMONG THE REST IS A POLICY, AND IS MARKED AS ONE.
 * Which merely-helpful question to prefer is not settled by the axiom,
 * exactly as g() is not. So it is a named choice rather than a constant
 * buried in a comparison:
 *
 *   SR_ASK_GUARANTEE   most worlds eliminated in the WORST case. What you
 *                      get no matter which way the answer falls. A
 *                      guarantee, and it needs no probabilities to state,
 *                      which is why it is the default.
 *   SR_ASK_OPPORTUNITY most worlds eliminated in the BEST case. What you
 *                      could get if the answer is favourable. Worth
 *                      having, but it is a hope rather than a floor.
 *
 * Neither is an expectation. An expectation would need a distribution
 * over the answers, and there isn't one: the worlds are possibilities,
 * not outcomes with frequencies. Refusing to average over them is the
 * whole reason S is not a probability, and it applies here unchanged.
 *
 * Ties break to the lowest index. Deterministic, so a replay of the same
 * situation asks the same question, because a decision that cannot be
 * replayed is not evidence.
 */

typedef struct {
  /* Worlds that would survive for each answer. These PARTITION the live
     set, so they sum to it -- a checked invariant, not an assumption. */
  unsigned surviving[SR_MAX_ANSWERS];
  /* Answers the question can still have. An answer no live world gives is
     not a branch; treating it as one would let an impossible reply drag
     the worst case down. */
  uint64_t reachable;
  unsigned worst_case_removed;   /* the floor: true whatever the answer */
  unsigned best_case_removed;    /* the ceiling: true if it falls well */
  int sufficient;                /* every answer derives the target */
  int irrelevant;                /* no answer moves the target at all */
} sr_probe_t;

typedef enum {
  SR_ASK_GUARANTEE = 0,
  SR_ASK_OPPORTUNITY = 1
} sr_ask_policy_t;

/*
 * What this question would do, counted rather than guessed. Does not
 * modify the state: nothing is eliminated by asking what asking would do.
 */
sm_status_t sr_probe(const sr_state_t *s, const sr_query_t *target,
                     const sr_query_t *question, sr_probe_t *out);

/*
 * Pick the next question from `questions`. Writes the chosen index to
 * `out_index`, or `n_questions` to mean "do not ask any of these", which
 * happens in two derived cases:
 *
 *   - the target is already decided, so there is nothing to ask about;
 *   - every candidate is irrelevant, so no answer to any ONE of them
 *     could move the target now. That is not a proof the set is useless:
 *     candidates irrelevant alone can be decisive together (see
 *     IRRELEVANT above). A caller that needs to know checks the joint
 *     refinement.
 *
 * A sufficient question wins outright when one exists, since it ends the
 * matter whatever the answer, and that is derived rather than preferred.
 * Otherwise `policy` orders the merely-helpful ones.
 */
sm_status_t sr_choose(const sr_state_t *s, const sr_query_t *target,
                      const sr_query_t *questions, unsigned n_questions,
                      sr_ask_policy_t policy, unsigned *out_index,
                      sr_probe_t *out_probe);

/* ==================================================================== */
/* REFINEMENT: the worlds are not declared, they are forced             */
/* ==================================================================== */

/*
 * Everything above takes a world set as given, and that looked like the
 * one thing handed in from outside. It is not, and this section is why.
 *
 * A question is a total function from situations to answers, so each
 * question PARTITIONS the situations: two situations are in the same cell
 * when the question cannot tell them apart. Given a set of questions, the
 * minimal world set is the common refinement of those partitions. Any
 * finer is redundant, because no question can see the extra distinction.
 * Any coarser makes some question ill-defined, because it would have to
 * take two answers in one world.
 *
 * So worlds are not a modelling choice. GIVEN THE QUESTIONS, THEY ARE
 * FORCED. What has to be supplied is a set of questions, and you cannot
 * have reasoning without questions, so that is not a gap that can be
 * closed by anything -- it is the floor.
 *
 * ============================================================
 * WHAT THIS BUYS: DEFINABILITY IS DECIDABLE
 * ============================================================
 * sr_project takes a question stated over raw situations and re-expresses
 * it over the cells of a partition. It succeeds exactly when the question
 * is CONSTANT on every cell, and that is the whole test:
 *
 *   PROJECTS    the question is already definable from the questions that
 *               built this partition. It adds no distinction. It may
 *               still be worth having -- a shorter way to say something
 *               is worth having -- but it does not extend what can be
 *               told apart.
 *
 *   REFUSES     the question needs a distinction this partition cannot
 *               make. It is a genuinely new capacity, and refusing is the
 *               signal, not a failure.
 *
 * That is a decision procedure for "is this a new representational stage
 * or a restatement of the current one", and it is counted, not judged.
 */

#define SR_MAX_SITUATIONS SR_MAX_WORLDS
#define SR_MAX_QUESTIONS 64u

typedef struct {
  /* situation -> world. Cells are numbered in order of first appearance,
     so the numbering is a function of the input and a replay gives the
     same partition. */
  uint8_t cell[SR_MAX_SITUATIONS];
  unsigned n_situations;
  unsigned n_cells;      /* the minimal world count */
} sr_partition_t;

/*
 * The common refinement of the questions' partitions: the coarsest world
 * set on which all of them are well defined. Deriving this is what makes
 * the world count a RESULT rather than a parameter.
 */
sm_status_t sr_refine(const sr_query_t *questions, unsigned n_questions,
                      unsigned n_situations, sr_partition_t *out);

/*
 * Re-express a question over the cells. SM_ERR_INDEX_OUT_OF_DOMAIN when
 * the question is not constant on some cell, which means the partition is
 * too coarse for it -- see the note above; that refusal is the useful
 * result, not an error to route around.
 */
sm_status_t sr_project(const sr_partition_t *p, const sr_query_t *situation_q,
                       sr_query_t *out);

/*
 * Does adding this question tell apart situations the partition cannot?
 * 1 means a new distinction, 0 means it is already definable. Exactly the
 * complement of sr_project succeeding, computed without building the
 * projection.
 */
int sr_distinguishes(const sr_partition_t *p, const sr_query_t *situation_q);

/* ---- reopening a claim ---------------------------------------------- */

/*
 * The draft's section 1 says a token chain has "no operation for reopen an
 * earlier claim". Here there is one, and it is cheap, because the state is
 * a fixed-size value with no pointers in it: a checkpoint is a copy.
 *
 * Licensed eliminations never need reopening -- they are only wrong if the
 * constraint was wrong. Speculations are exactly the thing that does, so
 * sr_retract restores a checkpoint AND drops that guess from the ancestry,
 * so the debt disappears along with what it bought. Dropping one without
 * the other would leave either a phantom guess or an unpaid one.
 */
void sr_checkpoint(const sr_state_t *s, sr_state_t *out);

sm_status_t sr_retract(sr_state_t *s, const sr_state_t *checkpoint,
                       sm_ancestry_t *ancestry, unsigned guess_id);

/*
 * ============================================================
 * WHAT THE DRAFT ASKS FOR THAT THIS DOES NOT ANSWER
 * ============================================================
 * The draft names its own bottleneck: a confidence signal reliable enough
 * to say "this claim is ungrounded". This file does not provide one, and
 * does not try. It makes the question unnecessary instead -- S is not a
 * confidence and predicts nothing, so there is no correlation for it to
 * have or to lack. It is a count of what has been eliminated, and it
 * cannot be miscalibrated any more than a subtraction can.
 *
 * That is a real answer to that bottleneck and it is not a free one. It
 * moves the difficulty rather than removing it: the kernel is exact GIVEN
 * a world set, and building the world set out of unstructured input is
 * now where the whole problem lives. That is the formulation step, it is
 * unsolved, and nothing in this file touches it.
 *
 * Also untouched: visual grounding as a reasoning substrate (draft 5.1),
 * context as retrieval (5.2), and any bridge to a generative model at all
 * -- this reasons over worlds, it does not emit language.
 */

#endif /* SMARSH_REASON_H */
