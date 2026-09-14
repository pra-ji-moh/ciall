// deviceExecutor.js; Phase 4 — the real executor, finally wired.
//
// This is the ONLY file in this repo that imports `node:fs` to touch a
// real path. Everything before this (deviceGate.js) was a decision
// engine with nothing to execute; this file is the thing that executes,
// and it exists specifically because those three blocking questions got
// real answers:
//
//   1. Which directory — a new, separate sandbox folder
//      (sandboxConfig.js), never this project's own folder and never
//      anything broader. Every path here is resolved against that one
//      directory and REFUSED if it would land outside it, symlinks
//      included (see resolveInSandbox below).
//   2. Consent flow — ask every single action. There is no "grant once,
//      write many times" here: every call requires its own `confirm`
//      function, and that function is invoked fresh for THIS action
//      every time, with no way to skip it. The persisted sandbox
//      boundary from sandboxConfig.js says WHERE an action could ever be
//      allowed; it never says an action IS allowed. Only confirm() says
//      that, per call.
//   3. Persistence — the boundary (the sandbox directory itself)
//      persists across sessions via sandboxConfig.js. The execution
//      audit log also persists (appended to `.ciall-execution-audit.jsonl`
//      in this project's root), so what Ciall has actually done to the
//      sandbox survives a restart and can be inspected later. No write
//      APPROVAL persists; only the record that one happened.
//
// Every call, whether it executes or is refused at any stage, is
// recorded via deviceGate's requestAction() (the scope/audit layer) AND
// appended to the execution-specific audit log below, so "was this
// approved" and "did this actually touch disk" are both answerable from
// logs alone.
//
// writeFile ALSO accepts an optional `verify` option — the missing piece
// this file originally left open: scope was checked through the shared
// kernel substrate (deviceGate -> boundary-check), but a write's CONTENT
// was never run through the claim-verification kernels at all. When
// `verify` is given (the same `{kernelIds, designSpec}` shape
// orchestrator.runPipeline already takes), the content is run through
// the substrate BEFORE confirm() is asked, and the result is attached to
// the action confirm() sees. This does not auto-block on a violation —
// consistencyKernel.js's own stance is "which one to drop is your call,
// not the tool's," and that holds here too: the kernel surfaces what it
// found, the human still decides. This substrate still owns no model
// client, so `designSpec` for a model-driven kernel (consistency, mcmc,
// ...) has to come from the caller; a deterministic kernel like
// numeric-check needs no model at all and works with `verify` as-is (see
// bin/ciall.mjs's `write --check` for a working, model-free example).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ensureSandboxExists, getSandboxPath } from './sandboxConfig.js';
import { requestAction, grant } from './deviceGate.js';
import { runPipeline } from './orchestrator.js';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '..', '..'); // src/lib -> project root
// Overridable for tests, so a test run's entries never land in the real
// persisted audit trail.
const AUDIT_LOG_PATH = process.env.CIALL_AUDIT_LOG_PATH
  ? path.resolve(process.env.CIALL_AUDIT_LOG_PATH)
  : path.join(PROJECT_ROOT, '.ciall-execution-audit.jsonl');

const FAR_FUTURE = () => Date.now() + 1000 * 60 * 60 * 24 * 365 * 50; // ~50 years: represents "persisted", not "unbounded in principle"
let boundaryRegistered = false;

// No cap existed here before — a caller (or a bug upstream feeding this
// function) could write content of unbounded size into the sandbox with
// nothing to stop it: a real disk-fill/DoS surface for what is supposed
// to be a small, reviewed sandbox, not a place arbitrary volumes of data
// land. 10MB is generous for what this sandbox is for (claims, notes,
// small artifacts) and rejects before confirm() is even asked, since an
// oversized write isn't a decision for a human to review — it's already
// out of scope regardless of what they'd say.
export const MAX_CONTENT_BYTES = 10 * 1024 * 1024;

// Registers a deviceGate grant matching the persisted sandbox boundary,
// once. This is what lets requestAction()'s generic scope check
// recognize the sandbox as eligible at all — it is NOT a write
// authorization by itself; writeFile/readFile below still require their
// own confirm() on every single call regardless of this grant existing.
function ensureBoundaryGrant() {
  if (boundaryRegistered) return;
  const sandboxPath = ensureSandboxExists();
  for (const scope of ['filesystem-write', 'filesystem-read']) {
    try {
      grant({ id: `sandbox-boundary-${scope}`, scope, boundary: sandboxPath, expiresAt: FAR_FUTURE(), grantedBy: 'sandboxConfig (persisted boundary, not a write authorization)' });
    } catch (e) {
      if (!/Duplicate/.test(e.message)) throw e;
    }
  }
  boundaryRegistered = true;
}

function appendExecutionAudit(entry) {
  fs.mkdirSync(path.dirname(AUDIT_LOG_PATH), { recursive: true });
  fs.appendFileSync(AUDIT_LOG_PATH, JSON.stringify({ ...entry, at: Date.now() }) + '\n', 'utf8');
}

export function getExecutionAuditPath() {
  return AUDIT_LOG_PATH;
}

export function readExecutionAudit() {
  if (!fs.existsSync(AUDIT_LOG_PATH)) return [];
  return fs.readFileSync(AUDIT_LOG_PATH, 'utf8').split('\n').filter(Boolean).map((l) => JSON.parse(l));
}

