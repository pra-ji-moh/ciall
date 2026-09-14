/*
 * demo_analogy.c -- an engineering question comes in, and it reaches into
 * philosophy, law or medicine on its own, because the shape matches.
 *
 * The library holds things met before, from many domains, each one a
 * question and the factors that decide it. Nothing links them. Given a
 * new question, it searches all of it: invariants first, which drops most
 * of the library without a search, then an exact renaming, which is the
 * proof. What was known about the old question is carried across in the
 * new question's own words and re-checked there.
 */

#include <stdio.h>

#include "smarsh_analogy.h"

static sa_library_t LIB;
static sa_struct_t Q;
static sa_found_t F;

/* ---- things met before ---------------------------------------------- */
static unsigned permissible(const unsigned *v) { return v[0] && (v[1] || v[2]); }
static unsigned guilty(const unsigned *v) { return v[0] && v[1] && !v[2]; }
static unsigned expressed(const unsigned *v) { return v[0] && !v[1]; }
static unsigned majority(const unsigned *v) { return v[0] + v[1] + v[2] >= 2u; }
static unsigned light(const unsigned *v) { return v[0] != v[1]; }
static unsigned all3(const unsigned *v) { return v[0] && v[1] && v[2]; }
static unsigned all2(const unsigned *v) { return v[0] && v[1]; }
static unsigned treat(const unsigned *v) { return v[0] == 2u || (v[0] == 1u && v[1]); }

/* ---- questions that come in ------------------------------------------ */
static unsigned may_run(const unsigned *v) { return v[0] && (v[1] || v[2]); }
static unsigned alarm(const unsigned *v) { return v[0] + v[1] + v[2] >= 2u; }
static unsigned valve(const unsigned *v) { return v[0] == 2u || (v[0] == 1u && v[1]); }
static unsigned pump(const unsigned *v) { return (v[0] && !v[1]) || (v[2] && v[1]); }
static unsigned breaker(const unsigned *v) { return !v[0] && !v[1]; }

static const unsigned B2[2] = {2u, 2u}, B3[3] = {2u, 2u, 2u}, L3B[2] = {3u, 2u};

static void add(const char *name, const char *domain, unsigned n, const char *const *f,
                const unsigned *d, unsigned (*fn)(const unsigned *)) {
  sa_struct_t s;
  sa_define(&s, name, domain, n, f, d, 2u, fn);
  sa_add(&LIB, &s);
}

static const char *yes(unsigned v, unsigned dom) {
  static const char *lv[3] = {"low", "middle", "high"};
  if (dom == 3u) return lv[v];
  return v ? "yes" : "no";
}

static void say(const sa_struct_t *s, const sa_prop_t *p) {
  if (p->kind == SA_FORCES) {
    printf("if \"%s\" is %s, then \"%s\" is %s whatever else holds", s->factor[p->factor],
           yes(p->value, s->dom[p->factor]), s->name, yes(p->answer, 2u));
  } else if (p->kind == SA_IRRELEVANT) {
    printf("\"%s\" never matters to \"%s\"", s->factor[p->factor], s->name);
  } else {
    printf("\"%s\" and \"%s\" play exactly the same role", s->factor[p->factor], s->factor[p->other]);
  }
}

