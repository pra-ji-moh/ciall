/*
 * boundary_kernel.c -- see boundary_kernel.h for the full disclosure.
 * Mirrors ../src/lib/boundaryKernel.js exactly. Compiled and run:
 * test_boundary_kernel.c passes.
 */

#include "boundary_kernel.h"

/* Bounded string length: the loop bound is BK_MAX_PATH_LEN, a
   compile-time constant -- NEVER the actual length of `s`, which is
   exactly the property that makes this WCET-analyzable (see the header
   disclosure). Returns BK_MAX_PATH_LEN if `s` has no NUL within that
   many characters, signaling "too long" to the caller rather than
   reading past an unbounded/unterminated buffer. */
static unsigned int bk_bounded_strlen(const char *s) {
  unsigned int i;
  for (i = 0; i < BK_MAX_PATH_LEN; i++) {
    if (s[i] == '\0') return i;
  }
  return BK_MAX_PATH_LEN;
}

/* Copies `in` into `out` (both statically sized BK_MAX_PATH_LEN),
   translating '\' to '/' and then stripping trailing '/' characters --
   exactly boundaryKernel.js's `norm()`. `len` must already be a
   verified in-bounds length (< BK_MAX_PATH_LEN) from
   bk_bounded_strlen(); this function performs no further length
   checking of its own, keeping its own loop bounds unconditionally
   fixed at BK_MAX_PATH_LEN. */
static void bk_normalize(const char *in, unsigned int len, char out[BK_MAX_PATH_LEN]) {
  unsigned int i;
  unsigned int end;
  for (i = 0; i < BK_MAX_PATH_LEN; i++) {
    if (i < len) {
      out[i] = (in[i] == '\\') ? '/' : in[i];
    } else {
      out[i] = '\0';
      break;
    }
  }
  end = len;
  for (i = 0; i < BK_MAX_PATH_LEN; i++) {
    if (end == 0 || out[end - 1] != '/') break;
    end--;
  }
  out[end] = '\0';
}

/* Bounded exact-prefix check: does `s` start with `prefix` followed
   immediately by '/'? Both strings are already NUL-terminated,
   in-bounds buffers of size BK_MAX_PATH_LEN; the loop bound is that
   fixed constant, not either string's actual length. */
static int bk_starts_with_slash_suffix(const char *s, const char *prefix) {
  unsigned int i;
  for (i = 0; i < BK_MAX_PATH_LEN; i++) {
    if (prefix[i] == '\0') {
      return s[i] == '/';
    }
    if (s[i] != prefix[i]) return 0;
    if (s[i] == '\0') return 0; /* s ended before prefix did */
  }
  return 0; /* unreachable given both inputs are already bounded, kept for a total function */
}

static int bk_streq(const char *a, const char *b) {
  unsigned int i;
  for (i = 0; i < BK_MAX_PATH_LEN; i++) {
    if (a[i] != b[i]) return 0;
    if (a[i] == '\0') return 1;
  }
  return 1; /* both ran the full fixed length identically */
}

static void bk_copy_detail(bk_result_t *out, const char *msg) {
  unsigned int i;
  for (i = 0; i < BK_MAX_DETAIL_LEN - 1 && msg[i] != '\0'; i++) {
    out->detail[i] = msg[i];
  }
  out->detail[i] = '\0';
}

bk_status_t bk_check_path_containment(const char *target, const char *boundary, bk_result_t *out) {
  char norm_target[BK_MAX_PATH_LEN];
  char norm_boundary[BK_MAX_PATH_LEN];
  unsigned int target_len;
  unsigned int boundary_len;
  int within;

  target_len = bk_bounded_strlen(target);
  if (target_len >= BK_MAX_PATH_LEN) return BK_ERR_TARGET_TOO_LONG;
  if (target_len == 0) return BK_ERR_EMPTY_TARGET;

  boundary_len = bk_bounded_strlen(boundary);
  if (boundary_len >= BK_MAX_PATH_LEN) return BK_ERR_BOUNDARY_TOO_LONG;

  bk_normalize(target, target_len, norm_target);
  bk_normalize(boundary, boundary_len, norm_boundary);

  within = bk_streq(norm_target, norm_boundary) || bk_starts_with_slash_suffix(norm_target, norm_boundary);

  out->verdict = within ? BK_VERDICT_HELD : BK_VERDICT_VIOLATED;
  bk_copy_detail(out, within
    ? "target is within the granted boundary"
    : "target is outside the granted boundary (this check does not resolve symlinks; a real executor must realpath before acting)");
  return BK_OK;
}

bk_status_t bk_check_allowlist(const char *target, const bk_allowlist_entry_t *entries, unsigned int entry_count, bk_result_t *out) {
  unsigned int i;
  unsigned int target_len;
  int within = 0;

  target_len = bk_bounded_strlen(target);
  if (target_len >= BK_MAX_PATH_LEN) return BK_ERR_TARGET_TOO_LONG;
  if (target_len == 0) return BK_ERR_EMPTY_TARGET;
  if (entry_count > BK_MAX_ALLOWLIST_ENTRIES) return BK_ERR_TOO_MANY_ALLOWLIST_ENTRIES;

  /* Loop bound is BK_MAX_ALLOWLIST_ENTRIES, a compile-time constant --
     entry_count is already checked <= it above, so this loop's WORST
     case is still the fixed constant, never an unbounded caller value. */
  for (i = 0; i < BK_MAX_ALLOWLIST_ENTRIES; i++) {
    if (i >= entry_count) break;
    if (bk_streq(target, entries[i].text)) { within = 1; break; }
  }

  out->verdict = within ? BK_VERDICT_HELD : BK_VERDICT_VIOLATED;
  bk_copy_detail(out, within ? "target is within the granted boundary" : "target is outside the granted boundary");
  return BK_OK;
}
