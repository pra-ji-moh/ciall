/*
 * smarsh_interp.c -- the tree-walking evaluator. See smarsh_interp.h for
 * which engine this is, what it covers, and what it deliberately is not.
 */

#include "smarsh_interp.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define SI_MAX_DEPTH 64u

typedef enum {
  FLOW_NORMAL = 0,
  FLOW_RETURN = 1,
  FLOW_BREAK = 2,
  FLOW_CONTINUE = 3
} flow_t;

typedef struct {
  sp_arena_t *arena;
  sv_heap_t *heap;
  si_result_t *res;
  unsigned steps_left;
  flow_t flow;
  sv_value_t returned;
} interp_t;

static sv_value_t eval(interp_t *in, sp_node_id_t id, sv_scope_t scope,
                       unsigned depth);

/* ------------------------------------------------------------------ */
/* helpers                                                             */
/* ------------------------------------------------------------------ */

static sv_value_t fail(interp_t *in, si_status_t st, unsigned line) {
  if (in->res->status == SI_OK) {
    in->res->status = st;
    in->res->line = line;
  }
  return sv_nil();
}

static int spend(interp_t *in, unsigned line) {
  if (in->steps_left == 0u) {
    /* Not catchable. A program that could swallow its own budget stop
       could ignore it, which would make the budget advisory. */
    (void)fail(in, SI_ERR_STEPS_EXHAUSTED, line);
    return 0;
  }
  in->steps_left--;
  return 1;
}

/* Render a value the way the JS engine's `str()` does, since the whole
   point of the output buffer is being diffable against it. */
/* Renders v into buf. Returns 0, or RENDER_TOO_DEEP when lists nest past
   SV_MAX_EQ_DEPTH (the recursion's stack bound), or RENDER_TOO_LONG when
   the text does not fit: a checked error either way, never a silently
   truncated value. It used to format each element into a 64-byte local,
   so a long nested list printed cut short without saying so. */
#define RENDER_TOO_DEEP (-1)
#define RENDER_TOO_LONG (-2)

static int render_at(const sv_heap_t *h, sv_value_t v, char *buf, unsigned cap,
                     unsigned depth);

static int put(char *buf, unsigned cap, unsigned *used, const char *s) {
  size_t n = strlen(s);
  if (*used + n + 1u > cap) {
    return RENDER_TOO_LONG;
  }
  memcpy(buf + *used, s, n + 1u);
  *used += (unsigned)n;
  return 0;
}

static int render(const sv_heap_t *h, sv_value_t v, char *buf, unsigned cap) {
  return render_at(h, v, buf, cap, 0u);
}

static int render_at(const sv_heap_t *h, sv_value_t v, char *buf, unsigned cap,
                     unsigned depth) {
  if (depth >= SV_MAX_EQ_DEPTH) {
    return RENDER_TOO_DEEP;
  }
  if (v.kind == SV_LIST) {
    unsigned i;
    unsigned used = 0u;
    int e = put(buf, cap, &used, "[");
    for (i = 0u; e == 0 && i < v.list_n; i++) {
      if (i > 0u) {
        e = put(buf, cap, &used, ", ");
      }
      if (e == 0) {
        e = render_at(h, h->lists[v.list_at + i], buf + used, cap - used, depth + 1u);
        used += (unsigned)strlen(buf + used);
      }
    }
    return e != 0 ? e : put(buf, cap, &used, "]");
  }
  {
    unsigned used = 0u;
    char tmp[40];
    switch (v.kind) {
      case SV_NIL:
        return put(buf, cap, &used, "nil");
      case SV_GROUNDLESS:
        return put(buf, cap, &used, "groundless");
      case SV_BOOL:
        return put(buf, cap, &used, (v.num != 0.0) ? "true" : "false");
      case SV_NUM:
        /* Integers print without a decimal point, matching the JS engine;
           a bare %g would render 1 as "1" but 1e21 differently, so the
           integer case is separated explicitly. */
        if (v.num == (double)(long long)v.num
            && v.num < 1e15 && v.num > -1e15) {
          (void)snprintf(tmp, sizeof tmp, "%lld", (long long)v.num);
        } else {
          (void)snprintf(tmp, sizeof tmp, "%.12g", v.num);
        }
        return put(buf, cap, &used, tmp);
      case SV_STR:
        return put(buf, cap, &used, sv_str_of(h, v));
      default:
        return put(buf, cap, &used, "?");
    }
  }
}

