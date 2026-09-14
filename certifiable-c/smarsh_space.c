/*
 * smarsh_space.c -- see smarsh_space.h.
 *
 * Recursion: the parser and the printer recurse on the shape of the
 * expression, each call passing depth + 1 and refusing past SX_MAX_DEPTH,
 * so their stack is bounded. Evaluation, propagation and search are loops
 * over the node array (children always come before parents) and an
 * explicit region stack.
 */

#include "smarsh_space.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SX_MAX_DEPTH 64u
#define SX_PRINT_DEPTH 256u
#define SX_STACK 4096u

/* workspace: one question at a time (stated in the header) */
static iv_t VAL[SX_MAX_NODES];
static iv_t TGT[SX_MAX_NODES];
static sx_box_t STACK[SX_STACK];

void sx_init(sx_theory_t *th) {
  if (th != 0) memset(th, 0, sizeof *th);
}

sx_opts_t sx_default_opts(void) {
  sx_opts_t o;
  o.rel_eps = 1e-3;
  o.value_tol = 1e-3;
  o.max_regions = 400000u;
  return o;
}

sm_status_t sx_var(sx_theory_t *th, const char *name, sx_type_t type, double lo, double hi,
                   unsigned *idx) {
  if (th == 0 || name == 0 || idx == 0) return SM_ERR_NULL_ARGUMENT;
  if (th->n_vars >= SX_MAX_VARS) return SM_ERR_DOMAIN_TOO_LARGE;
  if (!(lo <= hi)) return SM_ERR_EMPTY_DOMAIN;
  strncpy(th->name[th->n_vars], name, SX_NAME - 1u);
  th->type[th->n_vars] = type;
  if (type == SX_BOOL) th->domain[th->n_vars] = iv_int(0.0, 1.0);
  else if (type == SX_INT) th->domain[th->n_vars] = iv_int(lo, hi);
  else th->domain[th->n_vars] = iv_make(lo, hi);
  *idx = th->n_vars++;
  return SM_OK;
}

/* ---- building nodes -------------------------------------------------- */

static unsigned mk(sx_theory_t *th, sx_op_t op, unsigned a, unsigned b) {
  sx_node_t *n;
  if (th->n_nodes >= SX_MAX_NODES) {
    snprintf(th->error, sizeof th->error, "expression too large (over %u parts)", SX_MAX_NODES);
    return SX_NONE;
  }
  /* a child that failed to build fails its parent (unary ops pass b = 0) */
  if ((a == SX_NONE || b == SX_NONE) && op != SX_CONST && op != SX_VAR) return SX_NONE;
  n = &th->node[th->n_nodes];
  memset(n, 0, sizeof *n);
  n->op = op;
  n->a = a;
  n->b = b;
  return th->n_nodes++;
}

static unsigned mk_const(sx_theory_t *th, iv_t c) {
  unsigned k = mk(th, SX_CONST, 0u, 0u);
  if (k != SX_NONE) th->node[k].c = c;
  return k;
}

static unsigned mk_var(sx_theory_t *th, unsigned v) {
  unsigned k = mk(th, SX_VAR, 0u, 0u);
  if (k != SX_NONE) th->node[k].var = v;
  return k;
}

static int is_bool_op(sx_op_t op) {
  return op == SX_LT || op == SX_LE || op == SX_EQ || op == SX_NE || op == SX_AND ||
         op == SX_OR || op == SX_NOT;
}

int sx_is_bool(const sx_theory_t *th, unsigned root) {
  const sx_node_t *n;
  if (th == 0 || root >= th->n_nodes) return 0;
  n = &th->node[root];
  if (is_bool_op(n->op)) return 1;
  return n->op == SX_VAR && th->type[n->var] == SX_BOOL;
}

/* ---- the parser ------------------------------------------------------ */

typedef struct {
  sx_theory_t *th;
  const char *s;
  unsigned pos;
  int failed;
} parser_t;

static void skip(parser_t *p) {
  while (p->s[p->pos] == ' ' || p->s[p->pos] == '\t' || p->s[p->pos] == '\n') p->pos++;
}

