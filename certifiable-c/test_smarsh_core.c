/*
 * test_smarsh_core.c -- the checks from native/test_node_reasoner.py,
 * native/test_tau_boundary.py, ported. The composition half is gone;
 * see the note where it used to be, and smarsh_reason.h.
 *
 * Those Python versions were RUN and passed against hand-computed values
 * and brute-force ground truth. This file has not been run, because no C
 * compiler is present. It exists so that verifying this port is one
 * command rather than a project:
 *
 *   gcc -std=c99 -Wall -Wextra -pedantic -lm -o test_smarsh \
 *       smarsh_core.c test_smarsh_core.c && ./test_smarsh
 *
 * Every expected value below was computed by hand or copied from a
 * Python run that passed -- none were produced by this C code, which
 * would be circular.
 */

#include "smarsh_core.h"

#include <stdio.h>
#include <math.h>

static int failures = 0;

static void check(const char *name, int condition) {
  if (condition) {
    printf("  ok    %s\n", name);
  } else {
    printf("  FAIL  %s\n", name);
    failures++;
  }
}

static int close_to(double a, double b) {
  double d = a - b;
  if (d < 0.0) {
    d = -d;
  }
  return d < 1e-9;
}

/* A boolean node: index 0 is false, index 1 is true. */
static void boolean_node(sm_possibility_t *p) {
  (void)sm_init(p, 2u);
}

/* derived_result / undetermined_result / groundless_result /
   speculated_result lived here. They existed only to build inputs
   for sm_compose_and, which is gone -- see the note in main(). An
   unused static function is a -Wunused-function warning, and this
   file claims a clean -Wall -Wextra -pedantic build. */

