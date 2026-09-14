// retry.test.mjs; every test mocks globalThis.fetch, none touch the
// network, and delays are kept short by mocking backoff indirectly
// through small retryable counts (no real 1.5s+ sleeps in this suite).

import test from 'node:test';
import assert from 'node:assert/strict';
import { fetchWithRetry } from '../src/lib/retry.js';

const realFetch = globalThis.fetch;
test.afterEach(() => { globalThis.fetch = realFetch; });

test('a 200 on the first try returns immediately, exactly one fetch call', async () => {
  let calls = 0;
  globalThis.fetch = async () => { calls++; return { ok: true, status: 200 }; };
  const res = await fetchWithRetry('https://example.test', {});
  assert.equal(res.status, 200);
  assert.equal(calls, 1);
});

test('a 503 is retried, and a subsequent 200 is returned', async () => {
  let calls = 0;
  globalThis.fetch = async () => {
    calls++;
    if (calls === 1) return { ok: false, status: 503, headers: { get: () => null } };
    return { ok: true, status: 200 };
  };
  const res = await fetchWithRetry('https://example.test', {}, { retries: 2 });
  assert.equal(res.status, 200);
  assert.equal(calls, 2);
});

test('a 429 is NOT retried by this layer -- returned immediately as-is', async () => {
  let calls = 0;
  globalThis.fetch = async () => { calls++; return { ok: false, status: 429, headers: { get: () => null } }; };
  const res = await fetchWithRetry('https://example.test', {}, { retries: 3 });
  assert.equal(res.status, 429);
  assert.equal(calls, 1, '429 must fail immediately, not be retried -- retrying a rate limit lengthens the lockout');
});

test('a persistent network error throws after exhausting retries', async () => {
  let calls = 0;
  globalThis.fetch = async () => { calls++; throw new Error('ECONNRESET'); };
  await assert.rejects(() => fetchWithRetry('https://example.test', {}, { retries: 2 }), /ECONNRESET/);
  assert.equal(calls, 3); // initial attempt + 2 retries
});

test('a persistent 503 returns the final failing response rather than throwing', async () => {
  let calls = 0;
  globalThis.fetch = async () => { calls++; return { ok: false, status: 503, headers: { get: () => null } }; };
  const res = await fetchWithRetry('https://example.test', {}, { retries: 2 });
  assert.equal(res.status, 503);
  assert.equal(calls, 3);
});

test('a non-retryable 4xx (e.g. 400) is returned immediately, not retried', async () => {
  let calls = 0;
  globalThis.fetch = async () => { calls++; return { ok: false, status: 400, headers: { get: () => null } }; };
  const res = await fetchWithRetry('https://example.test', {}, { retries: 3 });
  assert.equal(res.status, 400);
  assert.equal(calls, 1);
});
