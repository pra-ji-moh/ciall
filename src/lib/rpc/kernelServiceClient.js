// kernelServiceClient.js; upgrade 8 — calls a remote ciall serve
// instance's KernelService.Run RPC over TLS + hand-written HTTP/2 +
// HPACK + protobuf + MessagePack. orchestrator.js is the only intended
// caller (see CIALL_REMOTE_KERNELS routing there); this module knows
// nothing about retries/fallback — that policy lives in orchestrator.js
// per the task's own split (transport here, resilience policy there).
//
// SIMPLICITY, DISCLOSED: one TLS+HTTP/2 connection per call, closed
// immediately after — no connection pooling/keep-alive. A pooled
// connection would save a handshake per call, but the protocol layer
// underneath (http2Connection.js) already supports concurrent streams
// per connection for whenever pooling is worth adding; this keeps the
// client's own state (and failure modes — a half-dead pooled connection
// is a real source of bugs) minimal while the rest of upgrade 8's scope
// is this large.

import tls from 'node:tls';
import { createHttp2Connection } from './http2Connection.js';
import { encodeKernelRequest, decodeKernelResult } from '../kernelProto.js';
import { encode as msgpackEncode, decode as msgpackDecode } from '../msgpack.js';
import { wrapGrpcMessage, unwrapGrpcMessage } from './grpcMessage.js';

const DEFAULT_TIMEOUT_MS = 4000;

function connectTls(host, port, tlsOptions) {
  return new Promise((resolve, reject) => {
    const socket = tls.connect({
      host,
      port,
      // "Accept self-signed certs for local network use" (upgrade 8
      // spec): this deliberately skips CA-chain verification of the
      // server's certificate. The connection is still fully encrypted
      // (rejectUnauthorized only controls certificate TRUST, not
      // whether TLS itself runs) — appropriate for the stated use case
      // (a private/local network of ciall nodes), not for exposing this
      // port across an untrusted network, where it would permit MITM.
      rejectUnauthorized: false,
      ...tlsOptions,
    }, () => resolve(socket));
    socket.once('error', reject);
  });
}

/**
 * Calls KernelService.Run on host:port for `kernelName` with
 * `claimValue` (an arbitrary MessagePack-able value — the already-
 * normalized-or-not spec, matching whatever the caller would otherwise
 * pass to kernel.run() locally; the remote side runs normalize() too,
 * so raw model-designed specs work the same as local ones).
 *
 * Returns { result, wallClockMs }. Throws on any failure (connection,
 * TLS, protocol, or a grpc-status the server returned) — orchestrator.js
 * is responsible for retry/fallback around this call, not this function.
 */
export async function callKernelRemote(host, port, kernelName, claimValue, { tlsOptions = {}, timeoutMs = DEFAULT_TIMEOUT_MS } = {}) {
  const socket = await connectTls(host, port, tlsOptions);
  socket.setTimeout(timeoutMs);
  let conn;
  try {
    conn = createHttp2Connection(socket, { role: 'client' });

    const claimBytes = msgpackEncode(claimValue);
    const requestBytes = encodeKernelRequest({ kernelName, claim: claimBytes });
    const grpcBody = wrapGrpcMessage(requestBytes);

    const headers = [
      [':method', 'POST'],
      [':scheme', 'https'],
      [':path', '/ciall.KernelService/Run'],
      [':authority', `${host}:${port}`],
      ['content-type', 'application/grpc+proto'],
      ['te', 'trailers'],
    ];

    const timeoutPromise = new Promise((_, reject) => {
      socket.once('timeout', () => reject(new Error(`remote kernel call to ${host}:${port} timed out after ${timeoutMs}ms`)));
    });

    const response = await Promise.race([conn.request(headers, grpcBody), timeoutPromise]);

    const grpcStatus = response.trailers?.find(([k]) => k === 'grpc-status')?.[1];
    if (grpcStatus !== '0') {
      const message = response.trailers?.find(([k]) => k === 'grpc-message')?.[1] || 'unknown remote error';
      const err = new Error(`remote kernel "${kernelName}" at ${host}:${port} failed: ${message}`);
      // Distinguishes "the server was reached and ran the kernel, which
      // rejected the claim or threw" from every other throw path in this
      // function (connect/TLS/timeout/protocol failures) — the former is
      // a real, meaningful result orchestrator.js's retry/fallback logic
      // must NOT mask by silently re-running the same claim locally (it
      // would just hit the identical validation error); the latter is
      // exactly what retry+fallback exists for. See orchestrator.js's
      // callRemoteWithRetry for how this flag is used.
      err.isRemoteKernelError = true;
      throw err;
    }

    const resultBytes = unwrapGrpcMessage(response.body);
    const { output, wallClockMs } = decodeKernelResult(resultBytes);
    return { result: msgpackDecode(output), wallClockMs };
  } finally {
    if (conn) conn.close();
    socket.destroy();
  }
}
