// dimensionalAnalysis.js; widening the kernel without weakening it.
//
// The standing constraint on this codebase: every widening that gives up
// the ability to PROVE turns the tool back into a generator, and there are
// already thousands of those. Dimensional analysis is the rare extension
// that widens coverage enormously while giving up nothing, because
// dimensional consistency is fully decidable. Two expressions either have
// the same dimensions or they do not, and the answer is computed, not
// judged. No oracle, no corpus, no model in the deciding step.
//
// It is also the single most reliable error detector in physics and the
// one every practitioner runs by hand. A language model will happily emit
// a dimensionally impossible equation with total fluency, because nothing
// in next-token prediction is checking exponents of mass and length. This
// module checks them.
//
// WHAT IT CATCHES, all as proofs rather than suspicions:
//   1. An equation whose sides have different dimensions.
//   2. Addition or subtraction of unlike quantities, which is where the
//      error usually actually is, buried inside one side of an equation
//      that a casual reader scans past.
//   3. A dimensioned argument to a transcendental function. exp(E) and
//      log(t) are meaningless: the series expansion would add a length to
//      a length squared. This is a very common real mistake and almost
//      nothing catches it automatically.
//
// WHAT IT DOES NOT CLAIM. Dimensional correctness is necessary, never
// sufficient. E = 2mc^2 is dimensionally perfect and false. So a clean
// result here is reported as the absence of one specific class of error
// and nothing more, in keeping with the asymmetry the whole codebase
// enforces: a found violation is decisive, a passed check is weak.
//
// An unknown symbol is an ERROR, never an assumption. Guessing that an
// unrecognised variable is dimensionless would silently manufacture false
// passes, which is worse than refusing to answer.

// SI base dimensions. Exponents are numbers and may be fractional, which
// is needed the moment a square root appears.
export const BASE = ['M', 'L', 'T', 'I', 'K', 'N', 'J'];
const ZERO = Object.freeze({ M: 0, L: 0, T: 0, I: 0, K: 0, N: 0, J: 0 });

export function makeDim(spec = {}) {
  const d = { ...ZERO };
  for (const k of BASE) if (Number.isFinite(spec[k])) d[k] = spec[k];
  return d;
}
const mul = (a, b) => makeDim(Object.fromEntries(BASE.map((k) => [k, a[k] + b[k]])));
const div = (a, b) => makeDim(Object.fromEntries(BASE.map((k) => [k, a[k] - b[k]])));
const powd = (a, n) => makeDim(Object.fromEntries(BASE.map((k) => [k, a[k] * n])));
export const isDimensionless = (a) => BASE.every((k) => Math.abs(a[k]) < 1e-9);
export const dimEqual = (a, b) => BASE.every((k) => Math.abs(a[k] - b[k]) < 1e-9);

// Human-readable, in the form a physicist would actually write.
export function formatDim(d) {
  if (isDimensionless(d)) return 'dimensionless';
  const parts = BASE.filter((k) => Math.abs(d[k]) > 1e-9)
    .map((k) => (Math.abs(d[k] - 1) < 1e-9 ? k : `${k}^${round(d[k])}`));
  return parts.join(' ');
}
const round = (n) => (Math.abs(n - Math.round(n)) < 1e-9 ? Math.round(n) : Math.round(n * 100) / 100);

// A working library so a user can write real physics without declaring
// everything. Deliberately covers the constants that appear in gravity
// work, since a dimensionally wrong expression in G, c and hbar is the
// classic way a plausible-looking result is silently nonsense.
export const KNOWN_DIMENSIONS = {
  // kinematics and mechanics
  length: { L: 1 }, distance: { L: 1 }, radius: { L: 1 }, time: { T: 1 }, mass: { M: 1 },
  area: { L: 2 }, volume: { L: 3 }, velocity: { L: 1, T: -1 }, speed: { L: 1, T: -1 },
  acceleration: { L: 1, T: -2 }, momentum: { M: 1, L: 1, T: -1 },
  force: { M: 1, L: 1, T: -2 }, energy: { M: 1, L: 2, T: -2 }, work: { M: 1, L: 2, T: -2 },
  power: { M: 1, L: 2, T: -3 }, pressure: { M: 1, L: -1, T: -2 }, density: { M: 1, L: -3 },
  frequency: { T: -1 }, angular_momentum: { M: 1, L: 2, T: -1 }, action: { M: 1, L: 2, T: -1 },
  torque: { M: 1, L: 2, T: -2 }, spin: { M: 1, L: 2, T: -1 },
  // constants
  c: { L: 1, T: -1 }, G: { M: -1, L: 3, T: -2 }, hbar: { M: 1, L: 2, T: -1 }, h: { M: 1, L: 2, T: -1 },
  k_B: { M: 1, L: 2, T: -2, K: -1 }, temperature: { K: 1 },
  // gravity / cosmology
  curvature: { L: -2 }, ricci_scalar: { L: -2 }, torsion: { L: -1 },
  hubble_parameter: { T: -1 }, cosmological_constant: { L: -2 },
  energy_density: { M: 1, L: -1, T: -2 }, spin_density: { M: 1, L: -1, T: -1 },
};

