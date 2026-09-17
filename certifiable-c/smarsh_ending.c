/*
 * smarsh_ending.c -- see smarsh_ending.h.
 */
#include "smarsh_ending.h"

#include <string.h>
#include <stdlib.h>

static const char *ATOM_NAME[EN_ATOMS + 1u] = {
  "ONTO", "LAST", "GONE", "POINT", "MATCH", "ONTO_R", "LAST_R", "GONE_R", "POINT_R",
  "PSIZE", "PNEW", "ACT", "TOUCH", "TOUCH_R", "PAT", "BODYAT", "ALIGN", "EDGE", ""
};

static int by_role(en_atom_t a) {
  return (a >= EN_ONTO_R && a <= EN_POINT_R) || a == EN_TOUCH_R;
}

static unsigned atom_values(en_atom_t a) {
  if (a == EN_MATCH || a == EN_PNEW || a == EN_EDGE || a == EN_NONE) return 1u;
  if (a == EN_PAT || a == EN_BODYAT) return EN_WHERE;
  if (a == EN_PSIZE) return EN_SIZES;
  if (a == EN_ACT) return EN_ACTS;
  return by_role(a) ? EN_ROLES : EN_COLOURS;
}

/* is the atom, with colour v, true of this act? */
static int atom_holds(en_atom_t a, unsigned v, const en_obs_t *o) {
  switch (a) {
    case EN_ONTO: return (o->onto >> v) & 1u;
    case EN_LAST: return (o->last >> v) & 1u;
    case EN_GONE: return (o->gone >> v) & 1u;
    case EN_POINT: return (o->point >> v) & 1u;
    case EN_MATCH: return o->match != 0u;
    case EN_ONTO_R: return (o->onto_r >> v) & 1u;
    case EN_LAST_R: return (o->last_r >> v) & 1u;
    case EN_GONE_R: return (o->gone_r >> v) & 1u;
    case EN_POINT_R: return (o->point_r >> v) & 1u;
    case EN_PSIZE: return (o->psize >> v) & 1u;
    case EN_PNEW: return o->pnew != 0u;
    case EN_ACT: return (o->act >> v) & 1u;
    case EN_TOUCH: return (o->touch >> v) & 1u;
    case EN_TOUCH_R: return (o->touch_r >> v) & 1u;
    case EN_PAT: return (o->pat >> v) & 1u;
    case EN_BODYAT: return (o->bodyat >> v) & 1u;
    case EN_ALIGN: return (o->align >> v) & 1u;
    case EN_EDGE: return o->edge != 0u;
    default: return 1;
  }
}

static void apply(en_family_t *f, const en_obs_t *o) {
  unsigned va = atom_values(f->a), vb = atom_values(f->b), x, y;
  int any_held = 0;
  for (x = 0u; x < va; x++) {
    int ha = atom_holds(f->a, x, o);
    for (y = 0u; y < vb; y++) {
      unsigned idx = x * EN_COLOURS + y;
      int holds;
      if (!sm_is_possible(&f->dom, idx)) continue;
      holds = ha && atom_holds(f->b, y, o);
      if (o->ended ? !holds : holds) {
        (void)sm_eliminate(&f->dom, idx);
      } else if (o->ended) {
        any_held = 1;
      }
    }
  }
  if (o->ended && any_held) f->survived_ending = 1;
}

static int has_family(const en_theory_t *t, en_atom_t a, en_atom_t b) {
  unsigned i;
  for (i = 0u; i < t->n_fam; i++) {
    if (t->fam[i].a == a && t->fam[i].b == b) return 1;
  }
  return 0;
}

