/*
 * demo_informant.c -- learning from being told, without believing
 * whoever speaks last or loudest.
 *
 * Three sources tell it things. One is wrong about something that can be
 * checked. It checks, catches it, and stops believing that source about
 * everything, including what it could not check. Then an engineering
 * question comes in, and the analogy it finds says exactly whose word it
 * rests on.
 */

#include <stdio.h>

#include "smarsh_informant.h"

static sk_store_t K;
static sa_library_t LIB;
static sa_found_t F;

static unsigned light_xor(const unsigned *v) { return v[0] != v[1]; }
static unsigned light_or(const unsigned *v) { return v[0] || v[1]; }
static unsigned jury_unanimous(const unsigned *v) { return v[0] && v[1] && v[2]; }
static unsigned jury_majority(const unsigned *v) { return v[0] + v[1] + v[2] >= 2u; }
static unsigned alarm(const unsigned *v) { return v[0] + v[1] + v[2] >= 2u; }

static const unsigned B2[2] = {2u, 2u}, B3[3] = {2u, 2u, 2u};
static const char *const F_LIGHT[2] = {"top switch up", "bottom switch up"};
static const char *const F_JURY[3] = {"juror 1 convinced", "juror 2 convinced", "juror 3 convinced"};
static const char *const F_ALARM[3] = {"sensor A", "sensor B", "sensor C"};

static const char *STANDING[4] = {"unknown", "told", "SEEN", "DISPUTED"};

static void who(uint32_t mask) {
  unsigned s, first = 1u;
  if (mask == 0u) { printf("nobody: it was seen"); return; }
  for (s = 0u; s < K.n_sources; s++) {
    if (mask & ((uint32_t)1 << s)) { printf("%s%s", first ? "" : ", ", K.source_name[s]); first = 0u; }
  }
}

static void show(const char *q, unsigned n) {
  unsigned e, i, v[3], total = 1u;
  for (i = 0u; i < n; i++) total *= 2u;
  printf("  \"%s\":\n", q);
  for (e = 0u; e < total; e++) {
    unsigned a;
    uint32_t src;
    sk_standing_t st;
    for (i = 0u; i < n; i++) v[i] = (e >> (n - 1u - i)) & 1u;
    st = sk_row(&K, q, v, &a, &src);
    printf("    ");
    for (i = 0u; i < n; i++) printf("%s", v[i] ? "1" : "0");
    printf("  %-9s", STANDING[st]);
    if (st == SK_CONFIRMED) printf(" -> %-4s seen directly", a ? "yes" : "no");
    if (st == SK_TOLD) printf(" -> %-4s on the word of: ", a ? "yes" : "no"), who(src);
    printf("\n");
  }
}

int main(void) {
  unsigned book, forum, ai, caught, i;
  sa_struct_t s, q;
  uint32_t src;
  unsigned open;
  static const unsigned both_up[2] = {1u, 1u};

  sk_init(&K);
  sk_source(&K, "physics textbook", &book);
  sk_source(&K, "forum post", &forum);
  sk_source(&K, "another AI", &ai);

  printf("LEARNING BY BEING TOLD, WITHOUT BELIEVING EVERYTHING\n\n");
  printf("Three sources tell it things. Nothing marks which is reliable.\n\n");

  sa_define(&s, "the hallway light is on", "physics", 2u, F_LIGHT, B2, 2u, light_xor);
  sk_tell(&K, book, &s);
  sa_define(&s, "the hallway light is on", "physics", 2u, F_LIGHT, B2, 2u, light_or);
  sk_tell(&K, forum, &s);
  sa_define(&s, "a jury of three convicts", "law", 3u, F_JURY, B3, 2u, jury_unanimous);
  sk_tell(&K, forum, &s);
  sa_define(&s, "a jury of three convicts", "law", 3u, F_JURY, B3, 2u, jury_majority);
  sk_tell(&K, ai, &s);

  printf("WHAT IT HAS BEEN TOLD\n");
  show("the hallway light is on", 2u);
  show("a jury of three convicts", 3u);
  printf("  Where sources disagree it does not pick a side, and two sources\n");
  printf("  agreeing would not count as evidence either.\n\n");

  printf("IT LOOKS: both switches up, and the light is off\n");
  sk_observe(&K, "the hallway light is on", both_up, 0u, &caught);
  for (i = 0u; i < K.n_sources; i++) {
    if (K.distrusted[i]) printf("  caught: \"%s\" said otherwise. Everything it said is set aside.\n", K.source_name[i]);
  }
  show("the hallway light is on", 2u);
  show("a jury of three convicts", 3u);
  printf("  The forum post was never caught about juries. It was caught about\n");
  printf("  lights, and that is enough: its word no longer counts anywhere.\n\n");

  /* the library, built from the best the store can offer */
  sk_best(&K, "the hallway light is on", &s, &src, &open);
  sa_add(&LIB, &s);
  sk_best(&K, "a jury of three convicts", &s, &src, &open);
  sa_add(&LIB, &s);

  printf("QUESTION: when does \"the alarm sounds\" (sensors A, B, C)?\n");
  sa_define(&q, "the alarm sounds", "engineering", 3u, F_ALARM, B3, 2u, alarm);
  sa_find(&LIB, &q, &F);
  for (i = 0u; i < F.n_matches; i++) {
    const sa_struct_t *b = &LIB.s[F.match[i]];
    sk_best(&K, b->name, &s, &src, &open);
    printf("  same shape as \"%s\" (%s), proof checked on every case\n", b->name, b->domain);
    printf("  but that knowledge rests on the word of: ");
    who(src);
    printf("\n  and nothing it has seen confirms it yet. The analogy is exact; the\n");
    printf("  thing it points to is only as good as who said it, and it says so.\n");
  }
  if (F.n_matches == 0u) printf("  nothing it knows has this shape\n");
  return 0;
}