static int is_id(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
static int is_dig(char c) { return c >= '0' && c <= '9'; }

static void fail(parser_t *p, const char *what) {
  if (!p->failed) {
    snprintf(p->th->error, sizeof p->th->error, "%s at position %u", what, p->pos);
  }
  p->failed = 1;
}

/* does the input continue with this operator or word? consumes it if so */
static int accept(parser_t *p, const char *tok) {
  size_t n = strlen(tok);
  skip(p);
  if (strncmp(p->s + p->pos, tok, n) != 0) return 0;
  if (is_id(tok[0]) && (is_id(p->s[p->pos + n]) || is_dig(p->s[p->pos + n]))) return 0;
  /* "<" must not swallow "<=", "=" must not be half of "==" or "=>" */
  if ((strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0) && p->s[p->pos + 1] == '=') return 0;
  p->pos += (unsigned)n;
  return 1;
}

static unsigned p_expr(parser_t *p, unsigned depth);

static unsigned p_primary(parser_t *p, unsigned depth) {
  sx_theory_t *th = p->th;
  skip(p);
  if (depth >= SX_MAX_DEPTH) { fail(p, "expression nested too deeply"); return SX_NONE; }
  if (accept(p, "(")) {
    unsigned e = p_expr(p, depth + 1u);
    if (!accept(p, ")")) fail(p, "expected )");
    return e;
  }
  if (is_dig(p->s[p->pos]) || p->s[p->pos] == '.') {
    char *end;
    double v = strtod(p->s + p->pos, &end);
    unsigned len = (unsigned)(end - (p->s + p->pos)), i;
    int whole_text = 1;
    for (i = 0u; i < len; i++) {
      char c = p->s[p->pos + i];
      if (c == '.' || c == 'e' || c == 'E') whole_text = 0;
    }
    p->pos += len;
    /* a whole number is exact; a decimal may not be representable, so it
       becomes the smallest interval of doubles around it */
    return mk_const(th, whole_text ? iv_point(v) : iv_make(nextafter(v, -INFINITY), nextafter(v, INFINITY)));
  }
  if (is_id(p->s[p->pos])) {
    char word[SX_NAME + 8];
    unsigned n = 0u, i;
    static const char *const f1[6] = {"sqrt", "exp", "log", "sin", "cos", "abs"};
    static const sx_op_t o1[6] = {SX_SQRT, SX_EXP, SX_LOG, SX_SIN, SX_COS, SX_ABS};
    while ((is_id(p->s[p->pos]) || is_dig(p->s[p->pos])) && n + 1u < sizeof word) word[n++] = p->s[p->pos++];
    word[n] = '\0';
    if (strcmp(word, "pi") == 0) return mk_const(th, iv_make(3.141592653589793, 3.1415926535897936));
    if (strcmp(word, "true") == 0) return mk_const(th, iv_int(1.0, 1.0));
    if (strcmp(word, "false") == 0) return mk_const(th, iv_int(0.0, 0.0));
    for (i = 0u; i < 6u; i++) {
      if (strcmp(word, f1[i]) == 0) {
        unsigned a;
        if (!accept(p, "(")) { fail(p, "expected ( after a function name"); return SX_NONE; }
        a = p_expr(p, depth + 1u);
        if (!accept(p, ")")) fail(p, "expected )");
        return mk(th, o1[i], a, 0u);
      }
    }
    if (strcmp(word, "min") == 0 || strcmp(word, "max") == 0) {
      unsigned a, b;
      sx_op_t op = word[1] == 'i' ? SX_MIN : SX_MAX;
      if (!accept(p, "(")) { fail(p, "expected ("); return SX_NONE; }
      a = p_expr(p, depth + 1u);
      if (!accept(p, ",")) fail(p, "expected ,");
      b = p_expr(p, depth + 1u);
      if (!accept(p, ")")) fail(p, "expected )");
      return mk(th, op, a, b);
    }
    for (i = 0u; i < th->n_vars; i++) {
      if (strcmp(word, th->name[i]) == 0) return mk_var(th, i);
    }
    /* a quantity it has defined for itself is as good as a variable: the
       concept is IN the description, not in a table beside it */
    for (i = 0u; i < th->n_defs; i++) {
      if (strcmp(word, th->def_name[i]) == 0) return th->def_root[i];
    }
    fail(p, "unknown name");
    return SX_NONE;
  }
  fail(p, "expected a number, a name or (");
  return SX_NONE;
}

static unsigned p_power(parser_t *p, unsigned depth) {
  unsigned a = p_primary(p, depth);
  if (accept(p, "^")) {
    char *end;
    long n;
    unsigned k;
    skip(p);
    n = strtol(p->s + p->pos, &end, 10);
    if (end == p->s + p->pos || n < 0 || n > 64) { fail(p, "a power must be a whole number 0..64"); return SX_NONE; }
    p->pos += (unsigned)(end - (p->s + p->pos));
    k = mk(p->th, SX_POWI, a, 0u);
    if (k != SX_NONE) p->th->node[k].n = (int)n;
    return k;
  }
  return a;
}

static unsigned p_unary(parser_t *p, unsigned depth) {
  if (depth >= SX_MAX_DEPTH) { fail(p, "expression nested too deeply"); return SX_NONE; }
  if (accept(p, "-")) return mk(p->th, SX_NEG, p_unary(p, depth + 1u), 0u);
  if (accept(p, "not")) return mk(p->th, SX_NOT, p_unary(p, depth + 1u), 0u);   /* a == not b */
  return p_power(p, depth);
}

static unsigned p_prod(parser_t *p, unsigned depth) {
  unsigned a = p_unary(p, depth);
  for (;;) {
    if (p->failed) return SX_NONE;
    if (accept(p, "*")) a = mk(p->th, SX_MUL, a, p_unary(p, depth));
    else if (accept(p, "/")) a = mk(p->th, SX_DIV, a, p_unary(p, depth));
    else if (accept(p, "%")) a = mk(p->th, SX_MOD, a, p_unary(p, depth));
    else return a;
  }
}

static unsigned p_sum(parser_t *p, unsigned depth) {
  unsigned a = p_prod(p, depth);
  for (;;) {
    if (p->failed) return SX_NONE;
    if (accept(p, "+")) a = mk(p->th, SX_ADD, a, p_prod(p, depth));
    else if (accept(p, "-")) a = mk(p->th, SX_SUB, a, p_prod(p, depth));
    else return a;
  }
}

static unsigned p_cmp(parser_t *p, unsigned depth) {
  unsigned a = p_sum(p, depth);
  if (accept(p, "<=")) return mk(p->th, SX_LE, a, p_sum(p, depth));
  if (accept(p, ">=")) { unsigned b = p_sum(p, depth); return mk(p->th, SX_LE, b, a); }
  if (accept(p, "==")) return mk(p->th, SX_EQ, a, p_sum(p, depth));
  if (accept(p, "!=")) return mk(p->th, SX_NE, a, p_sum(p, depth));
  if (accept(p, "<")) return mk(p->th, SX_LT, a, p_sum(p, depth));
  if (accept(p, ">")) { unsigned b = p_sum(p, depth); return mk(p->th, SX_LT, b, a); }
  return a;
}

static unsigned p_not(parser_t *p, unsigned depth) {
  if (depth >= SX_MAX_DEPTH) { fail(p, "expression nested too deeply"); return SX_NONE; }
  if (accept(p, "not")) return mk(p->th, SX_NOT, p_not(p, depth + 1u), 0u);
  return p_cmp(p, depth);
}

static unsigned p_and(parser_t *p, unsigned depth) {
  unsigned a = p_not(p, depth);
  while (!p->failed && accept(p, "and")) a = mk(p->th, SX_AND, a, p_not(p, depth));
  return a;
}

static unsigned p_or(parser_t *p, unsigned depth) {
  unsigned a = p_and(p, depth);
  while (!p->failed && accept(p, "or")) a = mk(p->th, SX_OR, a, p_and(p, depth));
  return a;
}

static unsigned p_expr(parser_t *p, unsigned depth) {
  unsigned a;
  if (depth >= SX_MAX_DEPTH) { fail(p, "expression nested too deeply"); return SX_NONE; }
  a = p_or(p, depth);
  if (accept(p, "=>")) {   /* a => b  is  (not a) or b */
    unsigned b = p_expr(p, depth + 1u);
    return mk(p->th, SX_OR, mk(p->th, SX_NOT, a, 0u), b);
  }
  return a;
}

sm_status_t sx_parse(sx_theory_t *th, const char *text, unsigned *root) {
  parser_t p;
  unsigned r;
  if (th == 0 || text == 0 || root == 0) return SM_ERR_NULL_ARGUMENT;
  p.th = th;
  p.s = text;
  p.pos = 0u;
  p.failed = 0;
  th->error[0] = '\0';
  r = p_expr(&p, 0u);
  skip(&p);
  if (!p.failed && p.s[p.pos] != '\0') fail(&p, "unexpected text");
  if (p.failed || r == SX_NONE) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  *root = r;
  return SM_OK;
}

sm_status_t sx_require(sx_theory_t *th, const char *text) {
  unsigned r;
  sm_status_t st;
  if (th == 0 || text == 0) return SM_ERR_NULL_ARGUMENT;
  if (th->n_cons >= SX_MAX_CONS) return SM_ERR_DOMAIN_TOO_LARGE;
  st = sx_parse(th, text, &r);
  if (st != SM_OK) return st;
  if (!sx_is_bool(th, r)) {
    snprintf(th->error, sizeof th->error, "a constraint must be true or false, not a number");
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  th->con[th->n_cons] = r;
  th->source[th->n_cons] = 0u;        /* what it saw itself */
  th->conjectured[th->n_cons] = 0;
  th->active[th->n_cons] = 1;
  th->n_cons++;
  return SM_OK;
}

sm_status_t sx_define(sx_theory_t *th, const char *name, const char *text) {
  unsigned r;
  sm_status_t st;
  if (th == 0 || name == 0 || text == 0) return SM_ERR_NULL_ARGUMENT;
  if (th->n_defs >= SX_MAX_DEFS) return SM_ERR_DOMAIN_TOO_LARGE;
  st = sx_parse(th, text, &r);
  if (st != SM_OK) return st;
  strncpy(th->def_name[th->n_defs], name, SX_NAME - 1u);
  th->def_root[th->n_defs] = r;
  th->n_defs++;
  return SM_OK;
}

sm_status_t sx_source(sx_theory_t *th, const char *name, unsigned *id) {
  if (th == 0 || name == 0 || id == 0) return SM_ERR_NULL_ARGUMENT;
  if (th->n_sources == 0u) {
    strncpy(th->source_name[0], "what it saw itself", SX_NAME - 1u);
    th->n_sources = 1u;
  }
  if (th->n_sources >= SX_MAX_SOURCES) return SM_ERR_DOMAIN_TOO_LARGE;
  strncpy(th->source_name[th->n_sources], name, SX_NAME - 1u);
  *id = th->n_sources++;
  return SM_OK;
}

sm_status_t sx_claim(sx_theory_t *th, const char *text, unsigned source, int conjectured) {
  sm_status_t st;
  if (th == 0 || text == 0) return SM_ERR_NULL_ARGUMENT;
  if (source >= SX_MAX_SOURCES) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  st = sx_require(th, text);
  if (st != SM_OK) return st;
  th->source[th->n_cons - 1u] = source;
  th->conjectured[th->n_cons - 1u] = conjectured ? 1 : 0;
  th->active[th->n_cons - 1u] = th->distrusted[source] ? 0 : 1;
  return SM_OK;
}

void sx_distrust(sx_theory_t *th, unsigned source) {
  unsigned i;
  if (th == 0 || source >= SX_MAX_SOURCES) return;
  th->distrusted[source] = 1;
  /* everything that source said stops counting, everywhere at once */
  for (i = 0u; i < th->n_cons; i++) if (th->source[i] == source) th->active[i] = 0;
}

sm_status_t sx_ask_grounded(sx_theory_t *th, unsigned question, const sx_opts_t *opts,
                            sx_answer_t *out, uint32_t *rests_on, int *proved_outright) {
  sx_answer_t bare;
  int was[SX_MAX_CONS];
  unsigned i;
  sm_status_t st;
  if (th == 0 || opts == 0 || out == 0 || rests_on == 0 || proved_outright == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  *rests_on = 0u;
  *proved_outright = 0;
  st = sx_ask(th, question, opts, out);          /* with everything it has */
  if (st != SM_OK) return st;
  /* and again standing only on what is proved: no guesses, nothing from a
     source it cannot check. If the verdict survives that, it is proved. */
  for (i = 0u; i < th->n_cons; i++) {
    was[i] = th->active[i];
    if (th->conjectured[i]) th->active[i] = 0;
  }
  st = sx_ask(th, question, opts, &bare);
  for (i = 0u; i < th->n_cons; i++) th->active[i] = was[i];
  if (st != SM_OK) return st;
  if (bare.verdict == out->verdict &&
      (out->verdict == SX_TRUE_ALL || out->verdict == SX_FALSE_ALL || out->verdict == SX_VALUE ||
       out->verdict == SX_NO_WORLDS)) {
    *proved_outright = 1;
    return SM_OK;
  }
  for (i = 0u; i < th->n_cons; i++) {
    if (th->active[i] && th->conjectured[i]) *rests_on |= (uint32_t)1 << i;
  }
  return SM_OK;
}

/* ---- evaluation -------------------------------------------------------- */

static iv_t eval_node(const sx_theory_t *th, unsigned k, const sx_box_t *box) {
  const sx_node_t *n = &th->node[k];
  iv_t a = VAL[n->a], b = VAL[n->b];
  switch (n->op) {
    case SX_CONST: return n->c;
    case SX_VAR: return box->v[n->var];
    case SX_ADD: return iv_add(a, b);
    case SX_SUB: return iv_sub(a, b);
    case SX_MUL: return iv_mul(a, b);
    case SX_DIV: return iv_div(a, b);
    case SX_NEG: return iv_neg(a);
    case SX_POWI: return iv_powi(a, n->n);
    case SX_SQRT: return iv_sqrt(a);
    case SX_EXP: return iv_exp(a);
    case SX_LOG: return iv_log(a);
    case SX_SIN: return iv_sin(a);
    case SX_COS: return iv_cos(a);
    case SX_ABS: return iv_abs(a);
    case SX_MIN: return iv_min(a, b);
    case SX_MAX: return iv_max(a, b);
    case SX_MOD: return iv_mod(a, b);
    case SX_LT: return iv_lt(a, b);
    case SX_LE: return iv_le(a, b);
    case SX_EQ: return iv_eq(a, b);
    case SX_NE: return iv_not(iv_eq(a, b));
    case SX_AND: return iv_and(a, b);
    case SX_OR: return iv_or(a, b);
    default: return iv_not(a);
  }
}

static void forward(const sx_theory_t *th, unsigned upto, const sx_box_t *box) {
  unsigned k;
  for (k = 0u; k < SX_MAX_NODES; k++) {
    if (k > upto || k >= th->n_nodes) break;
    VAL[k] = eval_node(th, k, box);
  }
}

iv_t sx_eval(sx_theory_t *th, unsigned root, const sx_box_t *box) {
  if (th == 0 || box == 0 || root >= th->n_nodes) return iv_empty();
  forward(th, root, box);
  return VAL[root];
}

sx_box_t sx_start(const sx_theory_t *th) {
  sx_box_t b;
  unsigned i;
  for (i = 0u; i < SX_MAX_VARS; i++) b.v[i] = (th != 0 && i < th->n_vars) ? th->domain[i] : iv_point(0.0);
  return b;
}

/* ---- propagation: cutting regions away --------------------------------- */

static iv_t shrink(iv_t have, iv_t cut) { return iv_meet(have, cut); }

/* how many children an operation has */
static unsigned arity(sx_op_t op) {
  switch (op) {
    case SX_CONST: case SX_VAR: return 0u;
    case SX_NEG: case SX_POWI: case SX_SQRT: case SX_EXP: case SX_LOG:
    case SX_SIN: case SX_COS: case SX_ABS: case SX_NOT: return 1u;
    default: return 2u;
  }
}

static iv_t root_n(iv_t t, int n) {   /* the n-th root of t, for odd n: increasing */
  double lo = t.lo < 0 ? -pow(-t.lo, 1.0 / n) : pow(t.lo, 1.0 / n);
  double hi = t.hi < 0 ? -pow(-t.hi, 1.0 / n) : pow(t.hi, 1.0 / n);
  return iv_make(nextafter(nextafter(lo, -INFINITY), -INFINITY), nextafter(nextafter(hi, INFINITY), INFINITY));
}

/* one constraint, forward then backward; 0 if it cannot hold in the box */
static int revise(const sx_theory_t *th, unsigned root, sx_box_t *box) {
  unsigned k;
  forward(th, root, box);
  for (k = 0u; k <= root; k++) TGT[k] = VAL[k];
  TGT[root] = iv_meet(TGT[root], iv_int(1.0, 1.0));
  for (k = root + 1u; k-- > 0u;) {
    const sx_node_t *n = &th->node[k];
    iv_t T = TGT[k], A = VAL[n->a], B = VAL[n->b];
    if (iv_is_empty(T)) return 0;
    switch (n->op) {
      case SX_VAR:
        box->v[n->var] = iv_meet(box->v[n->var], T);
        if (th->type[n->var] != SX_REAL) box->v[n->var] = iv_to_int(box->v[n->var]);
        if (iv_is_empty(box->v[n->var])) return 0;
        break;
      case SX_CONST:
        if (iv_is_empty(iv_meet(T, n->c))) return 0;
        break;
      case SX_ADD:
        TGT[n->a] = shrink(TGT[n->a], iv_sub(T, B));
        TGT[n->b] = shrink(TGT[n->b], iv_sub(T, A));
        break;
      case SX_SUB:
        TGT[n->a] = shrink(TGT[n->a], iv_add(T, B));
        TGT[n->b] = shrink(TGT[n->b], iv_sub(A, T));
        break;
      case SX_NEG:
        TGT[n->a] = shrink(TGT[n->a], iv_neg(T));
        break;
      case SX_MUL:
        if (!(B.lo <= 0.0 && B.hi >= 0.0)) TGT[n->a] = shrink(TGT[n->a], iv_div(T, B));
        if (!(A.lo <= 0.0 && A.hi >= 0.0)) TGT[n->b] = shrink(TGT[n->b], iv_div(T, A));
        break;
      case SX_DIV:
        TGT[n->a] = shrink(TGT[n->a], iv_mul(T, B));
        if (!(T.lo <= 0.0 && T.hi >= 0.0)) TGT[n->b] = shrink(TGT[n->b], iv_div(A, T));
        break;
      case SX_POWI:
        if (n->n % 2 == 1) {
          TGT[n->a] = shrink(TGT[n->a], root_n(T, n->n));
        } else if (n->n > 0) {
          iv_t pos = iv_meet(T, iv_make(0.0, INFINITY)), r;
          if (iv_is_empty(pos)) return 0;
          r = iv_make(pow(pos.lo, 1.0 / n->n), pow(pos.hi, 1.0 / n->n));
          r = iv_make(nextafter(nextafter(r.lo, -INFINITY), -INFINITY), nextafter(nextafter(r.hi, INFINITY), INFINITY));
          if (A.lo >= 0.0) TGT[n->a] = shrink(TGT[n->a], iv_make(r.lo < 0 ? 0 : r.lo, r.hi));
          else if (A.hi <= 0.0) TGT[n->a] = shrink(TGT[n->a], iv_make(-r.hi, r.lo < 0 ? 0 : -r.lo));
          else TGT[n->a] = shrink(TGT[n->a], iv_make(-r.hi, r.hi));
        }
        break;
      case SX_SQRT: {
        iv_t pos = iv_meet(T, iv_make(0.0, INFINITY));
        if (iv_is_empty(pos)) return 0;
        TGT[n->a] = shrink(TGT[n->a], iv_mul(pos, pos));
        break;
      }
      case SX_EXP:
        TGT[n->a] = shrink(TGT[n->a], iv_log(T));
        break;
      case SX_LOG:
        TGT[n->a] = shrink(TGT[n->a], iv_exp(T));
        break;
      case SX_LT:
      case SX_LE:
        if (iv_true(T)) {
          TGT[n->a] = shrink(TGT[n->a], iv_make(-INFINITY, B.hi));
          TGT[n->b] = shrink(TGT[n->b], iv_make(A.lo, INFINITY));
        } else if (iv_false(T)) {
          TGT[n->a] = shrink(TGT[n->a], iv_make(B.lo, INFINITY));
          TGT[n->b] = shrink(TGT[n->b], iv_make(-INFINITY, A.hi));
        }
        break;
      case SX_EQ:
      case SX_NE:
        if ((n->op == SX_EQ && iv_true(T)) || (n->op == SX_NE && iv_false(T))) {
          iv_t m = iv_meet(A, B);
          if (iv_is_empty(m)) return 0;
          TGT[n->a] = shrink(TGT[n->a], m);
          TGT[n->b] = shrink(TGT[n->b], m);
        }
        break;
      case SX_AND:
        if (iv_true(T)) {
          TGT[n->a] = shrink(TGT[n->a], iv_int(1.0, 1.0));
          TGT[n->b] = shrink(TGT[n->b], iv_int(1.0, 1.0));
        } else if (iv_false(T)) {
          if (iv_true(A)) TGT[n->b] = shrink(TGT[n->b], iv_int(0.0, 0.0));
          if (iv_true(B)) TGT[n->a] = shrink(TGT[n->a], iv_int(0.0, 0.0));
        }
        break;
      case SX_OR:
        if (iv_false(T)) {
          TGT[n->a] = shrink(TGT[n->a], iv_int(0.0, 0.0));
          TGT[n->b] = shrink(TGT[n->b], iv_int(0.0, 0.0));
        } else if (iv_true(T)) {
          if (iv_false(A)) TGT[n->b] = shrink(TGT[n->b], iv_int(1.0, 1.0));
          if (iv_false(B)) TGT[n->a] = shrink(TGT[n->a], iv_int(1.0, 1.0));
        }
        break;
      case SX_NOT:
        TGT[n->a] = shrink(TGT[n->a], iv_not(T));
        break;
      default:
        break;   /* sin, cos, abs, min, max, mod: no cut, which is always safe */
    }
    /* a child cut to nothing: no world in this box satisfies it */
    if (arity(n->op) >= 1u && iv_is_empty(TGT[n->a])) return 0;
    if (arity(n->op) == 2u && iv_is_empty(TGT[n->b])) return 0;
  }
  return 1;
}

int sx_contract(sx_theory_t *th, sx_box_t *box) {
  unsigned round, c, i;
  if (th == 0 || box == 0) return 0;
  for (round = 0u; round < 16u; round++) {
    double before = 0.0, after = 0.0;
    for (i = 0u; i < th->n_vars; i++) before += iv_width(box->v[i]) < 1e300 ? iv_width(box->v[i]) : 1e300;
    for (c = 0u; c < th->n_cons; c++) {
      if (!th->active[c]) continue;
      if (!revise(th, th->con[c], box)) return 0;
    }
    for (i = 0u; i < th->n_vars; i++) after += iv_width(box->v[i]) < 1e300 ? iv_width(box->v[i]) : 1e300;
    if (!(after < before * (1.0 - 1e-9))) break;   /* standstill */
  }
  return 1;
}

/* ---- the search --------------------------------------------------------- */

static iv_t all_constraints(sx_theory_t *th, const sx_box_t *box) {
  iv_t c = iv_int(1.0, 1.0);
  unsigned i;
  for (i = 0u; i < th->n_cons; i++) {
    if (th->active[i]) c = iv_and(c, sx_eval(th, th->con[i], box));
  }
  return c;
}

static sx_box_t middle(const sx_theory_t *th, const sx_box_t *b) {
  sx_box_t m = *b;
  unsigned i;
  for (i = 0u; i < th->n_vars; i++) {
    double x = iv_mid(b->v[i]);
    if (th->type[i] != SX_REAL) x = floor(x);
    if (x < b->v[i].lo) x = b->v[i].lo;
    m.v[i] = th->type[i] != SX_REAL ? iv_int(x, x) : iv_point(x);
  }
  return m;
}

static sm_status_t search(sx_theory_t *th, unsigned q, const sx_opts_t *o, int propagate,
                          sx_answer_t *out) {
  double eps[SX_MAX_VARS];
  unsigned i, top = 0u, processed = 0u;
  unsigned bnd_true = 0u, bnd_false = 0u, bnd_open = 0u;
  int all_whole = 1;
  double worlds = 0.0, worlds_true = 0.0;

  /* sx_ask and sx_ask_plain have checked the pointers */
  if (q != SX_NONE && q >= th->n_nodes) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  memset(out, 0, sizeof *out);
  out->is_bool = q == SX_NONE || sx_is_bool(th, q);
  out->range = iv_empty();
  for (i = 0u; i < th->n_vars; i++) {
    double w = iv_width(th->domain[i]);
    eps[i] = (w > 0.0 && w < 1e300) ? w * o->rel_eps : 1e-6;
    if (th->type[i] == SX_REAL) all_whole = 0;
  }

  STACK[top++] = sx_start(th);
  while (top > 0u) {
    sx_box_t box = STACK[--top];
    iv_t c, qv = iv_int(1.0, 1.0);
    int decided, minimal = 1, minimal_override = 0;
    unsigned split = SX_MAX_VARS;
    double best = 0.0;

    if (++processed > o->max_regions) {
      /* Out of budget. The region is not examined further, but it may
         still hold worlds, so it still counts: in the range, and as
         undecided. Dropping it would break the guarantee. */
      out->capped = 1;
      out->boundary++;
      bnd_open++;
      if (q != SX_NONE && !out->is_bool) {
        iv_t qc = sx_eval(th, q, &box);
        if (!iv_is_empty(qc)) out->range = iv_hull(out->range, qc);
      }
      continue;
    }
    if (propagate && !sx_contract(th, &box)) { out->eliminated++; continue; }
    c = all_constraints(th, &box);
    if (iv_is_empty(c) || iv_false(c)) { out->eliminated++; continue; }
    if (q != SX_NONE) qv = sx_eval(th, q, &box);

    /* the constraints certainly hold here: worlds exist, and the middle
       of this region is one of them */
    if (iv_true(c)) {
      sx_box_t m = middle(th, &box);
      if (!out->has_world) { out->has_world = 1; out->witness_world = m; }
      if (q != SX_NONE && !out->is_bool) {
        iv_t atm = sx_eval(th, q, &m);
        if (!iv_is_empty(atm)) {
          if (!out->has_seen) { out->has_seen = 1; out->seen_lo = atm.hi; out->seen_hi = atm.lo; }
          if (atm.hi < out->seen_lo) out->seen_lo = atm.hi;
          if (atm.lo > out->seen_hi) out->seen_hi = atm.lo;
        }
      }
    }

    if (out->is_bool) {
      decided = (q == SX_NONE || iv_true(qv) || iv_false(qv)) && iv_true(c);
    } else {
      /* a number-valued answer: this region is settled when its values are
         pinned, or when they all lie between values already seen at real
         worlds, so it cannot widen the range either way */
      int inside = out->has_seen && !iv_is_empty(qv) && qv.lo >= out->seen_lo && qv.hi <= out->seen_hi;
      int pinned = !iv_is_empty(qv) && iv_width(qv) <= o->value_tol;
      decided = inside || (pinned && iv_true(c));
      if (inside && !iv_true(c)) decided = 0, minimal_override = 1;
    }

    for (i = 0u; i < th->n_vars; i++) {
      double w = iv_width(box.v[i]), score;
      if (th->type[i] == SX_REAL) { if (w <= eps[i]) continue; score = w / eps[i]; }
      else { if (w < 1.0) continue; score = 1e12 + w; }   /* whole numbers split to points */
      minimal = 0;
      if (score > best) { best = score; split = i; }
    }

    if (minimal_override) minimal = 1;   /* nothing to gain for the range by splitting */
    if (decided || minimal) {
      sx_box_t m = middle(th, &box);
      if (!out->is_bool && !iv_is_empty(qv)) out->range = iv_hull(out->range, qv);
      if (decided) {
        out->certain++;
        if (q != SX_NONE && out->is_bool) {
          if (iv_true(qv) && !out->has_true) { out->has_true = 1; out->witness_true = m; }
          if (iv_false(qv) && !out->has_false) { out->has_false = 1; out->witness_false = m; }
        }
        if (all_whole) {
          double count = 1.0;
          for (i = 0u; i < th->n_vars; i++) count *= iv_width(box.v[i]) + 1.0;
          worlds += count;
          if (q != SX_NONE && out->is_bool && iv_true(qv)) worlds_true += count;
        }
      } else {
        out->boundary++;
        if (q != SX_NONE && out->is_bool && iv_true(qv)) bnd_true++;
        else if (q != SX_NONE && out->is_bool && iv_false(qv)) bnd_false++;
        else bnd_open++;
      }
      continue;
    }

    /* split the widest range, in half */
    if (top + 2u > SX_STACK) { out->capped = 1; out->boundary++; bnd_open++; continue; }
    {
      sx_box_t lo = box, hi = box;
      iv_t v = box.v[split];
      if (th->type[split] == SX_REAL) {
        double m = iv_mid(v);
        lo.v[split] = iv_make(v.lo, m);
        hi.v[split] = iv_make(m, v.hi);
      } else {
        double m = floor(iv_mid(v));
        if (m >= v.hi) m = v.hi - 1.0;
        lo.v[split] = iv_int(v.lo, m);
        hi.v[split] = iv_int(m + 1.0, v.hi);
      }
      STACK[top++] = hi;
      STACK[top++] = lo;
      out->splits++;
    }
  }

  /* the verdict */
  if (out->certain == 0u && out->boundary == 0u) {
    out->verdict = SX_NO_WORLDS;
  } else if (!out->is_bool) {
    out->verdict = (!iv_is_empty(out->range) && out->range.lo == out->range.hi) ? SX_VALUE : SX_RANGE;
    if (!out->has_world && out->boundary > 0u && out->verdict == SX_VALUE) out->verdict = SX_RANGE;
  } else if (q == SX_NONE) {
    out->verdict = out->has_world ? SX_TRUE_ALL : SX_UNRESOLVED;
  } else if (out->has_true && out->has_false) {
    out->verdict = SX_BOTH;
  } else if (!out->has_false && bnd_false == 0u && bnd_open == 0u) {
    out->verdict = SX_TRUE_ALL;
  } else if (!out->has_true && bnd_true == 0u && bnd_open == 0u) {
    out->verdict = SX_FALSE_ALL;
  } else {
    out->verdict = SX_UNRESOLVED;
  }
  if (all_whole && out->boundary == 0u && !out->capped) {
    out->counted = 1;
    out->worlds = worlds;
    out->worlds_true = worlds_true;
  }
  return SM_OK;
}

sm_status_t sx_ask(sx_theory_t *th, unsigned question, const sx_opts_t *opts, sx_answer_t *out) {
  if (th == 0 || opts == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  return search(th, question, opts, 1, out);
}

sm_status_t sx_ask_plain(sx_theory_t *th, unsigned question, const sx_opts_t *opts,
                         sx_answer_t *out) {
  if (th == 0 || opts == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  return search(th, question, opts, 0, out);
}

/* ---- derivatives -------------------------------------------------------- */

static int is_const(const sx_theory_t *th, unsigned k, double v) {
  const sx_node_t *n = &th->node[k];
  return n->op == SX_CONST && n->c.exact && n->c.lo == v && n->c.hi == v;
}

static unsigned d_add(sx_theory_t *th, unsigned a, unsigned b) {
  if (is_const(th, a, 0.0)) return b;
  if (is_const(th, b, 0.0)) return a;
  return mk(th, SX_ADD, a, b);
}
static unsigned d_sub(sx_theory_t *th, unsigned a, unsigned b) {
  if (is_const(th, b, 0.0)) return a;
  if (is_const(th, a, 0.0)) return mk(th, SX_NEG, b, 0u);
  return mk(th, SX_SUB, a, b);
}
static int exact_const(const sx_theory_t *th, unsigned k) {
  return th->node[k].op == SX_CONST && th->node[k].c.exact && th->node[k].c.lo == th->node[k].c.hi;
}

static unsigned d_mul(sx_theory_t *th, unsigned a, unsigned b) {
  if (a == SX_NONE || b == SX_NONE) return SX_NONE;
  if (is_const(th, a, 0.0) || is_const(th, b, 0.0)) return mk_const(th, iv_point(0.0));
  if (is_const(th, a, 1.0)) return b;
  if (is_const(th, b, 1.0)) return a;
  if (exact_const(th, b) && !exact_const(th, a)) { unsigned t = a; a = b; b = t; }   /* constant first */
  if (exact_const(th, a) && exact_const(th, b)) return mk_const(th, iv_mul(th->node[a].c, th->node[b].c));
  /* 6 * (2 * x) is 12 * x */
  if (exact_const(th, a) && th->node[b].op == SX_MUL && exact_const(th, th->node[b].a)) {
    return mk(th, SX_MUL, mk_const(th, iv_mul(th->node[a].c, th->node[th->node[b].a].c)), th->node[b].b);
  }
  return mk(th, SX_MUL, a, b);
}

sm_status_t sx_diff(sx_theory_t *th, unsigned root, unsigned var, unsigned *out) {
  static unsigned D[SX_MAX_NODES];
  static unsigned char used[SX_MAX_NODES];
  unsigned k, zero, one;
  if (th == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (root >= th->n_nodes || var >= th->n_vars) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  /* only the nodes this expression actually uses: other constraints may
     hold abs or mod without that blocking this derivative */
  memset(used, 0, sizeof used);
  used[root] = 1u;
  for (k = root + 1u; k-- > 0u;) {
    if (!used[k]) continue;
    if (arity(th->node[k].op) >= 1u) used[th->node[k].a] = 1u;
    if (arity(th->node[k].op) == 2u) used[th->node[k].b] = 1u;
  }
  zero = mk_const(th, iv_point(0.0));
  one = mk_const(th, iv_point(1.0));
  if (zero == SX_NONE || one == SX_NONE) return SM_ERR_DOMAIN_TOO_LARGE;
  for (k = 0u; k <= root; k++) {
    const sx_node_t n = th->node[k];
    if (!used[k]) continue;
    unsigned a = n.a, b = n.b, da = n.op >= SX_ADD ? D[a] : SX_NONE, db = D[b], r;
    switch (n.op) {
      case SX_CONST: r = zero; break;
      case SX_VAR: r = n.var == var ? one : zero; break;
      case SX_ADD: r = d_add(th, da, db); break;
      case SX_SUB: r = d_sub(th, da, db); break;
      case SX_NEG: r = is_const(th, da, 0.0) ? zero : mk(th, SX_NEG, da, 0u); break;
      case SX_MUL: r = d_add(th, d_mul(th, da, b), d_mul(th, a, db)); break;
      case SX_DIV:   /* (a'b - ab') / b^2 */
        r = is_const(th, da, 0.0) && is_const(th, db, 0.0) ? zero :
            mk(th, SX_DIV, d_sub(th, d_mul(th, da, b), d_mul(th, a, db)), mk(th, SX_POWI, b, 0u));
        if (r != SX_NONE && !is_const(th, r, 0.0)) th->node[th->node[r].b].n = 2;
        break;
      case SX_POWI: {
        unsigned p;
        if (n.n == 0) { r = zero; break; }
        if (n.n == 1) { r = da; break; }
        if (n.n == 2) {
          p = a;   /* x^1 is x */
        } else {
          p = mk(th, SX_POWI, a, 0u);
          if (p != SX_NONE) th->node[p].n = n.n - 1;
        }
        r = d_mul(th, d_mul(th, mk_const(th, iv_point((double)n.n)), p), da);
        break;
      }
      case SX_SQRT:
        r = is_const(th, da, 0.0) ? zero :
            mk(th, SX_DIV, da, d_mul(th, mk_const(th, iv_point(2.0)), mk(th, SX_SQRT, a, 0u)));
        break;
      case SX_EXP: r = d_mul(th, mk(th, SX_EXP, a, 0u), da); break;
      case SX_LOG: r = is_const(th, da, 0.0) ? zero : mk(th, SX_DIV, da, a); break;
      case SX_SIN: r = d_mul(th, mk(th, SX_COS, a, 0u), da); break;
      case SX_COS: r = d_mul(th, mk(th, SX_NEG, mk(th, SX_SIN, a, 0u), 0u), da); break;
      default:
        snprintf(th->error, sizeof th->error, "no derivative through abs, min, max, mod or a comparison");
        return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
    if (r == SX_NONE) return SM_ERR_DOMAIN_TOO_LARGE;
    D[k] = r;
  }
  *out = D[root];
  return SM_OK;
}

/* ---- printing ----------------------------------------------------------- */

static int prec(sx_op_t op) {
  switch (op) {
    case SX_OR: return 1;
    case SX_AND: return 2;
    case SX_NOT: return 3;
    case SX_LT: case SX_LE: case SX_EQ: case SX_NE: return 4;
    case SX_ADD: case SX_SUB: return 5;
    case SX_MUL: case SX_DIV: case SX_MOD: return 6;
    case SX_NEG: return 7;
    case SX_POWI: return 8;
    default: return 9;
  }
}

static void put(char *buf, unsigned cap, unsigned *used, const char *s) {
  size_t n = strlen(s);
  if (*used + n + 1u > cap) return;
  memcpy(buf + *used, s, n + 1u);
  *used += (unsigned)n;
}

static void print_at(const sx_theory_t *th, unsigned k, char *buf, unsigned cap, unsigned *used,
                     unsigned depth) {
  static const char *binop[] = {"+", "-", "*", "/"};
  const sx_node_t *n = &th->node[k];
  char tmp[48];
  if (depth >= SX_PRINT_DEPTH) { put(buf, cap, used, "..."); return; }
  switch (n->op) {
    case SX_CONST:
      if (n->c.lo == n->c.hi) snprintf(tmp, sizeof tmp, "%.10g", n->c.lo);
      else snprintf(tmp, sizeof tmp, "%.10g", iv_mid(n->c));
      put(buf, cap, used, tmp);
      return;
    case SX_VAR:
      put(buf, cap, used, th->name[n->var]);
      return;
    case SX_ADD: case SX_SUB: case SX_MUL: case SX_DIV: {
      int p = prec(n->op);
      int pa = prec(th->node[n->a].op) < p, pb = prec(th->node[n->b].op) <= p;
      if (pa) put(buf, cap, used, "(");
      print_at(th, n->a, buf, cap, used, depth + 1u);
      if (pa) put(buf, cap, used, ")");
      put(buf, cap, used, n->op == SX_MUL ? "*" : " ");
      if (n->op != SX_MUL) { put(buf, cap, used, binop[n->op - SX_ADD]); put(buf, cap, used, " "); }
      if (pb) put(buf, cap, used, "(");
      print_at(th, n->b, buf, cap, used, depth + 1u);
      if (pb) put(buf, cap, used, ")");
      return;
    }
    case SX_NEG:
      put(buf, cap, used, "-");
      if (prec(th->node[n->a].op) < 8) put(buf, cap, used, "(");
      print_at(th, n->a, buf, cap, used, depth + 1u);
      if (prec(th->node[n->a].op) < 8) put(buf, cap, used, ")");
      return;
    case SX_POWI:
      if (prec(th->node[n->a].op) < 9) put(buf, cap, used, "(");
      print_at(th, n->a, buf, cap, used, depth + 1u);
      if (prec(th->node[n->a].op) < 9) put(buf, cap, used, ")");
      snprintf(tmp, sizeof tmp, "^%d", n->n);
      put(buf, cap, used, tmp);
      return;
    default: {
      static const char *fname[] = {"sqrt", "exp", "log", "sin", "cos", "abs"};
      if (n->op >= SX_SQRT && n->op <= SX_ABS) {
        put(buf, cap, used, fname[n->op - SX_SQRT]);
        put(buf, cap, used, "(");
        print_at(th, n->a, buf, cap, used, depth + 1u);
        put(buf, cap, used, ")");
        return;
      }
      put(buf, cap, used, "(...)");
      return;
    }
  }
}

void sx_print(const sx_theory_t *th, unsigned root, char *buf, unsigned cap) {
  unsigned used = 0u;
  if (buf == 0 || cap == 0u) return;
  buf[0] = '\0';
  if (th == 0 || root >= th->n_nodes) return;
  print_at(th, root, buf, cap, &used, 0u);
}

/* ---- calculus ------------------------------------------------------------ */

static iv_t at(sx_theory_t *th, unsigned f, unsigned var, iv_t x) {
  sx_box_t b = sx_start(th);
  b.v[var] = x;
  return sx_eval(th, f, &b);
}

sm_status_t sx_roots(sx_theory_t *th, unsigned f, unsigned var, double lo, double hi, double eps,
                     sx_roots_t *out) {
  static iv_t stack[SX_STACK];
  unsigned top = 0u, df, guard = 0u;
  sm_status_t st;
  if (th == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (f >= th->n_nodes || var >= th->n_vars || !(lo < hi)) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  memset(out, 0, sizeof *out);
  out->complete = 1;
  st = sx_diff(th, f, var, &df);
  if (st != SM_OK) return st;
  stack[top++] = iv_make(lo, hi);
  while (top > 0u && guard++ < 1000000u) {
    iv_t x = stack[--top], F = at(th, f, var, x), D, flo, fhi;
    double m;
    if (iv_is_empty(F) || F.lo > 0.0 || F.hi < 0.0) continue;   /* no root here, proved */
    D = at(th, df, var, x);
    flo = at(th, f, var, iv_point(x.lo));
    fhi = at(th, f, var, iv_point(x.hi));
    if (!iv_is_empty(D) && (D.lo > 0.0 || D.hi < 0.0)) {   /* strictly monotone here */
      int sign_change = (flo.hi < 0.0 && fhi.lo > 0.0) || (flo.lo > 0.0 && fhi.hi < 0.0);
      int no_change = (flo.lo > 0.0 && fhi.lo > 0.0) || (flo.hi < 0.0 && fhi.hi < 0.0);
      if (no_change) continue;
      if (sign_change) {
        /* exactly one root, by the intermediate value theorem and strict
           monotonicity; narrow it while the sign change stays certain */
        unsigned r;
        for (r = 0u; r < 200u && iv_width(x) > eps; r++) {
          double mm = iv_mid(x);
          iv_t fm = at(th, f, var, iv_point(mm));
          if ((fm.hi < 0.0) == (flo.hi < 0.0) && (fm.hi < 0.0 || fm.lo > 0.0)) { x.lo = mm; flo = fm; }
          else if (fm.hi < 0.0 || fm.lo > 0.0) { x.hi = mm; fhi = fm; }
          else break;   /* the value at the middle is too close to zero to sign */
        }
        if (out->n < SX_MAX_ROOTS) out->root[out->n++] = x;
        continue;
      }
    }
    if (iv_width(x) <= eps) {
      out->complete = 0;
      if (out->n_unresolved < SX_MAX_ROOTS) out->unresolved[out->n_unresolved++] = x;
      continue;
    }
    if (top + 2u > SX_STACK) { out->complete = 0; continue; }
    m = iv_mid(x);
    stack[top++] = iv_make(m, x.hi);
    stack[top++] = iv_make(x.lo, m);
  }
  /* roots come out in order of the search, which is left to right */
  return SM_OK;
}

int sx_monotone(sx_theory_t *th, unsigned f, unsigned var, double lo, double hi) {
  unsigned df, i, pieces = 1024u;
  int sign = 0;
  if (th == 0 || f >= th->n_nodes || var >= th->n_vars || !(lo < hi)) return 0;
  if (sx_diff(th, f, var, &df) != SM_OK) return 0;
  for (i = 0u; i < pieces; i++) {
    double a = lo + (hi - lo) * i / pieces, b = i + 1u == pieces ? hi : lo + (hi - lo) * (i + 1u) / pieces;
    iv_t D = at(th, df, var, iv_make(a, b));
    int s = iv_is_empty(D) ? 0 : (D.lo > 0.0 ? 1 : (D.hi < 0.0 ? -1 : 0));
    if (s == 0 || (sign != 0 && s != sign)) return 0;
    sign = s;
  }
  return sign;
}

iv_t sx_integral(sx_theory_t *th, unsigned f, unsigned var, double lo, double hi, unsigned pieces) {
  iv_t sum = iv_point(0.0);
  unsigned i;
  double prev = lo;
  if (th == 0 || f >= th->n_nodes || var >= th->n_vars || pieces == 0u || !(lo < hi)) return iv_empty();
  for (i = 0u; i < pieces; i++) {
    double next = i + 1u == pieces ? hi : lo + (hi - lo) * (double)(i + 1u) / (double)pieces;
    iv_t piece = iv_make(prev, next);
    iv_t width = iv_sub(iv_point(next), iv_point(prev));
    sum = iv_add(sum, iv_mul(at(th, f, var, piece), width));
    prev = next;
  }
  return sum;
}

/* ---- systems of equations: the Krawczyk test ------------------------------ */

/* Y = inverse of the n x n matrix M (plain doubles), by Gaussian
   elimination with partial pivoting. 0 when it is (nearly) singular. Y
   need not be exact: the Krawczyk test is valid for any Y. */
static int invert(unsigned n, double M[SX_MAX_VARS][SX_MAX_VARS], double Y[SX_MAX_VARS][SX_MAX_VARS]) {
  double a[SX_MAX_VARS][2u * SX_MAX_VARS];
  unsigned i, j, k;
  for (i = 0u; i < n; i++) {
    for (j = 0u; j < n; j++) { a[i][j] = M[i][j]; a[i][n + j] = i == j ? 1.0 : 0.0; }
  }
  for (k = 0u; k < n; k++) {
    unsigned piv = k;
    double best = fabs(a[k][k]);
    for (i = k + 1u; i < n; i++) if (fabs(a[i][k]) > best) { best = fabs(a[i][k]); piv = i; }
    if (!(best > 1e-300)) return 0;
    if (piv != k) for (j = 0u; j < 2u * n; j++) { double t = a[k][j]; a[k][j] = a[piv][j]; a[piv][j] = t; }
    for (j = 0u; j < 2u * n; j++) if (j != k) a[k][j] /= a[k][k];
    a[k][k] = 1.0;
    for (i = 0u; i < n; i++) {
      double f;
      if (i == k) continue;
      f = a[i][k];
      for (j = 0u; j < 2u * n; j++) a[i][j] -= f * a[k][j];
    }
  }
  for (i = 0u; i < n; i++) for (j = 0u; j < n; j++) Y[i][j] = a[i][n + j];
  return 1;
}

static int overlaps(unsigned n, const sx_box_t *a, const sx_box_t *b) {
  unsigned i;
  for (i = 0u; i < n; i++) if (iv_is_empty(iv_meet(a->v[i], b->v[i]))) return 0;
  return 1;
}

sm_status_t sx_solve(sx_theory_t *th, const unsigned *f, unsigned n, double rel_eps,
                     sx_solutions_t *out) {
  static unsigned J[SX_MAX_VARS][SX_MAX_VARS];
  double eps[SX_MAX_VARS];
  unsigned top = 0u, i, j, k;
  sm_status_t st;

  if (th == 0 || f == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (n == 0u || n != th->n_vars) return SM_ERR_INDEX_OUT_OF_DOMAIN;
  memset(out, 0, sizeof *out);
  out->complete = 1;
  for (i = 0u; i < n; i++) {
    if (f[i] >= th->n_nodes || th->type[i] != SX_REAL) return SM_ERR_INDEX_OUT_OF_DOMAIN;
    for (j = 0u; j < n; j++) {
      st = sx_diff(th, f[i], j, &J[i][j]);
      if (st != SM_OK) return st;
    }
    eps[i] = iv_width(th->domain[i]) * rel_eps;
  }

  STACK[top++] = sx_start(th);
  while (top > 0u) {
    sx_box_t X = STACK[--top], Xi, m, K;
    iv_t fm[SX_MAX_VARS], JX[SX_MAX_VARS][SX_MAX_VARS];
    double Jm[SX_MAX_VARS][SX_MAX_VARS], Y[SX_MAX_VARS][SX_MAX_VARS];
    int excluded = 0, inside = 1, empty = 0, small = 1;
    unsigned split = 0u;
    double widest = 0.0;

    if (++out->regions > 200000u) { out->complete = 0; continue; }
    /* 1. some f_i cannot be zero here: no solution, proved */
    for (i = 0u; i < n && !excluded; i++) {
      iv_t F = sx_eval(th, f[i], &X);
      if (iv_is_empty(F) || F.lo > 0.0 || F.hi < 0.0) excluded = 1;
    }
    if (excluded) continue;

    /* 2. the Krawczyk operator, on X widened by a tenth each way
       (epsilon-inflation): a solution sitting exactly on X's edge can
       never be strictly inside X, but it is strictly inside the widened
       region, and whatever is proved of the wider region holds of X */
    Xi = X;
    for (i = 0u; i < n; i++) {
      double h = iv_width(X.v[i]) * 0.1 + 1e-15 * (1.0 + fabs(iv_mid(X.v[i])));
      Xi.v[i] = iv_make(X.v[i].lo - h, X.v[i].hi + h);
    }
    m = Xi;
    for (i = 0u; i < n; i++) m.v[i] = iv_point(iv_mid(Xi.v[i]));
    for (i = 0u; i < n; i++) {
      fm[i] = sx_eval(th, f[i], &m);
      for (j = 0u; j < n; j++) {
        JX[i][j] = sx_eval(th, J[i][j], &Xi);
        Jm[i][j] = iv_mid(sx_eval(th, J[i][j], &m));
      }
    }
    if (invert(n, Jm, Y)) {
      K = X;
      for (i = 0u; i < n; i++) {
        iv_t acc = m.v[i];
        for (k = 0u; k < n; k++) acc = iv_sub(acc, iv_mul(iv_point(Y[i][k]), fm[k]));
        for (j = 0u; j < n; j++) {
          iv_t c = iv_point(i == j ? 1.0 : 0.0);
          for (k = 0u; k < n; k++) c = iv_sub(c, iv_mul(iv_point(Y[i][k]), JX[k][j]));
          acc = iv_add(acc, iv_mul(c, iv_sub(Xi.v[j], m.v[j])));
        }
        K.v[i] = acc;
        if (iv_is_empty(acc) || !(acc.lo > Xi.v[i].lo && acc.hi < Xi.v[i].hi)) inside = 0;
        if (iv_is_empty(iv_meet(acc, X.v[i]))) empty = 1;
      }
      if (empty) continue;   /* no solution in X, proved */
      if (inside) {
        /* exactly one solution in X, proved; tighten it by iterating */
        sx_box_t S = K;
        unsigned r;
        for (r = 0u; r < 20u; r++) {
          sx_box_t Z = S, mm;
          int ok = 1;
          for (i = 0u; i < n; i++) mm.v[i] = iv_point(iv_mid(S.v[i]));
          for (i = 0u; i < n; i++) {
            iv_t acc = mm.v[i];
            for (k = 0u; k < n; k++) acc = iv_sub(acc, iv_mul(iv_point(Y[i][k]), sx_eval(th, f[k], &mm)));
            for (j = 0u; j < n; j++) {
              iv_t c = iv_point(i == j ? 1.0 : 0.0);
              for (k = 0u; k < n; k++) c = iv_sub(c, iv_mul(iv_point(Y[i][k]), sx_eval(th, J[k][j], &S)));
              acc = iv_add(acc, iv_mul(c, iv_sub(S.v[j], mm.v[j])));
            }
            Z.v[i] = iv_meet(acc, S.v[i]);
            if (iv_is_empty(Z.v[i])) ok = 0;
          }
          if (!ok) break;
          S = Z;
        }
        /* the widened region may reach outside the range: a solution proved
           outside it is not an answer; one straddling the edge is not
           claimed either way */
        {
          int within = 1, outside = 0;
          for (i = 0u; i < n; i++) {
            if (!iv_subset(S.v[i], th->domain[i])) within = 0;
            if (iv_is_empty(iv_meet(S.v[i], th->domain[i]))) outside = 1;
          }
          if (outside) continue;
          if (!within) {
            out->complete = 0;
            if (out->n_unresolved < SX_MAX_SOLUTIONS) out->unresolved[out->n_unresolved++] = S;
            continue;
          }
        }
        /* the same solution can be caught from two neighbouring regions */
        for (k = 0u; k < out->n; k++) if (overlaps(n, &out->solution[k], &S)) break;
        if (k == out->n && out->n < SX_MAX_SOLUTIONS) out->solution[out->n++] = S;
        continue;
      }
      for (i = 0u; i < n; i++) X.v[i] = iv_meet(X.v[i], K.v[i]);   /* keep only what K allows */
    }

    /* 3. split, or give up at this precision */
    for (i = 0u; i < n; i++) {
      double w = iv_width(X.v[i]) / (eps[i] > 0.0 ? eps[i] : 1e-12);
      if (iv_width(X.v[i]) > eps[i]) small = 0;
      if (w > widest) { widest = w; split = i; }
    }
    if (small || top + 2u > SX_STACK) {
      out->complete = 0;
      if (out->n_unresolved < SX_MAX_SOLUTIONS) out->unresolved[out->n_unresolved++] = X;
      continue;
    }
    {
      sx_box_t lo = X, hi = X;
      /* a little off centre, so exact values like 1 or 2 do not keep
         landing on the dividing line */
      double mid = X.v[split].lo + iv_width(X.v[split]) * 0.4990234375;
      lo.v[split] = iv_make(X.v[split].lo, mid);
      hi.v[split] = iv_make(mid, X.v[split].hi);
      STACK[top++] = hi;
      STACK[top++] = lo;
    }
  }
  return SM_OK;
}
