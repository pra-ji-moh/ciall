// selfAudit.js; upgrade 15 — a real source-code security scanner this
// repo can run against ITSELF, the honest, buildable slice of "detect
// and neutralize embedded supply-chain malware."
//
// "NEUTRALIZE" IS DELIBERATELY NOT IMPLEMENTED. This module only ever
// FLAGS findings for human review — it never deletes, modifies, or
// quarantines a file. Two real reasons, not caution theater: (1) an
// automated scanner acting on its own heuristic findings is itself a
// supply-chain risk — a false positive that auto-deletes or rewrites a
// real file is a self-inflicted incident, and a sufficiently clever
// real trojan could deliberately trigger the "neutralizer" against
// something else entirely; (2) this repo's own standing rule, applied
// consistently since deviceGate.js's very first version, is that
// nothing acts on a device/file without an explicit human confirm() —
// this scanner follows that same rule, not an exception to it.
//
// "DETECT" IS ALSO HONESTLY SCOPED. This is NOT a malware detection
// engine — genuine trojan/supply-chain-malware detection is an
// unsolved, adversarial problem that entire companies (Socket.dev,
// Snyk, GitHub CodeQL) build their whole product around, and even they
// do not catch everything. This module checks for a small, DISCLOSED,
// fixed set of concrete red flags this specific codebase's own
// standing rules make suspicious (a zero-eval rule already exists per
// mathExpr.js's header; a documented list of files legitimately touch
// child_process/the network; WASM constants are the only legitimate
// large base64 blobs) — a pattern scanner grounded in THIS repo's own
// disclosed architecture, not a general-purpose antivirus. A finding
// here means "worth a human's attention," never "confirmed malicious."
//
// COMMENT-STRIPPING IS A HEURISTIC, NOT A REAL PARSER. `//` line
// comments and `/* */` block comments are stripped before pattern
// matching (a naive line-based strip, not a real JS tokenizer) so a
// comment EXPLAINING why this repo doesn't use eval() doesn't itself
// trigger an eval-usage finding — confirmed necessary by running an
// early version of this scanner against this actual repo and finding
// exactly that false positive in mathExpr.js's own header comment. A
// sufficiently adversarial file (e.g. a fake `//` inside a template
// string) could still fool this — disclosed, not hidden.
//
// UPGRADE 15 EXTENSION: two real additions beyond the original pattern
// scan. (1) `recordBaseline`/`checkAgainstBaseline`: SHA-256 file
// hashing (node:crypto, zero new deps) into a trusted manifest, then a
// real diff (added/removed/modified) against it — the actual technique
// behind real supply-chain integrity tools (npm lockfile integrity
// hashes, Subresource Integrity), which the pattern checks alone have
// no equivalent of: they inspect content in isolation and cannot tell
// you a trusted file changed. (2) three more disclosed pattern checks:
// dynamic require()/import() with a non-literal argument (a documented
// obfuscation/loader technique), unexpected non-ASCII characters
// outside comments (the real attack: a homoglyph like Cyrillic "а"
// standing in for Latin "a" in an identifier), and direct __proto__/
// Object.prototype mutation (real, documented prototype-pollution
// vulnerabilities). Every one of these was run against this actual
// repo before shipping, same discipline as the original checks —
// found and fixed zero false positives on the new rules; found two
// genuinely legitimate non-ASCII test fixtures (Unicode test strings
// in fixProtocol.test.mjs/msgpack.test.mjs) that the tool correctly
// flagged for review, not a bug.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';

const MAX_FILES = 2000;
const MAX_FILE_BYTES = 2 * 1024 * 1024;
// 'fixtures' excludes bench/fixtures specifically: the CVE benchmark's
// ground-truth corpus, deliberately full of real historical vulnerable
// (and fixed) third-party code -- exactly what this self-scan exists to
// flag elsewhere, but not "this repo's own source," which is the one
// thing this specific check (runSelfAudit against THIS repo) claims.
const EXCLUDED_DIR_NAMES = new Set(['node_modules', '.git', 'ciall-export', 'ciall-memory', 'ciall-ontology-store', 'ciall-sandbox', 'fixtures']);