static void emit(interp_t *in, const char *text) {
  unsigned len = (unsigned)strlen(text);

  if (in->res->output_used + len + 2u >= SI_MAX_OUTPUT) {
    (void)fail(in, SI_ERR_OUTPUT_FULL, 0u);
    return;
  }
  memcpy(&in->res->output[in->res->output_used], text, len);
  in->res->output_used += len;
  in->res->output[in->res->output_used] = '\n';
  in->res->output_used++;
  in->res->output[in->res->output_used] = '\0';
}

/* ------------------------------------------------------------------ */
/* operators                                                           */
/* ------------------------------------------------------------------ */

static sv_value_t binary_op(interp_t *in, const char *op, sv_value_t l,
                            sv_value_t r, unsigned line) {
  /* Groundless propagates through every operator except equality, so a
     refusal travels to where the answer was going to be used instead of
     being caught and substituted. Equality stays concrete or the
     distinction could not be observed at all. */
  if (l.kind == SV_GROUNDLESS || r.kind == SV_GROUNDLESS) {
    if (strcmp(op, "==") == 0) {
      return sv_bool(l.kind == SV_GROUNDLESS && r.kind == SV_GROUNDLESS);
    }
    if (strcmp(op, "!=") == 0) {
      return sv_bool(!(l.kind == SV_GROUNDLESS && r.kind == SV_GROUNDLESS));
    }
    return sv_groundless();
  }

  if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0) {
    int e = sv_equal(in->heap, l, r);
    if (e < 0) {
      return fail(in, SI_ERR_DEPTH_EXCEEDED, line);
    }
    return sv_bool(strcmp(op, "==") == 0 ? e : !e);
  }

  /* String concatenation is the one non-numeric binary case. */
  if (strcmp(op, "+") == 0 && l.kind == SV_STR && r.kind == SV_STR) {
    char joined[512];
    sv_value_t out;
    (void)snprintf(joined, sizeof(joined), "%s%s",
                   sv_str_of(in->heap, l), sv_str_of(in->heap, r));
    if (sv_string(in->heap, joined, &out) != SV_OK) {
      return fail(in, SI_ERR_INTERNAL, line);
    }
    return out;
  }

  if (l.kind != SV_NUM || r.kind != SV_NUM) {
    return fail(in, SI_ERR_TYPE, line);
  }

  if (strcmp(op, "+") == 0) { return sv_num(l.num + r.num); }
  if (strcmp(op, "-") == 0) { return sv_num(l.num - r.num); }
  if (strcmp(op, "*") == 0) { return sv_num(l.num * r.num); }
  if (strcmp(op, "**") == 0) { return sv_num(pow(l.num, r.num)); }
  if (strcmp(op, "<") == 0) { return sv_bool(l.num < r.num); }
  if (strcmp(op, ">") == 0) { return sv_bool(l.num > r.num); }
  if (strcmp(op, "<=") == 0) { return sv_bool(l.num <= r.num); }
  if (strcmp(op, ">=") == 0) { return sv_bool(l.num >= r.num); }
  if (strcmp(op, "/") == 0) {
    if (r.num == 0.0) {
      return fail(in, SI_ERR_DIVIDE_BY_ZERO, line);
    }
    return sv_num(l.num / r.num);
  }
  if (strcmp(op, "%") == 0) {
    if (r.num == 0.0) {
      return fail(in, SI_ERR_DIVIDE_BY_ZERO, line);
    }
    return sv_num(fmod(l.num, r.num));
  }
  return fail(in, SI_ERR_UNSUPPORTED, line);
}

/* ------------------------------------------------------------------ */
/* builtins                                                            */
/* ------------------------------------------------------------------ */

