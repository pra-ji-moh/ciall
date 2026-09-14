// deviceGate.js; Phase 3 of VISION-PLTR.md — the permission/audit
// scaffold for device actions, WITHOUT an execution backend.
//
// READ THIS BEFORE WIRING ANYTHING TO THIS FILE.
//
// This module makes it possible to REQUEST a device action (a file
// write, a process launch, whatever) and get back an approve/deny
// decision plus a full audit trail. It does NOT execute anything. There
// is no fs.writeFile here, no child_process, no browser automation. That
// is deliberate, not an oversight: which directory should be writable,
// what the consent UI looks like, and what exactly gets logged were never
// specified when this was built (see VISION-PLTR.md section 4). Building
// the gate without an executor means the worst thing this file can do
// today is return `{ approved: true }` to a caller who then has nowhere
// to take that decision — there is no code path anywhere in this repo
// that turns an approval into a real filesystem or OS action. Wiring an
// actual executor behind an approved decision is Phase 4, and needs its
// own explicit go-ahead once the scope questions above have real answers.
//
// THE DESIGN, otherwise. Same principle as every kernel in this
// substrate: a proposed action is checked by something structurally
// separate from whatever proposed it. `requestAction` does not trust the
// caller's claim that an action is in-scope; it recomputes scope
// membership itself, deterministically, against the grants actually held
// — the same "generator vs. structurally-separate verifier" pattern
// VISION-PLTR.md names as the thing to carry forward into Ciall's own
// agency, not just the claims it evaluates.
//
// THIS FILE DOES NOT IMPLEMENT ITS OWN SCOPE-MEMBERSHIP CHECK. Scope
// membership (is this target within this grant's boundary) is computed
// by the 'boundary-check' kernel in kernelRegistry.js — the same
// substrate every claim-verification kernel plugs into. An earlier
// version of this file had its own inline pathWithinBoundary()/
// allowlist-includes logic; that was exactly the bespoke-pipeline
// pattern VISION-PLTR.md argues against ("a new domain is a new
// registry entry, not a new call site"), so it was pulled out into
// boundaryKernel.js and registered like any other kernel. This file is
// now a CONSUMER of the shared substrate, not a second implementation of
// the same principle living next to it.
//
// No grants exist until something explicitly calls grant(). An empty
// registry denies everything by construction; there is no "device
// control" active anywhere until a grant is deliberately created, and
// every grant is scoped, not blanket.

import { getKernel } from './kernelRegistry.js';

const grants = new Map(); // id -> grant
const auditLog = [];

/**
 * Grant shape:
 *   {
 *     id            stable string, caller-supplied, unique
 *     scope         'filesystem-read' | 'filesystem-write' | 'process-launch' | 'network'
 *     boundary      what the grant covers, meaning depends on scope:
 *                     filesystem-*: an absolute directory path; actions
 *                       are in-scope only if their target path is that
 *                       directory or a descendant of it (no traversal
 *                       past it)
 *                     process-launch: an array of allowed executable names
 *                     network: an array of allowed hostnames
 *     expiresAt     epoch ms; required, no grant without an expiry
 *     grantedBy     free-text: who/what created this grant, for the audit trail
 *   }
 */
export function grant(g) {
  if (!g || typeof g !== 'object') throw new Error('grant() needs a grant object');
  if (!g.id || typeof g.id !== 'string') throw new Error('grant needs a stable string id');
  if (!['filesystem-read', 'filesystem-write', 'process-launch', 'network'].includes(g.scope)) {
    throw new Error(`Unknown grant scope "${g.scope}"`);
  }
  if (!Number.isFinite(g.expiresAt) || g.expiresAt <= Date.now()) {
    throw new Error('grant needs a future expiresAt (epoch ms); no standing/unexpiring grants');
  }
  if (!g.grantedBy) throw new Error('grant needs grantedBy for the audit trail');
  const record = { ...g, createdAt: Date.now() };
  grants.set(g.id, record);
  auditLog.push({ type: 'grant-created', grantId: g.id, scope: g.scope, boundary: g.boundary, expiresAt: g.expiresAt, grantedBy: g.grantedBy, at: record.createdAt });
  return record;
}

export function revoke(id) {
  const existed = grants.delete(id);
  auditLog.push({ type: 'grant-revoked', grantId: id, existed, at: Date.now() });
  return existed;
}

function activeGrantsFor(scope) {
  const now = Date.now();
  return [...grants.values()].filter((g) => g.scope === scope && g.expiresAt > now);
}

/**
 * Action shape: { scope, target, requestedBy }.
 *   filesystem-*: target is an absolute path
 *   process-launch: target is an executable name
 *   network: target is a hostname
 *
 * Returns { approved, reason, grantId? }. Every call is recorded in the
 * audit log regardless of outcome — a denial is logged exactly as
 * faithfully as an approval, because a caller silently retrying denied
 * actions is exactly the failure mode an audit trail exists to catch.
 */
export function requestAction(action) {
  if (!action || typeof action !== 'object') throw new Error('requestAction needs an action object');
  const { scope, target, requestedBy } = action;

  const candidates = activeGrantsFor(scope);
  const boundaryKernel = getKernel('boundary-check');
  const kind = (scope === 'filesystem-read' || scope === 'filesystem-write') ? 'path-containment' : 'allowlist';
  let matched = null;

  for (const g of candidates) {
    const spec = boundaryKernel.normalize({ kind, target, boundary: g.boundary });
    const result = boundaryKernel.run(spec);
    if (result.verdict === 'held') { matched = g; break; }
  }

  const decision = {
    approved: Boolean(matched),
    reason: matched
      ? `covered by grant "${matched.id}"`
      : candidates.length === 0
        ? `no active grant for scope "${scope}"`
        : `target "${target}" is outside every active grant's boundary for scope "${scope}"`,
    grantId: matched ? matched.id : null,
  };

  auditLog.push({ type: 'action-requested', scope, target, requestedBy: requestedBy || 'unknown', ...decision, at: Date.now() });

  // Deliberately no execution here. See module header. This function's
  // entire contract ends at returning a decision; nothing downstream in
  // this repo turns `approved: true` into a real filesystem or OS call.
  return decision;
}

export function getAuditLog() {
  return [...auditLog]; // copy; callers cannot mutate the real log
}

export function listActiveGrants() {
  const now = Date.now();
  return [...grants.values()].filter((g) => g.expiresAt > now);
}