// ── Parser ───────────────────────────────────────────────────────────
// Small recursive descent over the arithmetic a physical formula uses.
// Written here rather than reusing mathExpr.js because that evaluates to
// NUMBERS; this evaluates to dimension vectors, and the interesting rules
// (addition demands equality, transcendentals demand dimensionlessness)
// have no numeric counterpart.

const TRANSCENDENTAL = new Set(['sin', 'cos', 'tan', 'exp', 'log', 'ln', 'sinh', 'cosh', 'tanh', 'asin', 'acos', 'atan']);

function tokenize(src) {
  const tokens = [];
  const re = /\s*([A-Za-z_][A-Za-z0-9_]*|\d+\.?\d*|\*\*|[+\-*/^()])/g;
  let m, last = 0;
  while ((m = re.exec(src)) !== null) {
    if (m.index !== last) throw new Error(`Unexpected character at position ${last} in "${src}"`);
    tokens.push(m[1] === '**' ? '^' : m[1]);
    last = re.lastIndex;
  }
  if (last !== src.length && src.slice(last).trim()) throw new Error(`Unexpected trailing input in "${src}"`);
  return tokens;
}

function parseDimExpr(src, env) {
  const tokens = tokenize(src);
  let pos = 0;
  const peek = () => tokens[pos];
  const eat = (t) => { if (tokens[pos] !== t) throw new Error(`Expected "${t}" in "${src}"`); pos++; };
  // Collected as we go so one pass reports every violation rather than
  // only the first; a formula with two errors should show both.
  const violations = [];

  function expr() {
    let left = term();
    while (peek() === '+' || peek() === '-') {
      const op = peek(); pos++;
      const right = term();
      // THE central rule. You cannot add a length to a time. This is where
      // the real error usually lives.
      if (!dimEqual(left.dim, right.dim)) {
        violations.push({
          kind: 'unlike-addition',
          detail: `"${left.text} ${op} ${right.text}" adds ${formatDim(left.dim)} to ${formatDim(right.dim)}`,
        });
      }
      left = { dim: left.dim, text: `${left.text} ${op} ${right.text}` };
    }
    return left;
  }

  function term() {
    let left = factor();
    while (peek() === '*' || peek() === '/') {
      const op = peek(); pos++;
      const right = factor();
      left = { dim: op === '*' ? mul(left.dim, right.dim) : div(left.dim, right.dim), text: `${left.text} ${op} ${right.text}` };
    }
    return left;
  }

  function factor() {
    const base = unary();
    if (peek() === '^') {
      pos++;
      let sign = 1;
      if (peek() === '-') { sign = -1; pos++; }
      const n = Number(peek());
      // A dimensioned exponent is meaningless and so is a symbolic one for
      // this purpose; refuse rather than guess.
      if (!Number.isFinite(n)) throw new Error(`Exponent must be a plain number in "${src}"`);
      pos++;
      return { dim: powd(base.dim, sign * n), text: `${base.text}^${sign * n}` };
    }
    return base;
  }

  function unary() {
    if (peek() === '-') { pos++; return unary(); } // negation cannot change dimension
    return primary();
  }

  function primary() {
    const t = peek();
    if (t === undefined) throw new Error(`Unexpected end of expression in "${src}"`);
    if (t === '(') { pos++; const e = expr(); eat(')'); return { dim: e.dim, text: `(${e.text})` }; }
    if (/^\d/.test(t)) { pos++; return { dim: makeDim(), text: t }; } // pure numbers are dimensionless
    if (/^[A-Za-z_]/.test(t)) {
      pos++;
      if (peek() === '(') {
        pos++; const arg = expr(); eat(')');
        if (t === 'sqrt') return { dim: powd(arg.dim, 0.5), text: `sqrt(${arg.text})` };
        if (t === 'abs') return { dim: arg.dim, text: `abs(${arg.text})` };
        if (TRANSCENDENTAL.has(t)) {
          // exp of an energy is not a large number, it is meaningless: the
          // series would add an energy to an energy squared.
          if (!isDimensionless(arg.dim)) {
            violations.push({
              kind: 'dimensioned-transcendental',
              detail: `${t}(${arg.text}) takes ${formatDim(arg.dim)}; the argument of ${t} must be dimensionless`,
            });
          }
          return { dim: makeDim(), text: `${t}(${arg.text})` };
        }
        throw new Error(`Unknown function "${t}" in "${src}"`);
      }
      const d = env[t];
      // Never assume. An unrecognised symbol silently treated as
      // dimensionless would manufacture false passes.
      if (!d) throw new Error(`Unknown symbol "${t}". Declare its dimensions or use a known quantity.`);
      return { dim: makeDim(d), text: t };
    }
    throw new Error(`Unexpected token "${t}" in "${src}"`);
  }

  const result = expr();
  if (pos !== tokens.length) throw new Error(`Unexpected trailing tokens in "${src}"`);
  return { dim: result.dim, violations };
}

