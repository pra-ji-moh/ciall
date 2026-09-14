/*
 * smarsh_ast.h -- the Smarsh AST in C: a statically-allocated node arena
 * with children addressed by index rather than pointer.
 *
 * COMPILED AND RUN: checked through test_frontend.c against the JS
 * engine (76 of 76 cases identical). The node kinds below are transcribed from the 49 `type:`
 * tags ../../smarsh/src/parser.js actually emits, so the enum is complete
 * even where the parser does not yet build every one of them.
 *
 * ============================================================
 * WHY AN ARENA AND WHY INDICES
 * ============================================================
 * The JS parser allocates node objects freely and links them by
 * reference. Neither is available here: no malloc, and a pointer into a
 * growable structure is invalidated the moment it grows. So every node
 * lives in one fixed array and every link is a uint16 index into it.
 * That also makes the whole tree trivially serialisable and comparable,
 * which matters for the differential oracle: two engines can be checked
 * for producing an identical tree by memcmp on the arena, without
 * walking anything.
 *
 * Running out of nodes is a checked error (SP_ERR_ARENA_FULL), never a
 * silent truncation -- a half-parsed tree that looks complete is the
 * worst possible failure for something whose job is producing evidence.
 *
 * ============================================================
 * THE FOUR GENERIC SLOTS
 * ============================================================
 * Rather than a union per node kind -- which needs a tag-dispatch to read
 * safely and grows every time a kind is added -- each node has four child
 * slots and a text handle. What each slot MEANS is documented per kind in
 * the enum below, and nothing reads a slot a kind does not define. This
 * trades a little type safety for a fixed node size, which is what makes
 * the arena a flat array rather than a heap of variably-sized things.
 *
 * Variable-length children (a block's statements, a call's arguments) do
 * not fit in four slots, so they live in a separate flat pool: a node
 * stores the offset of its first child and how many there are.
 */

#ifndef SMARSH_AST_H
#define SMARSH_AST_H

#include <stdint.h>

#define SP_MAX_NODES 4096u
#define SP_MAX_CHILDREN 8192u
#define SP_MAX_STRING_POOL 32768u
#define SP_NO_NODE 0xFFFFu

typedef uint16_t sp_node_id_t;
typedef uint16_t sp_str_t;      /* offset into the string pool */

/*
 * Every `type:` the JS parser emits. Kinds the C parser does not yet
 * build are still listed, so that adding one is filling in a case rather
 * than changing the type -- and so a reader can see exactly how much of
 * the language is not here yet.
 */
typedef enum {
  SP_PROGRAM = 0,

  /* --- literals and names -------------------------------------- */
  SP_NUM,          /* text = digits                                   */
  SP_DEC_LIT,      /* text = digits, exact decimal                    */
  SP_STR,          /* text = contents                                 */
  SP_BOOL,         /* a = 1 for true, 0 for false                     */
  SP_NIL,
  SP_IDENT,        /* text = name                                     */
  SP_TEMPLATE,     /* children = alternating literal/expr             */
  SP_LIST_LIT,     /* children = elements                             */
  SP_MAP_LIT,      /* children = alternating key/value                */
  SP_TENSOR_LIT,   /* children = elements                             */

  /* --- expressions ---------------------------------------------- */
  SP_UNARY,        /* text = op, a = operand                          */
  SP_BINARY,       /* text = op, a = left, b = right                  */
  SP_LOGICAL,      /* text = and/or, a = left, b = right              */
  SP_CALL,         /* a = callee, children = arguments                */
  SP_INDEX,        /* a = object, children = indices                  */
  SP_MEMBER,       /* a = object, text = field name                   */
  SP_ASSIGN,       /* a = target, b = value                           */

  /* --- statements ------------------------------------------------ */
  SP_BLOCK,        /* children = statements                           */
  SP_EXPR_STMT,    /* a = expression                                  */
  SP_DECLARE,      /* text = name, a = value, b = 1 if mutable (var)  */
  SP_IF,           /* a = test, b = then-block, c = else (or none)    */
  SP_WHILE,        /* a = test, b = body                              */
  SP_FOR,          /* text = binding, a = iterable, b = body          */
  SP_RETURN,       /* a = value, or SP_NO_NODE                        */
  SP_BREAK,
  SP_CONTINUE,

  /* --- declarations ---------------------------------------------- */
  SP_FN,           /* an anonymous fn expression                      */
  SP_FN_DECL,      /* text = name, a = body, children = params        */
  SP_RECORD_DECL,
  SP_CHOICE_DECL,
  SP_AGENT_DECL,
  SP_IMPORT,
  SP_REDEFINE,
  SP_USING,

  /* --- the parts that make this language what it is --------------- */
  SP_ATTEMPT,      /* a = body, b = rescue block, text = rescue name  */
  SP_MAYBE,        /* a = probability, b = body                       */
  SP_CHOOSE,
  SP_FORK,
  SP_MATCH,
  SP_GROUNDED,     /* a = body                                        */
  SP_REGION,       /* text = region, a = body                         */
  SP_SECRET,
  SP_ATOMIC,
  SP_BUDGET,
  SP_AUTHORITY,
  SP_DEVICE,
  SP_RELEASE_TO,   /* text = principal, a = body                      */
  SP_VOUCHED_BY,
  SP_SPAWN,

  SP_KIND_COUNT
} sp_kind_t;

