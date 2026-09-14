/*
 * smarsh_child.c -- see smarsh_child.h.
 */

#include "smarsh_child.h"

#include <stdio.h>
#include <string.h>

const char *const CH_ABILITY_NAME[CH_N_ABILITIES] = {
  "counting: it invented \"how many\" and built its own worlds",
  "telling which tray is heavier, by a rule over its own questions",
  "saying what pouring two trays gives, where it has poured",
  "and past that, by a law of its own",
  "proving things about every number at once"
};

void ch_init(ch_child_t *c) {
  if (c != 0) memset(c, 0, sizeof *c);
}

/* ---- folding everything it has seen into everything it knows ---------- */

static sr_query_t QA, QB, QT, Q1SRC, Q1DST;
static sc_discovery_t DISC;

static void relearn(ch_child_t *c, const ch_world_t *w) {
  unsigned i;
  if (c->n_scale > 0u && ab_abstract(&c->scale_seen, &c->abstraction) == SM_OK) {
    c->has_abstraction = 1;
    c->has_which_way = 0;
    if (c->abstraction.determined && c->abstraction.n_groups >= 1u &&
        c->abstraction.n_groups <= 2u) {
      if (sc_discover(c->abstraction.question, c->abstraction.n_groups, &c->abstraction.outcome_q,
                      c->abstraction.n_situations, &DISC) == SM_OK && DISC.found) {
        c->which_way = DISC.op;
        strcpy(c->which_way.name, "which way");
        c->has_which_way = 1;
      }
    }
  }

  if (c->n_pours >= 2u) {
    unsigned dom = w->max_count + 1u;
    sr_query_init(&QA, c->n_pours, dom);
    sr_query_init(&QB, c->n_pours, dom);
    sr_query_init(&QT, c->n_pours, 2u * w->max_count + 1u);
    for (i = 0u; i < c->n_pours; i++) {
      sr_query_set(&QA, i, c->pour_a[i]);
      sr_query_set(&QB, i, c->pour_b[i]);
      sr_query_set(&QT, i, c->pour_t[i]);
    }
    if (sc_discover(&QA, 1u, &QT, c->n_pours, &DISC) == SM_OK && !DISC.found) {
      sr_query_t pair[2];
      pair[0] = QA;
      pair[1] = QB;
      if (sc_discover(pair, 2u, &QT, c->n_pours, &DISC) == SM_OK && DISC.found) {
        c->combine = DISC.op;
        strcpy(c->combine.name, "combine");
        c->has_combine = 1;
      }
    }
  }

  /* the law behind the table, once "one more" is grounded far enough to
     stand on: one more is what pouring a single pebble in does */
  c->has_law = 0;
  if (c->has_combine) {
    unsigned k = 0u;
    sc_op_t one;
    while (k + 1u <= w->max_count && sc_grounded(&c->combine, k, 1u) &&
           sc_value(&c->combine, k, 1u) == k + 1u) {
      k++;
    }
    if (k >= 3u) {
      sr_query_init(&Q1SRC, k, k);
      sr_query_init(&Q1DST, k, k + 1u);
      for (i = 0u; i < k; i++) {
        sr_query_set(&Q1SRC, i, i);
        sr_query_set(&Q1DST, i, i + 1u);
      }
      if (sc_discover(&Q1SRC, 1u, &Q1DST, k, &DISC) == SM_OK && DISC.found) {
        one = DISC.op;
        if (lw_init(&c->laws, &one, k, "one more is always the next number") == SM_OK &&
            lw_discover(&c->laws, &c->combine, "combine", "combining goes on working past what it has poured",
                        &c->law_combine) == SM_OK) {
          c->has_law = 1;
        }
      }
    }
  }

  c->has_closed_form = 0;
  c->commutative_proved = 0;
  if (c->has_law && pf_laws(&c->laws, &c->proofs) == SM_OK && c->proofs.proved[c->law_combine]) {
    pf_expr_t L, R;
    pf_verdict_t v;
    c->has_closed_form = 1;
    pf_clear(&L);
    pf_clear(&R);
    pf_apply(&L, c->law_combine, pf_x(&L), pf_y(&L));
    pf_apply(&R, c->law_combine, pf_y(&R), pf_x(&R));
    if (pf_prove(&c->laws, &c->proofs, &L, &R, &v) == SM_OK && v.proved) c->commutative_proved = 1;
  }
}