static sv_value_t call_builtin(interp_t *in, const char *name,
                               const sv_value_t *args, unsigned argc,
                               unsigned line) {
  char buf[1024];
  int e;

  if (strcmp(name, "print") == 0) {
    if (argc != 1u) {
      return fail(in, SI_ERR_TYPE, line);
    }
    e = render(in->heap, args[0], buf, sizeof(buf));
    if (e != 0) {
      return fail(in, e == RENDER_TOO_DEEP ? SI_ERR_DEPTH_EXCEEDED
                                           : SI_ERR_OUTPUT_FULL, line);
    }
    emit(in, buf);
    return sv_nil();
  }
  if (strcmp(name, "str") == 0) {
    sv_value_t out;
    if (argc != 1u) {
      return fail(in, SI_ERR_TYPE, line);
    }
    e = render(in->heap, args[0], buf, sizeof(buf));
    if (e != 0) {
      return fail(in, e == RENDER_TOO_DEEP ? SI_ERR_DEPTH_EXCEEDED
                                           : SI_ERR_OUTPUT_FULL, line);
    }
    if (sv_string(in->heap, buf, &out) != SV_OK) {
      return fail(in, SI_ERR_INTERNAL, line);
    }
    return out;
  }
  if (strcmp(name, "len") == 0) {
    if (argc != 1u) {
      return fail(in, SI_ERR_TYPE, line);
    }
    if (args[0].kind == SV_LIST) {
      return sv_num((double)args[0].list_n);
    }
    if (args[0].kind == SV_STR) {
      return sv_num((double)strlen(sv_str_of(in->heap, args[0])));
    }
    return fail(in, SI_ERR_TYPE, line);
  }
  return fail(in, SI_ERR_UNSUPPORTED, line);
}

/* ------------------------------------------------------------------ */
/* evaluation                                                          */
/* ------------------------------------------------------------------ */

static sv_value_t eval_block(interp_t *in, sp_node_id_t id, sv_scope_t parent,
                             unsigned depth) {
  sv_scope_t scope;
  unsigned i;
  const sp_node_t *n = sp_node_const(in->arena, id);

  if (sv_scope_push(in->heap, parent, &scope) != SV_OK) {
    return fail(in, SI_ERR_DEPTH_EXCEEDED, n ? n->line : 0u);
  }
  for (i = 0u; i < n->child_count; i++) {
    (void)eval(in, sp_child(in->arena, id, i), scope, depth + 1u);
    if (in->res->status != SI_OK || in->flow != FLOW_NORMAL) {
      break;
    }
  }
  sv_scope_pop(in->heap, scope);
  return sv_nil();
}