typedef enum {
  SP_OK = 0,
  SP_ERR_ARENA_FULL = 1,
  SP_ERR_CHILDREN_FULL = 2,
  SP_ERR_STRINGS_FULL = 3,
  SP_ERR_UNEXPECTED_TOKEN = 4,
  SP_ERR_DEPTH_EXCEEDED = 5,
  SP_ERR_UNSUPPORTED = 6,   /* a construct this port does not build yet */
  SP_ERR_NULL_ARGUMENT = 7,
  /* A child appended to a node whose run is no longer at the end of the
     pool. This used to be reported as SP_ERR_CHILDREN_FULL, which was
     false -- the pool was nowhere near full -- and it hid a real parser
     bug for as long as the parser was only ever checked through a Python
     imitation that had no pool. */
  SP_ERR_NONCONTIGUOUS = 8
} sp_status_t;

typedef struct {
  sp_kind_t kind;
  unsigned line;
  sp_node_id_t a;
  sp_node_id_t b;
  sp_node_id_t c;
  sp_node_id_t d;
  sp_str_t text;
  /* Variable-length children live in the arena's flat pool. */
  uint16_t first_child;
  uint16_t child_count;
} sp_node_t;

typedef struct {
  sp_node_t nodes[SP_MAX_NODES];
  unsigned node_count;

  sp_node_id_t children[SP_MAX_CHILDREN];
  unsigned child_count;

  char strings[SP_MAX_STRING_POOL];
  unsigned strings_used;

  sp_node_id_t root;

  /* Children waiting to be committed. A list pushes each child here as it
     finishes parsing it; a nested list commits and pops its own segment
     before the outer list pushes its next child, so the outer segment is
     always contiguous at the top and commits in one run. Kept in the
     arena, which the caller owns, rather than on the call stack. */
  sp_node_id_t pending[SP_MAX_CHILDREN];
  unsigned pending_top;
} sp_arena_t;

/* ---- building ------------------------------------------------------ */

void sp_arena_init(sp_arena_t *arena);

/* Allocate a node. Every slot is set to SP_NO_NODE and text to 0, so a
   kind that does not use a slot never reads a stale index from a
   previous parse that reused the arena. */
sp_status_t sp_new_node(sp_arena_t *arena, sp_kind_t kind, unsigned line,
                        sp_node_id_t *out_id);

/* Intern a string. Returns a handle valid for the arena's lifetime.
   Deliberately does NOT deduplicate: dedup costs a scan per intern and
   buys nothing here, since the pool is bounded and a parse is short. */
sp_status_t sp_intern(sp_arena_t *arena, const char *text, sp_str_t *out);

const char *sp_string(const sp_arena_t *arena, sp_str_t handle);

/* Append to a node's child list. Children of one node must be appended
   consecutively -- the pool is flat and a node owns a contiguous run, so
   interleaving two nodes' children would corrupt both. Checked: appending
   to a node that is not the most recent one to have children returns
   SP_ERR_CHILDREN_FULL rather than silently splicing. */
sp_status_t sp_add_child(sp_arena_t *arena, sp_node_id_t parent,
                         sp_node_id_t child);

sp_node_t *sp_node(sp_arena_t *arena, sp_node_id_t id);
const sp_node_t *sp_node_const(const sp_arena_t *arena, sp_node_id_t id);
sp_node_id_t sp_child(const sp_arena_t *arena, sp_node_id_t parent, unsigned i);

/* ---- building a list of children without interleaving -------------- */
/* The height of the pending stack, to hand back to sp_commit_children. */
unsigned sp_pending_mark(const sp_arena_t *arena);
sp_status_t sp_pending_push(sp_arena_t *arena, sp_node_id_t child);
/* Give `parent` everything pushed since `mark`, as one contiguous run, and
   pop it. `parent` must have no children yet. */
sp_status_t sp_commit_children(sp_arena_t *arena, sp_node_id_t parent,
                               unsigned mark);

#endif /* SMARSH_AST_H */