static en_family_t *formulate(en_theory_t *t, en_atom_t a, en_atom_t b, int from_library) {
  en_family_t *f;
  unsigned va = atom_values(a), vb = atom_values(b), x, y, i;
  if (has_family(t, a, b) || t->n_fam >= EN_MAX_FAMILIES) return 0;
  f = &t->fam[t->n_fam++];
  memset(f, 0, sizeof *f);
  f->a = a;
  f->b = b;
  f->from_library = from_library;
  (void)sm_init(&f->dom, EN_COLOURS * EN_COLOURS);
  /* only the indices that name real choices are possible */
  for (x = 0u; x < EN_COLOURS; x++) {
    for (y = 0u; y < EN_COLOURS; y++) {
      if (x >= va || y >= vb || (a == b && x >= y)) (void)sm_eliminate(&f->dom, x * EN_COLOURS + y);
    }
  }
  f->size = sm_count(&f->dom);
  /* everything already seen this game is evidence for it too */
  for (i = 0u; i < t->n_log; i++) apply(f, &t->log[i]);
  return f;
}

/* two-atom families, in a fixed order: the shapes one atom could not say */
static void widen(en_theory_t *t) {
  unsigned a, b;
  for (a = 0u; a < EN_ATOMS; a++) {
    for (b = a; b < EN_ATOMS; b++) {
      if (a == b && atom_values((en_atom_t)a) == 1u) continue;   /* says nothing twice */
      (void)formulate(t, (en_atom_t)a, (en_atom_t)b, 0);
    }
  }
  t->depth = 2u;
  t->widenings++;
}

/*
 * Widening a third time.
 *
 * Every theory of one fact is ruled out, and so is every theory of two facts joined.
 * That is the world saying, as plainly as it can, that what ends a level takes more
 * saying than the child has been able to say. It does not give up and it does not
 * guess: it looks at what was true of the acts that did end a level, takes only the
 * facts true of every one of them, and formulates every way of joining three of
 * those. Each is then set against everything it has seen. A theory true of an act
 * that ended nothing is ruled out, exactly as before. What is left is what it has.
 *
 * Only facts true of every ending are used, so this is not a search for something
 * that fits: it is the same elimination, over a language one word longer.
 */
static void widen_deep(en_theory_t *t) {
  unsigned char fa[64], fv[64];
  unsigned n_f = 0u, a, v, i, x, y, z;
  t->depth = 3u;
  t->widenings++;
  t->n_deep = 0u;
  t->deep_tried = 0u;
  for (a = 0u; a < EN_ATOMS && n_f < 64u; a++) {
    for (v = 0u; v < atom_values((en_atom_t)a) && n_f < 64u; v++) {
      int every = 1;
      unsigned seen = 0u;
      for (i = 0u; i < t->n_log; i++) {
        if (!t->log[i].ended) continue;
        seen++;
        if (!atom_holds((en_atom_t)a, v, &t->log[i])) {
          every = 0;
          break;
        }
      }
      if (every && seen > 0u) {
        fa[n_f] = (unsigned char)a;
        fv[n_f] = (unsigned char)v;
        n_f++;
      }
    }
  }
  for (x = 0u; x + 2u < n_f; x++) {
    for (y = x + 1u; y + 1u < n_f; y++) {
      for (z = y + 1u; z < n_f; z++) {
        int killed = 0;
        if (t->n_deep >= EN_DEEP_MAX) return;
        t->deep_tried++;
        for (i = 0u; i < t->n_log; i++) {
          const en_obs_t *o = &t->log[i];
          if (o->ended) continue;
          if (atom_holds((en_atom_t)fa[x], fv[x], o) &&
              atom_holds((en_atom_t)fa[y], fv[y], o) &&
              atom_holds((en_atom_t)fa[z], fv[z], o)) {
            killed = 1;   /* true of an act that ended nothing: it is not what ends a level */
            break;
          }
        }
        if (!killed) {
          en_conj_t *c = &t->deep[t->n_deep++];
          c->atom[0] = fa[x];
          c->val[0] = fv[x];
          c->atom[1] = fa[y];
          c->val[1] = fv[y];
          c->atom[2] = fa[z];
          c->val[2] = fv[z];
        }
      }
    }
  }
}