static void notice(ch_child_t *c) {
  int now[CH_N_ABILITIES];
  unsigned i;
  now[CH_COUNT] = c->has_abstraction && c->abstraction.any_symmetry && c->abstraction.determined;
  now[CH_WHICH_WAY] = c->has_which_way;
  now[CH_POUR_SEEN] = c->has_combine;
  now[CH_POUR_LAW] = c->has_law;
  now[CH_THEOREM] = c->has_closed_form && c->commutative_proved;
  for (i = 0u; i < CH_N_ABILITIES; i++) {
    if (now[i] && !c->ever[i]) c->acquired_at[i] = c->experiments;
    /* an ability can be LOST: an experiment that refutes the law it stood
       on takes it away, and that is reported rather than papered over */
    c->ever[i] = c->ever[i] || now[i];
    c->acquired[i] = now[i];
  }
}

/* ---- what is worth doing next ---------------------------------------- */

ch_plan_t ch_decide(const ch_child_t *c, const ch_world_t *w) {
  ch_plan_t best;
  unsigned p, a, b, total;
  memset(&best, 0, sizeof best);
  best.kind = CH_EXP_NONE;
  if (c == 0 || w == 0) return best;
  total = 1u << w->n_slots;

  for (p = 0u; p < total; p++) {
    unsigned value = 0u, outcome;
    const char *why = "";
    if (c->scale_seen.n_raw != 0u && c->scale_seen.observed[p]) continue;
    if (!c->has_abstraction) {
      value = 3u;
      why = "it has no way yet to tell one arrangement from another";
    } else {
      ab_standing_t st = ab_predict(&c->scale_seen, &c->abstraction, p, &outcome);
      if (st == AB_NO_GROUNDS) { value = 3u; why = "nothing it knows says what the scale would do"; }
      else if (c->abstraction.leaps) { value = 2u; why = "it can only guess this through a symmetry it has not seen every case of, so the scale can prove it wrong"; }
      else { value = 1u; why = "it can already work this out from what it has proved"; }
    }
    if (value > best.value) {
      best.kind = CH_EXP_SCALE;
      best.pattern = p;
      best.value = value;
      strncpy(best.why, why, CH_TEXT - 1u);
      best.why[CH_TEXT - 1u] = '\0';
    }
    if (best.value >= 3u) break;
  }

  for (a = 0u; a <= w->max_count && best.value < 3u; a++) {
    for (b = 0u; b <= w->max_count; b++) {
      unsigned value = 0u, i;
      const char *why = "";
      int seen = 0;
      for (i = 0u; i < c->n_pours; i++) if (c->pour_a[i] == a && c->pour_b[i] == b) seen = 1;
      if (seen) continue;
      if (!c->has_combine) { value = 3u; why = "it does not yet know what pouring does"; }
      else if (sc_grounded(&c->combine, a, b)) continue;
      else if (c->has_law) { value = 2u; why = "its law says what this should give, and pouring can prove the law wrong"; }
      else { value = 3u; why = "it has never poured piles like these"; }
      if (value > best.value) {
        best.kind = CH_EXP_POUR;
        best.a = a;
        best.b = b;
        best.value = value;
        strncpy(best.why, why, CH_TEXT - 1u);
        best.why[CH_TEXT - 1u] = '\0';
      }
      if (best.value >= 3u) break;
    }
  }
  if (best.value <= 1u) {
    /* nothing left that could teach it or catch it out */
    best.kind = CH_EXP_NONE;
    strncpy(best.why, "everything it could try is already settled on ground it has proved", CH_TEXT - 1u);
  }
  return best;
}

/* ---- doing it, and folding it in --------------------------------------- */

