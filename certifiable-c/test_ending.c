/*
 * test_ending.c -- theories of what ends a level, and the language widening itself.
 */
#include <stdio.h>
#include <string.h>

#include "smarsh_ending.h"

static unsigned FAILED;

static void check(const char *what, int ok) {
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok) FAILED++;
}

static en_obs_t act(uint16_t onto, uint16_t last, uint16_t gone, int ended) {
  en_obs_t o;
  memset(&o, 0, sizeof o);
  o.onto = onto;
  o.last = last;
  o.gone = gone;
  o.ended = (unsigned char)ended;
  return o;
}

#define C(v) ((uint16_t)(1u << (v)))

static const en_family_t *family(const en_theory_t *t, const char *name) {
  unsigned i;
  char buf[32];
  for (i = 0u; i < t->n_fam; i++) {
    if (strcmp(en_family_name(&t->fam[i], buf, sizeof buf), name) == 0) return &t->fam[i];
  }
  return 0;
}

int main(void) {
  static en_theory_t t;
  uint16_t onto, gone, point;
  int match;

  printf("WHAT ENDS A LEVEL, AND A LANGUAGE THAT WIDENS ITSELF\n\n");

  /* a door (11) that ends the level only once every key (9) is taken; floor is 0 */
  en_begin(&t, 0);
  en_observe(&t, &(en_obs_t){0});                              /* nothing happens */
  { en_obs_t o = act(C(0), 0, 0, 0); en_observe(&t, &o); }      /* onto floor */
  { en_obs_t o = act(C(11), 0, 0, 0); en_observe(&t, &o); }     /* onto the door, keys left: nothing */
  { en_obs_t o = act(C(9), 0, 0, 0); en_observe(&t, &o); }      /* a key, not the last */
  { en_obs_t o = act(C(9), C(9), 0, 0); en_observe(&t, &o); }   /* the last key: nothing ends */
  { en_obs_t o = act(C(0), 0, C(9), 0); en_observe(&t, &o); }   /* no keys left, onto floor */
  check("before the ending, one fact is still enough to say it", t.depth == 1u);
  { en_obs_t o = act(C(11), 0, C(9), 1); en_observe(&t, &o); }  /* onto the door, no keys left: it ends */
  check("the ending rules out every single fact: the language widens itself to pairs",
        t.depth == 2u && t.widenings == 1u);
  {
    const en_family_t *f = family(&t, "ONTO+GONE");
    check("it finds, among pairs, 'onto the door once no key is left'",
          f != 0 && sm_is_possible(&f->dom, 11u * EN_COLOURS + 9u) && f->survived_ending);
  }
  en_aim(&t, &onto, &gone, &point, &match);
  check("so it goes for the door, and for having no keys", (onto & C(11)) && (gone & C(9)));
  en_report(&t, stdout);

  /* a plain goal: stepping onto 4 ends it. One fact says it; nothing widens */
  en_begin(&t, 0);
  { en_obs_t o = act(C(0), 0, 0, 0); en_observe(&t, &o); }
  { en_obs_t o = act(C(4), 0, 0, 1); en_observe(&t, &o); }
  en_aim(&t, &onto, &gone, &point, &match);
  check("a plain goal is found without widening", t.depth == 1u && (onto & C(4)) && !(onto & C(0)));

  /* a new game, with the family another game kept: formulated from the first act */
  en_begin(&t, "ONTO,ONTO+GONE");
  {
    const en_family_t *f = family(&t, "ONTO+GONE");
    check("a family kept from another game is there from the start", f != 0 && f->from_library);
  }
  { en_obs_t o = act(C(0), 0, 0, 0); en_observe(&t, &o); }
  en_aim(&t, &onto, &gone, &point, &match);
  check("and it already aims by it, before any ending in this game", onto != 0u);

  check("NULL arguments are checked errors",
        en_begin(0, 0) == SM_ERR_NULL_ARGUMENT && en_observe(0, 0) == SM_ERR_NULL_ARGUMENT &&
        en_aim(&t, 0, &gone, &point, &match) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks failed\n", FAILED);
  return FAILED == 0u ? 0 : 1;
}
