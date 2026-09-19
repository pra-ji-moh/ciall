/*
 * grasp_books.c -- the child learning to understand stories, by elimination.
 *
 *     grasp_books books/babi_train.txt books/babi_test.txt
 *
 * The stories are Facebook's twenty bAbI tasks (from Hugging Face): a few short
 * sentences, then a question. "Mary moved to the bathroom. John went to the
 * hallway. Where is Mary?" -- "bathroom".
 *
 * It is told no word's meaning and no rule. It holds every rule for answering
 * that its language can say:
 *
 *   one step:  find the most recent (or the first) sentence holding the question's
 *              k-th word (and, if the rule says so, its j-th word too), and answer
 *              with that sentence's word at place p;
 *   two steps: the same, and then take that answer as the word to look for, and
 *              do it again;
 *   and either: say the word found, or say "yes" if it is the question's i-th
 *              word and "no" if it is not.
 *
 * Each story it is shown the answer to rules out every rule that would have
 * answered it differently, or not at all. What is left is what it has understood.
 * It starts with one-step rules only; when every one of those has been ruled out,
 * its language was too poor, and it widens it itself to two steps, replaying
 * every story it has seen against the new rules, as it does with what ends a level.
 *
 * Then it answers 1000 stories per task it has never seen. Where the rules still
 * standing agree, it says the answer; where they do not, it chooses uniformly
 * among them. Its Hartley measure is log2 of the rules still standing.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WORDS 8192u
#define MAX_TOK 16u
#define MAX_Q 12u
#define MAX_SENT 400u
#define N_POS 8
static const int POS[N_POS] = {0, 1, 2, 3, -1, -2, -3, -4};

/* ---- words ------------------------------------------------------------------ */

static char *WORD[MAX_WORDS];
static unsigned N_WORDS;

static unsigned word(const char *s, unsigned n) {
  unsigned i;
  for (i = 0u; i < N_WORDS; i++) {
    if (strlen(WORD[i]) == n && memcmp(WORD[i], s, n) == 0) return i;
  }
  if (N_WORDS >= MAX_WORDS) return 0u;
  WORD[N_WORDS] = (char *)malloc(n + 1u);
  memcpy(WORD[N_WORDS], s, n);
  WORD[N_WORDS][n] = '\0';
  return N_WORDS++;
}

/* ---- stories ---------------------------------------------------------------- */

typedef struct {
  unsigned n;
  unsigned short t[MAX_TOK];
} line_t;

typedef struct {
  int task;
  unsigned n_sent;
  line_t *sent;
  line_t q;
  unsigned answer;   /* the whole answer as one word, e.g. "south east" */
} story_t;

static void split(const char *s, line_t *l) {
  l->n = 0u;
  while (*s != '\0' && *s != '\n' && *s != '\r') {
    const char *e = s;
    while (*e != '\0' && *e != ' ' && *e != '\n' && *e != '\r') e++;
    if (e > s && l->n < MAX_TOK) l->t[l->n++] = (unsigned short)word(s, (unsigned)(e - s));
    s = (*e == ' ') ? e + 1 : e;
  }
}

static story_t *load(const char *path, unsigned *count) {
  FILE *f = fopen(path, "r");
  char buf[1024];
  story_t *st = (story_t *)calloc(21000u, sizeof(story_t)), *cur = 0;
  static line_t tmp[MAX_SENT];
  *count = 0u;
  if (f == 0) {
    fprintf(stderr, "cannot open %s\n", path);
    exit(1);
  }
  while (fgets(buf, sizeof buf, f) != 0) {
    if (buf[0] == 'T') {
      cur = &st[(*count)++];
      cur->task = atoi(buf + 2);
      cur->n_sent = 0u;
    } else if (buf[0] == 'S' && cur != 0 && cur->n_sent < MAX_SENT) {
      split(buf + 2, &tmp[cur->n_sent++]);
    } else if (buf[0] == 'Q' && cur != 0) {
      split(buf + 2, &cur->q);
      cur->sent = (line_t *)malloc(sizeof(line_t) * (cur->n_sent + 1u));
      memcpy(cur->sent, tmp, sizeof(line_t) * cur->n_sent);
    } else if (buf[0] == 'A' && cur != 0) {
      size_t n = strcspn(buf + 2, "\r\n");
      cur->answer = word(buf + 2, (unsigned)n);
    }
  }
  fclose(f);
  return st;
}

/* ---- rules for answering ---------------------------------------------------- */

typedef struct {
  signed char key;    /* question word to look for; -1: what the step before found */
  signed char also;   /* a second question word the sentence must hold; -1: none */
  unsigned char dir;  /* 0: the most recent such sentence; 1: the first */
  unsigned char pos;  /* index into POS */
} step_t;

typedef struct {
  unsigned char depth;
  step_t step[2];
  signed char yes;    /* -1: say the word; else "yes" if it is question word `yes`, "no" if not */
  unsigned idx;       /* where its ruled-out words are kept (see STILL) */
} rule_t;

/*
 * Which words change things. For each rule and each step, every word starts as one
 * that might ("moved" might be why Mary is where she is; so might "the"). A
 * sentence about Mary that had to be passed over to reach the right answer is one
 * that did not change where Mary is, so every word in it is ruled out as a word
 * that changes things -- for that rule. A sentence all of whose words are ruled out
 * is passed over from then on. Nothing is counted: a word is ruled out or it is not.
 */
#define WB 8u                            /* 64-bit words of bitset: up to 512 words */
static unsigned long long *STILL;        /* per rule, per step: bit set = ruled out */
static unsigned N_STILL, CAP_STILL;
static unsigned long long SCRATCH[2][WB];

static unsigned YES, NO;
#define NONE 0xffffffffu

static int holds(const line_t *l, unsigned w) {
  unsigned i;
  for (i = 0u; i < l->n; i++) {
    if (l->t[i] == w) return 1;
  }
  return 0;
}

