/*
 * smarsh_parser.c -- see smarsh_parser.h for why shunting-yard rather
 * than recursive descent, for the precedence table, and for exactly
 * which constructs are not built yet.
 */

#include "smarsh_parser.h"

#include <string.h>

typedef struct {
  const sl_token_t *tok;
  unsigned count;
  unsigned pos;
  sp_arena_t *arena;
  sp_status_t status;
} parser_t;

/* ------------------------------------------------------------------ */
/* token helpers                                                       */
/* ------------------------------------------------------------------ */

static const sl_token_t *peek(const parser_t *p) {
  if (p->pos >= p->count) {
    return &p->tok[p->count - 1u];   /* the EOF token; always present */
  }
  return &p->tok[p->pos];
}

static int at_op(const parser_t *p, const char *op) {
  const sl_token_t *t = peek(p);
  return t->kind == SL_TOK_OP && strcmp(t->text, op) == 0;
}

static int at_kw(const parser_t *p, const char *kw) {
  const sl_token_t *t = peek(p);
  return t->kind == SL_TOK_KW && strcmp(t->text, kw) == 0;
}

static int at_eof(const parser_t *p) {
  return peek(p)->kind == SL_TOK_EOF;
}

static void advance(parser_t *p) {
  if (p->pos < p->count) {
    p->pos++;
  }
}

static int accept_op(parser_t *p, const char *op) {
  if (at_op(p, op)) {
    advance(p);
    return 1;
  }
  return 0;
}

static int expect_op(parser_t *p, const char *op) {
  if (accept_op(p, op)) {
    return 1;
  }
  p->status = SP_ERR_UNEXPECTED_TOKEN;
  return 0;
}

/* ------------------------------------------------------------------ */
/* precedence, transcribed from the JS chain (see the header)          */
/* ------------------------------------------------------------------ */

static int precedence_of(const sl_token_t *t) {
  if (t->kind == SL_TOK_KW) {
    if (strcmp(t->text, "or") == 0) {
      return 1;
    }
    if (strcmp(t->text, "and") == 0) {
      return 2;
    }
    return 0;
  }
  if (t->kind != SL_TOK_OP) {
    return 0;
  }
  if (strcmp(t->text, "==") == 0 || strcmp(t->text, "!=") == 0) {
    return 3;
  }
  if (strcmp(t->text, "<") == 0 || strcmp(t->text, ">") == 0
      || strcmp(t->text, "<=") == 0 || strcmp(t->text, ">=") == 0) {
    return 4;
  }
  if (strcmp(t->text, "+") == 0 || strcmp(t->text, "-") == 0) {
    return 5;
  }
  if (strcmp(t->text, "*") == 0 || strcmp(t->text, "/") == 0
      || strcmp(t->text, "%") == 0 || strcmp(t->text, "@") == 0) {
    return 6;
  }
  if (strcmp(t->text, "**") == 0) {
    return 7;
  }
  return 0;
}

/* `**` is the only right-associative operator, so a run of equal
   precedence folds from the right for it and from the left for
   everything else. */
static int right_assoc(const char *op) {
  return strcmp(op, "**") == 0;
}

static int is_logical(const char *op) {
  return strcmp(op, "and") == 0 || strcmp(op, "or") == 0;
}

/* ------------------------------------------------------------------ */
/* expressions: shunting-yard, no recursion                            */
/* ------------------------------------------------------------------ */

typedef struct {
  char op[4];
  int prec;
  unsigned line;
} op_entry_t;

static sp_node_id_t parse_expression(parser_t *p, unsigned depth);

/* A primary, plus any postfix chain (call, index, member) that follows
   it. Postfix binds tighter than every binary operator, so it is
   consumed here rather than through the operator stack. */