int ch_step(ch_child_t *c, const ch_world_t *w) {
  ch_plan_t plan;
  if (c == 0 || w == 0) return 0;
  if (c->scale_seen.n_raw == 0u) ab_clear(&c->scale_seen, w->n_slots, 3u);
  plan = ch_decide(c, w);
  if (plan.kind == CH_EXP_NONE) {
    snprintf(c->note, CH_TEXT, "nothing left worth trying: %s", plan.why);
    return 0;
  }

  if (plan.kind == CH_EXP_SCALE) {
    unsigned real = w->tips(plan.pattern), guess = 0u;
    int had_guess = c->has_abstraction &&
                    ab_predict(&c->scale_seen, &c->abstraction, plan.pattern, &guess) == AB_INFERRED;
    ab_observe(&c->scale_seen, plan.pattern, real);
    c->n_scale++;
    c->experiments++;
    if (had_guess) {
      c->predictions++;
      if (guess == real) {
        c->confirmations++;
        snprintf(c->note, CH_TEXT, "arrangement %u: it expected %u and saw %u, so its guess stands",
                 plan.pattern, guess, real);
      } else {
        c->refutations++;
        snprintf(c->note, CH_TEXT, "arrangement %u: it expected %u but saw %u. Something it guessed is wrong",
                 plan.pattern, guess, real);
      }
    } else {
      snprintf(c->note, CH_TEXT, "arrangement %u: the scale did %u, which it could not have said",
               plan.pattern, real);
    }
  } else {
    unsigned real = w->pour(plan.a, plan.b);
    uint64_t guess = 0u;
    uint32_t leaps = 0u;
    int had_guess = c->has_law &&
                    lw_eval(&c->laws, c->law_combine, plan.a, plan.b, &guess, &leaps) == SM_OK;
    if (c->n_pours < CH_MAX_POURS) {
      c->pour_a[c->n_pours] = plan.a;
      c->pour_b[c->n_pours] = plan.b;
      c->pour_t[c->n_pours] = real;
      c->n_pours++;
    }
    c->experiments++;
    if (had_guess) {
      c->predictions++;
      if (guess == (uint64_t)real) {
        c->confirmations++;
        snprintf(c->note, CH_TEXT, "poured %u and %u: its law said %llu, and it was %u",
                 plan.a, plan.b, (unsigned long long)guess, real);
      } else {
        c->refutations++;
        snprintf(c->note, CH_TEXT, "poured %u and %u: its law said %llu but it was %u. The law is wrong",
                 plan.a, plan.b, (unsigned long long)guess, real);
      }
    } else {
      snprintf(c->note, CH_TEXT, "poured %u and %u and counted %u", plan.a, plan.b, real);
    }
  }

  relearn(c, w);
  notice(c);
  return 1;
}

/* ---- answering, with what it rests on ---------------------------------- */

ch_standing_t ch_which_way(const ch_child_t *c, unsigned left, unsigned right, unsigned *out,
                           char *why, unsigned cap) {
  if (c == 0 || out == 0) return CH_REFUSED;
  if (!c->has_which_way) {
    if (why != 0) snprintf(why, cap, "it has no rule for the scale yet");
    return CH_REFUSED;
  }
  if (!sc_grounded(&c->which_way, left, right)) {
    if (why != 0) {
      snprintf(why, cap, "its rule was learned on trays of at most %u, and it will not guess past that",
               c->which_way.dom_a - 1u);
    }
    return CH_REFUSED;
  }
  *out = sc_value(&c->which_way, left, right);
  if (c->abstraction.leaps) {
    if (why != 0) snprintf(why, cap, "resting on a symmetry it has not seen every case of");
    return CH_ANSWERED_CONJECTURE;
  }
  if (why != 0) snprintf(why, cap, "from what it saw, with the rule checked on every case");
  return CH_ANSWERED_SEEN;
}

ch_standing_t ch_pour(const ch_child_t *c, unsigned a, unsigned b, uint64_t *out, char *why,
                      unsigned cap) {
  uint32_t leaps = 0u;
  if (c == 0 || out == 0) return CH_REFUSED;
  if (c->has_combine && sc_grounded(&c->combine, a, b)) {
    *out = sc_value(&c->combine, a, b);
    if (why != 0) snprintf(why, cap, "it has poured exactly this");
    return CH_ANSWERED_SEEN;
  }
  if (c->has_law && lw_eval(&c->laws, c->law_combine, a, b, out, &leaps) == SM_OK) {
    if (leaps == 0u) {
      if (why != 0) snprintf(why, cap, "from what it has poured");
      return CH_ANSWERED_SEEN;
    }
    if (why != 0) {
      unsigned i, used = 0u;
      used += (unsigned)snprintf(why, cap, "resting on");
      for (i = 0u; i < c->laws.n && used + 4u < cap; i++) {
        if (leaps & (1u << i)) used += (unsigned)snprintf(why + used, cap - used, " \"%s\";", c->laws.law[i].said);
      }
    }
    return CH_ANSWERED_CONJECTURE;
  }
  if (why != 0) snprintf(why, cap, "it has never poured piles like these and has no law yet");
  return CH_REFUSED;
}