static int all_ruled_out(const en_theory_t *t) {
  unsigned i;
  for (i = 0u; i < t->n_fam; i++) {
    if (sm_count(&t->fam[i].dom) > 0u) return 0;
  }
  return 1;
}

sm_status_t en_begin(en_theory_t *t, const char *library) {
  unsigned a;
  if (t == 0) return SM_ERR_NULL_ARGUMENT;
  t->n_fam = 0u;
  t->depth = 1u;
  t->endings = 0u;
  t->widenings = 0u;
  t->n_log = 0u;
  t->n_deep = 0u;
  t->deep_tried = 0u;
  t->blank_at = 0u;
  t->blank_endings = 0u;
  t->blank_acts = 0u;
  for (a = 0u; a < EN_ATOMS; a++) (void)formulate(t, (en_atom_t)a, EN_NONE, 0);
  if (library != 0) {
    /* families other games kept: "A+B" names */
    unsigned x, y;
    for (x = 0u; x < EN_ATOMS; x++) {
      for (y = x; y < EN_ATOMS; y++) {
        char name[32];
        const char *hit;
        unsigned len;
        if (x == EN_MATCH && y == EN_MATCH) continue;
        len = (unsigned)(strlen(ATOM_NAME[x]) + 1u + strlen(ATOM_NAME[y]));
        strcpy(name, ATOM_NAME[x]);
        strcat(name, "+");
        strcat(name, ATOM_NAME[y]);
        for (hit = strstr(library, name); hit != 0; hit = strstr(hit + 1, name)) {
          char after = hit[len];
          if ((hit == library || hit[-1] == ',') && (after == '\0' || after == ',')) {
            en_family_t *f = formulate(t, (en_atom_t)x, (en_atom_t)y, 1);
            (void)f;
            break;
          }
        }
      }
    }
    for (x = 0u; x < t->n_fam; x++) {   /* singles named in the library are marked as kept */
      char name[32];
      const char *hit;
      if (t->fam[x].b != EN_NONE) continue;
      strcpy(name, ATOM_NAME[t->fam[x].a]);
      for (hit = strstr(library, name); hit != 0; hit = strstr(hit + 1, name)) {
        char after = hit[strlen(name)];
        if ((hit == library || hit[-1] == ',') && (after == '\0' || after == ',')) {
          t->fam[x].from_library = 1;
          break;
        }
      }
    }
  }
  return SM_OK;
}

