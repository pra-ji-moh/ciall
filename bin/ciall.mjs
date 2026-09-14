#!/usr/bin/env node
// bin/ciall.mjs; the thing that was missing — a real caller.
//
// Everything in src/lib/deviceExecutor.js and commandExecutor.js requires
// a confirm(action) function and refuses to run without one. Until now,
// nothing in this repo supplied one, so none of it could actually do
// anything. This file supplies a real one: it prints the exact proposed
// action to the terminal and blocks on a literal y/N answer, the same
// shape as a permission prompt in an IDE or coding agent — nothing runs
// until a human looks at the specific action and says yes, every time.
//
// Usage:
//   node bin/ciall.mjs write <relPath> <content...>
//   node bin/ciall.mjs write <relPath> <content...> --check "<lhs><=|>=<rhs>" --var name=lo:hi [--var ...]
//     Runs the numeric-check kernel against the claim BEFORE asking for
//     confirmation, and attaches the verdict to what confirm() sees. No
//     model needed — the spec is built directly from these flags.
//     Example: --check "-1<=x^2" --var x=-10:10   (always holds)
//              --check "x^2<=-1" --var x=-10:10   (never holds; try it)
//   node bin/ciall.mjs write <relPath> <content...> --live [core|all|k1,k2,...] [--provider gemini|anthropic]
//     Runs the CONTENT through model-backed kernels (consistency, mcmc,
//     numeric-check, dynamics, and with an explicit id: combinatorial,
//     domain-of-validity) via a REAL model call, before asking for
//     confirmation. Defaults to Gemini (GEMINI_API_KEY) for now;
//     `--provider anthropic` switches to Claude (ANTHROPIC_API_KEY)
//     instead. The key is read from your environment ONLY — never as an
//     argument, never prompted for. Bare `--live` runs the "core" set
//     (consistency, mcmc, numeric-check, dynamics); `--live all` runs
//     every kernel that has one; `--live mcmc,dynamics` runs exactly
//     those. Same rule as --check: a violation does not auto-block, you
//     still decide, now informed.
//   node bin/ciall.mjs verify-iterative "<claim text>" [--kernels k1,k2] [--max-rounds N] [--provider gemini|anthropic]
//     The iterative counterpart to --live above (src/lib/iterativeVerify.js):
//     instead of one model call designing one spec, the model sees each
//     round's REAL deterministic kernel result and decides to accept,
//     revise (same or a different kernel, a genuinely new spec — never a
//     louder restatement of the same unverified claim), or propose a
//     real action. Capped at --max-rounds (default 4, hard max 10).
//     Prints the full round trace. Read-only: this command NEVER writes
//     a file or runs a command itself — a round proposing one is printed
//     and the command stops there; use `write ... --check`/`--live`
//     (with its own real confirm() prompt) if you actually want to act
//     on what it found.
//   node bin/ciall.mjs shift-left-scan [dir]        (default: this repo)
//     src/lib/shiftLeftScan.js: runs self-audit, then attempts to PROVE
//     (chainKernel.js's real SAT+DRAT machinery, zero model calls)
//     whether two or more of the findings compose into a genuine chain
//     -- proactive, before any CVE is ever filed. Read-only, no
//     confirm() gate. See that file's header for the mapping table and
//     its disclosed scope; never generates a PoC or trigger sequence.
//   node bin/ciall.mjs read <relPath>
//   node bin/ciall.mjs run <executable> [args...]
//   node bin/ciall.mjs audit                       (prints the persisted execution audit log)
//   node bin/ciall.mjs domains                     (lists the registered vertical packs — finance, engineering, requirements — and what each exposes)
//   node bin/ciall.mjs stream-motion [--model "5*t" --tolerance 0.5] [--ring-buffer N]
//     Real-time event ingestion (upgrade 7): reads newline-delimited
//     JSON from stdin as it arrives and verifies each event immediately
//     against the kernel its "type" names. Two line shapes:
//       {"type":"<kernelName>","payload":{...},"ts":<epoch_ms>}   (any
//         kernel registered in src/lib/kernelRegistry.js, by its id —
//         "consistency", "mcmc", "numeric-check", "dynamics",
//         "combinatorial", "domain-of-validity", "order-consistency",
//         "boundary-check", "neuromorphic-power", "event-camera-pixel",
//         "decision-helper" — or the special built-in "motion")
//       {"t":...,"value":...}   (no "type" field at all — the ORIGINAL
//         stream-motion schema, still fully supported: treated exactly
//         as {"type":"motion","payload":{...}}, same computation, same
//         verdicts as before this upgrade. --model/--tolerance are only
//         needed if the stream actually contains motion events — a
//         pure non-motion stream can omit both.)
//     "Fast" kernels (consistency, order-consistency, boundary-check,
//     domain-of-validity, neuromorphic-power, motion) run synchronously,
//     target latency <1ms. "Slow" kernels (mcmc, dynamics, numeric-check,
//     combinatorial — every one that budgets its own real wall-clock
//     time) dispatch to the same worker pool upgrade 1 added; reading
//     more input is never blocked by one of these running. A
//     configurable ring buffer (--ring-buffer N, default 1024) absorbs
//     bursts beyond the worker pool's capacity; on overflow it drops
//     the OLDEST buffered event and logs the drop to stderr, never
//     stdin. Output is NDJSON on stdout, one line per input event, IN
//     ARRIVAL ORDER regardless of which events resolve out of order:
//     {"type":"<kernelName>","ts":<epoch_ms>,"result":{...}}.
//     Reads nothing but stdin — no camera, no hardware, no confirm()
//     gate (read-only analysis, not a device action). Feed it from
//     anything that produces a live stream: `tail -f data.jsonl | node
//     bin/ciall.mjs stream-motion`, a camera-tracking pipeline piping
//     JSON lines in, a sensor, whatever the real source is.
//   node bin/ciall.mjs serve --port <n>
//     Distributed kernel offloading (upgrade 8): exposes every
//     registered kernel over TLS + a from-scratch HTTP/2 + HPACK +
//     protobuf + MessagePack gRPC-shaped transport (KernelService.Run),
//     no new kernel logic — the exact same {buildPrompt, normalize, run}
//     interface every kernel already has, just reachable over the
//     network. Requires CIALL_TLS_CERT_PATH and CIALL_TLS_KEY_PATH
//     (self-signed is fine for local-network use — this command does
//     not generate one for you; see src/lib/rpc/devCert.js if you need
//     a throwaway cert for local testing). On the CALLING side, point
//     orchestrator.js at this server with
//     CIALL_REMOTE_KERNELS="kernelName:host:port,...": named kernels
//     route here instead of running locally, with automatic retry +
//     local fallback if this server is unreachable — a verification
//     call never fails just because the network did.
//   node bin/ciall.mjs memory <put|get|list|related|contradictions> ...
//     A persistent, long-term memory layer for this substrate — GBrain-
//     inspired, deliberately not the same (file-backed, zero-LLM
//     self-wiring, no database/embeddings/API key). `put <id> <text>`
//     writes a page's compiled-truth; `get <id>` prints it (including
//     its full append-only timeline); `list` enumerates every page;
//     `related <id>` walks the self-wiring graph (shared entities AND
//     contradiction edges); `contradictions` runs consistencyKernel.js's
//     REAL findContradictions() across every claim recorded in memory,
//     catching disagreements even across pages that never mention each
//     other directly. See src/lib/memory/memoryStore.js's header for
//     the full design rationale.
//
// This CLI operates on the REAL persisted sandbox
// (../ciall-sandbox next to this repo) and the REAL persisted audit log —
// unlike the test suite, which always overrides both to a throwaway temp
// location. Nothing here runs automatically; you invoke each command by
// hand, and each one still asks before doing anything.
//
// A NOTE ON "run" SPECIFICALLY. deviceGate's grants live in memory and do
// NOT survive between separate CLI invocations (each `node bin/ciall.mjs
// ...` is a fresh process) — an earlier version of this file had a
// separate `grant-run` command for this reason, and it did not work: the
// grant vanished the instant that process exited, so a following `run`
// in a new process never saw it. Fixed by scoping the executable for
// THIS invocation only, right before asking for confirmation — the scope
// grant and the actual execution happen in the same process, but they
// remain two independent checks: being in scope never skips the
// confirm() prompt below.

