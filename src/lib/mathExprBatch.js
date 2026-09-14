// mathExprBatch.js; upgrade 10 — a batched-evaluation compiler for the
// SAME grammar mathExpr.js's compileExpr() parses, aimed at the same
// principle jax.lax.vmap uses to push per-call latency down: avoid
// re-paying fixed per-call dispatch overhead by compiling the
// computation into a form the runtime can execute in a tight,
// predictable loop instead of one Python-interpreter-loop-style step
// (or here, one nested-JS-closure-call step) per point.
//
// WHY NOT new Function()/eval — a real, deliberate boundary, not an
// oversight. mathExpr.js's own file header states its whole design
// point: "compiled by mathExpr.js's whitelisted grammar (no eval, no
// network, CSP-safe)" (see mcmcSearch.js's file header, which quotes
// this). An expression string here is MODEL-DESIGNED, i.e. untrusted/
// attacker-shaped input by this repo's own stated threat model.
// Generating and executing JS source via `new Function(...)` from that
// input — even carefully, even only ever splicing vetted tokens from
// an already-validated parse — would be a real, security-relevant
// departure from an existing, intentional design decision, not a
// silent implementation detail. So this compiles to neither closures
// (compileExpr's approach) NOR generated JS source: it compiles to a
// flat array of {op, ...} instruction objects, executed by a small
// stack-machine interpreter loop (evalProgram() below). No string of
// user-influenced text is ever handed to eval/Function/vm — every
// value that can appear in the emitted bytecode is either a number
// literal already validated by the tokenizer, a validated slot index,
// or a direct reference to a whitelisted function from mathExpr.js's
// own FUNCS table. This is a genuinely different, safe mechanism, not
// a workaround for the same risk.
//
// GRAMMAR: identical to compileExpr's, same precedence, same
// FUNCS/CONSTS whitelist, same let-binding support (bindings compile
// to extra scope slots, resolved the same left-to-right, no-self-
// reference way compileExpr's `known` set already enforces). The
// tokenizer is REUSED directly from mathExpr.js (pure, stateless, zero
// duplication risk); only the parse-time ACTION differs: instead of
// building a nested closure, each grammar rule appends bytecode
// instructions to a flat array.
//
// HONEST RESULT (measured, not assumed): compare
// tests/mathExprBatch.test.mjs's benchmark output. The real speedup
// over compileExpr's closure-tree evaluation is genuine but modest for
// simple expressions and grows for more complex ones (more nodes means
// more saved closure-call overhead per evaluation) — reported, not
// gated on a specific ratio, matching this repo's own benchmark
// discipline (consistencyBenchmark.test.mjs, mlpWasmSimd.test.mjs).

import { FUNCS, CONSTS, tokenize } from './mathExpr.js';

function compileToBytecode(tokens, varNames) {
  let pos = 0;
  const peek = () => tokens[pos];
  const eat = (t) => {
    if (!tokens[pos] || tokens[pos].t !== t) throw new Error(`Expected "${t}"`);
    return tokens[pos++];
  };

  const scopeNames = [...varNames]; // input vars first, then let-bindings appended in declared order
  const nameIndex = (name) => scopeNames.indexOf(name);
  const known = new Set(varNames);

  function parseExpr(code) {
    parseTerm(code);
    while (peek() && (peek().t === '+' || peek().t === '-')) {
      const op = tokens[pos++].t;
      parseTerm(code);
      code.push({ op: op === '+' ? 'add' : 'sub' });
    }
  }
  function parseTerm(code) {
    parseUnary(code);
    while (peek() && (peek().t === '*' || peek().t === '/' || peek().t === '%')) {
      const op = tokens[pos++].t;
      parseUnary(code);
      code.push({ op: op === '*' ? 'mul' : op === '/' ? 'div' : 'call', fn: op === '%' ? FUNCS.mod : undefined, argc: op === '%' ? 2 : undefined });
    }
  }
  function parseUnary(code) {
    if (peek() && peek().t === '-') { pos++; parseUnary(code); code.push({ op: 'neg' }); return; }
    parsePower(code);
  }
  function parsePower(code) {
    parseAtom(code);
    if (peek() && peek().t === '^') {
      pos++;
      parseUnary(code); // right-assoc, matches compileExpr exactly
      code.push({ op: 'pow' });
    }
  }
  function parseAtom(code) {
    const tok = peek();
    if (!tok) throw new Error('Unexpected end of expression');
    if (tok.t === 'num') { pos++; code.push({ op: 'const', value: tok.v }); return; }
    if (tok.t === '(') { pos++; parseExpr(code); eat(')'); return; }
    if (tok.t === 'ident') {
      pos++;
      const name = tok.v;
      if (peek() && peek().t === '(') {
        if (!Object.hasOwn(FUNCS, name)) throw new Error(`Unknown function "${name}"`);
        const fn = FUNCS[name];
        pos++;
        let argc = 0;
        parseExpr(code); argc++;
        while (peek() && peek().t === ',') { pos++; parseExpr(code); argc++; }
        eat(')');
        code.push({ op: 'call', fn, argc });
        return;
      }
      if (Object.hasOwn(CONSTS, name)) { code.push({ op: 'const', value: CONSTS[name] }); return; }
      if (known.has(name)) { code.push({ op: 'var', index: nameIndex(name) }); return; }
      throw new Error(`Unknown identifier "${name}"`);
    }
    throw new Error(`Unexpected token "${tok.t}"`);
  }

  // Bindings: same rules as compileExpr (unique name, no shadowing a
  // var/func/const, no self/forward reference). Each binding compiles
  // to its own bytecode program, executed into the next scope slot
  // before the main program runs.
  const bindingPrograms = []; // [{ code: instr[] }] in declaration order, index i writes scope slot varNames.length+i
  while (peek() && peek().t === 'ident' && peek().v === 'let') {
    pos++;
    const nameTok = peek();
    if (!nameTok || nameTok.t !== 'ident') throw new Error('Expected a name after "let"');
    const name = nameTok.v;
    if (name === 'let') throw new Error('"let" is reserved and cannot be a binding name');
    if (name === '__proto__' || name === 'constructor' || name === 'prototype') throw new Error(`"${name}" is not a permitted binding name`);
    if (Object.hasOwn(FUNCS, name)) throw new Error(`Cannot bind "${name}"; it is a function name`);
    if (Object.hasOwn(CONSTS, name)) throw new Error(`Cannot bind "${name}"; it is a constant name`);
    if (known.has(name)) throw new Error(`"${name}" is already defined; a let name must be unique and cannot shadow a variable`);
    pos++;
    eat('=');
    const bindingCode = [];
    parseExpr(bindingCode); // compiled against the CURRENT scope only -- cannot see itself or later bindings, same as compileExpr
    eat(';');
    bindingPrograms.push({ name, code: bindingCode });
    scopeNames.push(name);
    known.add(name);
  }

  const mainCode = [];
  parseExpr(mainCode);
  if (pos !== tokens.length) throw new Error('Trailing input after expression');

  return { scopeNames, bindingPrograms, mainCode };
}

