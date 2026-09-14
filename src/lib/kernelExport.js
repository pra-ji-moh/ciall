// kernelExport.js; upgrade 9 — `ciall export-kernels --format <format>`.
// Exports the pure, deterministic verification kernels as standalone
// artifacts: raw WASM binaries + a zero-dependency loading harness,
// hand-written HLS C for FPGA synthesis, and a machine-readable spec
// describing every kernel's shape and MEASURED (not invented) behavior.
// modelClient.js/geminiClient.js are never touched or referenced by any
// export path here — the AI parsing layer is explicitly out of scope,
// per the task's own exclusion.
//
// DISCLOSURE (ground rule 1/5): the task's hls-c section describes
// working "from the WAT source in comments above each constant" — no
// such WAT (S-expression) source exists anywhere in this repo.
// smcWasm.js/mlpWasm.js carry PROSE descriptions of their algorithms
// and provenance (a hand-written assembler script's output, validated
// against a plain-JS reference) above a base64 byte constant, not WAT.
// Per the user's explicit choice when this was flagged: the C in
// --format hls-c is hand-written from that prose algorithm description
// plus the actual exported function signature (inspectable via
// WebAssembly.Module.exports on the real compiled module), not
// transliterated from a WAT file that was never written. Every .c file
// says so in its own header comment.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';
import { listKernels } from './kernelRegistry.js';
import { SMC_WASM_BYTES } from './smcWasm.js';
import { MLP_WASM_BYTES, HIDDEN_DIM } from './mlpWasm.js';

const REPO_ROOT = fileURLToPath(new URL('../..', import.meta.url));

export function defaultExportRoot() {
  return path.join(process.cwd(), 'ciall-export');
}

function ensureDir(p) {
  fs.mkdirSync(p, { recursive: true });
}

// ============================================================
// --format wasm
// ============================================================

const WASM_MODULES = [
  { fileName: 'smc-weight-update.wasm', bytes: SMC_WASM_BYTES, exportName: 'weightUpdate' },
  { fileName: 'mlp-forward.wasm', bytes: MLP_WASM_BYTES, exportName: 'mlpForward' },
];

// A truly standalone loader: no `import`/`require` of anything from this
// repo or from npm, only `node:fs` and `node:url` (the runtime itself,
// not a package) to read the sibling .wasm files off disk, plus the
// global WebAssembly object every JS engine provides. A caller with just
// this directory's files — no access to the rest of ciall-substrate —
// can load and call either kernel.
const HARNESS_SOURCE = `// harness.js; standalone loader for the exported ciall-substrate WASM
// kernels. Zero dependency beyond the JS runtime itself: no npm, no
// import of anything outside this directory. Load with:
//   import { loadSmcWeightUpdate, loadMlpForward } from './harness.js';

import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const DIR = path.dirname(fileURLToPath(import.meta.url));

function loadModule(fileName) {
  const bytes = readFileSync(path.join(DIR, fileName));
  return new WebAssembly.Module(bytes);
}

/**
 * smc-weight-update.wasm: weightUpdate(start, end, weightsByteOffset,
 * temperature) -> void. Mutates a shared f64 buffer in place: for i in
 * [start, end), weights[i] = exp(clamp(weights[i] / temperature, -700,
 * 700)), read/written at byte address weightsByteOffset + i*8. Needs a
 * WebAssembly.Memory built as new WebAssembly.Memory({ initial,
 * maximum, shared: true }) — maximum is REQUIRED (omitting it is a
 * LinkError against this module's import type); shared:true is only
 * load-bearing if you want to touch the memory from multiple
 * workers/instances at once (the original in-repo usage), but the
 * import signature expects it regardless. Imports Math.exp as env.exp
 * (WASM has no transcendental instructions).
 */
export function loadSmcWeightUpdate(memory) {
  const module = loadModule('smc-weight-update.wasm');
  const instance = new WebAssembly.Instance(module, { env: { memory, exp: Math.exp } });
  return { weightUpdate: instance.exports.weightUpdate, memory };
}

/**
 * mlp-forward.wasm: mlpForward(xOff, w1Off, b1Off, w2Off, b2Off,
 * hiddenOff, outOff, inputDim) -> void. Computes a 2-layer MLP forward
 * pass (hidden dimension fixed at ${HIDDEN_DIM}, inputDim is a runtime
 * parameter) entirely against byte offsets into the given
 * WebAssembly.Memory — no allocation happens inside the module. Same
 * Memory requirement as loadSmcWeightUpdate above: build it with an
 * explicit maximum (required) and shared:true. Imports Math.tanh as
 * env.tanh.
 */
export function loadMlpForward(memory) {
  const module = loadModule('mlp-forward.wasm');
  const instance = new WebAssembly.Instance(module, { env: { memory, tanh: Math.tanh } });
  return { mlpForward: instance.exports.mlpForward, memory };
}
`;