// ── Public API ───────────────────────────────────────────────────────

// `assignments` maps symbol to a dimension spec, e.g. { E: {M:1,L:2,T:-2} }
// or to the name of a known quantity, e.g. { v: 'velocity' }.
export function buildEnv(assignments = {}) {
  const env = { ...KNOWN_DIMENSIONS };
  for (const [sym, spec] of Object.entries(assignments)) {
    if (typeof spec === 'string') {
      const known = KNOWN_DIMENSIONS[spec];
      if (!known) throw new Error(`Unknown quantity name "${spec}" for symbol "${sym}"`);
      env[sym] = known;
    } else if (spec && typeof spec === 'object') {
      env[sym] = spec;
    }
  }
  return env;
}

// The instrument. Returns a proof-carrying result, or an honest failure
// when the equation could not be analysed at all, which is different from
// and must never be reported as a pass.
export function analyzeEquation({ lhs, rhs, assignments = {}, source = '' }) {
  let env;
  try { env = buildEnv(assignments); }
  catch (e) { return { status: 'unanalyzable', reason: e.message, source }; }

  let L, R;
  try { L = parseDimExpr(String(lhs), env); }
  catch (e) { return { status: 'unanalyzable', reason: e.message, side: 'left', source }; }
  try { R = parseDimExpr(String(rhs), env); }
  catch (e) { return { status: 'unanalyzable', reason: e.message, side: 'right', source }; }

  const violations = [...L.violations, ...R.violations];
  const sidesMatch = dimEqual(L.dim, R.dim);
  if (!sidesMatch) {
    violations.unshift({
      kind: 'side-mismatch',
      detail: `left side is ${formatDim(L.dim)}, right side is ${formatDim(R.dim)}`,
    });
  }

  return {
    status: violations.length ? 'violation' : 'consistent',
    source,
    lhsDim: L.dim, rhsDim: R.dim,
    lhsText: String(lhs), rhsText: String(rhs),
    violations,
    // Stated on every clean result so it is never read as endorsement.
    caveat: violations.length ? '' : 'Dimensional consistency is necessary but never sufficient. E = 2mc^2 is dimensionally perfect and false. This rules out one class of error and says nothing about whether the equation is right.',
  };
}

export function summarizeDimensional(results) {
  const list = results || [];
  const violated = list.filter((r) => r.status === 'violation');
  const unanalyzable = list.filter((r) => r.status === 'unanalyzable');
  if (violated.length > 0) {
    return {
      verdict: 'dimensionally-impossible',
      headline: violated.length === 1
        ? 'One equation here cannot be right: its dimensions do not balance.'
        : `${violated.length} equations here cannot be right: their dimensions do not balance.`,
      detail: 'This is computed from the exponents of mass, length and time, not argued. A dimensionally unbalanced equation is false regardless of how plausible the surrounding reasoning is.',
    };
  }
  if (list.length === 0 || list.length === unanalyzable.length) {
    return { verdict: 'not-checked', headline: '', detail: '' };
  }
  return {
    verdict: 'dimensionally-consistent',
    headline: 'The equations here balance dimensionally.',
    detail: 'That rules out one specific class of error and nothing more. Dimensional consistency is necessary, never sufficient, so this is weak evidence and should not be read as support.',
  };
}
