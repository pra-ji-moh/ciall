// mathExpr.js; a tiny, safe math-expression compiler.
//
// The visualization engine (visualSpec.js / NodeVisual.jsx) renders
// function plots from expressions the MODEL writes ("sin(x)/x",
// "exp(-t)*cos(6*t)"). Executing model-written strings with eval() or
// new Function() is exactly what this app's CSP forbids (script-src has
// no 'unsafe-eval'; deliberately, see netlify.toml). So: a
// recursive-descent parser over a closed grammar of numbers, arithmetic,
// and a whitelist of Math functions. Nothing outside the whitelist can
// be expressed, so nothing outside it can execute; a model emitting
// "fetch('...')" gets a parse error, not a network call.
//
// Grammar:  expr := term (('+'|'-') term)*
//           term := unary (('*'|'/'|'%') unary)*
//           unary := '-' unary | power
//           power := atom ('^' unary)?          (right-associative)
//           atom := number | ident['(' args ')'] | '(' expr ')'

export const FUNCS = {
  sin: Math.sin, cos: Math.cos, tan: Math.tan,
  asin: Math.asin, acos: Math.acos, atan: Math.atan, atan2: Math.atan2,
  sinh: Math.sinh, cosh: Math.cosh, tanh: Math.tanh,
  exp: Math.exp, log: Math.log, ln: Math.log, log2: Math.log2, log10: Math.log10,
  sqrt: Math.sqrt, cbrt: Math.cbrt, abs: Math.abs, sign: Math.sign,
  floor: Math.floor, ceil: Math.ceil, round: Math.round,
  min: Math.min, max: Math.max, pow: Math.pow,
  mod: (a, b) => ((a % b) + b) % b,
};

export const CONSTS = { pi: Math.PI, e: Math.E, tau: 2 * Math.PI };

export function tokenize(src) {
  const tokens = [];
  let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (c === ' ' || c === '\t' || c === '\n') { i++; continue; }
    if (/[0-9.]/.test(c)) {
      const m = src.slice(i).match(/^\d*\.?\d+(?:[eE][+-]?\d+)?/);
      if (!m) throw new Error(`Bad number at ${i}`);
      // Keep the raw literal text alongside the float parse: exact mode
      // must build its BigInt from the STRING, never from `v`; routing
      // a large integer literal through parseFloat first (a JS double)
      // silently rounds it before BigInt() ever sees it, e.g. the
      // Mersenne prime 2305843009213693951 loses its last few digits to
      // float rounding and BigInt(that float) is a DIFFERENT, wrong
      // integer. This is exactly the class of bug exact mode exists to
      // prevent, so it must not be reintroduced at the tokenizer.
      tokens.push({ t: 'num', v: parseFloat(m[0]), raw: m[0] });
      i += m[0].length;
      continue;
    }
    if (/[a-zA-Z_]/.test(c)) {
      const m = src.slice(i).match(/^[a-zA-Z_][a-zA-Z_0-9]*/);
      tokens.push({ t: 'ident', v: m[0] });
      i += m[0].length;
      continue;
    }
    if ('+-*/%^(),;='.includes(c)) { tokens.push({ t: c }); i++; continue; }
    throw new Error(`Unexpected character "${c}"`);
  }
  return tokens;
}

/**
 * Compiles an expression string into an evaluator `(vars) => number`.
 * `vars` is a plain object like { x: 1.5 } or { n: 12 }. Unknown
 * identifiers that are neither whitelisted functions, constants, nor
 * provided variables throw at parse time; fail loud, not silently NaN.
 */