// Small stack-machine interpreter -- no eval, no Function, no vm
// module; every instruction is a plain data object built entirely from
// vetted tokens/whitelisted function refs during compileToBytecode
// above.
function runProgram(code, scope) {
  const stack = [];
  let sp = 0;
  for (let ip = 0; ip < code.length; ip++) {
    const instr = code[ip];
    switch (instr.op) {
      case 'const': stack[sp++] = instr.value; break;
      case 'var': stack[sp++] = scope[instr.index]; break;
      case 'add': stack[sp - 2] = stack[sp - 2] + stack[sp - 1]; sp--; break;
      case 'sub': stack[sp - 2] = stack[sp - 2] - stack[sp - 1]; sp--; break;
      case 'mul': stack[sp - 2] = stack[sp - 2] * stack[sp - 1]; sp--; break;
      case 'div': stack[sp - 2] = stack[sp - 2] / stack[sp - 1]; sp--; break;
      case 'neg': stack[sp - 1] = -stack[sp - 1]; break;
      case 'pow': stack[sp - 2] = Math.pow(stack[sp - 2], stack[sp - 1]); sp--; break;
      case 'call': {
        const args = stack.slice(sp - instr.argc, sp);
        sp -= instr.argc;
        stack[sp++] = instr.fn(...args);
        break;
      }
      default: throw new Error(`mathExprBatch: unknown bytecode op "${instr.op}"`);
    }
  }
  return stack[0];
}

/**
 * Compiles `src` (identical grammar to compileExpr, including
 * let-bindings) against `varNames` into a batched evaluator. Returns:
 *  - scopeSize: total scope slots (input vars + bindings) -- informational.
 *  - evalOne(values): values is an array in the SAME order as varNames; returns a number.
 *  - evalBatch(rows): rows is an array of such arrays; returns an array of numbers,
 *    reusing one scope buffer across the whole batch (the actual "batching" —
 *    no per-point allocation of a fresh scope array or binding object).
 */
export function compileExprBatch(src, varNames = []) {
  const tokens = tokenize(String(src));
  const { scopeNames, bindingPrograms, mainCode } = compileToBytecode(tokens, varNames);
  const scopeSize = scopeNames.length;
  const inputCount = varNames.length;

  function evalOne(values) {
    const scope = new Array(scopeSize);
    for (let i = 0; i < inputCount; i++) scope[i] = values[i];
    for (let i = 0; i < bindingPrograms.length; i++) scope[inputCount + i] = runProgram(bindingPrograms[i].code, scope);
    return runProgram(mainCode, scope);
  }

  function evalBatch(rows) {
    const out = new Array(rows.length);
    const scope = new Array(scopeSize); // ONE buffer reused across the whole batch -- the actual allocation saving
    for (let r = 0; r < rows.length; r++) {
      const values = rows[r];
      for (let i = 0; i < inputCount; i++) scope[i] = values[i];
      for (let i = 0; i < bindingPrograms.length; i++) scope[inputCount + i] = runProgram(bindingPrograms[i].code, scope);
      out[r] = runProgram(mainCode, scope);
    }
    return out;
  }

  return { scopeSize, scopeNames: [...scopeNames], evalOne, evalBatch };
}
