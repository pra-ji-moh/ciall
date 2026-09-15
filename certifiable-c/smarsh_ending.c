/*
 * smarsh_ending.c -- see smarsh_ending.h.
 */
#include "smarsh_ending.h"

#include <string.h>

static const char *ATOM_NAME[EN_ATOMS + 1u] = {
  "ONTO", "LAST", "GONE", "POINT", "MATCH", "ONTO_R", "LAST_R", "GONE_R", "POINT_R", ""
};

static int by_role(en_atom_t a) {
  return a >= EN_ONTO_R && a <= EN_POINT_R;
}

static unsigned atom_values(en_atom_t a) {
  if (a == EN_MATCH || a == EN_NONE) return 1u;
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
      if (a == EN_MATCH && b == EN_MATCH) continue;
      (void)formulate(t, (en_atom_t)a, (en_atom_t)b, 0);
    }
  }
  t->depth = 2u;
  t->widenings++;
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
  for (i = 0u; i < t->n_fam; i++) apply(&t->fam[i], o);
  if (all_ruled_out(t) && t->depth < 2u) widen(t);   /* its language was too poor: it widens it */
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
          if (at == EN_ONTO || at == EN_LAST) *onto |= (uint16_t)(1u << v);
          else if (at == EN_GONE) *gone |= (uint16_t)(1u << v);
          else if (at == EN_POINT) *point |= (uint16_t)(1u << v);
          else if (at == EN_MATCH) *match = 1;
          else if (at == EN_ONTO_R || at == EN_LAST_R) *onto_role |= (uint16_t)(1u << v);
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