import readline from 'node:readline';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { writeFile, readFile, readExecutionAudit, getExecutionAuditPath } from '../src/lib/deviceExecutor.js';
import { runCommand } from '../src/lib/commandExecutor.js';
import { grant } from '../src/lib/deviceGate.js';
import { getSandboxPath } from '../src/lib/sandboxConfig.js';
import { makeLiveDesignSpec } from '../src/lib/modelDesignSpec.js';
import { runIterativeVerification, makeLiveIterativeModel, DEFAULT_MAX_ROUNDS, HARD_MAX_ROUNDS } from '../src/lib/iterativeVerify.js';
import { shutdownPool, runKernelTask, POOL_SIZE } from '../src/lib/kernelWorkerPool.js';
import { createEventStreamProcessor, DEFAULT_RING_CAPACITY } from '../src/lib/eventStream.js';
import { exportWasm, exportHlsC, exportSpecJson, defaultExportRoot } from '../src/lib/kernelExport.js';
import { startKernelServiceServer } from '../src/lib/rpc/kernelServiceServer.js';
import { defaultMemoryRoot } from '../src/lib/memory/memoryStore.js';
import { putPage, getPage, listPages, buildGraph, edgesFor, findContradictingMemories } from '../src/lib/memory/claimMemory.js';
import { getKernel, listKernels } from '../src/lib/kernelRegistry.js';
import { measureTimingVarianceAsync } from '../src/lib/timingVariance.js';
import { gatePhysicalAction } from '../src/lib/physicalActionGate.js';
import { startDashboardServer } from '../src/lib/dashboardServer.js';

const CORE_LIVE_KERNELS = ['consistency', 'mcmc', 'numeric-check', 'dynamics'];

// Builds a { kernelIds, designSpec } verify option backed by a REAL model
// call. The key comes from process.env ONLY — this is the one and only
// place in this CLI that reads it, and it is never logged, never passed
// as a CLI argument (which would land in shell history / the process
// list), never written to the audit log.
//
// Defaults to Gemini (GEMINI_API_KEY) for now, per current preference.
// `--live [which] --provider anthropic` switches to Claude
// (ANTHROPIC_API_KEY) instead; everything downstream is identical either
// way, since modelClient.js and geminiClient.js mirror each other's
// interface on purpose.
// Shared by --live and verify-iterative: resolves the provider's API key
// from process.env ONLY (never a CLI argument, never a prompt -- see
// buildLiveVerify's own header note, unchanged by this factoring). Prints
// its own error and returns null on any problem, so callers can just
// check for that rather than duplicating the two error messages.
function resolveApiKey(provider) {
  if (provider !== 'gemini' && provider !== 'anthropic') {
    console.error(`unknown --provider "${provider}"; expected "gemini" or "anthropic"`);
    return null;
  }
  const envVar = provider === 'gemini' ? 'GEMINI_API_KEY' : 'ANTHROPIC_API_KEY';
  const apiKey = process.env[envVar];
  if (!apiKey) {
    console.error(`(provider: ${provider}) needs ${envVar} set in your environment. Never pass it as an argument or type it into a prompt.`);
    return null;
  }
  return apiKey;
}

