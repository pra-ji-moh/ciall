/*
 * diff_core.c -- the C speculation gate (smarsh_core.c, sm_decide) against
 * Smarsh's JavaScript one (src/speculate.js through the `speculate`
 * builtin), case by case. Needs node and the Smarsh repo, like
 * test_frontend.c.
 *
 * Both compute support S = 1 - |D|/|Dom| from a real possibility set, the
 * bar tau = max(stakes, 1 - formalizability), speculate iff S >= tau, and
 * intensity S. For an undetermined set (two or more values left) they are
 * meant to agree exactly: on the decision, and on the intensity to the
 * bit. JS prints numbers to 12 digits, so each line of the generated
 * program has Smarsh compare its own result against the C value written
 * as a 17-digit literal (which parses back to the same double) and print
 * true or false.
 *
 * Half the cases put the bar EXACTLY on the support, because that is where
 * two formulas for S that differ in the last bit would decide differently:
 * C computes 1 - d/n, JS (n - d)/n.
 *
 * Two classes differ BY DESIGN and are counted separately, not diffed:
 *   one value left   C answers it as derived, ungated; JS still gates it
 *   no value left    C reports a contradiction; JS computes S = 1 and
 *                    speculates at full intensity (see the report printed)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "smarsh_core.h"

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

#define SMARSH_CLI "C:/Users/USER/smarsh/bin/smarsh.mjs"
#define CASE_FILE "diff_core_case.tmp"
#define N_CASES 3000u

static uint32_t lcg = 777u;
static unsigned rnd(unsigned n) {
  lcg = lcg * 1103515245u + 12345u;
  return (lcg >> 16) % n;
}

typedef struct {
  unsigned n, d, boundary;
  double stakes, formal, tau;
  int allowed;
  double intensity;
} case_t;

static case_t CASES[N_CASES];
static char OUT[1u << 20];

int main(void) {
  FILE *f = fopen(CASE_FILE, "wb");
  FILE *p;
  unsigned i, k, agree = 0u, bad = 0u, at_boundary = 0u, allowed = 0u;
  size_t got;
  char cmd[256], *line;

  if (f == NULL) { printf("cannot write %s\n", CASE_FILE); return 1; }
  for (i = 0u; i < N_CASES; i++) {
    case_t *c = &CASES[i];
    sm_possibility_t poss;
    sm_result_t r;
    c->n = 2u + rnd(63u);                 /* domain size 2..64 */
    c->d = 2u + rnd(c->n - 1u);           /* 2..n values still possible */
    c->boundary = rnd(2u);
    sm_init(&poss, c->n);
    for (k = c->d; k < c->n; k++) sm_eliminate(&poss, k);
    if (c->boundary) {
      c->stakes = sm_support(&poss);      /* the bar exactly on S */
      c->formal = 1.0;
    } else {
      c->stakes = (double)rnd(1001u) / 1000.0;
      c->formal = (double)rnd(1001u) / 1000.0;
    }
    c->tau = c->stakes > 1.0 - c->formal ? c->stakes : 1.0 - c->formal;
    sm_decide(&poss, c->tau, 1u, SM_INTENSITY_SUPPORT, 0u, &r);
    c->allowed = r.verdict == SM_SPECULATED;
    c->intensity = r.intensity;
    at_boundary += c->boundary;
    allowed += (unsigned)c->allowed;

    fprintf(f, "let r%u = speculate({\"domain\": [", i);
    for (k = 0u; k < c->n; k++) fprintf(f, "%s%u", k ? ", " : "", k);
    fprintf(f, "], \"derivable\": [");
    for (k = 0u; k < c->d; k++) fprintf(f, "%s%u", k ? ", " : "", k);
    fprintf(f, "], \"stakes\": %.17g, \"formalizability\": %.17g})\n", c->stakes, c->formal);
    if (c->allowed) fprintf(f, "print(r%u == %.17g)\n", i, c->intensity);
    else fprintf(f, "print(is_groundless(r%u))\n", i);
  }
  fclose(f);

  sprintf(cmd, "node %s run %s", SMARSH_CLI, CASE_FILE);
  p = POPEN(cmd, "r");
  if (p == NULL) { printf("cannot run node\n"); return 1; }
  got = fread(OUT, 1u, sizeof OUT - 1u, p);
  OUT[got] = '\0';
  PCLOSE(p);
  remove(CASE_FILE);

  line = strtok(OUT, "\r\n");
  for (i = 0u; i < N_CASES; i++) {
    if (line == NULL) { printf("JS output ended after %u lines\n", i); return 1; }
    if (strcmp(line, "true") == 0) agree++;
    else {
      if (bad < 5u) {
        printf("  MISMATCH n=%u d=%u tau=%.17g: C %s %.17g, JS said %s\n", CASES[i].n,
               CASES[i].d, CASES[i].tau, CASES[i].allowed ? "speculates at" : "refuses",
               CASES[i].intensity, line);
      }
      bad++;
    }
    line = strtok(NULL, "\r\n");
  }
  printf("%u undetermined cases (%u with the bar exactly on S, %u speculated): "
         "%u agree, %u differ\n", N_CASES, at_boundary, allowed, agree, bad);
  return bad == 0u ? 0 : 1;
}
