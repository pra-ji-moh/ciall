// jsonExtract.js; tolerant JSON extraction for LLM responses. Copied
// unmodified from client-backend-fixed/src/lib/jsonExtract.js -- pure,
// dependency-free, so it belongs in this substrate directly rather than
// importing across repos.
//
// Strategy: try the fence-stripped text as-is first (unchanged behavior,
// cheapest path). Only if that fails, fall back to scanning for the
// first '{' or '[' and locating ITS matching close bracket by depth;
// respecting string literals and escapes so a brace inside a quoted
// string doesn't miscount; then parse just that span. If neither works,
// throw the ORIGINAL parse error (on the un-extracted text) so the
// message still points at the real output, not a confusing substring.

function findMatchingBracket(text, openIdx) {
  const open = text[openIdx];
  const close = open === '{' ? '}' : ']';
  let depth = 0;
  let inString = false;
  let escaped = false;
  for (let i = openIdx; i < text.length; i++) {
    const ch = text[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === '"') inString = false;
      continue;
    }
    if (ch === '"') { inString = true; continue; }
    if (ch === open) depth++;
    else if (ch === close) {
      depth--;
      if (depth === 0) return i;
    }
  }
  return -1; // unbalanced; no clean extraction possible
}

/**
 * Strips markdown code fences and parses JSON, tolerating stray prose
 * around a single well-formed JSON object/array. Throws with the
 * original JSON.parse error message on failure (not a generic one).
 */
export function parseJsonLoose(rawText) {
  const cleaned = String(rawText || '').replace(/```json|```/g, '').trim();
  try {
    return JSON.parse(cleaned);
  } catch (firstErr) {
    const openIdx = cleaned.search(/[{[]/);
    if (openIdx === -1) throw firstErr;
    const closeIdx = findMatchingBracket(cleaned, openIdx);
    if (closeIdx === -1) throw firstErr;
    const span = cleaned.slice(openIdx, closeIdx + 1);
    try {
      return JSON.parse(span);
    } catch {
      throw firstErr; // extraction didn't help; surface the original error
    }
  }
}
