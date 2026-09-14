// kernelService.test.mjs; upgrade 8 — end-to-end test of the real TLS
// + hand-written HTTP/2 + HPACK + protobuf + MessagePack stack: starts
// a real KernelService TLS server (self-signed dev cert), calls it as
// a real TCP client would, and verifies the result matches calling the
// SAME kernel locally. Also covers the required "TLS test" (connection
// rejected without a valid TLS handshake).

import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import fs from 'node:fs';
import net from 'node:net';
import { ensureDevCert } from '../src/lib/rpc/devCert.js';
import { startKernelServiceServer } from '../src/lib/rpc/kernelServiceServer.js';
import { callKernelRemote } from '../src/lib/rpc/kernelServiceClient.js';
import { getKernel } from '../src/lib/kernelRegistry.js';

let certDir;
let certPath, keyPath;
try {
  certDir = fs.mkdtempSync(path.join(os.tmpdir(), 'ciall-devcert-'));
  ({ certPath, keyPath } = ensureDevCert(certDir));
} catch {
  certPath = null; // openssl unavailable in this environment; TLS tests below skip themselves
}

after(() => { if (certDir) fs.rmSync(certDir, { recursive: true, force: true }); });

test('round-trip: a claim sent to the real TLS server is verified remotely and matches calling the kernel locally', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    const boundaryKernel = getKernel('boundary-check');
    const claim = { kind: 'allowlist', target: 'a', boundary: ['a', 'b'] };

    const { result: remoteResult, wallClockMs } = await callKernelRemote('127.0.0.1', port, 'boundary-check', claim);
    const localResult = boundaryKernel.run(boundaryKernel.normalize(claim));

    assert.deepEqual(remoteResult, localResult);
    assert.equal(typeof wallClockMs, 'number');
    assert.ok(wallClockMs >= 0);
  } finally {
    await close();
  }
});

test('round-trip: an mcmc claim (a kernel with real, nontrivial computation) matches local execution bit-for-bit', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    const mcmcKernel = getKernel('mcmc');
    const rawSpec = { kind: 'mcmc_search', params: [{ name: 'x', domain: [0, 10] }], objective: 'x - 5' };

    const { result: remoteResult } = await callKernelRemote('127.0.0.1', port, 'mcmc', rawSpec);
    const localResult = mcmcKernel.run(mcmcKernel.normalize(rawSpec));

    // mcmc is seeded-stochastic but bit-for-bit reproducible: the SAME
    // spec run twice (once "locally" here, once remotely, both cold —
    // neither is warm-started off the other since they're separate
    // module instances / separate JS realms via the child server
    // process boundary is NOT crossed here, but see the note below)
    // must produce the identical result.
    assert.deepEqual(remoteResult, localResult);
  } finally {
    await close();
  }
});

test('round-trip: an unknown kernel name fails with a clear remote error, not a hang or a crash', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    await assert.rejects(
      callKernelRemote('127.0.0.1', port, 'not-a-real-kernel', {}),
      /not-a-real-kernel/,
    );
  } finally {
    await close();
  }
});

test('round-trip: a malformed claim fails with a clear remote error (normalize() rejected it), not a hang or a crash', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    await assert.rejects(
      callKernelRemote('127.0.0.1', port, 'boundary-check', { kind: 'not-a-real-kind' }),
      /remote kernel "boundary-check"/,
    );
  } finally {
    await close();
  }
});

test('TLS: a plaintext (non-TLS) connection to the TLS-only server never completes a working request', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    const outcome = await new Promise((resolve) => {
      const socket = net.connect(port, '127.0.0.1', () => {
        // Send the raw HTTP/2 plaintext preface directly, no TLS at
        // all -- a TLS-only server must not interpret this as a valid
        // request.
        socket.write(Buffer.from('PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n', 'ascii'));
      });
      socket.setTimeout(1500);
      socket.on('data', () => resolve('got-plaintext-response')); // would be a real failure if this ever fired
      socket.on('error', () => resolve('errored'));
      socket.on('timeout', () => { socket.destroy(); resolve('timed-out'); });
      socket.on('close', () => resolve((r) => r)); // no-op fallback, 'error'/'timeout' above already resolve
    });
    assert.notEqual(outcome, 'got-plaintext-response', 'a TLS-only server must never serve a plaintext HTTP/2 connection as if it were valid');
  } finally {
    await close();
  }
});

test('TLS: the client actually negotiates TLS (not a bare TCP echo) — the connection is a real tls.TLSSocket with a peer certificate', { skip: !certPath && 'openssl not available to generate a dev cert' }, async () => {
  const { close, port } = await startKernelServiceServer({ port: 0, certPath, keyPath });
  try {
    const tls = await import('node:tls');
    const socket = await new Promise((resolve, reject) => {
      const s = tls.connect({ host: '127.0.0.1', port, rejectUnauthorized: false }, () => resolve(s));
      s.once('error', reject);
    });
    assert.ok(socket.encrypted, 'the socket must be a real TLS-encrypted connection');
    const cert = socket.getPeerCertificate();
    assert.ok(cert && cert.subject, 'the server must present an actual certificate');
    socket.destroy();
  } finally {
    await close();
  }
});
