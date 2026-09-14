/*
 * smarsh_value.c -- see smarsh_value.h for why an arena and why
 * groundless is a value kind rather than a sentinel.
 */

#include "smarsh_value.h"

#include <string.h>

void sv_heap_init(sv_heap_t *h) {
  unsigned i;

  if (h == 0) {
    return;
  }
  /* Offset 0 is a permanent empty string, so handle 0 is always valid. */
  h->strings[0] = '\0';
  h->strings_used = 1u;
  h->lists_used = 0u;
  h->scope_count = 0u;
  for (i = 0u; i < SV_MAX_SCOPES; i++) {
    h->scopes[i].in_use = 0;
    h->scopes[i].slot_count = 0u;
    h->scopes[i].parent = SV_NO_SCOPE;
  }
}

sv_value_t sv_nil(void) {
  sv_value_t v;
  v.kind = SV_NIL;
  v.num = 0.0;
  v.str = 0u;
  v.list_at = 0u;
  v.list_n = 0u;
  return v;
}

sv_value_t sv_groundless(void) {
  sv_value_t v = sv_nil();
  v.kind = SV_GROUNDLESS;
  return v;
}

sv_value_t sv_bool(int b) {
  sv_value_t v = sv_nil();
  v.kind = SV_BOOL;
  v.num = b ? 1.0 : 0.0;
  return v;
}

sv_value_t sv_num(double n) {
  sv_value_t v = sv_nil();
  v.kind = SV_NUM;
  v.num = n;
  return v;
}

sv_status_t sv_string(sv_heap_t *h, const char *text, sv_value_t *out) {
  unsigned len;
  unsigned i;

  if (h == 0 || text == 0 || out == 0) {
    return SV_ERR_NULL_ARGUMENT;
  }
  len = (unsigned)strlen(text);
  if (h->strings_used + len + 1u > SV_MAX_STRING_POOL) {
    return SV_ERR_STRINGS_FULL;
  }
  *out = sv_nil();
  out->kind = SV_STR;
  out->str = (sv_str_t)h->strings_used;
  for (i = 0u; i < len; i++) {
    h->strings[h->strings_used + i] = text[i];
  }
  h->strings[h->strings_used + len] = '\0';
  h->strings_used += len + 1u;
  return SV_OK;
}

const char *sv_str_of(const sv_heap_t *h, sv_value_t v) {
  if (h == 0 || v.kind != SV_STR || v.str >= h->strings_used) {
    return "";
  }
  return &h->strings[v.str];
}

int sv_truthy(sv_value_t v) {
  switch (v.kind) {
    case SV_NIL:
      return 0;
    /* A refusal does not take a branch. `if speculate(...)` must not run
       the body when there were no grounds to run it on. */
    case SV_GROUNDLESS:
      return 0;
    case SV_BOOL:
      return v.num != 0.0;
    case SV_NUM:
      return v.num != 0.0;
    case SV_STR:
      return 1;
    case SV_LIST:
      return 1;
    default:
      return 0;
  }
}

static int equal_at(const sv_heap_t *h, sv_value_t a, sv_value_t b,
                    unsigned depth) {
  if (depth >= SV_MAX_EQ_DEPTH) {
    return -1;
  }
  if (a.kind != b.kind) {
    /* Notably: groundless != nil. If these compared equal the whole
       distinction would be unobservable from inside a program. */
    return 0;
  }
  switch (a.kind) {
    case SV_NIL:
      return 1;
    case SV_GROUNDLESS:
      /* Two refusals are the same refusal for comparison purposes; what
         is NOT true is that either equals nil. */
      return 1;
    case SV_BOOL:
    case SV_NUM:
      return a.num == b.num;
    case SV_STR:
      return strcmp(sv_str_of(h, a), sv_str_of(h, b)) == 0;
    case SV_LIST: {
      unsigned i;
      if (a.list_n != b.list_n) {
        return 0;
      }
      for (i = 0u; i < a.list_n; i++) {
        int e = equal_at(h, h->lists[a.list_at + i], h->lists[b.list_at + i],
                         depth + 1u);
        if (e != 1) {
          return e;   /* 0 not equal, or -1 too deep: either way, stop */
        }
      }
      return 1;
    }
    default:
      return 0;
  }
}

int sv_equal(const sv_heap_t *h, sv_value_t a, sv_value_t b) {
  return equal_at(h, a, b, 0u);
}

const char *sv_kind_name(sv_kind_t k) {
  switch (k) {
    case SV_NIL: return "nil";
    case SV_BOOL: return "bool";
    case SV_NUM: return "num";
    case SV_STR: return "str";
    case SV_LIST: return "list";
    case SV_GROUNDLESS: return "groundless";
    default: return "?";
  }
}