int main(void) {
  sm_possibility_t p;
  sm_possibility_t q;
  sm_result_t r;
  sm_status_t st;
  unsigned i;

  printf("S and H against hand-computed values\n");

  /* Derived: one of two remains. S = 1 - 1/2 = 0.5, H = 0. Matches the
     Python check 'derived: |D|=1, |Dom|=2 gives S=0.5, not 1'. */
  boolean_node(&p);
  (void)sm_eliminate(&p, 0u);
  check("derived: |D|=1 of 2 gives S=0.5", close_to(sm_support(&p), 0.5));
  check("derived: H=0, the emission was forced", close_to(sm_hartley(&p), 0.0));
  check("derived: verdict is derived", sm_verdict(&p) == SM_DERIVED);

  /* Undetermined: nothing eliminated. S = 0 exactly, H = 1 bit. */
  boolean_node(&q);
  check("undetermined: S=0 exactly, nothing ruled out", close_to(sm_support(&q), 0.0));
  check("undetermined: H=1.0 bit", close_to(sm_hartley(&q), 1.0));
  check("undetermined: verdict is undetermined", sm_verdict(&q) == SM_UNDETERMINED);

  /* Contradiction: a domain with everything eliminated. Distinct from
     groundless, which has no domain at all -- the whole point. */
  boolean_node(&p);
  (void)sm_eliminate(&p, 0u);
  (void)sm_eliminate(&p, 1u);
  check("contradiction: everything eliminated, but a domain existed",
        sm_verdict(&p) == SM_CONTRADICTION);
  check("contradiction reports S=1.0, all of it ruled out",
        close_to(sm_support(&p), 1.0));

  sm_init_groundless(&p);
  check("groundless: no domain at all", sm_verdict(&p) == SM_GROUNDLESS);
  check("groundless: S is out of band (-1), not 0",
        close_to(sm_support(&p), -1.0));
  check("groundless and contradiction are different verdicts",
        SM_GROUNDLESS != SM_CONTRADICTION);

  /* Idempotence: applying a constraint twice must equal applying it once. */
  boolean_node(&p);
  (void)sm_eliminate(&p, 0u);
  (void)sm_eliminate(&p, 0u);
  check("eliminating twice equals eliminating once", sm_count(&p) == 1u);

  printf("\nthe tau boundary, mirroring test_tau_boundary.py\n");

  /* S = 0.6: a 5-value domain with 2 remaining. 1 - 2/5 = 0.6. This is
     the exact case verified in Python across 500 seeds. */
  (void)sm_init(&p, 5u);
  (void)sm_eliminate(&p, 0u);
  (void)sm_eliminate(&p, 1u);
  (void)sm_eliminate(&p, 2u);
  check("constructed S=0.6 as intended", close_to(sm_support(&p), 0.6));

  st = sm_decide(&p, 0.5, 1u, SM_INTENSITY_SUPPORT, 0u, &r);
  check("S=0.6 >= tau=0.5 speculates", st == SM_OK && r.verdict == SM_SPECULATED);

  /* S = 0.4: a 5-value domain with 3 remaining. 1 - 3/5 = 0.4. */
  (void)sm_init(&q, 5u);
  (void)sm_eliminate(&q, 0u);
  (void)sm_eliminate(&q, 1u);
  check("constructed S=0.4 as intended", close_to(sm_support(&q), 0.4));

  st = sm_decide(&q, 0.5, 1u, SM_INTENSITY_SUPPORT, 0u, &r);
  check("S=0.4 < tau=0.5 declines, stays undetermined",
        st == SM_OK && r.verdict == SM_UNDETERMINED);

  /* The exact tie. Python settled this explicitly: >= clears the bar. */
  (void)sm_init(&p, 4u);
  (void)sm_eliminate(&p, 0u);
  (void)sm_eliminate(&p, 1u);
  check("constructed S=0.5 exactly", close_to(sm_support(&p), 0.5));
  st = sm_decide(&p, 0.5, 7u, SM_INTENSITY_SUPPORT, 0u, &r);
  check("S == tau clears the bar (>=), so it speculates",
        st == SM_OK && r.verdict == SM_SPECULATED);

  /* The decision must not depend on the seed. Only the value may. */
  {
    int all_speculated = 1;
    (void)sm_init(&p, 5u);
    (void)sm_eliminate(&p, 0u);
    (void)sm_eliminate(&p, 1u);
    (void)sm_eliminate(&p, 2u);
    for (i = 0u; i < 200u; i++) {
      st = sm_decide(&p, 0.5, (uint64_t)i, SM_INTENSITY_SUPPORT, 0u, &r);
      if (st != SM_OK || r.verdict != SM_SPECULATED) {
        all_speculated = 0;
      }
    }
    check("the decision to speculate is seed-independent", all_speculated);
  }

  /* Determinism: same seed, same value, every time. */
  {
    sm_result_t r1;
    sm_result_t r2;
    (void)sm_decide(&p, 0.5, 42u, SM_INTENSITY_SUPPORT, 0u, &r1);
    (void)sm_decide(&p, 0.5, 42u, SM_INTENSITY_SUPPORT, 0u, &r2);
    check("same seed reproduces the same guess", r1.value == r2.value);
  }

  /* Uniformity: the guess must not be weighted toward any value. Python
     measured 0.500 over 6000 trials; here the domain has 2 survivors and
     the tolerance is loose enough for 4000 draws. */
  {
    unsigned counts[SM_MAX_DOMAIN];
    unsigned trials = 4000u;
    unsigned survivors;
    double expected;
    int within = 1;

    for (i = 0u; i < SM_MAX_DOMAIN; i++) {
      counts[i] = 0u;
    }
    for (i = 0u; i < trials; i++) {
      if (sm_decide(&p, 0.5, (uint64_t)i, SM_INTENSITY_SUPPORT, 0u, &r) == SM_OK
          && r.verdict == SM_SPECULATED) {
        counts[r.value]++;
      }
    }
    survivors = sm_count(&p);
    expected = (double)trials / (double)survivors;
    for (i = 0u; i < SM_MAX_DOMAIN; i++) {
      if (counts[i] != 0u) {
        double ratio = (double)counts[i] / expected;
        if (ratio < 0.94 || ratio > 1.06) {
          within = 0;
        }
      }
    }
    check("the guess is uniform over what remains, not weighted", within);
  }

  printf("\ncomposition: removed, and why\n");

  /* sm_compose_and() used to be exercised here. It is gone: it took two
     sm_result_t, so its inputs were the two answer sets, and
     verify_reason.py section 4 exhibits three composites with one operand
     signature and three different verdicts. No such function exists.
     sr_ask2() in smarsh_reason.h replaces it by composing the questions
     instead of the answers. What is kept below is the ancestry algebra,
     which was right and is still used. */

  printf("\nancestry as a set of guesses, not a running sum\n");

  {
    sm_ancestry_t one;
    sm_ancestry_t twice;
    sm_ancestry_t other;
    sm_ancestry_t joined;
    sm_ancestry_t deep;
    unsigned i;

    sm_ancestry_clear(&one);
    (void)sm_ancestry_add(&one, 5u, 2u);
    check("a single 1-bit guess costs 1.0 bit",
          close_to(sm_ancestry_bits(&one), 1.0));

    sm_ancestry_union(&one, &one, &twice);
    check("unioning a guess with itself does not double it",
          close_to(sm_ancestry_bits(&twice), 1.0));

    sm_ancestry_clear(&other);
    (void)sm_ancestry_add(&other, 6u, 2u);
    sm_ancestry_union(&one, &other, &joined);
    check("a genuinely different guess does add",
          close_to(sm_ancestry_bits(&joined), 2.0));

    /* The diamond: one guess, two paths, recombined. This is the bug the
       set representation exists to fix -- a running double reported 2^n
       for a diamond of depth n. */
    sm_ancestry_union(&one, &one, &deep);
    for (i = 0u; i < 5u; i++) {
      sm_ancestry_t next;
      sm_ancestry_union(&deep, &deep, &next);
      deep = next;
    }
    check("a depth-5 diamond does not explode to 2^5 bits",
          close_to(sm_ancestry_bits(&deep), 1.0));

    sm_ancestry_clear(&one);
    (void)sm_ancestry_add(&one, 0u, 4u);
    check("a 4-way guess costs 2.0 bits, not 1",
          close_to(sm_ancestry_bits(&one), 2.0));
  }

  check("a guess_id past SM_MAX_GUESSES is refused",
        sm_decide(&p, 0.5, 1u, SM_INTENSITY_SUPPORT, SM_MAX_GUESSES, &r)
        == SM_ERR_GUESS_ID_OUT_OF_RANGE);

  printf("\nchecked rejections rather than silent truncation\n");

  check("a domain larger than SM_MAX_DOMAIN is refused",
        sm_init(&p, SM_MAX_DOMAIN + 1u) == SM_ERR_DOMAIN_TOO_LARGE);
  check("an empty domain is refused", sm_init(&p, 0u) == SM_ERR_EMPTY_DOMAIN);
  (void)sm_init(&p, 4u);
  check("eliminating outside the domain is refused",
        sm_eliminate(&p, 9u) == SM_ERR_INDEX_OUT_OF_DOMAIN);
  check("a tau outside [0,1] is refused",
        sm_decide(&p, 1.5, 0u, SM_INTENSITY_SUPPORT, 0u, &r) == SM_ERR_BAD_THRESHOLD);
  check("a NaN tau is refused rather than passing a comparison",
        sm_decide(&p, (double)NAN, 0u, SM_INTENSITY_SUPPORT, 0u, &r) == SM_ERR_BAD_THRESHOLD);

  printf("\n");
  if (failures != 0) {
    printf("%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
