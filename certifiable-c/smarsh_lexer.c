/*
 * smarsh_lexer.c -- see smarsh_lexer.h for the disclosure and for what is
 * deliberately not ported yet.
 */

#include "smarsh_lexer.h"

#include <string.h>

/* Transcribed from KEYWORDS in ../../smarsh/src/lexer.js. Order is
   irrelevant (linear compare), but the CONTENTS must match exactly: a
   word missing here lexes as an identifier and changes what parses. */
static const char *const KEYWORDS[] = {
  "let", "var", "fn", "return", "if", "else", "while", "for", "in",
  "true", "false", "nil", "and", "or", "not", "break", "continue",
  "maybe", "choose", "fork", "tensor", "needs", "requires", "ensures",
  "attempt", "rescue",
  "agent", "on", "spawn", "redefine", "import", "as",
  "invariant", "variant", "using",
  "match", "when",
  0
};

/* NOT reserved. Recognised by the parser only where they cannot be
   anything else, so a program may still have a field called `record` or
   `region`. The JS lexer's comment records that this was found the hard
   way twice -- an agent handler named `record`, then a customer field
   named `region`. */
static const char *const CONTEXTUAL[] = {
  "record", "region", "secret", "atomic", "grounded", "device", "budget",
  "authority",
  0
};

/* Longest first: the matcher takes the first that fits, so "==" must be
   tried before "=" or it lexes as two tokens. */
static const char *const PUNCT[] = {
  "**", "==", "!=", "<=", ">=", "=>", "&&", "||",
  "+", "-", "*", "/", "%", "@", "<", ">", "=",
  "(", ")", "{", "}", "[", "]", ",", ";", ".", ":", "!",
  0
};

static int is_digit(char c) {
  return c >= '0' && c <= '9';
}

