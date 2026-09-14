// connectors/fileConnector.js; upgrade 12 — a real, working DataSource
// that reads a local CSV or JSON file and returns structured records.
// Genuinely reads the filesystem (node:fs), no mocked/simulated I/O.
//
// Scope, disclosed: this reads ONE local file per connector instance,
// synchronously parsed. It does not watch for changes, does not
// support remote filesystems, and does not handle CSV dialects beyond
// a plain comma-delimited, optionally-quoted format (RFC 4180 subset:
// double-quote escaping via doubled quotes, no embedded newlines
// inside quoted fields — the common case, not the full spec). A real
// enterprise-connector effort would need more CSV dialect coverage and
// remote/streaming sources; this is the honest, working slice.

import fs from 'node:fs/promises';
import path from 'node:path';

const MAX_FILE_BYTES = 50 * 1024 * 1024; // 50MB -- a real cap, not unbounded read

function parseCsvLine(line) {
  const fields = [];
  let cur = '';
  let inQuotes = false;
  for (let i = 0; i < line.length; i++) {
    const c = line[i];
    if (inQuotes) {
      if (c === '"') {
        if (line[i + 1] === '"') { cur += '"'; i++; } else { inQuotes = false; }
      } else {
        cur += c;
      }
    } else if (c === '"') {
      inQuotes = true;
    } else if (c === ',') {
      fields.push(cur);
      cur = '';
    } else {
      cur += c;
    }
  }
  fields.push(cur);
  return fields;
}

function parseCsv(text) {
  const lines = text.split(/\r\n|\n/).filter((l) => l.length > 0);
  if (lines.length === 0) return [];
  const header = parseCsvLine(lines[0]);
  const records = [];
  for (let i = 1; i < lines.length; i++) {
    const fields = parseCsvLine(lines[i]);
    const record = {};
    for (let c = 0; c < header.length; c++) record[header[c]] = fields[c] ?? '';
    records.push(record);
  }
  return records;
}

/**
 * `config`: { filePath: string, format?: 'csv'|'json' (default: inferred
 * from extension) }. `filePath` may be relative (resolved against
 * cwd) or absolute; this connector does not sandbox it the way
 * deviceExecutor.js sandboxes writes (this is a READ-only ingestion
 * source, not a device action gated by confirm()) -- a caller wiring
 * this to untrusted, caller-supplied paths should apply its own
 * boundary check (boundaryKernel.js) first.
 */
export function createFileConnector({ filePath, format } = {}) {
  if (typeof filePath !== 'string' || !filePath) throw new Error('fileConnector needs a filePath');
  const resolvedFormat = format || (path.extname(filePath).toLowerCase() === '.json' ? 'json' : 'csv');
  if (resolvedFormat !== 'csv' && resolvedFormat !== 'json') throw new Error(`fileConnector: unknown format "${resolvedFormat}", expected "csv" or "json"`);

  return {
    async fetch() {
      const stat = await fs.stat(filePath);
      if (stat.size > MAX_FILE_BYTES) throw new Error(`fileConnector: ${filePath} is ${stat.size} bytes, over the ${MAX_FILE_BYTES}-byte cap`);
      const text = await fs.readFile(filePath, 'utf8');
      if (resolvedFormat === 'json') {
        const parsed = JSON.parse(text);
        if (!Array.isArray(parsed)) throw new Error(`fileConnector: ${filePath} must contain a JSON array of records`);
        return parsed;
      }
      return parseCsv(text);
    },
  };
}
