/*
 * smarsh_read.c -- see smarsh_read.h.
 *
 * Kinds are not chained. "dog" read where "cat" was read makes the two alike, and
 * "cat" read where "it" was read makes those two alike, but that does not make
 * "dog" like "it": each step is a guess, and a guess on a guess on a guess soon
 * joins every word into one kind and rules nothing out. So an order hypothesis
 * "B never follows A" is ruled out only by a sentence that had A B, or by one step
 * of likeness: A was read where A' was read, and A' B was read (or the same for B).
 */
#include "smarsh_read.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* ---- words ------------------------------------------------------------------- */

static unsigned WHASH[RD_MAX_WORDS * 2u];   /* open addressing, 0 = empty, else id + 1 */

static unsigned long hash_str(const char *s, unsigned n) {
  unsigned long h = 5381ul;
  unsigned i;
  for (i = 0u; i < n; i++) h = h * 33ul + (unsigned char)s[i];
  return h;
}

/* the id of a word, made if `make` and it is new; RD_MAX_WORDS if unknown */
static unsigned word_id(rd_reader_t *r, const char *s, unsigned n, int make) {
  unsigned long h = hash_str(s, n);
  unsigned cap = RD_MAX_WORDS * 2u, i, slot = 0u;
  for (i = 0u; i < cap; i++) {
    slot = (unsigned)((h + i) % cap);
    if (WHASH[slot] == 0u) break;
    {
      unsigned id = WHASH[slot] - 1u;
      if (strlen(r->word[id]) == n && memcmp(r->word[id], s, n) == 0) return id;
    }
  }
  if (!make || i == cap || r->n_words >= RD_MAX_WORDS) return RD_MAX_WORDS;
  {
    unsigned id = r->n_words++;
    r->word[id] = (char *)malloc(n + 1u);
    memcpy(r->word[id], s, n);
    r->word[id][n] = '\0';
    WHASH[slot] = id + 1u;
    return id;
  }
}

/* ---- sets of pairs: which A B it has read, and which word stood in which place -- */

typedef struct {
  uint64_t *key;
  unsigned cap, n;
} pairset_t;

static uint64_t mix(uint64_t k) {
  k ^= k >> 33;
  k *= 0xff51afd7ed558ccdull;
  k ^= k >> 33;
  return k;
}

static void ps_init(pairset_t *p, unsigned cap) {
  p->cap = cap;
  p->n = 0u;
  p->key = (uint64_t *)calloc(cap, sizeof(uint64_t));
}

/* 1 if it was new */
static int ps_add(pairset_t *p, uint64_t k) {
  unsigned s = (unsigned)(mix(k + 1u) & (p->cap - 1u));
  k += 1u;   /* 0 means empty */
  while (p->key[s] != 0u) {
    if (p->key[s] == k) return 0;
    s = (s + 1u) & (p->cap - 1u);
  }
  if (p->n * 2u >= p->cap) return 0;   /* full: it stops noting new pairs rather than grow */
  p->key[s] = k;
  p->n++;
  return 1;
}

static int ps_has(const pairset_t *p, uint64_t k) {
  unsigned s = (unsigned)(mix(k + 1u) & (p->cap - 1u));
  k += 1u;
  while (p->key[s] != 0u) {
    if (p->key[s] == k) return 1;
    s = (s + 1u) & (p->cap - 1u);
  }
  return 0;
}

static pairset_t FOLLOWS;   /* (a << 32) | b : it has read a then b */
static pairset_t STOOD;     /* (place << 32) | w : word w stood in that place */

/* places: (word before, word after), numbered as met */
static pairset_t PLACE_OF;  /* key (l << 32 | r) -> number stored alongside */
static uint64_t *PLACE_KEY;
static unsigned N_PLACES;

/* for each word, the places it stood in; for each place, the words that stood there */
static unsigned *W_START, *W_PLACES, *P_START, *P_WORDS;

void rd_begin(rd_reader_t *r, int use_kinds) {
  memset(r, 0, sizeof *r);
  memset(WHASH, 0, sizeof WHASH);
  r->use_kinds = use_kinds;
  (void)word_id(r, "<", 1u, 1);   /* RD_BEGIN */
  (void)word_id(r, ">", 1u, 1);   /* RD_END */
  ps_init(&FOLLOWS, 1u << 22);
  ps_init(&STOOD, 1u << 22);
  ps_init(&PLACE_OF, 1u << 21);
  PLACE_KEY = (uint64_t *)calloc(1u << 21, sizeof(uint64_t));
  N_PLACES = 0u;
  W_START = W_PLACES = P_START = P_WORDS = 0;
}

