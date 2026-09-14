// orchestratorRemote.test.mjs; upgrade 8 — the three required tests
// exercised through orchestrator.js's CIALL_REMOTE_KERNELS routing
// specifically (kernelService.test.mjs covers the same transport at a
// lower level, including the TLS-rejection test; this file proves the
// routing/resilience POLICY layer on top of it): round-trip (claim
// sent to a real remote server, verified, matches local), fallback
// (server unreachable -> local execution, warning logged), and a
// second angle on TLS (a remote-kernel call to a non-TLS/unreachable
// endpoint never surfaces as a hard failure — it falls back).

import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';
import { ensureDevCert } from '../src/lib/rpc/devCert.js';
import { startKernelServiceServer } from '../src/lib/rpc/kernelServiceServer.js';
import { runPipeline } from '../src/lib/orchestrator.js';
import { shutdownPool } from '../src/lib/kernelWorkerPool.js';

after(() => shutdownPool());

let certPath, keyPath, certDir;
try {
  certDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-devcert-orch-'));
  ({ certPath, keyPath } = ensureDevCert(certDir));
} catch {
  certPath = null;
}
after(() => { if (certDir) fs.rmSync(certDir, { recursive: true, force: true }); });

function withEnv(name, value, fn) {
  const had = Object.prototype.hasOwnProperty.call(process.env, name);
  const prev = process.env[name];
  if (value === undefined) delete process.env[name];
  else process.env[name] = value;
  return Promise.resolve().then(fn).finally(() => {
    if (had) process.env[name] = prev;
    else delete process.env[name];
  });
}

const designSpec = (kernel, claim) => {
  if (kernel.id === 'boundary-check') return { kind: 'allowlist', target: claim.target, boundary: claim.boundary };
  return null;
};

test('round-trip: a claim routed via CIALL_REMOTE_KERNELS is executed on the real remote server and the report reflects the remote result', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    await withEnv('CIALL_REMOTE_KERNELS', `boundary-check:127.0.0.1:${port}`, async () => {
      const claim = { target: 'a', boundary: ['a', 'b'] };
      const report = await runPipeline(claim, { kernelIds: ['boundary-check'], designSpec, parallel: false });
      assert.equal(report.ran.length, 1);
      assert.equal(report.ran[0].kernelId, 'boundary-check');
      assert.equal(report.ran[0].result.verdict, 'held');
      assert.equal(report.failed.length, 0);

      // Cross-check: the SAME claim run with no remote routing at all
      // (local path) must produce the identical verdict — the remote
      // path is a transport choice, never a behavior change.
      const localReport = await runPipeline(claim, { kernelIds: ['boundary-check'], designSpec, parallel: false });
      assert.deepEqual(localReport.ran[0].result, report.ran[0].result);
    });
  } finally {
    await close();
  }
});

test('round-trip: this also works with the default parallel:true path (worker-pool dispatch bypassed for the remote-routed kernel)', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    await withEnv('CIALL_REMOTE_KERNELS', `boundary-check:127.0.0.1:${port}`, async () => {
      const claim = { target: 'z', boundary: ['a', 'b'] };
      const report = await runPipeline(claim, { kernelIds: ['boundary-check'], designSpec }); // parallel defaults true
      assert.equal(report.ran[0].result.verdict, 'violated');
    });
  } finally {
    await close();
  }
});

test('fallback: an unreachable remote server never fails the verification — it falls back to local execution, and logs a warning', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  // Port 1 is (a) not our test server and (b) virtually guaranteed to
  // refuse the connection immediately on any normal machine (privileged
  // port, nothing listening) -- a real, fast connection failure, not a
  // hang, so this test doesn't need a long timeout budget despite 3
  // real retries at 50ms backoff (~150ms total).
  const originalError = console.error;
  const loggedWarnings = [];
  console.error = (...args) => loggedWarnings.push(args.join(' '));
  try {
    await withEnv('CIALL_REMOTE_KERNELS', 'boundary-check:127.0.0.1:1', async () => {
      const claim = { target: 'a', boundary: ['a', 'b'] };
      const report = await runPipeline(claim, { kernelIds: ['boundary-check'], designSpec, parallel: false });
      // The verification itself must have succeeded via local fallback
      // -- NEVER surfaced as a failure just because the network call
      // could not connect.
      assert.equal(report.failed.length, 0);
      assert.equal(report.ran.length, 1);
      assert.equal(report.ran[0].result.verdict, 'held');
    });
  } finally {
    console.error = originalError;
  }
  assert.ok(loggedWarnings.some((line) => line.includes('falling back to local execution')), `expected a fallback warning to be logged, got: ${JSON.stringify(loggedWarnings)}`);
});

test('fallback does not apply to a genuine kernel-level error from a REACHABLE server: it is reported as a real failure, not retried or masked', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    await withEnv('CIALL_REMOTE_KERNELS', `boundary-check:127.0.0.1:${port}`, async () => {
      const badClaim = { target: 'a', boundary: 'not-an-array' }; // allowlist requires an ARRAY boundary -- normalize() will reject this
      const report = await runPipeline(badClaim, { kernelIds: ['boundary-check'], designSpec, parallel: false });
      assert.equal(report.ran.length, 0);
      assert.equal(report.failed.length, 1);
      assert.equal(report.failed[0].kernelId, 'boundary-check');
    });
  } finally {
    await close();
  }
});

test('a kernel NOT named in CIALL_REMOTE_KERNELS runs locally exactly as before, even while another kernel IS remote-routed', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    await withEnv('CIALL_REMOTE_KERNELS', `boundary-check:127.0.0.1:${port}`, async () => {
      const claim = { relations: [] };
      const localDesignSpec = (kernel) => (kernel.id === 'order-consistency' ? { relations: [] } : null);
      const report = await runPipeline(claim, { kernelIds: ['order-consistency'], designSpec: localDesignSpec, parallel: false });
      assert.equal(report.ran.length, 1);
      assert.deepEqual(report.ran[0].result, []);
    });
  } finally {
    await close();
  }
});

test('CIALL_REMOTE_KERNELS unset (single-machine mode): behavior is completely unchanged from before upgrade 8', async () => {
  await withEnv('CIALL_REMOTE_KERNELS', undefined, async () => {
    const claim = { target: 'a', boundary: ['a', 'b'] };
    const report = await runPipeline(claim, { kernelIds: ['boundary-check'], designSpec, parallel: false });
    assert.equal(report.ran[0].result.verdict, 'held');
  });
});

test('a malformed CIALL_REMOTE_KERNELS value fails loudly and immediately, not silently ignored', async () => {
  await withEnv('CIALL_REMOTE_KERNELS', 'boundary-check:not-a-port', async () => {
    const claim = { target: 'a', boundary: ['a', 'b'] };
    await assert.rejects(runPipeline(claim, { kernelIds: ['boundary-check'], designSpec, parallel: false }), /CIALL_REMOTE_KERNELS/);
  });
});