/* ---- where it stands ---------------------------------------------------- */

void ch_report(const ch_child_t *c, const ch_world_t *w, char *buf, unsigned cap) {
  unsigned used = 0u, i, reach = 0u;
  ch_plan_t next;
  if (buf == 0 || cap == 0u) return;
  buf[0] = '\0';
  if (c == 0 || w == 0) return;
  used += (unsigned)snprintf(buf + used, cap - used,
                             "  after %u experiments (%u guesses made: %u held, %u caught out)\n",
                             c->experiments, c->predictions, c->confirmations, c->refutations);
  used += (unsigned)snprintf(buf + used, cap - used, "  WHAT IT CAN DO:\n");
  for (i = 0u; i < CH_N_ABILITIES; i++) {
    if (c->acquired[i]) {
      used += (unsigned)snprintf(buf + used, cap - used, "    %s\n      (after experiment %u)\n",
                                 CH_ABILITY_NAME[i], c->acquired_at[i]);
    }
  }
  for (i = 0u; i < CH_N_ABILITIES; i++) {
    if (!c->acquired[i]) {
      used += (unsigned)snprintf(buf + used, cap - used, "  %s: %s\n",
                                 c->ever[i] ? "LOST (what it stood on was refuted)" : "NOT YET",
                                 CH_ABILITY_NAME[i]);
    }
  }
  used += (unsigned)snprintf(buf + used, cap - used, "  WHAT RESTS ON A GUESS:\n");
  if (c->has_abstraction && c->abstraction.leaps) {
    used += (unsigned)snprintf(buf + used, cap - used,
                               "    a symmetry it has not seen every case of\n");
  }
  if (c->has_law) {
    used += (unsigned)snprintf(buf + used, cap - used, "    %s\n", c->laws.law[c->law_combine].said);
  }
  if (!(c->has_abstraction && c->abstraction.leaps) && !c->has_law) {
    used += (unsigned)snprintf(buf + used, cap - used, "    nothing: everything it says is from what it saw\n");
  }
  if (c->has_closed_form) {
    used += (unsigned)snprintf(buf + used, cap - used,
                               "  WHAT IT HAS PROVED: pouring has a closed form, proved by induction%s\n",
                               c->commutative_proved ? ", and order never matters, for every number" : "");
  }
  if (c->has_combine) {
    unsigned biggest = 0u;
    for (i = 0u; i < c->n_pours; i++) {
      if (c->pour_a[i] > biggest) biggest = c->pour_a[i];
      if (c->pour_b[i] > biggest) biggest = c->pour_b[i];
      if (c->pour_a[i] + c->pour_b[i] > reach) reach = c->pour_a[i] + c->pour_b[i];
    }
    used += (unsigned)snprintf(buf + used, cap - used,
                               "  WHERE IT IS GROUNDED: %u pours done, the largest tray %u, the largest pile %u\n",
                               c->n_pours, biggest, reach);
  }
  next = ch_decide(c, w);
  if (next.kind == CH_EXP_NONE) {
    used += (unsigned)snprintf(buf + used, cap - used, "  WHAT IT WANTS NEXT: nothing; %s\n", next.why);
  } else if (next.kind == CH_EXP_SCALE) {
    used += (unsigned)snprintf(buf + used, cap - used,
                               "  WHAT IT WANTS NEXT: to try arrangement %u, because %s\n",
                               next.pattern, next.why);
  } else {
    (void)snprintf(buf + used, cap - used,
                   "  WHAT IT WANTS NEXT: to pour %u and %u, because %s\n", next.a, next.b, next.why);
  }
}
