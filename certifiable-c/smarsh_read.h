/*
 * smarsh_read.h -- the child reading, by elimination.
 *
 * It learns the shape of a language the way it learns what ends a level: it
 * holds hypotheses and lets what it reads rule them out. Nothing is counted and
 * nothing is weighed. No word is ever "likelier" than another.
 *
 * Two families of hypothesis:
 *
 *   KINDS.  At the start every word is its own kind: "dog" and "cat" are held to
 *   be different kinds of word. When the two are read in exactly the same place --
 *   the same word before and the same word after, "the dog ran", "the cat ran" --
 *   that hypothesis is contradicted, and they are one kind from then on.
 *
 *   ORDER.  For every two kinds A and B it holds "B never comes straight after A".
 *   Every sentence it reads rules out each such hypothesis the sentence breaks.
 *   What is still standing is its grammar: the tightest one consistent with
 *   everything it has read.
 *
 * Its Hartley measure is the number of order hypotheses still standing: each is
 * one bit it has not yet had settled by what it read.
 *
 * It then judges sentences it has never read: one that breaks no hypothesis still
 * standing is one it can say is its language; one that breaks any is not. A word
 * it has never met is a thing it cannot judge, and it says so.
 */
#ifndef SMARSH_READ_H
#define SMARSH_READ_H

#include <stdint.h>

#define RD_MAX_WORDS 24000u
#define RD_MAX_TOKENS 64u        /* a sentence longer than this is set aside */
#define RD_FRAMES (1u << 20)     /* places a word has been read in: (word before, word after) */
#define RD_BEGIN 0u              /* the edge before a sentence */
#define RD_END 1u                /* the edge after it */

typedef struct {
  unsigned n_words;
  char *word[RD_MAX_WORDS];
  unsigned parent[RD_MAX_WORDS];    /* kinds: which word each word was found to be the same kind as */
  unsigned n_kinds;
  unsigned kind[RD_MAX_WORDS];      /* compact kind number, after kinds_settle */
  uint8_t *follows;                 /* n_kinds x n_kinds: 1 once a sentence ruled out "never follows" */
  unsigned standing;                /* order hypotheses still standing (its Hartley measure, in bits) */
  unsigned merged;                  /* "different kinds" hypotheses ruled out */
  int use_kinds;                    /* 0: every word stays its own kind (for comparison) */
} rd_reader_t;

typedef struct {
  unsigned n;
  unsigned tok[RD_MAX_TOKENS + 2u];   /* with the two edges */
  int unknown;                        /* holds a word it has never met */
} rd_sentence_t;

void rd_begin(rd_reader_t *r, int use_kinds);
void rd_free(rd_reader_t *r);

/* one story: split into sentences, each passed to `each` */
typedef void (*rd_each_fn)(rd_reader_t *r, const rd_sentence_t *s, void *ctx);
void rd_sentences(rd_reader_t *r, const char *story, int learn_words, rd_each_fn each, void *ctx);

/* reading, in two passes: first where words stand (kinds), then what follows what */
void rd_read_places(rd_reader_t *r, const rd_sentence_t *s);
void rd_kinds_settle(rd_reader_t *r);
void rd_read_order(rd_reader_t *r, const rd_sentence_t *s);

/* 1: breaks no hypothesis still standing; 0: breaks one; -1: holds a word it has never met */
int rd_judge(const rd_reader_t *r, const rd_sentence_t *s);

/* pairs in a sentence that nothing it read supports: read directly, and allowing one step of likeness */
void rd_unsupported(const rd_reader_t *r, const rd_sentence_t *s, unsigned *direct, unsigned *by_likeness);

#endif
