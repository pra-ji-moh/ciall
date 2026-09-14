/*
 * smarsh_time.c -- see smarsh_time.h.
 */

#include "smarsh_time.h"

#include <string.h>

#define BIT(i) ((tm_set_t)1 << (i))

sm_status_t tm_init(tm_model_t *m, unsigned n_states, unsigned n_actions) {
  if (m == 0) return SM_ERR_NULL_ARGUMENT;
  if (n_states == 0u || n_actions == 0u) return SM_ERR_EMPTY_DOMAIN;
  if (n_states > TM_MAX_STATES || n_actions > TM_MAX_ACTIONS) return SM_ERR_DOMAIN_TOO_LARGE;
  memset(m, 0, sizeof *m);
  m->n_states = n_states;
  m->n_actions = n_actions;
  return SM_OK;
}

tm_set_t tm_all(const tm_model_t *m) {
  if (m == 0) return 0u;
  return m->n_states >= 64u ? ~(tm_set_t)0 : BIT(m->n_states) - 1u;
}

sm_status_t tm_watch(tm_model_t *m, unsigned state, unsigned action, unsigned next) {
  if (m == 0) return SM_ERR_NULL_ARGUMENT;
  if (state >= m->n_states || next >= m->n_states || action >= m->n_actions) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  m->next[action][state] |= BIT(next);
  m->seen[action] |= BIT(state);
  return SM_OK;
}

tm_set_t tm_step(const tm_model_t *m, tm_set_t now, unsigned action, unsigned *unseen) {
  tm_set_t out = 0u;
  unsigned s, missing = 0u;
  if (m == 0 || action >= m->n_actions) return 0u;
  for (s = 0u; s < TM_MAX_STATES; s++) {
    if (s >= m->n_states) break;
    if (!(now & BIT(s))) continue;
    if (m->seen[action] & BIT(s)) out |= m->next[action][s];
    else { missing++; out = tm_all(m); }
  }
  if (unseen != 0) *unseen = missing;
  return out & tm_all(m);
}

tm_set_t tm_sense(const tm_model_t *m, tm_set_t now, unsigned reading) {
  tm_set_t out = 0u;
  unsigned s;
  if (m == 0) return 0u;
  for (s = 0u; s < TM_MAX_STATES; s++) {
    if (s >= m->n_states) break;
    if ((now & BIT(s)) && m->sensor[s] == reading) out |= BIT(s);
  }
  return out;
}

/* ---- planning: breadth-first over sets of possible states ------------- */

typedef struct {
  tm_set_t set;
  unsigned parent;
  unsigned action;
  unsigned depth;
} node_t;

static node_t QUEUE[TM_MAX_BELIEFS];   /* static: 4096 nodes, not on the stack */

sm_status_t tm_plan(const tm_model_t *m, tm_set_t from, tm_set_t goal, tm_plan_t *out) {
  unsigned head = 0u, tail = 0u, a, i;
  if (m == 0 || out == 0) return SM_ERR_NULL_ARGUMENT;
  memset(out, 0, sizeof *out);
  from &= tm_all(m);
  if (from == 0u) return SM_ERR_EMPTY_DOMAIN;
  QUEUE[tail].set = from;
  QUEUE[tail].parent = TM_MAX_BELIEFS;
  QUEUE[tail].depth = 0u;
  tail++;
  while (head < tail) {
    node_t cur = QUEUE[head];
    unsigned here = head++;
    if ((cur.set & ~goal) == 0u) {
      /* reached: walk back to recover the actions */
      unsigned k = cur.depth, n = here;
      out->found = 1;
      out->length = cur.depth;
      while (k > 0u) {
        out->action[--k] = QUEUE[n].action;
        n = QUEUE[n].parent;
      }
      out->explored = tail;
      return SM_OK;
    }
    if (cur.depth >= TM_MAX_PLAN) continue;
    for (a = 0u; a < TM_MAX_ACTIONS; a++) {
      tm_set_t nxt;
      int fresh = 1;
      if (a >= m->n_actions) break;
      if ((cur.set & ~m->seen[a]) != 0u) continue;   /* would rest on an unwatched transition */
      nxt = tm_step(m, cur.set, a, 0);
      for (i = 0u; i < tail; i++) if (QUEUE[i].set == nxt) { fresh = 0; break; }
      if (!fresh) continue;
      if (tail >= TM_MAX_BELIEFS) { out->explored = tail; return SM_ERR_DOMAIN_TOO_LARGE; }
      QUEUE[tail].set = nxt;
      QUEUE[tail].parent = here;
      QUEUE[tail].action = a;
      QUEUE[tail].depth = cur.depth + 1u;
      tail++;
    }
  }
  /* Every set of states reachable from `from` by watched actions was
     considered, and none lies inside the goal: no plan exists. Only a
     plan-length cap could make that incomplete; say so if it was hit. */
  out->explored = tail;
  for (i = 0u; i < tail; i++) if (QUEUE[i].depth >= TM_MAX_PLAN) return SM_OK;
  out->proved_impossible = 1;
  return SM_OK;
}

int tm_check_plan(const tm_model_t *m, tm_set_t from, tm_set_t goal, const tm_plan_t *plan) {
  unsigned k, unseen;
  tm_set_t now = from;
  if (m == 0 || plan == 0 || !plan->found) return 0;
  for (k = 0u; k < plan->length && k < TM_MAX_PLAN; k++) {
    now = tm_step(m, now, plan->action[k], &unseen);
    if (unseen != 0u) return 0;
  }
  return (now & ~goal) == 0u;
}

/* ---- merging states that behave the same ------------------------------ */

sm_status_t tm_merge(const tm_model_t *m, unsigned *cls, unsigned *n_classes) {
  unsigned s, t, a, round, n = 0u;
  unsigned next_cls[TM_MAX_STATES];
  if (m == 0 || cls == 0 || n_classes == 0) return SM_ERR_NULL_ARGUMENT;
  /* start: states apart only if the sensor tells them apart */
  for (s = 0u; s < m->n_states; s++) {
    cls[s] = TM_MAX_STATES;
    for (t = 0u; t < s; t++) if (m->sensor[t] == m->sensor[s]) { cls[s] = cls[t]; break; }
    if (cls[s] == TM_MAX_STATES) cls[s] = n++;
  }
  /* refine: states stay together only if every action leads to the same
     classes, and was watched from both or from neither */
  for (round = 0u; round < TM_MAX_STATES; round++) {
    unsigned m2 = 0u;
    for (s = 0u; s < m->n_states; s++) {
      next_cls[s] = TM_MAX_STATES;
      for (t = 0u; t < s; t++) {
        int same = cls[t] == cls[s];
        for (a = 0u; a < m->n_actions && same; a++) {
          tm_set_t cs = 0u, ct = 0u;
          unsigned u;
          if (((m->seen[a] >> s) & 1u) != ((m->seen[a] >> t) & 1u)) { same = 0; break; }
          for (u = 0u; u < m->n_states; u++) {
            if (m->next[a][s] & BIT(u)) cs |= BIT(cls[u]);
            if (m->next[a][t] & BIT(u)) ct |= BIT(cls[u]);
          }
          if (cs != ct) same = 0;
        }
        if (same) { next_cls[s] = next_cls[t]; break; }
      }
      if (next_cls[s] == TM_MAX_STATES) next_cls[s] = m2++;
    }
    memcpy(cls, next_cls, m->n_states * sizeof *cls);
    if (m2 == n) break;   /* stable */
    n = m2;
  }
  *n_classes = n;
  return SM_OK;
}
