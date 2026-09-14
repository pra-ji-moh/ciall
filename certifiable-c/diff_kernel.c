/*
 * diff_kernel.c -- the C kernel driven by a random generator that
 * diff_kernel.py reproduces exactly, printed in a canonical form so the
 * two can be diffed line for line.
 *
 * World counts run from 1 to 256, so roughly three trials in four use more
 * than one 64-bit word. That is the multi-word path the audit found had
 * never been exercised by anything.
 *
 * Per trial: compose, ask, check the witness and a tampered copy, observe,
 * probe, choose, refine, distinguish, project, speculate and retract,
 * settle and commit, and absorption through a groundless operand.
 */

#include <stdio.h>

#include "smarsh_reason.h"

#define TRIALS 3000u

static uint64_t X = 20260911u;

static unsigned rnd(unsigned n) {
  X = X * 6364136223846793005ULL + 1442695040888963407ULL;
  return (unsigned)((X >> 33) % (uint64_t)n);
}

static void hex64(uint64_t v) {
  printf("%08x%08x", (unsigned)(v >> 32), (unsigned)(v & 0xFFFFFFFFu));
}

static void rand_query(sr_query_t *q, unsigned n, unsigned dom) {
  unsigned w;
  sr_query_init(q, n, dom);
  for (w = 0u; w < n; w++) {
    sr_query_set(q, w, rnd(dom));
  }
}

static sr_query_t QA, QB, QP, COMP, COMP2, QG, PQ;
static sr_query_t ARR[4];
static sr_op2_t OP;

int main(void) {
  unsigned i;

  for (i = 0u; i < TRIALS; i++) {
    unsigned n = 1u + rnd(256u);
    unsigned da = 1u + rnd(5u);
    unsigned db = 1u + rnd(5u);
    unsigned dout = 1u + rnd(5u);
    unsigned k, w;
    sm_status_t st;
    sr_state_t s, s2, snap;
    sr_result_t r;
    sr_witness_t tw;
    sr_probe_t pr;
    sr_partition_t p;
    sr_settle_t t;
    sm_ancestry_t anc;
    unsigned idx;

    rand_query(&QA, n, da);
    rand_query(&QB, n, db);
    for (k = 0u; k < SR_MAX_TABLE; k++) {
      OP.out[k] = 0u;
    }
    for (k = 0u; k < da * db; k++) {
      OP.out[k] = (uint8_t)rnd(dout);
    }
    OP.dom_a = da;
    OP.dom_b = db;
    OP.dom_out = dout;

    sr_state_init(&s, n);
    for (w = 0u; w < n; w++) {
      if (rnd(10u) < 4u) {
        sr_eliminate(&s, w);
      }
    }

    printf("T%u n%u", i, n);

    /* compose and ask */
    st = sr_map2(&OP, &QA, &QB, &COMP);
    sr_ask(&COMP, &s, &r);
    printf(" | m%d v%d x%u S%.6f H%.6f i", (int)st, (int)r.verdict, r.value,
           r.S, r.H);
    hex64(r.witness.image);
    printf(" wc%d lc%u", sr_witness_check(&COMP, &r.witness), sr_live_count(&s));
    tw = r.witness;
    tw.image ^= (uint64_t)1;
    printf(" tam%d", sr_witness_check(&COMP, &tw));
    tw = r.witness;
    tw.live[SR_WORLD_WORDS - 1u] ^= (uint64_t)1 << 63;
    printf(" tmw%d", sr_witness_check(&COMP, &tw));

    /* observe, then ask the other operand */
    st = sr_observe(&s, &QA, rnd(da));
    sr_ask(&QB, &s, &r);
    printf(" | o%d v%d x%u S%.6f lc%u", (int)st, (int)r.verdict, r.value, r.S,
           sr_live_count(&s));

    /* probe and choose -- with a FRESH question. Probing with QA, which
       was just observed and so is constant on the survivors, can never
       help, and an earlier version of this harness did exactly that: every
       probe came back irrelevant and the interesting paths never ran. */
    rand_query(&QP, n, 1u + rnd(5u));
    st = sr_probe(&s, &QB, &QP, &pr);
    printf(" | p%d r", (int)st);
    hex64(pr.reachable);
    {
      unsigned sum = 0u;
      for (k = 0u; k < SR_MAX_ANSWERS; k++) {
        sum += pr.surviving[k];
      }
      printf(" wr%u br%u su%d ir%d ss%u", pr.worst_case_removed,
             pr.best_case_removed, pr.sufficient, pr.irrelevant, sum);
    }
    ARR[0] = QA;
    ARR[1] = QP;
    ARR[2] = COMP;
    idx = 99u;
    st = sr_choose(&s, &QB, ARR, 3u, (sr_ask_policy_t)rnd(2u), &idx, &pr);
    printf(" c%d ix%u", (int)st, idx);

    /* refine, distinguish, project */
    {
      unsigned nq = rnd(4u);
      unsigned h = 0u;
      for (k = 0u; k < nq; k++) {
        rand_query(&ARR[k], n, 1u + rnd(4u));
      }
      st = sr_refine(ARR, nq, n, &p);
      for (w = 0u; w < n; w++) {
        h = h * 31u + (unsigned)p.cell[w];
      }
      printf(" | rf%d nc%u h%u d%d pj%d", (int)st, p.n_cells, h,
             sr_distinguishes(&p, &QA), (int)sr_project(&p, &QA, &PQ));
    }

    /* speculate, then retract */
    {
      double tau = (double)rnd(5u) / 4.0;
      uint64_t seed = (uint64_t)rnd(100000u);
      sm_intensity_policy_t pol = (sm_intensity_policy_t)rnd(2u);
      unsigned gid = rnd(8u);
      s2 = s;
      sr_checkpoint(&s2, &snap);
      sm_ancestry_clear(&anc);
      st = sr_speculate(&s2, &COMP, tau, seed, pol, gid, &anc, &r);
      printf(" | sp%d al%d v%d x%u S%.6f in%.6f b%.6f lc%u", (int)st,
             r.allowed, (int)r.verdict, r.value, r.S, r.intensity,
             sm_ancestry_bits(&anc), sr_live_count(&s2));
      st = sr_retract(&s2, &snap, &anc, gid);
      printf(" rt%d lc%u b%.6f", (int)st, sr_live_count(&s2),
             sm_ancestry_bits(&anc));
    }

    /* settle and commit */
    {
      sr_state_t fs;
      sr_state_init(&fs, n);
      sr_settle_begin(&t, &fs, &COMP);
      for (k = 0u; k < 3u; k++) {
        rand_query(&PQ, n, 3u);
        sr_settle_step(&t, &fs, &COMP, &PQ, rnd(3u));
      }
      sr_settle_commit(&t, &COMP, &fs, &r);
      printf(" | se r%u p%u i%u s%d c%d l%u m", t.rounds, t.productive,
             t.informative, t.settled, t.contradicted, t.live_now);
      hex64(t.image);
      printf(" al%d v%d x%u", r.allowed, (int)r.verdict, r.value);
    }

    /* absorption through a groundless operand */
    if (rnd(3u) == 0u) {
      sr_query_groundless(&QG, n, db);
    } else {
      rand_query(&QG, n, db);
    }
    st = sr_ask2(&OP, &QA, &QG, &s, &COMP2, &r);
    printf(" | a2%d v%d x%u k%d ab%d", (int)st, (int)r.verdict, r.value,
           (int)r.witness.kind, sr_witness_check_absorb(&OP, &r.witness));

    printf("\n");
  }
  return 0;
}
