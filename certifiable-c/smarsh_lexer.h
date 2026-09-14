/*
 * smarsh_lexer.h -- Smarsh's lexer in C, ported from ../../smarsh/src/lexer.js.
 *
 * COMPILED AND RUN: test_frontend.c checks it against the JS engine (76
 * of 76 cases identical). The token set, keyword list,
 * contextual-keyword list and operator table below are transcribed from
 * the JS lexer that IS tested (985 tests, 3,065-program differential
 * oracle), so a divergence between the two is a bug in this file.
 *
 * Static allocation throughout: tokens go into a caller-owned array with
 * a caller-stated capacity, and running out is a checked error return
 * rather than a realloc or a truncation. Source length is bounded by
 * SL_MAX_SOURCE for the same WCET reason as the core.
 *
 * WHAT IS DELIBERATELY NOT HERE YET:
 *   - Template strings (`"${x}"`). The JS lexer produces a nested token
 *     structure for these; doing that without allocation needs a
 *     flattened representation, and inventing one before the parser
 *     exists would be guessing at what the parser wants. Interpolated
 *     strings currently return SL_ERR_UNSUPPORTED rather than being
 *     silently lexed wrong.
 *   - Comment retention. The JS lexer keeps comments so `smarsh fmt` can
 *     put them back. Nothing in C consumes them yet, so they are skipped
 *     rather than stored in a buffer nobody reads.
 * Both are stated because a port that quietly handles less than the
 * original, without saying so, is the kind of thing that gets found much
 * later by a program that used to work.
 */

#ifndef SMARSH_LEXER_H
#define SMARSH_LEXER_H

#include <stddef.h>

#define SL_MAX_SOURCE 65536u
#define SL_MAX_TOKEN_TEXT 256u

typedef enum {
  SL_TOK_EOF = 0,
  SL_TOK_IDENT = 1,
  SL_TOK_KW = 2,
  SL_TOK_NUM = 3,     /* 42, 1.5   -- a float in the runtime */
  SL_TOK_DEC = 4,     /* 1.50d     -- exact decimal, digits kept verbatim */
  SL_TOK_STR = 5,
  SL_TOK_OP = 6
} sl_kind_t;

typedef enum {
  SL_OK = 0,
  SL_ERR_SOURCE_TOO_LONG = 1,
  SL_ERR_TOO_MANY_TOKENS = 2,
  SL_ERR_TOKEN_TOO_LONG = 3,
  SL_ERR_UNTERMINATED_STRING = 4,
  SL_ERR_UNEXPECTED_CHAR = 5,
  SL_ERR_UNSUPPORTED = 6,      /* template strings, for now */
  SL_ERR_NULL_ARGUMENT = 7
} sl_status_t;

typedef struct {
  sl_kind_t kind;
  char text[SL_MAX_TOKEN_TEXT];
  unsigned line;
  unsigned start;
  /* Set when a newline was passed since the previous token. The parser
     uses this to stop a call chain at a line break -- it is not
     cosmetic, it changes what parses. */
  int nl_before;
} sl_token_t;

/*
 * Tokenize `source` into `out`, at most `capacity` tokens including the
 * terminating EOF. On success returns SL_OK and writes the count to
 * *out_count. On any error returns the status and leaves *out_count at
 * the number of tokens successfully produced before the failure, so a
 * caller can report where lexing stopped. `err_line` receives the line
 * the failure occurred on, or 0 if not applicable.
 */
sl_status_t sl_tokenize(const char *source, sl_token_t *out, unsigned capacity,
                        unsigned *out_count, unsigned *err_line);

/* Exposed for the parser and for tests: is this identifier a reserved
   keyword? Contextual words (record, region, secret, ...) are NOT
   keywords and return 0 here -- they are recognised by the parser in the
   one position each can appear, so that a program may still use them as
   ordinary names. */
int sl_is_keyword(const char *word);
int sl_is_contextual(const char *word);

#endif /* SMARSH_LEXER_H */
