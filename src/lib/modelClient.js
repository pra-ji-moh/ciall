// modelClient.js; the model dependency this substrate previously,
// deliberately, did not have. Closing the last documented gap in
// VISION-PLTR.md section 8: a model-backed kernel (consistency, MCMC,
// dynamics, combinatorial, domain-of-validity, numeric-check) needs a
// real designSpec backed by a real model call, and this is that call.
//
// SAFETY BOUNDARY, load-bearing. The API key is NEVER read from a CLI
// argument, NEVER logged, NEVER written to the execution audit log —
// only from the ANTHROPIC_API_KEY environment variable, read once by the
// caller (see bin/ciall.mjs) and passed in here as a plain parameter,
// exactly like the original anthropicClient.js in client-backend-fixed
// does. This file does not read the environment itself, so it stays
// testable (a test can pass any string) and there is exactly one place
// in this whole repo (bin/ciall.mjs) responsible for sourcing the real
// key. Enforced by tests/keyHandling.test.mjs, not just this comment.
//
// Deliberately smaller than client-backend-fixed's anthropicClient.js:
// no proxy mode (this is a local CLI, not a deployed app with a shared
// server-side key), no web search tool, no deep/Opus tier, no prompt
// caching (each kernel design call is a single one-off request, not a
// resent system prompt across many calls the way the app's tree
// generation is). Every kernel's buildPrompt() output still goes through
// the exact same request shape a real Claude Messages API call expects.
//
// RELIABILITY: routed through retry.js's fetchWithRetry — bounded
// backoff on 5xx/dropped-connection/timeout (30s per attempt by
// default), 429 deliberately NOT retried here (see retry.js). An earlier
// version of this file called fetch() directly with no retry and no
// timeout at all; a single transient failure or a stalled connection
// would kill the call outright, or hang forever with no way out but
// Ctrl-C.

import { parseJsonLoose } from './jsonExtract.js';
import { fetchWithRetry } from './retry.js';

export const MODEL = 'claude-sonnet-5';

export class NoApiKeyError extends Error {
  constructor() {
    super('No ANTHROPIC_API_KEY set. Export it in your shell before running a live-verified command; this file never reads or accepts a key any other way.');
    this.name = 'NoApiKeyError';
  }
}

function tokensAndCost(usage) {
  if (!usage) return { tokens: 0, cost: 0 };
  const input = usage.input_tokens || 0;
  const output = usage.output_tokens || 0;
  // Sonnet 5 pricing, matching client-backend-fixed/src/lib/anthropicClient.js.
  const cost = input * (3.0 / 1_000_000) + output * (15.0 / 1_000_000);
  return { tokens: input + output, cost };
}

/**
 * Calls the Claude Messages API directly (Node's built-in fetch; no
 * browser-access header needed here, unlike the app version, since Node
 * has no CORS restriction to route around). Returns { text, usage }.
 */
export async function callClaude(apiKey, prompt, opts = {}) {
  if (!apiKey || !apiKey.trim()) throw new NoApiKeyError();

  const response = await fetchWithRetry('https://api.anthropic.com/v1/messages', {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'x-api-key': apiKey.trim(),
      'anthropic-version': '2023-06-01',
    },
    body: JSON.stringify({
      model: MODEL,
      max_tokens: (opts.maxTokens || 1200) + 2000, // headroom for adaptive thinking, same reasoning as the app's reasoningParams
      messages: [{ role: 'user', content: prompt }],
    }),
  }, { retries: opts.retries, timeoutMs: opts.timeoutMs });

  if (!response.ok) {
    const errText = await response.text().catch(() => '');
    const err = new Error(`Anthropic API error ${response.status}: ${errText.slice(0, 300)}`);
    err.status = response.status;
    throw err;
  }

  const data = await response.json();
  const text = (data.content || []).map((b) => b.text || '').join('');
  return { text, usage: data.usage || null };
}

/**
 * Calls Claude and parses the response as JSON via the same tolerant
 * extraction the app uses. Returns { result, usage, tokens, cost }.
 */
export async function callClaudeJSON(apiKey, prompt, opts = {}) {
  const { text, usage } = await callClaude(apiKey, prompt, opts);
  let result;
  try {
    result = parseJsonLoose(text);
  } catch (e) {
    throw new Error(`Could not parse the model's response as JSON: ${e.message}`);
  }
  const { tokens, cost } = tokensAndCost(usage);
  return { result, usage, tokens, cost };
}
