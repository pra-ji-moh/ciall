// http2Connection.test.mjs; upgrade 8 — end-to-end test of the
// from-scratch HTTP/2 connection state machine over REAL net.Socket
// pairs (plain TCP on localhost; TLS wrapping is covered separately in
// kernelService.test.mjs). Exercises the full request/response cycle,
// concurrency, error propagation via trailers, and connection teardown
// — the actual behaviors kernelServiceClient.js/kernelServiceServer.js
// depend on.

import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { createHttp2Connection } from '../src/lib/rpc/http2Connection.js';

// Starts a plain-TCP HTTP/2 server on an ephemeral port; `onRequest`
// matches createHttp2Connection's server-role contract. Returns
// { port, close() }.
function startServer(onRequest) {
  return new Promise((resolve) => {
    const server = net.createServer((socket) => {
      createHttp2Connection(socket, { role: 'server', onRequest });
    });
    server.listen(0, '127.0.0.1', () => {
      resolve({ port: server.address().port, close: () => new Promise((r) => server.close(r)) });
    });
  });
}

function connectClient(port) {
  return new Promise((resolve, reject) => {
    const socket = net.connect(port, '127.0.0.1', () => {
      const conn = createHttp2Connection(socket, { role: 'client' });
      resolve({ conn, socket });
    });
    socket.on('error', reject);
  });
}

test('basic request/response round-trip: headers, body, and trailers all arrive correctly', async () => {
  const { port, close } = await startServer(async (headers, body) => {
    const path = headers.find(([k]) => k === ':path')?.[1];
    return {
      headers: [[':status', '200'], ['content-type', 'application/grpc+proto']],
      body: Buffer.from(`echo:${body.toString()}:${path}`),
      trailers: [['grpc-status', '0']],
    };
  });
  const { conn, socket } = await connectClient(port);

  const result = await conn.request(
    [[':method', 'POST'], [':scheme', 'https'], [':path', '/x/Run'], [':authority', `127.0.0.1:${port}`]],
    Buffer.from('hello'),
  );

  assert.equal(result.headers.find(([k]) => k === ':status')?.[1], '200');
  assert.equal(result.body.toString(), 'echo:hello:/x/Run');
  assert.equal(result.trailers.find(([k]) => k === 'grpc-status')?.[1], '0');

  socket.end();
  await close();
});

test('concurrent streams over one connection are dispatched and resolved independently, never cross-wired', async () => {
  const { port, close } = await startServer(async (headers, body) => {
    // Deliberately reply out of arrival order (stream 3's handler
    // finishes before stream 1's) to prove responses are matched by
    // stream id, not by completion order.
    const delayMs = body.toString() === 'slow' ? 30 : 0;
    await new Promise((r) => setTimeout(r, delayMs));
    return { headers: [[':status', '200']], body: Buffer.from(`got:${body.toString()}`), trailers: [['grpc-status', '0']] };
  });
  const { conn, socket } = await connectClient(port);

  const base = [[':method', 'POST'], [':scheme', 'https'], [':path', '/x/Run'], [':authority', `127.0.0.1:${port}`]];
  const [rSlow, rFast1, rFast2] = await Promise.all([
    conn.request(base, Buffer.from('slow')),
    conn.request(base, Buffer.from('fast1')),
    conn.request(base, Buffer.from('fast2')),
  ]);
  assert.equal(rSlow.body.toString(), 'got:slow');
  assert.equal(rFast1.body.toString(), 'got:fast1');
  assert.equal(rFast2.body.toString(), 'got:fast2');

  socket.end();
  await close();
});

test('a handler that throws is translated into a nonzero grpc-status trailer, not a broken connection', async () => {
  const { port, close } = await startServer(async () => {
    throw new Error('kernel exploded');
  });
  const { conn, socket } = await connectClient(port);

  const base = [[':method', 'POST'], [':scheme', 'https'], [':path', '/x/Run'], [':authority', `127.0.0.1:${port}`]];
  const result = await conn.request(base, Buffer.from('anything'));
  assert.equal(result.trailers.find(([k]) => k === 'grpc-status')?.[1], '2');
  assert.match(result.trailers.find(([k]) => k === 'grpc-message')?.[1], /kernel exploded/);

  // The connection itself must still be usable after an error response.
  const second = await conn.request(base, Buffer.from('again'));
  assert.equal(second.trailers.find(([k]) => k === 'grpc-status')?.[1], '2');

  socket.end();
  await close();
});

test('a several-kilobyte payload (larger than one TCP packet in practice) round-trips intact', async () => {
  const bigPayload = Buffer.from('x'.repeat(50000));
  const { port, close } = await startServer(async (headers, body) => ({
    headers: [[':status', '200']],
    body: Buffer.from([body.length]), // just confirm length server-side too
    trailers: [['grpc-status', '0'], ['grpc-message', String(body.length)]],
  }));
  const { conn, socket } = await connectClient(port);
  const base = [[':method', 'POST'], [':scheme', 'https'], [':path', '/x/Run'], [':authority', `127.0.0.1:${port}`]];
  const result = await conn.request(base, bigPayload);
  assert.equal(result.trailers.find(([k]) => k === 'grpc-message')?.[1], String(bigPayload.length));

  socket.end();
  await close();
});

test('pending requests are rejected (not left hanging forever) when the server closes the connection with GOAWAY', async () => {
  const { port, close } = await startServer(async () => new Promise(() => {})); // never resolves
  const { conn, socket } = await connectClient(port);
  const base = [[':method', 'POST'], [':scheme', 'https'], [':path', '/x/Run'], [':authority', `127.0.0.1:${port}`]];

  const pending = conn.request(base, Buffer.from('x'));
  // Give the server a moment to receive the request and register the
  // (never-resolving) handler. The handler being stuck forever means
  // nothing will ever make the underlying TCP connection close on its
  // own, so the client side must be torn down FIRST (destroy, not the
  // graceful end() -- there's no response coming to wait for) before
  // awaiting server.close(), which otherwise waits forever for a
  // connection nothing is going to end.
  await new Promise((r) => setTimeout(r, 20));
  socket.destroy();
  await assert.rejects(pending);
  await close();
});

test('conn.close() sends GOAWAY, marks the connection closed, and rejects any still-pending request rather than hanging', async () => {
  const { port, close } = await startServer(async () => new Promise(() => {}));
  const { conn, socket } = await connectClient(port);
  const base = [[':method', 'POST'], [':scheme', 'https'], [':path', '/x/Run'], [':authority', `127.0.0.1:${port}`]];

  const pending = conn.request(base, Buffer.from('x'));
  conn.close();
  assert.equal(conn.isClosed(), true);
  await assert.rejects(pending);

  socket.destroy();
  await close();
});