void rd_free(rd_reader_t *r) {
  unsigned i;
  for (i = 0u; i < r->n_words; i++) free(r->word[i]);
  free(FOLLOWS.key);
  free(STOOD.key);
  free(PLACE_OF.key);
  free(PLACE_KEY);
  free(W_START);
  free(W_PLACES);
  free(P_START);
  free(P_WORDS);
}

/* ---- sentences --------------------------------------------------------------- */

static int word_char(int c) {
  return isalnum(c) || c == '\'' || c >= 128;
}

void rd_sentences(rd_reader_t *r, const char *story, int learn_words, rd_each_fn each, void *ctx) {
  rd_sentence_t s;
  const char *p = story;
  s.n = 0u;
  s.unknown = 0;
  s.tok[s.n++] = RD_BEGIN;
  while (*p != '\0' && *p != '\n') {
    char buf[64];
    unsigned n = 0u, id;
    int ends = 0;
    if (isspace((unsigned char)*p)) {
      p++;
      continue;
    }
    if (word_char((unsigned char)*p)) {
      while (*p != '\0' && word_char((unsigned char)*p)) {
        if (n < sizeof buf) buf[n++] = (char)tolower((unsigned char)*p);
        p++;
      }
    } else {
      buf[n++] = *p;
      ends = (*p == '.' || *p == '!' || *p == '?');
      p++;
    }
    id = word_id(r, buf, n, learn_words);
    if (id == RD_MAX_WORDS && s.unknown == 0) s.unknown = 1;
    if (s.n < RD_MAX_TOKENS + 1u) s.tok[s.n++] = id;
    else s.unknown = 2;   /* too long: set aside */
    if (ends) {
      if (s.unknown != 2 && s.n > 2u) {
        s.tok[s.n++] = RD_END;
        each(r, &s, ctx);
      }
      s.n = 0u;
      s.unknown = 0;
      s.tok[s.n++] = RD_BEGIN;
    }
  }
}

/* ---- reading: where each word stood ------------------------------------------- */

static unsigned place_number(uint64_t key) {
  /* PLACE_OF maps a place to its number: stored as (number) in a parallel table */
  unsigned cap = PLACE_OF.cap, s = (unsigned)(mix(key + 7u) & (cap - 1u));
  while (PLACE_OF.key[s] != 0u) {
    if (PLACE_KEY[s] == key) return (unsigned)(PLACE_OF.key[s] - 1u);
    s = (s + 1u) & (cap - 1u);
  }
  if (N_PLACES * 2u >= cap) return 0xffffffffu;
  PLACE_OF.key[s] = (uint64_t)N_PLACES + 1u;
  PLACE_KEY[s] = key;
  return N_PLACES++;
}

void rd_read_places(rd_reader_t *r, const rd_sentence_t *s) {
  unsigned i;
  if (!r->use_kinds || s->unknown) return;
  for (i = 1u; i + 1u < s->n; i++) {
    unsigned pl = place_number(((uint64_t)s->tok[i - 1u] << 32) | s->tok[i + 1u]);
    if (pl != 0xffffffffu) (void)ps_add(&STOOD, ((uint64_t)pl << 32) | s->tok[i]);
  }
}