export function compileExpr(src, varNames = []) {
  const tokens = tokenize(String(src));
  let pos = 0;
  const peek = () => tokens[pos];
  const eat = (t) => {
    if (!tokens[pos] || tokens[pos].t !== t) throw new Error(`Expected "${t}"`);
    return tokens[pos++];
  };
  // Names resolvable as values: the declared input variables, PLUS any
  // let-bound intermediates as they are parsed. A binding is added only
  // after its own value expression is parsed, so `let a = a` cannot
  // reference itself and no recursion is expressible; the grammar stays
  // total and non-Turing-complete, which is the whole safety argument.
  const known = new Set(varNames);

  function parseExpr() {
    let left = parseTerm();
    while (peek() && (peek().t === '+' || peek().t === '-')) {
      const op = tokens[pos++].t;
      const right = parseTerm();
      const l = left;
      left = op === '+' ? (v) => l(v) + right(v) : (v) => l(v) - right(v);
    }
    return left;
  }
  function parseTerm() {
    let left = parseUnary();
    while (peek() && (peek().t === '*' || peek().t === '/' || peek().t === '%')) {
      const op = tokens[pos++].t;
      const right = parseUnary();
      const l = left;
      left = op === '*' ? (v) => l(v) * right(v)
        : op === '/' ? (v) => l(v) / right(v)
        : (v) => FUNCS.mod(l(v), right(v));
    }
    return left;
  }
  function parseUnary() {
    if (peek() && peek().t === '-') { pos++; const inner = parseUnary(); return (v) => -inner(v); }
    return parsePower();
  }
  function parsePower() {
    const base = parseAtom();
    if (peek() && peek().t === '^') {
      pos++;
      const exp = parseUnary(); // right-assoc: 2^3^2 = 2^(3^2)
      return (v) => Math.pow(base(v), exp(v));
    }
    return base;
  }
  function parseAtom() {
    const tok = peek();
    if (!tok) throw new Error('Unexpected end of expression');
    if (tok.t === 'num') { pos++; return () => tok.v; }
    if (tok.t === '(') { pos++; const inner = parseExpr(); eat(')'); return inner; }
    if (tok.t === 'ident') {
      pos++;
      const name = tok.v;
      if (peek() && peek().t === '(') {
        // Object.hasOwn, not `in` or bare lookup; `in` walks the prototype
        // chain, so "constructor"/"toString" would otherwise resolve to
        // Object.prototype members instead of throwing.
        if (!Object.hasOwn(FUNCS, name)) throw new Error(`Unknown function "${name}"`);
        const fn = FUNCS[name];
        pos++;
        const args = [parseExpr()];
        while (peek() && peek().t === ',') { pos++; args.push(parseExpr()); }
        eat(')');
        return (v) => fn(...args.map((a) => a(v)));
      }
      if (Object.hasOwn(CONSTS, name)) return () => CONSTS[name];
      if (known.has(name)) return (v) => v[name];
      throw new Error(`Unknown identifier "${name}"`);
    }
    throw new Error(`Unexpected token "${tok.t}"`);
  }

  // A program is zero or more `let name = expr;` bindings followed by one
  // final expression. Bindings let the model express a multi-step
  // recurrence (an optimizer update unrolled over several steps, say)
  // without inlining an enormous single expression; the failure mode this
  // fixes is real (see mcmcSearch/novelHypotheses history). Names must be
  // unique and may not shadow a variable, function, or constant, so every
  // identifier resolves to exactly one thing.
  const bindings = [];
  while (peek() && peek().t === 'ident' && peek().v === 'let') {
    pos++; // 'let'
    const nameTok = peek();
    if (!nameTok || nameTok.t !== 'ident') throw new Error('Expected a name after "let"');
    const name = nameTok.v;
    if (name === 'let') throw new Error('"let" is reserved and cannot be a binding name');
    if (name === '__proto__' || name === 'constructor' || name === 'prototype') throw new Error(`"${name}" is not a permitted binding name`);
    if (Object.hasOwn(FUNCS, name)) throw new Error(`Cannot bind "${name}"; it is a function name`);
    if (Object.hasOwn(CONSTS, name)) throw new Error(`Cannot bind "${name}"; it is a constant name`);
    if (known.has(name)) throw new Error(`"${name}" is already defined; a let name must be unique and cannot shadow a variable`);
    pos++; // name
    eat('=');
    const valFn = parseExpr(); // compiled against the CURRENT known set, so it cannot see itself or any later binding
    bindings.push({ name, fn: valFn });
    known.add(name);
    eat(';');
  }

  const evaluate = parseExpr();
  if (pos !== tokens.length) throw new Error('Trailing input after expression');
  if (bindings.length === 0) return evaluate;

  // Evaluate bindings in order into a null-prototype scope (so a stray
  // "__proto__"/"constructor" key can never reach Object.prototype), then
  // the final expression against that scope. A fresh scope per call keeps
  // evaluations independent and the compiled program pure.
  return (inputVars) => {
    const env = Object.assign(Object.create(null), inputVars);
    for (const b of bindings) env[b.name] = b.fn(env);
    return evaluate(env);
  };
}

