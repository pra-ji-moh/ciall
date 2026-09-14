/*
 * smarsh_interval.c -- see smarsh_interval.h.
 */

#include "smarsh_interval.h"

#include <math.h>

#define BIG 9007199254740992.0   /* 2^53: whole numbers below this are exact */
#define PI_LO 3.141592653589793
#define PI_HI 3.1415926535897936

static double down(double x, int n) {
  int i;
  for (i = 0; i < n; i++) x = nextafter(x, -INFINITY);
  return x;
}
static double up(double x, int n) {
  int i;
  for (i = 0; i < n; i++) x = nextafter(x, INFINITY);
  return x;
}

static int whole(double x) { return x == floor(x) && fabs(x) <= BIG; }

/* a result computed with rounding, widened to a guarantee */
static iv_t rounded(double lo, double hi, int ulps) {
  iv_t r;
  if (lo != lo || hi != hi) return iv_entire();   /* NaN: know nothing */
  r.lo = down(lo, ulps);
  r.hi = up(hi, ulps);
  r.exact = 0;
  return r;
}

/* exact when every input was exact and the result is still a whole
   number doubles hold exactly; otherwise widened */
static iv_t result(double lo, double hi, int inputs_exact) {
  iv_t r;
  if (inputs_exact && whole(lo) && whole(hi)) {
    r.lo = lo;
    r.hi = hi;
    r.exact = 1;
    return r;
  }
  return rounded(lo, hi, 1);
}

iv_t iv_make(double lo, double hi) { return rounded(lo, hi, 0); }
iv_t iv_int(double lo, double hi) {
  iv_t r;
  r.lo = ceil(lo);
  r.hi = floor(hi);
  r.exact = 1;
  return r;
}
iv_t iv_point(double x) {
  iv_t r;
  r.lo = r.hi = x;
  r.exact = whole(x);
  return r;
}
iv_t iv_empty(void) { iv_t r; r.lo = 1.0; r.hi = 0.0; r.exact = 1; return r; }
iv_t iv_entire(void) { iv_t r; r.lo = -INFINITY; r.hi = INFINITY; r.exact = 0; return r; }
int iv_is_empty(iv_t a) { return !(a.lo <= a.hi); }
double iv_width(iv_t a) { return iv_is_empty(a) ? 0.0 : a.hi - a.lo; }
double iv_mid(iv_t a) {
  if (a.lo == -INFINITY && a.hi == INFINITY) return 0.0;
  if (a.lo == -INFINITY) return a.hi - 1.0;
  if (a.hi == INFINITY) return a.lo + 1.0;
  return a.lo + (a.hi - a.lo) / 2.0;
}

iv_t iv_meet(iv_t a, iv_t b) {
  iv_t r;
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  r.lo = a.lo > b.lo ? a.lo : b.lo;
  r.hi = a.hi < b.hi ? a.hi : b.hi;
  r.exact = a.exact && b.exact;
  if (iv_is_empty(r)) return iv_empty();
  return r;
}

iv_t iv_hull(iv_t a, iv_t b) {
  iv_t r;
  if (iv_is_empty(a)) return b;
  if (iv_is_empty(b)) return a;
  r.lo = a.lo < b.lo ? a.lo : b.lo;
  r.hi = a.hi > b.hi ? a.hi : b.hi;
  r.exact = a.exact && b.exact;
  return r;
}

int iv_subset(iv_t a, iv_t b) {
  if (iv_is_empty(a)) return 1;
  if (iv_is_empty(b)) return 0;
  return a.lo >= b.lo && a.hi <= b.hi;
}

/* ---- arithmetic ------------------------------------------------------ */

iv_t iv_add(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  return result(a.lo + b.lo, a.hi + b.hi, a.exact && b.exact);
}

iv_t iv_sub(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  return result(a.lo - b.hi, a.hi - b.lo, a.exact && b.exact);
}

static double mul0(double x, double y) {   /* 0 times infinity is 0 here */
  if (x == 0.0 || y == 0.0) return 0.0;
  return x * y;
}

