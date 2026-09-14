// hashChainLog.js; upgrade 17 — a real, tamper-evident append-only log:
// every entry commits cryptographically to the hash of the entry
// before it (SHA-256, node:crypto, zero new dependencies), the same
// hash-chaining principle git's own commit history and every
// blockchain's block history use. Editing, reordering, or deleting an
// entry ANYWHERE except the very end breaks every hash after it,
// detectably, without needing a second copy to compare against.
//
// HONEST LIMITATION, stated plainly rather than oversold: a hash chain
// cannot detect TRUNCATION from the end (deleting the last N entries
// and stopping there leaves a shorter but internally-consistent
// chain). This is not a bug or an oversight — it is a mathematical
// property of every hash chain that has ever existed, git included; a
// verifier with no independent record of how many entries SHOULD
// exist cannot distinguish "this log always had 40 entries" from
// "this log had 100 entries and someone deleted the last 60." Real
// tamper-evidence for that specific threat needs an independent,
// out-of-band record of the expected latest hash (e.g. a human noting
// today's chain tip somewhere else) — this file gives you the primitive,
// not a false promise that it alone solves every threat model.
//
// This is a generic, reusable primitive — not wired to any specific
// log by default. A caller (the execution audit log, the self-audit
// baseline, a future use) supplies its own file path and entry data.

import fs from 'node:fs';
import crypto from 'node:crypto';

const GENESIS_HASH = '0'.repeat(64); // the "previous hash" of the very first entry -- SHA-256's own digest length, not an arbitrary sentinel

function sha256(s) {
  return crypto.createHash('sha256').update(s, 'utf8').digest('hex');
}

// The exact, fixed field order hashed at both append time and verify
// time -- consistency here, not JSON.stringify's own key-ordering
// behavior, is what makes the hash reproducible.
function canonicalPayload(seq, at, data, prevHash) {
  return JSON.stringify({ seq, at, data, prevHash });
}

function readLines(logPath) {
  if (!fs.existsSync(logPath)) return [];
  return fs.readFileSync(logPath, 'utf8').split('\n').filter((l) => l.length > 0);
}

/**
 * Appends `data` (any JSON-serializable value) as a new, hash-chained
 * entry to `logPath`, creating the file if it doesn't exist yet.
 * Returns the full written record, including its own hash.
 */
export function appendEntry(logPath, data, { at = Date.now() } = {}) {
  const lines = readLines(logPath);
  const seq = lines.length;
  let prevHash = GENESIS_HASH;
  if (lines.length > 0) {
    const prevRecord = JSON.parse(lines[lines.length - 1]);
    prevHash = prevRecord.hash;
  }
  const hash = sha256(canonicalPayload(seq, at, data, prevHash));
  const record = { seq, at, data, prevHash, hash };
  fs.appendFileSync(logPath, JSON.stringify(record) + '\n', 'utf8');
  return record;
}

/**
 * Walks every entry in `logPath` from the start, recomputing each
 * entry's hash from scratch (never trusting the stored `hash` field)
 * and confirming both (a) the recomputed hash matches what's stored,
 * and (b) each entry's `prevHash` matches the ACTUAL (verified) hash
 * of the entry before it. Returns the specific first broken entry, if
 * any -- never a bare true/false, same discipline as every other
 * checker in this repo.
 */
export function verifyChain(logPath) {
  const lines = readLines(logPath);
  if (lines.length === 0) return { valid: true, entriesChecked: 0 };

  let expectedPrevHash = GENESIS_HASH;
  for (let i = 0; i < lines.length; i++) {
    let record;
    try {
      record = JSON.parse(lines[i]);
    } catch (e) {
      return { valid: false, brokenAtEntry: i, entriesChecked: i, reason: `entry ${i} is not valid JSON: ${e.message}` };
    }
    if (record.seq !== i) {
      return { valid: false, brokenAtEntry: i, entriesChecked: i, reason: `entry ${i} claims seq=${record.seq}, expected ${i} -- entries have been reordered or deleted` };
    }
    if (record.prevHash !== expectedPrevHash) {
      return { valid: false, brokenAtEntry: i, entriesChecked: i, reason: `entry ${i}'s prevHash does not match the actual hash of the entry before it -- an earlier entry was edited, reordered, or removed` };
    }
    const recomputed = sha256(canonicalPayload(record.seq, record.at, record.data, record.prevHash));
    if (recomputed !== record.hash) {
      return { valid: false, brokenAtEntry: i, entriesChecked: i, reason: `entry ${i}'s content does not match its own recorded hash -- this entry was edited after being appended` };
    }
    expectedPrevHash = record.hash;
  }

  return { valid: true, entriesChecked: lines.length, chainTipHash: expectedPrevHash };
}

/** Reads every entry's `data` field, in order, without re-verifying the chain (see verifyChain for that). */
export function readEntries(logPath) {
  return readLines(logPath).map((l) => JSON.parse(l).data);
}
