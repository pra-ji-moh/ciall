// boundaryKernel.js; scope-membership as a proper kernel, not a bespoke
// checker living inside deviceGate.js.
//
// WHY THIS IS A KERNEL AND NOT JUST A FUNCTION IN deviceGate.js. The
// PLTR-level direction in VISION-PLTR.md commits to one substrate: a new
// domain plugs into kernelRegistry.js as a registry entry, not a new
// bespoke pipeline off to the side. Device-action scoping is exactly the
// same shape as every other kernel here — a deterministic, sound,
// structurally-separate check — just checking scope membership instead
// of a logical/numeric/combinatorial claim. Pulling it out and
// registering it means deviceGate.js becomes a CONSUMER of the shared
// substrate, like any other caller, instead of a second implementation
// of the same principle sitting next to it.
//
// needsModelExtraction is false for this one (see kernelRegistry.js):
// scope membership is fully determined by the action and the grant,
// there is no prose to extract a spec from.

export function normalizeBoundarySpec(raw) {
  if (!raw || typeof raw !== 'object') throw new Error('Boundary spec is not an object');
  const { kind, target, boundary } = raw;
  if (kind !== 'path-containment' && kind !== 'allowlist') throw new Error(`Unknown boundary kind "${kind}"`);
  if (typeof target !== 'string' || !target) throw new Error('Boundary spec needs a non-empty target string');
  if (kind === 'path-containment') {
    if (typeof boundary !== 'string' || !boundary) throw new Error('path-containment needs a non-empty string boundary');
  } else if (!Array.isArray(boundary)) {
    throw new Error('allowlist needs an array boundary');
  }
  return { kind, target, boundary };
}

// Normalizes both paths and checks the target is the boundary or a
// descendant of it. Does not touch the filesystem (no realpath
// resolution of symlinks) — a caller wiring a real executor behind this
// MUST resolve symlinks before acting, since this check alone cannot see
// through them (deviceExecutor.js does this).
function pathWithinBoundary(target, boundary) {
  const norm = (p) => String(p).replace(/\\/g, '/').replace(/\/+$/, '');
  const t = norm(target);
  const b = norm(boundary);
  return t === b || t.startsWith(b + '/');
}

export function checkBoundary(spec) {
  const within = spec.kind === 'path-containment'
    ? pathWithinBoundary(spec.target, spec.boundary)
    : spec.boundary.includes(spec.target);
  return {
    verdict: within ? 'held' : 'violated',
    kind: spec.kind,
    target: spec.target,
    boundary: spec.boundary,
    detail: within
      ? 'target is within the granted boundary'
      : `target is outside the granted boundary${spec.kind === 'path-containment' ? ' (this check does not resolve symlinks; a real executor must realpath before acting)' : ''}`,
  };
}
