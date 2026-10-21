/*
 * ol_wasm.c -- the being's life on a phone: ob_life.c, compiled to
 * WebAssembly, with nothing else. The page (phone/) writes what the camera,
 * the microphone, the screen and the motion sensors bring into `ol_input`, and
 * acts out what `ol_step` returns.
 *
 *   clang --target=wasm32 -std=c99 -O2 -nostdlib -DOL_MEMORY=30000u \
 *         -Wl,--no-entry -Wl,--export-dynamic -o life.wasm ob_life.c ol_wasm.c
 *
 * On the phone its memory holds the last 30000 moments (about forty minutes
 * of its life), so that it fits in the browser's storage.
 */

#include "ob_life.h"

/* the compiler may call these for copying and clearing; there is no library */
void *memset(void *p, int c, unsigned long n) {
  unsigned char *d = (unsigned char *)p;
  while (n--) *d++ = (unsigned char)c;
  return p;
}

void *memcpy(void *to, const void *from, unsigned long n) {
  unsigned char *d = (unsigned char *)to;
  const unsigned char *s = (const unsigned char *)from;
  while (n--) *d++ = *s++;
  return to;
}

static ol_life_t LIFE;
static float INPUT[OL_INPUT];

#define EXPORT __attribute__((visibility("default")))

EXPORT void ol_begin(unsigned seed) { ol_born(&LIFE, seed, OL_PROGRESS); }
EXPORT float *ol_input(void) { return INPUT; }
EXPORT unsigned ol_step(void) { return ol_live(&LIFE, INPUT); }

/* its whole self, to keep between visits */
EXPORT void *ol_self(void) { return &LIFE; }
EXPORT unsigned ol_self_size(void) { return (unsigned)sizeof LIFE; }

/* to watch it */
EXPORT const float *ol_surprise(void) { return LIFE.surprise; }
EXPORT const float *ol_expect(void) { return LIFE.expect; }
EXPORT const float *ol_err(void) { return LIFE.err; }
EXPORT const float *ol_urge(void) { return LIFE.urge; }
EXPORT float ol_dopamine(void) { return LIFE.dopamine; }
EXPORT double ol_moments(void) { return (double)LIFE.moments; }
EXPORT double ol_acts(unsigned m) { return m < OL_MOTORS ? (double)LIFE.acts[m] : 0.0; }
EXPORT float ol_curious(unsigned g, unsigned k) { return ol_progress(&LIFE, g, k); }
/* coded optogenetics: light the memory of one moment; what it brought is then in ol_lit() */
static float LIT[OL_SENSE];
EXPORT int ol_light_moment(double moment) { return moment >= 0.0 && ol_light(&LIFE, (unsigned long)moment, LIT); }
EXPORT const float *ol_lit(void) { return LIT; }
EXPORT double ol_recalls(unsigned g) { return g < OL_GROUPS ? (double)LIFE.recalls[g] : 0.0; }
EXPORT double ol_capacity(void) { return (double)OL_MEMORY; }
EXPORT unsigned ol_visits(unsigned g, unsigned k) {
  return g < OL_GROUPS && k < OL_PLACES ? LIFE.visits[g][k] : 0u;
}
