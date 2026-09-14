/*
 * test_frontend.c -- the C lexer, parser and interpreter, each diffed
 * against the real JavaScript Smarsh engine.
 *
 * Replaces verify_lexer.py, verify_parser.py and verify_interp.py, and is
 * stronger than they were. Those checked a Python IMITATION of the C
 * against the JS engine, because nothing could compile the C. This runs
 * the actual C and compares it against the engine the C was ported from:
 *
 *   lexer        token stream, printed in the exact JSON the JS dumper
 *                prints, compared as strings
 *   parser       tree, normalised to kind + text + ordered children,
 *                compared as strings
 *   interpreter  every line a whole program prints, against
 *                `node smarsh.mjs run`
 *
 * Needs node on the PATH and the Smarsh repo at C:/Users/USER/smarsh.
 * The two JS dumpers live in ./js and read their source from a file, not
 * the command line, since test programs contain < and > and a shell would
 * redirect them. The tree serialiser below recurses; that is fine in a
 * test, where depth is bounded by SP_MAX_DEPTH, and the runtime code it
 * checks still does not.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smarsh_interp.h"
#include "smarsh_lexer.h"
#include "smarsh_parser.h"

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

#define CASE_FILE "frontend_case.tmp"
#define SMARSH_CLI "C:/Users/USER/smarsh/bin/smarsh.mjs"
#define MAX_TOKENS 2048u

static int n_checks = 0, n_failed = 0;

static sl_token_t TOKS[MAX_TOKENS];
static sp_arena_t ARENA;
static sv_heap_t HEAP;
static si_result_t RUN;

static char mine[65536];
static char theirs[65536];

/* ---- plumbing ------------------------------------------------------ */

static int write_case(const char *src) {
  FILE *f = fopen(CASE_FILE, "wb");
  if (f == 0) return 0;
  fputs(src, f);
  fclose(f);
  return 1;
}

/* Run a command, capture stdout, strip CR and one trailing newline. */
static int run_capture(const char *cmd, char *out, size_t cap, int *exit_code) {
  FILE *p = POPEN(cmd, "r");
  size_t n = 0u;
  int c;
  if (p == 0) return 0;
  while ((c = fgetc(p)) != EOF && n + 1u < cap) {
    if (c != '\r') out[n++] = (char)c;
  }
  out[n] = '\0';
  *exit_code = PCLOSE(p);
  while (n > 0u && out[n - 1u] == '\n') out[--n] = '\0';
  return 1;
}

static void emit(char **w, const char *s) {
  size_t L = strlen(s);
  memcpy(*w, s, L);
  *w += L;
  **w = '\0';
}

/* JSON.stringify's string escaping, exactly. */
static void emit_json_string(char **w, const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  emit(w, "\"");
  for (; *p != '\0'; p++) {
    char buf[8];
    switch (*p) {
      case '"': emit(w, "\\\""); break;
      case '\\': emit(w, "\\\\"); break;
      case '\b': emit(w, "\\b"); break;
      case '\f': emit(w, "\\f"); break;
      case '\n': emit(w, "\\n"); break;
      case '\r': emit(w, "\\r"); break;
      case '\t': emit(w, "\\t"); break;
      default:
        if (*p < 0x20u) {
          sprintf(buf, "\\u%04x", (unsigned)*p);
          emit(w, buf);
        } else {
          buf[0] = (char)*p;
          buf[1] = '\0';
          emit(w, buf);
        }
    }
  }
  emit(w, "\"");
}

static const char *kind_name(sl_kind_t k) {
  switch (k) {
    case SL_TOK_EOF: return "eof";
    case SL_TOK_IDENT: return "ident";
    case SL_TOK_KW: return "kw";
    case SL_TOK_NUM: return "num";
    case SL_TOK_DEC: return "dec";
    case SL_TOK_STR: return "str";
    default: return "op";
  }
}

