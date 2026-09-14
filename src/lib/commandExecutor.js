// commandExecutor.js; running a command, with the same discipline as
// deviceExecutor.js's file operations — scoped, confirmed every time,
// audited. This is the second (and, for now, last) real executor in this
// repo alongside deviceExecutor.js.
//
// Runs via execFile, never a shell (`exec`/`spawn(..., {shell:true})`) —
// execFile takes the executable and its argument LIST separately, so
// there is no shell string for a malicious or malformed argument to
// break out of. There is no argument-escaping code here because there is
// no shell parsing anything; the arguments array reaches the OS exactly
// as given, one argv entry per array element.
//
// SCOPE. Unlike the filesystem sandbox (one fixed directory, decided
// once), there is no sensible default allowlist of executables — "which
// commands should ever be runnable" is inherently caller-specific. So
// this file registers NOTHING by default: `runCommand` for an executable
// with no active deviceGate 'process-launch' grant is refused exactly
// like any other out-of-scope action, and the CLI (bin/ciall.mjs) is
// responsible for deciding what, if anything, to grant.

import { execFile } from 'node:child_process';
import { requestAction } from './deviceGate.js';

const TIMEOUT_MS = 30_000;
const MAX_OUTPUT_BYTES = 1_000_000;

/**
 * Runs `executable` with `args` (a plain array of argv strings). Requires
 * `confirm(action)`, invoked fresh for this exact call — same contract as
 * deviceExecutor's writeFile/readFile: no grant, however long-lived,
 * substitutes for a per-call approval.
 */
export async function runCommand(executable, args = [], { confirm, cwd, requestedBy = 'unknown' } = {}) {
  if (typeof confirm !== 'function') {
    throw new Error('runCommand requires an explicit confirm(action) function; no command runs without a fresh per-action approval');
  }
  if (typeof executable !== 'string' || !executable) throw new Error('executable must be a non-empty string');
  if (!Array.isArray(args) || !args.every((a) => typeof a === 'string')) throw new Error('args must be an array of strings');

  const scopeDecision = requestAction({ scope: 'process-launch', target: executable, requestedBy });
  if (!scopeDecision.approved) {
    return { executed: false, reason: scopeDecision.reason };
  }

  const action = { op: 'run', executable, args, cwd: cwd || null, requestedBy };
  const approved = await confirm(action);
  if (!approved) {
    return { executed: false, reason: 'confirm() declined this specific action' };
  }

  return new Promise((resolve) => {
    execFile(executable, args, { cwd, timeout: TIMEOUT_MS, maxBuffer: MAX_OUTPUT_BYTES }, (error, stdout, stderr) => {
      if (error) {
        resolve({ executed: true, exitCode: typeof error.code === 'number' ? error.code : null, timedOut: error.killed && error.signal === 'SIGTERM', stdout, stderr, error: error.message });
        return;
      }
      resolve({ executed: true, exitCode: 0, stdout, stderr });
    });
  });
}