iv_t iv_mul(iv_t a, iv_t b) {
  double p[4], lo, hi;
  int i;
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  p[0] = mul0(a.lo, b.lo);
  p[1] = mul0(a.lo, b.hi);
  p[2] = mul0(a.hi, b.lo);
  p[3] = mul0(a.hi, b.hi);
  lo = hi = p[0];
  for (i = 1; i < 4; i++) {
    if (p[i] < lo) lo = p[i];
    if (p[i] > hi) hi = p[i];
  }
  return result(lo, hi, a.exact && b.exact);
}

iv_t iv_div(iv_t a, iv_t b) {
  double q[4], lo, hi;
  int i;
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  if (b.lo <= 0.0 && b.hi >= 0.0) {
    if (b.lo == 0.0 && b.hi == 0.0) return iv_empty();   /* division by zero: no value */
    return iv_entire();
  }
  q[0] = a.lo / b.lo;
  q[1] = a.lo / b.hi;
  q[2] = a.hi / b.lo;
  q[3] = a.hi / b.hi;
  lo = hi = q[0];
  for (i = 1; i < 4; i++) {
    if (q[i] != q[i]) return iv_entire();
    if (q[i] < lo) lo = q[i];
    if (q[i] > hi) hi = q[i];
  }
  return rounded(lo, hi, 1);   /* a quotient of whole numbers need not be whole */
}

iv_t iv_neg(iv_t a) {
  iv_t r;
  if (iv_is_empty(a)) return iv_empty();
  r.lo = -a.hi;
  r.hi = -a.lo;
  r.exact = a.exact;
  return r;
}

iv_t iv_abs(iv_t a) {
  iv_t r;
  if (iv_is_empty(a)) return iv_empty();
  if (a.lo >= 0.0) return a;
  if (a.hi <= 0.0) return iv_neg(a);
  r.lo = 0.0;
  r.hi = -a.lo > a.hi ? -a.lo : a.hi;
  r.exact = a.exact;
  return r;
}

iv_t iv_min(iv_t a, iv_t b) {
  iv_t r;
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  r.lo = a.lo < b.lo ? a.lo : b.lo;
  r.hi = a.hi < b.hi ? a.hi : b.hi;
  r.exact = a.exact && b.exact;
  return r;
}

iv_t iv_max(iv_t a, iv_t b) {
  iv_t r;
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  r.lo = a.lo > b.lo ? a.lo : b.lo;
  r.hi = a.hi > b.hi ? a.hi : b.hi;
  r.exact = a.exact && b.exact;
  return r;
}

iv_t iv_powi(iv_t a, int n) {
  iv_t r;
  int i;
  if (iv_is_empty(a)) return iv_empty();
  if (n <= 0) return iv_point(1.0);
  if (n % 2 == 1) {   /* odd: increasing */
    double lo = 1.0, hi = 1.0;
    for (i = 0; i < n; i++) { lo *= a.lo; hi *= a.hi; }
    return result(lo, hi, a.exact);
  }
  /* even: through zero it is 0 at the bottom */
  r = iv_abs(a);
  {
    double lo = 1.0, hi = 1.0;
    for (i = 0; i < n; i++) { lo *= r.lo; hi *= r.hi; }
    return result(lo, hi, a.exact);
  }
}

iv_t iv_sqrt(iv_t a) {
  a = iv_meet(a, iv_make(0.0, INFINITY));
  if (iv_is_empty(a)) return iv_empty();   /* undefined on the whole region */
  if (a.exact && a.lo == a.hi) {
    double s = sqrt(a.lo);
    if (s == floor(s) && s * s == a.lo) return iv_point(s);   /* a perfect square */
  }
  /* a square root is never negative, whatever the rounding says */
  return iv_meet(rounded(sqrt(a.lo), sqrt(a.hi), 1), iv_make(0.0, INFINITY));
}

iv_t iv_exp(iv_t a) {
  if (iv_is_empty(a)) return iv_empty();
  return iv_meet(rounded(exp(a.lo), exp(a.hi), IV_LIBM_SLACK), iv_make(0.0, INFINITY));
}

iv_t iv_log(iv_t a) {
  a = iv_meet(a, iv_make(0.0, INFINITY));
  if (iv_is_empty(a) || a.hi <= 0.0) return iv_empty();
  return rounded(a.lo <= 0.0 ? -INFINITY : log(a.lo), log(a.hi), IV_LIBM_SLACK);
}