static int all_out(const line_t *l, const unsigned long long *out) {
  unsigned i;
  for (i = 0u; i < l->n; i++) {
    unsigned w = l->t[i];
    if (w >= WB * 64u || !((out[w >> 6] >> (w & 63u)) & 1ull)) return 0;
  }
  return 1;
}

static void rule_out(const line_t *l, unsigned long long *out) {
  unsigned i;
  for (i = 0u; i < l->n; i++) {
    unsigned w = l->t[i];
    if (w < WB * 64u) out[w >> 6] |= 1ull << (w & 63u);
  }
}

static unsigned do_step(const story_t *s, const step_t *st, unsigned key) {
  unsigned k, also = NONE;
  if (st->also >= 0) {
    if ((unsigned)st->also >= s->q.n) return NONE;
    also = s->q.t[(unsigned)st->also];
  }
  for (k = 0u; k < s->n_sent; k++) {
    const line_t *l = &s->sent[st->dir == 0u ? s->n_sent - 1u - k : k];
    int p = POS[st->pos];
    if (!holds(l, key) || (also != NONE && !holds(l, also))) continue;
    if (p < 0) p += (int)l->n;
    if (p < 0 || (unsigned)p >= l->n) return NONE;
    return l->t[p];
  }
  return NONE;
}

/* the next sentence at or after `from` (in the step's direction) that the step could use; its word in *got */
static int next_use(const story_t *s, const step_t *st, unsigned key, const unsigned long long *out,
                    unsigned from, unsigned *got) {
  unsigned k, also = NONE;
  if (st->also >= 0) {
    if ((unsigned)st->also >= s->q.n) return -1;
    also = s->q.t[(unsigned)st->also];
  }
  for (k = from; k < s->n_sent; k++) {
    const line_t *l = &s->sent[st->dir == 0u ? s->n_sent - 1u - k : k];
    int p = POS[st->pos];
    if (!holds(l, key) || (also != NONE && !holds(l, also))) continue;
    if (all_out(l, out)) continue;   /* nothing in it changes things: passed over */
    if (p < 0) p += (int)l->n;
    *got = (p < 0 || (unsigned)p >= l->n) ? NONE : l->t[p];
    return (int)k;
  }
  return -1;
}

static const line_t *line_at(const story_t *s, const step_t *st, unsigned k) {
  return &s->sent[st->dir == 0u ? s->n_sent - 1u - k : k];
}

/* what the rule finally says, given the word its last step reached */
static unsigned verdict(const story_t *s, const rule_t *r, unsigned w) {
  if (w == NONE) return NONE;
  if (r->yes >= 0) {
    if ((unsigned)r->yes >= s->q.n) return NONE;
    return w == s->q.t[(unsigned)r->yes] ? YES : NO;
  }
  return w;
}

/*
 * The last step, learning: it takes sentences in order, and every one that would
 * have given the wrong verdict is passed over, and its words ruled out as words
 * that change things. For "yes" and "no" this needs no knowing which sentence the
 * answer came from: a sentence that would have said "yes" when the answer was "no"
 * is one that did not change things, and that is enough.
 */
static unsigned last_step(const story_t *s, const rule_t *r, const step_t *st, unsigned key,
                          unsigned long long *out, unsigned want) {
  unsigned from = 0u, got;
  int k;
  while ((k = next_use(s, st, key, out, from, &got)) >= 0) {
    unsigned v = verdict(s, r, got);
    if (want == NONE || v == want) return v;
    rule_out(line_at(s, st, (unsigned)k), out);
    from = (unsigned)k + 1u;
  }
  return NONE;
}

/*
 * A rule, learning (want = the answer) or answering (want = NONE). With two steps,
 * when the chain does not reach the answer it retraces: the first step's sentence
 * is passed over, its words ruled out as ones that change the first thing, and the
 * next one is tried -- "who had it before that?". What the second step learned on
 * a wrong first link is not kept.
 */
static unsigned answer_l(const story_t *s, const rule_t *r, unsigned long long *out0, unsigned long long *out1, unsigned want) {
  unsigned key;
  if (r->step[0].key < 0 || (unsigned)r->step[0].key >= s->q.n) return NONE;
  key = s->q.t[(unsigned)r->step[0].key];
  if (r->depth == 1u) return last_step(s, r, &r->step[0], key, out0, want);
  {
    unsigned from = 0u, w1;
    int k;
    while ((k = next_use(s, &r->step[0], key, out0, from, &w1)) >= 0) {
      unsigned long long trial[WB];
      unsigned v;
      if (w1 == NONE) return NONE;
      memcpy(trial, out1, sizeof trial);
      v = last_step(s, r, &r->step[1], w1, trial, want);
      if (want == NONE || v == want) {
        memcpy(out1, trial, sizeof trial);
        return v;
      }
      rule_out(line_at(s, &r->step[0], (unsigned)k), out0);   /* the wrong first link */
      from = (unsigned)k + 1u;
    }
  }
  return NONE;
}

static unsigned answer(const story_t *s, const rule_t *r) {
  unsigned w, d;
  if (STILL != 0 && r->idx != NONE) {
    return answer_l(s, r, &STILL[(size_t)r->idx * 2u * WB], &STILL[((size_t)r->idx * 2u + 1u) * WB], NONE);
  }
  if (r->step[0].key < 0 || (unsigned)r->step[0].key >= s->q.n) return NONE;
  w = s->q.t[(unsigned)r->step[0].key];
  for (d = 0u; d < r->depth; d++) {
    w = do_step(s, &r->step[d], w);
    if (w == NONE) return NONE;
  }
  if (r->yes >= 0) {
    if ((unsigned)r->yes >= s->q.n) return NONE;
    return w == s->q.t[(unsigned)r->yes] ? YES : NO;
  }
  return w;
}

/* ---- learning one task by elimination --------------------------------------- */

static rule_t *ALIVE;
static unsigned N_ALIVE, CAP_ALIVE;

/* the stories of the task being learned, in the order they are read */
static story_t **LESSON;
static unsigned N_LESSON;

