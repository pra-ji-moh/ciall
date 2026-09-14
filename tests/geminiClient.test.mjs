// geminiClient.test.mjs; every test here mocks globalThis.fetch. No real
// network call, no API key needed, nothing leaves this machine.

import test from 'node:test';
import assert from 'node:assert/strict';
import { callGemini, callGeminiJSON, NoApiKeyError } from '../src/lib/geminiClient.js';

const realFetch = globalThis.fetch;
function mockFetchOnce(status, body) {
  globalThis.fetch = async () => ({
    ok: status >= 200 && status < 300,
    status,
    json: async () => body,
    text: async () => JSON.stringify(body),
  });
}
test.afterEach(() => { globalThis.fetch = realFetch; });

test('callGemini throws NoApiKeyError without ever making a network call', async () => {
  let fetchCalled = false;
  globalThis.fetch = async () => { fetchCalled = true; };
  await assert.rejects(() => callGemini('', 'hello'), NoApiKeyError);
  assert.equal(fetchCalled, false, 'no network call should happen without a key');
});

test('callGemini sends the key only in the URL query string, never in headers or body', async () => {
  let seenUrl = null;
  let seenBody = null;
  globalThis.fetch = async (url, init) => {
    seenUrl = url;
    seenBody = JSON.parse(init.body);
    return { ok: true, status: 200, json: async () => ({ candidates: [{ content: { parts: [{ text: '{}' }] } }], usageMetadata: { promptTokenCount: 1, candidatesTokenCount: 1 } }) };
  };
  await callGemini('gm-test-key', 'a prompt');
  assert.ok(seenUrl.includes('key=gm-test-key'));
  assert.ok(!JSON.stringify(seenBody).includes('gm-test-key'), 'the key must never appear in the request body');
});

test('callGemini returns the joined text and normalized usage', async () => {
  mockFetchOnce(200, {
    candidates: [{ content: { parts: [{ text: 'hello ' }, { text: 'world' }] } }],
    usageMetadata: { promptTokenCount: 10, candidatesTokenCount: 5 },
  });
  const { text, usage } = await callGemini('gm-fake', 'prompt');
  assert.equal(text, 'hello world');
  assert.deepEqual(usage, { input_tokens: 10, output_tokens: 5 });
});

test('callGemini throws with the API status on a non-2xx response', async () => {
  mockFetchOnce(429, { error: 'rate limited' });
  await assert.rejects(() => callGemini('gm-fake', 'prompt'), /Gemini API error 429/);
});

test('callGemini throws a clear error when the response was cut off at the token limit', async () => {
  mockFetchOnce(200, { candidates: [{ content: { parts: [{ text: 'truncat' }] }, finishReason: 'MAX_TOKENS' }] });
  await assert.rejects(() => callGemini('gm-fake', 'prompt'), /cut off/);
});

test('callGeminiJSON parses fenced JSON and computes real cost from usage', async () => {
  mockFetchOnce(200, {
    candidates: [{ content: { parts: [{ text: '```json\n{"kind":"none","reason":"test"}\n```' }] } }],
    usageMetadata: { promptTokenCount: 1_000_000, candidatesTokenCount: 1_000_000 },
  });
  const { result, tokens, cost } = await callGeminiJSON('gm-fake', 'prompt');
  assert.deepEqual(result, { kind: 'none', reason: 'test' });
  assert.equal(tokens, 2_000_000);
  assert.equal(cost, 2.0 + 12.0); // 1M input @ $2/M + 1M output @ $12/M
});

test('callGeminiJSON throws a clear error when the model did not return valid JSON', async () => {
  mockFetchOnce(200, { candidates: [{ content: { parts: [{ text: 'not json' }] } }] });
  await assert.rejects(() => callGeminiJSON('gm-fake', 'prompt'), /Could not parse the model's response as JSON/);
});

test('callGemini retries a transient 503 and succeeds on the next attempt (proves retry.js is actually wired in)', async () => {
  let calls = 0;
  globalThis.fetch = async () => {
    calls++;
    if (calls === 1) return { ok: false, status: 503, headers: { get: () => null }, text: async () => 'overloaded' };
    return { ok: true, status: 200, json: async () => ({ candidates: [{ content: { parts: [{ text: 'recovered' }] } }] }) };
  };
  const { text } = await callGemini('gm-fake', 'prompt');
  assert.equal(calls, 2);
  assert.equal(text, 'recovered');
});

test('a stalled Gemini connection is treated as a timeout and retried, not hung forever', async () => {
  let calls = 0;
  globalThis.fetch = async (url, init) => {
    calls++;
    if (calls === 1) {
      return new Promise((_, reject) => {
        init.signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), { name: 'AbortError' })));
      });
    }
    return { ok: true, status: 200, json: async () => ({ candidates: [{ content: { parts: [{ text: 'recovered' }] } }] }) };
  };
  const { text } = await callGemini('gm-fake', 'prompt', { timeoutMs: 30 });
  assert.equal(calls, 2);
  assert.equal(text, 'recovered');
});
