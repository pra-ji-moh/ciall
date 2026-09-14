// modelClient.test.mjs; every test here mocks globalThis.fetch. No real
// network call, no API key needed, nothing leaves this machine.

import test from 'node:test';
import assert from 'node:assert/strict';
import { callClaude, callClaudeJSON, NoApiKeyError } from '../src/lib/modelClient.js';

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

test('callClaude throws NoApiKeyError without ever making a network call', async () => {
  let fetchCalled = false;
  globalThis.fetch = async () => { fetchCalled = true; };
  await assert.rejects(() => callClaude('', 'hello'), NoApiKeyError);
  assert.equal(fetchCalled, false, 'no network call should happen without a key');
});

test('callClaude sends the key in x-api-key and never in the URL or body', async () => {
  let seenHeaders = null;
  let seenBody = null;
  globalThis.fetch = async (url, init) => {
    seenHeaders = init.headers;
    seenBody = JSON.parse(init.body);
    assert.ok(!url.includes('sk-ant-test-key'), 'the key must never appear in the URL');
    return { ok: true, status: 200, json: async () => ({ content: [{ text: '{}' }], usage: { input_tokens: 1, output_tokens: 1 } }) };
  };
  await callClaude('sk-ant-test-key', 'a prompt');
  assert.equal(seenHeaders['x-api-key'], 'sk-ant-test-key');
  assert.ok(!JSON.stringify(seenBody).includes('sk-ant-test-key'), 'the key must never appear in the request body');
});

test('callClaude returns the joined text and real usage', async () => {
  mockFetchOnce(200, { content: [{ text: 'hello ' }, { text: 'world' }], usage: { input_tokens: 10, output_tokens: 5 } });
  const { text, usage } = await callClaude('sk-ant-fake', 'prompt');
  assert.equal(text, 'hello world');
  assert.deepEqual(usage, { input_tokens: 10, output_tokens: 5 });
});

test('callClaude throws with the API status and body on a non-2xx response', async () => {
  mockFetchOnce(429, { error: 'rate limited' });
  await assert.rejects(() => callClaude('sk-ant-fake', 'prompt'), /Anthropic API error 429/);
});

test('callClaudeJSON parses fenced JSON and computes real cost from usage', async () => {
  mockFetchOnce(200, { content: [{ text: '```json\n{"kind":"none","reason":"test"}\n```' }], usage: { input_tokens: 1_000_000, output_tokens: 1_000_000 } });
  const { result, tokens, cost } = await callClaudeJSON('sk-ant-fake', 'prompt');
  assert.deepEqual(result, { kind: 'none', reason: 'test' });
  assert.equal(tokens, 2_000_000);
  assert.equal(cost, 3.0 + 15.0); // 1M input @ $3/M + 1M output @ $15/M
});

test('callClaudeJSON throws a clear error when the model did not return valid JSON', async () => {
  mockFetchOnce(200, { content: [{ text: 'this is not json at all' }], usage: null });
  await assert.rejects(() => callClaudeJSON('sk-ant-fake', 'prompt'), /Could not parse the model's response as JSON/);
});

test('callClaude retries a transient 503 and succeeds on the next attempt (proves retry.js is actually wired in)', async () => {
  let calls = 0;
  globalThis.fetch = async () => {
    calls++;
    if (calls === 1) return { ok: false, status: 503, headers: { get: () => null }, text: async () => 'overloaded' };
    return { ok: true, status: 200, json: async () => ({ content: [{ text: '{"ok":true}' }], usage: { input_tokens: 1, output_tokens: 1 } }) };
  };
  const { text } = await callClaude('sk-ant-fake', 'prompt');
  assert.equal(calls, 2);
  assert.equal(text, '{"ok":true}');
});

test('a stalled connection is treated as a timeout and retried, not hung forever', async () => {
  let calls = 0;
  globalThis.fetch = async (url, init) => {
    calls++;
    if (calls === 1) {
      // Never resolves on its own; only the AbortController's signal
      // ends it, exactly like a real stalled connection.
      return new Promise((_, reject) => {
        init.signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), { name: 'AbortError' })));
      });
    }
    return { ok: true, status: 200, json: async () => ({ content: [{ text: 'recovered' }], usage: null }) };
  };
  const { text } = await callClaude('sk-ant-fake', 'prompt', { timeoutMs: 30 }); // short timeout so this test stays fast
  assert.equal(calls, 2);
  assert.equal(text, 'recovered');
});
