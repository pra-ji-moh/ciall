// bench/datasetBaseline.mjs; version-pins the benchmark dataset using
// the SAME tamper-evidence system selfAudit.js already has
// (recordBaseline/checkAgainstBaseline), reused completely unmodified.
//
// Why not just extend selfAudit's baseline to also cover dataset.json
// directly: selfAudit's `walkFiles` is deliberately scoped to
// `.js`/`.mjs` only (it is a JAVASCRIPT SOURCE scanner's baseline, not
// a general file-integrity tool), and `checkAgainstBaseline` re-derives
// the current file list with that SAME walker internally -- feeding it
// a manifest that includes a `.json` file would make that file show up
// as spuriously "removed" on every single check, a false tamper alarm
// manufactured by this benchmark, not a real one. So: selfAudit's
// baseline system is reused EXACTLY as designed, scoped to exactly what
// it already claims to cover (the fixture .js files -- the actual CVE
// source code, which is the part that matters for reproducibility), and
// dataset.json's own metadata is separately hashed with one direct
// SHA-256 call, using the same algorithm, stored alongside it.

import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { recordBaseline, checkAgainstBaseline } from '../src/lib/selfAudit.js';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const FIXTURES_ROOT = path.join(HERE, 'fixtures');
export const DATASET_PATH = path.join(HERE, 'dataset.json');
export const BASELINE_PATH = path.join(HERE, 'dataset-baseline.json');

function sha256File(p) {
  return crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
}

export function recordDatasetBaseline() {
  const fixturesBaseline = recordBaseline(FIXTURES_ROOT);
  const manifest = {
    version: 1,
    recordedAt: fixturesBaseline.createdAt,
    fixturesBaseline,
    datasetJsonSha256: sha256File(DATASET_PATH),
  };
  fs.writeFileSync(BASELINE_PATH, JSON.stringify(manifest, null, 2) + '\n');
  return manifest;
}

/**
 * Returns {verified, recordedAt, fixturesDiff, datasetJsonMatches}.
 * `verified` is true only if BOTH the fixture corpus is byte-identical
 * to what was recorded AND dataset.json itself is unchanged -- "we
 * scored X" means "against exactly this pinned input," not a moving
 * target.
 */
export function verifyDatasetBaseline() {
  if (!fs.existsSync(BASELINE_PATH)) {
    return { verified: false, reason: 'no baseline recorded yet -- run: node bench/datasetBaseline.mjs' };
  }
  const manifest = JSON.parse(fs.readFileSync(BASELINE_PATH, 'utf8'));
  const fixturesDiff = checkAgainstBaseline(FIXTURES_ROOT, manifest.fixturesBaseline);
  const datasetJsonMatches = sha256File(DATASET_PATH) === manifest.datasetJsonSha256;
  return {
    verified: fixturesDiff.clean && datasetJsonMatches,
    recordedAt: manifest.recordedAt,
    fixturesDiff,
    datasetJsonMatches,
  };
}

// `node bench/datasetBaseline.mjs` records fresh; imported as a module
// (bench/run.mjs), only the two functions above are used and nothing
// runs automatically.
if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const manifest = recordDatasetBaseline();
  const fileCount = Object.keys(manifest.fixturesBaseline.files).length;
  console.log(`recorded baseline: ${fileCount} fixture files hashed -> ${BASELINE_PATH}`);
  console.log(`dataset.json sha256=${manifest.datasetJsonSha256}`);
}
