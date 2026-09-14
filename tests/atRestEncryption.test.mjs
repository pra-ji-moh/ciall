// atRestEncryption.test.mjs; upgrade 17 — validates real AES-256-GCM
// authenticated encryption, with particular focus on the property that
// matters most for "fail-safe cryptography": a wrong passphrase or any
// tampering with the ciphertext must throw, NEVER return corrupted or
// partial plaintext. Every tamper test here mutates REAL bytes in a
// REAL blob (not a mocked failure), then confirms decryption throws.

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { encryptAtRest, decryptAtRest, writeEncryptedFile, readEncryptedFile } from '../src/lib/atRestEncryption.js';

function tempDir() {
  return fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-atrest-test-'));
}

test('encryptAtRest/decryptAtRest round-trips real plaintext correctly', () => {
  const blob = encryptAtRest('the quick brown fox', 'correct horse battery staple');
  const recovered = decryptAtRest(blob, 'correct horse battery staple');
  assert.equal(recovered, 'the quick brown fox');
});

test('the blob is fully self-contained JSON (real base64 fields), never exposes the passphrase or derived key', () => {
  const blob = encryptAtRest('secret data', 'my-passphrase');
  assert.equal(blob.alg, 'aes-256-gcm');
  assert.equal(blob.kdf, 'scrypt');
  assert.match(blob.salt, /^[A-Za-z0-9+/=]+$/);
  assert.match(blob.iv, /^[A-Za-z0-9+/=]+$/);
  assert.match(blob.authTag, /^[A-Za-z0-9+/=]+$/);
  assert.match(blob.ciphertext, /^[A-Za-z0-9+/=]+$/);
  const serialized = JSON.stringify(blob);
  assert.ok(!serialized.includes('my-passphrase'));
});

test('FAIL CLOSED: decrypting with the WRONG passphrase throws, never returns corrupted plaintext', () => {
  const blob = encryptAtRest('sensitive value', 'right-passphrase');
  assert.throws(() => decryptAtRest(blob, 'wrong-passphrase'));
});

test('FAIL CLOSED: tampering with the ciphertext bytes is detected by GCM\'s auth tag, decryption throws', () => {
  const blob = encryptAtRest('sensitive value', 'passphrase');
  const bytes = Buffer.from(blob.ciphertext, 'base64');
  bytes[0] ^= 0xff; // flip a real bit in the real ciphertext
  const tampered = { ...blob, ciphertext: bytes.toString('base64') };
  assert.throws(() => decryptAtRest(tampered, 'passphrase'));
});

test('FAIL CLOSED: tampering with the authTag is detected, decryption throws', () => {
  const blob = encryptAtRest('sensitive value', 'passphrase');
  const bytes = Buffer.from(blob.authTag, 'base64');
  bytes[0] ^= 0xff;
  const tampered = { ...blob, authTag: bytes.toString('base64') };
  assert.throws(() => decryptAtRest(tampered, 'passphrase'));
});

test('FAIL CLOSED: tampering with the IV is detected (GCM authenticates the IV as associated context), decryption throws', () => {
  const blob = encryptAtRest('sensitive value', 'passphrase');
  const bytes = Buffer.from(blob.iv, 'base64');
  bytes[0] ^= 0xff;
  const tampered = { ...blob, iv: bytes.toString('base64') };
  assert.throws(() => decryptAtRest(tampered, 'passphrase'));
});

test('FAIL CLOSED: tampering with the salt changes the derived key, decryption throws (wrong-key path, not a crash)', () => {
  const blob = encryptAtRest('sensitive value', 'passphrase');
  const bytes = Buffer.from(blob.salt, 'base64');
  bytes[0] ^= 0xff;
  const tampered = { ...blob, salt: bytes.toString('base64') };
  assert.throws(() => decryptAtRest(tampered, 'passphrase'));
});

test('two encryptions of the SAME plaintext under the SAME passphrase produce DIFFERENT salts, IVs, and ciphertexts -- no nonce/key reuse', () => {
  const blob1 = encryptAtRest('same plaintext', 'same passphrase');
  const blob2 = encryptAtRest('same plaintext', 'same passphrase');
  assert.notEqual(blob1.salt, blob2.salt);
  assert.notEqual(blob1.iv, blob2.iv);
  assert.notEqual(blob1.ciphertext, blob2.ciphertext);
});

test('encryptAtRest rejects an empty passphrase and a non-string plaintext', () => {
  assert.throws(() => encryptAtRest('x', ''), /non-empty string passphrase/);
  assert.throws(() => encryptAtRest(123, 'passphrase'), /string plaintext/);
});

test('encryptAtRest refuses a scryptN weaker than RFC 7914\'s own recommended minimum -- never silently accepts a weak KDF cost', () => {
  assert.throws(() => encryptAtRest('x', 'passphrase', { scryptN: 1024 }), /RFC 7914's own recommended minimum/);
});

test('encryptAtRest refuses a non-power-of-two scryptN', () => {
  assert.throws(() => encryptAtRest('x', 'passphrase', { scryptN: 20000 }), /power of two/);
});

test('a caller-supplied stronger scryptN is honored and round-trips correctly', () => {
  const blob = encryptAtRest('x', 'passphrase', { scryptN: 32768 });
  assert.equal(blob.kdfParams.N, 32768);
  assert.equal(decryptAtRest(blob, 'passphrase'), 'x');
});

test('decryptAtRest rejects a blob with an unrecognized algorithm/KDF', () => {
  const blob = encryptAtRest('x', 'passphrase');
  assert.throws(() => decryptAtRest({ ...blob, alg: 'aes-128-cbc' }, 'passphrase'), /unrecognized blob format/);
});

test('empty-string plaintext round-trips correctly (a real edge case for authenticated encryption)', () => {
  const blob = encryptAtRest('', 'passphrase');
  assert.equal(decryptAtRest(blob, 'passphrase'), '');
});

test('a large plaintext (well beyond a single AES block) round-trips correctly', () => {
  const large = 'x'.repeat(100000);
  const blob = encryptAtRest(large, 'passphrase');
  assert.equal(decryptAtRest(blob, 'passphrase'), large);
});

test('unicode plaintext round-trips correctly', () => {
  const text = 'héllo wörld 日本語 🎉';
  const blob = encryptAtRest(text, 'passphrase');
  assert.equal(decryptAtRest(blob, 'passphrase'), text);
});

// ---- real file round trip -------------------------------------------------

test('writeEncryptedFile/readEncryptedFile round-trips through a REAL file on disk', () => {
  const dir = tempDir();
  const filePath = path.join(dir, 'secret.json');
  writeEncryptedFile(filePath, 'file contents to protect', 'file-passphrase');
  assert.ok(fs.existsSync(filePath));
  const raw = fs.readFileSync(filePath, 'utf8');
  assert.ok(!raw.includes('file contents to protect'), 'the plaintext must never appear in the written file');
  const recovered = readEncryptedFile(filePath, 'file-passphrase');
  assert.equal(recovered, 'file contents to protect');
});

test('readEncryptedFile FAILS CLOSED with the wrong passphrase against a real file', () => {
  const dir = tempDir();
  const filePath = path.join(dir, 'secret.json');
  writeEncryptedFile(filePath, 'protected', 'right-passphrase');
  assert.throws(() => readEncryptedFile(filePath, 'wrong-passphrase'));
});
