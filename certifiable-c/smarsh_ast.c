/*
 * smarsh_ast.c -- arena implementation. See smarsh_ast.h for why indices
 * rather than pointers, and for its status (compiled, checked by
 * test_frontend.c).
 */

#include "smarsh_ast.h"

#include <string.h>

void sp_arena_init(sp_arena_t *arena) {
  if (arena == 0) {
    return;
  }
  arena->node_count = 0u;
  arena->child_count = 0u;
  /* Offset 0 is a permanent empty string, so sp_str_t 0 is always valid
     and a node that never set its text reads "" rather than garbage. */
  arena->strings[0] = '\0';
  arena->strings_used = 1u;
  arena->root = SP_NO_NODE;
  arena->pending_top = 0u;
}

sp_status_t sp_new_node(sp_arena_t *arena, sp_kind_t kind, unsigned line,
                        sp_node_id_t *out_id) {
  sp_node_t *n;

  if (arena == 0 || out_id == 0) {
    return SP_ERR_NULL_ARGUMENT;
  }
  if (arena->node_count >= SP_MAX_NODES) {
    /* A truncated tree that still looks like a tree is the worst failure
       available to something whose output is evidence. */
    return SP_ERR_ARENA_FULL;
  }

  n = &arena->nodes[arena->node_count];
  n->kind = kind;
  n->line = line;
  n->a = SP_NO_NODE;
  n->b = SP_NO_NODE;
  n->c = SP_NO_NODE;
  n->d = SP_NO_NODE;
  n->text = 0u;
  n->first_child = 0u;
  n->child_count = 0u;

  *out_id = (sp_node_id_t)arena->node_count;
  arena->node_count++;
  return SP_OK;
}

sp_status_t sp_intern(sp_arena_t *arena, const char *text, sp_str_t *out) {
  unsigned len;
  unsigned i;

  if (arena == 0 || text == 0 || out == 0) {
    return SP_ERR_NULL_ARGUMENT;
  }
  len = (unsigned)strlen(text);
  if (arena->strings_used + len + 1u > SP_MAX_STRING_POOL) {
    return SP_ERR_STRINGS_FULL;
  }

  *out = (sp_str_t)arena->strings_used;
  for (i = 0u; i < len; i++) {
    arena->strings[arena->strings_used + i] = text[i];
  }
  arena->strings[arena->strings_used + len] = '\0';
  arena->strings_used += len + 1u;
  return SP_OK;
}

const char *sp_string(const sp_arena_t *arena, sp_str_t handle) {
  if (arena == 0 || handle >= arena->strings_used) {
    return "";
  }
  return &arena->strings[handle];
}

sp_status_t sp_add_child(sp_arena_t *arena, sp_node_id_t parent,
                         sp_node_id_t child) {
  sp_node_t *p;

  if (arena == 0) {
    return SP_ERR_NULL_ARGUMENT;
  }
  if (parent >= arena->node_count) {
    return SP_ERR_NULL_ARGUMENT;
  }
  if (arena->child_count >= SP_MAX_CHILDREN) {
    return SP_ERR_CHILDREN_FULL;
  }

  p = &arena->nodes[parent];
  if (p->child_count == 0u) {
    p->first_child = (uint16_t)arena->child_count;
  } else if ((unsigned)(p->first_child + p->child_count) != arena->child_count) {
    /* A node owns a CONTIGUOUS run of the flat pool. If anything else
       appended in between, extending this node would overwrite that
       other node's children. Refused rather than silently corrupting
       two trees at once -- the caller has to finish one node's children
       before starting another's. */
    return SP_ERR_NONCONTIGUOUS;
  }

  arena->children[arena->child_count] = child;
  arena->child_count++;
  p->child_count++;
  return SP_OK;
}

sp_node_t *sp_node(sp_arena_t *arena, sp_node_id_t id) {
  if (arena == 0 || id >= arena->node_count) {
    return 0;
  }
  return &arena->nodes[id];
}

const sp_node_t *sp_node_const(const sp_arena_t *arena, sp_node_id_t id) {
  if (arena == 0 || id >= arena->node_count) {
    return 0;
  }
  return &arena->nodes[id];
}

sp_node_id_t sp_child(const sp_arena_t *arena, sp_node_id_t parent,
                      unsigned i) {
  const sp_node_t *p;

  if (arena == 0 || parent >= arena->node_count) {
    return SP_NO_NODE;
  }
  p = &arena->nodes[parent];
  if (i >= p->child_count) {
    return SP_NO_NODE;
  }
  return arena->children[p->first_child + i];
}

unsigned sp_pending_mark(const sp_arena_t *arena) {
  return arena == 0 ? 0u : arena->pending_top;
}

sp_status_t sp_pending_push(sp_arena_t *arena, sp_node_id_t child) {
  if (arena == 0) {
    return SP_ERR_NULL_ARGUMENT;
  }
  if (arena->pending_top >= SP_MAX_CHILDREN) {
    return SP_ERR_CHILDREN_FULL;
  }
  arena->pending[arena->pending_top] = child;
  arena->pending_top++;
  return SP_OK;
}

sp_status_t sp_commit_children(sp_arena_t *arena, sp_node_id_t parent,
                               unsigned mark) {
  sp_node_t *p;
  unsigned n;
  unsigned i;

  if (arena == 0 || parent >= arena->node_count) {
    return SP_ERR_NULL_ARGUMENT;
  }
  if (mark > arena->pending_top) {
    return SP_ERR_NONCONTIGUOUS;
  }
  p = &arena->nodes[parent];
  if (p->child_count != 0u) {
    return SP_ERR_NONCONTIGUOUS;   /* a node's run is committed once */
  }
  n = arena->pending_top - mark;
  if (arena->child_count + n > SP_MAX_CHILDREN) {
    return SP_ERR_CHILDREN_FULL;
  }
  p->first_child = (uint16_t)arena->child_count;
  for (i = 0u; i < SP_MAX_CHILDREN; i++) {
    if (i >= n) {
      break;
    }
    arena->children[arena->child_count] = arena->pending[mark + i];
    arena->child_count++;
  }
  p->child_count = (uint16_t)n;
  arena->pending_top = mark;
  return SP_OK;
}