// ── EXACT MODE; BigInt arithmetic for number theory & combinatorics ──
//
// The float compiler above is honest about what it is: continuous-
// function arithmetic in doubles. It CANNOT correctly check a claim like
// Erdős #728 (a!b! | n!(a+b-n)!) because factorial(400) already exceeds
// double precision; the float engine would silently return a wrong
// answer that LOOKS like a number. Rather than paper over that with
// float factorial, this is a genuinely separate compiler over BigInt:
// integer-only, arbitrary precision, exact equality and exact
// divisibility instead of floating tolerance. Same tokenizer (lexing is
// mode-agnostic), same whitelist discipline (Object.hasOwn, no eval, no
// prototype-chain lookups), different arithmetic semantics.
//
// Every function here is deterministic and every result is bounds-
// checked (MAX_DIGITS) so a pathological spec (factorial of something
// absurd) throws a clear error instead of hanging the tab or exhausting
// memory. numericCheck.js additionally wall-clock-bounds the sampling
// loop that calls these, so even a slow-but-bounded computation can't
// freeze the session indefinitely.

const MAX_DIGITS = 40000; // ~ headroom past factorial(10000)'s ~35660 digits
const MAX_FACTORIAL_N = 10000n;
const MAX_CHOOSE_N = 2_000_000n; // choose() is a k-step loop, not exponential; cheap even at this size
const MAX_EXPONENT = 100000n;

// Precomputed ONCE: a plain BigInt magnitude comparison against this
// constant is cheap (roughly linear in digit count). Converting the
// VALUE to a decimal string to measure its own length, as this function
// used to do, is the opposite of cheap; V8's BigInt-to-decimal-string
// conversion is a genuinely expensive algorithm (no way to extract
// decimal digits without real big-integer division), and calling it on
// every single multiplication step inside factorial's accumulation loop
// is what actually caused a multi-minute hang during testing: it wasn't
// the factorial arithmetic that was slow, it was re-stringifying a
// 30,000-digit number on every one of the ~10,000 steps that built it.
const DIGIT_LIMIT = 10n ** BigInt(MAX_DIGITS);

function assertBounded(v, label) {
  const mag = v < 0n ? -v : v;
  if (mag > DIGIT_LIMIT) {
    // Only pay the real stringification cost HERE, off the hot path, to
    // build a readable error message for the one call that actually failed.
    throw new Error(`${label || 'result'} exceeds ${MAX_DIGITS} digits; refusing to compute further (this is a safety bound, not a real answer)`);
  }
  return v;
}

function bigFactorial(n) {
  if (n < 0n) throw new Error('factorial of a negative number');
  if (n > MAX_FACTORIAL_N) throw new Error(`factorial(${n}) exceeds the ${MAX_FACTORIAL_N}-cap; pick a smaller n or restate the claim asymptotically`);
  let r = 1n;
  for (let i = 2n; i <= n; i++) { r *= i; assertBounded(r, 'factorial'); }
  return r;
}

function bigChoose(n, k) {
  if (k < 0n || k > n) return 0n;
  if (n > MAX_CHOOSE_N) throw new Error(`choose(${n},${k}); n exceeds the ${MAX_CHOOSE_N} cap`);
  const kk = k > n - k ? n - k : k; // symmetry: choose the smaller side, fewer loop iterations
  let result = 1n;
  for (let i = 0n; i < kk; i++) {
    // Always exactly divisible at this point (binomial recurrence); see
    // module comment: multiply-then-divide keeps every partial result an
    // integer, so BigInt truncating division never actually truncates.
    result = (result * (n - i)) / (i + 1n);
    assertBounded(result, 'choose');
  }
  return result;
}

