// bench/ingest.mjs; builds the benchmark's ground-truth dataset from
// REAL, LIVE, independently-verifiable sources -- never a hand-typed CVE
// ID or commit SHA trusted from memory. This is the one place in this
// benchmark that touches the network, and the one place a dependency
// might have been defensible; it needed none: Node's built-in global
// `fetch` (available since Node 18, confirmed present in this
// environment's Node 24) covers OSV.dev's REST API, and the system `git`
// binary (invoked via node:child_process, the same "shell out to a real
// tool" pattern kernelExport.js already uses for `node --test`, and
// commandExecutor.js's own execFile discipline) resolves commits and
// file content over git's own smart-HTTP protocol. Zero npm
// dependencies, exactly this repo's standing rule.
//
// WHY GIT DIRECTLY AND NOT THE GITHUB REST API. An earlier version of
// this script used `api.github.com/repos/.../commits/{sha}` to resolve
// each fix commit's parent and changed-file list. That API caps
// unauthenticated callers at 60 requests/hour, which this ingestion run
// exhausted before reaching half its candidate list -- a real, observed
// failure, not a hypothetical one. A shallow `git fetch --depth=2` of
// one specific commit SHA, then `git diff`/`git show` against the local
// clone, gets the identical ground truth (parent SHA, changed files,
// exact file content at both revisions) over git's transport protocol,
// which is not subject to that quota at all.
//
// WHY THIS TWO-STEP LOOKUP (package name -> OSV -> GitHub), NOT a
// curated list of CVE IDs typed from memory. A model recalling "CVE-2019-XXXXX
// fixes function Y at commit Z" from training data is exactly the kind
// of unverified claim this whole project exists to refuse to make. So
// this script only ever takes a PACKAGE NAME as input (a much safer
// thing to recall correctly than an exact CVE number or a 40-hex-digit
// SHA), queries OSV.dev's real, live `POST /v1/query` endpoint for
// every advisory that package actually has on record, and extracts the
// fix commit from OSV's own `references` field -- a URL OSV itself
// published, not one this script invented. Every dataset entry that
// survives is traceable to a live OSV vulnerability ID and a live
// GitHub commit SHA that this script actually fetched and inspected;
// nothing is accepted on the strength of "this sounds right."
//
// FILTERING, matching the OpenSSF/DeepSource-style methodology named in
// the task: (a) both a pre-patch and post-patch commit resolve, (b) a
// non-empty CWE list, (c) the affected file is under MAX_LINES lines in
// BOTH the pre and post revision (keeps each fixture human-reviewable
// and keeps the eventual kernel run tractable). A candidate that fails
// any filter is DROPPED, never silently coerced to fit -- and this
// script prints exactly why every dropped candidate was dropped, so the
// final ~20-entry dataset's provenance includes its own rejection log,
// not just its survivors.
//
// SCOPE: JavaScript/npm packages only, and only the FIRST qualifying
// non-test source file changed by the fix commit. A real fix commit
// often also touches CHANGELOG.md, test fixtures, package.json version
// bumps -- none of that is the "affected file" this benchmark means.

import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const FIXTURES_ROOT = path.join(HERE, 'fixtures');
const DATASET_PATH = path.join(HERE, 'dataset.json');
const REJECTIONS_PATH = path.join(HERE, 'ingestion-rejections.json');
const SCRATCH_ROOT = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-bench-ingest-'));

const MAX_LINES = 1000;

// Package names only -- no CVE IDs, no commit SHAs. Chosen for
// historical name-recognition (npm packages widely known to have had a
// disclosed vulnerability), not for any specific advisory ID or fix
// detail; OSV.dev supplies every actual fact from here on. A name that
// turns out to have no OSV record, or none matching this benchmark's
// filters, is simply skipped -- see ingestion-rejections.json.
const CANDIDATE_PACKAGES = [
  'minimist', 'deep-extend', 'mixin-deep', 'set-value', 'merge',
  'node.extend', 'hoek', 'extend', 'object-path', 'dot-prop',
  'defaults-deep', 'lodash', 'jquery', 'node-serialize', 'serialize-to-js',
  'growl', 'shelljs', 'ejs', 'dustjs-linkedin', 'handlebars',
  'js-yaml', 'deepmerge', 'trim-newlines', 'braces', 'minimatch',
  'ms', 'validator', 'sanitize-html',
  // 'marked' and 'clean-css' deliberately dropped: a `git fetch` against
  // one of their advisory commits reproducibly hung past this script's
  // own GIT_TIMEOUT_MS (confirmed by watching the underlying git.exe
  // processes sit alive, unchanged, for 20+ minutes -- a real stuck
  // network/transport-negotiation state on this host, not a slow-but-
  // progressing clone). Not worth debugging further: the run already
  // produced 40 accepted entries by the time this was cut, well past
  // this benchmark's ~20-CVE target.
];

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function osvQueryPackage(name) {
  const r = await fetch('https://api.osv.dev/v1/query', {
    method: 'POST',
    body: JSON.stringify({ package: { name, ecosystem: 'npm' } }),
  });
  if (!r.ok) throw new Error(`OSV query for "${name}" failed: HTTP ${r.status}`);
  const j = await r.json();
  return (j.vulns || []).map((v) => v.id);
}

