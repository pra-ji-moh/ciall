// fnv1a.js; FNV-1a hash (64-bit, BigInt), shared by consistencyKernel.js
// and mcmcSearch.js for sparse incremental recomputation (upgrade 6).
// Fowler/Noll/Vo's published algorithm and constants — a fast,
// non-cryptographic, well-distributed hash, not a new invention. Zero
// external dependencies, ~15 lines of actual logic.
//
// hashBytes() is the primitive; hashValue() serializes an arbitrary JS
// value (the shapes this repo actually hashes: commitment objects, MCMC
// spec fragments — plain objects/arrays/strings/numbers/booleans/null,
// nothing exotic) into a canonical byte sequence first. "Canonical"
// matters here: two structurally-identical objects must hash identically
// regardless of key insertion order, since a claim re-extracted by a
// model on a later call is not guaranteed to produce keys in the same
// order even when the content is the same.

const FNV_OFFSET_BASIS_64 = 0xcbf29ce484222325n;
const FNV_PRIME_64 = 0x100000001b3n;
const MASK_64 = (1n << 64n) - 1n;

/** FNV-1a over a byte sequence (any array-like of 0-255 integers, e.g. a Buffer or Uint8Array). Returns a BigInt. */
export function hashBytes(bytes) {
  let hash = FNV_OFFSET_BASIS_64;
  for (let i = 0; i < bytes.length; i++) {
    hash ^= BigInt(bytes[i]);
    hash = (hash * FNV_PRIME_64) & MASK_64;
  }
  return hash;
}

// Canonical JSON-like serialization: object keys sorted, so key order
// never affects the hash. Not full JSON (doesn't need to be — this is
// hashed, never parsed back), just deterministic and unambiguous enough
// that no two distinct values in this repo's actual data shapes can
// collide by construction (distinct types/values always produce
// distinct byte sequences here, independent of the hash function itself).
function canonicalize(value) {
  if (value === null || value === undefined) return 'null';
  const t = typeof value;
  if (t === 'string') return JSON.stringify(value);
  if (t === 'number' || t === 'boolean') return String(value);
  if (Array.isArray(value)) return `[${value.map(canonicalize).join(',')}]`;
  if (t === 'object') {
    const keys = Object.keys(value).sort();
    return `{${keys.map((k) => `${JSON.stringify(k)}:${canonicalize(value[k])}`).join(',')}}`;
  }
  return JSON.stringify(String(value));
}

/** Hashes an arbitrary JS value (object/array/primitive) via FNV-1a-64 over its canonical serialization. Returns a BigInt. */
export function hashValue(value) {
  const s = canonicalize(value);
  const bytes = Buffer.from(s, 'utf8');
  return hashBytes(bytes);
}

/** hashValue(), formatted as a fixed-width hex string — convenient as a Map key (BigInt itself works fine as a Map key too, but string keys are easier to log/debug). */
export function hashKey(value) {
  return hashValue(value).toString(16).padStart(16, '0');
}