function buildLiveVerify(flagArgs) {
  let which = 'core';
  let provider = 'gemini';
  let i = 1;
  if (flagArgs[i] && flagArgs[i] !== '--provider') { which = flagArgs[i]; i++; }
  if (flagArgs[i] === '--provider') { provider = flagArgs[i + 1] || provider; }

  const apiKey = resolveApiKey(provider);
  if (!apiKey) return null;
  const kernelIds = which === 'all' ? undefined : which === 'core' ? CORE_LIVE_KERNELS : which.split(',').map((s) => s.trim()).filter(Boolean);
  return { kernelIds, designSpec: makeLiveDesignSpec(apiKey, { provider }) };
}

function askYesNo(promptText) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  return new Promise((resolve) => {
    rl.question(`${promptText} [y/N] `, (answer) => {
      rl.close();
      resolve(String(answer).trim().toLowerCase() === 'y');
    });
  });
}

// The one real confirm() implementation in this repo. Describes the
// EXACT action (not a category, not "file access") and requires an
// explicit "y" — anything else, including just pressing enter, is a no.
async function terminalConfirm(action) {
  console.log('');
  console.log('Ciall wants to do the following, on this machine, right now:');
  console.log(JSON.stringify(action, null, 2));
  return askYesNo('Allow this exact action?');
}

// Builds a { kernelIds: ['numeric-check'], designSpec } verify option
// directly from CLI flags — deterministic, no model call, which is why
// this is the one kernel the CLI can drive standalone. Returns null (and
// prints why) on a malformed flag set.
function parseCheckFlags(flagArgs) {
  let checkExpr = null;
  const vars = [];
  for (let i = 0; i < flagArgs.length; i++) {
    if (flagArgs[i] === '--check') { checkExpr = flagArgs[++i]; }
    else if (flagArgs[i] === '--var') {
      const spec = flagArgs[++i];
      const m = /^([A-Za-z_]\w*)=(-?[\d.]+):(-?[\d.]+)$/.exec(spec || '');
      if (!m) { console.error(`bad --var "${spec}"; expected name=lo:hi, e.g. x=-10:10`); return null; }
      vars.push({ name: m[1], domain: [Number(m[2]), Number(m[3])] });
    }
  }
  if (!checkExpr) { console.error('--check needs an expression, e.g. --check "-1<=x^2"'); return null; }
  const opMatch = /^(.+?)(<=|>=)(.+)$/.exec(checkExpr);
  if (!opMatch) { console.error('--check expression must contain <= or >='); return null; }
  const [, lhsRaw, op, rhsRaw] = opMatch;
  const [lhs, rhs] = op === '<=' ? [lhsRaw.trim(), rhsRaw.trim()] : [rhsRaw.trim(), lhsRaw.trim()];
  if (vars.length === 0) { console.error('--check needs at least one --var name=lo:hi'); return null; }

  return {
    kernelIds: ['numeric-check'],
    designSpec: (kernel) => (kernel.id === 'numeric-check' ? { kind: 'inequality', lhs, rhs, vars } : null),
  };
}