static sp_node_id_t parse_operand(parser_t *p, unsigned depth) {
  const sl_token_t *t;
  sp_node_id_t node = SP_NO_NODE;
  sp_status_t st;
  unsigned mark;   /* pending-stack height where the current list began */

  if (depth >= SP_MAX_DEPTH) {
    p->status = SP_ERR_DEPTH_EXCEEDED;
    return SP_NO_NODE;
  }

  t = peek(p);

  /* Unary. `not` and `-` and `!`; the operand is parsed at the same
     depth+1, and because `**` outranks unary in the JS chain, a unary
     applied to a power expression wraps the whole power. */
  if (at_kw(p, "not") || at_op(p, "-") || at_op(p, "!")) {
    sp_node_id_t operand;
    char opbuf[4];
    unsigned line = t->line;

    strncpy(opbuf, t->text, sizeof(opbuf) - 1u);
    opbuf[sizeof(opbuf) - 1u] = '\0';
    advance(p);
    operand = parse_operand(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    /* A `**` immediately after binds tighter than this unary. */
    while (at_op(p, "**")) {
      sp_node_id_t rhs;
      sp_node_id_t pow;
      advance(p);
      rhs = parse_operand(p, depth + 1u);
      if (p->status != SP_OK) {
        return SP_NO_NODE;
      }
      st = sp_new_node(p->arena, SP_BINARY, line, &pow);
      if (st != SP_OK) {
        p->status = st;
        return SP_NO_NODE;
      }
      (void)sp_intern(p->arena, "**", &sp_node(p->arena, pow)->text);
      sp_node(p->arena, pow)->a = operand;
      sp_node(p->arena, pow)->b = rhs;
      operand = pow;
    }
    st = sp_new_node(p->arena, SP_UNARY, line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    (void)sp_intern(p->arena, opbuf, &sp_node(p->arena, node)->text);
    sp_node(p->arena, node)->a = operand;
    return node;
  }

  /* Primary. */
  if (t->kind == SL_TOK_NUM || t->kind == SL_TOK_DEC || t->kind == SL_TOK_STR) {
    sp_kind_t k = (t->kind == SL_TOK_NUM) ? SP_NUM
                : (t->kind == SL_TOK_DEC) ? SP_DEC_LIT : SP_STR;
    st = sp_new_node(p->arena, k, t->line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    (void)sp_intern(p->arena, t->text, &sp_node(p->arena, node)->text);
    advance(p);
  } else if (t->kind == SL_TOK_KW
             && (strcmp(t->text, "true") == 0 || strcmp(t->text, "false") == 0)) {
    st = sp_new_node(p->arena, SP_BOOL, t->line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->a = (strcmp(t->text, "true") == 0) ? 1u : 0u;
    advance(p);
  } else if (at_kw(p, "nil")) {
    st = sp_new_node(p->arena, SP_NIL, t->line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    advance(p);
  } else if (t->kind == SL_TOK_IDENT) {
    st = sp_new_node(p->arena, SP_IDENT, t->line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    (void)sp_intern(p->arena, t->text, &sp_node(p->arena, node)->text);
    advance(p);
  } else if (at_op(p, "(")) {
    advance(p);
    node = parse_expression(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    if (!expect_op(p, ")")) {
      return SP_NO_NODE;
    }
  } else if (at_op(p, "[")) {
    unsigned line = t->line;
    advance(p);
    st = sp_new_node(p->arena, SP_LIST_LIT, line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    mark = sp_pending_mark(p->arena);
    if (!at_op(p, "]")) {
      for (;;) {
        sp_node_id_t el = parse_expression(p, depth + 1u);
        if (p->status != SP_OK) {
          return SP_NO_NODE;
        }
        st = sp_pending_push(p->arena, el);
        if (st != SP_OK) {
          p->status = st;
          return SP_NO_NODE;
        }
        if (!accept_op(p, ",")) {
          break;
        }
      }
    }
    st = sp_commit_children(p->arena, node, mark);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    if (!expect_op(p, "]")) {
      return SP_NO_NODE;
    }
  } else {
    p->status = SP_ERR_UNEXPECTED_TOKEN;
    return SP_NO_NODE;
  }

  /* Postfix chain. Bounded by SP_MAX_DEPTH so a pathological `a.b.c...`
     cannot run unbounded. */
  {
    unsigned steps = 0u;
    while (steps < SP_MAX_DEPTH) {
      steps++;
      if (at_op(p, "(")) {
        sp_node_id_t call;
        unsigned line = peek(p)->line;
        advance(p);
        st = sp_new_node(p->arena, SP_CALL, line, &call);
        if (st != SP_OK) {
          p->status = st;
          return SP_NO_NODE;
        }
        sp_node(p->arena, call)->a = node;
        mark = sp_pending_mark(p->arena);
        if (!at_op(p, ")")) {
          for (;;) {
            sp_node_id_t arg = parse_expression(p, depth + 1u);
            if (p->status != SP_OK) {
              return SP_NO_NODE;
            }
            st = sp_pending_push(p->arena, arg);
            if (st != SP_OK) {
              p->status = st;
              return SP_NO_NODE;
            }
            if (!accept_op(p, ",")) {
              break;
            }
          }
        }
        st = sp_commit_children(p->arena, call, mark);
        if (st != SP_OK) {
          p->status = st;
          return SP_NO_NODE;
        }
        if (!expect_op(p, ")")) {
          return SP_NO_NODE;
        }
        node = call;
      } else if (at_op(p, "[")) {
        sp_node_id_t idx;
        unsigned line = peek(p)->line;
        advance(p);
        st = sp_new_node(p->arena, SP_INDEX, line, &idx);
        if (st != SP_OK) {
          p->status = st;
          return SP_NO_NODE;
        }
        sp_node(p->arena, idx)->a = node;
        mark = sp_pending_mark(p->arena);
        for (;;) {
          sp_node_id_t e = parse_expression(p, depth + 1u);
          if (p->status != SP_OK) {
            return SP_NO_NODE;
          }
          st = sp_pending_push(p->arena, e);
          if (st != SP_OK) {
            p->status = st;
            return SP_NO_NODE;
          }
          if (!accept_op(p, ",")) {
            break;
          }
        }
        st = sp_commit_children(p->arena, idx, mark);
        if (st != SP_OK) {
          p->status = st;
          return SP_NO_NODE;
        }
        if (!expect_op(p, "]")) {
          return SP_NO_NODE;
        }
        node = idx;
      } else if (at_op(p, ".")) {
        sp_node_id_t mem;
        unsigned line = peek(p)->line;
        advance(p);
        if (peek(p)->kind != SL_TOK_IDENT) {
          p->status = SP_ERR_UNEXPECTED_TOKEN;
          return SP_NO_NODE;
        }
        st = sp_new_node(p->arena, SP_MEMBER, line, &mem);
        if (st != SP_OK) {
          p->status = st;
          return SP_NO_NODE;
        }
        sp_node(p->arena, mem)->a = node;
        (void)sp_intern(p->arena, peek(p)->text, &sp_node(p->arena, mem)->text);
        advance(p);
        node = mem;
      } else {
        break;
      }
    }
  }

  return node;
}

/* Fold one operator off the stack into a node. */
static int reduce(parser_t *p, sp_node_id_t *operands, unsigned *n_operands,
                  const op_entry_t *op) {
  sp_node_id_t node;
  sp_status_t st;

  if (*n_operands < 2u) {
    p->status = SP_ERR_UNEXPECTED_TOKEN;
    return 0;
  }
  st = sp_new_node(p->arena,
                   is_logical(op->op) ? SP_LOGICAL : SP_BINARY,
                   op->line, &node);
  if (st != SP_OK) {
    p->status = st;
    return 0;
  }
  (void)sp_intern(p->arena, op->op, &sp_node(p->arena, node)->text);
  sp_node(p->arena, node)->a = operands[*n_operands - 2u];
  sp_node(p->arena, node)->b = operands[*n_operands - 1u];
  *n_operands -= 2u;
  operands[*n_operands] = node;
  (*n_operands)++;
  return 1;
}

static sp_node_id_t parse_expression(parser_t *p, unsigned depth) {
  sp_node_id_t operands[SP_MAX_DEPTH];
  op_entry_t ops[SP_MAX_DEPTH];
  unsigned n_operands = 0u;
  unsigned n_ops = 0u;

  if (depth >= SP_MAX_DEPTH) {
    p->status = SP_ERR_DEPTH_EXCEEDED;
    return SP_NO_NODE;
  }

  operands[n_operands] = parse_operand(p, depth);
  if (p->status != SP_OK) {
    return SP_NO_NODE;
  }
  n_operands++;

  for (;;) {
    const sl_token_t *t = peek(p);
    int prec = precedence_of(t);
    op_entry_t entry;

    if (prec == 0) {
      break;
    }
    /* A binary operator on a new line does not continue the expression;
       the JS parser uses nlBefore for the same reason, so that a call
       chain stops at a line break rather than swallowing the next
       statement. */
    if (t->nl_before != 0) {
      break;
    }

    strncpy(entry.op, t->text, sizeof(entry.op) - 1u);
    entry.op[sizeof(entry.op) - 1u] = '\0';
    entry.prec = prec;
    entry.line = t->line;

    while (n_ops > 0u
           && (ops[n_ops - 1u].prec > prec
               || (ops[n_ops - 1u].prec == prec && !right_assoc(entry.op)))) {
      if (!reduce(p, operands, &n_operands, &ops[n_ops - 1u])) {
        return SP_NO_NODE;
      }
      n_ops--;
    }

    if (n_ops >= SP_MAX_DEPTH) {
      p->status = SP_ERR_DEPTH_EXCEEDED;
      return SP_NO_NODE;
    }
    ops[n_ops] = entry;
    n_ops++;
    advance(p);

    if (n_operands >= SP_MAX_DEPTH) {
      p->status = SP_ERR_DEPTH_EXCEEDED;
      return SP_NO_NODE;
    }
    operands[n_operands] = parse_operand(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    n_operands++;
  }

  while (n_ops > 0u) {
    if (!reduce(p, operands, &n_operands, &ops[n_ops - 1u])) {
      return SP_NO_NODE;
    }
    n_ops--;
  }

  if (n_operands != 1u) {
    p->status = SP_ERR_UNEXPECTED_TOKEN;
    return SP_NO_NODE;
  }
  return operands[0];
}

/* ------------------------------------------------------------------ */
/* statements                                                          */
/* ------------------------------------------------------------------ */

static sp_node_id_t parse_statement(parser_t *p, unsigned depth);

static sp_node_id_t parse_block(parser_t *p, unsigned depth) {
  sp_node_id_t block;
  sp_status_t st;
  unsigned mark;
  unsigned line = peek(p)->line;

  if (!expect_op(p, "{")) {
    return SP_NO_NODE;
  }
  st = sp_new_node(p->arena, SP_BLOCK, line, &block);
  if (st != SP_OK) {
    p->status = st;
    return SP_NO_NODE;
  }
  mark = sp_pending_mark(p->arena);
  while (!at_op(p, "}") && !at_eof(p)) {
    sp_node_id_t stmt = parse_statement(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    st = sp_pending_push(p->arena, stmt);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
  }
  st = sp_commit_children(p->arena, block, mark);
  if (st != SP_OK) {
    p->status = st;
    return SP_NO_NODE;
  }
  if (!expect_op(p, "}")) {
    return SP_NO_NODE;
  }
  return block;
}

static sp_node_id_t parse_statement(parser_t *p, unsigned depth) {
  const sl_token_t *t = peek(p);
  sp_node_id_t node = SP_NO_NODE;
  sp_status_t st;

  if (depth >= SP_MAX_DEPTH) {
    p->status = SP_ERR_DEPTH_EXCEEDED;
    return SP_NO_NODE;
  }

  /* Constructs this port does not build yet are refused by name rather
     than misparsed. Listed in the header. */
  if (t->kind == SL_TOK_KW) {
    static const char *const unsupported[] = {
      "fn", "agent", "for", "match", "attempt", "maybe", "choose", "fork",
      "import", "redefine", "using", "spawn", "tensor", 0
    };
    unsigned i;
    for (i = 0u; unsupported[i] != 0; i++) {
      if (strcmp(t->text, unsupported[i]) == 0) {
        p->status = SP_ERR_UNSUPPORTED;
        return SP_NO_NODE;
      }
    }
  }

  if (at_kw(p, "let") || at_kw(p, "var")) {
    int mutable_binding = at_kw(p, "var");
    unsigned line = t->line;
    advance(p);
    if (peek(p)->kind != SL_TOK_IDENT) {
      p->status = SP_ERR_UNEXPECTED_TOKEN;
      return SP_NO_NODE;
    }
    st = sp_new_node(p->arena, SP_DECLARE, line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    (void)sp_intern(p->arena, peek(p)->text, &sp_node(p->arena, node)->text);
    sp_node(p->arena, node)->b = (sp_node_id_t)(mutable_binding ? 1u : 0u);
    advance(p);
    if (!expect_op(p, "=")) {
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->a = parse_expression(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    return node;
  }

  if (at_kw(p, "if")) {
    unsigned line = t->line;
    advance(p);
    st = sp_new_node(p->arena, SP_IF, line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->a = parse_expression(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->b = parse_block(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    if (at_kw(p, "else")) {
      advance(p);
      /* `else if` chains without recursion into parse_statement: the
         else branch is either a block or another if-statement. */
      if (at_kw(p, "if")) {
        sp_node(p->arena, node)->c = parse_statement(p, depth + 1u);
      } else {
        sp_node(p->arena, node)->c = parse_block(p, depth + 1u);
      }
      if (p->status != SP_OK) {
        return SP_NO_NODE;
      }
    }
    return node;
  }

  if (at_kw(p, "while")) {
    unsigned line = t->line;
    advance(p);
    st = sp_new_node(p->arena, SP_WHILE, line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->a = parse_expression(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->b = parse_block(p, depth + 1u);
    return node;
  }

  if (at_kw(p, "return")) {
    unsigned line = t->line;
    advance(p);
    st = sp_new_node(p->arena, SP_RETURN, line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    /* A bare `return` is legal; a value only follows if it is on the
       same line, matching the JS parser's nlBefore rule. */
    if (!at_op(p, "}") && !at_eof(p) && peek(p)->nl_before == 0) {
      sp_node(p->arena, node)->a = parse_expression(p, depth + 1u);
    }
    return node;
  }

  if (at_kw(p, "break") || at_kw(p, "continue")) {
    sp_kind_t k = at_kw(p, "break") ? SP_BREAK : SP_CONTINUE;
    st = sp_new_node(p->arena, k, t->line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    advance(p);
    return node;
  }

  if (at_op(p, "{")) {
    return parse_block(p, depth);
  }

  /* An expression statement, possibly an assignment. */
  {
    sp_node_id_t expr = parse_expression(p, depth + 1u);
    if (p->status != SP_OK) {
      return SP_NO_NODE;
    }
    if (at_op(p, "=")) {
      sp_node_id_t assign;
      unsigned line = peek(p)->line;
      advance(p);
      st = sp_new_node(p->arena, SP_ASSIGN, line, &assign);
      if (st != SP_OK) {
        p->status = st;
        return SP_NO_NODE;
      }
      sp_node(p->arena, assign)->a = expr;
      sp_node(p->arena, assign)->b = parse_expression(p, depth + 1u);
      if (p->status != SP_OK) {
        return SP_NO_NODE;
      }
      expr = assign;
    }
    st = sp_new_node(p->arena, SP_EXPR_STMT, t->line, &node);
    if (st != SP_OK) {
      p->status = st;
      return SP_NO_NODE;
    }
    sp_node(p->arena, node)->a = expr;
    return node;
  }
}

/* ------------------------------------------------------------------ */

sp_parse_result_t sp_parse(const sl_token_t *tokens, unsigned token_count,
                           sp_arena_t *arena) {
  parser_t p;
  sp_parse_result_t result;
  sp_node_id_t program;
  sp_status_t st;

  result.status = SP_OK;
  result.line = 0u;
  result.token_index = 0u;

  if (tokens == 0 || arena == 0 || token_count == 0u) {
    result.status = SP_ERR_NULL_ARGUMENT;
    return result;
  }

  sp_arena_init(arena);
  p.tok = tokens;
  p.count = token_count;
  p.pos = 0u;
  p.arena = arena;
  p.status = SP_OK;

  st = sp_new_node(arena, SP_PROGRAM, 1u, &program);
  if (st != SP_OK) {
    result.status = st;
    return result;
  }

  while (!at_eof(&p) && p.status == SP_OK) {
    sp_node_id_t stmt = parse_statement(&p, 0u);
    if (p.status != SP_OK) {
      break;
    }
    st = sp_pending_push(arena, stmt);
    if (st != SP_OK) {
      p.status = st;
      break;
    }
  }
  if (p.status == SP_OK) {
    st = sp_commit_children(arena, program, 0u);
    if (st != SP_OK) {
      p.status = st;
    }
  }

  if (p.status != SP_OK) {
    result.status = p.status;
    result.line = peek(&p)->line;
    result.token_index = p.pos;
    /* root stays SP_NO_NODE: a partial tree is never handed back looking
       like a whole one. */
    return result;
  }

  arena->root = program;
  return result;
}
