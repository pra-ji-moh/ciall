/*
 * read_books.c -- the child reads stories and is tested on stories it has never read.
 *
 *     read_books books/tinystories.txt
 *
 * The last 200 stories are never read. From each of their sentences it is shown
 * the sentence as written and the same sentence with two neighbouring words
 * swapped, and says of each whether it is its language. It is told nothing about
 * which is which. Done after reading 100, 400, 1000 and 1800 stories, and once
 * more with kinds of word switched off, so what the kinds add can be seen.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smarsh_read.h"

#define HELD_OUT 200u

static char **STORY;
static unsigned N_STORIES;

static void load(const char *path) {
  FILE *f = fopen(path, "rb");
  long size;
  char *all, *p;
  if (f == 0) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(1);
  }
  fseek(f, 0, SEEK_END);
  size = ftell(f);
  fseek(f, 0, SEEK_SET);
  all = (char *)malloc((size_t)size + 1u);
  if (fread(all, 1u, (size_t)size, f) != (size_t)size) exit(1);
  all[size] = '\0';
  fclose(f);
  STORY = (char **)malloc(sizeof(char *) * 200000u);
  for (p = all; *p != '\0' && N_STORIES < 200000u;) {
    STORY[N_STORIES++] = p;
    p = strchr(p, '\n');
    if (p == 0) break;
    *p++ = '\0';
  }
}

static void on_place(rd_reader_t *r, const rd_sentence_t *s, void *ctx) {
  (void)ctx;
  rd_read_places(r, s);
}

static void on_order(rd_reader_t *r, const rd_sentence_t *s, void *ctx) {
  (void)ctx;
  rd_read_order(r, s);
}

typedef struct {
  unsigned sentences, unknown, real_ok, swap_no, both, chose_real, chose_swap, undecided;
} tally_t;

static void on_test(rd_reader_t *r, const rd_sentence_t *s, void *ctx) {
  tally_t *t = (tally_t *)ctx;
  rd_sentence_t w = *s;
  unsigned i, mid;
  int a, b;
  t->sentences++;
  a = rd_judge(r, s);
  if (a < 0) {
    t->unknown++;
    return;
  }
  /* two neighbouring words in the middle, swapped (they must differ) */
  mid = 1u + (s->n - 2u) / 2u;
  for (i = mid; i + 2u < s->n && s->tok[i] == s->tok[i + 1u]; i++) {
  }
  if (i + 2u >= s->n) {
    t->sentences--;
    return;
  }
  w.tok[i] = s->tok[i + 1u];
  w.tok[i + 1u] = s->tok[i];
  b = rd_judge(r, &w);
  if (a == 1) t->real_ok++;
  if (b == 0) t->swap_no++;
  if (a == 1 && b == 0) t->both++;
  {
    /* both shown at once: the one with fewer pairs nothing it read supports is its language;
       counted first without likeness, and likeness only breaks a tie */
    unsigned rd, rl, wd, wl;
    rd_unsupported(r, s, &rd, &rl);
    rd_unsupported(r, &w, &wd, &wl);
    if (rd < wd || (rd == wd && rl < wl)) t->chose_real++;
    else if (rd > wd || (rd == wd && rl > wl)) t->chose_swap++;
    else t->undecided++;
  }
}

static void run(unsigned stories, int use_kinds) {
  static rd_reader_t r;
  tally_t t;
  unsigned i;
  unsigned first_test = N_STORIES - HELD_OUT;
  if (stories > first_test) stories = first_test;
  rd_begin(&r, use_kinds);
  for (i = 0u; i < stories; i++) rd_sentences(&r, STORY[i], 1, on_place, 0);
  rd_kinds_settle(&r);
  for (i = 0u; i < stories; i++) rd_sentences(&r, STORY[i], 0, on_order, 0);
  memset(&t, 0, sizeof t);
  for (i = first_test; i < N_STORIES; i++) rd_sentences(&r, STORY[i], 0, on_test, &t);
  {
    unsigned judged = t.sentences - t.unknown;
    printf("  %s read %4u stories: %5u words, %6u places words stood in, %6u word pairs read\n",
           use_kinds ? "likeness on " : "likeness off", stories, r.n_words, r.n_kinds, r.standing);
    printf("      never-read sentences it could judge: %u of %u (the rest hold a word it has never met)\n",
           judged, t.sentences);
    if (judged > 0u) {
      printf("      real sentences it said were its language:      %5.1f%%\n", 100.0 * t.real_ok / judged);
      printf("      swapped sentences it said were not:            %5.1f%%\n", 100.0 * t.swap_no / judged);
      printf("      told the real one from the swapped one:        %5.1f%%\n", 100.0 * t.both / judged);
      printf("      SHOWN BOTH, picked the real one: %5.1f%%   picked the swapped one: %4.1f%%   could not tell: %4.1f%%\n",
             100.0 * t.chose_real / judged, 100.0 * t.chose_swap / judged, 100.0 * t.undecided / judged);
    }
  }
  rd_free(&r);
}

int main(int argc, char **argv) {
  static const unsigned steps[4] = {100u, 400u, 1000u, 0xffffffffu};   /* the last: all it has */
  unsigned k;
  load(argc > 1 ? argv[1] : "books/tinystories.txt");
  printf("%u stories; the last %u are never read, and it is tested on them\n\n", N_STORIES, HELD_OUT);
  for (k = 0u; k < 4u; k++) run(steps[k], 1);
  printf("\n");
  run(steps[3], 0);
  return 0;
}
