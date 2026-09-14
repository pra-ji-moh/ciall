/*
 * test_informant.c -- the rules of smarsh_informant.c, each checked.
 *
 * Especially the ones that are easy to get quietly wrong: a majority of
 * sources does not settle a dispute; one contradiction discredits a source
 * everywhere; a claim made AFTER something was seen is checked against it
 * immediately; sight outranks every source; and nothing told is ever
 * reported as seen.
 */

#include <stdio.h>

#include "smarsh_informant.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

static sk_store_t K;
static const unsigned B2[2] = {2u, 2u};
static const char *const FN[2] = {"p", "q"};
static unsigned f_and(const unsigned *v) { return v[0] && v[1]; }
static unsigned f_or(const unsigned *v) { return v[0] || v[1]; }
static unsigned f_xor(const unsigned *v) { return v[0] != v[1]; }

static void tell(unsigned src, const char *q, unsigned (*fn)(const unsigned *)) {
  sa_struct_t s;
  sa_define(&s, q, "t", 2u, FN, B2, 2u, fn);
  sk_tell(&K, src, &s);
}

int main(void) {
  unsigned a, b, c, d, caught, ans, open;
  uint32_t src;
  sa_struct_t best;
  static const unsigned v11[2] = {1u, 1u}, v10[2] = {1u, 0u}, v00[2] = {0u, 0u};

  printf("informants, checked rule by rule\n\n");

  sk_init(&K);
  sk_source(&K, "a", &a);
  sk_source(&K, "b", &b);
  sk_source(&K, "c", &c);
  tell(a, "Q", f_or);
  tell(b, "Q", f_or);
  tell(c, "Q", f_xor);   /* disagrees only at 1,1 */
  check("two sources against one is still a dispute, not a verdict",
        sk_row(&K, "Q", v11, &ans, &src) == SK_DISPUTED);
  check("rows everyone agrees on are told, on everyone's word",
        sk_row(&K, "Q", v10, &ans, &src) == SK_TOLD && ans == 1u && src == 7u);
  check("with a dispute open, the store offers no whole structure",
        sk_best(&K, "Q", &best, &src, &open) == SM_ERR_EMPTY_DOMAIN && open == 1u);

  sk_observe(&K, "Q", v11, 0u, &caught);   /* xor was right */
  check("seeing the disputed row catches both sources that got it wrong",
        caught == 2u && K.distrusted[a] && K.distrusted[b] && !K.distrusted[c]);
  check("and the row is now seen, on nobody's word",
        sk_row(&K, "Q", v11, &ans, &src) == SK_CONFIRMED && ans == 0u && src == 0u);
  check("the other rows now rest on the one source still trusted",
        sk_row(&K, "Q", v10, &ans, &src) == SK_TOLD && src == (1u << c));

  tell(a, "R", f_and);
  check("a discredited source is not believed about anything else either",
        sk_row(&K, "R", v11, &ans, &src) == SK_UNKNOWN);

  sk_source(&K, "d", &d);
  tell(d, "Q", f_and);   /* says 1,1 -> yes; already seen to be no */
  check("a claim made after the fact is checked against what was already seen",
        K.distrusted[d]);

  check("the whole structure, offered with who it rests on",
        sk_best(&K, "Q", &best, &src, &open) == SM_OK && src == (1u << c) &&
            best.table[0] == 0u && best.table[3] == 0u);

  sk_observe(&K, "Q", v00, 0u, &caught);
  sk_observe(&K, "Q", v10, 1u, &caught);
  {
    static const unsigned v01[2] = {0u, 1u};
    sk_observe(&K, "Q", v01, 1u, &caught);
  }
  check("once every row is seen, the structure rests on nobody's word",
        sk_best(&K, "Q", &best, &src, &open) == SM_OK && src == 0u);

  {
    unsigned nothing;
    check("an observation of a question nobody has spoken about is refused, not filed",
          sk_observe(&K, "never mentioned", v00, 0u, &nothing) == SM_ERR_EMPTY_DOMAIN);
  }
  check("NULL arguments are checked errors",
        sk_tell(0, 0u, &best) == SM_ERR_NULL_ARGUMENT &&
            sk_observe(&K, 0, v00, 0u, &caught) == SM_ERR_NULL_ARGUMENT);

  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