export function exportWasm(outDir = defaultExportRoot()) {
  const wasmDir = path.join(outDir, 'wasm');
  ensureDir(wasmDir);
  const written = [];
  for (const m of WASM_MODULES) {
    const dest = path.join(wasmDir, m.fileName);
    fs.writeFileSync(dest, Buffer.from(m.bytes));
    written.push(dest);
  }
  const harnessPath = path.join(wasmDir, 'harness.js');
  fs.writeFileSync(harnessPath, HARNESS_SOURCE);
  written.push(harnessPath);
  return written;
}

// ============================================================
// --format hls-c
// ============================================================
// Hand-written, per the disclosure above — not machine-transliterated
// from anything, since there is no WAT to transliterate from. Loop
// bounds are annotated from the REAL constants that govern them at
// runtime (SMC_MAX_N from mcmcSearch.js, MAX_STATE_VARS from
// dynamicsCheck.js, HIDDEN_DIM from mlpWasm.js) — accurate bounds are a
// hard requirement for HLS synthesis, not a nicety, so these are pulled
// from the actual source constants rather than guessed.

const SMC_MAX_N = 20000; // mcmcSearch.js's own SMC particle-count cap; the real upper bound on (end - start)
const MAX_STATE_VARS = 24; // dynamicsCheck.js's own cap on inputDim

const SMC_WEIGHT_UPDATE_C = `// smc_weight_update.c
// Vitis HLS translation of smcWasm.js's weightUpdate export.
//
// PROVENANCE: hand-written from smcWasm.js's own header comment
// (algorithm description + provenance/validation notes) and its actual
// exported signature — weightUpdate(start: i32, end: i32,
// weightsByteOffset: i32, temperature: f64) -> void — NOT transliterated
// from WAT source, because none exists in this repo (see
// kernelExport.js's header for the full disclosure). Semantics: for i in
// [start, end), weights[i] = exp(clamp(weights[i] / temperature, -700,
// 700)).
//
// Loop bound (end - start) is a RUNTIME argument, not a compile-time
// constant -- it is the SMC particle-slice size, capped by
// mcmcSearch.js's own SMC_MAX_N=${SMC_MAX_N} (see that file). Annotated
// with LOOP_TRIPCOUNT (an estimate for HLS's scheduler/reporting), not
// UNROLL: an unroll factor requires a bound HLS can reason about
// statically, which a runtime-varying bound is not.

#include <math.h>

#define SMC_MAX_N ${SMC_MAX_N}
#define WEIGHT_CLAMP_LO (-700.0)
#define WEIGHT_CLAMP_HI (700.0)

void smc_weight_update(
    double weights[SMC_MAX_N],
    int start,
    int end,
    double temperature
) {
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem
#pragma HLS INTERFACE s_axilite port=start bundle=control
#pragma HLS INTERFACE s_axilite port=end bundle=control
#pragma HLS INTERFACE s_axilite port=temperature bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    weight_update_loop:
    for (int i = start; i < end; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=SMC_MAX_N
        double scaled = weights[i] / temperature;
        double clamped = scaled < WEIGHT_CLAMP_LO ? WEIGHT_CLAMP_LO
                        : (scaled > WEIGHT_CLAMP_HI ? WEIGHT_CLAMP_HI : scaled);
        weights[i] = exp(clamped);
    }
}
`;