static int survives(const rule_t *r) {
  unsigned i;
  memset(SCRATCH, 0, sizeof SCRATCH);
  for (i = 0u; i < N_LESSON; i++) {
    unsigned got = answer_l(LESSON[i], r, SCRATCH[0], SCRATCH[1], LESSON[i]->answer);
    if (got != LESSON[i]->answer) return 0;   /* contradicted: ruled out */
  }
  return 1;
}

static void lesson(story_t *train, unsigned n_tr, int task, unsigned upto) {
  unsigned i;
  N_LESSON = 0u;
  for (i = 0u; i < n_tr && N_LESSON < upto; i++) {
    if (train[i].task == task) LESSON[N_LESSON++] = &train[i];
  }
}

static void keep(const rule_t *r) {
  if (N_ALIVE == CAP_ALIVE) {
    CAP_ALIVE = CAP_ALIVE ? CAP_ALIVE * 2u : 4096u;
    ALIVE = (rule_t *)realloc(ALIVE, sizeof(rule_t) * CAP_ALIVE);
  }
  if (N_STILL == CAP_STILL) {
    CAP_STILL = CAP_STILL ? CAP_STILL * 2u : 4096u;
    STILL = (unsigned long long *)realloc(STILL, sizeof(unsigned long long) * 2u * WB * CAP_STILL);
  }
  memcpy(&STILL[(size_t)N_STILL * 2u * WB], SCRATCH, sizeof SCRATCH);
  ALIVE[N_ALIVE] = *r;
  ALIVE[N_ALIVE].idx = N_STILL++;
  N_ALIVE++;
}

/* every rule of the given depth, set against the stories (first n of them, this task) */
static unsigned formulate(int depth, unsigned qmax) {
  rule_t r;
  int k, a, d, p, y, a2, d2, p2;
  unsigned tried = 0u;
  memset(&r, 0, sizeof r);
  r.depth = (unsigned char)depth;
  r.idx = NONE;
  for (k = 0; k < (int)qmax; k++)
    for (a = -1; a < (int)qmax; a++)
      for (d = 0; d < 2; d++)
        for (p = 0; p < N_POS; p++) {
          if (a == k) continue;
          r.step[0].key = (signed char)k;
          r.step[0].also = (signed char)a;
          r.step[0].dir = (unsigned char)d;
          r.step[0].pos = (unsigned char)p;
          for (a2 = -1; a2 < (depth == 2 ? (int)qmax : 0); a2++)
            for (d2 = 0; d2 < (depth == 2 ? 2 : 1); d2++)
              for (p2 = 0; p2 < (depth == 2 ? N_POS : 1); p2++)
                for (y = -1; y < (int)qmax; y++) {
                  r.step[1].key = -1;
                  r.step[1].also = (signed char)a2;
                  r.step[1].dir = (unsigned char)d2;
                  r.step[1].pos = (unsigned char)p2;
                  r.yes = (signed char)y;
                  tried++;
                  if (survives(&r)) keep(&r);
                }
        }
  return tried;
}

static void say(const rule_t *r, char *out) {
  static const char *where[N_POS] = {"first", "second", "third", "fourth", "last", "second-last", "third-last", "fourth-last"};
  char a[64] = "";
  if (r->step[0].also >= 0) sprintf(a, " and question word %d", r->step[0].also + 1);
  sprintf(out, "find the %s sentence holding question word %d%s; take its %s word",
          r->step[0].dir ? "first" : "most recent", r->step[0].key + 1, a, where[r->step[0].pos]);
  if (r->depth == 2) {
    char b[64] = "";
    if (r->step[1].also >= 0) sprintf(b, " and question word %d", r->step[1].also + 1);
    sprintf(out + strlen(out), "; then find the %s sentence holding that%s; take its %s word",
            r->step[1].dir ? "first" : "most recent", b, where[r->step[1].pos]);
  }
  if (r->yes >= 0) sprintf(out + strlen(out), "; say yes if it is question word %d, else no", r->yes + 1);
}

static unsigned long long SEED = 1ull;
static unsigned pick(unsigned n) {
  SEED = SEED * 6364136223846793005ull + 1442695040888963407ull;
  return (unsigned)((SEED >> 33) % n);
}

/* answers on stories never seen: agreed, or chosen uniformly among the rules standing */
static void test(story_t *te, unsigned n_te, int task, unsigned *right, unsigned *total, unsigned *sure) {
  unsigned i, j;
  *right = *total = *sure = 0u;
  for (i = 0u; i < n_te; i++) {
    unsigned first = NONE, agree = 1u, got;
    if (te[i].task != task) continue;
    (*total)++;
    if (N_ALIVE == 0u) continue;
    for (j = 0u; j < N_ALIVE; j++) {
      unsigned w = answer(&te[i], &ALIVE[j]);
      if (j == 0u) first = w;
      else if (w != first) {
        agree = 0u;
        break;
      }
    }
    got = agree ? first : answer(&te[i], &ALIVE[pick(N_ALIVE)]);
    if (agree && first != NONE) (*sure)++;
    if (got == te[i].answer) (*right)++;
  }
}

