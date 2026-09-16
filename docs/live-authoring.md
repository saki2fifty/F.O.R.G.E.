# Live authoring transport 1

The editor explicitly enables a `LiveAuthoring` listener on IPv4 `127.0.0.1` with an OS-assigned port. This is a raw TCP JSON-lines adapter around [Authoring API 1](authoring-api.md), not HTTP, WebSocket, Flecs remote API or MCP. No new external dependency is introduced. Runtime links neither this transport nor the authoring service.

## Permission and identity

The editor chooses read-only or scene-edit access at startup of each listener. An OS cryptographic random generator supplies a 256-bit token: Windows `BCryptGenRandom` with the system-preferred RNG or Linux `getrandom`. Discovery is authenticated too. Only the UI's explicit Copy action exposes connection JSON. Tokens are never stored in settings, projects or status logs. Local tools holding the token have the advertised authority; a compromised editor/user account is outside this isolation boundary. Stop, editor exit and document generation changes revoke the listener/token. Restart generates a new token and document session identity.

The connection JSON contains `transport:1`, `host:"127.0.0.1"`, `port`, and `token`. The token is distinct from `AuthoringSession.target.session`, which is only a routing identity. Scene file UUIDs and non-scene asset targets remain future formats.

## Framing and dispatch

Open a TCP connection for one request and one response. Send a single UTF-8 JSON line, then read the response line. No pipelining or subscriptions. The server closes after sending the response. Envelope:

```json
{"transport":1,"token":"SECRET","id":"client-generated-correlation-id","request":{"api":1,"method":"discover"}}
```

Successful discovery returns the underlying scene target/revision, capabilities, catalog and schema, plus actual mutation availability and transport limits. Read-only discovery excludes mutation methods and commands. Response envelope:

```json
{"transport":1,"id":"client-generated-correlation-id","next_sequence":1,"response":{"api":1,"ok":true,"revision":1,"result":{}}}
```

`id` must contain 1–64 bytes and is echoed for correlation; it is not the replay key. `response` uses the shared API result/error schema. Scene methods require the complete target. Edits require `expected_revision` and a positive integer outer `sequence` equal to `next_sequence`.

Sockets are nonblocking. The editor owner thread accepts/reads/validates/commits/sends in its frame pump, with at most 64 KiB read and 64 KiB written per frame and one request dispatched per frame. It handles one active client, has a listen backlog of one, a 10-second total connection deadline, a 1 MiB request-line bound excluding newline, a 64-level nesting bound, and a 16 MiB response bound including newline. Backlogged connection acceptance is OS controlled. Oversized/pipelined input closes that client; malformed JSON returns a structured failure when possible. A response that exceeds the bound returns `limit_exceeded`; clients should use paged queries for large reads. These defensive caps do not guarantee large-scene frame latency: JSON parsing, scene reconstruction and serialization still run synchronously.

Edits return `busy` during play, native builds, file operations, popups and active UI gestures. Reads describe committed authoring data. Save/Save As/project operations and native execution are not exposed. New supported domains must join the service with their own capability/target contracts.

## Retry and cancellation

Mutation sequence allocation is shared by clients of one listener. Start at the `next_sequence` returned by discovery or a response. An accepted next sequence is consumed even if validation returns a failure or `busy`. The listener keeps up to 32 request/result receipts with an 8 MiB combined serialized size cap. Repeating a retained sequence with exactly equal inner request JSON returns its original result without mutation, even if the scene has since changed. A different request gets `sequence_conflict`; an older evicted sequence gets `receipt_expired` and never executes. Future/out-of-order sequences get `invalid_sequence`. Denied capability/credentials do not allocate receipts. Restart clears receipts and invalidates all old credentials/targets.

There is no automatic retry in the included Python client. After loss of a response, an integration may repeat the identical numbered request to retrieve its receipt; an expired receipt requires resynchronization with the scene. A returned old receipt describes its original revision. Never create a new sequence blindly to retry an uncertain commit. Multiple clients must rediscover/resynchronize on sequence conflicts.

Disconnect before a complete request cancels it. Once a complete request commits, disconnect does not undo it. Stop discards incomplete input and pending output but preserves authored state and normal undo. No transient drag or native job can be created through this API.

## Validation and sources

`authoring_live` tests real sockets, capability/auth rejection, fragmented/malformed/oversized/deep input, stale and foreign targets, duplicate/conflicting/expired sequences, disconnected requests and the packaged Python client. Separate process tests prove writer rejection and reacquisition after a killed process. Editor tests cover failed project switches, generation revocation, regular Save versus Save As, token rotation and scaled controls.

API contracts were checked against [Microsoft CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew), [ioctlsocket](https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-ioctlsocket), [BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom), and Linux [flock](https://man7.org/linux/man-pages/man2/flock.2.html), [getrandom](https://man7.org/linux/man-pages/man2/getrandom.2.html) and [socket](https://man7.org/linux/man-pages/man2/socket.2.html). Windows SDK/toolchain identity remains part of the CI cache fingerprint. Linux coverage is a service test harness, not a shipped Linux editor.
