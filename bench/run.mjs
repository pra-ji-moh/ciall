// bench/run.mjs; the actual benchmark run. Loads the pinned
// dataset.json + fixtures (committed, offline, no network needed here
// -- only bench/ingest.mjs touches the network), runs selfAudit.js's
// real auditSourceTree against every pre-patch and post-patch fixture,
// grades it via bench/harness.mjs's pure scoring rules, marks every
// other registered kernel not-applicable (see harness.mjs's header for
// why that is the honest answer, not a cop-out), and writes
// bench/REPORT.json + bench/REPORT.md.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { auditSourceTree } from '../src/lib/selfAudit.js';
import {
  gradeSelfAuditAcrossDataset,
  notApplicableRowsForDataset,
  structurallyNotApplicableKernelIds,
  aggregate,
  SELF_AUDIT_KERNEL_ID,
} from './harness.mjs';
import { verifyDatasetBaseline, DATASET_PATH, FIXTURES_ROOT } from './datasetBaseline.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPORT_JSON_PATH = path.join(HERE, 'REPORT.json');
const REPORT_MD_PATH = path.join(HERE, 'REPORT.md');

function collectFindings(dir) {
  if (!fs.existsSync(dir)) return [];
  return auditSourceTree(dir).findings;
}

function runSelfAuditOverFixtures(dataset) {
  const findingsByEntry = {};
  for (const entry of dataset) {
    const preDir = path.join(FIXTURES_ROOT, entry.id, 'pre');
    const postDir = path.join(FIXTURES_ROOT, entry.id, 'post');
    const postFindings = collectFindings(postDir);
    findingsByEntry[entry.id] = {
      preCodes: collectFindings(preDir).map((f) => f.code),
      postCodes: postFindings.map((f) => f.code),
      postFindings, // full objects, incl. guardDetected/usesShellInterpolation -- see harness.mjs's annotation-aware secondary metric
    };
  }
  return findingsByEntry;
}

function formatPct(x) {
  return x === null ? 'n/a' : `${(x * 100).toFixed(1)}%`;
}

function renderMarkdown({ integrity, dataset, perKernel, notCoveredByCwe }) {
  const lines = [];
  lines.push('# ciall-substrate CVE benchmark report');
  lines.push('');
  lines.push(`Dataset: ${dataset.length} CVE/GHSA entries, pinned at \`bench/dataset.json\` (recorded ${integrity.recordedAt ? new Date(integrity.recordedAt).toISOString() : 'unknown'}).`);
  lines.push(`Dataset integrity: **${integrity.verified ? 'VERIFIED against pinned baseline' : 'NOT VERIFIED -- ' + (integrity.reason || 'fixture/dataset content has drifted from the recorded baseline')}**.`);
  lines.push('');
  lines.push('## Per-kernel breakdown (no blended score -- see bench/FINDINGS.md for why)');
  lines.push('');
  lines.push('| kernel | status | covered | TP | FN | FP | precision | recall |');
  lines.push('|---|---|---|---|---|---|---|---|');
  for (const k of perKernel) {
    lines.push(`| ${k.kernelId} | ${k.status} | ${k.summary.covered}/${dataset.length} | ${k.summary.tp} | ${k.summary.fn} | ${k.summary.fp} | ${formatPct(k.summary.precision)} | ${formatPct(k.summary.recall)} |`);
  }
  lines.push('');
  lines.push('"covered" = how many dataset entries even have a rule this kernel claims could apply (selfAudit\'s CWE table for self-audit; zero, structurally, for every other kernel). precision/recall are computed ONLY over covered entries -- an excluded entry is not a zero.');
  lines.push('');
  if (perKernel[0].annotationAwareSummary) {
    const a = perKernel[0].annotationAwareSummary;
    lines.push('## Secondary lens: annotation-aware precision (self-audit only)');
    lines.push('');
    lines.push(`selfAudit's \`guardDetected\`/\`usesShellInterpolation\` fields (see selfAudit.js's header) let a human deprioritize a post-patch finding that LOOKS fixed. This does NOT change the strict primary metric above -- it answers a different, additional question: "if a human used these annotations, how many of the strict FPs would they have correctly set aside?"`);
    lines.push('');
    lines.push(`Strict FP: ${perKernel[0].summary.fp}. Annotation-aware FP: ${a.fp} (precision ${formatPct(perKernel[0].summary.precision)} -> ${formatPct(a.precision)} over the same ${a.covered} covered entries).`);
    lines.push('');
  }
  lines.push('## CWE classes with zero selfAudit coverage in this dataset');
  lines.push('');
  for (const [cwe, count] of notCoveredByCwe) lines.push(`- ${cwe}: ${count} dataset entr${count === 1 ? 'y' : 'ies'}, no selfAudit rule targets it`);
  lines.push('');
  lines.push('See `bench/FINDINGS.md` for the full write-up (which kernels are structurally not-applicable and why, per-rule noise, and what a real recall number here does and does not mean).');
  return lines.join('\n') + '\n';
}

