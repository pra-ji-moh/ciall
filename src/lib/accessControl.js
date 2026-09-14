// accessControl.js; upgrade 12 — the missing governance/access-control
// layer named against Palantir Foundry's four-layer permission model
// (what you see, what you can query, what you can do, who has
// permission), spanning a real multi-tenant organization.
// `deviceGate.js` (unmodified by this file) is a single-user,
// single-machine sandbox grant system with no identity or tenancy
// concept at all — this is additive, sitting IN FRONT of deviceGate's
// scope check, not a replacement for it.
//
// WHAT THIS IS. Real users, real roles (named bundles of permission
// strings), real tenants, and a `checkPermission` call that enforces
// STRICT tenant isolation: a user belonging to tenant T can never be
// granted an action against a resource declared to belong to a
// different tenant, regardless of what role they hold. This is a
// deliberate simplification versus Foundry's actual model (no
// cross-tenant "platform admin" bypass exists here at all) — safer to
// reason about and to test exhaustively than a bypass path would be,
// and disclosed as a real scope choice, not an oversight.
//
// WHAT THIS IS NOT. Not a directory service, not SSO/OIDC, not
// row-level data governance over a real data estate (there is no real
// data estate — see connectors/), not a UI. A real enterprise
// deployment would need identity federation (SAML/OIDC) in front of
// this; this file is the authorization DECISION engine, the same
// relationship kernelRegistry.js's kernels have to whatever produces
// the claim they check.

const tenants = new Map(); // tenantId -> { id, name, createdAt }
const users = new Map(); // userId -> { id, tenantId, displayName, createdAt }
const roles = new Map(); // roleName -> Set<permission string>
const userRoles = new Map(); // userId -> Set<roleName>
const auditLog = [];

function requireTenant(tenantId) {
  if (!tenants.has(tenantId)) throw new Error(`Unknown tenant "${tenantId}"`);
}

export function createTenant(id, { name } = {}) {
  if (typeof id !== 'string' || !id) throw new Error('createTenant needs a non-empty string id');
  if (tenants.has(id)) throw new Error(`Duplicate tenant "${id}"`);
  const record = { id, name: name || id, createdAt: Date.now() };
  tenants.set(id, record);
  auditLog.push({ type: 'tenant-created', tenantId: id, at: record.createdAt });
  return record;
}

export function createUser(id, { tenantId, displayName } = {}) {
  if (typeof id !== 'string' || !id) throw new Error('createUser needs a non-empty string id');
  if (users.has(id)) throw new Error(`Duplicate user "${id}"`);
  requireTenant(tenantId);
  const record = { id, tenantId, displayName: displayName || id, createdAt: Date.now() };
  users.set(id, record);
  userRoles.set(id, new Set());
  auditLog.push({ type: 'user-created', userId: id, tenantId, at: record.createdAt });
  return record;
}

/**
 * Defines a role as a named bundle of permission strings (caller's own
 * vocabulary, e.g. 'kernel:run', 'device:write', 'connector:read' —
 * this file does not prescribe the permission namespace, matching
 * kernelRegistry.js's own stance of not prescribing claim domains).
 */
export function defineRole(name, permissions) {
  if (typeof name !== 'string' || !name) throw new Error('defineRole needs a non-empty string name');
  if (!Array.isArray(permissions) || permissions.some((p) => typeof p !== 'string')) {
    throw new Error('defineRole needs an array of permission strings');
  }
  if (roles.has(name)) throw new Error(`Duplicate role "${name}"`);
  roles.set(name, new Set(permissions));
  auditLog.push({ type: 'role-defined', role: name, permissions: [...permissions], at: Date.now() });
}

export function assignRole(userId, roleName) {
  if (!users.has(userId)) throw new Error(`Unknown user "${userId}"`);
  if (!roles.has(roleName)) throw new Error(`Unknown role "${roleName}"`);
  userRoles.get(userId).add(roleName);
  auditLog.push({ type: 'role-assigned', userId, role: roleName, at: Date.now() });
}

export function revokeRole(userId, roleName) {
  if (!users.has(userId)) throw new Error(`Unknown user "${userId}"`);
  const had = userRoles.get(userId)?.delete(roleName) ?? false;
  auditLog.push({ type: 'role-revoked', userId, role: roleName, existed: had, at: Date.now() });
  return had;
}

