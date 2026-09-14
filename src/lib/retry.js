// retry.js; transient-failure tolerance for provider calls, ported from
// client-backend-fixed/src/lib/retry.js (same logic, same reasoning —
// this substrate previously had NO retry at all on modelClient.js /
// geminiClient.js, meaning a single 5xx or dropped connection killed a
// live-verify call outright. Real gap for anything meant to run
// unattended or under real load, closed here).
//
// 429 is deliberately NOT retried here — see the original comment this
// carries forward: retrying a rate limit multiplies attempts across
// layers and lengthens the lockout, which is the opposite of what a rate
// limit is asking for. This module retries genuinely transient
// INFRASTRUCTURE faults (5xx, overload, dropped connections) only.
const RETRYABLE_STATUS = new Set([500, 502, 503, 529]);
const MAX_DELAY_MS = 30_000;
const DEFAULT_RETRIES = 3;
// A hung request (the provider accepts the connection but never
// responds) is a real, observed failure mode this substrate previously
// had NO protection against — a stalled model call would hang the whole
// CLI forever with no way out but Ctrl-C. Every attempt gets its own
// AbortController on this timer; a timeout is treated the same as a
// dropped connection (retried, not thrown immediately) since it's the
// same category of transient infrastructure fault.
const DEFAULT_TIMEOUT_MS = 30_000;

function backoffMs(attempt, retryAfterHeader) {
  const retryAfter = Number(retryAfterHeader);
  if (Number.isFinite(retryAfter) && retryAfter > 0) {
    return Math.min(retryAfter * 1000, MAX_DELAY_MS);
  }
  // 1.5s, 3s, 6s … + up to 400ms jitter so concurrent callers don't
  // re-stampede a struggling endpoint in lockstep.
  return Math.min(1500 * 2 ** (attempt - 1) + Math.random() * 400, MAX_DELAY_MS);
}

export async function fetchWithRetry(url, init, { retries = DEFAULT_RETRIES, timeoutMs = DEFAULT_TIMEOUT_MS } = {}) {
  let lastNetworkError = null;
  let pendingDelay = 0;
  for (let attempt = 0; attempt <= retries; attempt++) {
    if (attempt > 0) await new Promise((r) => setTimeout(r, pendingDelay || backoffMs(attempt)));
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);
    let response;
    try {
      response = await fetch(url, { ...init, signal: controller.signal });
    } catch (e) {
      lastNetworkError = e.name === 'AbortError' ? new Error(`Request timed out after ${timeoutMs}ms`) : e;
      pendingDelay = 0;
      continue;
    } finally {
      clearTimeout(timer);
    }
    lastNetworkError = null;
    if (response.ok || !RETRYABLE_STATUS.has(response.status) || attempt === retries) {
      return response;
    }
    pendingDelay = backoffMs(attempt + 1, response.headers.get('retry-after'));
  }
  throw lastNetworkError || new Error('Network request failed after retries.');
}