/* ------------------------------------------------------------------ */
/* scopes                                                               */
/* ------------------------------------------------------------------ */

sv_status_t sv_scope_push(sv_heap_t *h, sv_scope_t parent, sv_scope_t *out) {
  unsigned i;

  if (h == 0 || out == 0) {
    return SV_ERR_NULL_ARGUMENT;
  }
  /* Reuse a freed frame if there is one; the count only grows when no
     frame is free, so a loop that pushes and pops does not exhaust the
     table. Bounded by SV_MAX_SCOPES either way. */
  for (i = 0u; i < SV_MAX_SCOPES; i++) {
    if (h->scopes[i].in_use == 0) {
      h->scopes[i].in_use = 1;
      h->scopes[i].slot_count = 0u;
      h->scopes[i].parent = parent;
      if (i >= h->scope_count) {
        h->scope_count = i + 1u;
      }
      *out = (sv_scope_t)i;
      return SV_OK;
    }
  }
  return SV_ERR_SCOPES_FULL;
}

void sv_scope_pop(sv_heap_t *h, sv_scope_t scope) {
  if (h == 0 || scope >= SV_MAX_SCOPES) {
    return;
  }
  h->scopes[scope].in_use = 0;
  h->scopes[scope].slot_count = 0u;
  h->scopes[scope].parent = SV_NO_SCOPE;
}

sv_status_t sv_declare(sv_heap_t *h, sv_scope_t scope, const char *name,
                       sv_value_t value, int mutable_binding) {
  sv_scope_frame_t *f;
  unsigned i;

  if (h == 0 || name == 0 || scope >= SV_MAX_SCOPES) {
    return SV_ERR_NULL_ARGUMENT;
  }
  if (strlen(name) >= SV_MAX_NAME) {
    return SV_ERR_NAME_TOO_LONG;
  }
  f = &h->scopes[scope];

  /* Redeclaring in the same scope overwrites, matching the JS engine's
     shadowing within a block rather than erroring. */
  for (i = 0u; i < f->slot_count; i++) {
    if (strcmp(f->slots[i].name, name) == 0) {
      f->slots[i].value = value;
      f->slots[i].mutable_binding = mutable_binding;
      return SV_OK;
    }
  }
  if (f->slot_count >= SV_MAX_SLOTS_PER_SCOPE) {
    return SV_ERR_SLOTS_FULL;
  }
  strncpy(f->slots[f->slot_count].name, name, SV_MAX_NAME - 1u);
  f->slots[f->slot_count].name[SV_MAX_NAME - 1u] = '\0';
  f->slots[f->slot_count].value = value;
  f->slots[f->slot_count].mutable_binding = mutable_binding;
  f->slot_count++;
  return SV_OK;
}

sv_status_t sv_lookup(const sv_heap_t *h, sv_scope_t scope, const char *name,
                      sv_value_t *out) {
  unsigned hops = 0u;
  sv_scope_t s = scope;

  if (h == 0 || name == 0 || out == 0) {
    return SV_ERR_NULL_ARGUMENT;
  }
  /* Iterative, not recursive, and bounded by SV_MAX_SCOPES so a cycle in
     the parent chain cannot hang this. */
  while (s != SV_NO_SCOPE && hops < SV_MAX_SCOPES) {
    const sv_scope_frame_t *f = &h->scopes[s];
    unsigned i;
    for (i = 0u; i < f->slot_count; i++) {
      if (strcmp(f->slots[i].name, name) == 0) {
        *out = f->slots[i].value;
        return SV_OK;
      }
    }
    s = f->parent;
    hops++;
  }
  return SV_ERR_UNKNOWN_NAME;
}

sv_status_t sv_assign(sv_heap_t *h, sv_scope_t scope, const char *name,
                      sv_value_t value) {
  unsigned hops = 0u;
  sv_scope_t s = scope;

  if (h == 0 || name == 0) {
    return SV_ERR_NULL_ARGUMENT;
  }
  while (s != SV_NO_SCOPE && hops < SV_MAX_SCOPES) {
    sv_scope_frame_t *f = &h->scopes[s];
    unsigned i;
    for (i = 0u; i < f->slot_count; i++) {
      if (strcmp(f->slots[i].name, name) == 0) {
        if (f->slots[i].mutable_binding == 0) {
          /* `let` means immutable. Refused, not overwritten. */
          return SV_ERR_IMMUTABLE;
        }
        f->slots[i].value = value;
        return SV_OK;
      }
    }
    s = f->parent;
    hops++;
  }
  return SV_ERR_UNKNOWN_NAME;
}