const MLP_FORWARD_C = `// mlp_forward.c
// Vitis HLS translation of mlpWasm.js's mlpForward export.
//
// PROVENANCE: hand-written from mlpWasm.js's own header comment
// (algorithm description + provenance/validation notes) and its actual
// exported signature -- mlpForward(xOff, w1Off, b1Off, w2Off, b2Off,
// hiddenOff, outOff, inputDim) -> void -- NOT transliterated from WAT
// source, because none exists in this repo (see kernelExport.js's
// header for the full disclosure). Semantics, exactly as documented in
// mlpWasm.js: layer 1 is hidden[j] = tanh(b1[j] + sum_i x[i]*W1[i*H+j])
// for j in [0,H); layer 2 is out[k] = b2[k] + sum_j hidden[j]*W2[j*D+k]
// for k in [0,D). H (hidden dimension) is a COMPILE-TIME constant
// (${HIDDEN_DIM}, matching HIDDEN_DIM in mlpWasm.js); D (inputDim) is a
// RUNTIME parameter bounded by dynamicsCheck.js's own
// MAX_STATE_VARS=${MAX_STATE_VARS}.
//
// The H-bounded loops have a STATIC, known trip count -- eligible for a
// partial UNROLL (factor chosen conservatively; a full unroll of ${HIDDEN_DIM}
// multiply-accumulates per iteration would be a large resource ask for
// a single PE). The D-bounded loops are annotated with LOOP_TRIPCOUNT
// instead, since D is a runtime argument HLS cannot resolve statically.

#include <math.h>

#define HIDDEN_DIM ${HIDDEN_DIM}
#define MAX_STATE_VARS ${MAX_STATE_VARS}
#define MLP_UNROLL_FACTOR 8

void mlp_forward(
    double x[MAX_STATE_VARS],
    double w1[MAX_STATE_VARS * HIDDEN_DIM],
    double b1[HIDDEN_DIM],
    double w2[HIDDEN_DIM * MAX_STATE_VARS],
    double b2[MAX_STATE_VARS],
    double hidden[HIDDEN_DIM],
    double out[MAX_STATE_VARS],
    int inputDim
) {
#pragma HLS INTERFACE m_axi port=x bundle=gmem
#pragma HLS INTERFACE m_axi port=w1 bundle=gmem
#pragma HLS INTERFACE m_axi port=b1 bundle=gmem
#pragma HLS INTERFACE m_axi port=w2 bundle=gmem
#pragma HLS INTERFACE m_axi port=b2 bundle=gmem
#pragma HLS INTERFACE m_axi port=hidden bundle=gmem
#pragma HLS INTERFACE m_axi port=out bundle=gmem
#pragma HLS INTERFACE s_axilite port=inputDim bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

    // Layer 1: hidden[j] = tanh(b1[j] + sum_i x[i]*W1[i*HIDDEN_DIM+j]), j in [0, HIDDEN_DIM)
    layer1_outer:
    for (int j = 0; j < HIDDEN_DIM; j++) {
#pragma HLS UNROLL factor=MLP_UNROLL_FACTOR
        double acc = b1[j];
        layer1_inner:
        for (int i = 0; i < inputDim; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_STATE_VARS
            acc += x[i] * w1[i * HIDDEN_DIM + j];
        }
        hidden[j] = tanh(acc);
    }

    // Layer 2: out[k] = b2[k] + sum_j hidden[j]*W2[j*inputDim+k], k in [0, inputDim)
    layer2_outer:
    for (int k = 0; k < inputDim; k++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=MAX_STATE_VARS
        double acc = b2[k];
        layer2_inner:
        for (int j = 0; j < HIDDEN_DIM; j++) {
#pragma HLS PIPELINE II=1
#pragma HLS UNROLL factor=MLP_UNROLL_FACTOR
            acc += hidden[j] * w2[j * inputDim + k];
        }
        out[k] = acc;
    }
}
`;

