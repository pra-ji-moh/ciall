/*
 * demo_abstract.c -- it is given raw attributes with no meaning and what
 * happened, and it builds the questions, and so the worlds, itself.
 *
 * In demo_ladder.c counting was a given: "count pile 1". Here nothing is
 * given but pegs and lamps, on or off. Counting appears because the
 * outcome does not care WHICH pegs hold weights, only how many: symmetry
 * first, number second.
 */

#include <stdio.h>
#include <string.h>

#include "smarsh_abstract.h"

static ab_raw_t RAW;
static ab_result_t AB;
static sc_discovery_t D;

static void report(void) {
  unsigned i, g;
  static const char *rel[4] = {"undetermined (kept, to be safe)", "matters", "never matters (proved)",
                               "never seen to matter (dropped: a conjecture)"};
  for (i = 0u; i < RAW.n_raw; i++) printf("    %-18s %s\n", RAW.name[i], rel[AB.relevance[i]]);
  if (!AB.any_symmetry) {
    printf("  no two attributes can be swapped without changing what happens, so\n");
    printf("  there is nothing to count. Every raw distinction matters on its own.\n");
  }
  for (g = 0u; g < AB.n_groups; g++) {
    unsigned first = 1u;
    printf("  %s: {", AB.group_size[g] < 2u ? "on its own" :
                      (AB.group_proved[g] ? "interchangeable" : "interchangeable (conjectured)"));
    for (i = 0u; i < RAW.n_raw; i++) {
      if (AB.group_members[g] & (1u << i)) { printf("%s%s", first ? "" : ", ", RAW.name[i]); first = 0u; }
    }
    if (AB.group_size[g] > 1u) printf("}  -> invents: how many of these are on (0 to %u)\n", AB.group_size[g]);
    else printf("}\n");
  }
  printf("  worlds: %u raw situations seen, %u worlds in what it built;", AB.raw_worlds, AB.built_worlds);
  printf(" its questions %s the outcome\n", AB.determined ? "DETERMINE" : "do NOT determine");
}

/* ---- a balance: three pegs each side, and the colour of the beam ------ */
static unsigned balance(unsigned s) {
  unsigned l = (s & 1u) + ((s >> 1) & 1u) + ((s >> 2) & 1u);
  unsigned r = ((s >> 3) & 1u) + ((s >> 4) & 1u) + ((s >> 5) & 1u);
  return l > r ? 0u : (l == r ? 1u : 2u);
}

int main(void) {
  unsigned s, a, b, i;
  static const char *peg[7] = {"left peg 1", "left peg 2", "left peg 3", "right peg 1",
                               "right peg 2", "right peg 3", "beam painted red"};
  static const char *lamp[6] = {"lamp 1", "lamp 2", "lamp 3", "lamp 4", "lamp 5", "lamp 6"};
  static const char *sw[3] = {"switch 1", "switch 2", "switch 3"};

  printf("BUILDING ITS OWN WORLDS FROM RAW OBSERVATION\n\n");

  printf("A BALANCE. Seven raw yes/no attributes: a weight on each of six pegs,\n");
  printf("and whether the beam is painted red. It sees all 128 situations and\n");
  printf("which way the beam goes. Nothing says what \"left\" or \"count\" means.\n");
  ab_clear(&RAW, 7u, 3u);
  for (i = 0u; i < 7u; i++) ab_name(&RAW, i, peg[i]);
  for (s = 0u; s < 128u; s++) ab_observe(&RAW, s, balance(s));
  ab_abstract(&RAW, &AB);
  report();
  if (AB.determined && AB.n_groups == 2u) {
    static const char sym[3] = {'L', '=', 'R'};
    sc_discover(AB.question, 2u, &AB.outcome_q, AB.n_situations, &D);
    printf("  and then, over its own questions, the rule (rows: how many in the first\n");
    printf("  group, columns: how many in the second; L tips left, R tips right):\n");
    for (a = 0u; a < 4u; a++) {
      printf("      ");
      for (b = 0u; b < 4u; b++) printf(" %c", sym[sc_value(&D.op, a, b)]);
      printf("\n");
    }
    printf("  It found \"more\" as well as \"how many\", and the paint never mattered.\n");
  }

  printf("\nSIX LAMPS, and a chime that rings when an even number are lit\n");
  ab_clear(&RAW, 6u, 2u);
  for (i = 0u; i < 6u; i++) ab_name(&RAW, i, lamp[i]);
  for (s = 0u; s < 64u; s++) {
    unsigned c = 0u, k;
    for (k = 0u; k < 6u; k++) c += (s >> k) & 1u;
    ab_observe(&RAW, s, c % 2u == 0u);
  }
  ab_abstract(&RAW, &AB);
  report();
  sc_discover(AB.question, 1u, &AB.outcome_q, AB.n_situations, &D);
  printf("  the chime, by how many are lit:");
  for (a = 0u; a <= 6u; a++) printf(" %u:%s", a, sc_value(&D.op, a, 0u) ? "rings" : "-");
  printf("\n  64 raw situations became 7 worlds, and \"even\" is a fact about those 7.\n");

  printf("\nTHE BALANCE AGAIN, but it only ever sees 90 of the 128 situations\n");
  ab_clear(&RAW, 7u, 3u);
  for (i = 0u; i < 7u; i++) ab_name(&RAW, i, peg[i]);
  for (s = 0u; s < 128u; s++) if ((s * 37u) % 128u < 90u) ab_observe(&RAW, s, balance(s));
  ab_abstract(&RAW, &AB);
  report();
  {
    unsigned right = 0u, wrong = 0u, none = 0u, o;
    for (s = 0u; s < 128u; s++) {
      if (RAW.observed[s]) continue;
      if (ab_predict(&RAW, &AB, s, &o) == AB_INFERRED) {
        if (o == balance(s)) right++;
        else wrong++;
      } else {
        none++;
      }
    }
    printf("  the %u situations it never saw, through the worlds it built:\n", right + wrong + none);
    printf("    %u inferred; checked against what really happens: %u right, %u wrong\n",
           right + wrong, right, wrong);
    printf("    %u with no grounds (no seen situation in the same world): no answer\n", none);
    printf("  Each inference rests on the conjectures above and says so. It got from\n");
    printf("  90 cases to the ones it never saw by finding WHAT to generalise over.\n");
  }

  printf("\nTHREE SWITCHES, and a light that comes on when they spell a prime\n");
  printf("number in binary (switch 1 worth 4, switch 2 worth 2, switch 3 worth 1)\n");
  ab_clear(&RAW, 3u, 2u);
  for (i = 0u; i < 3u; i++) ab_name(&RAW, i, sw[i]);
  for (s = 0u; s < 8u; s++) {
    unsigned v = ((s & 1u) << 2) | (s & 2u) | ((s >> 2) & 1u);
    ab_observe(&RAW, s, v == 2u || v == 3u || v == 5u || v == 7u);
  }
  ab_abstract(&RAW, &AB);
  report();
  printf("  Place value is an abstraction, but not one that comes from symmetry,\n");
  printf("  and this stage only finds those. It says so instead of forcing one.\n");
  return 0;
}
