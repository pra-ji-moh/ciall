// hashChainLog.test.mjs; upgrade 17 — validates the tamper-evident
// hash-chained log against a REAL file on disk, with REAL tampering
// (editing bytes in the actual file, not a mocked scenario) at several
// different positions in the chain, confirming each is caught with the
// specific broken entry identified -- and confirming the one honest
// limitation (undetectable truncation from the end) is real too, not
// just claimed.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import crypto from 'node:crypto';
import { appendEntry, verifyChain, readEntries } from '../src/lib/hashChainLog.js';

function tempLogPath() {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-hashchain-test-'));
  return path.join(dir, 'log.jsonl');
}

test('an empty/nonexistent log verifies as trivially valid with zero entries', () => {
  const logPath = tempLogPath();
  const result = verifyChain(logPath);
  assert.equal(result.valid, true);
  assert.equal(result.entriesChecked, 0);
});

test('appendEntry writes a real record with seq, prevHash, and a real 64-hex-char SHA-256 hash', () => {
  const logPath = tempLogPath();
  const record = appendEntry(logPath, { action: 'write', target: 'file.txt' });
  assert.equal(record.seq, 0);
  assert.match(record.hash, /^[0-9a-f]{64}$/);
  assert.equal(record.prevHash, '0'.repeat(64));
});

test('a chain of several entries verifies cleanly, front to back', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  appendEntry(logPath, { n: 3 });
  const result = verifyChain(logPath);
  assert.equal(result.valid, true);
  assert.equal(result.entriesChecked, 3);
  assert.match(result.chainTipHash, /^[0-9a-f]{64}$/);
});

test('each entry\'s prevHash genuinely chains to the actual hash of the entry before it, not just sequentially numbered', () => {
  const logPath = tempLogPath();
  const r1 = appendEntry(logPath, { n: 1 });
  const r2 = appendEntry(logPath, { n: 2 });
  assert.equal(r2.prevHash, r1.hash);
});

test('readEntries returns just the data payloads, in order', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  assert.deepEqual(readEntries(logPath), [{ n: 1 }, { n: 2 }]);
});

// ---- REAL tampering, at real byte positions in the real file -----------

test('editing the CONTENT of a MIDDLE entry is detected, at that exact entry', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  appendEntry(logPath, { n: 3 });

  const lines = fs.readFileSync(logPath, 'utf8').split('\n').filter(Boolean);
  const record1 = JSON.parse(lines[1]);
  record1.data = { n: 999 }; // tamper with entry 1's content, WITHOUT recomputing its hash -- exactly what an attacker editing a log file by hand would do
  lines[1] = JSON.stringify(record1);
  fs.writeFileSync(logPath, lines.join('\n') + '\n', 'utf8');

  const result = verifyChain(logPath);
  assert.equal(result.valid, false);
  assert.equal(result.brokenAtEntry, 1);
  assert.match(result.reason, /does not match its own recorded hash/);
});

test('editing a middle entry\'s content AND recomputing ITS OWN hash (to hide the direct edit) still breaks the NEXT entry\'s prevHash link', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  appendEntry(logPath, { n: 3 });

  const lines = fs.readFileSync(logPath, 'utf8').split('\n').filter(Boolean);
  const record1 = JSON.parse(lines[1]);
  record1.data = { n: 999 };
  // Recompute entry 1's OWN hash to match its tampered content (a more
  // sophisticated forgery attempt) -- this is exactly why the chain
  // links via prevHash, not just per-entry self-consistency: entry 1
  // can be made internally consistent with itself, but entry 2's
  // prevHash still points at the ORIGINAL (now-wrong) hash.
  const canonical = JSON.stringify({ seq: record1.seq, at: record1.at, data: record1.data, prevHash: record1.prevHash });
  record1.hash = crypto.createHash('sha256').update(canonical, 'utf8').digest('hex');
  lines[1] = JSON.stringify(record1);
  fs.writeFileSync(logPath, lines.join('\n') + '\n', 'utf8');

  const result = verifyChain(logPath);
  assert.equal(result.valid, false);
  assert.equal(result.brokenAtEntry, 2); // entry 1 now looks self-consistent; entry 2's prevHash no longer matches it
  assert.match(result.reason, /prevHash does not match/);
});

test('deleting a middle entry entirely is detected, both by the seq gap and the broken prevHash link', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  appendEntry(logPath, { n: 3 });

  const lines = fs.readFileSync(logPath, 'utf8').split('\n').filter(Boolean);
  lines.splice(1, 1); // remove entry 1 entirely
  fs.writeFileSync(logPath, lines.join('\n') + '\n', 'utf8');

  const result = verifyChain(logPath);
  assert.equal(result.valid, false);
  assert.equal(result.brokenAtEntry, 1); // the entry now AT position 1 (originally entry 2) has seq=2, expected 1
  assert.match(result.reason, /reordered or deleted/);
});

test('reordering two entries is detected', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  appendEntry(logPath, { n: 3 });

  const lines = fs.readFileSync(logPath, 'utf8').split('\n').filter(Boolean);
  [lines[1], lines[2]] = [lines[2], lines[1]];
  fs.writeFileSync(logPath, lines.join('\n') + '\n', 'utf8');

  const result = verifyChain(logPath);
  assert.equal(result.valid, false);
  assert.equal(result.brokenAtEntry, 1);
});

test('HONEST LIMITATION, demonstrated not just claimed: truncating the log from the END (deleting the most recent entries) verifies as a clean, valid, SHORTER chain -- a hash chain alone cannot detect this', () => {
  const logPath = tempLogPath();
  appendEntry(logPath, { n: 1 });
  appendEntry(logPath, { n: 2 });
  const r3 = appendEntry(logPath, { n: 3 });

  const lines = fs.readFileSync(logPath, 'utf8').split('\n').filter(Boolean);
  lines.pop(); // delete the LAST entry only
  fs.writeFileSync(logPath, lines.join('\n') + '\n', 'utf8');

  const result = verifyChain(logPath);
  assert.equal(result.valid, true); // genuinely valid -- this is the real, disclosed limitation, not a bug
  assert.equal(result.entriesChecked, 2);
  assert.notEqual(result.chainTipHash, r3.hash); // the tip is now stale -- an out-of-band record of the real tip is the only real defense against this specific attack
});

test('appendEntry works correctly across many sequential real file writes (not just held in memory)', () => {
  const logPath = tempLogPath();
  for (let i = 0; i < 50; i++) appendEntry(logPath, { i });
  const result = verifyChain(logPath);
  assert.equal(result.valid, true);
  assert.equal(result.entriesChecked, 50);
  assert.deepEqual(readEntries(logPath).map((d) => d.i), Array.from({ length: 50 }, (_, i) => i));
});