static sv_value_t eval(interp_t *in, sp_node_id_t id, sv_scope_t scope,
                       unsigned depth) {
  const sp_node_t *n;

  if (in->res->status != SI_OK) {
    return sv_nil();
  }
  if (depth >= SI_MAX_DEPTH) {
    return fail(in, SI_ERR_DEPTH_EXCEEDED, 0u);
  }
  n = sp_node_const(in->arena, id);
  if (n == 0) {
    return sv_nil();
  }
  if (!spend(in, n->line)) {
    return sv_nil();
  }

  switch (n->kind) {
    case SP_NUM: {
      double d = 0.0;
      (void)sscanf(sp_string(in->arena, n->text), "%lf", &d);
      return sv_num(d);
    }
    case SP_DEC_LIT: {
      /* Exact decimals are not implemented here. Reading the digits
         through a double is precisely the rounding that type exists to
         avoid, so this refuses rather than quietly being wrong. */
      return fail(in, SI_ERR_UNSUPPORTED, n->line);
    }
    case SP_STR: {
      sv_value_t out;
      if (sv_string(in->heap, sp_string(in->arena, n->text), &out) != SV_OK) {
        return fail(in, SI_ERR_INTERNAL, n->line);
      }
      return out;
    }
    case SP_BOOL:
      return sv_bool(n->a != 0u);
    case SP_NIL:
      return sv_nil();

    case SP_IDENT: {
      sv_value_t v;
      sv_status_t st = sv_lookup(in->heap, scope, sp_string(in->arena, n->text), &v);
      if (st != SV_OK) {
        return fail(in, SI_ERR_UNKNOWN_NAME, n->line);
      }
      return v;
    }

    case SP_LIST_LIT: {
      sv_value_t out = sv_nil();
      unsigned i;
      unsigned at = in->heap->lists_used;

      if (at + n->child_count > SV_MAX_LIST_POOL) {
        return fail(in, SI_ERR_INTERNAL, n->line);
      }
      /* Reserve the slots BEFORE evaluating the elements. An element can
         itself be a list literal, which allocates from lists_used; if the
         reservation came after, the inner list would be placed on top of
         these slots and the outer list would end up containing itself.
         It did: [1, [2, 3]] printed forever until the stack ran out. The
         same contiguity bug the parser's pending-children stack fixed. */
      in->heap->lists_used += n->child_count;
      for (i = 0u; i < n->child_count; i++) {
        in->heap->lists[at + i] = eval(in, sp_child(in->arena, id, i), scope, depth + 1u);
        if (in->res->status != SI_OK) {
          return sv_nil();
        }
      }
      out.kind = SV_LIST;
      out.list_at = (uint16_t)at;
      out.list_n = (uint16_t)n->child_count;
      return out;
    }

    case SP_UNARY: {
      sv_value_t v = eval(in, n->a, scope, depth + 1u);
      const char *op = sp_string(in->arena, n->text);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      if (v.kind == SV_GROUNDLESS) {
        /* `not groundless` is deliberately NOT groundless: asking
           whether something is falsy is a question with an answer even
           when the value has no grounds. Arithmetic negation still
           propagates. */
        if (strcmp(op, "not") == 0 || strcmp(op, "!") == 0) {
          return sv_bool(1);
        }
        return sv_groundless();
      }
      if (strcmp(op, "not") == 0 || strcmp(op, "!") == 0) {
        return sv_bool(!sv_truthy(v));
      }
      if (strcmp(op, "-") == 0) {
        if (v.kind != SV_NUM) {
          return fail(in, SI_ERR_TYPE, n->line);
        }
        return sv_num(-v.num);
      }
      return fail(in, SI_ERR_UNSUPPORTED, n->line);
    }

    case SP_BINARY: {
      sv_value_t l = eval(in, n->a, scope, depth + 1u);
      sv_value_t r;
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      r = eval(in, n->b, scope, depth + 1u);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      return binary_op(in, sp_string(in->arena, n->text), l, r, n->line);
    }

    case SP_LOGICAL: {
      /* Short circuit, matching the JS engine: the right side is not
         evaluated when the left already decides it. */
      sv_value_t l = eval(in, n->a, scope, depth + 1u);
      const char *op = sp_string(in->arena, n->text);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      if (strcmp(op, "and") == 0) {
        if (!sv_truthy(l)) {
          return l;
        }
        return eval(in, n->b, scope, depth + 1u);
      }
      if (sv_truthy(l)) {
        return l;
      }
      return eval(in, n->b, scope, depth + 1u);
    }

    case SP_INDEX: {
      sv_value_t obj = eval(in, n->a, scope, depth + 1u);
      sv_value_t idx;
      unsigned i;

      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      if (n->child_count != 1u) {
        return fail(in, SI_ERR_UNSUPPORTED, n->line);
      }
      idx = eval(in, sp_child(in->arena, id, 0u), scope, depth + 1u);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      if (obj.kind != SV_LIST || idx.kind != SV_NUM) {
        return fail(in, SI_ERR_TYPE, n->line);
      }
      i = (unsigned)idx.num;
      if (idx.num < 0.0 || i >= obj.list_n) {
        return fail(in, SI_ERR_INDEX_OUT_OF_RANGE, n->line);
      }
      return in->heap->lists[obj.list_at + i];
    }

    case SP_CALL: {
      const sp_node_t *callee = sp_node_const(in->arena, n->a);
      sv_value_t args[8];
      unsigned i;

      if (callee == 0 || callee->kind != SP_IDENT) {
        /* Only direct calls to named builtins exist here; user
           functions are not implemented. */
        return fail(in, SI_ERR_UNSUPPORTED, n->line);
      }
      if (n->child_count > 8u) {
        return fail(in, SI_ERR_UNSUPPORTED, n->line);
      }
      for (i = 0u; i < n->child_count; i++) {
        args[i] = eval(in, sp_child(in->arena, id, i), scope, depth + 1u);
        if (in->res->status != SI_OK) {
          return sv_nil();
        }
      }
      return call_builtin(in, sp_string(in->arena, callee->text), args,
                          n->child_count, n->line);
    }

    /* --- statements ---------------------------------------------- */

    case SP_PROGRAM: {
      unsigned i;
      for (i = 0u; i < n->child_count; i++) {
        (void)eval(in, sp_child(in->arena, id, i), scope, depth + 1u);
        if (in->res->status != SI_OK || in->flow != FLOW_NORMAL) {
          break;
        }
      }
      return sv_nil();
    }

    case SP_BLOCK:
      return eval_block(in, id, scope, depth);

    case SP_EXPR_STMT:
      return eval(in, n->a, scope, depth + 1u);

    case SP_DECLARE: {
      sv_value_t v = eval(in, n->a, scope, depth + 1u);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      if (sv_declare(in->heap, scope, sp_string(in->arena, n->text), v,
                     n->b != 0u) != SV_OK) {
        return fail(in, SI_ERR_INTERNAL, n->line);
      }
      return sv_nil();
    }

    case SP_ASSIGN: {
      const sp_node_t *target = sp_node_const(in->arena, n->a);
      sv_value_t v;
      sv_status_t st;

      if (target == 0 || target->kind != SP_IDENT) {
        return fail(in, SI_ERR_UNSUPPORTED, n->line);
      }
      v = eval(in, n->b, scope, depth + 1u);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      st = sv_assign(in->heap, scope, sp_string(in->arena, target->text), v);
      if (st == SV_ERR_IMMUTABLE) {
        return fail(in, SI_ERR_IMMUTABLE, n->line);
      }
      if (st != SV_OK) {
        return fail(in, SI_ERR_UNKNOWN_NAME, n->line);
      }
      return v;
    }

    case SP_IF: {
      sv_value_t test = eval(in, n->a, scope, depth + 1u);
      if (in->res->status != SI_OK) {
        return sv_nil();
      }
      if (sv_truthy(test)) {
        return eval(in, n->b, scope, depth + 1u);
      }
      if (n->c != SP_NO_NODE) {
        return eval(in, n->c, scope, depth + 1u);
      }
      return sv_nil();
    }

    case SP_WHILE: {
      /* Bounded by the step budget rather than an iteration count: an
         infinite loop stops because it runs out of steps, which is the
         same guarantee the JS engine gives. */
      for (;;) {
        sv_value_t test = eval(in, n->a, scope, depth + 1u);
        if (in->res->status != SI_OK) {
          return sv_nil();
        }
        if (!sv_truthy(test)) {
          break;
        }
        (void)eval(in, n->b, scope, depth + 1u);
        if (in->res->status != SI_OK) {
          return sv_nil();
        }
        if (in->flow == FLOW_BREAK) {
          in->flow = FLOW_NORMAL;
          break;
        }
        if (in->flow == FLOW_CONTINUE) {
          in->flow = FLOW_NORMAL;
        }
        if (in->flow == FLOW_RETURN) {
          break;
        }
      }
      return sv_nil();
    }

    case SP_RETURN: {
      if (n->a != SP_NO_NODE) {
        in->returned = eval(in, n->a, scope, depth + 1u);
      } else {
        in->returned = sv_nil();
      }
      in->flow = FLOW_RETURN;
      return in->returned;
    }

    case SP_BREAK:
      in->flow = FLOW_BREAK;
      return sv_nil();

    case SP_CONTINUE:
      in->flow = FLOW_CONTINUE;
      return sv_nil();

    default:
      return fail(in, SI_ERR_UNSUPPORTED, n->line);
  }
}