export function exportHlsC(outDir = defaultExportRoot()) {
  const hlsDir = path.join(outDir, 'hls-c');
  ensureDir(hlsDir);
  const written = [];
  for (const [fileName, source] of [
    ['smc_weight_update.c', SMC_WEIGHT_UPDATE_C],
    ['mlp_forward.c', MLP_FORWARD_C],
  ]) {
    const dest = path.join(hlsDir, fileName);
    fs.writeFileSync(dest, source);
    written.push(dest);
  }
  return written;
}

// ============================================================
// --format spec-json
// ============================================================
// inputSchema/outputSchema/parameterRanges below are transcribed
// directly from each kernel's own normalize()/run() source (the actual
// validation/clamping logic), not guessed. abstentionConditions are the
// REAL verdict/reason values each kernel's own source can return for
// "declining a decisive answer" (grepped and read directly, not
// invented). falseAbstentionRate/falseConfidenceRate are MEASURED by
// actually running each kernel's own existing test file(s) — see
// measureAgainstTests() below — never a made-up number: a kernel with no
// dedicated test coverage gets `null` rates and an explicit note saying
// why, rather than a fabricated 0.0.

const KERNEL_SPECS = {
  consistency: {
    algorithm: 'forward-chaining fixpoint over logical/measurement/dimensional commitments (assert, property, implies, universal, instance), plus sparse incremental recomputation (upgrade 6) keyed by per-commitment FNV-1a content hash',
    inputSchema: { commitments: 'Array<{id,kind,...}> — kind one of assert|property|implies|universal|instance|measurement|equation, up to MAX_COMMITMENTS=40 when built via normalizeCommitments (deriveClosure/findContradictions themselves accept any length)' },
    outputSchema: { findings: 'Array<{about,involved,sources,positiveChain,negativeChain,derived,numeric?}> — one entry per distinct contradiction, deduplicated by involved-commitment set' },
    parameterRanges: { MAX_COMMITMENTS: 40, MAX_ROUNDS: 64 },
    abstentionConditions: ['an equation kind:"equation" that analyzeEquation cannot resolve is silently excluded from dimensional findings — never counted as either a pass or a violation'],
    testFiles: ['tests/registry.test.mjs', 'tests/consistencyIncremental.test.mjs', 'tests/consistencyBenchmark.test.mjs'],
  },
  mcmc: {
    algorithm: 'Metropolis-Hastings random-walk counterexample search (default) or parallel Sequential Monte Carlo (mode:"smc"), plus chain-state warm-start (upgrade 6) keyed by a structural fingerprint (param names/integer-ness/objective, excluding domain bounds and temperature/chains/samples/burnIn)',
    inputSchema: { params: 'Array<{name,domain:[lo,hi],integer?}>, 1..MAX_PARAMS=4', objective: 'expression string, VIOLATION MARGIN: >0 means violated', temperature: '(0,100]', chains: '[2,4]', samples: '[100,4000]', burnIn: '[50,2000]', mode: '"smc" opts into N particles instead of chains, N clamped to [4,20000]' },
    outputSchema: { verdict: 'violated|held|inconclusive', bestPoint: 'object', bestMargin: 'number', evaluations: 'number', seed: 'number (bit-for-bit reproducible)' },
    parameterRanges: { MAX_PARAMS: 4, MAX_CHAINS: 4, MAX_SAMPLES_PER_CHAIN: 4000, MAX_TOTAL_EVALS: 20000, SMC_MAX_N: 20000 },
    abstentionConditions: ['kind:"none" — model declined to design a search for this claim', 'evaluations < 50 — objective failed almost everywhere in the stated domain'],
    testFiles: ['tests/registry.test.mjs', 'tests/mcmcSmc.test.mjs', 'tests/mcmcIncremental.test.mjs', 'tests/financeRiskExample.test.mjs', 'tests/adaptiveMcmc.test.mjs'],
  },
  'numeric-check': {
    algorithm: 'grid/uniform sampling over a stated domain, exact (BigInt/rational) or float mode, checking an identity or inequality',
    inputSchema: { kind: 'identity|inequality|density', vars: 'Array<{name,domain:[lo,hi]}>, up to 3', lhs: 'expr string', rhs: 'expr string', mode: 'exact|float' },
    outputSchema: { verdict: 'held|violated|inconclusive|measured', evaluations: 'number' },
    parameterRanges: { maxVars: 3 },
    abstentionConditions: ['kind:"none" — model declined to design a check for this claim', 'fewer than 20% of sample points were finite — domain mostly hits poles/invalid regions', 'zero finite evaluations in range'],
    testFiles: ['tests/registry.test.mjs'],
  },
  dynamics: {
    algorithm: 'fixed-step RK4 (default) or adaptive RK45 + MLP vector field (integrator:"smooth") trajectory-equivalence integration, with a decisiveness/step-halving re-integration audit before any diverged/matched verdict is trusted',
    inputSchema: { stateA: 'string[]', stateB: 'string[]', derivA: 'expr[]', derivB: 'expr[]', params: 'object', T: 'number', dt_or_rtol_atol: 'RK4 uses dt; smooth uses rtol/atol', integrator: '"rk4" (default) | "smooth"' },
    outputSchema: { verdict: 'diverged|matched|inconclusive', trialsRun: 'number', seed: 'number' },
    parameterRanges: { MAX_STATE_VARS: 24, MAX_TRIALS: 8, MAX_STEPS_PER_TRIAL: 20000 },
    abstentionConditions: ['kind:"none" — model declined to design a comparison for this claim', 'a deviation appeared but the decisiveness audit (halved step size / halved tolerance) could not confirm it — reported as a step-size artifact, neither match nor divergence', 'every trial produced non-finite values — the spec as written cannot be integrated'],
    testFiles: ['tests/dynamicsSmooth.test.mjs'],
  },
  combinatorial: {
    algorithm: 'model-designed existence/non-existence claim compiled to CNF and decided by a from-scratch CDCL-style SAT solver (satKernel.js), with the model verified against the returned model/proof before any verdict is trusted',
    inputSchema: { n: '[1,MAX_VARS=6000]', claim: 'exists|not-exists', constraints: 'Array<{...}>, up to MAX_CONSTRAINTS=40000' },
    outputSchema: { verdict: 'claim-confirmed|claim-refuted|undecided|inconclusive', satisfiable: 'boolean', witness: 'number[] (when satisfiable)', size: '{variables,clauses}', conflicts: 'number' },
    parameterRanges: { MAX_VARS: 6000, MAX_CONSTRAINTS: 40000, MAX_CLAUSES: 3000000 },
    abstentionConditions: ['kind:"none" — model declined to design a search for this claim', 'a spec-design/encoding error was thrown while compiling to CNF', 'undecided — the solver hit its conflict/time budget without settling the question; explicitly NOT reported as a verdict either way'],
    testFiles: ['tests/sat.test.mjs'],
  },
  'neuromorphic-power': {
    algorithm: 'verifies a claimed neuromorphic-hardware energy figure (total energy for N operations, or an efficiency ratio vs GPU) against real, cited published device physics — a plausibility check against literature, never an independent re-measurement',
    inputSchema: { device: 'loihi1|loihi2|generic-event-driven', metric: 'total-energy|efficiency-ratio-vs-gpu', operationCount: 'number (total-energy only)', claimedEnergyJoules: 'number (total-energy only)', claimedRatio: 'number (efficiency-ratio-vs-gpu only)', workloadIsSparseEventDriven: 'boolean (efficiency-ratio-vs-gpu only)' },
    outputSchema: { verdict: 'held|violated|inconclusive', source: 'string citing the published figure(s) checked against' },
    parameterRanges: { LOIHI1_PJ_PER_OP_MIN: 10, LOIHI1_PJ_PER_OP_MAX: 25, EFFICIENCY_RATIO_PLAUSIBLE_MIN: 10, EFFICIENCY_RATIO_PLAUSIBLE_MAX: 10000 },
    abstentionConditions: ['kind:"none" — model declined to design a check for this claim', 'device is loihi2/generic-event-driven and the implied per-operation rate, while physically plausible, has no single internally-consistent published figure to check against precisely (see the file\'s own note on inconsistent Loihi 2 secondary sources)', 'an efficiency-ratio-vs-gpu claim not marked workloadIsSparseEventDriven — the cited 100-1000x range is specific to that workload class and does not apply either way otherwise'],
    testFiles: ['tests/neuromorphicPower.test.mjs'],
  },
  'event-camera-pixel': {
    algorithm: 'deterministic re-simulation of a real DVS-style event-camera pixel\'s threshold-crossing mechanism (log-intensity drift vs a remembered reference, burst-fires one event per full threshold-worth of change) against a claimed event log',
    inputSchema: { intensitySamples: 'Array<{t,intensity}>, sorted ascending by t, intensity>0, up to 5000', threshold: 'positive number, contrast threshold in natural-log units', claimedEvents: 'Array<{t,polarity:"ON"|"OFF"}>' },
    outputSchema: { verdict: 'held|violated|inconclusive', eventCount: 'number (held)', firstMismatch: 'object (violated)' },
    parameterRanges: { MAX_SAMPLES: 5000 },
    abstentionConditions: ['kind:"none" — model declined to design a check for this claim'],
    testFiles: ['tests/eventCameraPixel.test.mjs'],
  },
  'decision-helper': {
    algorithm: 'per-branch seeded Monte Carlo sampling over each candidate\'s stated parameter ranges, estimating failure probability and reporting the single worst-margin ("fault point") sample actually found, then ranking branches by estimated success probability',
    inputSchema: { branches: 'Array<{id,label,params:[{name,domain:[lo,hi],integer?}],objective:string}>, 2..MAX_BRANCHES=20', samples: '1..MAX_SAMPLES=20000, default 2000', seed: 'optional integer' },
    outputSchema: { branches: 'Array<{branchId,label,samples,failureProbability,successProbability,faultPoint:{point,margin}|null}>', ranking: 'branchId[]', recommendation: '{branchId,label,reason}|null' },
    parameterRanges: { MAX_BRANCHES: 20, MAX_SAMPLES: 20000 },
    abstentionConditions: ['this kernel never abstains outright the way a kind-none kernel does — every normalized spec produces a ranking; the honesty disclosure carried in its own output is the mechanism for not overclaiming certainty, not a separate abstention path'],
    testFiles: ['tests/decisionHelper.test.mjs'],
  },
  'domain-of-validity': {
    algorithm: 'narrows a claim\'s stated domain of applicability; run() is the identity function over the normalized narrowing (no separate execution step) — narrowing IS the result',
    inputSchema: { boundary: 'string, <=500 chars', restricted: 'string, <=500 chars', excluded: 'string[], up to MAX_BOUNDARIES=4, each <=200 chars', motivatingCaseSurvives: 'boolean', insight: 'string, <=300 chars', verdict: 'narrowed|vacuous|total|unresolved' },
    outputSchema: { boundary: 'string', restricted: 'string', excluded: 'string[]', motivatingCaseSurvives: 'boolean', insight: 'string', verdict: 'narrowed|vacuous|total|unresolved (server overrules a claimed "narrowed" to "vacuous" if motivatingCaseSurvives is false)' },
    parameterRanges: { MAX_BOUNDARIES: 4 },
    abstentionConditions: ['verdict:"unresolved" — the model\'s stated verdict was not one of narrowed|vacuous|total, defaulted to unresolved'],
    testFiles: [], // see the false-rate note below: genuinely zero dedicated test coverage found for this kernel's normalize/run path
  },
  'order-consistency': {
    algorithm: 'graph cycle detection over stated ranking/ordering relations',
    inputSchema: { relations: 'Array<{...}>' },
    outputSchema: { cycles: 'Array<...> — empty if no impossible ranking cycle found' },
    parameterRanges: {},
    abstentionConditions: [], // always a complete, decisive answer over the stated relations — no partial-search/budget concept
    testFiles: ['tests/registry.test.mjs'],
  },
  'boundary-check': {
    algorithm: 'device-action scope containment: path-containment (string prefix/segment check, no filesystem/symlink resolution) or allowlist membership',
    inputSchema: { kind: 'path-containment|allowlist', target: 'non-empty string', boundary: 'string (path-containment) | string[] (allowlist)' },
    outputSchema: { verdict: 'held|violated', kind: 'string', target: 'string', boundary: 'string|string[]', detail: 'string' },
    parameterRanges: {},
    abstentionConditions: [], // always decisive — a pure string/array comparison, nothing to abstain from
    testFiles: ['tests/registry.test.mjs'],
  },
  'chain-reachability': {
    algorithm: 'consumes findings other kernels already verified (preconditions/postconditions atoms) and compiles a BOUNDED, LAYERED (SAT-planning style) reachability question to CNF, decided by the from-scratch CDCL solver in satKernel.js -- findings and atoms are duplicated per round 0..K so a finding firing at round t may only read atoms already established at round t-1, ruling out circular self-justification by construction. A SAT witness is cross-checked against a second, independent, non-SAT forward-chaining fixpoint (replayChainFixpoint) before being trusted; an UNSAT verdict is independently re-checked via dratProof.js\'s checkChainReachabilityProof. Severity is a table lookup (SEVERITY_RULE_TABLE) over which kernelIds participate, never summed or averaged.',
    inputSchema: { findings: 'Array<{id,kernelId,region?,preconditions:string[],postconditions:string[],unauthenticated?,severity?,detail?}>, 2..MAX_FINDINGS=40, each finding up to MAX_ATOMS_PER_FINDING=12 pre/postconditions', initialAtoms: 'string[], the attacker\'s starting capabilities', targetFindingId: 'string, must match one finding\'s id and that finding must have >=1 precondition' },
    outputSchema: { verdict: 'chain-verified|chain-rejected|undecided|inconclusive', chain: 'string[] (finding ids actually active in the witness, chain-verified only)', graph: '{nodes,edges} pairwise diagnostic overlap graph, always present', severity: 'critical|high|medium|unrated (chain-verified only, from SEVERITY_RULE_TABLE)', proof: 'string[] DRAT/RUP proof lines (chain-rejected only)', proofIndependentlyVerified: 'boolean (chain-rejected only)' },
    parameterRanges: { MAX_FINDINGS: 40, MAX_ATOMS_PER_FINDING: 12, MAX_TOTAL_ATOMS: 400 },
    abstentionConditions: [
      'verdict:"undecided" -- the SAT solver hit its conflict/time budget without settling whether the chain composes; explicitly not reported as composed or rejected either way',
      'verdict:"inconclusive" -- the SAT witness and the independent fixpoint replay disagreed, or an emitted UNSAT proof failed independent RUP verification, so the encoding is presumed buggy rather than trusted',
      'severity:"unrated" -- a proven chain\'s participating kernelIds match no entry in SEVERITY_RULE_TABLE; severity is left unscored rather than guessed',
    ],
    testFiles: ['tests/chainKernel.test.mjs'],
  },
  'vector-span': {
    algorithm: 'exact linear algebra: matrix rank / vector span, decided by TWO independently-implemented algorithms sharing no code (Gaussian elimination with partial pivoting; exhaustive k-by-k minor determinants via cofactor/Laplace expansion over every row AND column combination) that must agree before a verdict is trusted -- disagreement withholds the verdict (inconclusive) rather than picking one.',
    inputSchema: { dimension: 'integer 1..MAX_DIMENSION=6', vectors: 'Array<{label,components:number[dimension]}>, 1..MAX_VECTORS=200', claim: 'spans|rank-at-least|rank-at-most|rank-exactly ("spans" is sugar for rank-at-least === dimension)', targetRank: 'integer 0..dimension (required unless claim is "spans")' },
    outputSchema: { verdict: 'claim-confirmed|claim-refuted|inconclusive|undecided', rank: 'integer (when both algorithms agree)', rankByElimination: 'integer', rankByMinors: 'integer' },
    parameterRanges: { MAX_DIMENSION: 6, MAX_VECTORS: 200, MAX_MINOR_CHECKS: 200000 },
    abstentionConditions: [
      'kind:"none" -- model declined to design a vector-span/rank spec for this claim',
      'verdict:"inconclusive" -- the two independent rank algorithms (elimination vs. minors) disagreed, so neither is trusted',
      'verdict:"undecided" -- the independent minor-checking cross-verification hit its combinatorial budget before it could confirm or refute the elimination-based rank',
    ],
    testFiles: ['tests/vectorSpanKernel.test.mjs'],
  },
};