static void ask(const char *name, unsigned n, const char *const *f, const unsigned *d,
                unsigned (*fn)(const unsigned *)) {
  unsigned i, k, np;
  sa_prop_t props[SA_MAX_PROPS];
  sa_define(&Q, name, "engineering", n, f, d, 2u, fn);
  sa_find(&LIB, &Q, &F);
  printf("\nQUESTION: when does \"%s\"?\n", name);
  printf("  all %u things it knows: %u ruled out by invariants alone, without a search;\n",
         LIB.n, F.filtered);
  printf("  %u searched exactly: %u the same shape, %u not the same shape after all\n",
         LIB.n - F.filtered, F.n_matches, F.searched_no_match);
  if (F.n_matches == 0u) {
    printf("  NOTHING it has met has this shape. It says so, rather than reaching for\n");
    printf("  the nearest thing; a loose analogy is exactly what this refuses to make.\n");
    return;
  }
  for (k = 0u; k < F.n_matches; k++) {
    const sa_struct_t *b = &LIB.s[F.match[k]];
    const sa_map_t *m = &F.map[k];
    printf("\n  SAME SHAPE AS \"%s\" (%s)   proof re-checked on every case: %s\n", b->name,
           b->domain, sa_check_map(&Q, b, m) ? "yes" : "NO");
    for (i = 0u; i < n; i++) {
      unsigned v, j = m->factor_to[i];
      printf("    %-22s <->  %-22s", Q.factor[i], b->factor[j]);
      for (v = 0u; v < Q.dom[i]; v++) {
        printf("  %s=%s", yes(v, Q.dom[i]), yes(m->value_to[i][v], b->dom[j]));
      }
      printf("\n");
    }
    printf("    what it knew there, said here, and checked here:\n");
    np = sa_properties(b, props, SA_MAX_PROPS);
    for (i = 0u; i < np; i++) {
      sa_prop_t here;
      printf("      ");
      if (!sa_transfer(m, &Q, b, &props[i], &here)) {
        say(b, &props[i]);
        printf("\n        (does not translate through this renaming)\n");
        continue;
      }
      say(&Q, &here);
      printf("\n        (from: ");
      say(b, &props[i]);
      printf(") %s\n", sa_holds(&Q, &here) ? "holds" : "FAILS HERE");
    }
  }
}

int main(void) {
  static const char *f_perm[3] = {"consent given", "harm avoided", "harm outweighed"};
  static const char *f_guilt[3] = {"act committed", "intent present", "excuse exists"};
  static const char *f_gene[2] = {"activator bound", "repressor bound"};
  static const char *f_jury[3] = {"juror 1 convinced", "juror 2 convinced", "juror 3 convinced"};
  static const char *f_light[2] = {"top switch up", "bottom switch up"};
  static const char *f_trade[3] = {"buyer willing", "seller willing", "price agreed"};
  static const char *f_know[3] = {"believed", "true", "justified"};
  static const char *f_sound[2] = {"argument valid", "premises true"};
  static const char *f_treat[2] = {"symptoms", "test positive"};
  static const char *f_run[3] = {"guard closed", "operator present", "remote override"};
  static const char *f_alarm[3] = {"sensor A tripped", "sensor B tripped", "sensor C tripped"};
  static const char *f_valve[2] = {"pressure", "manual release"};
  static const char *f_pump[3] = {"tank low", "float stuck", "manual start"};
  static const char *f_break[2] = {"overload", "manual trip"};

  add("the act is permissible", "philosophy", 3u, f_perm, B3, permissible);
  add("the verdict is guilty", "law", 3u, f_guilt, B3, guilty);
  add("the gene is expressed", "biology", 2u, f_gene, B2, expressed);
  add("a jury of three convicts", "law", 3u, f_jury, B3, majority);
  add("the hallway light is on", "physics", 2u, f_light, B2, light);
  add("a trade happens", "economics", 3u, f_trade, B3, all3);
  add("it counts as knowledge", "philosophy", 3u, f_know, B3, all3);
  add("the argument is sound", "logic", 2u, f_sound, B2, all2);
  add("treat now", "medicine", 2u, f_treat, L3B, treat);

  printf("ANALOGY BY STRUCTURE, FOUND AND PROVED\n");
  printf("\nIt has met %u things before, in philosophy, law, biology, physics,\n", LIB.n);
  printf("economics, logic and medicine. Nothing tells it which one is relevant.\n");

  ask("the machine may run", 3u, f_run, B3, may_run);
  ask("the alarm sounds", 3u, f_alarm, B3, alarm);
  ask("the relief valve opens", 2u, f_valve, L3B, valve);
  ask("the breaker stays closed", 2u, f_break, B2, breaker);
  ask("the pump runs", 3u, f_pump, B3, pump);

  printf("\nWHAT THIS DOES NOT DO YET\n");
  printf("  Each problem arrives already stated as a question and its factors,\n");
  printf("  and each thing in the library was stated the same way. Turning a raw\n");
  printf("  situation into that form by itself is the stage still to come.\n");
  return 0;
}
