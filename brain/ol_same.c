/*
 * ol_same.c -- one life on made-up senses, summed up in one number, to check
 * that the phone's brain (phone/index.html) lives exactly the same life as
 * this one. See phone/check_phone.mjs.
 */

#include <stdio.h>

#include "ob_life.h"

static ol_life_t L;

int main(void) {
  float in[OL_INPUT];
  unsigned long t;
  unsigned k, h = 0u;
  ol_born(&L, 42u, OL_PROGRESS);
  for (t = 0u; t < 20000u; t++) {
    for (k = 0u; k < OL_INPUT; k++) in[k] = (float)((t * 31u + k * 17u + (t * k) % 7u) % 101u) / 100.0f;
    h = h * 31u + ol_live(&L, in);
  }
  printf("%u %.9g\n", h, L.expect[3]);
  return 0;
}
