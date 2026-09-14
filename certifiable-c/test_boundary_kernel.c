/*
 * test_boundary_kernel.c -- verifies boundary_kernel.c, which was written
 * with no compiler present. It now builds and passes; ../build_c.sh runs it.
 *
 * The algorithm itself was cross-checked against the real, already-
 * tested ../src/lib/boundaryKernel.js on these exact cases via a JS
 * simulation of the same bounded-loop logic (0 mismatches across 10
 * cases) -- that validates the LOGIC, not the C syntax. This file is
 * what actually validates the C syntax/compilation. Build with:
 *   gcc -std=c99 -Wall -Wextra -pedantic -o test_boundary_kernel \
 *       boundary_kernel.c test_boundary_kernel.c && ./test_boundary_kernel
 * A nonzero exit code means at least one case failed.
 */

#include <stdio.h>
#include <string.h>
#include "boundary_kernel.h"

static int failures = 0;

static void expect_path(const char *target, const char *boundary, bk_verdict_t expected) {
  bk_result_t result;
  bk_status_t status = bk_check_path_containment(target, boundary, &result);
  if (status != BK_OK) {
    printf("FAIL (unexpected status %d) target=\"%s\" boundary=\"%s\"\n", (int)status, target, boundary);
    failures++;
    return;
  }
  if (result.verdict != expected) {
    printf("FAIL target=\"%s\" boundary=\"%s\": got verdict=%d expected=%d (detail: %s)\n",
           target, boundary, (int)result.verdict, (int)expected, result.detail);
    failures++;
  } else {
    printf("PASS target=\"%s\" boundary=\"%s\": verdict=%d\n", target, boundary, (int)result.verdict);
  }
}

static void expect_allowlist(const char *target, const bk_allowlist_entry_t *entries, unsigned int count, bk_verdict_t expected) {
  bk_result_t result;
  bk_status_t status = bk_check_allowlist(target, entries, count, &result);
  if (status != BK_OK) {
    printf("FAIL (unexpected status %d) target=\"%s\"\n", (int)status, target);
    failures++;
    return;
  }
  if (result.verdict != expected) {
    printf("FAIL allowlist target=\"%s\": got verdict=%d expected=%d\n", target, (int)result.verdict, (int)expected);
    failures++;
  } else {
    printf("PASS allowlist target=\"%s\": verdict=%d\n", target, (int)result.verdict);
  }
}

int main(void) {
  bk_allowlist_entry_t entries[2];
  bk_result_t err_result;
  char too_long[BK_MAX_PATH_LEN + 10];
  unsigned int i;

  /* Same cases the JS reference's own test suite exercises
     (tests/registry.test.mjs: "path-containment holds inside, violates
     outside and on a look-alike sibling", "allowlist holds only for an
     exact match"), plus the backslash-normalization and trailing-slash
     cases boundaryKernel.js's norm() specifically handles. */
  expect_path("/sandbox/project", "/sandbox/project", BK_VERDICT_HELD);
  expect_path("/sandbox/project/file.txt", "/sandbox/project", BK_VERDICT_HELD);
  expect_path("/sandbox/project", "/sandbox", BK_VERDICT_HELD);
  expect_path("/sandbox/projectile", "/sandbox/project", BK_VERDICT_VIOLATED); /* look-alike sibling -- the case that actually exercises the "+/" suffix check */
  expect_path("/other/path", "/sandbox/project", BK_VERDICT_VIOLATED);
  expect_path("C:\\sandbox\\project\\file.txt", "C:/sandbox/project", BK_VERDICT_HELD); /* backslash normalization */
  expect_path("/sandbox/project/", "/sandbox/project", BK_VERDICT_HELD); /* trailing slash stripped */

  /* Zero the whole struct first, not just the bytes strictly needed --
     MISRA-C-style discipline: never leave a fixed buffer partially
     uninitialized even when the current logic happens not to read the
     untouched tail, since that safety property is easy to break by a
     LATER, unrelated change to bk_streq that this test wouldn't catch
     until it actually read garbage. */
  memset(&entries[0], 0, sizeof(entries[0]));
  memset(&entries[1], 0, sizeof(entries[1]));
  memcpy(entries[0].text, "acme", 5);
  memcpy(entries[1].text, "globex", 7);
  expect_allowlist("acme", entries, 2, BK_VERDICT_HELD);
  expect_allowlist("initech", entries, 2, BK_VERDICT_VIOLATED);
  expect_allowlist("ACME", entries, 2, BK_VERDICT_VIOLATED); /* case-sensitive exact match only, same as the JS reference */

  /* Bounded-input error paths: these must be checked, explicit errors,
     never a silent truncation or an out-of-bounds read. */
  for (i = 0; i < sizeof(too_long) - 1; i++) too_long[i] = 'a';
  too_long[sizeof(too_long) - 1] = '\0';
  if (bk_check_path_containment(too_long, "/sandbox", &err_result) != BK_ERR_TARGET_TOO_LONG) {
    printf("FAIL: overlong target was not rejected with BK_ERR_TARGET_TOO_LONG\n");
    failures++;
  } else {
    printf("PASS: overlong target correctly rejected, not truncated\n");
  }
  if (bk_check_path_containment("", "/sandbox", &err_result) != BK_ERR_EMPTY_TARGET) {
    printf("FAIL: empty target was not rejected with BK_ERR_EMPTY_TARGET\n");
    failures++;
  } else {
    printf("PASS: empty target correctly rejected\n");
  }

  printf("\n%d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