function main() {
  if (!fs.existsSync(DATASET_PATH)) {
    console.error(`no dataset found at ${DATASET_PATH} -- run bench/ingest.mjs first`);
    process.exit(1);
  }
  const dataset = JSON.parse(fs.readFileSync(DATASET_PATH, 'utf8'));
  const integrity = verifyDatasetBaseline();
  if (!integrity.verified) {
    console.warn(`WARNING: dataset integrity NOT verified (${integrity.reason || 'drift detected'}). Report will say so; results are still computed against whatever is on disk right now.`);
  }

  const findingsByEntry = runSelfAuditOverFixtures(dataset);
  const { rows: selfAuditRows, summary: selfAuditSummary, annotationAwareSummary } = gradeSelfAuditAcrossDataset(dataset, findingsByEntry);

  const notApplicableKernelIds = structurallyNotApplicableKernelIds();
  const perKernel = [
    { kernelId: SELF_AUDIT_KERNEL_ID, status: 'ran (pattern scan, no model)', summary: selfAuditSummary, rows: selfAuditRows, annotationAwareSummary },
    ...notApplicableKernelIds.map((id) => {
      const rows = notApplicableRowsForDataset(dataset, id);
      return { kernelId: id, status: 'not-applicable (needs model-based spec extraction)', summary: aggregate(rows), rows };
    }),
  ];

  const allRows = perKernel.flatMap((k) => k.rows);

  const notCoveredCounts = new Map();
  for (const entry of dataset) {
    const row = selfAuditRows.find((r) => r.cve_id === entry.id);
    if (row.verdict === 'not-covered') {
      for (const cwe of entry.cwe) notCoveredCounts.set(cwe, (notCoveredCounts.get(cwe) || 0) + 1);
    }
  }
  const notCoveredByCwe = [...notCoveredCounts.entries()].sort((a, b) => b[1] - a[1]);

  const report = {
    generatedAt: null, // stamped by the caller printing this, not computed inside pure logic -- see kernelExport.js's exportSpecJson for the same discipline
    datasetSize: dataset.length,
    datasetIntegrity: integrity,
    methodology: {
      trueP: 'kernel flags the ground-truth code/rule on the PRE-patch fixture',
      falseN: 'kernel does not flag it on the PRE-patch fixture',
      falseP: 'kernel flags the SAME code/rule on the POST-patch (fixed) fixture',
      precision: 'TP / (TP + FP)',
      recall: 'TP / (TP + FN)',
      notApplicable: 'a kernel that cannot run against raw source code at all without a model-based spec-extraction step this benchmark does not perform -- excluded from precision/recall, never scored as a miss',
      notCovered: 'the CWE class has no selfAudit rule targeting it at all -- excluded from precision/recall, never scored as a miss',
    },
    perKernelSummary: perKernel.map((k) => ({ kernelId: k.kernelId, status: k.status, ...k.summary, annotationAware: k.annotationAwareSummary || null })),
    rows: allRows,
  };

  fs.writeFileSync(REPORT_JSON_PATH, JSON.stringify(report, null, 2) + '\n');
  fs.writeFileSync(REPORT_MD_PATH, renderMarkdown({ integrity, dataset, perKernel, notCoveredByCwe }));

  console.log(`wrote ${REPORT_JSON_PATH}`);
  console.log(`wrote ${REPORT_MD_PATH}`);
  console.log('');
  console.log(`self-audit: covered=${selfAuditSummary.covered}/${dataset.length}  TP=${selfAuditSummary.tp} FN=${selfAuditSummary.fn} FP=${selfAuditSummary.fp}  precision=${formatPct(selfAuditSummary.precision)}  recall=${formatPct(selfAuditSummary.recall)}`);
  console.log(`${notApplicableKernelIds.length} other registered kernels: not-applicable (see bench/FINDINGS.md)`);
}

main();