function bigGcd(a, b) {
  a = a < 0n ? -a : a; b = b < 0n ? -b : b;
  while (b) { [a, b] = [b, a % b]; }
  return a;
}

function bigModPow(base, exp, mod) {
  if (exp < 0n) throw new Error('modpow: negative exponent not supported');
  if (mod === 0n) throw new Error('modpow: modulus 0');
  base = ((base % mod) + mod) % mod;
  let result = 1n;
  while (exp > 0n) {
    if (exp & 1n) result = (result * base) % mod;
    base = (base * base) % mod;
    exp >>= 1n;
  }
  return result;
}

// Deterministic Miller-Rabin; exact (not probabilistic) for every n this
// engine can realistically be asked about: valid for all n < 3.3 * 10^24
// with these witnesses (a standard, well-documented witness set).
function bigIsPrime(n) {
  if (n < 2n) return 0n;
  for (const p of [2n, 3n, 5n, 7n, 11n, 13n, 17n, 19n, 23n, 29n, 31n, 37n]) {
    if (n === p) return 1n;
    if (n % p === 0n) return 0n;
  }
  let d = n - 1n, r = 0n;
  while (d % 2n === 0n) { d /= 2n; r++; }
  witnessLoop:
  for (const a of [2n, 3n, 5n, 7n, 11n, 13n, 17n, 19n, 23n, 29n, 31n, 37n]) {
    if (a >= n) continue;
    let x = bigModPow(a, d, n);
    if (x === 1n || x === n - 1n) continue;
    for (let i = 0n; i < r - 1n; i++) {
      x = (x * x) % n;
      if (x === n - 1n) continue witnessLoop;
    }
    return 0n;
  }
  return 1n;
}

// p-adic valuation: the largest k with p^k | n. Directly what Kummer's
// theorem is stated in terms of, and what Erdős #728's proof turns on.
function bigValuation(n, p) {
  if (p < 2n) throw new Error('valuation: p must be >= 2');
  n = n < 0n ? -n : n;
  if (n === 0n) throw new Error('valuation: n must be nonzero');
  let k = 0n;
  while (n % p === 0n) { n /= p; k++; }
  return k;
}

// Kummer's theorem, computed directly rather than cited: the number of
// carries when adding m + m in base p equals the p-adic valuation of
// choose(2m, m). This IS the mechanism behind Erdős #728's proof; now a
// primitive any claim in this family can call natively.
function bigCarries(m, p) {
  if (p < 2n) throw new Error('carries: p must be >= 2');
  if (m < 0n) throw new Error('carries: m must be nonnegative');
  let a = m, carry = 0n, count = 0n;
  while (a > 0n || carry > 0n) {
    const digit = a % p;
    const sum = digit + digit + carry;
    if (sum >= p) { carry = 1n; count++; } else { carry = 0n; }
    a /= p;
  }
  return count;
}

function bigDigitSum(n, base) {
  if (base < 2n) throw new Error('digitsum: base must be >= 2');
  n = n < 0n ? -n : n;
  let s = 0n;
  while (n > 0n) { s += n % base; n /= base; }
  return s;
}

const EXACT_FUNCS = {
  factorial: bigFactorial,
  choose: bigChoose, binom: bigChoose,
  gcd: bigGcd,
  lcm: (a, b) => { const g = bigGcd(a, b); return g === 0n ? 0n : assertBounded((a < 0n ? -a : a) / g * (b < 0n ? -b : b), 'lcm'); },
  isprime: bigIsPrime,
  modpow: bigModPow,
  valuation: bigValuation,
  carries: bigCarries,
  digitsum: bigDigitSum,
  abs: (a) => (a < 0n ? -a : a),
  min: (a, b) => (a < b ? a : b),
  max: (a, b) => (a > b ? a : b),
  sign: (a) => (a > 0n ? 1n : a < 0n ? -1n : 0n),
  mod: (a, b) => ((a % b) + b) % b,
};

/**
 * Compiles an expression into a BigInt evaluator `(vars) => bigint`.
 * `vars` values are coerced to BigInt; throws if a variable is passed a
 * non-integer, since "exact mode" only means something if every input is
 * exact too. No constants (pi/e/tau are irrational; meaningless here).
 */