static int survey(int argc, char **argv) {
  unsigned n_tr, n_te, task, total_right = 0u, total = 0u, passed = 0u;
  story_t *tr, *te;
  YES = word("yes", 3u);
  NO = word("no", 2u);
  tr = load(argc > 1 ? argv[1] : "books/babi_train.txt", &n_tr);
  te = load(argc > 2 ? argv[2] : "books/babi_test.txt", &n_te);
  LESSON = (story_t **)malloc(sizeof(story_t *) * (n_tr + 1u));
  printf("%u stories to learn from, %u never seen to be tested on\n\n", n_tr, n_te);
  for (task = 1u; task <= 20u; task++) {
    unsigned qmax = 0u, i, tried, right, all, sure, seen = 0u, depth = 1u, r10;
    for (i = 0u; i < n_tr; i++) {
      if (tr[i].task == (int)task && tr[i].q.n > qmax) qmax = tr[i].q.n;
    }
    if (qmax > MAX_Q) qmax = MAX_Q;
    /* after only ten stories */
    (void)seen;
    lesson(tr, n_tr, (int)task, 10u);
    N_ALIVE = 0u;
    N_STILL = 0u;
    tried = formulate(1, qmax);
    if (N_ALIVE == 0u) (void)formulate(2, qmax);
    test(te, n_te, (int)task, &r10, &all, &sure);
    /* after all of them */
    lesson(tr, n_tr, (int)task, 100000u);
    N_ALIVE = 0u;
    N_STILL = 0u;
    tried = formulate(1, qmax);
    if (N_ALIVE == 0u) {
      depth = 2u;   /* every one-step rule ruled out: it widens its own language */
      tried += formulate(2, qmax);
    }
    test(te, n_te, (int)task, &right, &all, &sure);
    printf("task %2u: after 10 stories %5.1f%%, after 900: %5.1f%% right (%5.1f%% sure) | %s rules; %u of %u left (%.1f bits)\n",
           task, 100.0 * r10 / all, 100.0 * right / all, 100.0 * sure / all,
           depth == 1u ? "one-step" : "widened to two-step", N_ALIVE, tried,
           N_ALIVE ? log2((double)N_ALIVE) : 0.0);
    if (N_ALIVE > 0u) {
      char words[400];
      say(&ALIVE[0], words);
      printf("         e.g. \"%s\"\n", words);
      {
        /* the words it has not ruled out as ones that change things, among those in what it read */
        const unsigned long long *out = &STILL[(size_t)ALIVE[0].idx * 2u * WB];
        unsigned w, shown = 0u;
        printf("         words it holds may change things: ");
        for (w = 2u; w < N_WORDS && w < WB * 64u && shown < 16u; w++) {
          unsigned i2, seen_it = 0u;
          for (i2 = 0u; i2 < N_LESSON && !seen_it; i2++) {
            unsigned k2;
            for (k2 = 0u; k2 < LESSON[i2]->n_sent && !seen_it; k2++) seen_it = (unsigned)holds(&LESSON[i2]->sent[k2], w);
          }
          if (seen_it && !((out[w >> 6] >> (w & 63u)) & 1ull)) {
            printf("%s ", WORD[w]);
            shown++;
          }
        }
        printf("\n");
      }
    }
    total_right += right;
    total += all;
    if (right * 100u >= all * 95u) passed++;
  }
  printf("\nall twenty: %.1f%% right on stories never seen; %u of 20 tasks at 95%% or better\n",
         100.0 * total_right / total, passed);
  return 0;
}


/* ==== a picture of the world =================================================

   When no rule it can say fits a kind of story, looking things up in the story
   is not enough: John picks up the football, walks to the garden, drops it and
   walks on -- the football is in the garden, and nothing in the last sentence
   about John or the football says so. It needs a picture of the world, changed
   by each sentence, and to learn what each word does to that picture.

   The picture: for each thing, where it is (or who has it). A sentence changes it
   through one word in it (the word at place `tpos`), and what that word does is
   one of:
       nothing
       SET a b     thing a is now at b                "Mary moved to the kitchen"
       COPY a b    thing a is now wherever b is       "John dropped the football"
       SET2 a b    things a and a+2 are now at b      "Mary and John went to ..."
   An answer follows the picture from the thing asked about to where it ends up
   (football -> John -> garden), or says yes or no by comparing.

   It holds one picture of how the world works, and it is wrong. Each time a story
   shows it wrong, it looks for the smallest change -- what one word does, or how
   it reads the question -- that makes that story come out right without making
   any story it already gets right come out wrong. That is a mistake fixed. When
   no such change exists, the mistake is kept, unfixed, and said so.
*/

#define N_WP 7
static const int WPOS[N_WP] = {0, 1, 2, 3, 4, -1, -2};
#define N_EFF (1u + 3u * N_WP * N_WP)
#define MAX_READ 1000u

typedef struct {
  unsigned char tpos, akind, ak, aj;   /* which word acts; how it answers: 1 follow, 2 one step, 3 yes/no */
  unsigned short eff[MAX_WORDS];       /* what each word does: 0 nothing, else 1 + index */
  story_t *read[MAX_READ];
  unsigned char ok[MAX_READ];
  unsigned n_read;
  unsigned fixed, unfixed, revisions;
} world_t;

static world_t *WORLD[21];
static unsigned short PIC[MAX_WORDS];
static unsigned short TOUCHED[MAX_WORDS];
static unsigned N_TOUCHED;
#define NOWHERE 0xffffu

static int wpos(const line_t *l, unsigned i) {
  int p = WPOS[i];
  if (p < 0) p += (int)l->n;
  return (p >= 0 && (unsigned)p < l->n) ? p : -1;
}

static void place(unsigned thing, unsigned at) {
  if (PIC[thing] == NOWHERE) TOUCHED[N_TOUCHED++] = (unsigned short)thing;
  PIC[thing] = (unsigned short)at;
}

static unsigned where(unsigned thing) {
  unsigned v = thing, steps = 0u, moved = 0u;
  while (steps++ < 6u && PIC[v] != NOWHERE && PIC[v] != v) {
    v = PIC[v];
    moved = 1u;
  }
  return moved ? v : NONE;
}

