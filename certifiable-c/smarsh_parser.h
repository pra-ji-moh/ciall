/*
 * smarsh_parser.h -- Smarsh's parser in C, ported from
 * ../../smarsh/src/parser.js.
 *
 * COMPILED AND RUN: test_frontend.c checks it against the JS engine (76
 * of 76 cases identical). Compiling it found a real bug the Python check
 * had hidden by imitating it: children of a list, call, index, block or
 * program could be stored non-contiguously. Fixed with the pending-
 * children stack in smarsh_ast.h.
 *
 * ============================================================
 * RECURSION: A DEVIATION FROM THIS DIRECTORY'S CONVENTION, STATED
 * ============================================================
 * boundary_kernel.h sets the convention here as "No recursion anywhere
 * (recursion depth is not generally statically WCET-bounded without
 * dedicated analysis)". THIS FILE DOES NOT MEET THAT. It is worth being
 * exact about, because the first draft of this comment claimed it did.
 *
 * What is true: operator PRECEDENCE is handled without recursion, by
 * shunting-yard with two fixed-size explicit stacks, so the deep chain
 * the JS parser walks per precedence level (or -> and -> equality ->
 * comparison -> additive -> multiplicative -> unary -> power) costs no
 * stack here at all.
 *
 * What is still recursive, and unavoidably so without a full explicit
 * state machine:
 *   parse_expression -> parse_operand -> parse_expression
 *     for a parenthesised group, a list literal's elements, and a call's
 *     arguments
 *   parse_statement -> parse_block -> parse_statement
 *     for a nested block
 *
 * Both are bounded: every entry checks `depth >= SP_MAX_DEPTH` and
 * returns SP_ERR_DEPTH_EXCEEDED, so the worst-case call depth is a
 * compile-time constant (64) rather than a function of input size. That
 * gives a statically known stack bound, which is the property WCET
 * analysis actually needs -- but it is a WEAKER claim than "no
 * recursion", and stating the stronger one would have been false.
 *
 * Removing it entirely means an explicit work stack holding partially
 * built nodes across a resumable state machine. That is a real piece of
 * work and it is not done here.
 *
 * Precedence, transcribed from the JS chain
 * (assignment < or < and < equality < comparison < additive <
 *  multiplicative < unary < power < callChain < primary):
 *
 *   or               1
 *   and              2
 *   == !=            3
 *   < > <= >=        4
 *   + -              5
 *   * / % @          6
 *   **               7, RIGHT associative
 *
 * `power` binds tighter than `unary` in the JS chain, so `-2 ** 2`
 * parses as `-(2 ** 2)`, not `(-2) ** 2`. That is easy to get backwards
 * and changes results, so it is stated here and tested.
 *
 * ============================================================
 * COVERAGE, STATED RATHER THAN IMPLIED
 * ============================================================
 * Built: expressions (all operators above, unary, calls, indexing,
 * member access, list literals, parenthesised groups), and the
 * statements let/var, expression statements, if/else, while, return,
 * break, continue, blocks.
 *
 * NOT built yet, and returning SP_ERR_UNSUPPORTED rather than parsing
 * wrong: fn/agent/record declarations, for-in, match, attempt/rescue,
 * maybe/choose/fork, and every contextual block (region, secret,
 * grounded, budget, authority, device, atomic, release_to). The AST
 * enum names all of them so adding one is filling in a case; the
 * parser refusing loudly is the difference between a port that is
 * incomplete and a port that is wrong.
 */

#ifndef SMARSH_PARSER_H
#define SMARSH_PARSER_H

#include "smarsh_ast.h"
#include "smarsh_lexer.h"

#define SP_MAX_DEPTH 64u

typedef struct {
  sp_status_t status;
  unsigned line;
  /* The token index parsing stopped at, so a caller can point at it
     rather than reporting only that something went wrong somewhere. */
  unsigned token_index;
} sp_parse_result_t;

/*
 * Parse `tokens` into `arena`. On success arena->root is the SP_PROGRAM
 * node. On failure the arena is left as-is (partially built) and the
 * result carries where and why -- a partial tree is never presented as
 * a complete one, because arena->root stays SP_NO_NODE unless the whole
 * program parsed.
 */
sp_parse_result_t sp_parse(const sl_token_t *tokens, unsigned token_count,
                           sp_arena_t *arena);

#endif /* SMARSH_PARSER_H */