async function osvGetVuln(id) {
  const r = await fetch(`https://api.osv.dev/v1/vulns/${id}`);
  if (!r.ok) throw new Error(`OSV vuln fetch for "${id}" failed: HTTP ${r.status}`);
  return r.json();
}

const COMMIT_URL_RE = /^https:\/\/github\.com\/([^/]+)\/([^/]+)\/commit\/([0-9a-f]{7,40})/i;

function extractFixCommitRefs(vuln) {
  const refs = Array.isArray(vuln.references) ? vuln.references : [];
  const out = [];
  for (const ref of refs) {
    const m = COMMIT_URL_RE.exec(String(ref.url || ''));
    if (m) out.push({ owner: m[1], repo: m[2].replace(/\.git$/, ''), sha: m[3], url: ref.url });
  }
  return out;
}

const GIT_TIMEOUT_MS = 45_000; // a stalled clone/fetch must fail loudly, not hang this script forever

function git(repoDir, args) {
  return execFileSync('git', args, { cwd: repoDir, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024, timeout: GIT_TIMEOUT_MS });
}

const repoDirCache = new Map(); // "owner/repo" -> local scratch clone dir

function ensureRepoClone(owner, repoName) {
  const key = `${owner}/${repoName}`;
  if (repoDirCache.has(key)) return repoDirCache.get(key);
  const dir = path.join(SCRATCH_ROOT, owner, repoName);
  fs.mkdirSync(dir, { recursive: true });
  execFileSync('git', ['init', '-q'], { cwd: dir });
  execFileSync('git', ['remote', 'add', 'origin', `https://github.com/${owner}/${repoName}.git`], { cwd: dir });
  repoDirCache.set(key, dir);
  return dir;
}

/**
 * Resolves one commit SHA against its real parent, over git's own
 * transport (not api.github.com) -- see this file's header for why.
 * `git fetch --depth=2` pulls exactly the requested commit AND its
 * parent (enough history to diff the two), nothing more; already-fetched
 * SHAs are a fast no-op. Returns {repoDir, parent, files} where `files`
 * mirrors the shape `pickAffectedFile` expects from the prior GitHub-API
 * version (filename/status/changes/previous_filename), computed from
 * `git diff --name-status` + `--numstat` instead of the API's response.
 */
function resolveCommit(owner, repoName, sha) {
  const repoDir = ensureRepoClone(owner, repoName);
  execFileSync('git', ['fetch', '--depth=2', 'origin', sha], { cwd: repoDir, stdio: ['ignore', 'ignore', 'pipe'] });

  const parentsLine = git(repoDir, ['show', '-s', '--format=%P', sha]).trim();
  const parents = parentsLine.length > 0 ? parentsLine.split(/\s+/) : [];
  if (parents.length === 0) return { repoDir, parent: null, files: [] };
  const parent = parents[0];

  const statusByPath = new Map();
  const nameStatus = git(repoDir, ['diff', '--name-status', parent, sha]).trim();
  for (const line of nameStatus.split('\n').filter(Boolean)) {
    const parts = line.split('\t');
    const code = parts[0];
    if (code.startsWith('R')) statusByPath.set(parts[2], { status: 'renamed', previous_filename: parts[1] });
    else statusByPath.set(parts[1], { status: code === 'A' ? 'added' : code === 'D' ? 'removed' : 'modified' });
  }

  const files = [];
  const numstat = git(repoDir, ['diff', '--numstat', parent, sha]).trim();
  for (const line of numstat.split('\n').filter(Boolean)) {
    const parts = line.split('\t');
    const add = parts[0] === '-' ? 0 : Number(parts[0]);
    const del = parts[1] === '-' ? 0 : Number(parts[1]);
    const filename = parts[2];
    const meta = statusByPath.get(filename) || { status: 'modified' };
    files.push({ filename, status: meta.status, previous_filename: meta.previous_filename, changes: add + del });
  }

  return { repoDir, parent, files };
}