export function compileExprExact(src, varNames = []) {
  const tokens = tokenize(String(src));
  // let-bindings are float-mode only for now: an exact binding would have
  // to carry the same integer-only guarantee as every other exact value,
  // and that is not yet wired. Fail loud rather than silently mis-parse a
  // ';' or '=' as something else.
  if (tokens.some((t) => t.t === ';' || t.t === '=')) {
    throw new Error('let-bindings are not supported in exact mode; expand intermediates by hand');
  }
  let pos = 0;
  const peek = () => tokens[pos];
  const eat = (t) => {
    if (!tokens[pos] || tokens[pos].t !== t) throw new Error(`Expected "${t}"`);
    return tokens[pos++];
  };

  function toExactVars(v) {
    const out = {};
    for (const name of varNames) {
      const raw = v[name];
      if (!Number.isInteger(raw)) throw new Error(`Exact mode requires an integer value for "${name}", got ${raw}`);
      out[name] = BigInt(raw);
    }
    return out;
  }

  function parseExpr() {
    let left = parseTerm();
    while (peek() && (peek().t === '+' || peek().t === '-')) {
      const op = tokens[pos++].t;
      const right = parseTerm();
      const l = left;
      left = op === '+' ? (v) => assertBounded(l(v) + right(v)) : (v) => assertBounded(l(v) - right(v));
    }
    return left;
  }
  function parseTerm() {
    let left = parseUnary();
    while (peek() && (peek().t === '*' || peek().t === '/' || peek().t === '%')) {
      const op = tokens[pos++].t;
      const right = parseUnary();
      const l = left;
      left = op === '*' ? (v) => assertBounded(l(v) * right(v))
        : op === '/' ? (v) => { const d = right(v); if (d === 0n) throw new Error('division by zero'); return l(v) / d; } // truncating BigInt division
        : (v) => EXACT_FUNCS.mod(l(v), right(v));
    }
    return left;
  }
  function parseUnary() {
    if (peek() && peek().t === '-') { pos++; const inner = parseUnary(); return (v) => -inner(v); }
    return parsePower();
  }
  function parsePower() {
    const base = parseAtom();
    if (peek() && peek().t === '^') {
      pos++;
      const exp = parseUnary();
      return (v) => {
        const e = exp(v);
        if (e < 0n) throw new Error('exact mode: negative exponent not supported');
        if (e > MAX_EXPONENT) throw new Error(`exponent exceeds ${MAX_EXPONENT} cap`);
        return assertBounded(base(v) ** e);
      };
    }
    return base;
  }
  function parseAtom() {
    const tok = peek();
    if (!tok) throw new Error('Unexpected end of expression');
    if (tok.t === 'num') {
      pos++;
      // Built from tok.raw (the exact source text), never tok.v (a float
      // that may already have rounded a large integer literal); see the
      // tokenizer comment for why this distinction is load-bearing.
      if (!/^\d+$/.test(tok.raw)) throw new Error(`Exact mode only accepts integer literals, got ${tok.raw}`);
      return () => BigInt(tok.raw);
    }
    if (tok.t === '(') { pos++; const inner = parseExpr(); eat(')'); return inner; }
    if (tok.t === 'ident') {
      pos++;
      const name = tok.v;
      if (peek() && peek().t === '(') {
        if (!Object.hasOwn(EXACT_FUNCS, name)) throw new Error(`Unknown exact-mode function "${name}"`);
        const fn = EXACT_FUNCS[name];
        pos++;
        const args = [parseExpr()];
        while (peek() && peek().t === ',') { pos++; args.push(parseExpr()); }
        eat(')');
        return (v) => assertBounded(fn(...args.map((a) => a(v))));
      }
      if (varNames.includes(name)) return (v) => v[name];
      throw new Error(`Unknown identifier "${name}"`);
    }
    throw new Error(`Unexpected token "${tok.t}"`);
  }

  const evaluate = parseExpr();
  if (pos !== tokens.length) throw new Error('Trailing input after expression');
  return (v) => evaluate(toExactVars(v));
}

export const EXACT_FUNCTION_NAMES = Object.keys(EXACT_FUNCS);