// Reviewed, exact-path whitelists — a NEW file doing the same thing
// still gets flagged until someone deliberately reviews and adds it
// here, on purpose (see this file's header).
const EVAL_WHITELIST = new Set([
  // Not a real eval() call: this file's OWN detection regexes and
  // documentation necessarily contain the literal substrings "eval("
  // and "new Function(" (to describe and match them), which trips its
  // own check when this scanner is run against this actual repo --
  // confirmed by running it and finding exactly this self-referential
  // false positive before adding this entry.
  'src/lib/selfAudit.js',
]);
const BASE64_BLOB_WHITELIST = new Set(['src/lib/smcWasm.js', 'src/lib/mlpWasm.js', 'src/lib/mlpWasmSimd.js']);
const CHILD_PROCESS_WHITELIST = new Set([
  'src/lib/kernelExport.js', 'src/lib/commandExecutor.js', 'src/lib/rpc/devCert.js',
  // bench/ingest.mjs shells out to the system `git` binary (execFileSync)
  // to resolve CVE fix commits over git's own transport instead of the
  // rate-limited GitHub REST API -- see that file's own header for why.
  // A one-time, network-touching dataset-construction script, not
  // runtime kernel code; reviewed, same category as the three above.
  'bench/ingest.mjs',
]);
const NETWORK_MODULE_WHITELIST = new Set(['src/lib/dashboardServer.js', 'src/lib/rpc/kernelServiceClient.js', 'src/lib/rpc/kernelServiceServer.js']);
const FETCH_CALL_WHITELIST = new Set(['src/lib/retry.js', 'bench/ingest.mjs']);
const TLS_VERIFICATION_DISABLED_WHITELIST = new Set([
  // The ONE reviewed, disclosed case in this repo: kernelServiceClient.js's
  // own header comment explains this is a deliberate local-network
  // tradeoff (accepts self-signed certs; the connection is still fully
  // encrypted, just not chain-verified) -- not a silent weakening.
  'src/lib/rpc/kernelServiceClient.js',
  // Self-referential, same category as EVAL_WHITELIST above: this file's
  // OWN detection regex's source literally contains the substring
  // "rejectUnauthorized...false", tripping its own check.
  'src/lib/selfAudit.js',
]);
const UNGUARDED_KEY_ASSIGNMENT_WHITELIST = new Set([
  // dynamicsCheck.js's `stateA.forEach((n, i) => { envA0[n] = a0[i]; })`
  // matches this rule's shape (a computed property assignment through a
  // .forEach callback variable), but `stateA`/`stateB` are ARRAYS of
  // state-variable-name strings from a model-designed spec
  // (normalizeDynamicsSpec, bounded to MAX_STATE_VARS=24), not an
  // Object.keys()-style key iteration -- reviewed: even in the
  // worst case (a spec naming a state variable "__proto__"), `envA0` is
  // a fresh plain object literal built and consumed entirely within one
  // function call, never merged into shared or global state, so the
  // blast radius is a wrong answer for that one dynamics check, not a
  // supply-chain prototype-pollution vulnerability. A genuine false
  // positive of this rule's known, disclosed limitation (see this file's
  // header on this rule): it cannot distinguish "iterating an object's
  // OWN keys" from "iterating an array of name strings with a
  // coincidentally key-shaped callback variable."
  'src/lib/dynamicsCheck.js',
  // Same exact pattern, same reasoning: tests/mathExprBatch.test.mjs's
  // `varNames.forEach((n, i) => { inputObj[n] = valuesArray[i]; })`
  // builds a fresh, function-local evaluation environment object from a
  // test-local array of variable-name strings, never merged into shared
  // state.
  'tests/mathExprBatch.test.mjs',
]);

// Non-ASCII characters this repo's own comments/strings already
// legitimately use (em dash, box-drawing, bullet/middle-dot, common
// math symbols) -- surveyed directly against the real repo before
// picking this list, not guessed. Anything non-ASCII NOT in this set
// gets flagged: the real attack this check targets is a HOMOGLYPH
// identifier (e.g. Cyrillic "а" standing in for Latin "a" in a
// variable name, visually indistinguishable) -- a technique with real
// documented supply-chain incidents, not a hypothetical.
const ALLOWED_NON_ASCII = new Set(['—', '─', 'Σ', '·', '²', 'ℹ', 'ő', 'Δ', '≈', '⇒', '…', '§']);

function findSuspiciousNonAscii(code) {
  const found = new Set();
  for (const ch of code) {
    if (ch.charCodeAt(0) > 127 && !ALLOWED_NON_ASCII.has(ch)) found.add(ch);
  }
  return [...found];
}