async function main() {
  const [cmd, ...rest] = process.argv.slice(2);

  if (cmd === 'write') {
    if (rest.includes('--check') && rest.includes('--live')) {
      console.error('use either --check or --live, not both, in one write');
      process.exitCode = 1;
      return;
    }
    const flagIdx = rest.findIndex((a) => a === '--check' || a === '--live');
    const mainArgs = flagIdx === -1 ? rest : rest.slice(0, flagIdx);
    const [relPath, ...contentParts] = mainArgs;
    if (!relPath) { console.error('usage: ciall write <relPath> <content...> [--check "lhs<=rhs" --var name=lo:hi ...] [--live [core|all|k1,k2]]'); process.exitCode = 1; return; }
    const content = contentParts.join(' ');

    let verify;
    if (flagIdx !== -1) {
      const flagArgs = rest.slice(flagIdx);
      verify = flagArgs[0] === '--check' ? parseCheckFlags(flagArgs) : buildLiveVerify(flagArgs);
      if (!verify) { process.exitCode = 1; return; } // the relevant parser already printed why
    }

    try {
      const result = await writeFile(relPath, content, { confirm: terminalConfirm, requestedBy: 'cli', verify });
      console.log(result.executed ? `Wrote ${result.target}` : `Not written: ${result.reason}`);
    } catch (e) {
      console.log(`Refused: ${e.message}`);
      process.exitCode = 1;
    }
    return;
  }

  if (cmd === 'read') {
    const [relPath] = rest;
    if (!relPath) { console.error('usage: ciall read <relPath>'); process.exitCode = 1; return; }
    try {
      const result = await readFile(relPath, { confirm: terminalConfirm, requestedBy: 'cli' });
      console.log(result.executed ? result.content : `Not read: ${result.reason}`);
    } catch (e) {
      console.log(`Refused: ${e.message}`);
      process.exitCode = 1;
    }
    return;
  }

  if (cmd === 'run') {
    const [executable, ...args] = rest;
    if (!executable) { console.error('usage: ciall run <executable> [args...]'); process.exitCode = 1; return; }
    // Scope this executable for THIS invocation only (see header note).
    // Being in scope is one independent check; confirm() below is the
    // other, and it is never skipped by this grant existing.
    grant({ id: `cli-run-${Date.now()}`, scope: 'process-launch', boundary: [executable], expiresAt: Date.now() + 60_000, grantedBy: 'cli (this invocation only)' });
    const result = await runCommand(executable, args, { confirm: terminalConfirm, requestedBy: 'cli' });
    if (!result.executed) { console.log(`Not run: ${result.reason}`); return; }
    if (result.stdout) process.stdout.write(result.stdout);
    if (result.stderr) process.stderr.write(result.stderr);
    console.log(`(exit code ${result.exitCode})`);
    return;
  }

  if (cmd === 'audit') {
    console.log(`Sandbox: ${getSandboxPath()}`);
    console.log(`Audit log: ${getExecutionAuditPath()}`);
    console.log('');
    for (const entry of readExecutionAudit()) {
      console.log(`${new Date(entry.at).toISOString()}  ${entry.op}  executed=${entry.executed}  ${entry.target || entry.executable || ''}  ${entry.reason || ''}`);
    }
    return;
  }

  if (cmd === 'domains') {
    const { listDomains } = await import('../src/domains/registry.js');
    for (const d of listDomains()) {
      console.log(`${d.id}: ${d.label}`);
      console.log(`  ${d.description}`);
      console.log(`  functions: ${Object.keys(d.module).join(', ')}`);
      console.log('');
    }
    return;
  }

  if (cmd === 'ontology') {
    const { listObjectTypes } = await import('../src/ontology/objectTypes.js');
    for (const t of listObjectTypes()) {
      console.log(`${t.name} (${t.domain})`);
      console.log(`  ${t.description}`);
      console.log(`  properties: ${Object.entries(t.properties).map(([k, v]) => `${k}:${v}`).join(', ')}`);
      console.log(`  relationships: ${t.relationships.length ? t.relationships.join(', ') : '(none)'}`);
      console.log(`  verifiedBy kernel: ${t.verifiedBy}`);
      console.log('');
    }
    return;
  }

  if (cmd === 'stream-motion') {
    // Real-time typed event ingestion (upgrade 7 — see the header
    // comment above for the full shape). This does NOT open a camera or
    // any hardware — it's a real-time CONSUMER; feed it from anything
    // that produces a live stream (a camera-tracking pipeline piping
    // JSON lines into this process, a sensor, `tail -f` into a small
    // adapter, whatever). No confirm() gate here since this is
    // read-only analysis of incoming data, not a device action.
    const modelIdx = process.argv.indexOf('--model');
    const tolIdx = process.argv.indexOf('--tolerance');
    const ringIdx = process.argv.indexOf('--ring-buffer');
    // --model/--tolerance are only required if a motion event actually
    // shows up; a stream that never contains one has no need of them.
    // A caller that supplies ONE but not the other almost certainly
    // meant to enable motion and mistyped a flag — fail loudly rather
    // than silently treat motion events as errors for the whole run.
    if ((modelIdx === -1) !== (tolIdx === -1)) {
      console.error('usage: ciall stream-motion [--model "<expr in t>" --tolerance <number>] [--ring-buffer <n>]   (--model and --tolerance must be given together, or not at all)');
      process.exitCode = 1;
      return;
    }
    const motion = modelIdx === -1 ? undefined : { modelExpr: process.argv[modelIdx + 1], tolerance: Number(process.argv[tolIdx + 1]) };
    const ringCapacity = ringIdx === -1 ? DEFAULT_RING_CAPACITY : Number(process.argv[ringIdx + 1]);
    if (!Number.isInteger(ringCapacity) || ringCapacity < 1) {
      console.error(`--ring-buffer must be a positive integer, got "${process.argv[ringIdx + 1]}"`);
      process.exitCode = 1;
      return;
    }

    const processor = createEventStreamProcessor({
      dispatchSlow: runKernelTask,
      poolSize: POOL_SIZE,
      ringCapacity,
      motion,
      onOutput: (line) => console.log(line),
      onDrop: (total, dropped) => console.error(`(back-pressure: dropped event type="${dropped.type}" — ${total} dropped so far)`),
    });

    // Manual newline scan over stdin in flowing mode, per the upgrade 7
    // spec — not readline. buffer holds at most ONE incomplete line at
    // a time: every complete line found in a chunk is sliced off and
    // handed to processLine() immediately, and buffer is trimmed down
    // to only whatever trailing, not-yet-terminated fragment remains.
    //
    // main() awaits this whole block (see the bottom of this file):
    // 'end' firing means stdin is closed, but an 'async' kernel event
    // submitted right before EOF can still be running in a worker: the
    // command must not return (and let shutdownPool() fire) until
    // processor.end() confirms every submitted event has actually
    // flushed to output.
    await new Promise((resolveStream) => {
      let buffer = '';
      process.stdin.setEncoding('utf8');
      process.stdin.on('data', (chunk) => {
        buffer += chunk;
        let newlineIdx;
        while ((newlineIdx = buffer.indexOf('\n')) !== -1) {
          const line = buffer.slice(0, newlineIdx).replace(/\r$/, '');
          buffer = buffer.slice(newlineIdx + 1);
          if (line.trim()) processor.processLine(line);
        }
      });
      process.stdin.on('end', async () => {
        if (buffer.trim()) processor.processLine(buffer);
        await processor.end();
        const status = processor.motionStatus();
        if (status) {
          console.error('');
          console.error(`Final (motion): ${status.verdict} across ${status.sampleCount} samples`);
          console.error(status.honesty);
        }
        if (processor.droppedTotal() > 0) {
          console.error(`(${processor.droppedTotal()} event(s) dropped total due to back-pressure)`);
        }
        resolveStream();
      });
    });
    return;
  }

  if (cmd === 'export-kernels') {
    // Upgrade 9: exports the pure deterministic kernels as standalone
    // artifacts. modelClient.js/geminiClient.js (the AI parsing layer)
    // are never touched by any format here.
    const formatIdx = process.argv.indexOf('--format');
    const format = formatIdx === -1 ? null : process.argv[formatIdx + 1];
    if (!['wasm', 'hls-c', 'spec-json'].includes(format)) {
      console.error('usage: ciall export-kernels --format <wasm|hls-c|spec-json>');
      process.exitCode = 1;
      return;
    }
    const outDir = defaultExportRoot();
    if (format === 'wasm') {
      const written = exportWasm(outDir);
      console.log(`Wrote ${written.length} file(s):`);
      written.forEach((f) => console.log(`  ${f}`));
    } else if (format === 'hls-c') {
      const written = exportHlsC(outDir);
      console.log(`Wrote ${written.length} file(s):`);
      written.forEach((f) => console.log(`  ${f}`));
    } else {
      console.log('Measuring abstention/false-rate metrics against each kernel\'s real test suite (this runs `node --test` per kernel; may take a moment)...');
      const dest = exportSpecJson(outDir);
      console.log(`Wrote ${dest}`);
    }
    return;
  }

  if (cmd === 'serve') {
    // Upgrade 8: exposes every registered kernel over TLS (see the
    // header comment above). Runs until interrupted (Ctrl+C / SIGTERM)
    // — main() below does not resolve, and shutdownPool() does not
    // fire, until the server has actually stopped.
    const portIdx = process.argv.indexOf('--port');
    const port = portIdx === -1 ? NaN : Number(process.argv[portIdx + 1]);
    if (!Number.isInteger(port) || port <= 0 || port > 65535) {
      console.error('usage: ciall serve --port <n>   (needs CIALL_TLS_CERT_PATH and CIALL_TLS_KEY_PATH set in your environment)');
      process.exitCode = 1;
      return;
    }
    const certPath = process.env.CIALL_TLS_CERT_PATH;
    const keyPath = process.env.CIALL_TLS_KEY_PATH;
    if (!certPath || !keyPath) {
      console.error('ciall serve needs CIALL_TLS_CERT_PATH and CIALL_TLS_KEY_PATH set in your environment (a self-signed pair is fine for local-network use; this command does not generate one for you).');
      process.exitCode = 1;
      return;
    }

    const { close, port: boundPort } = await startKernelServiceServer({ port, certPath, keyPath });
    console.log(`ciall serve: KernelService listening on :${boundPort} (TLS). Every registered kernel is reachable; Ctrl+C to stop.`);

    await new Promise((resolve) => {
      const stop = async () => {
        console.log('\nciall serve: shutting down...');
        await close();
        resolve();
      };
      process.once('SIGINT', stop);
      process.once('SIGTERM', stop);
    });
    return;
  }

  if (cmd === 'sat-proof-demo') {
    // Upgrade 16: a real, independently-verified DRAT proof export.
    // Runs the Erdos discrepancy non-existence claim (already used in
    // tests/sat.test.mjs) at a chosen length, gets a REAL proof from
    // the solver, independently re-checks it with dratProof.js's
    // separate RUP checker, then writes the standard DIMACS-DRAT file.
    // NOT run through the external `drat-trim` reference tool in this
    // environment (no C compiler/internet access here, same disclosed
    // limitation as certifiable-c/) -- writing the file so a user WITH
    // drat-trim installed can independently re-verify it themselves.
    const { normalizeCombinatorialSpec, executeCombinatorialSearch } = await import('../src/lib/combinatorialSearch.js');
    const { writeDratFile } = await import('../src/lib/dratProof.js');
    const nIdx = process.argv.indexOf('--n');
    const n = nIdx === -1 ? 12 : Number(process.argv[nIdx + 1]);
    if (!Number.isInteger(n) || n < 2) {
      console.error('usage: ciall sat-proof-demo [--n <length>] [--out <path>]   (default n=12; the Erdos discrepancy-1 non-existence claim)');
      process.exitCode = 1;
      return;
    }
    const outIdx = process.argv.indexOf('--out');
    const outPath = outIdx === -1 ? `discrepancy-${n}.drat` : process.argv[outIdx + 1];

    const constraints = [];
    for (let d = 1; d <= n; d++) {
      const hap = [];
      for (let k = 1; k * d <= n; k++) hap.push(k * d);
      if (hap.length > 1) constraints.push({ type: 'boundedRunningSum', lits: hap, C: 1 });
    }
    const spec = normalizeCombinatorialSpec({ kind: 'combinatorial_search', n, claim: 'not-exists', meaning: 'variable i true means f(i)=+1', constraints });
    console.log(`Solving: does a +/-1 sequence of length ${n} exist with discrepancy <=1? (emitting a real DRAT proof if not)`);
    const result = executeCombinatorialSearch(spec, { emitProof: true });
    console.log(`Verdict: ${result.verdict}`);
    if (!result.proof) {
      console.log('No proof to export (either satisfiable, or the search did not settle).');
      return;
    }
    console.log(`Independently verified by dratProof.js: ${result.proofIndependentlyVerified}`);
    const fs = await import('node:fs');
    fs.writeFileSync(outPath, writeDratFile(result.proof));
    console.log(`Wrote ${result.proof.length}-line DRAT proof to ${outPath}`);
    console.log('This has NOT been run through the external drat-trim reference tool (unavailable in this environment) -- if you have it installed, verify independently with your own DIMACS CNF for this instance.');
    return;
  }

  if (cmd === 'self-audit') {
    // Upgrade 15: see src/lib/selfAudit.js's header for the full
    // honesty disclosure -- this flags disclosed red-flag patterns for
    // human review, it does not detect all malware and never modifies
    // any file. No confirm() gate here since this is read-only analysis.
    // --record-baseline [path] / --check-baseline <path> (upgrade 15
    // extension): a real SHA-256 file-hash manifest, diffed against a
    // future scan -- see recordBaseline/checkAgainstBaseline's own docs.
    const { runSelfAudit, recordBaseline, checkAgainstBaseline } = await import('../src/lib/selfAudit.js');
    const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
    const fs = await import('node:fs');
    const defaultBaselinePath = path.join(projectRoot, '.ciall-selfaudit-baseline.json');

    const recordIdx = process.argv.indexOf('--record-baseline');
    const checkIdx = process.argv.indexOf('--check-baseline');
    if (recordIdx !== -1 && checkIdx !== -1) {
      console.error('use either --record-baseline or --check-baseline, not both');
      process.exitCode = 1;
      return;
    }
    if (recordIdx !== -1) {
      const outPath = process.argv[recordIdx + 1] && !process.argv[recordIdx + 1].startsWith('--') ? process.argv[recordIdx + 1] : defaultBaselinePath;
      const baseline = recordBaseline(projectRoot);
      fs.writeFileSync(outPath, JSON.stringify(baseline, null, 2));
      console.log(`Recorded baseline of ${Object.keys(baseline.files).length} file hashes to ${outPath}`);
      return;
    }
    if (checkIdx !== -1) {
      const inPath = process.argv[checkIdx + 1] || defaultBaselinePath;
      if (!fs.existsSync(inPath)) {
        console.error(`no baseline found at ${inPath} -- run "ciall self-audit --record-baseline" first`);
        process.exitCode = 1;
        return;
      }
      const baseline = JSON.parse(fs.readFileSync(inPath, 'utf8'));
      const diff = checkAgainstBaseline(projectRoot, baseline);
      console.log(`Baseline recorded ${new Date(diff.baselineRecordedAt).toISOString()}; ${diff.unchanged} file(s) unchanged.`);
      if (diff.clean) {
        console.log('No changes since baseline.');
      } else {
        if (diff.added.length) console.log(`Added: ${diff.added.join(', ')}`);
        if (diff.removed.length) console.log(`Removed: ${diff.removed.join(', ')}`);
        if (diff.modified.length) console.log(`Modified: ${diff.modified.join(', ')}`);
      }
      return;
    }

    const result = runSelfAudit(projectRoot);
    console.log(`Scanned ${result.filesScanned} files.`);
    if (result.clean) {
      console.log('No findings.');
    } else {
      console.log(`${result.findings.length} finding(s):`);
      for (const f of result.findings) console.log(`  [${f.severity}] ${f.code} (${f.file}): ${f.detail}`);
    }
    console.log('');
    console.log(result.honesty);
    return;
  }

  if (cmd === 'shift-left-scan') {
    // src/lib/shiftLeftScan.js: selfAudit.js's real pattern findings,
    // mapped through a small disclosed table into chainKernel.js's
    // SAT+DRAT-proven compositional reachability -- proactive, before
    // any CVE exists, zero model calls. Read-only, same as self-audit;
    // no confirm() gate needed. See that file's own header for the
    // mapping table and its disclosed scope.
    const { scanForProactiveChains } = await import('../src/lib/shiftLeftScan.js');
    const targetDir = process.argv[3] && !process.argv[3].startsWith('--') ? process.argv[3] : path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
    const result = scanForProactiveChains(targetDir);

    console.log(`Scanned ${result.filesScanned} file(s), ${result.findingsScanned} selfAudit finding(s), ${result.chainEligibleCount} chain-eligible.`);
    if (result.note) {
      console.log(result.note);
    } else {
      console.log('');
      console.log(`${result.proven.length} PROVEN compositional chain(s) (2+ distinct findings, SAT+DRAT-verified):`);
      for (const c of result.proven) {
        console.log(`  [${c.severity}] ${c.target}`);
        console.log(`    causal ancestry: ${c.causalAncestry.join(' -> ')}`);
        console.log(`    ${c.severityHonesty}`);
      }
      console.log('');
      console.log(`${result.directlyReachable.length} finding(s) directly reachable on their own (not a chain -- see selfAudit's own finding for these).`);
      console.log(`${result.rejected.length} candidate target(s) proven NOT to compose (independently DRAT-verified where applicable).`);
      if (result.undecided.length) console.log(`${result.undecided.length} candidate target(s) undecided -- not evidence either way.`);
    }
    console.log('');
    console.log('No exploit code, payload, or trigger sequence was generated or is possible from this output -- see shiftLeftScan.js\'s header. A human security researcher decides what, if anything, to do with a proven chain.');
    return;
  }

  if (cmd === 'dashboard') {
    // Upgrade 12: a real, live, server-rendered analyst dashboard (see
    // src/lib/dashboardServer.js's header for scope). Runs until
    // interrupted, same convention as `ciall serve`.
    const portIdx = process.argv.indexOf('--port');
    const port = portIdx === -1 ? 0 : Number(process.argv[portIdx + 1]);
    if (portIdx !== -1 && (!Number.isInteger(port) || port <= 0 || port > 65535)) {
      console.error('usage: ciall dashboard [--port <n>]   (omit --port for an ephemeral free port)');
      process.exitCode = 1;
      return;
    }
    const { close, port: boundPort } = await startDashboardServer({ port });
    console.log(`ciall dashboard: listening on http://127.0.0.1:${boundPort} — Ctrl+C to stop.`);
    await new Promise((resolve) => {
      const stop = async () => { console.log('\nciall dashboard: shutting down...'); await close(); resolve(); };
      process.once('SIGINT', stop);
      process.once('SIGTERM', stop);
    });
    return;
  }

  if (cmd === 'memory') {
    // A persistent, long-term memory layer for this substrate itself —
    // "similar to, not the same as, GBrain": compiled-truth/timeline
    // pages, zero-LLM self-wiring, file-backed (no database, no npm
    // dependency). See src/lib/memory/memoryStore.js's header for the
    // full comparison. Stores under ./ciall-memory/ next to wherever
    // this command is run, same convention as ./ciall-export/.
    const root = defaultMemoryRoot();
    const [sub, ...subRest] = rest;

    if (sub === 'put') {
      const [id, ...textParts] = subRest;
      if (!id || textParts.length === 0) { console.error('usage: ciall memory put <id> <compiledTruth...>'); process.exitCode = 1; return; }
      const page = putPage(root, id, { compiledTruth: textParts.join(' ') });
      console.log(`Wrote page "${page.id}".`);
      return;
    }
    if (sub === 'get') {
      const [id] = subRest;
      if (!id) { console.error('usage: ciall memory get <id>'); process.exitCode = 1; return; }
      const page = getPage(root, id);
      if (!page) { console.log(`No page "${id}".`); return; }
      console.log(JSON.stringify(page, null, 2));
      return;
    }
    if (sub === 'list') {
      for (const id of listPages(root)) console.log(id);
      return;
    }
    if (sub === 'related') {
      const [id] = subRest;
      if (!id) { console.error('usage: ciall memory related <id>'); process.exitCode = 1; return; }
      const pages = listPages(root).map((pid) => getPage(root, pid)).filter(Boolean);
      const graph = buildGraph(pages);
      for (const edge of edgesFor(graph, id)) console.log(`${edge.type}  ${edge.other}  (via ${edge.via})`);
      return;
    }
    if (sub === 'contradictions') {
      const findings = findContradictingMemories(root);
      if (findings.length === 0) { console.log('No contradictions found across recorded memory.'); return; }
      for (const f of findings) console.log(`${f.about}: ${f.sources.join(' vs ')}`);
      return;
    }
    console.log('Usage: ciall memory <put|get|list|related|contradictions> ...');
    process.exitCode = 1;
    return;
  }

  if (cmd === 'timing-variance') {
    // Upgrade 11: honest instrumentation for the "deterministic OUTPUT is
    // not deterministic TIMING" gap named in CERTIFICATION-GAPS.md. Runs
    // a REAL registered kernel's run(spec) repeatedly and reports actual
    // measured wall-clock variance -- not a bound, not a proof, a
    // measurement. `spec` is the already-NORMALIZED JSON a model would
    // otherwise have produced (this command skips extraction on purpose,
    // so it measures kernel execution time alone, not model-call latency).
    const [kernelId, specJson, repsArg] = rest;
    if (!kernelId || !specJson) {
      console.error('usage: ciall timing-variance <kernelId> \'<normalized spec JSON>\' [reps]');
      console.error('example: ciall timing-variance boundary-check \'{"kind":"path-containment","target":"/sandbox/x","boundary":"/sandbox"}\' 50');
      process.exitCode = 1;
      return;
    }
    const kernel = getKernel(kernelId);
    if (!kernel) {
      console.error(`unknown kernel "${kernelId}". Known: ${listKernels().map((k) => k.id).join(', ')}`);
      process.exitCode = 1;
      return;
    }
    let spec;
    try {
      spec = kernel.normalize(JSON.parse(specJson));
    } catch (e) {
      console.error(`spec failed normalize() for kernel "${kernelId}": ${e.message}`);
      process.exitCode = 1;
      return;
    }
    const runs = repsArg ? Number(repsArg) : 30;
    if (!Number.isInteger(runs) || runs < 2) {
      console.error(`reps must be an integer >= 2, got "${repsArg}"`);
      process.exitCode = 1;
      return;
    }

    // kernel.run() may return a Promise (the mcmc kernel's smc mode) or a
    // plain value (every other kernel) -- see kernelRegistry.js's header.
    // Awaiting a non-Promise is a no-op, so this one path covers both,
    // same convention orchestrator.js already relies on.
    const stats = await measureTimingVarianceAsync(async () => { await kernel.run(spec); }, { runs });

    console.log(`Kernel: ${kernelId}`);
    console.log(`Runs: ${stats.runs} (after ${stats.warmupRuns} discarded warmup runs)`);
    console.log(`Mean:   ${stats.meanMs.toFixed(4)}ms`);
    console.log(`Min:    ${stats.minMs.toFixed(4)}ms`);
    console.log(`Max:    ${stats.maxMs.toFixed(4)}ms`);
    console.log(`Stddev: ${stats.stddevMs.toFixed(4)}ms`);
    console.log(`Coefficient of variation: ${(stats.coefficientOfVariation * 100).toFixed(2)}%`);
    console.log(`Spread ratio (max/min): ${Number.isFinite(stats.spreadRatio) ? stats.spreadRatio.toFixed(2) + 'x' : 'undefined (min was ~0ms)'}`);
    console.log('');
    console.log(stats.honesty);
    return;
  }

  if (cmd === 'physical-action-check') {
    // Upgrade 11: the pre-actuation simulation gate for physical systems
    // (see src/lib/physicalActionGate.js's header for the full honesty
    // disclosure -- there is NO physical executor behind this; this
    // command simulates the proposed action, lists every concrete
    // failure mode found, and asks the same y/N confirm() every other
    // action in this CLI asks, informed by those findings.
    const [kind, specJson] = rest;
    if (!kind || !specJson) {
      console.error('usage: ciall physical-action-check <camera-capture|actuator-move|sensor-read> \'<spec JSON>\'');
      console.error('example: ciall physical-action-check actuator-move \'{"currentPosition":0,"targetPosition":90,"maxVelocity":10,"maxAcceleration":1,"positionLimits":[0,100]}\'');
      process.exitCode = 1;
      return;
    }
    let raw;
    try {
      raw = { kind, ...JSON.parse(specJson) };
    } catch (e) {
      console.error(`spec is not valid JSON: ${e.message}`);
      process.exitCode = 1;
      return;
    }
    try {
      const result = await gatePhysicalAction(raw, { confirm: terminalConfirm, requestedBy: 'cli' });
      console.log('');
      console.log(`Simulation: ${result.safe ? 'no failure modes found' : `${result.failureModes.length} failure mode(s) found (highest severity: ${result.highestSeverity})`}`);
      for (const f of result.failureModes) console.log(`  [${f.severity}] ${f.code}: ${f.detail}`);
      console.log(result.approved ? 'Approved.' : `Not approved: ${result.reason}`);
    } catch (e) {
      console.error(`Refused: ${e.message}`);
      process.exitCode = 1;
    }
    return;
  }

  if (cmd === 'verify-iterative') {
    // Read-only: this command only ever calls the model and the pure
    // kernel substrate. It CANNOT write a file or run a command itself
    // -- if a round proposes one, this prints it and stops; no
    // confirm() gate is needed here because nothing here ever reaches
    // one. Use `ciall write ... --check`/`--live` afterward, with a
    // real confirm(), if you actually want to act on what this found.
    let provider = 'gemini';
    let maxRounds = DEFAULT_MAX_ROUNDS;
    let kernels = null;
    const textParts = [];
    for (let i = 0; i < rest.length; i++) {
      if (rest[i] === '--provider') { provider = rest[++i] || provider; }
      else if (rest[i] === '--max-rounds') { maxRounds = Number(rest[++i]); }
      else if (rest[i] === '--kernels') { kernels = (rest[++i] || '').split(',').map((s) => s.trim()).filter(Boolean); }
      else textParts.push(rest[i]);
    }
    const claimText = textParts.join(' ');
    if (!claimText) {
      console.error('usage: ciall verify-iterative "<claim text>" [--kernels k1,k2] [--max-rounds N] [--provider gemini|anthropic]');
      process.exitCode = 1;
      return;
    }
    const apiKey = resolveApiKey(provider);
    if (!apiKey) { process.exitCode = 1; return; }
    if (!Number.isInteger(maxRounds) || maxRounds < 1) {
      console.error(`--max-rounds must be a positive integer (max ${HARD_MAX_ROUNDS})`);
      process.exitCode = 1;
      return;
    }

    const model = makeLiveIterativeModel(apiKey, { provider });
    const result = await runIterativeVerification({ text: claimText }, {
      model,
      maxRounds,
      ...(kernels ? { candidateKernelIds: kernels } : {}),
    });

    console.log('');
    console.log(`CLAIM: ${claimText}`);
    console.log('');
    for (const r of result.rounds) {
      console.log(`Round ${r.round} [${r.action}]${r.kernelId ? ` kernel=${r.kernelId}` : ''}`);
      if (r.reflection?.rationale) console.log(`  reasoning: ${r.reflection.rationale}`);
      if (r.declined) console.log(`  declined: ${r.declined}`);
      if (r.failed) console.log(`  failed: ${r.failed}`);
      if (r.redFlags) for (const f of r.redFlags) console.log(`  RED FLAG [${f.severity}] ${f.code}: ${f.detail}`);
      if (r.result != null) console.log(`  verdict=${r.verdict}  result=${JSON.stringify(r.result)}`);
    }
    console.log('');
    console.log(`VERDICT: ${result.verdict}`);
    if (result.verdict === 'awaiting-human-action') {
      console.log('The model proposed a real action below. NOTHING was executed -- this command never writes a file or runs a command itself.');
      console.log(JSON.stringify(result.proposedAction, null, 2));
      console.log('Use `ciall write <path> <content> --check ...` (or your own gated action) if you actually want to do this, with a real confirm() prompt.');
    } else if (result.verdict === 'terminated-red-flag') {
      console.log('A model response tripped selfAudit.js\'s red-flag rules (see above) -- stopped for human review, not continued.');
    } else if (result.verdict === 'unconfirmed-max-rounds') {
      console.log(`Reached the ${maxRounds}-round cap without the model accepting a backed result. This is NOT a finding either way.`);
    }
    return;
  }

  console.log('Usage: ciall <write|read|run|domains|ontology|stream-motion|export-kernels|serve|dashboard|memory|timing-variance|physical-action-check|self-audit|shift-left-scan|sat-proof-demo|audit|verify-iterative> ...');
  console.log('Every write/read/run asks for an explicit y before doing anything. Nothing here runs on its own.');
  process.exitCode = 1;
}

// writeFile's --check/--live verify path runs claims through
// orchestrator.runPipeline (parallel:true by default since upgrade 1),
// which starts kernelWorkerPool.js's worker pool and keeps it alive for
// the process lifetime by design. This IS the process's whole lifetime —
// a single CLI invocation does one thing and exits — so it must be shut
// down explicitly once main() is done, or every `write --check`/`--live`
// invocation would hang instead of returning control to the terminal.
// A no-op if the pool was never started (read/run/audit/domains/etc.).
main().finally(() => shutdownPool());