/* ------------------------------------------------------------------ */

si_status_t si_run(sp_arena_t *arena, sv_heap_t *heap, unsigned step_limit,
                   si_result_t *out) {
  interp_t in;
  sv_scope_t global;

  if (arena == 0 || heap == 0 || out == 0) {
    return SI_ERR_INTERNAL;
  }
  out->status = SI_OK;
  out->line = 0u;
  out->output[0] = '\0';
  out->output_used = 0u;
  out->steps_used = 0u;

  if (arena->root == SP_NO_NODE) {
    /* A partially parsed arena is never executed. */
    out->status = SI_ERR_INTERNAL;
    return out->status;
  }

  sv_heap_init(heap);
  if (sv_scope_push(heap, SV_NO_SCOPE, &global) != SV_OK) {
    out->status = SI_ERR_INTERNAL;
    return out->status;
  }

  in.arena = arena;
  in.heap = heap;
  in.res = out;
  in.steps_left = (step_limit == 0u) ? SI_DEFAULT_STEPS : step_limit;
  in.flow = FLOW_NORMAL;
  in.returned = sv_nil();

  (void)eval(&in, arena->root, global, 0u);
  out->steps_used = ((step_limit == 0u) ? SI_DEFAULT_STEPS : step_limit)
                    - in.steps_left;
  return out->status;
}