static int is_ident_start(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static int is_ident_part(char c) {
  return is_ident_start(c) || is_digit(c);
}

static int in_list(const char *const *list, const char *word) {
  unsigned i;
  /* Bounded by the table's own NUL terminator, which is a compile-time
     constant array -- not by anything a caller supplies. */
  for (i = 0u; list[i] != 0; i++) {
    if (strcmp(list[i], word) == 0) {
      return 1;
    }
  }
  return 0;
}

int sl_is_keyword(const char *word) {
  if (word == 0) {
    return 0;
  }
  return in_list(KEYWORDS, word);
}

int sl_is_contextual(const char *word) {
  if (word == 0) {
    return 0;
  }
  return in_list(CONTEXTUAL, word);
}

/* Copy [from,to) out of source into a token's fixed buffer. Overlong is a
   checked error, never a truncation: a silently shortened identifier
   would resolve to the wrong name. */
static sl_status_t copy_text(sl_token_t *tok, const char *source,
                             unsigned from, unsigned to) {
  unsigned len = to - from;
  unsigned i;

  if (len >= SL_MAX_TOKEN_TEXT) {
    return SL_ERR_TOKEN_TOO_LONG;
  }
  for (i = 0u; i < len; i++) {
    tok->text[i] = source[from + i];
  }
  tok->text[len] = '\0';
  return SL_OK;
}

sl_status_t sl_tokenize(const char *source, sl_token_t *out, unsigned capacity,
                        unsigned *out_count, unsigned *err_line) {
  unsigned n;
  unsigned i = 0u;
  unsigned line = 1u;
  unsigned count = 0u;
  int pending_newline = 0;
  sl_status_t st;

  if (source == 0 || out == 0 || out_count == 0 || err_line == 0) {
    return SL_ERR_NULL_ARGUMENT;
  }
  *out_count = 0u;
  *err_line = 0u;

  n = (unsigned)strlen(source);
  if (n > SL_MAX_SOURCE) {
    return SL_ERR_SOURCE_TOO_LONG;
  }
  if (capacity == 0u) {
    return SL_ERR_TOO_MANY_TOKENS;
  }

  /* Bounded by SL_MAX_SOURCE, checked above. Every branch inside either
     consumes at least one character or returns, so this terminates. */
  while (i < n) {
    char c = source[i];

    if (c == '\n') {
      line++;
      pending_newline = 1;
      i++;
      continue;
    }
    if (c == ' ' || c == '\t' || c == '\r') {
      i++;
      continue;
    }

    /* Comments. Skipped rather than retained -- see the header. */
    if (c == '/' && (i + 1u) < n && source[i + 1u] == '/') {
      while (i < n && source[i] != '\n') {
        i++;
      }
      continue;
    }

    if (count + 1u >= capacity) {
      /* +1 so there is always room for the EOF token below. */
      *out_count = count;
      *err_line = line;
      return SL_ERR_TOO_MANY_TOKENS;
    }

    /* --- numbers ------------------------------------------------- */
    if (is_digit(c)) {
      unsigned start = i;
      int is_dec = 0;

      while (i < n && is_digit(source[i])) {
        i++;
      }
      if (i < n && source[i] == '.' && (i + 1u) < n && is_digit(source[i + 1u])) {
        i++;
        while (i < n && is_digit(source[i])) {
          i++;
        }
      }
      /* A trailing `d` makes it an exact decimal, and the digits are kept
         verbatim -- reading them through a float first is exactly the
         rounding this type exists to avoid. */
      if (i < n && source[i] == 'd') {
        is_dec = 1;
      }

      out[count].kind = is_dec ? SL_TOK_DEC : SL_TOK_NUM;
      st = copy_text(&out[count], source, start, i);
      if (st != SL_OK) {
        *out_count = count;
        *err_line = line;
        return st;
      }
      if (is_dec) {
        i++; /* consume the 'd', which is not part of the digits */
      }
      out[count].line = line;
      out[count].start = start;
      out[count].nl_before = pending_newline;
      pending_newline = 0;
      count++;
      continue;
    }

    /* --- identifiers and keywords -------------------------------- */
    if (is_ident_start(c)) {
      unsigned start = i;

      while (i < n && is_ident_part(source[i])) {
        i++;
      }
      st = copy_text(&out[count], source, start, i);
      if (st != SL_OK) {
        *out_count = count;
        *err_line = line;
        return st;
      }
      out[count].kind = sl_is_keyword(out[count].text) ? SL_TOK_KW : SL_TOK_IDENT;
      out[count].line = line;
      out[count].start = start;
      out[count].nl_before = pending_newline;
      pending_newline = 0;
      count++;
      continue;
    }

    /* --- strings -------------------------------------------------- */
    if (c == '"') {
      unsigned start = i;
      unsigned wrote = 0u;

      i++; /* opening quote */
      while (i < n && source[i] != '"') {
        char ch = source[i];

        if (ch == '$' && (i + 1u) < n && source[i + 1u] == '{') {
          /* Interpolation. Refused rather than lexed as a plain string,
             which would silently drop the expression inside it. */
          *out_count = count;
          *err_line = line;
          return SL_ERR_UNSUPPORTED;
        }
        if (ch == '\\' && (i + 1u) < n) {
          char esc = source[i + 1u];
          i += 2u;
          if (esc == 'n') {
            ch = '\n';
          } else if (esc == 't') {
            ch = '\t';
          } else if (esc == 'r') {
            ch = '\r';
          } else {
            ch = esc; /* \" \\ and anything else stands for itself */
          }
        } else {
          if (ch == '\n') {
            line++;
          }
          i++;
        }
        if (wrote + 1u >= SL_MAX_TOKEN_TEXT) {
          *out_count = count;
          *err_line = line;
          return SL_ERR_TOKEN_TOO_LONG;
        }
        out[count].text[wrote] = ch;
        wrote++;
      }
      if (i >= n) {
        *out_count = count;
        *err_line = line;
        return SL_ERR_UNTERMINATED_STRING;
      }
      i++; /* closing quote */
      out[count].text[wrote] = '\0';
      out[count].kind = SL_TOK_STR;
      out[count].line = line;
      out[count].start = start;
      out[count].nl_before = pending_newline;
      pending_newline = 0;
      count++;
      continue;
    }

    /* --- operators ------------------------------------------------ */
    {
      unsigned k;
      int matched = 0;

      for (k = 0u; PUNCT[k] != 0; k++) {
        unsigned len = (unsigned)strlen(PUNCT[k]);
        if (i + len <= n && strncmp(&source[i], PUNCT[k], (size_t)len) == 0) {
          st = copy_text(&out[count], source, i, i + len);
          if (st != SL_OK) {
            *out_count = count;
            *err_line = line;
            return st;
          }
          out[count].kind = SL_TOK_OP;
          out[count].line = line;
          out[count].start = i;
          out[count].nl_before = pending_newline;
          pending_newline = 0;
          count++;
          i += len;
          matched = 1;
          break;
        }
      }
      if (matched != 0) {
        continue;
      }
    }

    *out_count = count;
    *err_line = line;
    return SL_ERR_UNEXPECTED_CHAR;
  }

  out[count].kind = SL_TOK_EOF;
  out[count].text[0] = '\0';
  out[count].line = line;
  out[count].start = n;
  out[count].nl_before = pending_newline;
  count++;

  *out_count = count;
  return SL_OK;
}