static void report(const char *label, int ok, const char *why) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", label);
  if (!ok && why != 0 && why[0] != '\0') printf("          %s\n", why);
  n_checks++;
  if (!ok) n_failed++;
}

static void label_of(const char *src, char *out) {
  size_t i, o = 0u;
  for (i = 0u; src[i] != '\0' && o < 46u; i++) {
    if (src[i] == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
    else out[o++] = src[i];
  }
  out[o] = '\0';
  if (strlen(src) > 46u) strcpy(out + 43, "...");
}

/* ---- the lexer ----------------------------------------------------- */

static void lexer_case(const char *src) {
  unsigned count = 0u, err_line = 0u, i;
  sl_status_t st = sl_tokenize(src, TOKS, MAX_TOKENS, &count, &err_line);
  int js_err = 0, code;
  char label[64], why[200];
  char *w = mine;

  write_case(src);
  run_capture("node js/dump_tokens.mjs " CASE_FILE, theirs, sizeof theirs, &code);
  js_err = strncmp(theirs, "{\"error\"", 8) == 0;
  label_of(src, label);

  if (js_err || st != SL_OK) {
    sprintf(why, "JS %s, C %s", js_err ? "errored" : "did not",
            st != SL_OK ? "errored" : "did not");
    report(label, js_err && st != SL_OK, why);
    return;
  }
  emit(&w, "[");
  for (i = 0u; i < count; i++) {
    if (i) emit(&w, ",");
    emit(&w, "{\"kind\":");
    emit_json_string(&w, kind_name(TOKS[i].kind));
    emit(&w, ",\"text\":");
    emit_json_string(&w, TOKS[i].kind == SL_TOK_EOF ? "" : TOKS[i].text);
    emit(&w, TOKS[i].nl_before ? ",\"nl\":true}" : ",\"nl\":false}");
  }
  emit(&w, "]");
  sprintf(why, "C  %.80s\n          JS %.80s", mine, theirs);
  report(label, strcmp(mine, theirs) == 0, why);
}

/* ---- the parser ---------------------------------------------------- */

static void emit_node(char **w, const sp_arena_t *A, sp_node_id_t id);

static void emit_kid(char **w, const sp_arena_t *A, sp_node_id_t id, int *first) {
  if (id == SP_NO_NODE) return;
  if (!*first) emit(w, ",");
  *first = 0;
  emit_node(w, A, id);
}

static void emit_node(char **w, const sp_arena_t *A, sp_node_id_t id) {
  const sp_node_t *n = sp_node_const(A, id);
  const char *t = "?";
  const char *x = "";
  int first = 1;
  unsigned i;
  /* which slots are children differs by kind: DECLARE's b and BOOL's a
     are flags, not node ids, and must not be followed */
  switch (n->kind) {
    case SP_PROGRAM: t = "Program"; break;
    case SP_NUM: t = "Num"; x = sp_string(A, n->text); break;
    case SP_DEC_LIT: t = "DecLit"; x = sp_string(A, n->text); break;
    case SP_STR: t = "Str"; x = sp_string(A, n->text); break;
    case SP_BOOL: t = "Bool"; x = n->a ? "true" : "false"; break;
    case SP_NIL: t = "Nil"; break;
    case SP_IDENT: t = "Ident"; x = sp_string(A, n->text); break;
    case SP_LIST_LIT: t = "ListLit"; break;
    case SP_UNARY: t = "Unary"; x = sp_string(A, n->text); break;
    case SP_BINARY: t = "Binary"; x = sp_string(A, n->text); break;
    case SP_LOGICAL: t = "Logical"; x = sp_string(A, n->text); break;
    case SP_CALL: t = "Call"; break;
    case SP_INDEX: t = "Index"; break;
    case SP_MEMBER: t = "Member"; x = sp_string(A, n->text); break;
    case SP_ASSIGN: t = "Assign"; break;
    case SP_BLOCK: t = "Block"; break;
    case SP_EXPR_STMT: t = "ExprStmt"; break;
    case SP_DECLARE: t = "Declare"; x = sp_string(A, n->text); break;
    case SP_IF: t = "If"; break;
    case SP_WHILE: t = "While"; break;
    case SP_RETURN: t = "Return"; break;
    case SP_BREAK: t = "Break"; break;
    case SP_CONTINUE: t = "Continue"; break;
    default: break;
  }
  emit(w, "{\"t\":");
  emit_json_string(w, t);
  emit(w, ",\"x\":");
  emit_json_string(w, x);
  emit(w, ",\"k\":[");
  switch (n->kind) {
    case SP_UNARY: case SP_MEMBER: case SP_EXPR_STMT: case SP_DECLARE:
    case SP_RETURN:
      emit_kid(w, A, n->a, &first);
      break;
    case SP_BINARY: case SP_LOGICAL: case SP_ASSIGN: case SP_WHILE:
      emit_kid(w, A, n->a, &first);
      emit_kid(w, A, n->b, &first);
      break;
    case SP_IF:
      emit_kid(w, A, n->a, &first);
      emit_kid(w, A, n->b, &first);
      emit_kid(w, A, n->c, &first);
      break;
    case SP_CALL: case SP_INDEX:
      emit_kid(w, A, n->a, &first);
      for (i = 0u; i < n->child_count; i++) emit_kid(w, A, sp_child(A, id, i), &first);
      break;
    case SP_PROGRAM: case SP_BLOCK: case SP_LIST_LIT:
      for (i = 0u; i < n->child_count; i++) emit_kid(w, A, sp_child(A, id, i), &first);
      break;
    default:
      break;
  }
  emit(w, "]}");
}

static void parser_case(const char *src) {
  unsigned count = 0u, err_line = 0u;
  sl_status_t lst = sl_tokenize(src, TOKS, MAX_TOKENS, &count, &err_line);
  sp_parse_result_t pr;
  int js_err, code, c_err;
  char label[64], why[400];
  char *w = mine;

  pr.status = SP_ERR_UNEXPECTED_TOKEN;
  if (lst == SL_OK) pr = sp_parse(TOKS, count, &ARENA);
  c_err = lst != SL_OK || pr.status != SP_OK;
  write_case(src);
  run_capture("node js/dump_ast.mjs " CASE_FILE, theirs, sizeof theirs, &code);
  js_err = strncmp(theirs, "{\"error\"", 8) == 0;
  label_of(src, label);
  if (js_err || c_err) {
    sprintf(why, "JS %s, C %s", js_err ? "errored" : "did not", c_err ? "errored" : "did not");
    report(label, js_err && c_err, why);
    return;
  }
  emit_node(&w, &ARENA, ARENA.root);
  sprintf(why, "C  %.150s\n          JS %.150s", mine, theirs);
  report(label, strcmp(mine, theirs) == 0, why);
}

/* ---- the interpreter ----------------------------------------------- */

static void interp_case(const char *src) {
  unsigned count = 0u, err_line = 0u;
  sl_status_t lst = sl_tokenize(src, TOKS, MAX_TOKENS, &count, &err_line);
  int code, c_err = 1;
  char label[64], why[400];

  mine[0] = '\0';
  if (lst == SL_OK) {
    sp_parse_result_t pr = sp_parse(TOKS, count, &ARENA);
    if (pr.status == SP_OK) {
      si_status_t st = si_run(&ARENA, &HEAP, SI_DEFAULT_STEPS, &RUN);
      if (st == SI_OK) {
        c_err = 0;
        memcpy(mine, RUN.output, RUN.output_used);
        mine[RUN.output_used] = '\0';
      }
    }
  }
  {
    size_t n = strlen(mine);
    while (n > 0u && mine[n - 1u] == '\n') mine[--n] = '\0';
  }
  write_case(src);
  run_capture("node " SMARSH_CLI " run " CASE_FILE, theirs, sizeof theirs, &code);
  label_of(src, label);
  if (code != 0 || c_err) {
    sprintf(why, "JS %s, C %s", code != 0 ? "errored" : "did not", c_err ? "errored" : "did not");
    report(label, code != 0 && c_err, why);
    return;
  }
  sprintf(why, "C  [%.120s]\n          JS [%.120s]", mine, theirs);
  report(label, strcmp(mine, theirs) == 0, why);
}

/* ---- cases, as in the Python suites they replace ------------------- */

static const char *LEX_CASES[] = {
  "let x = 42", "var y = 3.5", "let m = 1.50d",
  "fn f(a) needs fs { return a ** 2 }", "if a == b { } else { }",
  "let s = \"hello\"", "let s = \"tab\\there\"", "while i < 10 { i = i + 1 }",
  "let xs = [1, 2, 3]", "record Point(x, y)", "let region = 5",
  "attempt { f() } rescue e { }", "a.b.c", "x <= y and y >= z or not w",
  "maybe 0.5 { }", "match v { when 1 => 2 }", "// a comment\nlet z = 1",
  "let a = 1\nlet b = 2", "fn g() requires x > 0 ensures result != nil { }",
  "agent A { on msg(m) { } }"
};

static const char *PARSE_CASES[] = {
  "let x = 1 + 2 * 3", "let x = 1 * 2 + 3", "let x = 1 - 2 - 3",
  "let x = 2 ** 3 ** 2", "let x = 0 - 2 ** 2", "let x = a == b and c != d",
  "let x = a < b or c >= d", "let x = not a and b", "let x = (1 + 2) * 3",
  "let x = f(1, 2)", "let x = a.b.c", "let x = xs[0]", "let x = f(a)(b)",
  "let x = [1, 2, 3]", "var y = 1", "if a { } else { }",
  "while a < 10 { }", "return", "x = 5",
  /* multi-statement, with child lists nested inside child lists. The C
     parser failed every one of these until the pending-children fix; the
     single-statement cases above all passed while it was broken, which is
     why these were added rather than trusting them. */
  "let x = 5\nprint(x)",
  "f(g(1), [2, h(3)])[0]",
  "if a { f(1)\n g(2) } else { h([3]) }",
  "while a { b = f(x, y)\n c = [d, e(k)] }",
  "let q = [[1, 2], [f(3), [4]]]\nprint(q[1][0])"
};

static const char *RUN_CASES[] = {
  "print(1 + 2)", "print(2 * 3 + 4)", "print(2 + 3 * 4)", "print(10 - 3 - 2)",
  "print(2 ** 3 ** 2)", "print(7 / 2)", "print(7 % 3)", "print(1 < 2)",
  "print(2 == 2)", "print(1 != 2)", "print(true and false)",
  "print(true or false)", "print(not true)", "print(\"hi\")",
  "print(\"a\" + \"b\")", "let x = 5\nprint(x)", "let x = 5\nlet y = x * 2\nprint(y)",
  "var i = 0\nwhile i < 3 { i = i + 1 }\nprint(i)",
  "if 1 < 2 { print(\"yes\") } else { print(\"no\") }",
  "if 2 < 1 { print(\"yes\") } else { print(\"no\") }",
  "print([1, 2, 3])", "print(len([1, 2, 3]))", "let xs = [10, 20]\nprint(xs[1])",
  "print(str(42))", "var t = 0\nvar i = 0\nwhile i < 5 { t = t + i\n i = i + 1 }\nprint(t)",
  /* Nested list literals. Every case above used flat lists, and nested
     ones were broken: the inner list was allocated over the outer list's
     slots, so [1, [2, 3]] contained itself and printing it crashed. */
  "let a = [1, [2, 3]]\nprint(a)", "print([[2]] == [[2]])",
  "print([1, [2, 3]] == [1, [2, 3]])", "print([1, [2]] != [1, [3]])",
  "let q = [[1, 2], [3, [4, 5]]]\nprint(q[1][1][0])",
  /* longer than the old 64-byte per-element buffer, which cut it short */
  "print([[1000000, 2000000, 3000000, 4000000, 5000000, 6000000, 7000000, 8000000, 9000000]])",
  "var a = [1]\nvar b = [1]\nvar i = 0\nwhile i < 40 { a = [a]\n b = [b]\n i = i + 1 }\nprint(a == b)"
};

/* C-only: where the C deliberately differs from the JS engine, because it
   must have a stack bound and JS need not. Lists nested past
   SV_MAX_EQ_DEPTH compare as a checked error; JS compares them. */
static void c_only_depth_case(void) {
  static const char *deep =
      "var a = [1]\nvar b = [1]\nvar i = 0\n"
      "while i < 70 { a = [a]\n b = [b]\n i = i + 1 }\nprint(a == b)";
  unsigned count = 0u, err_line = 0u;
  si_status_t st = SI_OK;
  n_checks++;
  if (sl_tokenize(deep, TOKS, MAX_TOKENS, &count, &err_line) == SL_OK) {
    sp_parse_result_t pr = sp_parse(TOKS, count, &ARENA);
    if (pr.status == SP_OK) st = si_run(&ARENA, &HEAP, SI_DEFAULT_STEPS, &RUN);
  }
  if (st == SI_ERR_DEPTH_EXCEEDED) {
    printf("  ok    lists nested 71 deep: == is a checked depth error, not a stack overflow\n");
  } else {
    printf("  FAIL  lists nested 71 deep: expected SI_ERR_DEPTH_EXCEEDED, got %d\n", (int)st);
    n_failed++;
  }
  {
    static const char *deep_print =
        "var a = [1]\nvar i = 0\nwhile i < 70 { a = [a]\n i = i + 1 }\nprint(a)";
    st = SI_OK;
    n_checks++;
    if (sl_tokenize(deep_print, TOKS, MAX_TOKENS, &count, &err_line) == SL_OK) {
      sp_parse_result_t pr = sp_parse(TOKS, count, &ARENA);
      if (pr.status == SP_OK) st = si_run(&ARENA, &HEAP, SI_DEFAULT_STEPS, &RUN);
    }
    if (st == SI_ERR_DEPTH_EXCEEDED) {
      printf("  ok    and printing it is a checked depth error too\n");
    } else {
      printf("  FAIL  printing lists nested 71 deep: expected SI_ERR_DEPTH_EXCEEDED, got %d\n",
             (int)st);
      n_failed++;
    }
  }
}

int main(void) {
  unsigned i;
  int code;

  if (!run_capture("node --version", theirs, sizeof theirs, &code) || code != 0) {
    printf("node is not on the PATH; this test compares against the JS engine\n");
    return 2;
  }

  printf("C lexer vs the real JS lexer, token stream by token stream\n");
  for (i = 0u; i < sizeof LEX_CASES / sizeof LEX_CASES[0]; i++) lexer_case(LEX_CASES[i]);

  printf("\nC parser vs the real JS parser, tree by tree\n");
  for (i = 0u; i < sizeof PARSE_CASES / sizeof PARSE_CASES[0]; i++) parser_case(PARSE_CASES[i]);

  printf("\nC interpreter vs the real Smarsh runtime, whole programs\n");
  for (i = 0u; i < sizeof RUN_CASES / sizeof RUN_CASES[0]; i++) interp_case(RUN_CASES[i]);

  printf("\nC-only: a stated divergence, for a stack bound\n");
  c_only_depth_case();

  remove(CASE_FILE);
  printf("\n");
  if (n_failed) {
    printf("%d of %d cases diverge from the JS engine\n", n_failed, n_checks);
    return 1;
  }
  printf("all %d cases match the JS engine exactly\n", n_checks);
  return 0;
}
