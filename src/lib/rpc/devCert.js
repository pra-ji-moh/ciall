// devCert.js; upgrade 8 — generates a throwaway self-signed TLS
// certificate for local testing/development of `ciall serve`.
//
// NOT part of the "implement from scratch" scope (only gRPC framing and
// MessagePack were called out for that) — X.509 certificate generation
// is delegated to the system's `openssl` binary via child_process, the
// same tool anyone would reach for outside Node to make a local dev
// cert. This is NOT an npm dependency (nothing installed into
// node_modules; openssl is a pre-existing system tool, the same way
// this repo already shells out to git/node itself in various places).
// Production use of `ciall serve` is expected to point CIALL_TLS_CERT/
// CIALL_TLS_KEY at real cert/key files the operator provides — this
// helper exists so tests and local experimentation don't require
// pre-provisioning one by hand.

import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';

/**
 * Ensures cert.pem/key.pem exist in `dir` (generating a throwaway
 * self-signed pair via openssl if not), and returns their paths.
 * Idempotent: does nothing if both files already exist.
 */
export function ensureDevCert(dir) {
  fs.mkdirSync(dir, { recursive: true });
  const certPath = path.join(dir, 'cert.pem');
  const keyPath = path.join(dir, 'key.pem');
  if (fs.existsSync(certPath) && fs.existsSync(keyPath)) return { certPath, keyPath };

  execFileSync('openssl', [
    'req', '-x509', '-newkey', 'rsa:2048',
    '-keyout', keyPath, '-out', certPath,
    '-days', '1', '-nodes',
    '-subj', '/CN=localhost',
  ], { stdio: 'pipe' });

  return { certPath, keyPath };
}
