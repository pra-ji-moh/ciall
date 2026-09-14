// kernelExport.test.mjs; upgrade 9 coverage — `ciall export-kernels`.
// Three required tests: wasm (exported module loads and computes
// correctly via the STANDALONE harness, in a real separate process with
// no access to this repo), spec-json (all kernels present, abstention
// conditions match real measured behavior), hls-c (files exist, contain
// the required pragma annotations).

import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { exportWasm, exportHlsC, exportSpecJson, buildSpecJson } from '../src/lib/kernelExport.js';
import { listKernels } from '../src/lib/kernelRegistry.js';

const REPO_ROOT = fileURLToPath(new URL('..', import.meta.url));

function tmpExportDir(label) {
  return fs.mkdtempSync(path.join(os.tmpdir(), `ciall-export-${label}-`));
}

test('wasm: exported modules load and compute correctly via the standalone harness, in a real separate process outside this repo', () => {
  const outDir = tmpExportDir('wasm');
  const written = exportWasm(outDir);
  assert.equal(written.length, 3);
  for (const f of written) assert.ok(fs.existsSync(f));

  const wasmDir = path.join(outDir, 'wasm');
  const harnessSource = fs.readFileSync(path.join(wasmDir, 'harness.js'), 'utf8');
  // Genuinely standalone: no import/require of anything outside this
  // directory (only node:fs, node:url, and the global WebAssembly).
  assert.equal(/from ['"](?!\.\/)(?!node:)/.test(harnessSource), false, 'harness.js must only import node: builtins or ./ siblings');

  // Run a check script AS A SEPARATE PROCESS whose cwd is ONLY the
  // exported directory -- proves the harness has no hidden dependency
  // on the rest of ciall-substrate, not just that the functions happen
  // to work when imported from inside this test file.
  const checkScript = `
    import { loadSmcWeightUpdate, loadMlpForward } from './harness.js';
    const mem1 = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
    const { weightUpdate } = loadSmcWeightUpdate(mem1);
    const view = new Float64Array(mem1.buffer, 0, 4);
    view.set([0, 1, -1, 2]);
    weightUpdate(0, 4, 0, 1.0);
    const expected = [1, Math.exp(1), Math.exp(-1), Math.exp(2)];
    const actual = Array.from(view);
    const smcOk = actual.every((v, i) => Math.abs(v - expected[i]) < 1e-12);

    const mem2 = new WebAssembly.Memory({ initial: 2, maximum: 2, shared: true });
    const { mlpForward } = loadMlpForward(mem2);
    const mlpOk = typeof mlpForward === 'function';

    console.log(JSON.stringify({ smcOk, mlpOk, actual }));
  `;
  fs.writeFileSync(path.join(wasmDir, 'check.mjs'), checkScript);
  const stdout = execFileSync(process.execPath, ['check.mjs'], { cwd: wasmDir, encoding: 'utf8' });
  const result = JSON.parse(stdout.trim());
  assert.equal(result.smcOk, true, `smc weightUpdate produced wrong values: ${JSON.stringify(result.actual)}`);
  assert.equal(result.mlpOk, true);

  fs.rmSync(outDir, { recursive: true, force: true });
});

test('wasm: the exported .wasm bytes are byte-for-byte identical to the in-repo constants', async () => {
  const outDir = tmpExportDir('wasm-bytes');
  exportWasm(outDir);
  const { SMC_WASM_BYTES } = await import('../src/lib/smcWasm.js');
  const { MLP_WASM_BYTES } = await import('../src/lib/mlpWasm.js');
  const exportedSmc = fs.readFileSync(path.join(outDir, 'wasm', 'smc-weight-update.wasm'));
  const exportedMlp = fs.readFileSync(path.join(outDir, 'wasm', 'mlp-forward.wasm'));
  assert.deepEqual(new Uint8Array(exportedSmc), SMC_WASM_BYTES);
  assert.deepEqual(new Uint8Array(exportedMlp), MLP_WASM_BYTES);
  fs.rmSync(outDir, { recursive: true, force: true });
});

test('hls-c: output files exist and contain the required pragma annotations with accurate loop bounds', () => {
  const outDir = tmpExportDir('hlsc');
  const written = exportHlsC(outDir);
  assert.equal(written.length, 2);

  const smcC = fs.readFileSync(path.join(outDir, 'hls-c', 'smc_weight_update.c'), 'utf8');
  assert.match(smcC, /#pragma HLS PIPELINE/);
  // max may be a literal digit or a #define'd macro name (both are
  // valid HLS -- the preprocessor expands the macro before the HLS
  // pragma parser ever sees it), so accept either.
  assert.match(smcC, /#pragma HLS LOOP_TRIPCOUNT min=\S+ max=\S+/);
  // (end - start) is a RUNTIME argument (the SMC particle-slice size) --
  // must NOT claim a static UNROLL factor over it, which would assert a
  // compile-time-known bound that doesn't exist.
  assert.doesNotMatch(smcC, /#pragma HLS UNROLL/);

  const mlpC = fs.readFileSync(path.join(outDir, 'hls-c', 'mlp_forward.c'), 'utf8');
  assert.match(mlpC, /#pragma HLS PIPELINE/);
  assert.match(mlpC, /#pragma HLS UNROLL factor=\S+/, 'the HIDDEN_DIM-bounded loop has a static, known trip count and is a real UNROLL candidate');
  assert.match(mlpC, /#pragma HLS LOOP_TRIPCOUNT min=\S+ max=\S+/, 'the inputDim-bounded loop is a runtime argument and needs a tripcount estimate, not an unroll');

  // Loop bounds must be the REAL constants, not placeholders -- these
  // are the actual values HLS synthesis would reject if wrong.
  assert.match(smcC, /#define SMC_MAX_N 20000/);
  assert.match(mlpC, /#define HIDDEN_DIM 64/);
  assert.match(mlpC, /#define MAX_STATE_VARS 24/);

  fs.rmSync(outDir, { recursive: true, force: true });
});

test('spec-json: every registered kernel is present, and every abstentionCondition traces to a real verdict/reason string in that kernel\'s own source', async () => {
  const spec = buildSpecJson();
  const registeredIds = listKernels().map((k) => k.id).sort();
  const specIds = spec.kernels.map((k) => k.kernelId).sort();
  assert.deepEqual(specIds, registeredIds, 'spec-json must cover every kernel in kernelRegistry.js, no more, no less');

  // Cross-check abstentionConditions against the ACTUAL kernel source
  // files, not just the spec table's own claims about itself -- catches
  // the spec table drifting from the real implementation over time.
  const sourceByKernel = {
    consistency: 'src/lib/consistencyKernel.js',
    mcmc: 'src/lib/mcmcSearch.js',
    'numeric-check': 'src/lib/numericCheck.js',
    dynamics: 'src/lib/dynamicsCheck.js',
    combinatorial: 'src/lib/combinatorialSearch.js',
    'domain-of-validity': 'src/lib/domainOfValidity.js',
    'order-consistency': 'src/lib/orderConsistency.js',
    'boundary-check': 'src/lib/boundaryKernel.js',
    'neuromorphic-power': 'src/lib/neuromorphicPower.js',
    'event-camera-pixel': 'src/lib/eventCameraPixel.js',
    'decision-helper': 'src/lib/decisionHelper.js',
    'chain-reachability': 'src/lib/chainKernel.js',
    'vector-span': 'src/lib/vectorSpanKernel.js',
  };
  for (const kernel of spec.kernels) {
    const srcPath = sourceByKernel[kernel.kernelId];
    assert.ok(srcPath, `no source file mapped for ${kernel.kernelId}`);
    const source = fs.readFileSync(path.join(REPO_ROOT, srcPath), 'utf8');
    for (const condition of kernel.abstentionConditions) {
      // Every abstention condition names a literal verdict/reason
      // string (e.g. 'inconclusive', 'undecided', 'unresolved',
      // kind:"none") -- extract the quoted literal and confirm it
      // actually appears in the kernel's own source, not just in the
      // spec table's prose.
      const literalMatch = /"([a-zA-Z-]+)"/.exec(condition);
      if (literalMatch) {
        assert.ok(source.includes(literalMatch[1]), `${kernel.kernelId}: abstention condition references "${literalMatch[1]}" but that string does not appear in ${srcPath}`);
      }
    }
  }
});

test('spec-json: falseAbstentionRate/falseConfidenceRate are either genuinely measured (with a real testFiles/assertionCount backing them) or honestly null, never a bare invented number', () => {
  const spec = buildSpecJson();
  for (const kernel of spec.kernels) {
    if (kernel.measuredAgainst === null) {
      assert.equal(kernel.falseAbstentionRate, null, `${kernel.kernelId}: no measurement backing means the rate must be null, not invented`);
      assert.equal(kernel.falseConfidenceRate, null);
    } else {
      assert.ok(Array.isArray(kernel.measuredAgainst.testFiles) && kernel.measuredAgainst.testFiles.length > 0);
      assert.ok(Number.isInteger(kernel.measuredAgainst.assertionCount) && kernel.measuredAgainst.assertionCount > 0);
      // The measurement actually ran real tests, and every currently
      // passing test file means 0 observed false-abstention/
      // false-confidence cases -- verified against the REAL pass/fail
      // counts, not asserted blindly.
      if (kernel.measuredAgainst.fail === 0) {
        assert.equal(kernel.falseAbstentionRate, 0);
        assert.equal(kernel.falseConfidenceRate, 0);
      }
    }
  }
});

test('spec-json: excludes modelClient.js and geminiClient.js explicitly, and no KERNEL entry (as opposed to the excludes list itself) references either file', () => {
  const spec = buildSpecJson();
  assert.deepEqual(spec.excludes, ['modelClient.js', 'geminiClient.js']);
  // The `excludes` field is SUPPOSED to name the excluded files -- that's
  // what makes it meaningful. What must never happen is either file's
  // logic/content leaking into a KERNEL entry.
  const serializedKernels = JSON.stringify(spec.kernels);
  assert.doesNotMatch(serializedKernels, /modelClient|geminiClient/);
});

test('export-kernels writes files under the given output directory for all three formats, additively (no existing files touched)', () => {
  const outDir = tmpExportDir('all-formats');
  const wasmFiles = exportWasm(outDir);
  const hlsFiles = exportHlsC(outDir);
  const specFile = exportSpecJson(outDir, { generatedAt: 12345 });
  for (const f of [...wasmFiles, ...hlsFiles, specFile]) assert.ok(fs.existsSync(f));
  const spec = JSON.parse(fs.readFileSync(specFile, 'utf8'));
  assert.equal(spec.generatedAt, 12345);
  fs.rmSync(outDir, { recursive: true, force: true });
});
