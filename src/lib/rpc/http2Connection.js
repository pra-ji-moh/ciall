// http2Connection.js; upgrade 8 — a from-scratch HTTP/2 connection
// state machine (RFC 7540), driving frames over a real socket (a
// tls.TLSSocket in production; a plain net.Socket or in-memory duplex
// pair in tests). Symmetric core used by both roles: 'client' opens
// streams via request(); 'server' receives them via the onRequest
// callback and replies through the same frame machinery.
//
// SCOPE, DISCLOSED (same discipline as hpack.js/http2Frame.js): this
// implements exactly what a single gRPC unary RPC needs — HEADERS/DATA/
// SETTINGS/WINDOW_UPDATE/RST_STREAM/GOAWAY/PING, basic flow-control
// accounting sized for small (claim/result-shaped, not multi-megabyte)
// payloads, and no CONTINUATION, PRIORITY, or PUSH_PROMISE handling
// (unused by this repo's traffic; see http2Frame.js). Concurrent
// streams over one connection ARE supported (client-initiated odd
// stream ids, server dispatches each independently), matching how
// orchestrator.js can have multiple kernel calls in flight at once.

import * as H2 from './http2Frame.js';
import { encodeHeaders, decodeHeaders } from './hpack.js';

const DEFAULT_INITIAL_WINDOW = 65535;
const WINDOW_UPDATE_THRESHOLD = 32768; // replenish once this many bytes have been consumed, not on every single frame

export const ERROR_CODE = { NO_ERROR: 0, PROTOCOL_ERROR: 1, INTERNAL_ERROR: 2, FLOW_CONTROL_ERROR: 3, CANCEL: 8 };

/**
 * Wraps `socket` (any duplex stream with .write()/.on('data'|'error'|'close')/.destroy()) in an HTTP/2 connection.
 *
 * opts:
 *  - role: 'client' | 'server'
 *  - onRequest(headerPairs, body): 'server' only. Called once per
 *    complete incoming request stream; must return (or resolve to)
 *    { headers, body, trailers } — headers/trailers are [name,value][]
 *    pairs, body a Buffer (possibly empty).
 *
 * Returns { request(headerPairs, body): Promise<{headers,body,trailers}>, close(), isClosed() }.
 * request() is 'client'-only (throws if called on a 'server' connection — a server never initiates streams in this repo's usage).
 */