sm_status_t en_observe(en_theory_t *t, const en_obs_t *o) {
  unsigned i;
  if (t == 0 || o == 0) return SM_ERR_NULL_ARGUMENT;
  if (t->n_log < EN_LOG) t->log[t->n_log++] = *o;
  if (o->ended) t->endings++;
  if (o->ended && getenv("CIALL_ENDDBG")) {
    unsigned alive = 0u, fams = 0u;
    for (i = 0u; i < t->n_fam; i++) {
      unsigned c = sm_count(&t->fam[i].dom);
      alive += c;
      if (c > 0u) fams++;
    }
    fprintf(stderr, "ENDING after %u acts: alive %u in %u families; onto=%04x last=%04x gone=%04x point=%04x match=%u onto_r=%04x gone_r=%04x point_r=%04x act=%04x psize=%04x pnew=%u touch=%04x touch_r=%04x\n",
            t->n_log, alive, fams, o->onto, o->last, o->gone, o->point, o->match, o->onto_r, o->gone_r, o->point_r, o->act, o->psize, o->pnew, o->touch, o->touch_r);
  }
  {
    /*
     * Every act, written down as the facts that were true of it, so the child (and
     * whoever reads it) can see for itself whether any pair of facts tells the act
     * that ended a level apart from the ones that ended nothing. When no pair does,
     * the language is short of a word, and this says which.
     */
    const char *dump = getenv("CIALL_ENDDUMP");
    if (dump != 0) {
      FILE *df = fopen(dump, "a");
      if (df != 0) {
        fprintf(df, "%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n", o->ended,
                o->onto, o->last, o->gone, o->point, o->match, o->onto_r, o->last_r,
                o->gone_r, o->point_r, o->act, o->psize, o->pnew, o->touch, o->one, o->few, o->most, o->grew, o->shrank, o->a_gone, o->a_one, o->a_few, o->a_most, o->bodyat, o->pat, o->align, o->edge);
        fclose(df);
      }
    }
  }
  for (i = 0u; i < t->n_fam; i++) apply(&t->fam[i], o);
  if (o->ended && getenv("CIALL_ENDDBG")) {
    unsigned k;
    for (k = 0u; k < t->n_fam; k++) {
      char nm[32];
      if (sm_count(&t->fam[k].dom) == 0u) continue;
      fprintf(stderr, "   survived: %s  %u left%s\n", en_family_name(&t->fam[k], nm, sizeof nm),
              sm_count(&t->fam[k].dom), t->fam[k].survived_ending ? " (held)" : "");
    }
  }
  if (all_ruled_out(t) && t->depth < 2u) widen(t);   /* its language was too poor: it widens it */
  /* two facts joined were not enough either, and it has an ending to work from */
  if (all_ruled_out(t) && t->depth == 2u && t->endings > 0u) widen_deep(t);
  /* a theory of three facts can be ruled out later, like any other */
  if (t->n_deep > 0u && !o->ended) {
    unsigned k = 0u, w = 0u;
    for (; k < t->n_deep; k++) {
      const en_conj_t *c = &t->deep[k];
      if (atom_holds((en_atom_t)c->atom[0], c->val[0], o) &&
          atom_holds((en_atom_t)c->atom[1], c->val[1], o) &&
          atom_holds((en_atom_t)c->atom[2], c->val[2], o)) {
        continue;
      }
      t->deep[w++] = *c;
    }
    t->n_deep = w;
  }
  if (all_ruled_out(t) && t->n_deep == 0u) {
    if (t->blank_at == 0u) {
      t->blank_at = t->n_log;
      t->blank_endings = t->endings;
    }
    t->blank_acts++;
  }
  return SM_OK;
}