static unsigned imagine(const world_t *w, const story_t *s) {
  unsigned i, got = NONE;
  for (i = 0u; i < s->n_sent; i++) {
    const line_t *l = &s->sent[i];
    unsigned e, kind, a, b;
    int pa, pb;
    if (w->tpos >= l->n) continue;
    e = w->eff[l->t[w->tpos]];
    if (e == 0u) continue;
    e--;
    kind = e / (N_WP * N_WP);
    a = (e / N_WP) % N_WP;
    b = e % N_WP;
    pa = wpos(l, a);
    pb = wpos(l, b);
    if (pa < 0 || pb < 0 || pa == pb) continue;
    if (kind == 0u) {
      place(l->t[pa], l->t[pb]);
    } else if (kind == 1u) {
      unsigned at = where(l->t[pb]);
      if (at != NONE) place(l->t[pa], at);
    } else {
      place(l->t[pa], l->t[pb]);
      if ((unsigned)pa + 2u < l->n && (unsigned)pa + 2u != (unsigned)pb) place(l->t[pa + 2], l->t[pb]);
    }
  }
  if (w->ak < s->q.n) {
    unsigned thing = s->q.t[w->ak];
    unsigned at = w->akind == 2u ? (PIC[thing] == NOWHERE ? NONE : PIC[thing]) : where(thing);
    if (w->akind == 3u) {
      if (w->aj < s->q.n && at != NONE) got = (at == s->q.t[w->aj]) ? YES : NO;
      else if (w->aj < s->q.n) got = NO;
    } else {
      got = at;
    }
  }
  for (i = 0u; i < N_TOUCHED; i++) PIC[TOUCHED[i]] = NOWHERE;
  N_TOUCHED = 0u;
  return got;
}

/* would this picture still get right every story it gets right now, and this one too? */
static int keeps_all(const world_t *w, const story_t *now) {
  unsigned i;
  if (imagine(w, now) != now->answer) return 0;
  for (i = 0u; i < w->n_read; i++) {
    if (w->ok[i] && imagine(w, w->read[i]) != w->read[i]->answer) return 0;
  }
  return 1;
}

/* the words in a story that could be the one acting, at this tpos */
static unsigned acting_words(const world_t *w, const story_t *s, unsigned *out) {
  unsigned i, j, n = 0u;
  for (i = 0u; i < s->n_sent; i++) {
    unsigned t;
    if (w->tpos >= s->sent[i].n) continue;
    t = s->sent[i].t[w->tpos];
    for (j = 0u; j < n && out[j] != t; j++) {
    }
    if (j == n && n < 64u) out[n++] = t;
  }
  return n;
}

/* the smallest change that fixes this story without breaking one it gets right */
static int revise(world_t *w, const story_t *s) {
  unsigned words[64], n, i, e, start;
  /* one word doing something else */
  n = acting_words(w, s, words);
  start = pick(N_EFF);
  for (i = 0u; i < n; i++) {
    unsigned short was = w->eff[words[i]];
    for (e = 0u; e < N_EFF; e++) {
      unsigned short cand = (unsigned short)((start + e) % N_EFF);
      if (cand == was) continue;
      w->eff[words[i]] = cand;
      if (keeps_all(w, s)) return 1;
    }
    w->eff[words[i]] = was;
  }
  /* reading the question another way */
  {
    unsigned char k0 = w->akind, a0 = w->ak, j0 = w->aj, kind, ak, aj;
    for (kind = 1u; kind <= 3u; kind++)
      for (ak = 0u; ak < 6u; ak++)
        for (aj = 0u; aj < (kind == 3u ? 8u : 1u); aj++) {
          w->akind = kind;
          w->ak = ak;
          w->aj = aj;
          if (keeps_all(w, s)) return 1;
        }
    w->akind = k0;
    w->ak = a0;
    w->aj = j0;
  }
  /* reading the question another way and one word doing something else, together
     (early on, when it has understood nothing, one change alone never fixes anything) */
  if (w->fixed < 3u) {
    unsigned char k0 = w->akind, a0 = w->ak, j0 = w->aj, t0 = w->tpos, kind, ak, aj, tp;
    for (tp = 0u; tp < 4u; tp++) {
      w->tpos = tp;
      n = acting_words(w, s, words);
      for (kind = 1u; kind <= 3u; kind++)
        for (ak = 0u; ak < 6u; ak++)
          for (aj = 0u; aj < (kind == 3u ? 8u : 1u); aj++) {
            w->akind = kind;
            w->ak = ak;
            w->aj = aj;
            for (i = 0u; i < n; i++) {
              unsigned short was = w->eff[words[i]];
              for (e = 1u; e < N_EFF; e++) {
                w->eff[words[i]] = (unsigned short)e;
                if (keeps_all(w, s)) return 1;
              }
              w->eff[words[i]] = was;
            }
          }
    }
    w->akind = k0;
    w->ak = a0;
    w->aj = j0;
    w->tpos = t0;
  }
  return 0;
}

/* a story, in a kind it now pictures: answer first, then fix what it got wrong */
static unsigned world_read(unsigned t, story_t *s, int *fixed_now) {
  world_t *w = WORLD[t];
  unsigned mine = imagine(w, s), i;
  *fixed_now = -1;
  if (mine != s->answer) {
    w->revisions++;
    if (revise(w, s)) {
      w->fixed++;
      *fixed_now = 1;
      for (i = 0u; i < w->n_read; i++) {   /* a fix can also mend older mistakes */
        if (!w->ok[i] && imagine(w, w->read[i]) == w->read[i]->answer) {
          w->ok[i] = 1u;
          w->fixed++;
          w->unfixed--;
        }
      }
    } else {
      w->unfixed++;
      *fixed_now = 0;
    }
  }
  if (w->n_read < MAX_READ) {
    w->read[w->n_read] = s;
    w->ok[w->n_read] = (unsigned char)(imagine(w, s) == s->answer);
    w->n_read++;
  }
  return mine;
}

static void world_begin(unsigned t, unsigned read_so_far, story_t **stories, const unsigned char *done) {
  unsigned i;
  int f;
  WORLD[t] = (world_t *)calloc(1u, sizeof(world_t));
  WORLD[t]->akind = 1u;
  for (i = 0u; i < MAX_WORDS; i++) PIC[i] = NOWHERE;
  (void)read_so_far;
  /* every story it has read of this kind is lived again in the new picture */
  for (i = 0u; i < 1000u; i++) {
    if (done[i]) (void)world_read(t, stories[i], &f);
  }
}

