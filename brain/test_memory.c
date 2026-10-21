/*
 * test_memory.c -- its eidetic memory, and our coded optogenetics.
 *
 * Checked:
 *   eidetic       every moment it has lived is kept as it came: lighting a
 *                 moment's memory brings back exactly what it saw, heard and
 *                 felt then, near the start of its life and near the end
 *   unseen        lighting a memory changes nothing in it: it never knows
 *   a glimpse     a glimpse of a moment, blurred by noise, brings back the
 *                 moment itself (pattern completion)
 *   once          a song it has heard once, it expects much of the second
 *                 time (not where it made a sound of its own over it one time
 *                 and not the other: its own voice changes what it hears); a
 *                 twin without the memory, its cerebellum alone, far less
 *   Funes         where the world is random, memory is no guide: it learns
 *                 not to trust it there, and its mother's answers stay as
 *                 expected as without memory
 */

#include <stdio.h>
#include <string.h>

#include "ob_crib.h"

static unsigned n_checks = 0u, n_fail = 0u;

static void check(const char *name, int ok) {
  n_checks++;
  printf("  %s  %s\n", ok ? "ok  " : "FAIL", name);
  if (!ok) n_fail++;
}

#define LIFE 100000ul
#define NOTES 30u

static ol_life_t L, T, before;
static ob_crib_t C;
static float SAW_EARLY[OL_SENSE], SAW_LATE[OL_SENSE];
static float SONG[NOTES][OL_EAR];

static unsigned rng = 12345u;
static float unit(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return (float)(rng >> 8) / 16777216.0f;
}

/* live n moments in the crib; if song >= 0, the mother sings it from now on */
static double live(ol_life_t *me, unsigned long n, int sing) {
  float in[OL_INPUT];
  unsigned long t;
  double err = 0.0;
  for (t = 0u; t < n; t++) {
    unsigned bits;
    ob_crib_sense(&C, in);
    if (sing && t < NOTES) memcpy(in + OL_VIS, SONG[t], sizeof SONG[t]);
    if (me->moments == 1000u) memcpy(SAW_EARLY, in, sizeof SAW_EARLY);
    if (me->moments == LIFE - 1000u) memcpy(SAW_LATE, in, sizeof SAW_LATE);
    bits = ol_live(me, in);
    if (sing && t >= 3u && t < NOTES) err += me->err[OL_HEAR];
    ob_crib_act(&C, bits);
  }
  return err;
}

static float worst(const float *a, const float *b) {
  float w = 0.0f;
  unsigned i;
  for (i = 0u; i < OL_SENSE; i++) {
    float d = a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
    if (d > w) w = d;
  }
  return w;
}

int main(void) {
  float got[OL_SENSE];
  unsigned i, j;

  printf("ITS EIDETIC MEMORY, AND OUR CODED OPTOGENETICS\n\n");
  ol_born(&L, 7u, OL_PROGRESS);
  ob_crib_make(&C, 7u);
  live(&L, LIFE, 0);

  {
    int early = ol_light(&L, 1000u, got);
    float e = early ? worst(got, SAW_EARLY) : 1.0f, l;
    int late = ol_light(&L, LIFE - 1000u, got);
    l = late ? worst(got, SAW_LATE) : 1.0f;
    printf("        (lit moment 1000: off by at most %.4f; moment %lu: %.4f)\n", e, LIFE - 1000u, l);
    check("eidetic: lighting a moment from early in its life brings back exactly what came then", e <= 0.5f / 255.0f + 1e-6f);
    check("eidetic: and one from late in its life", l <= 0.5f / 255.0f + 1e-6f);
    check("a moment it has not lived yet holds nothing", !ol_light(&L, LIFE + 5u, got));
  }
  {
    memcpy(&before, &L, sizeof before);
    for (i = 0u; i < 1000u; i++) ol_light(&L, i * 50u, got);
    check("unseen: lighting its memories changes nothing in it", memcmp(&before, &L, sizeof before) == 0);
  }
  {
    /* a glimpse: a moment's seeing, blurred by noise; does it bring that moment back? */
    unsigned found = 0u, tried = 0u;
    for (i = 0u; i < 200u; i++) {
      unsigned long m = 5000u + i * 450u;
      unsigned slot = (unsigned)(m % OL_MEMORY);
      float cue[OL_SENSE], orig[OL_SENSE];
      long back;
      if (!ol_light(&L, m, cue)) continue;
      memcpy(orig, cue, sizeof orig);
      for (j = 0u; j < OL_VIS; j++) cue[j] += (unit() - 0.5f) * 0.03f;
      back = ol_complete(&L, cue, L.hip.acts[slot]);
      tried++;
      if (back >= 0) {
        float was[OL_SENSE], w = 0.0f;
        ol_light(&L, (unsigned long)back, was);
        for (j = 0u; j < OL_VIS; j++) {
          float d = was[j] > orig[j] ? was[j] - orig[j] : orig[j] - was[j];
          if (d > w) w = d;
        }
        if (w < 0.02f) found++;   /* that moment, or one that looked just the same */
      }
    }
    printf("        (a blurred glimpse brought back its moment %u times in %u)\n", found, tried);
    check("a glimpse: a blurred glimpse of a moment brings the moment back (4 times in 5 or more)", found * 5u >= tried * 4u);
  }
  {
    /* a song, heard once; later, again. And a twin, the same in all but memory */
    double first, second, first_t, second_t;
    ob_crib_t keep;
    for (i = 0u; i < NOTES; i++)
      for (j = 0u; j < OL_EAR; j++) SONG[i][j] = 0.2f + 0.8f * unit();
    memcpy(&T, &L, sizeof T);
    T.remembers = 0u;
    memcpy(&keep, &C, sizeof keep);
    first = live(&L, NOTES, 1);
    live(&L, 2000u, 0);
    second = live(&L, NOTES, 1);
    memcpy(&C, &keep, sizeof C);
    first_t = live(&T, NOTES, 1);
    live(&T, 2000u, 0);
    second_t = live(&T, NOTES, 1);
    printf("        (surprise through the song: with memory %.2f, then %.2f; without, %.2f, then %.2f)\n", first,
           second, first_t, second_t);
    check("once: a song heard once, it expects much of it the second time (half the surprise or less)",
          second < 0.5 * first);
    check("once: a twin without the memory, its cerebellum alone, expects far less of it (1.5x the surprise or more)",
          second_t > 1.5 * second);
  }
  {
    /* Funes: where the world is random, memory is no guide */
    static ol_life_t R;
    double with[OC_EXAMS], without[OC_EXAMS];
    ob_crib_examine(&L, 99u, 40000u, with);
    memcpy(&R, &L, sizeof R);
    R.remembers = 0u;
    ob_crib_examine(&R, 99u, 40000u, without);
    printf("        (its mother's answers still surprise it %.2f with its memory, %.2f with its cerebellum alone)\n",
           with[OC_ANSWER], without[OC_ANSWER]);
    check("Funes: where the world is random it learns not to trust its memory (her answers no more surprising for it)",
          with[OC_ANSWER] < without[OC_ANSWER] + 0.1);
    check("and where the world repeats, memory makes it sharper: the wall as its eyes move",
          with[OC_EYES] <= without[OC_EYES]);
  }
  printf("\n%u checks, %u failed\n", n_checks, n_fail);
  return n_fail == 0u ? 0 : 1;
}
