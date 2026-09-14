// timingVariance.js; upgrade 11 — honest instrumentation for the gap
// CERTIFICATION-GAPS.md names directly: this repo proves deterministic
// OUTPUT extensively, but deterministic TIMING is a different, much
// harder property that no JS runtime provides. This module does not
// close that gap (it CANNOT be closed in JS) — it MEASURES real
// wall-clock variance so the gap is transparent and quantified rather
// than hand-waved. Nothing here is a bound, a guarantee, or a proof;
// it is a measurement of what actually happened on this run, on this
// machine, this time.

/**
 * Runs `fn` repeatedly, timing each call with performance.now(), and
 * returns real variance statistics. `warmupRuns` are executed and
 * timed but discarded before the measured runs start, so the reported
 * numbers reflect steady-state (JIT-warmed) behavior rather than V8's
 * cold-start compilation cost — matching how these kernels actually
 * run in the persistent `serve` process, not how they'd run as a
 * single one-shot CLI invocation (which has its own, larger and
 * separately-undisclosed-here, cold-start variance).
 */
export function measureTimingVariance(fn, { runs = 30, warmupRuns = 5, now = defaultNow } = {}) {
  if (typeof fn !== 'function') throw new Error('measureTimingVariance: fn must be a function');
  if (!Number.isInteger(runs) || runs < 2) throw new Error('measureTimingVariance: runs must be an integer >= 2');
  if (!Number.isInteger(warmupRuns) || warmupRuns < 0) throw new Error('measureTimingVariance: warmupRuns must be a non-negative integer');

  for (let i = 0; i < warmupRuns; i++) fn();

  const samplesMs = new Array(runs);
  for (let i = 0; i < runs; i++) {
    const t0 = now();
    fn();
    samplesMs[i] = now() - t0;
  }

  return summarize(samplesMs, runs, warmupRuns);
}

/**
 * Same statistics, for an async fn (measures wall-clock across the
 * awaited call, so it also captures any microtask/event-loop
 * scheduling jitter a sync measurement would miss — a real, distinct
 * source of timing variance for kernels that go through
 * kernelWorkerPool.js or orchestrator.js's async paths).
 */
export async function measureTimingVarianceAsync(fn, { runs = 30, warmupRuns = 5, now = defaultNow } = {}) {
  if (typeof fn !== 'function') throw new Error('measureTimingVarianceAsync: fn must be a function');
  if (!Number.isInteger(runs) || runs < 2) throw new Error('measureTimingVarianceAsync: runs must be an integer >= 2');
  if (!Number.isInteger(warmupRuns) || warmupRuns < 0) throw new Error('measureTimingVarianceAsync: warmupRuns must be a non-negative integer');

  for (let i = 0; i < warmupRuns; i++) await fn();

  const samplesMs = new Array(runs);
  for (let i = 0; i < runs; i++) {
    const t0 = now();
    await fn();
    samplesMs[i] = now() - t0;
  }

  return summarize(samplesMs, runs, warmupRuns);
}

function summarize(samplesMs, runs, warmupRuns) {
  const meanMs = samplesMs.reduce((a, b) => a + b, 0) / samplesMs.length;
  const minMs = Math.min(...samplesMs);
  const maxMs = Math.max(...samplesMs);
  const varianceMs2 = samplesMs.reduce((a, b) => a + (b - meanMs) ** 2, 0) / samplesMs.length;
  const stddevMs = Math.sqrt(varianceMs2);
  // Coefficient of variation: stddev relative to the mean, unitless,
  // comparable across kernels of very different absolute speeds.
  const coefficientOfVariation = meanMs > 0 ? stddevMs / meanMs : 0;
  // Spread ratio: how many times slower the slowest observed run was
  // than the fastest -- the number that matters most for anyone
  // asking "could this miss a hard real-time deadline sized off the
  // typical case." minMs can legitimately be 0 on a very fast kernel
  // if perf_hooks resolution rounds it down; guard against a
  // div-by-zero producing a misleadingly finite ratio.
  const spreadRatio = minMs > 0 ? maxMs / minMs : (maxMs > 0 ? Infinity : 1);

  return {
    runs,
    warmupRuns,
    meanMs,
    minMs,
    maxMs,
    stddevMs,
    coefficientOfVariation,
    spreadRatio,
    samplesMs,
    honesty: `Measured over ${runs} real timed runs (after ${warmupRuns} JIT-warmup runs, discarded, not counted). This is a MEASUREMENT of actual wall-clock variance on this machine, this run -- not a bound, not a WCET proof, not a guarantee. V8's JIT tiering and garbage collector mean a later call on the same process could still fall outside [${minMs.toFixed(4)}ms, ${maxMs.toFixed(4)}ms]. Deterministic OUTPUT (which this repo proves extensively elsewhere) and deterministic TIMING (which this number quantifies the ABSENCE of) are different properties; see CERTIFICATION-GAPS.md.`,
  };
}

// node:perf_hooks' performance.now() is this repo's existing timing
// idiom (see tests/consistencyBenchmark.test.mjs) -- reused here
// rather than introducing a second timing API. `now` is injectable
// (see the options above) so a test can supply synthetic timings.
function defaultNow() {
  return performance.now();
}