static void world_test(unsigned t, story_t *te, unsigned n_te, unsigned *right, unsigned *total) {
  unsigned i;
  *right = *total = 0u;
  for (i = 0u; i < n_te; i++) {
    if (te[i].task != (int)t) continue;
    (*total)++;
    if (imagine(WORLD[t], &te[i]) == te[i].answer) (*right)++;
  }
}

/* ==== the curious child ======================================================

   Nobody hands it task 1 and then task 2. Each moment it chooses what to read
   next, by where it can still be wrong -- a kind of story it has never met, or
   one it was just wrong about; then one it is unsure of, or cannot make sense of
   yet; and last, one it has not been wrong about for a while. The choice is
   uniform among those it is most drawn to: never weighed.

   It is never allowed to be sure it understands. Being wrong is the only thing
   that rules anything out, so a kind of story it has not been wrong about for a
   while is one it goes hunting in: it reads the story least like any it has read
   there -- the one holding most words it has never met in that kind of story --
   because that is where it is likeliest to be shown wrong.

   Before it is told any answer it answers for itself. Where every rule it still
   holds gives the same answer, it is sure. Sure and wrong is a surprise, and a
   surprise draws it straight back to that kind of story.

   One story at a time: what it holds is ruled down after each. Only when nothing
   it can say is left does it widen its language, and when even the wider one is
   ruled out it says so, and leaves that kind of story until it can say more.

   What it has read is its experience, kept in child/understanding.txt as the
   order it read things in. Each run rebuilds its understanding from that and then
   reads on, so each hour it knows what it knew and a little more.
*/

#define MASTERED 20u   /* not wrong this many times running: it stops trusting itself and hunts */

typedef struct {
  rule_t *alive;
  unsigned long long *still;
  unsigned n, cap_a, n_still, cap_s;
  unsigned depth, qmax, touched, stuck, surprised_last, streak, mastered;
  unsigned read, sure_right, sure_wrong, unsure, wrong_last, wrong, taught, hunts, fixed, unfixed;
  unsigned fixed_last, unfixed_run;   /* was its last mistake fixed; mistakes in a row it could not fix */
  story_t **story;
  unsigned n_story;
  unsigned char *done;               /* stories of this kind already read */
  unsigned long long met[WB];        /* words met in this kind of story */
} kind_t;

static kind_t K[21];
static FILE *DIARY;
static int QUIET;

static void swap_in(unsigned t) {
  ALIVE = K[t].alive;
  N_ALIVE = K[t].n;
  CAP_ALIVE = K[t].cap_a;
  STILL = K[t].still;
  N_STILL = K[t].n_still;
  CAP_STILL = K[t].cap_s;
}

static void swap_out(unsigned t) {
  K[t].alive = ALIVE;
  K[t].n = N_ALIVE;
  K[t].cap_a = CAP_ALIVE;
  K[t].still = STILL;
  K[t].n_still = N_STILL;
  K[t].cap_s = CAP_STILL;
}

static void note(const char *line) {
  if (QUIET) return;
  printf("  %s\n", line);
  if (DIARY != 0) fprintf(DIARY, "  %s\n", line);
}

/* its own answer, before being told: agreed on, or chosen uniformly among the rules it holds */
static unsigned own_answer(const story_t *s, int *sure) {
  unsigned j, first = NONE;
  *sure = 0;
  if (N_ALIVE == 0u) return NONE;
  for (j = 0u; j < N_ALIVE; j++) {
    unsigned w = answer(s, &ALIVE[j]);
    if (j == 0u) first = w;
    else if (w != first) return answer(s, &ALIVE[pick(N_ALIVE)]);
  }
  *sure = (first != NONE);
  return first;
}

/* told the answer: every rule that would have said otherwise is ruled out */
static void rule_down(const story_t *s) {
  unsigned j, w = 0u;
  for (j = 0u; j < N_ALIVE; j++) {
    unsigned got;
    memcpy(SCRATCH, &STILL[(size_t)ALIVE[j].idx * 2u * WB], sizeof SCRATCH);
    got = answer_l(s, &ALIVE[j], SCRATCH[0], SCRATCH[1], s->answer);
    if (got != s->answer) continue;
    ALIVE[w] = ALIVE[j];
    memcpy(&STILL[(size_t)w * 2u * WB], SCRATCH, sizeof SCRATCH);
    ALIVE[w].idx = w;
    w++;
  }
  N_ALIVE = N_STILL = w;
}

static unsigned unmet_words(const kind_t *k, const story_t *s) {
  unsigned n = 0u, i, j;
  for (i = 0u; i < s->n_sent; i++) {
    for (j = 0u; j < s->sent[i].n; j++) {
      unsigned w = s->sent[i].t[j];
      if (w < WB * 64u && !((k->met[w >> 6] >> (w & 63u)) & 1ull)) n++;
    }
  }
  return n;
}

/* the story it reads next: the next in order, or, hunting, the one least like what it has read */
static unsigned next_story(unsigned t) {
  kind_t *k = &K[t];
  unsigned i, best = 0u, n = 0u;
  static unsigned tie[1000];
  if (k->streak < MASTERED) {
    for (i = 0u; i < k->n_story; i++) {
      if (!k->done[i]) return i;
    }
    return 0u;
  }
  for (i = 0u; i < k->n_story; i++) {
    unsigned u;
    if (k->done[i]) continue;
    u = unmet_words(k, k->story[i]);
    if (n == 0u || u > best) {
      best = u;
      n = 0u;
    }
    if (u == best) tie[n++] = i;
  }
  return n ? tie[pick(n)] : 0u;
}

