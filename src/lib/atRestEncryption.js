// atRestEncryption.js; upgrade 17 — real, authenticated encryption at
// rest, `node:crypto` only, zero new dependencies. AES-256-GCM
// (authenticated encryption — confidentiality AND integrity in one
// primitive) with scrypt for passphrase-to-key derivation.
//
// THE FAIL-SAFE PROPERTY THAT MATTERS MOST, same discipline this
// project already applies everywhere else: decryption must FAIL
// CLOSED. A wrong passphrase, or ciphertext tampered with in any way,
// must throw — never silently return truncated, corrupted, or
// partially-decrypted plaintext. This is GCM's actual authentication
// tag doing real work, not a decorative check: `decipher.final()`
// itself throws when the tag doesn't verify, and this module lets
// that throw propagate as the one and only outcome for a bad key or
// tampered ciphertext, rather than catching it and returning
// `null`/partial data (a real, common mistake that quietly turns
// "authentication failed" into "here's some bytes, good luck").
//
// KEY DERIVATION. A passphrase is never used as an AES key directly —
// scrypt (RFC 7914) stretches it, with a fresh random salt PER
// ENCRYPTION (so the same passphrase encrypting two different things
// never derives the same key), at N=16384 by default — RFC 7914's own
// recommended MINIMUM for interactive use, chosen specifically so this
// runs within Node's default scrypt memory cap without needing manual
// tuning. A caller doing pure at-rest (non-interactive, latency
// tolerant) encryption can pass a higher `scryptN` via options for
// additional strength; this module does not silently accept a WEAK
// override — see `MIN_SCRYPT_N` below.
//
// NONCE DISCIPLINE. The IV (96 bits, GCM's recommended size) is
// cryptographically random per call, generated fresh every single
// encryption — GCM's security guarantee catastrophically fails if the
// same (key, IV) pair is ever reused, and since the key ITSELF is also
// freshly derived per call (via the random salt), this module has two
// independent reasons the same (key, IV) pair is never repeated, not
// just one.

import crypto from 'node:crypto';
import fs from 'node:fs';

const KEY_LEN = 32; // AES-256
const IV_LEN = 12; // 96 bits, GCM's recommended IV size
const SALT_LEN = 16;
const DEFAULT_SCRYPT_N = 16384; // 2^14, RFC 7914's own recommended interactive minimum
const MIN_SCRYPT_N = 16384; // refuse anything weaker than the RFC's own floor -- a caller can go higher, never lower

/**
 * Encrypts `plaintext` (a string) under `passphrase`, returning a
 * self-contained, JSON-serializable blob (salt/iv/authTag/ciphertext,
 * all base64, plus the KDF parameters used) — everything a future
 * `decryptAtRest` call needs, and nothing that leaks the passphrase or
 * key itself.
 */
export function encryptAtRest(plaintext, passphrase, opts = {}) {
  if (typeof plaintext !== 'string') throw new Error('encryptAtRest needs a string plaintext');
  if (typeof passphrase !== 'string' || passphrase.length === 0) throw new Error('encryptAtRest needs a non-empty string passphrase');
  const N = opts.scryptN ?? DEFAULT_SCRYPT_N;
  if (!Number.isInteger(N) || N < MIN_SCRYPT_N || (N & (N - 1)) !== 0) {
    throw new Error(`encryptAtRest: scryptN must be a power of two >= ${MIN_SCRYPT_N} (RFC 7914's own recommended minimum), got ${N}`);
  }
  const r = 8, p = 1;

  const salt = crypto.randomBytes(SALT_LEN);
  const key = crypto.scryptSync(passphrase, salt, KEY_LEN, { N, r, p, maxmem: 256 * N * r });
  const iv = crypto.randomBytes(IV_LEN);
  const cipher = crypto.createCipheriv('aes-256-gcm', key, iv);
  const ciphertext = Buffer.concat([cipher.update(plaintext, 'utf8'), cipher.final()]);
  const authTag = cipher.getAuthTag();

  return {
    v: 1,
    alg: 'aes-256-gcm',
    kdf: 'scrypt',
    kdfParams: { N, r, p },
    salt: salt.toString('base64'),
    iv: iv.toString('base64'),
    authTag: authTag.toString('base64'),
    ciphertext: ciphertext.toString('base64'),
  };
}

/**
 * Decrypts a blob from `encryptAtRest`. FAILS CLOSED: a wrong
 * passphrase or any tampering with the ciphertext/authTag/iv/salt
 * causes this to throw, never to return corrupted or partial
 * plaintext. This is not a defensive wrapper papering over a weaker
 * guarantee underneath — it IS the guarantee: GCM's authentication tag
 * verification happens inside `decipher.final()`, and this function
 * does nothing to weaken or bypass that.
 */
export function decryptAtRest(blob, passphrase) {
  if (!blob || typeof blob !== 'object') throw new Error('decryptAtRest needs a blob object from encryptAtRest');
  if (blob.alg !== 'aes-256-gcm' || blob.kdf !== 'scrypt') throw new Error(`decryptAtRest: unrecognized blob format (alg=${blob.alg}, kdf=${blob.kdf})`);
  if (typeof passphrase !== 'string' || passphrase.length === 0) throw new Error('decryptAtRest needs a non-empty string passphrase');

  const salt = Buffer.from(blob.salt, 'base64');
  const iv = Buffer.from(blob.iv, 'base64');
  const authTag = Buffer.from(blob.authTag, 'base64');
  const ciphertext = Buffer.from(blob.ciphertext, 'base64');
  const { N, r, p } = blob.kdfParams;

  const key = crypto.scryptSync(passphrase, salt, KEY_LEN, { N, r, p, maxmem: 256 * N * r });
  const decipher = crypto.createDecipheriv('aes-256-gcm', key, iv);
  decipher.setAuthTag(authTag);

  // Deliberately NOT wrapped in a try/catch that swallows the error --
  // a failed authentication must propagate as a thrown error, the one
  // and only outcome, never converted into a returned null/undefined a
  // careless caller could fail to check.
  const plaintext = Buffer.concat([decipher.update(ciphertext), decipher.final()]);
  return plaintext.toString('utf8');
}

/** Convenience: encrypts and writes a blob to `filePath` as JSON. */
export function writeEncryptedFile(filePath, plaintext, passphrase, opts = {}) {
  const blob = encryptAtRest(plaintext, passphrase, opts);
  fs.writeFileSync(filePath, JSON.stringify(blob, null, 2), 'utf8');
}

/** Convenience: reads and decrypts a blob written by `writeEncryptedFile`. Fails closed exactly as `decryptAtRest` does. */
export function readEncryptedFile(filePath, passphrase) {
  const blob = JSON.parse(fs.readFileSync(filePath, 'utf8'));
  return decryptAtRest(blob, passphrase);
}