function stripComments(content) {
  return content
    .replace(/\/\*[\s\S]*?\*\//g, '') // block comments
    .split('\n')
    .map((line) => {
      // Naive line-comment strip: does not understand strings, so a
      // literal "//" inside a string literal is also stripped from
      // THAT point onward on its line. Rare and low-stakes for this
      // repo's own style (no such strings exist in this codebase's
      // source today, confirmed by this scanner finding zero spurious
      // findings when run against the real repo before shipping).
      const idx = line.indexOf('//');
      return idx === -1 ? line : line.slice(0, idx);
    })
    .join('\n');
}

function walkFiles(root) {
  const results = [];
  const stack = [root];
  while (stack.length) {
    const dir = stack.pop();
    let entries;
    try { entries = fs.readdirSync(dir, { withFileTypes: true }); } catch { continue; }
    for (const entry of entries) {
      if (EXCLUDED_DIR_NAMES.has(entry.name)) continue;
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        stack.push(full);
      } else if (entry.isFile() && (entry.name.endsWith('.js') || entry.name.endsWith('.mjs'))) {
        results.push(full);
      }
    }
    if (results.length > MAX_FILES) throw new Error(`selfAudit: scan exceeded the ${MAX_FILES}-file cap -- refusing to scan an unbounded tree`);
  }
  return results.sort();
}