export function createHttp2Connection(socket, { role, onRequest } = {}) {
  if (role !== 'client' && role !== 'server') throw new Error(`http2Connection: role must be 'client' or 'server', got "${role}"`);
  if (role === 'server' && typeof onRequest !== 'function') throw new Error('http2Connection: server role requires an onRequest(headerPairs, body) callback');

  let recvBuffer = Buffer.alloc(0);
  let prefaceValidated = role === 'client'; // only servers need to validate an INCOMING preface
  let nextClientStreamId = 1; // client-initiated stream ids are odd, per spec
  let closed = false;
  let connSendWindow = DEFAULT_INITIAL_WINDOW;
  let connRecvConsumedSinceUpdate = 0;

  const streams = new Map(); // streamId -> stream state (see below)
  const pendingRequests = new Map(); // streamId -> { resolve, reject } (client role only)

  function send(buf) {
    if (closed) return;
    socket.write(buf);
  }

  function failAllPending(err) {
    for (const [, pending] of pendingRequests) pending.reject(err);
    pendingRequests.clear();
    streams.clear();
  }

  function requireSendWindow(streamState, byteLength) {
    if (connSendWindow < byteLength || streamState.sendWindow < byteLength) {
      // Our realistic payloads (a claim or a kernel result, MessagePack-
      // encoded) are always far smaller than the 65535-byte default
      // window, and each stream sends its whole body in one DATA frame
      // — so this only fires if the peer has fallen far behind on
      // WINDOW_UPDATEs (e.g. many concurrent large-ish requests). A
      // real implementation would queue and wait; this one fails loudly
      // instead of silently violating flow control, which is the
      // correct choice for the payload sizes this repo actually sends.
      throw new Error(`http2: flow control window exhausted (connection=${connSendWindow}, stream=${streamState.sendWindow}, need ${byteLength}) — payload too large for this implementation's un-queued send path`);
    }
    connSendWindow -= byteLength;
    streamState.sendWindow -= byteLength;
  }

  // ── client: open a new stream and send the whole request ───────────
  function request(headerPairs, bodyBytes) {
    if (role !== 'client') throw new Error('http2Connection: request() is only valid on a client-role connection');
    return new Promise((resolve, reject) => {
      if (closed) { reject(new Error('http2 connection is closed')); return; }
      const streamId = nextClientStreamId;
      nextClientStreamId += 2;
      const streamState = { responseHeaders: null, dataChunks: [], sendWindow: DEFAULT_INITIAL_WINDOW, recvConsumedSinceUpdate: 0 };
      streams.set(streamId, streamState);
      pendingRequests.set(streamId, { resolve, reject });
      try {
        const headerBlock = encodeHeaders(headerPairs);
        send(H2.encodeHeadersFrame({ streamId, headerBlock, endStream: false }));
        requireSendWindow(streamState, bodyBytes.length);
        send(H2.encodeDataFrame({ streamId, data: bodyBytes, endStream: true }));
      } catch (err) {
        pendingRequests.delete(streamId);
        streams.delete(streamId);
        reject(err);
      }
    });
  }

  function finishClientStream(streamId, streamState, trailerPairs) {
    const pending = pendingRequests.get(streamId);
    pendingRequests.delete(streamId);
    streams.delete(streamId);
    if (!pending) return;
    pending.resolve({ headers: streamState.responseHeaders, body: Buffer.concat(streamState.dataChunks), trailers: trailerPairs });
  }

  // ── server: a complete incoming request stream is ready to dispatch ─
  async function dispatchServerRequest(streamId, streamState) {
    streams.delete(streamId);
    const body = Buffer.concat(streamState.dataChunks);
    let result;
    try {
      result = await onRequest(streamState.requestHeaders, body);
    } catch (err) {
      result = {
        headers: [[':status', '200'], ['content-type', 'application/grpc+proto']],
        body: Buffer.alloc(0),
        trailers: [['grpc-status', '2'], ['grpc-message', String(err && err.message || err)]],
      };
    }
    if (closed) return;
    const respStreamState = { sendWindow: DEFAULT_INITIAL_WINDOW };
    try {
      send(H2.encodeHeadersFrame({ streamId, headerBlock: encodeHeaders(result.headers), endStream: false }));
      const body2 = result.body || Buffer.alloc(0);
      if (body2.length > 0) {
        requireSendWindow(respStreamState, body2.length);
        send(H2.encodeDataFrame({ streamId, data: body2, endStream: false }));
      }
      send(H2.encodeHeadersFrame({ streamId, headerBlock: encodeHeaders(result.trailers), endStream: true }));
    } catch (err) {
      // A flow-control failure (or any other send-time error) writing
      // the response: reset the stream rather than leaving the peer
      // waiting forever for a response that will never arrive.
      send(H2.encodeRstStreamFrame({ streamId, errorCode: ERROR_CODE.INTERNAL_ERROR }));
    }
  }

  // ── inbound frame dispatch ──────────────────────────────────────────
  function handleHeadersFrame(frame) {
    const headerPairs = decodeHeaders(frame.payload);
    const endStream = (frame.flags & H2.FLAG.END_STREAM) !== 0;
    const streamId = frame.streamId;

    if (role === 'server') {
      let s = streams.get(streamId);
      if (!s) {
        // A new request stream from the client.
        s = { requestHeaders: headerPairs, dataChunks: [], sendWindow: DEFAULT_INITIAL_WINDOW, recvConsumedSinceUpdate: 0 };
        streams.set(streamId, s);
        if (endStream) dispatchServerRequest(streamId, s); // headers-only request (empty body) — not this repo's shape, but handled correctly
        return;
      }
      // A second HEADERS frame on a stream the server already has open
      // would be a client-sent trailer, which this repo's client never
      // sends (its single DATA frame always carries END_STREAM) — ignore
      // rather than error, matching "ignore what you don't use."
      return;
    }

    // client role: first HEADERS on a stream we opened is the response;
    // a second is the trailers.
    const s = streams.get(streamId);
    if (!s) return; // unknown/already-finished stream; ignore
    if (s.responseHeaders === null) {
      s.responseHeaders = headerPairs;
      if (endStream) finishClientStream(streamId, s, []); // headers-only response, no trailers frame coming
    } else {
      finishClientStream(streamId, s, headerPairs);
    }
  }

  function handleDataFrame(frame) {
    // Connection-level flow-control receive accounting.
    connRecvConsumedSinceUpdate += frame.payload.length;
    if (connRecvConsumedSinceUpdate >= WINDOW_UPDATE_THRESHOLD) {
      send(H2.encodeWindowUpdateFrame({ streamId: 0, increment: connRecvConsumedSinceUpdate }));
      connRecvConsumedSinceUpdate = 0;
    }

    const s = streams.get(frame.streamId);
    if (!s) return; // stream already finished/unknown; ignore its trailing DATA
    s.dataChunks.push(frame.payload);
    s.recvConsumedSinceUpdate += frame.payload.length;
    if (s.recvConsumedSinceUpdate >= WINDOW_UPDATE_THRESHOLD) {
      send(H2.encodeWindowUpdateFrame({ streamId: frame.streamId, increment: s.recvConsumedSinceUpdate }));
      s.recvConsumedSinceUpdate = 0;
    }

    const endStream = (frame.flags & H2.FLAG.END_STREAM) !== 0;
    if (!endStream) return;
    if (role === 'server') dispatchServerRequest(frame.streamId, s);
    else finishClientStream(frame.streamId, s, null); // defensive: a client peer that ends the stream via DATA rather than trailers still resolves, with trailers:null
  }

  function handleFrame(frame) {
    switch (frame.type) {
      case H2.FRAME_TYPE.SETTINGS:
        if ((frame.flags & H2.FLAG.ACK) === 0) send(H2.encodeSettingsFrame([], { ack: true }));
        break;
      case H2.FRAME_TYPE.WINDOW_UPDATE: {
        const inc = H2.decodeWindowUpdatePayload(frame.payload);
        if (frame.streamId === 0) connSendWindow += inc;
        else { const s = streams.get(frame.streamId); if (s) s.sendWindow += inc; }
        break;
      }
      case H2.FRAME_TYPE.HEADERS:
        handleHeadersFrame(frame);
        break;
      case H2.FRAME_TYPE.DATA:
        handleDataFrame(frame);
        break;
      case H2.FRAME_TYPE.RST_STREAM: {
        const pending = pendingRequests.get(frame.streamId);
        if (pending) { pending.reject(new Error(`stream ${frame.streamId} reset by peer (error code ${H2.decodeRstStreamPayload(frame.payload)})`)); pendingRequests.delete(frame.streamId); }
        streams.delete(frame.streamId);
        break;
      }
      case H2.FRAME_TYPE.GOAWAY: {
        const g = H2.decodeGoawayPayload(frame.payload);
        closed = true;
        failAllPending(new Error(`connection closing (GOAWAY, error code ${g.errorCode})`));
        break;
      }
      case H2.FRAME_TYPE.PING:
        if ((frame.flags & H2.FLAG.ACK) === 0) send(H2.encodeFrame({ type: H2.FRAME_TYPE.PING, flags: H2.FLAG.ACK, streamId: 0, payload: frame.payload }));
        break;
      default:
        break; // PRIORITY/PUSH_PROMISE/CONTINUATION: unused by this repo's traffic, ignored rather than erroring
    }
  }

  function onSocketData(chunk) {
    recvBuffer = recvBuffer.length ? Buffer.concat([recvBuffer, chunk]) : chunk;

    if (!prefaceValidated) {
      if (recvBuffer.length < H2.CONNECTION_PREFACE.length) return;
      const got = recvBuffer.subarray(0, H2.CONNECTION_PREFACE.length);
      if (!got.equals(H2.CONNECTION_PREFACE)) {
        failAllPending(new Error('http2: peer did not send the expected connection preface'));
        closed = true;
        socket.destroy();
        return;
      }
      recvBuffer = recvBuffer.subarray(H2.CONNECTION_PREFACE.length);
      prefaceValidated = true;
    }

    for (;;) {
      const result = H2.tryReadFrame(recvBuffer);
      if (!result) break;
      recvBuffer = recvBuffer.subarray(result.bytesConsumed);
      try {
        handleFrame(result.frame);
      } catch (err) {
        // A malformed frame from the peer (e.g. bad HPACK) — reset just
        // that stream rather than tearing down the whole connection,
        // when we can tell which stream it was.
        if (result.frame.streamId) send(H2.encodeRstStreamFrame({ streamId: result.frame.streamId, errorCode: ERROR_CODE.PROTOCOL_ERROR }));
      }
    }
  }

  socket.on('data', onSocketData);
  socket.on('error', (err) => { closed = true; failAllPending(err); });
  socket.on('close', () => { closed = true; failAllPending(new Error('http2 connection closed')); });

  // Every HTTP/2 connection, both roles, sends a SETTINGS frame as its
  // first frame (RFC 7540 §3.5) — for a client, immediately after the
  // magic preface string; a server has no preface string of its own to
  // send (only to validate on receipt), so its SETTINGS frame IS its
  // whole side of the handshake.
  if (role === 'client') send(H2.CONNECTION_PREFACE);
  send(H2.encodeSettingsFrame([]));

  return {
    request,
    close(errorCode = ERROR_CODE.NO_ERROR) {
      if (closed) return;
      const lastStreamId = Math.max(0, ...[...streams.keys()]);
      send(H2.encodeGoawayFrame({ lastStreamId, errorCode }));
      closed = true;
      failAllPending(new Error('connection closed locally'));
      socket.end();
    },
    isClosed: () => closed,
  };
}