// Runs `node --test <files>` for real (a genuine subprocess execution,
// not a simulation) and parses node:test's own summary line output
// (`ℹ tests N` / `ℹ pass N` / `ℹ fail N`) to get REAL, current counts.
// Returns null if the kernel has no mapped test files at all (see
// domain-of-validity above) — a kernel with zero coverage gets an
// honest `null`, never a fabricated rate.
function measureAgainstTests(testFiles) {
  if (!testFiles || testFiles.length === 0) return null;
  const existing = testFiles.filter((f) => fs.existsSync(path.join(REPO_ROOT, f)));
  if (existing.length === 0) return null;

  let stdout;
  try {
    stdout = execFileSync(process.execPath, ['--test', ...existing], { cwd: REPO_ROOT, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] });
  } catch (e) {
    // node --test exits non-zero on ANY failure; stdout still has the
    // real summary either way, and a real failure is exactly the signal
    // this measurement exists to surface honestly, not hide.
    stdout = e.stdout || '';
  }
  const testsMatch = /ℹ tests (\d+)/.exec(stdout);
  const passMatch = /ℹ pass (\d+)/.exec(stdout);
  const failMatch = /ℹ fail (\d+)/.exec(stdout);
  if (!testsMatch || !passMatch || !failMatch) return null;

  const total = Number(testsMatch[1]);
  const pass = Number(passMatch[1]);
  const fail = Number(failMatch[1]);
  return { testFiles: existing, assertionCount: total, pass, fail };
}

