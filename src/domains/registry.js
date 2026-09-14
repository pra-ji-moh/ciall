// domains/registry.js; the same pattern as kernelRegistry.js, one level
// up. Kernels register into kernelRegistry so a new CLAIM domain is a
// registry entry, not a bespoke pipeline (see VISION-PLTR.md section 2).
// This file does the identical thing for VERTICALS: finance,
// engineering, requirements are now discoverable and dispatchable
// through one registry, not three files someone has to already know
// exist and import by hand.

import * as finance from './finance.js';
import * as engineering from './engineering.js';
import * as requirements from './requirements.js';
import * as supplyChain from './supplyChain.js';
import * as missionPlanning from './missionPlanning.js';
import * as legal from './legal.js';
import * as motion from './motion.js';
import * as camera from './camera.js';

const REGISTRY = new Map();

function register(entry) {
  if (REGISTRY.has(entry.id)) throw new Error(`Duplicate domain id "${entry.id}"`);
  REGISTRY.set(entry.id, Object.freeze(entry));
}

register({
  id: 'finance',
  label: 'Quant risk / finance',
  description: 'Risk-mark reconciliation, margin stress-testing, risk-ranking consistency.',
  module: finance,
});

register({
  id: 'engineering',
  label: 'Structural / design engineering',
  description: 'Design-margin stress-testing, dimensional-balance checking.',
  module: engineering,
});

register({
  id: 'requirements',
  label: 'Program requirements consistency',
  description: 'Catches contradictions accumulated across a program\'s requirement set over time.',
  module: requirements,
});

register({
  id: 'supply-chain',
  label: 'Supply chain resilience',
  description: 'Resilience stress-testing across supplier-failure combinations; supplier-priority ranking consistency.',
  module: supplyChain,
});

register({
  id: 'mission-planning',
  label: 'Mission / operational planning',
  description: 'Turns a scenario-level plan failure into a claim-level, falsifiable operational-envelope check.',
  module: missionPlanning,
});

register({
  id: 'legal',
  label: 'Contract / clause consistency',
  description: 'Catches contradictory obligations across a contract\'s clauses.',
  module: legal,
});

register({
  id: 'motion',
  label: 'Motion / trajectory verification',
  description: 'Detects whether an actual trajectory diverges from a claimed reference motion model (not video/camera motion detection — see motion.js header).',
  module: motion,
});

register({
  id: 'camera',
  label: 'Camera capture (real, confirm-gated)',
  description: 'Captures a still frame via ffmpeg through the existing confirm-gated command executor, and verifies claims about the captured image (e.g. resolution) via dependency-free JPEG parsing.',
  module: camera,
});

export function listDomains() {
  return [...REGISTRY.values()];
}

export function getDomain(id) {
  const entry = REGISTRY.get(id);
  if (!entry) throw new Error(`Unknown domain id "${id}"`);
  return entry;
}