const BASE64_LIKE = /['"`][A-Za-z0-9+/]{200,}={0,2}['"`]/;
const CHILD_PROCESS_IMPORT = /from\s+['"]node:child_process['"]|from\s+['"]child_process['"]|require\(\s*['"]child_process['"]\s*\)/;
// child_process's two families of API are NOT equally risky:
// exec()/execSync() run a SHELL COMMAND STRING (the shell itself
// interpolates metacharacters -- the classic injection vector), while
// execFile()/execFileSync()/spawn()/spawnSync() take an argv ARRAY with
// no shell in between, which is structurally immune to shell-metachar
// injection regardless of what the arguments contain. This distinction
// is not a guess: it is the ACTUAL real-world fix shape found by running
// this rule against this repo's own CVE benchmark -- growl's real
// command-injection fix (GHSA-qh2h-chj9-jffq) is exactly
// `require('child_process').exec` -> `require('child_process').spawn`,
// nothing else. `unreviewed-child-process` still fires either way (the
// import itself is still worth a human's attention), but
// `usesShellInterpolation` on that finding tells a reviewer whether the
// file uses the actually-dangerous half of the API at all.
const CHILD_PROCESS_SHELL_METHOD = /\b(?:exec|execSync)\s*\(/;
function usesChildProcessShellInterpolation(code) {
  return CHILD_PROCESS_SHELL_METHOD.test(code);
}

// A call to a function whose NAME looks guard-shaped, immediately
// preceding a risky call in the same enclosing top-level scope --
// confirmed against a real case: serialize-to-js's real CWE-502 fix
// (GHSA-mm62-wxc8-cf7m) adds exactly `str = sanitize(str)` on the line
// before the unchanged `new Function('...' + str)` call. This is a
// PROXIMITY heuristic, not real taint tracking -- it cannot verify the
// guard's return value is actually what reaches the risky call, only
// that a guard-shaped call exists nearby. `guardDetected` is reported
// so a human can deprioritize (not dismiss) a finding accordingly.
const GUARD_CALL_SHAPE = /\b(?:sanitiz|escape|validat|clean|whitelist|allowlist)\w*\s*\(/i;
function hasNearbyGuardCall(code, matchIndex) {
  const scopeStart = findEnclosingTopLevelStart(code, matchIndex);
  return GUARD_CALL_SHAPE.test(code.slice(scopeStart, matchIndex));
}
const NETWORK_MODULE_IMPORT = /from\s+['"]node:(https?|net|tls|dgram)['"]/;
// No `\s*` between the keyword and `(`: real dynamic-require/import
// obfuscation is written as a call expression, `require(x)`/`import(x)`
// with no space, same as every real call in this repo's own source is
// written -- requiring immediate adjacency was a deliberate choice
// after an early version flagged a false positive on a TEST NAME string
// containing the prose "...module-level import (not a fresh one)".
// Excludes a leading quote or backtick (a literal string, including a
// template literal, is not the risky pattern this targets -- this
// repo's own upgrade-6 cache-busting `import(\`...${r}\`)` pattern in
// several tests is real, reviewed, and must not be flagged).
const DYNAMIC_REQUIRE_IMPORT = /\b(?:require|import)\(\s*[^'"` )]/;
const PROTOTYPE_POLLUTION = /\.__proto__\s*(=[^=]|\[)|Object\s*\.\s*prototype\s*\.\s*\w+\s*=[^=]|Object\s*\.\s*setPrototypeOf\s*\(/;
// Real, known-weak/deprecated crypto: MD5/SHA-1/MD4 digests and HMACs
// (broken or badly weakened for integrity/authentication use), the
// deprecated createCipher/createDecipher API (no explicit IV -- a real,
// documented Node.js footgun), and legacy ciphers (DES/3DES/RC2/RC4).
const WEAK_CRYPTO = /createHash\(\s*['"](md5|sha1|md4)['"]|createHmac\(\s*['"](md5|sha1|md4)['"]|createCipher\(|createDecipher\(|createCipheriv\(\s*['"](des|des-ede3|rc4|rc2)/i;
// A "secret"-shaped identifier assigned directly to a quoted literal of
// meaningful length -- the real, extremely common hardcoded-credential
// vulnerability class. Cannot distinguish a genuine secret from an
// obvious placeholder ("YOUR_API_KEY_HERE") by pattern alone -- same
// "flag for human review, cannot be fully precise" disclosure as the
// fetch-call check.
const HARDCODED_CREDENTIAL = /\b(?:api[_-]?key|secret|password|passwd|access[_-]?token|auth[_-]?token)\b\s*[:=]\s*['"][A-Za-z0-9+/=_.\-]{12,}['"]/i;
const TLS_VERIFICATION_DISABLED = /rejectUnauthorized\s*:\s*false|NODE_TLS_REJECT_UNAUTHORIZED/;

// ── unguarded-dynamic-key-assignment ─────────────────────────────────
//
// The real shape most disclosed prototype-pollution CVEs actually have,
// confirmed directly by running THIS repo's own CVE benchmark
// (bench/FINDINGS.md section 3): a recursive merge/set/clone/extend
// helper iterates a source object's keys and assigns through a COMPUTED
// property on the target -- `o[key] = value` -- where `key` is data, not
// a literal, and nothing in the same scope compares `key` against
// "__proto__"/"constructor"/"prototype" before the assignment.
// PROTOTYPE_POLLUTION above only matches a LITERAL `.__proto__ =` token;
// it cannot see this shape AT ALL, because no such literal exists
// anywhere in the vulnerable source (see minimist's real `setKey`:
// `keys.slice(0,-1).forEach(function (key) { ... o[key] = {} ... })` --
// nine of this benchmark's ten real CWE-1321 dataset entries are exactly
// this shape and were missed by PROTOTYPE_POLLUTION alone before this
// rule existed).
//
// STILL A HEURISTIC, NOT A REAL TAINT TRACKER. This looks for the
// SYNTACTIC shape (a for..in loop or a .forEach whose loop variable is
// used as a computed property on an assignment's left side) and the
// ABSENCE of any same-scope comparison against the known-dangerous key
// names. It cannot see whether a guard actually lives in a helper
// function called from here instead of inline, whether the source is
// genuinely attacker-controlled, or a hundred other real-world
// variations -- flagged for human review, same as every other rule in
// this file, never "confirmed vulnerable."
const KEY_ITER_PATTERNS = [
  /for\s*\(\s*(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s+in\s+[^){]*\)\s*\{/g,
  /\.\s*forEach\s*\(\s*function\s*\(\s*([A-Za-z_$][\w$]*)[^)]*\)\s*\{/g,
  /\.\s*forEach\s*\(\s*\(\s*([A-Za-z_$][\w$]*)[^)]*\)\s*=>\s*\{/g,
  /\.\s*forEach\s*\(\s*([A-Za-z_$][\w$]*)\s*=>\s*\{/g,
];
// A plain, pre-ES6-style indexed loop -- `for (let i = 0; i < keys.length; i++) { let prop = keys[i]; ... prop used as [prop] = ... }`
// -- turned out, empirically, to be at least as common in real
// prototype-pollution CVEs as the for..in/.forEach shapes above (found
// by running this rule against this benchmark's OTHER "still missed"
// entries, e.g. set-value and hoek's clone/extend helpers, both written
// exactly this way). The loop header alone doesn't name a "key"
// variable -- it's bound inside the body via `let KEY = ARR[i]` -- so
// this is handled as its own two-step extraction rather than folded
// into KEY_ITER_PATTERNS above.
const INDEXED_FOR_LOOP = /for\s*\(\s*(?:var|let|const)\s+[A-Za-z_$][\w$]*\s*=\s*0\s*;[^){]*\)\s*\{/g;
const KEY_FROM_INDEX_RE = /(?:var|let|const)\s+([A-Za-z_$][\w$]*)\s*=\s*[A-Za-z_$][\w$]*\s*\[\s*[A-Za-z_$][\w$]*\s*\]\s*;/;
const DANGEROUS_KEY_LITERAL = /['"`](__proto__|constructor|prototype)['"`]/;
const MAX_KEY_ITER_MATCHES = 40; // bounded, same discipline as every other loop in this repo
const MAX_BODY_SCAN_CHARS = 6000;

// Where to START looking for a guard that precedes the loop -- NOT the
// loop's own {} body alone (misses a guard array declared just above the
// function that uses it, e.g. `var BLOCKED = ['__proto__', ...]`), and
// NOT the whole file either (an early, cruder version of this rule used
// a fixed lookback window and was caught by this repo's OWN benchmark
// producing a real, confirmed false positive: minimist's index.js has
// several unrelated top-level functions, and removing a `'__proto__'`
// literal from one function's guard, during an unrelated refactor,
// shifted what a fixed-size window considered "nearby" to a completely
// different, always-safe forEach elsewhere in the file -- a false
// finding driven by unrelated code moving, not by anything actually
// becoming less safe). The CORRECT boundary is the enclosing top-level
// declaration: scan backward from the match to the last point brace
// depth returned to 0, which is where the current top-level
// function/statement began. A guard declared in a DIFFERENT top-level
// function is correctly invisible; a guard declared just before THIS
// one, in the same enclosing scope, is correctly visible.
function findEnclosingTopLevelStart(code, matchIndex) {
  let depth = 0;
  let inString = null;
  let lastZeroDepthEnd = 0;
  for (let i = 0; i < matchIndex; i++) {
    const ch = code[i];
    if (inString) {
      if (ch === '\\') { i++; continue; }
      if (ch === inString) inString = null;
      continue;
    }
    if (ch === "'" || ch === '"' || ch === '`') { inString = ch; continue; }
    if (ch === '{') depth++;
    else if (ch === '}') { depth--; if (depth === 0) lastZeroDepthEnd = i + 1; }
  }
  return lastZeroDepthEnd;
}

// Extracts the first balanced {...} block starting at `openBraceIndex`,
// skipping over string-literal contents (so a stray brace character
// inside a string doesn't desynchronize the depth count) -- a bounded,
// disclosed heuristic, not a real parser, same category as
// stripComments() above.
function extractBalancedBraceBlock(code, openBraceIndex) {
  let depth = 0;
  let inString = null;
  const end = Math.min(code.length, openBraceIndex + MAX_BODY_SCAN_CHARS);
  for (let i = openBraceIndex; i < end; i++) {
    const ch = code[i];
    if (inString) {
      if (ch === '\\') { i++; continue; } // skip the escaped character, whatever it is
      if (ch === inString) inString = null;
      continue;
    }
    if (ch === "'" || ch === '"' || ch === '`') { inString = ch; continue; }
    if (ch === '{') depth++;
    else if (ch === '}') {
      depth--;
      if (depth === 0) return code.slice(openBraceIndex, i + 1);
    }
  }
  return code.slice(openBraceIndex, end); // unbalanced/truncated within the scan cap -- return what was seen
}

// Shared by both loop shapes below: true if `body` (the loop's own
// block) or `precedingScope` (everything since the enclosing top-level
// declaration began) references one of the dangerous key names -- see
// findEnclosingTopLevelStart's comment for why that boundary, not the
// whole file or a fixed window.
function hasNearbyDangerousKeyGuard(code, body, matchIndex) {
  if (DANGEROUS_KEY_LITERAL.test(body)) return true;
  const scopeStart = findEnclosingTopLevelStart(code, matchIndex);
  return DANGEROUS_KEY_LITERAL.test(code.slice(scopeStart, matchIndex));
}

function findUnguardedDynamicKeyAssignment(code) {
  let matchCount = 0;
  for (const pattern of KEY_ITER_PATTERNS) {
    pattern.lastIndex = 0;
    let m;
    while (matchCount < MAX_KEY_ITER_MATCHES && (m = pattern.exec(code))) {
      matchCount++;
      const keyVar = m[1];
      if (!keyVar) continue;
      const openBraceIndex = m.index + m[0].length - 1; // the loop/callback's opening '{', the last char of this match
      const body = extractBalancedBraceBlock(code, openBraceIndex);
      const assignsThroughKey = new RegExp(`\\[\\s*${keyVar}\\s*\\]\\s*=(?!=)`).test(body);
      if (!assignsThroughKey) continue;
      if (hasNearbyDangerousKeyGuard(code, body, m.index)) continue;
      return true; // one genuinely unguarded case is enough to flag the file
    }
  }

  INDEXED_FOR_LOOP.lastIndex = 0;
  let m;
  while (matchCount < MAX_KEY_ITER_MATCHES && (m = INDEXED_FOR_LOOP.exec(code))) {
    matchCount++;
    const openBraceIndex = m.index + m[0].length - 1;
    const body = extractBalancedBraceBlock(code, openBraceIndex);
    const keyBinding = KEY_FROM_INDEX_RE.exec(body);
    if (!keyBinding) continue; // no `let key = someArray[i];`-shaped binding in this loop -- not this shape
    const keyVar = keyBinding[1];
    const assignsThroughKey = new RegExp(`\\[\\s*${keyVar}\\s*\\]\\s*=(?!=)`).test(body);
    if (!assignsThroughKey) continue;
    if (hasNearbyDangerousKeyGuard(code, body, m.index)) continue;
    return true;
  }
  return false;
}

// Every pattern check below, factored out of scanFile so it can also run
// against ARBITRARY TEXT that is not a repo file at all -- specifically,
// iterativeVerify.js's per-round red-flag gate on a model's own proposed
// spec/rationale text, before that text is used for anything. Runs with
// NO whitelist: whitelisting means "this specific, reviewed REPO FILE is
// a known, disclosed exception," which has no meaning for ephemeral,
// freshly-generated model output -- there is nothing to have reviewed
// yet. scanFile below is unchanged in behavior; it now calls this and
// then applies its own file-scoped whitelist filtering on top.
export function scanCodeText(code, raw = code) {
  const findings = [];
  const evalMatch = /\beval\s*\(/.exec(code);
  if (evalMatch) findings.push({ code: 'eval-usage', severity: 'high', detail: 'eval( found in executable code -- this repo has a standing zero-eval rule; any real use needs explicit review and, if legitimate, adding to selfAudit.js\'s own EVAL_WHITELIST', guardDetected: hasNearbyGuardCall(code, evalMatch.index) });
  const newFunctionMatch = /\bnew\s+Function\s*\(/.exec(code);
  if (newFunctionMatch) findings.push({ code: 'function-constructor-usage', severity: 'high', detail: 'new Function( found -- equivalent to eval for this repo\'s purposes', guardDetected: hasNearbyGuardCall(code, newFunctionMatch.index) });
  if (BASE64_LIKE.test(raw)) findings.push({ code: 'unreviewed-base64-blob', severity: 'medium', detail: 'a long (200+ char) base64-like string literal was found in a file not on the reviewed WASM-constant whitelist -- verify what this actually encodes before trusting it' });
  if (CHILD_PROCESS_IMPORT.test(code)) findings.push({ code: 'unreviewed-child-process', severity: 'high', detail: 'node:child_process is imported outside the reviewed whitelist -- arbitrary process execution is one of the most common real supply-chain payload mechanisms', usesShellInterpolation: usesChildProcessShellInterpolation(code) });
  if (NETWORK_MODULE_IMPORT.test(code)) findings.push({ code: 'unreviewed-network-module', severity: 'medium', detail: 'a low-level networking module (node:http/https/net/tls/dgram) is imported outside the reviewed whitelist -- verify this is expected (a server or client this repo genuinely needs), not injected' });
  if (/\bfetch\s*\(/.test(code)) {
    // Deliberately broader/noisier than the checks above: this pattern
    // cannot distinguish a real call to the global fetch from a local
    // function/method also named "fetch" (e.g. connectors/*.js's own
    // DataSource.fetch() methods) -- disclosed directly in the finding
    // text rather than silently guessing which one it is.
    findings.push({ code: 'unreviewed-fetch-call', severity: 'low', detail: 'a "fetch(" call site was found outside the reviewed whitelist -- this heuristic cannot distinguish a real network call from a same-named local method (e.g. a DataSource\'s own fetch()); verify manually' });
  }
  if (DYNAMIC_REQUIRE_IMPORT.test(code)) findings.push({ code: 'dynamic-require-or-import', severity: 'high', detail: 'require(/import( was called with a non-literal, non-template argument -- a well-documented obfuscation/loader technique real npm supply-chain payloads use to hide which module actually gets loaded until runtime' });
  const suspiciousChars = findSuspiciousNonAscii(code);
  if (suspiciousChars.length > 0) findings.push({ code: 'suspicious-non-ascii-character', severity: 'medium', detail: `unexpected non-ASCII character(s) found outside comments: [${suspiciousChars.join(' ')}] -- could be entirely legitimate (a name, a symbol) or a homoglyph disguising an identifier (e.g. Cyrillic "а" standing in for Latin "a"); verify manually. Known-legitimate symbols this repo already uses are pre-whitelisted.` });
  if (PROTOTYPE_POLLUTION.test(code)) findings.push({ code: 'prototype-mutation', severity: 'high', detail: 'a direct __proto__ assignment or Object.prototype mutation was found -- prototype pollution is a real, well-documented class of supply-chain vulnerability; a comparison (===) or a safe read (hasOwnProperty.call) would not trigger this, only an actual assignment' });
  if (findUnguardedDynamicKeyAssignment(code)) findings.push({ code: 'unguarded-dynamic-key-assignment', severity: 'high', detail: 'a loop assigns through a COMPUTED property (o[key] = ...) using a key taken from iterating another object\'s keys, with no comparison against "__proto__"/"constructor"/"prototype" found anywhere in the same loop body -- the actual shape behind most disclosed prototype-pollution CVEs (a recursive merge/set/clone/extend helper), which a literal ".__proto__ =" pattern cannot see at all; verify whether the iterated keys can be attacker-controlled and add an explicit guard if so' });
  if (WEAK_CRYPTO.test(code)) findings.push({ code: 'weak-crypto-algorithm', severity: 'high', detail: 'a known-weak or deprecated cryptographic algorithm/API was found (MD5/SHA-1/MD4, the no-explicit-IV createCipher/createDecipher API, or a legacy cipher like DES/3DES/RC2/RC4) -- fail-safe cryptography means never silently downgrading to a broken primitive; replace with SHA-256+ / HMAC-SHA-256+ / createCipheriv with a real IV / AES-GCM' });
  if (HARDCODED_CREDENTIAL.test(code)) findings.push({ code: 'hardcoded-credential', severity: 'high', detail: 'a secret-shaped identifier (api key/secret/password/token) was assigned directly to a quoted literal -- this heuristic cannot distinguish a real credential from an obvious placeholder; verify manually. This repo\'s own standing rule is that every real key is read from process.env, never hardcoded (see modelClient.js/geminiClient.js) -- any real match here is a genuine regression from that rule' });
  if (TLS_VERIFICATION_DISABLED.test(code)) findings.push({ code: 'tls-verification-disabled', severity: 'high', detail: 'certificate verification is disabled (rejectUnauthorized:false or NODE_TLS_REJECT_UNAUTHORIZED) outside the one reviewed, disclosed case -- this is the single most common way TLS silently fails OPEN instead of failing closed; verify this is genuinely needed and disclosed, not a quietly reintroduced MITM surface' });
  return findings;
}

// code -> the Set to check relPath against for that code's whitelist, if it has one at all
const PER_FILE_WHITELISTS = {
  'eval-usage': EVAL_WHITELIST,
  'function-constructor-usage': EVAL_WHITELIST,
  'unreviewed-base64-blob': BASE64_BLOB_WHITELIST,
  'unreviewed-child-process': CHILD_PROCESS_WHITELIST,
  'unreviewed-network-module': NETWORK_MODULE_WHITELIST,
  'unreviewed-fetch-call': FETCH_CALL_WHITELIST,
  'tls-verification-disabled': TLS_VERIFICATION_DISABLED_WHITELIST,
  'unguarded-dynamic-key-assignment': UNGUARDED_KEY_ASSIGNMENT_WHITELIST,
};

function scanFile(root, absPath) {
  const relPath = path.relative(root, absPath).replace(/\\/g, '/');
  const stat = fs.statSync(absPath);
  if (stat.size > MAX_FILE_BYTES) {
    return [{ code: 'file-too-large-to-scan', severity: 'low', file: relPath, detail: `${stat.size} bytes exceeds the ${MAX_FILE_BYTES}-byte scan cap; not scanned` }];
  }
  const raw = fs.readFileSync(absPath, 'utf8');
  const code = stripComments(raw);

  return scanCodeText(code, raw)
    .filter((f) => !(PER_FILE_WHITELISTS[f.code]?.has(relPath)))
    .map((f) => ({ ...f, file: relPath }));
}

/** Scans every .js/.mjs file under `root` (excluding node_modules/.git/generated-state dirs) for the disclosed red-flag patterns above. Bounded, never modifies anything. */
export function auditSourceTree(root) {
  const files = walkFiles(root);
  const findings = files.flatMap((f) => scanFile(root, f));
  return {
    filesScanned: files.length,
    findings,
    clean: findings.length === 0,
  };
}

/**
 * A real, narrow supply-chain check specific to this repo's own
 * standing "zero npm dependencies" rule: verifies package.json
 * actually has none, and has no lifecycle scripts (preinstall/install/
 * postinstall) — the single most common real npm supply-chain attack
 * vector (a malicious package's postinstall hook), and one this repo
 * structurally cannot be exposed to as long as this stays true.
 */
export function auditPackageJson(root) {
  const pkgPath = path.join(root, 'package.json');
  if (!fs.existsSync(pkgPath)) return { findings: [{ code: 'no-package-json', severity: 'low', file: 'package.json', detail: 'no package.json found at repo root' }] };
  const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));
  const findings = [];

  const depFields = ['dependencies', 'devDependencies', 'optionalDependencies', 'peerDependencies'];
  for (const field of depFields) {
    const deps = pkg[field];
    if (deps && Object.keys(deps).length > 0) {
      findings.push({ code: 'unexpected-dependency', severity: 'high', file: 'package.json', detail: `${field} is non-empty (${Object.keys(deps).join(', ')}) — this repo's standing rule is zero npm dependencies; any real dependency here is a real supply-chain surface that didn't exist before and needs explicit review` });
    }
  }

  const lifecycleScripts = ['preinstall', 'install', 'postinstall'];
  const scripts = pkg.scripts || {};
  for (const name of lifecycleScripts) {
    if (scripts[name]) {
      findings.push({ code: 'lifecycle-script-present', severity: 'high', file: 'package.json', detail: `a "${name}" lifecycle script is defined ("${scripts[name]}") -- this is the single most common real npm supply-chain attack vector (code that runs automatically on install)` });
    }
  }

  return { findings, clean: findings.length === 0 };
}

function hashFile(absPath) {
  const content = fs.readFileSync(absPath);
  return crypto.createHash('sha256').update(content).digest('hex');
}

/**
 * Hashes every currently-scanned file (SHA-256, node:crypto, zero new
 * dependencies) into a manifest: `{version, createdAt, files:
 * {relPath: sha256hex}}`. This is the real technique behind actual
 * supply-chain integrity tools (the same idea as an npm lockfile's
 * integrity hashes, or Subresource Integrity for a script tag) — a
 * TRUSTED SNAPSHOT to diff future scans against, which the pattern
 * checks above have no equivalent of on their own (they inspect
 * content in isolation; they cannot tell you "this file changed since
 * you last trusted it").
 */
export function recordBaseline(root) {
  const files = walkFiles(root);
  const manifest = { version: 1, createdAt: Date.now(), files: {} };
  for (const absPath of files) {
    const relPath = path.relative(root, absPath).replace(/\\/g, '/');
    manifest.files[relPath] = hashFile(absPath);
  }
  return manifest;
}

/**
 * Recomputes the current tree's hashes and diffs against a
 * previously-recorded `baseline` manifest (from `recordBaseline`,
 * typically loaded back from wherever the caller persisted it — this
 * function takes the manifest object directly, it does not own any
 * storage format or location itself). Returns real, itemized
 * added/removed/modified lists, never a bare "changed: true/false" —
 * the same "specific finding, not a vague alarm" discipline as every
 * other check in this file.
 */
export function checkAgainstBaseline(root, baseline) {
  if (!baseline || typeof baseline.files !== 'object') throw new Error('checkAgainstBaseline needs a manifest object from recordBaseline()');
  const files = walkFiles(root);
  const currentByRelPath = new Map();
  for (const absPath of files) {
    const relPath = path.relative(root, absPath).replace(/\\/g, '/');
    currentByRelPath.set(relPath, hashFile(absPath));
  }

  const added = [];
  const removed = [];
  const modified = [];
  let unchanged = 0;

  for (const [relPath, hash] of currentByRelPath.entries()) {
    if (!(relPath in baseline.files)) {
      added.push(relPath);
    } else if (baseline.files[relPath] !== hash) {
      modified.push(relPath);
    } else {
      unchanged++;
    }
  }
  for (const relPath of Object.keys(baseline.files)) {
    if (!currentByRelPath.has(relPath)) removed.push(relPath);
  }

  return {
    baselineRecordedAt: baseline.createdAt,
    added: added.sort(),
    removed: removed.sort(),
    modified: modified.sort(),
    unchanged,
    clean: added.length === 0 && removed.length === 0 && modified.length === 0,
  };
}

/** Runs both checks and merges the result -- the one entry point a caller (or the CLI) actually needs. */
export function runSelfAudit(root) {
  const sourceResult = auditSourceTree(root);
  const pkgResult = auditPackageJson(root);
  const findings = [...sourceResult.findings, ...pkgResult.findings];
  return {
    filesScanned: sourceResult.filesScanned,
    findings,
    clean: findings.length === 0,
    honesty: 'This is a disclosed, narrow set of pattern checks grounded in this repo\'s own standing rules (zero eval, zero npm dependencies, a known list of files that legitimately touch the network/child_process) -- not a general malware/trojan detector. A clean result means none of THESE checks fired; it is not a certification that no malicious code exists. Findings are for human review; nothing here modifies, deletes, or quarantines any file.',
  };
}