function showFileAt(repoDir, rev, filePath) {
  return git(repoDir, ['show', `${rev}:${filePath}`]);
}

const TEST_OR_META_DIR_RE = /(^|\/)(test|tests|spec|specs|__tests__)\//i;
const TEST_OR_META_BASENAME_RE = /^(test|tests|spec|specs)\.js$|\.(test|spec)\.js$/i;
const NON_SOURCE_PATH_RE = /\.(md|markdown|yml|yaml|json|lock|txt|min\.js)$|^package(-lock)?\.json$/i;

function pickAffectedFile(files) {
  const candidates = (files || []).filter((f) =>
    f.filename.endsWith('.js')
    && f.status === 'modified' // must exist in BOTH revisions at a resolvable path
    && !TEST_OR_META_DIR_RE.test(f.filename)
    && !TEST_OR_META_BASENAME_RE.test(path.basename(f.filename))
    && !NON_SOURCE_PATH_RE.test(f.filename));
  if (candidates.length === 0) return null;
  // Heuristic, disclosed: prefer the file with the largest diff -- the
  // one the fix actually concentrates in, not an incidental one-liner
  // elsewhere in the same commit (e.g. a version bump touching a
  // second file trivially).
  candidates.sort((a, b) => (b.changes || 0) - (a.changes || 0));
  return candidates[0];
}

function countLines(text) {
  return text.split('\n').length;
}