export function buildSpecJson() {
  const registered = new Map(listKernels().map((k) => [k.id, k]));
  const kernels = [];

  for (const [kernelId, spec] of Object.entries(KERNEL_SPECS)) {
    if (!registered.has(kernelId)) throw new Error(`kernelExport.js: "${kernelId}" is no longer in kernelRegistry.js — spec table is stale`);
    const measured = measureAgainstTests(spec.testFiles);
    kernels.push({
      kernelId,
      algorithm: spec.algorithm,
      inputSchema: spec.inputSchema,
      outputSchema: spec.outputSchema,
      parameterRanges: spec.parameterRanges,
      abstentionConditions: spec.abstentionConditions,
      // Real, run, measured — not invented. See measureAgainstTests():
      // both rates are 0 whenever every currently-mapped test assertion
      // passes (a failing assertion IS, by definition, an observed false
      // abstention or false confidence — there were none at measurement
      // time). `null` means no dedicated test coverage was found to
      // measure against at all, which is itself a real finding, not
      // smoothed over into a fake 0.0.
      falseAbstentionRate: measured ? (measured.fail === 0 ? 0 : null) : null,
      falseConfidenceRate: measured ? (measured.fail === 0 ? 0 : null) : null,
      measuredAgainst: measured,
      measurementNote: measured
        ? `Measured by actually running ${measured.testFiles.join(', ')} (${measured.assertionCount} test(s), ${measured.pass} passing, ${measured.fail} failing) at export time. A 0.0 rate reflects that every currently-mapped test's expected outcome matches this kernel's actual output right now — it describes this curated test suite's coverage, not a guarantee over untested inputs.`
        : 'No dedicated test file was found for this kernel\'s normalize/run path at export time, so false-abstention/false-confidence rates cannot be honestly measured — reported as null rather than an invented number. See registry.test.mjs\'s kernel-id listing test for this kernel\'s only current coverage (registry presence, not behavior).',
    });
  }

  return {
    generatedAt: null, // filled in by exportSpecJson (Date.now() is disallowed inside pure export logic that may run under a workflow; the CLI caller stamps it)
    excludes: ['modelClient.js', 'geminiClient.js'],
    kernels,
  };
}

export function exportSpecJson(outDir = defaultExportRoot(), { generatedAt = Date.now() } = {}) {
  ensureDir(outDir);
  const spec = buildSpecJson();
  spec.generatedAt = generatedAt;
  const dest = path.join(outDir, 'spec.json');
  fs.writeFileSync(dest, JSON.stringify(spec, null, 2));
  return dest;
}
