// connectors/httpConnector.js; upgrade 12 — a real DataSource that
// polls a REST endpoint and returns structured records.
//
// `fetchImpl` is REQUIRED, never defaulted to a global network call:
// this repo's own testing discipline (see modelClient.test.mjs/
// geminiClient.test.mjs injecting `callJSON`) is that anything touching
// the network is injectable, so tests never make live HTTP calls. A
// real caller wires this to the global `fetch` explicitly
// (`createHttpConnector({url, fetchImpl: fetch})`); tests wire it to a
// fake that returns canned responses. This is not a mocked connector —
// it is a REAL connector with its network dependency inverted, the
// same pattern this repo already uses everywhere else network calls
// happen.

const MAX_RESPONSE_BYTES = 20 * 1024 * 1024; // 20MB cap, same "no unbounded read" discipline as fileConnector.js

/**
 * `config`: { url: string, fetchImpl: function, headers?: object,
 * recordsPath?: string[] } — recordsPath lets the caller point at a
 * nested array in the response (e.g. `{data:{items:[...]}}` ->
 * `recordsPath: ['data','items']`); omitted, the whole response body
 * must itself already be an array.
 */
export function createHttpConnector({ url, fetchImpl, headers = {}, recordsPath = [] } = {}) {
  if (typeof url !== 'string' || !url) throw new Error('httpConnector needs a url');
  if (typeof fetchImpl !== 'function') throw new Error('httpConnector requires an explicit fetchImpl (e.g. the global fetch) — no implicit network access');
  if (!Array.isArray(recordsPath)) throw new Error('httpConnector: recordsPath must be an array of keys');

  return {
    async fetch() {
      const response = await fetchImpl(url, { headers });
      if (!response || typeof response.text !== 'function') {
        throw new Error('httpConnector: fetchImpl must return a Response-like object with an async text() method');
      }
      if (!response.ok) throw new Error(`httpConnector: ${url} returned status ${response.status}`);
      const text = await response.text();
      if (Buffer.byteLength(text, 'utf8') > MAX_RESPONSE_BYTES) {
        throw new Error(`httpConnector: response from ${url} is over the ${MAX_RESPONSE_BYTES}-byte cap`);
      }
      let body = JSON.parse(text);
      for (const key of recordsPath) {
        if (body == null || typeof body !== 'object' || !(key in body)) {
          throw new Error(`httpConnector: recordsPath key "${key}" not found in response from ${url}`);
        }
        body = body[key];
      }
      if (!Array.isArray(body)) throw new Error(`httpConnector: resolved response from ${url} is not an array of records`);
      return body;
    },
  };
}
