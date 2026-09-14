/*
 * smarsh_analogy.h -- the same shape in a different domain, found and
 * proved, and what carries across.
 *
 * ============================================================
 * WHAT A STRUCTURE IS
 * ============================================================
 * A question that is determined by a few factors: "may the machine run"
 * by (guard closed, operator present, remote override); "is the act
 * permissible" by (consent, harm avoided, harm outweighed). Stored as the
 * complete table: the answer for every combination of factor values.
 * That is exactly what sc_discover (smarsh_concept.h) produces when it
 * finds a question determined by others, so discovered concepts can go
 * straight into the library.
 *
 * ============================================================
 * WHEN TWO ARE THE SAME
 * ============================================================
 * Two structures have the same shape when there is a renaming that turns
 * one into the other EXACTLY:
 *   - a one-to-one pairing of their factors (sizes must agree),
 *   - a relabeling of each factor's values,
 *   - a relabeling of the answer,
 * such that every entry of one table lands on the matching entry of the
 * other. That renaming is the analogy, and it is a proof: it is checked
 * on every combination, so nothing true on one side can fail to be true
 * of the image on the other.
 *
 * ============================================================
 * SEARCH, THEN FILTER, THEN PROOF
 * ============================================================
 * Exact matching is expensive in general, so every stored structure is
 * first compared on INVARIANTS: numbers that any renaming preserves.
 * Factor sizes (sorted), how often each answer occurs (sorted), and how
 * many combinations each factor can swing on its own (sorted). Different
 * invariants mean no renaming can exist, so the pair is dropped without
 * searching. Only survivors get the exact search. Both counts are
 * reported, so the filter's work is visible, not assumed.
 *
 * ============================================================
 * WHAT CARRIES ACROSS
 * ============================================================
 * Properties stated in terms of factors: "without consent, nothing else
 * matters" (a factor value that forces the answer), "this factor never
 * matters", "these two factors play interchangeable roles". Found on the
 * stored structure, translated through the renaming into the new
 * problem's own names, and then RE-CHECKED on the new problem directly,
 * because a transfer that is not re-checked is a claim, not a result.
 *
 * Nothing is learned statistically. One stored encounter is enough.
 */

#ifndef SMARSH_ANALOGY_H
#define SMARSH_ANALOGY_H

#include "smarsh_reason.h"

#define SA_MAX_FACTORS 5u
#define SA_MAX_VALUES 4u
#define SA_MAX_ENTRIES 1024u       /* 4^5 */
#define SA_MAX_LIBRARY 64u
#define SA_NAME 32u

typedef struct {
  char name[SA_NAME];              /* what the question is */
  char domain[SA_NAME];            /* where it was met */
  unsigned n_factors;
  char factor[SA_MAX_FACTORS][SA_NAME];
  unsigned dom[SA_MAX_FACTORS];    /* values each factor can take, 2..4 */
  unsigned dom_out;
  /* answer for each combination, mixed radix with factor 0 most
     significant */
  uint8_t table[SA_MAX_ENTRIES];
} sa_struct_t;

/* The renaming that proves two structures the same. */
typedef struct {
  unsigned factor_to[SA_MAX_FACTORS];              /* this factor i is that factor_to[i] */
  uint8_t value_to[SA_MAX_FACTORS][SA_MAX_VALUES]; /* and its value v is value_to[i][v] */
  uint8_t out_to[SA_MAX_VALUES];                   /* this answer a is that out_to[a] */
} sa_map_t;

typedef struct {
  unsigned n;
  sa_struct_t s[SA_MAX_LIBRARY];
} sa_library_t;

typedef struct {
  unsigned n_matches;
  unsigned match[SA_MAX_LIBRARY];      /* library indices, in library order */
  sa_map_t map[SA_MAX_LIBRARY];        /* query -> library structure */
  unsigned filtered;                   /* dropped by invariants, no search */
  unsigned searched_no_match;          /* same invariants, no renaming exists */
} sa_found_t;

/* A property of one structure, in terms of its factors. */
typedef enum {
  SA_FORCES = 0,        /* factor = value forces the answer to `answer` */
  SA_IRRELEVANT = 1,    /* the factor never changes the answer */
  SA_INTERCHANGEABLE = 2 /* swapping factor and other never changes it */
} sa_prop_kind_t;

typedef struct {
  sa_prop_kind_t kind;
  unsigned factor;
  unsigned value;       /* SA_FORCES */
  unsigned answer;      /* SA_FORCES */
  unsigned other;       /* SA_INTERCHANGEABLE */
} sa_prop_t;

#define SA_MAX_PROPS 64u

/* Build a structure from a function over the factors; checks sizes. */
sm_status_t sa_define(sa_struct_t *s, const char *name, const char *domain,
                      unsigned n_factors, const char *const *factor_names,
                      const unsigned *doms, unsigned dom_out,
                      unsigned (*answer)(const unsigned *values));

unsigned sa_entries(const sa_struct_t *s);
unsigned sa_at(const sa_struct_t *s, const unsigned *values);

sm_status_t sa_add(sa_library_t *lib, const sa_struct_t *s);

/* 1 if a and b have equal invariants (a necessary condition to match). */
int sa_same_invariants(const sa_struct_t *a, const sa_struct_t *b);

/* Exact search for a renaming a -> b. 1 and *map when one exists. */
int sa_match(const sa_struct_t *a, const sa_struct_t *b, sa_map_t *map);

/* Re-check a renaming on every combination. Independent of sa_match. */
int sa_check_map(const sa_struct_t *a, const sa_struct_t *b, const sa_map_t *map);

/* Search the whole library for structures shaped like q. */
sm_status_t sa_find(const sa_library_t *lib, const sa_struct_t *q, sa_found_t *out);

/* Every property of s of the three kinds above. */
unsigned sa_properties(const sa_struct_t *s, sa_prop_t *props, unsigned max);

/* Is this property true of s? Checked directly on the table. */
int sa_holds(const sa_struct_t *s, const sa_prop_t *p);

/*
 * Carry a property of library structure b back to query a through the
 * renaming a -> b. Returns 1 and the translated property; the caller can
 * (and the demo does) re-check it with sa_holds on a.
 */
int sa_transfer(const sa_map_t *map_a_to_b, const sa_struct_t *a,
                const sa_struct_t *b, const sa_prop_t *on_b, sa_prop_t *on_a);

#endif /* SMARSH_ANALOGY_H */
