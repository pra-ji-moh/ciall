// geminiClient.js; the second model provider, mirroring modelClient.js's
// interface exactly ({ text, usage } from the base call, { result, usage,
// tokens, cost } from *JSON) so modelDesignSpec.js can call either
// provider through the same shape — same pattern client-backend-fixed's
// geminiClient.js already uses to sit alongside anthropicClient.js there.
//
// Same safety boundary as modelClient.js: the API key is a PARAMETER,
// never read from the environment in this file, so it stays
// unit-testable (mocked fetch, any string works as a key) and
// bin/ciall.mjs remains the only place in this repo that reads a real
// key from the environment — GEMINI_API_KEY specifically, never
// ANTHROPIC_API_KEY, never passed as a CLI argument, never logged, never
// written to the audit log. Enforced by tests/keyHandling.test.mjs, not
// just this comment.
//
// Deliberately smaller than client-backend-fixed's geminiClient.js: no
// proxy mode (local CLI, no shared server-side key), no web search
// grounding — one-off kernel-spec calls, same simplification
// modelClient.js already made on the Anthropic side. Retry IS wired in
// (via retry.js's fetchWithRetry — bounded backoff on 5xx/dropped
// connections/30s timeout, 429 not retried here), unlike an earlier
// version of this file which called fetch() directly with neither.

import { parseJsonLoose } from './jsonExtract.js';
import { fetchWithRetry } from './retry.js';

// 'gemini-pro-latest' is Google's self-updating alias for their current
// top reasoning-tier Pro model — matches client-backend-fixed's choice
// and its reasoning: a stable alias, not a hardcoded preview snapshot
// that can 404 when Google retires it.
export const MODEL = 'gemini-pro-latest';
// Pricing mirrors client-backend-fixed/src/lib/geminiClient.js (verified
// 2026-07 Pro-tier rate); same "verify against
// ai.google.dev/gemini-api/docs/pricing before relying on this for real
// spend tracking" caveat applies here.
const PRICE_PER_M_INPUT = 2.00;
const PRICE_PER_M_OUTPUT = 12.00;

export class NoApiKeyError extends Error {
  constructor() {
    super('No GEMINI_API_KEY set. Export it in your shell before running a live-verified command; this file never reads or accepts a key any other way.');
    this.name = 'NoApiKeyError';
  }
}

function tokensAndCost(usage) {
  if (!usage) return { tokens: 0, cost: 0 };
  const input = usage.input_tokens || 0;
  const output = usage.output_tokens || 0;
  const cost = input * (PRICE_PER_M_INPUT / 1_000_000) + output * (PRICE_PER_M_OUTPUT / 1_000_000);
  return { tokens: input + output, cost };
}

/**
 * Calls the Gemini API directly (Node's built-in fetch). Returns
 * { text, usage } with usage normalized to the same
 * { input_tokens, output_tokens } shape modelClient.js uses, so a caller
 * never has to branch on provider to read the result.
 */
export async function callGemini(apiKey, prompt, opts = {}) {
  if (!apiKey || !apiKey.trim()) throw new NoApiKeyError();

  const requested = opts.maxTokens || 1500;
  const response = await fetchWithRetry(
    `https://generativelanguage.googleapis.com/v1beta/models/${MODEL}:generateContent?key=${encodeURIComponent(apiKey.trim())}`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        contents: [{ role: 'user', parts: [{ text: prompt }] }],
        generationConfig: { maxOutputTokens: requested + 2000 },
      }),
    },
    { retries: opts.retries, timeoutMs: opts.timeoutMs },
  );

  if (!response.ok) {
    const errText = await response.text().catch(() => '');
    const err = new Error(`Gemini API error ${response.status}: ${errText.slice(0, 300)}`);
    err.status = response.status;
    throw err;
  }

  const data = await response.json();
  const candidate = data.candidates?.[0];
  const parts = candidate?.content?.parts || [];
  const text = parts.map((p) => p.text || '').join('');
  const usage = data.usageMetadata
    ? { input_tokens: data.usageMetadata.promptTokenCount || 0, output_tokens: data.usageMetadata.candidatesTokenCount || 0 }
    : null;

  if (candidate?.finishReason === 'MAX_TOKENS') {
    throw new Error(`Gemini response was cut off at the ${requested}-token limit before finishing.`);
  }

  return { text, usage };
}

/**
 * Calls Gemini and parses the response as JSON via the same tolerant
 * extraction modelClient.js uses. Returns { result, usage, tokens, cost }.
 */
export async function callGeminiJSON(apiKey, prompt, opts = {}) {
  const { text, usage } = await callGemini(apiKey, prompt, opts);
  let result;
  try {
    result = parseJsonLoose(text);
  } catch (e) {
    throw new Error(`Could not parse the model's response as JSON: ${e.message}`);
  }
  const { tokens, cost } = tokensAndCost(usage);
  return { result, usage, tokens, cost };
}
