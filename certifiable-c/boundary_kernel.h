/*
 * boundary_kernel.h -- a statically-allocated, no-heap C port of
 * boundaryKernel.js, upgrade 11.
 *
 * ============================================================
 * DISCLOSURE, READ THIS BEFORE TRUSTING ANYTHING IN THIS DIRECTORY:
 * ============================================================
 * This was written with no C compiler available, mirroring the tested
 * JS reference (../src/lib/boundaryKernel.js) line for line, and said
 * so. It has since been compiled (zig cc 0.16.0, clang 21, strict C99,
 * -Wall -Wextra -pedantic: zero warnings) and test_boundary_kernel.c
 * passes with 0 failures, on its first build. ../build_c.sh reruns it.
 * Compiled and tested is still not certified; see below.
 *
 * ============================================================
 * WHAT THIS DOES AND DOES NOT PROVE ABOUT CERTIFICATION:
 * ============================================================
 * This demonstrates that the ALGORITHM (scope/path containment
 * checking) has no inherent dependency on garbage collection or
 * dynamic allocation -- it was never a hard requirement of the LOGIC,
 * only of the language it happened to be written in first. That is a
 * real, meaningful fact. It is NOT, by itself, DO-178C DAL A
 * certification, or evidence toward it, because certification requires
 * things no amount of C source code can provide on its own:
 *   - A QUALIFIED toolchain (the COMPILER needs DO-330 tool
 *     qualification evidence from its vendor -- a commercial/legal
 *     relationship, not a compile flag).
 *   - Formal WCET (worst-case execution time) proof via a real static
 *     timing analyzer CHARACTERIZED AGAINST ACTUAL TARGET HARDWARE
 *     (e.g. AbsInt aiT, Bound-T) -- "the loops are bounded" (true,
 *     see below) is necessary but nowhere near sufficient; WCET proof
 *     needs cycle-accurate knowledge of cache behavior, pipeline
 *     stalls, and the specific processor this would run on.
 *   - MC/DC coverage evidence from a QUALIFIED coverage tool, gathered
 *     under an approved test procedure.
 *   - A traceability matrix from certified REQUIREMENTS (not just code
 *     comments) through design, code, and test.
 *   - Actual engagement with a certification authority (FAA/EASA) or a
 *     delegated authority (a DER).
 * None of that exists here or anywhere in this repository. See
 * ../CERTIFICATION-GAPS.md for the full, honest accounting.
 *
 * ============================================================
 * STATIC-ALLOCATION / BOUNDED-EXECUTION DISCIPLINE ACTUALLY FOLLOWED:
 * ============================================================
 *   - Every buffer is a fixed-size stack array, sized by a compile-time
 *     constant (BK_MAX_PATH_LEN, BK_MAX_ALLOWLIST_ENTRIES). No malloc,
 *     no free, no realloc, anywhere in this file or boundary_kernel.c.
 *   - Every loop's iteration count is bounded by a compile-time
 *     constant (BK_MAX_PATH_LEN), NEVER by unbounded caller-supplied
 *     data -- this is the property that actually matters for WCET
 *     analysis: a loop bounded by "however long the input string is"
 *     is not statically WCET-provable without ALSO statically bounding
 *     every possible input, which callers of a general-purpose function
 *     cannot promise; a loop bounded by a FIXED constant already baked
 *     into the binary is. Overlong input is a checked ERROR return
 *     (bk_result.status == BK_ERR_TARGET_TOO_LONG etc.), never a
 *     silently-truncated result and never an unbounded loop.
 *   - No recursion anywhere (recursion depth is not generally statically
 *     WCET-bounded without dedicated analysis; simple iteration is).
 *   - No dynamic dispatch (no function pointers used for a "known set
 *     of kernel behaviors" -- both boundary kinds are handled by
 *     ordinary if/else, resolvable entirely at compile time).
 */

#ifndef CIALL_BOUNDARY_KERNEL_H
#define CIALL_BOUNDARY_KERNEL_H

#define BK_MAX_PATH_LEN 4096u          /* mirrors a generous real filesystem path limit */
#define BK_MAX_ALLOWLIST_ENTRIES 64u   /* mirrors this repo's other MAX_* registry-entry-count conventions */
#define BK_MAX_DETAIL_LEN 256u

typedef enum {
  BK_KIND_PATH_CONTAINMENT = 0,
  BK_KIND_ALLOWLIST = 1
} bk_kind_t;

typedef enum {
  BK_OK = 0,
  BK_ERR_TARGET_TOO_LONG = 1,
  BK_ERR_BOUNDARY_TOO_LONG = 2,
  BK_ERR_TOO_MANY_ALLOWLIST_ENTRIES = 3,
  BK_ERR_EMPTY_TARGET = 4
} bk_status_t;

typedef enum {
  BK_VERDICT_HELD = 0,
  BK_VERDICT_VIOLATED = 1
} bk_verdict_t;

/* A single allowlist entry, fixed-size, no pointer into caller-owned
   memory of unknown lifetime -- every string this module touches is
   copied into a buffer THIS module owns and sized. */
typedef struct {
  char text[BK_MAX_PATH_LEN];
} bk_allowlist_entry_t;

typedef struct {
  bk_verdict_t verdict;
  char detail[BK_MAX_DETAIL_LEN];
} bk_result_t;

/*
 * Path-containment check: is `target` equal to `boundary`, or a path
 * segment descendant of it? Mirrors boundaryKernel.js's
 * pathWithinBoundary() exactly: normalize backslashes to forward
 * slashes, strip trailing slashes, then compare (equal, or
 * target starts with boundary + "/").
 *
 * Does NOT resolve symlinks (same disclosed limitation as the JS
 * version) -- a real executor MUST realpath() before acting; this
 * check alone cannot see through a symlink that escapes the boundary.
 *
 * Returns BK_OK and fills *out on success; returns an error status
 * (and leaves *out untouched) if either input exceeds
 * BK_MAX_PATH_LEN-1 characters -- an explicit, checked rejection, never
 * silent truncation.
 */
bk_status_t bk_check_path_containment(const char *target, const char *boundary, bk_result_t *out);

/*
 * Allowlist check: is `target` exactly equal to one of the first
 * `entry_count` entries in `entries`? Mirrors boundaryKernel.js's
 * `spec.boundary.includes(spec.target)` exactly -- exact string match,
 * no normalization (allowlist entries are opaque tokens, not paths).
 *
 * `entry_count` beyond BK_MAX_ALLOWLIST_ENTRIES is a checked error, not
 * a silent truncation to the first BK_MAX_ALLOWLIST_ENTRIES entries.
 */
bk_status_t bk_check_allowlist(const char *target, const bk_allowlist_entry_t *entries, unsigned int entry_count, bk_result_t *out);

#endif /* CIALL_BOUNDARY_KERNEL_H */