function permissionsFor(userId) {
  const rolesHeld = userRoles.get(userId) || new Set();
  const perms = new Set();
  for (const roleName of rolesHeld) {
    for (const p of (roles.get(roleName) || new Set())) perms.add(p);
  }
  return perms;
}

/**
 * `{userId, action, resourceTenantId}` -> `{allowed, reason}`. Every
 * call is recorded, approved or denied, same "log every decision, not
 * just approvals" discipline as deviceGate.js's requestAction(). Tenant
 * isolation is checked FIRST and unconditionally: a permission grant
 * from a role can never override it, by construction (there is no code
 * path here that checks permissions before tenant match).
 */
export function checkPermission({ userId, action, resourceTenantId }) {
  const user = users.get(userId);
  const decision = (() => {
    if (!user) return { allowed: false, reason: `unknown user "${userId}"` };
    if (typeof action !== 'string' || !action) return { allowed: false, reason: 'action must be a non-empty string' };
    if (!tenants.has(resourceTenantId)) return { allowed: false, reason: `unknown resource tenant "${resourceTenantId}"` };
    if (user.tenantId !== resourceTenantId) {
      return { allowed: false, reason: `user "${userId}" belongs to tenant "${user.tenantId}", not "${resourceTenantId}" -- cross-tenant access is never granted, regardless of role` };
    }
    const perms = permissionsFor(userId);
    if (!perms.has(action)) {
      return { allowed: false, reason: `user "${userId}" holds no role granting action "${action}"` };
    }
    return { allowed: true, reason: `granted via a role held by "${userId}"` };
  })();

  auditLog.push({ type: 'permission-checked', userId, action, resourceTenantId, ...decision, at: Date.now() });
  return decision;
}

// --- upgrade 13: the four layers named explicitly ------------------
//
// checkPermission above is the one tenant-isolated decision ENGINE;
// these three functions are thin, explicitly-named layers over it
// (permission strings namespaced by layer: 'see:', 'query:', 'do:'),
// matching Palantir's own stated four-layer model precisely enough to
// test against it directly: WHAT YOU SEE, WHAT YOU CAN QUERY, WHAT YOU
// CAN DO, and WHO'S ALLOWED (the role/tenant assignment machinery
// above, already the 4th layer — not duplicated here). Namespacing by
// layer means a role granting 'do:kernel:run' does NOT also grant
// 'query:kernel:run' — SEEING an object type, QUERYING it with a
// kernel, and taking a DEVICE ACTION on it are three independent
// grants, on purpose: a real analyst role in an enterprise system is
// routinely allowed to see data it cannot act on.

/** Layer 1 — WHAT YOU CAN SEE: is `objectTypeName` visible to this user for this tenant's data? */
export function checkVisibility({ userId, objectTypeName, resourceTenantId }) {
  return checkPermission({ userId, action: `see:${objectTypeName}`, resourceTenantId });
}

/** Layer 2 — WHAT YOU CAN QUERY: may this user run `kernelId` against this tenant's data? */
export function checkQuery({ userId, kernelId, resourceTenantId }) {
  return checkPermission({ userId, action: `query:${kernelId}`, resourceTenantId });
}

/** Layer 3 — WHAT YOU CAN DO: may this user take `actionName` (a device/connector/write action) against this tenant's data? */
export function checkAction({ userId, actionName, resourceTenantId }) {
  return checkPermission({ userId, action: `do:${actionName}`, resourceTenantId });
}

/**
 * Convenience for defining a role that grants the same capability
 * across one or more layers at once (e.g. an "analyst" role that can
 * SEE and QUERY RiskMark but not DO anything to it). Just a thin
 * builder over defineRole's existing permission-string format — no new
 * storage, no new enforcement path.
 */
export function defineLayeredRole(name, { see = [], query = [], do: doActions = [] } = {}) {
  const permissions = [
    ...see.map((t) => `see:${t}`),
    ...query.map((k) => `query:${k}`),
    ...doActions.map((a) => `do:${a}`),
  ];
  defineRole(name, permissions);
}

export function getAuditLog() {
  return [...auditLog];
}

export function listTenants() {
  return [...tenants.values()];
}

export function listUsers(tenantId) {
  const all = [...users.values()];
  return tenantId ? all.filter((u) => u.tenantId === tenantId) : all;
}

export function getUserRoles(userId) {
  if (!users.has(userId)) throw new Error(`Unknown user "${userId}"`);
  return [...(userRoles.get(userId) || [])];
}
