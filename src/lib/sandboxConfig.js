// sandboxConfig.js; Phase 4 — the ONE persisted answer to "which
// directory should ever be writable."
//
// Per the explicit decision this was built against: a new, separate
// sandbox directory, not this project's own folder and not anything
// broader. The path is written to `.ciall-sandbox.json` on first use and
// read from there on every subsequent call, so it survives restarts —
// that's the "persist between sessions" answer, and it applies ONLY to
// this boundary decision, not to any write authorization (see
// deviceExecutor.js: every actual write still requires its own
// per-action confirm(), regardless of how long this config has existed).
//
// STALE-CONFIG DEFENSE. Found by actually copying this repo (not
// cloning it) into a fresh location and running it: a raw filesystem
// copy carries `.ciall-sandbox.json` along, since it's a regular file
// `.gitignore` only excludes from GIT-based copies. Without a check, the
// copied install would silently read the OLD absolute path and reuse the
// original machine's sandbox instead of establishing its own — not a
// security hole (still a real, bounded sandbox either way), but a
// portability surprise nobody asked for. Fixed by recording which
// install root wrote the config; a config found under a DIFFERENT
// install root than the one asking is treated as stale and a fresh
// default is computed and persisted for THIS install instead.
//
// CIALL_SANDBOX_PATH overrides the default/persisted path when set;
// CIALL_SANDBOX_CONFIG_PATH overrides where the config file itself
// lives — both exist so tests (and this bug's own regression test) can
// exercise the real logic against an isolated location, never the real
// config file.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROJECT_ROOT = path.resolve(__dirname, '..', '..'); // src/lib -> project root
const CONFIG_PATH = process.env.CIALL_SANDBOX_CONFIG_PATH
  ? path.resolve(process.env.CIALL_SANDBOX_CONFIG_PATH)
  : path.join(PROJECT_ROOT, '.ciall-sandbox.json');
const DEFAULT_SANDBOX = path.resolve(PROJECT_ROOT, '..', 'ciall-sandbox');

function persistFreshDefault() {
  fs.mkdirSync(path.dirname(CONFIG_PATH), { recursive: true });
  fs.writeFileSync(CONFIG_PATH, JSON.stringify({ sandboxPath: DEFAULT_SANDBOX, installRoot: PROJECT_ROOT, createdAt: Date.now() }, null, 2));
  return DEFAULT_SANDBOX;
}

export function getSandboxPath() {
  if (process.env.CIALL_SANDBOX_PATH) return path.resolve(process.env.CIALL_SANDBOX_PATH);

  if (fs.existsSync(CONFIG_PATH)) {
    const cfg = JSON.parse(fs.readFileSync(CONFIG_PATH, 'utf8'));
    // Only trust a persisted path recorded for THIS install root. A
    // config copied in from elsewhere (or missing installRoot entirely,
    // from before this field existed) is stale, not authoritative.
    if (cfg.sandboxPath && cfg.installRoot === PROJECT_ROOT) return cfg.sandboxPath;
  }

  return persistFreshDefault();
}

export function ensureSandboxExists() {
  const p = getSandboxPath();
  fs.mkdirSync(p, { recursive: true });
  return p;
}

export function getConfigPath() {
  return CONFIG_PATH;
}