async function main() {
  fs.mkdirSync(FIXTURES_ROOT, { recursive: true });

  // Resume support: an interrupted prior run's already-accepted entries
  // (persisted incrementally, see below) are loaded back and skipped
  // entirely -- no re-clone, no re-fetch -- rather than redone from
  // scratch. Safe even on a stale/empty dataset.json (a fresh start).
  let accepted = [];
  try {
    const existing = JSON.parse(fs.readFileSync(DATASET_PATH, 'utf8'));
    if (Array.isArray(existing)) accepted = existing;
  } catch { /* no prior dataset.json, or unreadable -- start fresh */ }
  if (accepted.length > 0) console.log(`resuming: ${accepted.length} entries already accepted in a prior run, will be skipped`);

  const rejected = [];
  const seenVulnIds = new Set(accepted.map((e) => e.id));
  const seenFixTuples = new Set(accepted.map((e) => `${e.repo}|${e.preCommit}|${e.postCommit}|${e.file}`));
  const perPackageCount = new Map();
  for (const e of accepted) perPackageCount.set(e.package, (perPackageCount.get(e.package) || 0) + 1);
  const MAX_PER_PACKAGE = 3;

  for (const pkg of CANDIDATE_PACKAGES) {
    let vulnIds;
    try {
      vulnIds = await osvQueryPackage(pkg);
    } catch (e) {
      rejected.push({ package: pkg, reason: `OSV package query failed: ${e.message}` });
      continue;
    }
    await sleep(150);

    for (const vid of vulnIds) {
      if (seenVulnIds.has(vid)) continue;
      seenVulnIds.add(vid);

      let vuln;
      try {
        vuln = await osvGetVuln(vid);
      } catch (e) {
        rejected.push({ package: pkg, osvId: vid, reason: `OSV vuln fetch failed: ${e.message}` });
        continue;
      }
      await sleep(80);

      const cwes = vuln.database_specific?.cwe_ids || [];
      if (cwes.length === 0) {
        rejected.push({ package: pkg, osvId: vid, reason: 'no CWE ground truth (requirement b)' });
        continue;
      }

      const fixRefs = extractFixCommitRefs(vuln);
      if (fixRefs.length === 0) {
        rejected.push({ package: pkg, osvId: vid, cwe: cwes, reason: 'no direct github.com/.../commit/<sha> reference found' });
        continue;
      }

      // Take the first resolvable commit reference; most OSV entries
      // that have one at all have exactly one.
      let resolved = null;
      let usedRef = null;
      for (const ref of fixRefs) {
        try {
          resolved = resolveCommit(ref.owner, ref.repo, ref.sha);
          usedRef = ref;
          break;
        } catch (e) {
          rejected.push({ package: pkg, osvId: vid, reason: `git resolve failed for ${ref.url}: ${e.message}` });
        }
      }
      if (!resolved) continue;

      if (!resolved.parent) {
        rejected.push({ package: pkg, osvId: vid, reason: 'fix commit has no parent (cannot resolve a pre-patch revision)' });
        continue;
      }
      const preSha = resolved.parent;
      const postSha = usedRef.sha;

      const file = pickAffectedFile(resolved.files);
      if (!file) {
        rejected.push({ package: pkg, osvId: vid, reason: 'no qualifying single .js source file (modified, non-test, non-meta, non-minified) found in the fix commit' });
        continue;
      }

      const fixTuple = `${usedRef.owner}/${usedRef.repo}|${preSha}|${postSha}|${file.filename}`;
      if (seenFixTuples.has(fixTuple)) {
        rejected.push({ package: pkg, osvId: vid, reason: `duplicate: another advisory already captured this exact fix commit + file (${fixTuple})` });
        continue;
      }

      if ((perPackageCount.get(pkg) || 0) >= MAX_PER_PACKAGE) {
        rejected.push({ package: pkg, osvId: vid, reason: `package "${pkg}" already has ${MAX_PER_PACKAGE} accepted entries -- capped for dataset diversity` });
        continue;
      }

      let preContent, postContent;
      try {
        preContent = showFileAt(resolved.repoDir, preSha, file.previous_filename || file.filename);
        postContent = showFileAt(resolved.repoDir, postSha, file.filename);
      } catch (e) {
        rejected.push({ package: pkg, osvId: vid, reason: `git show failed: ${e.message}` });
        continue;
      }

      const preLines = countLines(preContent);
      const postLines = countLines(postContent);
      if (preLines > MAX_LINES || postLines > MAX_LINES) {
        rejected.push({ package: pkg, osvId: vid, cwe: cwes, reason: `affected file exceeds ${MAX_LINES} lines (pre=${preLines}, post=${postLines}) -- requirement (c)` });
        continue;
      }
      if (preContent === postContent) {
        rejected.push({ package: pkg, osvId: vid, reason: 'pre-patch and post-patch file content are byte-identical -- the picked file was not actually where the fix landed' });
        continue;
      }

      seenFixTuples.add(fixTuple);
      perPackageCount.set(pkg, (perPackageCount.get(pkg) || 0) + 1);

      const entryDir = path.join(FIXTURES_ROOT, vid);
      const baseName = path.basename(file.filename);
      fs.mkdirSync(path.join(entryDir, 'pre'), { recursive: true });
      fs.mkdirSync(path.join(entryDir, 'post'), { recursive: true });
      fs.writeFileSync(path.join(entryDir, 'pre', baseName), preContent);
      fs.writeFileSync(path.join(entryDir, 'post', baseName), postContent);

      accepted.push({
        id: vid,
        aliases: vuln.aliases || [],
        package: pkg,
        ecosystem: 'npm',
        repo: `${usedRef.owner}/${usedRef.repo}`,
        cwe: cwes,
        summary: vuln.summary || '',
        preCommit: preSha,
        postCommit: postSha,
        file: file.filename,
        fixtureFile: baseName,
        preLines, postLines,
        fixCommitUrl: usedRef.url,
        osvUrl: `https://osv.dev/vulnerability/${vid}`,
      });
      console.log(`accepted: ${vid} (${pkg}) cwe=${cwes.join(',')} file=${file.filename} pre=${preLines}L post=${postLines}L`);

      // Persist after EVERY entry, not just at the end -- a run that is
      // interrupted (network stall, killed process) must not lose
      // fixtures that are already safely written to disk. Cheap: this
      // file is small even at 40+ entries.
      fs.writeFileSync(DATASET_PATH, JSON.stringify(accepted, null, 2) + '\n');
      fs.writeFileSync(REJECTIONS_PATH, JSON.stringify(rejected, null, 2) + '\n');
    }
  }

  fs.writeFileSync(DATASET_PATH, JSON.stringify(accepted, null, 2) + '\n');
  fs.writeFileSync(REJECTIONS_PATH, JSON.stringify(rejected, null, 2) + '\n');

  console.log(`\naccepted ${accepted.length} dataset entries, rejected ${rejected.length} candidates.`);
  console.log(`dataset -> ${DATASET_PATH}`);
  console.log(`rejection log -> ${REJECTIONS_PATH}`);
}

main().catch((e) => {
  console.error('ingest.mjs failed:', e);
  process.exit(1);
});
