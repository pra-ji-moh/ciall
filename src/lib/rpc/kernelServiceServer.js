// kernelServiceServer.js; upgrade 8 — `ciall serve --port <n>`: exposes
// every registered kernel over TLS + hand-written HTTP/2 + HPACK +
// protobuf + MessagePack, via a single unary RPC (KernelService.Run).
// No new kernel logic — this is a transport wrapper around the exact
// same { normalize, run } every kernel already exposes through
// kernelRegistry.js.

import tls from 'node:tls';
import fs from 'node:fs';
import { createHttp2Connection } from './http2Connection.js';
import { decodeKernelRequest, encodeKernelResult } from '../kernelProto.js';
import { encode as msgpackEncode, decode as msgpackDecode } from '../msgpack.js';
import { wrapGrpcMessage, unwrapGrpcMessage } from './grpcMessage.js';
import { getKernel } from '../kernelRegistry.js';

const GRPC_STATUS_OK = '0';
const GRPC_STATUS_UNKNOWN = '2'; // matches the generic default this repo's http2Connection.js already uses for a thrown handler
const GRPC_STATUS_NOT_FOUND = '5';
const GRPC_STATUS_INVALID_ARGUMENT = '3';

function okResponse(resultBytes) {
  return {
    headers: [[':status', '200'], ['content-type', 'application/grpc+proto']],
    body: wrapGrpcMessage(resultBytes),
    trailers: [['grpc-status', GRPC_STATUS_OK]],
  };
}

function errorResponse(grpcStatus, message) {
  return {
    headers: [[':status', '200'], ['content-type', 'application/grpc+proto']],
    body: Buffer.alloc(0),
    trailers: [['grpc-status', grpcStatus], ['grpc-message', message]],
  };
}

/** Runs `kernelName`'s normalize()+run() against `claimValue` (already msgpack-decoded), timing the run() call only — matches KernelResult's wall_clock_ms field, the cost of executing the kernel, not of transport/deserialization. */
export async function runKernelForRequest(kernelName, claimValue) {
  let kernel;
  try {
    kernel = getKernel(kernelName);
  } catch (err) {
    const notFoundErr = new Error(err.message);
    notFoundErr.grpcStatus = GRPC_STATUS_NOT_FOUND;
    throw notFoundErr;
  }
  let normalized;
  try {
    normalized = kernel.normalize(claimValue);
  } catch (err) {
    const badArgErr = new Error(`normalize failed: ${err.message}`);
    badArgErr.grpcStatus = GRPC_STATUS_INVALID_ARGUMENT;
    throw badArgErr;
  }
  const t0 = Date.now();
  const result = await kernel.run(normalized);
  const wallClockMs = Date.now() - t0;
  return { result, wallClockMs };
}

async function handleGrpcRequest(headerPairs, body) {
  try {
    const grpcMessage = unwrapGrpcMessage(body);
    const { kernelName, claim } = decodeKernelRequest(grpcMessage);
    const claimValue = msgpackDecode(claim);
    const { result, wallClockMs } = await runKernelForRequest(kernelName, claimValue);
    const resultBytes = encodeKernelResult({ output: msgpackEncode(result), wallClockMs });
    return okResponse(resultBytes);
  } catch (err) {
    return errorResponse(err.grpcStatus || GRPC_STATUS_UNKNOWN, String(err && err.message || err));
  }
}

/**
 * Starts the KernelService TLS server on `port`. Returns a Promise
 * resolving to { server, port, close() } once listening — `port` in
 * the result is the ACTUAL bound port (useful when the caller passed 0
 * for an ephemeral one, as tests do).
 */
export function startKernelServiceServer({ port, certPath, keyPath }) {
  const cert = fs.readFileSync(certPath);
  const key = fs.readFileSync(keyPath);
  const server = tls.createServer({ cert, key }, (socket) => {
    createHttp2Connection(socket, { role: 'server', onRequest: handleGrpcRequest });
  });
  return new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, () => {
      server.removeListener('error', reject);
      resolve({
        server,
        port: server.address().port,
        close: () => new Promise((r) => server.close(r)),
      });
    });
  });
}