static void read_one(unsigned t, unsigned which) {
  kind_t *k = &K[t];
  story_t *s = k->story[which];
  unsigned before_rules, hunting = (k->streak >= MASTERED);
  char line[600];
  int sure;
  unsigned mine;
  if (WORLD[t] != 0) {
    /* a kind it pictures: it answers from its picture, and fixes the picture when wrong */
    int fixed_now;
    unsigned i2, j2;
    if (hunting) k->hunts++;
    mine = world_read(t, s, &fixed_now);
    if (mine == s->answer) {
      k->streak++;
      k->wrong_last = 0u;
      if (k->streak == MASTERED) {
        k->mastered = 1u;
        sprintf(line, "task %u: my picture has not been wrong for %u stories. I do not trust that. "
                "I go looking for the story that proves it wrong", t, MASTERED);
        note(line);
      }
    } else {
      k->wrong++;
      k->wrong_last = 1u;
      k->streak = 0u;
      k->mastered = 0u;
      k->fixed_last = (fixed_now == 1);
      k->unfixed_run = (fixed_now == 1) ? 0u : k->unfixed_run + 1u;
      if (fixed_now == 1) {
        k->fixed++;
        if (k->fixed <= 3u || k->fixed % 25u == 0u) {
          sprintf(line, "task %u, story %u: I said \"%s\", it was \"%s\". I changed my picture of the world; "
                  "now I get it right, and everything I got right before too (%u fixed so far)",
                  t, which + 1u, mine == NONE ? "?" : WORD[mine], WORD[s->answer], k->fixed);
          note(line);
        }
      } else {
        k->unfixed++;
        if (k->unfixed <= 2u) {
          sprintf(line, "task %u, story %u: I said \"%s\", it was \"%s\", and no change to my picture fixes it "
                  "without breaking something I had right. I keep it, unfixed",
                  t, which + 1u, mine == NONE ? "?" : WORD[mine], WORD[s->answer]);
          note(line);
        }
      }
    }
    k->done[which] = 1u;
    for (i2 = 0u; i2 < s->n_sent; i2++) {
      for (j2 = 0u; j2 < s->sent[i2].n; j2++) {
        unsigned w2 = s->sent[i2].t[j2];
        if (w2 < WB * 64u) k->met[w2 >> 6] |= 1ull << (w2 & 63u);
      }
    }
    k->read++;
    return;
  }
  swap_in(t);
  if (!k->touched) {
    k->touched = 1u;
    k->depth = 1u;
    N_LESSON = 0u;
    N_ALIVE = N_STILL = 0u;
    (void)formulate(1, k->qmax);   /* everything it can say, nothing yet ruled out */
    sprintf(line, "task %u: a kind of story I have never met; I could answer it %u ways", t, N_ALIVE);
    note(line);
  }
  if (hunting) k->hunts++;
  mine = own_answer(s, &sure);   /* it always answers, sure or not, so that it can be wrong */
  before_rules = N_ALIVE;
  if (sure && mine == s->answer) {
    k->sure_right++;
    k->streak++;
    k->wrong_last = 0u;
    k->surprised_last = 0u;
    if (k->streak == MASTERED) {
      char said[400];
      k->mastered = 1u;
      say(&ALIVE[0], said);
      sprintf(line, "task %u: I have not been wrong for %u stories (\"%s\"). I do not trust that. "
              "I go looking for the story that proves me wrong", t, MASTERED, said);
      note(line);
    }
  } else {
    unsigned was_streak = k->streak;
    if (mine != s->answer) {
      k->wrong++;
      k->wrong_last = 1u;
    } else {
      k->wrong_last = 0u;
    }
    if (sure) {
      k->sure_wrong++;
      k->surprised_last = 1u;
    } else {
      k->unsure++;
      k->surprised_last = 0u;
    }
    k->streak = 0u;
    k->mastered = 0u;
    if (sure && (k->sure_wrong <= 3u || was_streak >= MASTERED)) {
      sprintf(line, "task %u, story %u%s: I was sure it was \"%s\", and it was \"%s\". Good: I was wrong",
              t, which + 1u, hunting ? " (hunting for it)" : "", mine == NONE ? "?" : WORD[mine], WORD[s->answer]);
      note(line);
    }
  }
  rule_down(s);
  if (mine != s->answer) {
    k->fixed_last = (N_ALIVE > 0u);
    k->unfixed_run = k->fixed_last ? 0u : k->unfixed_run + 1u;
    if (N_ALIVE > 0u) k->fixed++;   /* what was wrong is ruled out, and what is left gets it right */
    else k->unfixed++;
  }
  if (N_ALIVE < before_rules) {
    k->taught += before_rules - N_ALIVE;
    if (k->wrong_last && k->sure_wrong <= 3u && before_rules - N_ALIVE > 0u && sure) {
      sprintf(line, "task %u: being wrong ruled out %u of the %u ways I had", t, before_rules - N_ALIVE, before_rules);
      note(line);
    }
  }
  k->done[which] = 1u;
  {
    unsigned i, j;
    for (i = 0u; i < s->n_sent; i++) {
      for (j = 0u; j < s->sent[i].n; j++) {
        unsigned w = s->sent[i].t[j];
        if (w < WB * 64u) k->met[w >> 6] |= 1ull << (w & 63u);
      }
    }
  }
  k->read++;
  if (N_ALIVE == 0u && k->depth == 1u) {
    /* nothing it can say is left: its language was too poor, and it widens it */
    unsigned i;
    k->depth = 2u;
    N_LESSON = 0u;
    for (i = 0u; i < k->n_story; i++) {
      if (k->done[i]) LESSON[N_LESSON++] = k->story[i];
    }
    N_ALIVE = N_STILL = 0u;
    (void)formulate(2, k->qmax);
    sprintf(line, "task %u: nothing I could say was left after %u stories, so now I think in two steps (%u ways)",
            t, k->read, N_ALIVE);
    note(line);
  }
  if (N_ALIVE == 0u && !k->stuck) {
    k->stuck = 1u;
    swap_out(t);
    world_begin(t, k->read, k->story, k->done);
    sprintf(line, "task %u: no rule I can say fits these %u stories, so I stop looking things up and keep a picture "
            "of the world instead, and learn what each word does to it (%u of them right so far)",
            t, k->read, WORLD[t]->n_read - WORLD[t]->unfixed);
    note(line);
    k->fixed += WORLD[t]->fixed;
    return;
  }
  swap_out(t);
}

