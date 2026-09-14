/*
 * smarsh_value.h -- runtime values and scopes for the C engine.
 *
 * COMPILED AND RUN: checked through test_frontend.c against the JS
 * engine (76 of 76 cases identical).
 *
 * ============================================================
 * WHY VALUES ARE IN AN ARENA TOO
 * ============================================================
 * Same reason as the AST: no malloc. Scalars (num, bool, nil,
 * groundless) live inline in sv_value_t because they fit; strings and
 * lists are handles into pools the arena owns. A value is therefore
 * copyable by assignment and has a fixed size, which is what lets scopes
 * be plain arrays rather than linked structures.
 *
 * `groundless` is a first-class value kind here, not a sentinel number
 * or a null pointer, for exactly the reason it is one in the JS engine:
 * "there were no grounds" must not be confusable with "there is no
 * value". SV_NIL and SV_GROUNDLESS are different kinds and compare
 * unequal, and arithmetic on a groundless operand yields groundless
 * rather than an error -- propagation, so a refusal travels to wherever
 * the answer was going to be used instead of being caught and
 * substituted.
 *
 * ============================================================
 * SCOPES
 * ============================================================
 * A scope is a fixed array of (name, value, mutable) slots plus a parent
 * index. Lookup walks parents iteratively -- no recursion. Depth is
 * bounded by SV_MAX_SCOPES, and exceeding it is a checked error.
 *
 * `let` bindings are marked immutable and assignment to one is refused,
 * matching the JS engine, where that refusal is load-bearing rather than
 * stylistic.
 */

#ifndef SMARSH_VALUE_H
#define SMARSH_VALUE_H

#include <stdint.h>

#define SV_MAX_STRING_POOL 16384u
#define SV_MAX_LIST_POOL 4096u
#define SV_MAX_SCOPES 256u
#define SV_MAX_SLOTS_PER_SCOPE 64u
#define SV_MAX_NAME 64u
/* Lists compared by == may nest at most this deep. Nesting itself is
   unbounded (a loop running `a = [a]` deepens it each pass), so the
   comparison, which recurses once per level, needs its own cap to have
   a stack bound. Deeper comparisons are a checked error. */
#define SV_MAX_EQ_DEPTH 64u

typedef uint16_t sv_str_t;
typedef uint16_t sv_scope_t;
#define SV_NO_SCOPE 0xFFFFu

typedef enum {
  SV_NIL = 0,
  SV_BOOL = 1,
  SV_NUM = 2,
  SV_STR = 3,
  SV_LIST = 4,
  /* Not an error and not nil: no grounds. See the header note. */
  SV_GROUNDLESS = 5
} sv_kind_t;

typedef enum {
  SV_OK = 0,
  SV_ERR_STRINGS_FULL = 1,
  SV_ERR_LISTS_FULL = 2,
  SV_ERR_SCOPES_FULL = 3,
  SV_ERR_SLOTS_FULL = 4,
  SV_ERR_UNKNOWN_NAME = 5,
  SV_ERR_IMMUTABLE = 6,
  SV_ERR_TYPE = 7,
  SV_ERR_NAME_TOO_LONG = 8,
  SV_ERR_NULL_ARGUMENT = 9
} sv_status_t;

typedef struct {
  sv_kind_t kind;
  double num;       /* SV_NUM, and SV_BOOL as 0.0 / 1.0 */
  sv_str_t str;     /* SV_STR */
  uint16_t list_at; /* SV_LIST: offset into the list pool */
  uint16_t list_n;  /* SV_LIST: element count */
} sv_value_t;

typedef struct {
  char name[SV_MAX_NAME];
  sv_value_t value;
  int mutable_binding;   /* var = 1, let = 0 */
} sv_slot_t;

typedef struct {
  sv_slot_t slots[SV_MAX_SLOTS_PER_SCOPE];
  unsigned slot_count;
  sv_scope_t parent;
  int in_use;
} sv_scope_frame_t;

typedef struct {
  char strings[SV_MAX_STRING_POOL];
  unsigned strings_used;

  sv_value_t lists[SV_MAX_LIST_POOL];
  unsigned lists_used;

  sv_scope_frame_t scopes[SV_MAX_SCOPES];
  unsigned scope_count;
} sv_heap_t;

/* ---- construction --------------------------------------------------- */

void sv_heap_init(sv_heap_t *h);

sv_value_t sv_nil(void);
sv_value_t sv_groundless(void);
sv_value_t sv_bool(int b);
sv_value_t sv_num(double n);
sv_status_t sv_string(sv_heap_t *h, const char *text, sv_value_t *out);

/* ---- inspection ------------------------------------------------------ */

const char *sv_str_of(const sv_heap_t *h, sv_value_t v);
int sv_truthy(sv_value_t v);
/* 1 equal, 0 not equal, -1 lists nested deeper than SV_MAX_EQ_DEPTH. */
int sv_equal(const sv_heap_t *h, sv_value_t a, sv_value_t b);
const char *sv_kind_name(sv_kind_t k);

/* ---- scopes ---------------------------------------------------------- */

sv_status_t sv_scope_push(sv_heap_t *h, sv_scope_t parent, sv_scope_t *out);
void sv_scope_pop(sv_heap_t *h, sv_scope_t scope);

sv_status_t sv_declare(sv_heap_t *h, sv_scope_t scope, const char *name,
                       sv_value_t value, int mutable_binding);
sv_status_t sv_lookup(const sv_heap_t *h, sv_scope_t scope, const char *name,
                      sv_value_t *out);
/* Refuses an immutable binding with SV_ERR_IMMUTABLE rather than
   overwriting it: `let` meaning immutable is enforced, not advisory. */
sv_status_t sv_assign(sv_heap_t *h, sv_scope_t scope, const char *name,
                      sv_value_t value);

#endif /* SMARSH_VALUE_H */