/* is some x = offset + 2 k pi inside [lo, hi], allowing for pi's rounding? */
static int hits(double lo, double hi, double offset) {
  double k0 = ceil((lo - offset) / (2.0 * PI_HI) - 1e-9);
  double k1 = floor((hi - offset) / (2.0 * PI_LO) + 1e-9);
  return k0 <= k1;
}

iv_t iv_sin(iv_t a) {
  double lo, hi, s0, s1;
  if (iv_is_empty(a)) return iv_empty();
  if (a.hi - a.lo >= 6.3 || a.lo == -INFINITY || a.hi == INFINITY) return iv_make(-1.0, 1.0);
  s0 = sin(a.lo);
  s1 = sin(a.hi);
  lo = s0 < s1 ? s0 : s1;
  hi = s0 > s1 ? s0 : s1;
  if (hits(a.lo, a.hi, PI_LO / 2.0)) hi = 1.0;
  if (hits(a.lo, a.hi, -PI_LO / 2.0)) lo = -1.0;
  return iv_meet(rounded(lo, hi, IV_LIBM_SLACK), iv_make(-1.0, 1.0));
}

iv_t iv_cos(iv_t a) {
  double lo, hi, c0, c1;
  if (iv_is_empty(a)) return iv_empty();
  if (a.hi - a.lo >= 6.3 || a.lo == -INFINITY || a.hi == INFINITY) return iv_make(-1.0, 1.0);
  c0 = cos(a.lo);
  c1 = cos(a.hi);
  lo = c0 < c1 ? c0 : c1;
  hi = c0 > c1 ? c0 : c1;
  if (hits(a.lo, a.hi, 0.0)) hi = 1.0;
  if (hits(a.lo, a.hi, PI_LO)) lo = -1.0;
  return iv_meet(rounded(lo, hi, IV_LIBM_SLACK), iv_make(-1.0, 1.0));
}

iv_t iv_mod(iv_t a, iv_t m) {
  double k0, k1, M;
  if (iv_is_empty(a) || iv_is_empty(m)) return iv_empty();
  if (!a.exact || !m.exact || m.lo != m.hi || m.lo <= 0.0) return iv_entire();
  M = m.lo;
  k0 = floor(a.lo / M);
  k1 = floor(a.hi / M);
  if (k0 == k1) return iv_int(a.lo - k0 * M, a.hi - k0 * M);
  return iv_int(0.0, M - 1.0);
}

/* ---- truth values ------------------------------------------------------ */

static iv_t truth(int lo, int hi) { return iv_int((double)lo, (double)hi); }

iv_t iv_lt(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  if (a.hi < b.lo) return truth(1, 1);
  if (a.lo >= b.hi) return truth(0, 0);
  return truth(0, 1);
}

iv_t iv_le(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  if (a.hi <= b.lo) return truth(1, 1);
  if (a.lo > b.hi) return truth(0, 0);
  return truth(0, 1);
}

iv_t iv_eq(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  if (a.lo == a.hi && b.lo == b.hi && a.lo == b.lo) {
    /* two one-point intervals: each holds exactly one number, the true
       value (a rounded result is always widened, so it is never a point) */
    return truth(1, 1);
  }
  if (a.hi < b.lo || b.hi < a.lo) return truth(0, 0);
  return truth(0, 1);
}

iv_t iv_and(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  return truth(a.lo < b.lo ? (int)a.lo : (int)b.lo, a.hi < b.hi ? (int)a.hi : (int)b.hi);
}

iv_t iv_or(iv_t a, iv_t b) {
  if (iv_is_empty(a) || iv_is_empty(b)) return iv_empty();
  return truth(a.lo > b.lo ? (int)a.lo : (int)b.lo, a.hi > b.hi ? (int)a.hi : (int)b.hi);
}

iv_t iv_not(iv_t a) {
  if (iv_is_empty(a)) return iv_empty();
  return truth(1 - (int)a.hi, 1 - (int)a.lo);
}

int iv_true(iv_t a) { return !iv_is_empty(a) && a.lo >= 1.0; }
int iv_false(iv_t a) { return !iv_is_empty(a) && a.hi <= 0.0; }

iv_t iv_to_int(iv_t a) {
  if (iv_is_empty(a)) return iv_empty();
  return iv_int(a.lo, a.hi);
}