/* where its mistakes get fixed: 3 never met, or its last mistake there was fixed (it is learning);
   2 unsure, or its last mistake is not fixed yet; 1 not wrong for a while (it still goes, hunting);
   0 ten mistakes in a row it could not fix (it still comes back, when nothing else calls) */
static int curiosity(unsigned t) {
  const kind_t *k = &K[t];
  if (k->read >= k->n_story) return -1;
  if (!k->touched) return 3;
  if (k->unfixed_run >= 10u) return 0;
  if (k->wrong_last && k->fixed_last) return 3;
  if (!k->mastered) return 2;
  return 1;
}

static unsigned choose(void) {
  unsigned t, n = 0u, best[21];
  int top = -1;
  for (t = 1u; t <= 20u; t++) {
    int c = curiosity(t);
    if (c < 0) continue;
    if (c > top) {
      top = c;
      n = 0u;
    }
    if (c == top) best[n++] = t;
  }
  return n ? best[pick(n)] : 0u;
}

static char ORDER[400000];

static int curious(unsigned budget, const char *mind, const char *diary_path, story_t *tr, unsigned n_tr,
                   story_t *te, unsigned n_te) {
  unsigned t, i, before = 0u;
  size_t n_order = 0u;
  FILE *f;
  for (t = 1u; t <= 20u; t++) {
    K[t].story = (story_t **)malloc(sizeof(story_t *) * 1000u);
    K[t].done = (unsigned char *)calloc(1000u, 1u);
    for (i = 0u; i < n_tr; i++) {
      if (tr[i].task == (int)t && K[t].n_story < 1000u) {
        K[t].story[K[t].n_story++] = &tr[i];
        if (tr[i].q.n > K[t].qmax) K[t].qmax = tr[i].q.n;
      }
    }
    if (K[t].qmax > MAX_Q) K[t].qmax = MAX_Q;
  }
  /* its experience so far: the order it read things in, lived again silently */
  f = fopen(mind, "r");
  if (f != 0) {
    int c;
    unsigned v = 0u;
    QUIET = 1;
    unsigned which = 0u, *cur = &v;
    while ((c = fgetc(f)) != EOF) {
      if (c >= '0' && c <= '9') {
        *cur = *cur * 10u + (unsigned)(c - '0');
      } else if (c == '.') {
        cur = &which;
      } else {
        if (v >= 1u && v <= 20u && which < K[v].n_story && !K[v].done[which] && n_order + 16u < sizeof ORDER) {
          read_one(v, which);
          n_order += (size_t)sprintf(ORDER + n_order, "%u.%u,", v, which);
          before++;
        }
        v = which = 0u;
        cur = &v;
      }
    }
    fclose(f);
    QUIET = 0;
  }
  DIARY = fopen(diary_path, "a");
  printf("it remembers %u stories it has read; it reads %u more, choosing what it is curious about\n\n", before, budget);
  if (DIARY) fprintf(DIARY, "-- it remembers %u stories, and reads %u more --\n", before, budget);
  for (i = 0u; i < budget; i++) {
    unsigned c = choose(), which;
    if (c == 0u) break;
    which = next_story(c);
    read_one(c, which);
    if (n_order + 16u < sizeof ORDER) n_order += (size_t)sprintf(ORDER + n_order, "%u.%u,", c, which);
  }
  f = fopen(mind, "w");
  if (f != 0) {
    fprintf(f, "%s\n", ORDER);
    fclose(f);
  }
  {
    unsigned all_right = 0u, all = 0u, understood = 0u, wrong = 0u, hunts = 0u, fixed = 0u, unfixed = 0u;
    printf("\n  task  read   wrong  fixed  unfixed   how it answers          now                never-seen\n");
    for (t = 1u; t <= 20u; t++) {
      unsigned right, total, sure;
      const char *state = !K[t].touched ? "not met yet" : K[t].mastered ? "not wrong lately" : "being wrong";
      char how[40];
      if (WORLD[t] != 0) {
        world_test(t, te, n_te, &right, &total);
        strcpy(how, "a picture of the world");
      } else {
        swap_in(t);
        test(te, n_te, (int)t, &right, &total, &sure);
        sprintf(how, "%u rule%s", N_ALIVE, N_ALIVE == 1u ? "" : "s");
      }
      printf("  %4u  %4u  %6u  %5u  %7u   %-22s  %-17s  %5.1f%%\n", t, K[t].read, K[t].wrong, K[t].fixed,
             K[t].unfixed, how, state, total ? 100.0 * right / total : 0.0);
      wrong += K[t].wrong;
      hunts += K[t].hunts;
      fixed += K[t].fixed;
      unfixed += K[t].unfixed;
      all_right += right;
      all += total;
      if (K[t].mastered) understood++;
    }
    printf("\n  wrong %u times in all; %u of those mistakes fixed, %u not yet; %u stories read hunting for mistakes\n",
           wrong, fixed, unfixed, hunts);
    printf("  all twenty, on stories never seen: %.1f%%; kinds it has not been wrong about lately: %u of 20\n",
           100.0 * all_right / all, understood);
    if (DIARY) {
      fprintf(DIARY, "   now: wrong %u times, fixed %u, not yet %u; %.1f%% right on stories never seen; "
              "not wrong lately about %u kinds of 20\n", wrong, fixed, unfixed, 100.0 * all_right / all, understood);
      fclose(DIARY);
    }
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 && strcmp(argv[1], "--curious") == 0) {
    unsigned n_tr, n_te;
    story_t *tr, *te;
    YES = word("yes", 3u);
    NO = word("no", 2u);
    tr = load("books/babi_train.txt", &n_tr);
    te = load("books/babi_test.txt", &n_te);
    LESSON = (story_t **)malloc(sizeof(story_t *) * (n_tr + 1u));
    return curious(argc > 2 ? (unsigned)atoi(argv[2]) : 500u,
                   argc > 3 ? argv[3] : "child/understanding.txt",
                   argc > 4 ? argv[4] : "child/reading_journal.txt", tr, n_tr, te, n_te);
  }
  return survey(argc, argv);
}