// Resolves relPath against the sandbox root and refuses anything that
// would land outside it — including through a symlink. path.resolve
// alone only catches ".." traversal on paper; it does not see through a
// symlink whose target points outside the sandbox. realpathSync on the
// deepest EXISTING ancestor closes that gap: a symlinked ancestor
// directory resolves to its real, possibly-outside-the-sandbox location,
// and that's what gets checked, not the pre-resolution path string.
function resolveInSandbox(relPath) {
  if (typeof relPath !== 'string' || !relPath || path.isAbsolute(relPath)) {
    throw new Error(`relPath must be a non-empty relative path; refusing "${relPath}"`);
  }
  const sandboxPath = getSandboxPath();
  const realSandbox = fs.realpathSync(ensureSandboxExists());
  const target = path.resolve(sandboxPath, relPath);

  let ancestor = target;
  while (!fs.existsSync(ancestor)) {
    const parent = path.dirname(ancestor);
    if (parent === ancestor) break; // reached filesystem root without finding anything real
    ancestor = parent;
  }
  const realAncestor = fs.existsSync(ancestor) ? fs.realpathSync(ancestor) : ancestor;
  const realTargetPrefix = target.startsWith(ancestor) ? target.replace(ancestor, realAncestor) : target;

  if (realTargetPrefix !== realSandbox && !realTargetPrefix.startsWith(realSandbox + path.sep)) {
    throw new Error(`"${relPath}" resolves outside the sandbox (${realSandbox}); refused`);
  }
  return path.resolve(sandboxPath, relPath);
}

/**
 * Writes `content` to `relPath` inside the sandbox. Requires `confirm`,
 * an (optionally async) function called with the exact action being
 * proposed; it must return/resolve `true` for this specific call, every
 * time — there is no grant that substitutes for it.
 *
 * Optional `verify: { kernelIds, designSpec }` (same shape as
 * orchestrator.runPipeline) runs BEFORE confirm() and its report is
 * attached to the action confirm() receives as `action.verification`.
 * Does not auto-deny on a violation — confirm() still makes the actual
 * call, now with the kernel findings in front of it instead of blind.
 */
export async function writeFile(relPath, content, { confirm, requestedBy = 'unknown', verify } = {}) {
  if (typeof confirm !== 'function') {
    throw new Error('writeFile requires an explicit confirm(action) function; no write happens without a fresh per-action approval');
  }
  if (typeof content !== 'string') {
    throw new Error(`writeFile content must be a string, got ${typeof content}`);
  }
  const byteLength = Buffer.byteLength(content, 'utf8');
  if (byteLength > MAX_CONTENT_BYTES) {
    appendExecutionAudit({ op: 'write', relPath, executed: false, reason: `content is ${byteLength} bytes, over the ${MAX_CONTENT_BYTES}-byte cap` });
    throw new Error(`content is ${byteLength} bytes, over the ${MAX_CONTENT_BYTES}-byte cap for this sandbox`);
  }
  ensureBoundaryGrant();

  let target;
  try {
    target = resolveInSandbox(relPath);
  } catch (e) {
    appendExecutionAudit({ op: 'write', relPath, executed: false, reason: e.message });
    throw e;
  }

  const scopeDecision = requestAction({ scope: 'filesystem-write', target, requestedBy });
  if (!scopeDecision.approved) {
    appendExecutionAudit({ op: 'write', relPath, target, executed: false, reason: scopeDecision.reason });
    return { executed: false, reason: scopeDecision.reason };
  }

  let verification = null;
  if (verify) {
    const report = await runPipeline({ text: content, relPath }, verify);
    verification = {
      anyViolated: report.anyViolated,
      ran: report.ran.map((r) => ({ kernelId: r.kernelId, verdict: r.verdict })),
      skipped: report.skipped,
      failed: report.failed,
    };
  }

  const action = { op: 'write', target, relPath, contentLength: byteLength, requestedBy, verification };
  const approved = await confirm(action);
  if (!approved) {
    appendExecutionAudit({ ...action, executed: false, reason: 'confirm() declined this specific action' });
    return { executed: false, reason: 'confirm() declined this specific action', verification };
  }

  fs.mkdirSync(path.dirname(target), { recursive: true });
  fs.writeFileSync(target, content, 'utf8');
  appendExecutionAudit({ ...action, executed: true });
  return { executed: true, target, verification };
}

/**
 * Reads `relPath` from inside the sandbox. Same per-call confirm()
 * requirement as writeFile — reading is lower-risk than writing but
 * still surfaces file contents to whatever called this, so it gets the
 * same discipline rather than a quieter path.
 */
export async function readFile(relPath, { confirm, requestedBy = 'unknown' } = {}) {
  if (typeof confirm !== 'function') {
    throw new Error('readFile requires an explicit confirm(action) function; no read happens without a fresh per-action approval');
  }
  ensureBoundaryGrant();

  let target;
  try {
    target = resolveInSandbox(relPath);
  } catch (e) {
    appendExecutionAudit({ op: 'read', relPath, executed: false, reason: e.message });
    throw e;
  }

  const scopeDecision = requestAction({ scope: 'filesystem-read', target, requestedBy });
  if (!scopeDecision.approved) {
    appendExecutionAudit({ op: 'read', relPath, target, executed: false, reason: scopeDecision.reason });
    return { executed: false, reason: scopeDecision.reason };
  }

  const action = { op: 'read', target, relPath, requestedBy };
  const approved = await confirm(action);
  if (!approved) {
    appendExecutionAudit({ ...action, executed: false, reason: 'confirm() declined this specific action' });
    return { executed: false, reason: 'confirm() declined this specific action' };
  }

  if (!fs.existsSync(target)) {
    appendExecutionAudit({ ...action, executed: false, reason: 'file does not exist' });
    return { executed: false, reason: 'file does not exist' };
  }
  const content = fs.readFileSync(target, 'utf8');
  appendExecutionAudit({ ...action, executed: true, contentLength: content.length });
  return { executed: true, target, content };
}
