/*
 * smarsh_learner.c -- see smarsh_learner.h for what this is, why the loop
 * terminates, and what a witness here does and does not certify.
 *
 * Compiled and run: test_learner.c passes, and diff_learner.c matched the
 * Python original (python-reference/baby_auto.py) on 3,734 runs.
 */

#include "smarsh_learner.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* helpers                                                              */
/* ------------------------------------------------------------------ */

static int is_held(const sl_learner_t *L, unsigned cap) {
  unsigned i;

  for (i = 0u; i < SL_MAX_CAPS; i++) {
    if (i >= L->n_held) {
      break;
    }
    if (L->held[i] == cap) {
      return 1;
    }
  }
  return 0;
}

static int in_memory(const sl_learner_t *L, unsigned cap) {
  unsigned i;

  for (i = 0u; i < SL_MAX_MEMORY; i++) {
    if (i >= L->n_memory) {
      break;
    }
    if (L->memory[i].cap == cap) {
      return 1;
    }
  }
  return 0;
}

/* The log cannot overflow: at most n_table acquisitions, n_table
   measurements and 2 * n_table retractions, which is SL_MAX_LOG at the
   table limit. So a full log means the termination argument is wrong, and
   that is reported rather than truncated. */
static sm_status_t log_ev(sl_report_t *R, sl_ev_kind_t kind, unsigned cap,
                          unsigned a, unsigned b) {
  if (R->n_log >= SL_MAX_LOG) {
    return SM_ERR_INTERNAL_INVARIANT;
  }
  R->log[R->n_log].kind = kind;
  R->log[R->n_log].cap = cap;
  R->log[R->n_log].a = a;
  R->log[R->n_log].b = b;
  R->n_log++;
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* setup                                                                */
/* ------------------------------------------------------------------ */

sm_status_t sl_init(sl_learner_t *L, const sl_capacity_t *table,
                    unsigned n_table, unsigned n_situations) {
  unsigned i;

  if (L == 0 || table == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_table == 0u || n_situations == 0u) {
    return SM_ERR_EMPTY_DOMAIN;
  }
  if (n_table > SL_MAX_CAPS || n_situations > SR_MAX_SITUATIONS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  for (i = 0u; i < SL_MAX_CAPS; i++) {
    if (i < n_table && table[i].q.n_worlds != n_situations) {
      /* A capacity over a different situation space would be compared
         world by world against questions it does not line up with. */
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
    L->held[i] = 0u;
    L->distrusted[i] = 0;
  }
  L->table = table;
  L->n_table = n_table;
  L->n_situations = n_situations;
  L->n_held = 0u;
  L->n_memory = 0u;
  L->measurements = 0u;
  L->acquisitions = 0u;
  return SM_OK;
}

sm_status_t sl_scene_truthful(const sl_capacity_t *table, unsigned n_table,
                              unsigned situation, sl_scene_t *out) {
  unsigned i;

  if (table == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (n_table > SL_MAX_CAPS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  for (i = 0u; i < SL_MAX_CAPS; i++) {
    out->reading[i] = 0u;
    if (i < n_table) {
      if (situation >= table[i].q.n_worlds) {
        return SM_ERR_INDEX_OUT_OF_DOMAIN;
      }
      out->reading[i] = (unsigned)table[i].q.ans[situation];
    }
  }
  return SM_OK;
}

sm_status_t sl_grant(sl_learner_t *L, unsigned cap) {
  if (L == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  if (cap >= L->n_table) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (is_held(L, cap)) {
    return SM_ERR_INDEX_OUT_OF_DOMAIN;
  }
  if (L->n_held >= SL_MAX_CAPS) {
    return SM_ERR_DOMAIN_TOO_LARGE;
  }
  L->held[L->n_held] = cap;
  L->n_held++;
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* what it can currently tell apart                                     */
/* ------------------------------------------------------------------ */

sm_status_t sl_partition(sl_learner_t *L, unsigned extra, sr_partition_t *out) {
  unsigned i;
  unsigned n = 0u;

  if (L == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  for (i = 0u; i < SL_MAX_CAPS; i++) {
    if (i >= L->n_held) {
      break;
    }
    L->scratch[n] = L->table[L->held[i]].q;
    n++;
  }
  if (extra != SL_NONE) {
    if (extra >= L->n_table) {
      return SM_ERR_INDEX_OUT_OF_DOMAIN;
    }
    L->scratch[n] = L->table[extra].q;
    n++;
  }
  return sr_refine(L->scratch, n, L->n_situations, out);
}

sm_status_t sl_state(const sl_learner_t *L, const sr_partition_t *p,
                     sr_state_t *out) {
  unsigned i;
  sr_query_t pq;
  sm_status_t st;

  if (L == 0 || p == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  st = sr_state_init(out, p->n_cells);
  if (st != SM_OK) {
    return st;
  }
  for (i = 0u; i < SL_MAX_MEMORY; i++) {
    if (i >= L->n_memory) {
      break;
    }
    /* A claim the current vocabulary cannot express is kept in memory
       and skipped here, not discarded: a later distinction may make it
       expressible, and then it pays off without being observed again. */
    if (sr_project(p, &L->table[L->memory[i].cap].q, &pq) != SM_OK) {
      continue;
    }
    st = sr_observe(out, &pq, L->memory[i].answer);
    if (st != SM_OK) {
      return st;
    }
  }
  return SM_OK;
}

/* ------------------------------------------------------------------ */
/* acquisition                                                          */
/* ------------------------------------------------------------------ */

/*
 * POLICY, and the only real preference in this file. Among capacities
 * that would let it phrase the goal, take the one that refines LEAST:
 * acquire no more power to tell things apart than the question needs. If
 * none suffices alone, take the smallest refining one and come back.
 * Ties go to the lowest table index, so a replay acquires the same thing.
 *
 * Preferring the largest refinement would be just as consistent with the
 * axiom and would give a learner that jumps straight to exact counting.
 * That would not be wrong. It would be a different learner.
 */
static sm_status_t pick_capacity(sl_learner_t *L, const sr_query_t *goal,
                                 unsigned *out) {
  sr_partition_t now;
  sr_partition_t with;
  sr_query_t probe_q;
  unsigned c;
  unsigned best = SL_NONE;
  unsigned best_cells = 0u;
  int best_enables = 0;
  sm_status_t st;

  *out = SL_NONE;
  st = sl_partition(L, SL_NONE, &now);
  if (st != SM_OK) {
    return st;
  }
  for (c = 0u; c < SL_MAX_CAPS; c++) {
    int enables;
    if (c >= L->n_table) {
      break;
    }
    if (is_held(L, c)) {
      continue;
    }
    if (sr_distinguishes(&now, &L->table[c].q) == 0) {
      continue;   /* refines nothing: a restatement, not a capacity */
    }
    st = sl_partition(L, c, &with);
    if (st != SM_OK) {
      return st;
    }
    enables = (sr_project(&with, goal, &probe_q) == SM_OK) ? 1 : 0;
    /* An enabling candidate beats any non-enabling one outright; within
       the same class, fewer cells wins; strict comparison keeps the
       lowest index on a tie. */
    if (best == SL_NONE ||
        (enables && !best_enables) ||
        (enables == best_enables && with.n_cells < best_cells)) {
      best = c;
      best_cells = with.n_cells;
      best_enables = enables;
    }
  }
  *out = best;
  return SM_OK;
}

static sm_status_t acquire(sl_learner_t *L, unsigned cap, sl_report_t *R) {
  sr_partition_t before;
  sr_partition_t after;
  sm_status_t st;

  st = sl_partition(L, SL_NONE, &before);
  if (st != SM_OK) {
    return st;
  }
  st = sl_grant(L, cap);
  if (st != SM_OK) {
    return st;
  }
  st = sl_partition(L, SL_NONE, &after);
  if (st != SM_OK) {
    return st;
  }
  L->acquisitions++;
  return log_ev(R, SL_EV_ACQUIRE, cap, before.n_cells, after.n_cells);
}

/* ------------------------------------------------------------------ */
/* a scene                                                              */
/* ------------------------------------------------------------------ */

sm_status_t sl_glance(sl_learner_t *L, const sl_scene_t *scene) {
  unsigned i;

  if (L == 0 || scene == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  L->n_memory = 0u;
  for (i = 0u; i < SL_MAX_CAPS; i++) {
    L->distrusted[i] = 0;          /* trust resets with the scene */
  }
  for (i = 0u; i < SL_MAX_CAPS; i++) {
    unsigned cap;
    if (i >= L->n_held) {
      break;
    }
    cap = L->held[i];
    if (!L->table[cap].immediate) {
      continue;
    }
    if (L->n_memory >= SL_MAX_MEMORY) {
      return SM_ERR_INTERNAL_INVARIANT;
    }
    L->memory[L->n_memory].cap = cap;
    L->memory[L->n_memory].answer = scene->reading[cap];
    L->n_memory++;
  }
  return SM_OK;
}

static sm_status_t measure(sl_learner_t *L, unsigned cap,
                           const sl_scene_t *scene, sl_report_t *R) {
  if (L->n_memory >= SL_MAX_MEMORY) {
    return SM_ERR_INTERNAL_INVARIANT;
  }
  L->memory[L->n_memory].cap = cap;
  L->memory[L->n_memory].answer = scene->reading[cap];
  L->n_memory++;
  L->measurements++;
  return log_ev(R, SL_EV_MEASURE, cap, scene->reading[cap], 0u);
}

sm_status_t sl_recover(sl_learner_t *L, const sr_partition_t *p,
                       sl_obs_t *out) {
  unsigned k;
  sr_state_t st;
  sm_status_t s;

  if (L == 0 || p == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  for (k = 0u; k < SL_MAX_MEMORY; k++) {
    sl_obs_t bad;
    if (L->n_memory == 0u) {
      break;
    }
    L->n_memory--;
    bad = L->memory[L->n_memory];
    /* Distrusted whether or not it was the culprit. Most-recent-first is
       a crude policy and may drop an honest claim, which costs
       information. Without this line the learner re-measures the same
       source and gets the same impossible value back, forever. */
    L->distrusted[bad.cap] = 1;
    s = sl_state(L, p, &st);
    if (s != SM_OK) {
      return s;
    }
    if (sr_live_count(&st) > 0u) {
      *out = bad;
      return SM_OK;
    }
  }
  /* Reached only if memory was empty on entry: nothing to retract. With
     claims present it cannot be reached, because an empty memory leaves
     every cell live and the partition has at least one (see SL_OUT_
     CONTRADICTED in the header). */
  return SM_ERR_EMPTY_DOMAIN;
}

/* ------------------------------------------------------------------ */
/* the loop                                                             */
/* ------------------------------------------------------------------ */

/*
 * Could the candidate questions, answered TOGETHER, narrow the target?
 *
 * sr_choose looks one question ahead: a question is irrelevant when no
 * answer to it, alone, narrows the target. That misses information that
 * only pays off in combination. For "is s divisible by 7" over s = 16 *
 * hi + lo, every block of 16 contains a multiple of 7, so neither digit
 * alone narrows the answer, yet both together settle it. The learner used
 * to stop there and report STUCK, which was false.
 *
 * Exact, not heuristic: group the live worlds by their answers to ALL the
 * candidates at once. If some group's target values are fewer than the
 * whole live set's, answering everything would narrow the target, so
 * something is worth measuring. If no group is narrower, no sequence of
 * these measurements can help, and STUCK is true. The loops run over
 * SR_MAX_WORLDS and SL_MAX_CAPS, constants.
 */
static int jointly_relevant(const sr_state_t *s, const sr_query_t *target,
                            const sr_query_t *qs, unsigned n) {
  uint64_t whole = (uint64_t)0;
  unsigned w;
  unsigned v;
  unsigned k;

  for (w = 0u; w < SR_MAX_WORLDS; w++) {
    if (w < s->n_worlds && ((s->live[w >> 6] >> (w & 63u)) & (uint64_t)1)) {
      whole |= (uint64_t)1 << target->ans[w];
    }
  }
  for (w = 0u; w < SR_MAX_WORLDS; w++) {
    uint64_t img = (uint64_t)0;
    if (w >= s->n_worlds || !((s->live[w >> 6] >> (w & 63u)) & (uint64_t)1)) {
      continue;
    }
    for (v = 0u; v < SR_MAX_WORLDS; v++) {
      int same = 1;
      if (v >= s->n_worlds || !((s->live[v >> 6] >> (v & 63u)) & (uint64_t)1)) {
        continue;
      }
      for (k = 0u; k < SL_MAX_CAPS; k++) {
        if (k < n && qs[k].ans[v] != qs[k].ans[w]) {
          same = 0;
          break;
        }
      }
      if (same) {
        img |= (uint64_t)1 << target->ans[v];
      }
    }
    if (img != whole) {
      return 1;
    }
  }
  return 0;
}

static void report_init(sl_report_t *R) {
  R->outcome = SL_OUT_STUCK;
  R->value = 0u;
  R->H = 0.0;
  R->worlds = 0u;
  R->measured = 0u;
  R->acquired = 0u;
  R->proved = 0;
  R->n_log = 0u;
}

sm_status_t sl_pursue(sl_learner_t *L, const sr_query_t *goal,
                      const sl_scene_t *scene, sl_report_t *out) {
  unsigned pass;
  unsigned start_m;
  unsigned start_a;
  sm_status_t st;

  if (L == 0 || goal == 0 || scene == 0 || out == 0) {
    return SM_ERR_NULL_ARGUMENT;
  }
  /* Every reading must be an answer its capacity can actually give. The
     Python reference read the truth directly, so it never saw a malformed
     reading; here the caller supplies them, and an out-of-range one would
     otherwise be skipped silently deep inside sl_state. Checked at the
     boundary instead, where it can be reported. A reading that is IN range
     but false is a different thing entirely -- that is a lie, and the loop
     is built to survive it. */
  {
    unsigned i;
    for (i = 0u; i < SL_MAX_CAPS; i++) {
      if (i < L->n_table && scene->reading[i] >= L->table[i].q.dom) {
        return SM_ERR_INDEX_OUT_OF_DOMAIN;
      }
    }
  }
  report_init(out);
  st = sl_glance(L, scene);
  if (st != SM_OK) {
    return st;
  }
  start_m = L->measurements;
  start_a = L->acquisitions;

  for (pass = 0u; pass < SL_MAX_PASSES; pass++) {
    sr_partition_t p;
    sr_query_t gq;
    sr_state_t s;
    sr_result_t r;
    unsigned nxt;
    unsigned n_meas = 0u;
    unsigned c;

    st = sl_partition(L, SL_NONE, &p);
    if (st != SM_OK) {
      return st;
    }
    out->worlds = p.n_cells;

    /* Cannot even phrase it: learn something first. */
    if (sr_project(&p, goal, &gq) != SM_OK) {
      st = pick_capacity(L, goal, &nxt);
      if (st != SM_OK) {
        return st;
      }
      if (nxt == SL_NONE) {
        out->outcome = SL_OUT_CANNOT_PHRASE;
        out->measured = L->measurements - start_m;
        out->acquired = L->acquisitions - start_a;
        return SM_OK;
      }
      st = acquire(L, nxt, out);
      if (st != SM_OK) {
        return st;
      }
      continue;
    }

    st = sl_state(L, &p, &s);
    if (st != SM_OK) {
      return st;
    }
    st = sr_ask(&gq, &s, &r);
    if (st != SM_OK) {
      return st;
    }

    if (r.verdict == SM_CONTRADICTION) {
      sl_obs_t dropped;
      st = sl_recover(L, &p, &dropped);
      if (st == SM_ERR_EMPTY_DOMAIN) {
        /* A contradiction needs at least one claim, and dropping claims
           always ends in a live state (header, SL_OUT_CONTRADICTED). So
           this is unreachable; if the proof is wrong, say so loudly
           instead of reporting an outcome that was never supposed to be
           possible. */
        return SM_ERR_INTERNAL_INVARIANT;
      }
      if (st != SM_OK) {
        return st;
      }
      st = log_ev(out, SL_EV_RETRACT, dropped.cap, dropped.answer, 0u);
      if (st != SM_OK) {
        return st;
      }
      continue;
    }

    if (r.verdict == SM_DERIVED) {
      out->outcome = SL_OUT_DERIVED;
      out->value = r.value;
      out->H = r.H;
      out->proved = sr_witness_check(&gq, &r.witness);
      out->measured = L->measurements - start_m;
      out->acquired = L->acquisitions - start_a;
      return SM_OK;
    }

    /* Can phrase it, cannot settle it. What would? Only capacities it
       holds, has not already heard from, and has not learned to distrust. */
    for (c = 0u; c < SL_MAX_CAPS; c++) {
      unsigned cap;
      if (c >= L->n_held) {
        break;
      }
      cap = L->held[c];
      if (in_memory(L, cap) || L->distrusted[cap]) {
        continue;
      }
      if (sr_project(&p, &L->table[cap].q, &L->measure_buf[n_meas]) !=
          SM_OK) {
        continue;
      }
      L->measure_key[n_meas] = cap;
      n_meas++;
    }
    if (n_meas > 0u) {
      sr_probe_t pr;
      unsigned pick = n_meas;
      st = sr_choose(&s, &gq, L->measure_buf, n_meas, SR_ASK_GUARANTEE,
                     &pick, &pr);
      if (st != SM_OK) {
        return st;
      }
      if (pick == n_meas && jointly_relevant(&s, &gq, L->measure_buf, n_meas)) {
        /* No single question narrows the goal, but together they would.
           Measure the one that rules out the most worlds in the worst
           case; the lowest index wins a tie, so a replay does the same. */
        unsigned i;
        unsigned best_removed = 0u;
        for (i = 0u; i < n_meas; i++) {
          st = sr_probe(&s, &gq, &L->measure_buf[i], &pr);
          if (st != SM_OK) {
            return st;
          }
          if (pr.worst_case_removed > best_removed) {
            best_removed = pr.worst_case_removed;
            pick = i;
          }
        }
        if (pick == n_meas) {
          /* Unreachable: jointly relevant means some candidate splits
             the live worlds, so some worst case removes at least one. */
          return SM_ERR_INTERNAL_INVARIANT;
        }
      }
      if (pick < n_meas) {
        st = measure(L, L->measure_key[pick], scene, out);
        if (st != SM_OK) {
          return st;
        }
        continue;
      }
    }

    /* Nothing it holds would help. Learn something new, or stop. */
    st = pick_capacity(L, goal, &nxt);
    if (st != SM_OK) {
      return st;
    }
    if (nxt == SL_NONE) {
      out->outcome = SL_OUT_STUCK;
      out->H = r.H;
      out->measured = L->measurements - start_m;
      out->acquired = L->acquisitions - start_a;
      return SM_OK;
    }
    st = acquire(L, nxt, out);
    if (st != SM_OK) {
      return st;
    }
  }

  /* The header proves this is unreachable. If it is reached, the proof is
     wrong, and that should be loud rather than papered over. */
  return SM_ERR_INTERNAL_INVARIANT;
}