sm_status_t en_aim(const en_theory_t *t, uint16_t *onto, uint16_t *gone, uint16_t *point, int *match,
                   uint16_t *onto_role, uint16_t *gone_role, uint16_t *point_role) {
  unsigned i, x, y;
  if (t == 0 || onto == 0 || gone == 0 || point == 0 || match == 0 ||
      onto_role == 0 || gone_role == 0 || point_role == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  *onto = *gone = *point = 0u;
  *onto_role = *gone_role = *point_role = 0u;
  *match = 0;
  /* what it settled on when two facts joined were not enough: those say where to go too */
  for (i = 0u; i < t->n_deep; i++) {
    unsigned k;
    for (k = 0u; k < EN_DEEP_FACTS; k++) {
      en_atom_t at = (en_atom_t)t->deep[i].atom[k];
      unsigned v = t->deep[i].val[k];
      if (at == EN_ONTO || at == EN_LAST || at == EN_TOUCH) *onto |= (uint16_t)(1u << v);
      else if (at == EN_GONE) *gone |= (uint16_t)(1u << v);
      else if (at == EN_POINT) *point |= (uint16_t)(1u << v);
      else if (at == EN_MATCH) *match = 1;
      else if (at == EN_ONTO_R || at == EN_LAST_R || at == EN_TOUCH_R) *onto_role |= (uint16_t)(1u << v);
      else if (at == EN_GONE_R) *gone_role |= (uint16_t)(1u << v);
      else if (at == EN_POINT_R) *point_role |= (uint16_t)(1u << v);
    }
  }
  for (i = 0u; i < t->n_fam; i++) {
    const en_family_t *f = &t->fam[i];
    unsigned left = sm_count(&f->dom);
    if (left == 0u) continue;
    /* earned: an ending held for it here, or another game kept it; and it has narrowed */
    if (!(f->survived_ending || f->from_library)) continue;
    if (left == f->size && !f->survived_ending) continue;
    for (x = 0u; x < atom_values(f->a); x++) {
      for (y = 0u; y < atom_values(f->b); y++) {
        unsigned k;
        if (!sm_is_possible(&f->dom, x * EN_COLOURS + y)) continue;
        for (k = 0u; k < 2u; k++) {
          en_atom_t at = k == 0u ? f->a : f->b;
          unsigned v = k == 0u ? x : y;
          if (at == EN_ONTO || at == EN_LAST || at == EN_TOUCH) *onto |= (uint16_t)(1u << v);
          else if (at == EN_GONE) *gone |= (uint16_t)(1u << v);
          else if (at == EN_POINT) *point |= (uint16_t)(1u << v);
          else if (at == EN_MATCH) *match = 1;
          else if (at == EN_ONTO_R || at == EN_LAST_R || at == EN_TOUCH_R) *onto_role |= (uint16_t)(1u << v);
          else if (at == EN_GONE_R) *gone_role |= (uint16_t)(1u << v);
          else if (at == EN_POINT_R) *point_role |= (uint16_t)(1u << v);
        }
      }
    }
  }
  return SM_OK;
}

const char *en_family_name(const en_family_t *f, char *buf, unsigned cap) {
  if (f == 0 || buf == 0 || cap < 16u) return "";
  strcpy(buf, ATOM_NAME[f->a]);
  if (f->b != EN_NONE) {
    strcat(buf, "+");
    strcat(buf, ATOM_NAME[f->b]);
  }
  return buf;
}

sm_status_t en_report(const en_theory_t *t, FILE *out) {
  unsigned i;
  if (t == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  if (t->blank_at > 0u) {
    fprintf(out, "  it ran out of things it could say after %u acts, with %u ending%s seen,"
                 " and went on for %u more acts with nothing left to say\n",
            t->blank_at, t->blank_endings, t->blank_endings == 1u ? "" : "s", t->blank_acts);
  }
  if (t->deep_tried > 0u) {
    unsigned k, j;
    fprintf(out, "  one fact was not enough and two joined were not enough either, so it said it in three:"
                 " it weighed %u such theories against everything it had seen, and %u are left\n",
            t->deep_tried, t->n_deep);
    for (k = 0u; k < t->n_deep && k < 4u; k++) {
      fprintf(out, "    ");
      for (j = 0u; j < EN_DEEP_FACTS; j++) {
        fprintf(out, "%s%s(%u)", j > 0u ? " & " : "", ATOM_NAME[t->deep[k].atom[j]], t->deep[k].val[j]);
      }
      fprintf(out, "\n");
    }
  }
  fprintf(out, "  what ends a level, by elimination: %u ending%s seen; language %s%s\n", t->endings,
          t->endings == 1u ? "" : "s", t->depth == 1u ? "of single facts" : "widened by itself to pairs of facts",
          t->widenings > 0u ? " (every single fact had been ruled out)" : "");
  for (i = 0u; i < t->n_fam; i++) {
    const en_family_t *f = &t->fam[i];
    char name[32];
    unsigned left = sm_count(&f->dom), shown = 0u, x, y;
    if (left == 0u || !f->survived_ending) continue;
    fprintf(out, "    %s%s: %u of %u theories remain;", en_family_name(f, name, sizeof name),
            f->from_library ? " (kept from other games)" : "", left, f->size);
    for (x = 0u; x < atom_values(f->a) && shown < 4u; x++) {
      for (y = 0u; y < atom_values(f->b) && shown < 4u; y++) {
        if (!sm_is_possible(&f->dom, x * EN_COLOURS + y)) continue;
        fprintf(out, " %s(%u)", ATOM_NAME[f->a], x);
        if (f->b != EN_NONE) fprintf(out, "&%s(%u)", ATOM_NAME[f->b], y);
        shown++;
      }
    }
    fprintf(out, "\n");
  }
  return SM_OK;
}