/* the two indexes, word -> places and place -> words, from what it noted */
void rd_kinds_settle(rd_reader_t *r) {
  unsigned i, *wfill, *pfill;
  W_START = (unsigned *)calloc(r->n_words + 1u, sizeof(unsigned));
  P_START = (unsigned *)calloc(N_PLACES + 1u, sizeof(unsigned));
  for (i = 0u; i < STOOD.cap; i++) {
    uint64_t k;
    if (STOOD.key[i] == 0u) continue;
    k = STOOD.key[i] - 1u;
    W_START[(unsigned)(k & 0xffffffffu) + 1u]++;
    P_START[(unsigned)(k >> 32) + 1u]++;
  }
  for (i = 0u; i < r->n_words; i++) W_START[i + 1u] += W_START[i];
  for (i = 0u; i < N_PLACES; i++) P_START[i + 1u] += P_START[i];
  W_PLACES = (unsigned *)malloc(sizeof(unsigned) * (STOOD.n + 1u));
  P_WORDS = (unsigned *)malloc(sizeof(unsigned) * (STOOD.n + 1u));
  wfill = (unsigned *)calloc(r->n_words, sizeof(unsigned));
  pfill = (unsigned *)calloc(N_PLACES + 1u, sizeof(unsigned));
  for (i = 0u; i < STOOD.cap; i++) {
    uint64_t k;
    unsigned w, pl;
    if (STOOD.key[i] == 0u) continue;
    k = STOOD.key[i] - 1u;
    w = (unsigned)(k & 0xffffffffu);
    pl = (unsigned)(k >> 32);
    W_PLACES[W_START[w] + wfill[w]++] = pl;
    P_WORDS[P_START[pl] + pfill[pl]++] = w;
  }
  free(wfill);
  free(pfill);
  r->n_kinds = N_PLACES;
  r->standing = 0u;
}

/* ---- reading: what followed what ---------------------------------------------- */

void rd_read_order(rd_reader_t *r, const rd_sentence_t *s) {
  unsigned i;
  if (s->unknown) return;
  for (i = 0u; i + 1u < s->n; i++) {
    if (ps_add(&FOLLOWS, ((uint64_t)s->tok[i] << 32) | s->tok[i + 1u])) r->standing++;
  }
}

#ifndef RD_SHARE
#define RD_SHARE 2u   /* places two words must both have stood in before they are held alike */
#endif

/* the words held alike to w: they stood in at least RD_SHARE of the same places */
static unsigned LIKE_N[RD_MAX_WORDS];
static unsigned alike(unsigned w, unsigned *out, unsigned cap) {
  static unsigned touched[RD_MAX_WORDS];
  unsigned n_t = 0u, i, j, n = 0u;
  for (i = W_START[w]; i < W_START[w + 1u]; i++) {
    unsigned pl = W_PLACES[i];
    for (j = P_START[pl]; j < P_START[pl + 1u]; j++) {
      unsigned o = P_WORDS[j];
      if (o == w) continue;
      if (LIKE_N[o]++ == 0u) touched[n_t++] = o;
    }
  }
  for (i = 0u; i < n_t; i++) {
    if (LIKE_N[touched[i]] >= RD_SHARE && n < cap) out[n++] = touched[i];
    LIKE_N[touched[i]] = 0u;
  }
  return n;
}

/* has "b never follows a" been ruled out, by reading a b or by one step of likeness? */
static int ruled_out(const rd_reader_t *r, unsigned a, unsigned b) {
  static unsigned like[RD_MAX_WORDS];
  unsigned i, n;
  if (ps_has(&FOLLOWS, ((uint64_t)a << 32) | b)) return 1;
  if (!r->use_kinds || W_START == 0) return 0;
  n = alike(a, like, RD_MAX_WORDS);   /* a word like a, that b followed */
  for (i = 0u; i < n; i++) {
    if (ps_has(&FOLLOWS, ((uint64_t)like[i] << 32) | b)) return 1;
  }
  n = alike(b, like, RD_MAX_WORDS);   /* a word like b, that followed a */
  for (i = 0u; i < n; i++) {
    if (ps_has(&FOLLOWS, ((uint64_t)a << 32) | like[i])) return 1;
  }
  return 0;
}

void rd_unsupported(const rd_reader_t *r, const rd_sentence_t *s, unsigned *direct, unsigned *by_likeness) {
  unsigned i;
  *direct = *by_likeness = 0u;
  for (i = 0u; i + 1u < s->n; i++) {
    if (!ps_has(&FOLLOWS, ((uint64_t)s->tok[i] << 32) | s->tok[i + 1u])) (*direct)++;
    if (!ruled_out(r, s->tok[i], s->tok[i + 1u])) (*by_likeness)++;
  }
}

int rd_judge(const rd_reader_t *r, const rd_sentence_t *s) {
  unsigned i;
  if (s->unknown) return -1;
  for (i = 0u; i + 1u < s->n; i++) {
    if (!ruled_out(r, s->tok[i], s->tok[i + 1u])) return 0;
  }
  return 1;
}
