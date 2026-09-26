# PlaySession transport architecture

The editor's `forge::PlaySession` is the JSON-over-newline protocol
driver for the FORGE native runtime. The transport byte path is
owned by a dedicated background thread implemented in
`src/editor/play_transport_worker.hpp` /
`src/editor/play_transport_worker.cpp`. This document records the
exact contract.

## Why a background thread

The runtime is a separate OS process. Editor-side IO is done through
the SDL3 process API
(`SDL_CreateProcessWithProperties`,
`SDL_GetProcessInput/Output`, `SDL_ReadIO`, `SDL_WriteIO`,
`SDL_KillProcess`, `SDL_WaitProcess`, `SDL_DestroyProcess`).

Before the worker existed, `PlaySession::pump()` ran on the editor's
main thread, doing byte IO in the same pass that drew the RmlUi UI,
GPU frames, Flecs tooling, and snapshot publishing. Measured main-
thread stalls in the SDK acceptance fixture showed pump gaps of
multiple seconds when the runtime sent a large snapshot response.
A 5 s timeout was conflated with editor stall because both fired
from the main-thread clock: "runtime slow to respond" could not be
distinguished from "editor slow to apply".

## Single-owner model (SDL doc contract)

| Owner | What it touches |
|-------|-----------------|
| Main thread (`PlaySession`) | Protocol state machine (JSON parse, snapshot apply, candidates, acks), Flecs/editor/UI/GPU, scheduler. Reads `worker_.drain_one_line`/`drain_stderr`/`take_failure`. Submits via `worker_.submit`. Never touches the `SDL_Process*` or any `SDL_IOStream*` directly after `launch()`. |
| Worker thread (`PlayTransportWorker`) | The `SDL_Process*`, the stdin/stdout/stderr `SDL_IOStream*` exclusively. Calls `SDL_ReadIO`, `SDL_WriteIO`, `SDL_GetIOStatus`, `SDL_KillProcess`, `SDL_WaitProcess`, `SDL_DestroyProcess`. |

The single-owner split satisfies the SDL3.4.16 thread-safety
requirements:

- `SDL_GetProcessInput/Output/Properties`: "safe to call from any
  thread" — main thread uses these ONCE during `launch()` to grab
  the pointers, then never touches them again.
- `SDL_ReadIO`, `SDL_WriteIO`, `SDL_GetIOStatus`: "Do not use the
  same `SDL_IOStream` from two threads at once" — exclusive to the
  worker.
- `SDL_WaitProcess`, `SDL_KillProcess`, `SDL_DestroyProcess`,
  `SDL_ReadProcess`: "This function is not thread safe" — exclusive
  to the worker.

This is the selected owner policy for FORGE: the worker is the
narrow transport-only owner; the main thread keeps the protocol
state machine, JSON validation, snapshot / candidate / ack
application, Flecs / editor / UI / GPU, and the scheduler. The
split is what decouples byte arrival from editor frame cadence.
No engine-wide job scheduler is introduced by this design. See
ADR 012 for the threading ownership decision.

## Lifecycle

```
PlaySession::launch(initial, recovery):
    SDL_CreateProcessWithProperties(...)         // main thread (safe)
    worker_.start(process, in, out, err)        // spawns std::thread
    send({"command", "hello", ...})             // builds line + submit

PlaySession::pump():
    drain stderr via worker_.drain_stderr
    drain ONE receipt via worker_.drain_one_line
    if receipt present:
        parse JSON, enforce id / session correlation
        enforce 5 s deadline (received_at_ms - sent_at_monotonic_ms_
            > 5000 throws) before clearing waiting_
        apply response, clear waiting_
    take worker_.take_failure() — throw if non-empty
        (failure is checked AFTER receipt so a clean final Quit
        response on a process-exit is not discarded)

PlaySession::close_process():
    worker_.stop()       // sets stop_requested_, wakes cv
    worker_.join()       // waits for worker thread to finish
                         // (worker has called SDL_KillProcess +
                         //  SDL_WaitProcess + SDL_DestroyProcess)
    process_ = nullptr   // mirror the destruction locally
    clear local state

PlaySession::stop():
    close_process()
    clear sdk pending ack/epoch buffers, set status_ to "Stopped."

PlaySession::~PlaySession():
    stop()               // calls close_process(); defensive last-resort release
```

The worker is NEVER detached. The worker thread is owned by the
`PlayTransportWorker` member and joined in `close_process()`. The
destructor calls `stop()`, which calls `close_process()`, so there is
no double-join risk. `~PlayTransportWorker` is the last-resort
release of the std::thread.

EOF and process-exit are observed independently in the worker.
Either may be reported first by the underlying OS; the failure
string is whichever the worker observed. Both terminal signals
mean the runtime is no longer producing valid bytes; the receipt
(if any) is published via the bounded final-reply drain BEFORE
the failure path runs, so main can drain the receipt and then
observe the failure on the next `take_failure`.

## Queues and bounds

| Queue | Cap | Overflow behavior |
|-------|-----|-------------------|
| Outbound requests | 16 MiB per request | `worker_.submit()` returns false; caller decides retry |
| Inbound raw partial-line buffer | 16 MiB | Worker drops the data WITHOUT pushing a giant receipt copy and surfaces a failure. The cap is enforced by the shared `consume` helper on both the regular read phase and the bounded final-reply drain. |
| Stderr tail | 64 KiB | Head-drop; oldest bytes discarded first |

The protocol permits at most one in-flight request and at most one
published receipt at a time. The worker refuses to publish a
duplicate receipt while `has_receipt_` is set; refusing is a
protocol violation and surfaces a failure. The outbound 16 MiB cap
is checked before `submit()` moves the request into the mailbox.
The inbound 16 MiB cap is checked on `incoming_buffer_.size()`
AFTER each chunk append and BEFORE any framing / receipt move.

## Deadline algorithm

The receive deadline is measured from the worker monotonic clock:

    deadline_fire =
        waiting_ AND
        (worker_now - sent_at_monotonic_ms_ > 5000)

where `worker_now = PlayTransportWorker::monotonic_ms()` from a
`std::chrono::steady_clock` (single owner clock, used by both
threads so the timestamps are directly comparable).

When a receipt arrives, `PlaySession::pump()` checks the deadline
BEFORE clearing `waiting_` and BEFORE applying the response:

    if receipt.received_at_ms - sent_at_monotonic_ms_ > 5000:
        throw timeout

This separates "runtime slow to respond" from "editor slow to apply":
the worker records `received_at_ms` at the moment a newline is
read. If `received_at_ms - sent_at_monotonic_ms_ ≤ 5000`, the
runtime replied on time and the editor applies the response
immediately or after a UI stall — the deadline does not extend.
If `received_at_ms - sent_at_monotonic_ms_ > 5000`, the runtime is
genuinely slow and the receipt is rejected even though it arrived.
The 5 s bound is the existing implementation ceiling; it is
preserved unchanged.

Boundary is STRICTLY GREATER THAN on both sides:

- Worker timeout check: `now > pending_deadline_ms_`
- Late-receipt check: `read_ts > pending_deadline_ms_`

A receipt timestamped exactly at `sent_at_ms + 5000` is on time.

## Failure surfacing

The worker sets a single failure string on the first of:

- `SDL_WriteIO` returns 0 with status not `NOT_READY`.
- `SDL_ReadIO` returns 0 with status `EOF` or `ERROR` (treated as
  disconnect even if the process remains alive).
- `SDL_WaitProcess(process, false, ...)` returns true (process
  exited).
- Deadline exceeded (`now > pending_deadline_ms_`) while a request
  is in flight and no receipt is published.
- Receipt arrives with `read_ts > pending_deadline_ms_` (late
  receipt; never published as an acceptable receipt).
- Inbound raw buffer exceeds 16 MiB (data dropped, no giant copy).
- Receipt arrives with no pending request, or while a prior
  receipt is still queued (protocol violation; single-receipt
  protocol).
- An exception escapes `run_loop()` (caught at the outer try/catch;
  the message becomes the failure string; the `SDL_Process*` is still
  destroyed by the shutdown path).

EOF and process-exit are observed independently. The order in which
they reach the worker is OS-dependent; whichever the worker observed
first becomes the failure string. The dedicated stdout-closed-while-
alive test (scenario F2) pins the EOF path on a still-alive child;
the unexpected-exit-after-reply test (scenario I) pins the
process-exit path. The regular read phase and the bounded final-
reply drain share the same `consume` helper, so the cap, framing,
deadline and single-receipt guard are enforced uniformly.

Main thread drains via `worker_.take_failure()` AFTER processing
any queued receipt so a clean final Quit response is not discarded.
The throw only fires when `waiting_` is still true: a runtime exit
that arrives after the just-applied response is informational, not
a fault.

## Idle behavior

When no work is queued and the process is alive, the worker waits
on a condition variable for `kIdleWait` (2 ms). This avoids a CPU
busy-spin while keeping process-exit detection prompt.

The cv predicate is stop-only: `cv.wait_for(lk, kIdleWait,
[this]{ return stop_requested_.load(); })`. Stop notification ends
the wait early. Request notifications do not satisfy the predicate;
a request submitted while idle wakes the worker, the predicate is
re-checked (still false), and the wait continues. When idle, retry
after `kIdleWait`. Scheduling can delay resumption beyond
`kIdleWait`.

When a partial write returns `SDL_IO_STATUS_NOT_READY`, the worker
retains a per-entry `offset` so the partial bytes are re-queued
correctly. The outer loop is bounded to a fixed number of reads
per pass so the worker cannot busy-loop on the same partial entry;
it breaks to the read phase which does stdout drain and
`SDL_WaitProcess` between writes.

The bounded final-reply drain runs at most
`kReadIterationsPerPass` (32) reads of `kReadChunkBytes` (8 KiB)
without blocking on EOF. It does not wait for a fresh receipt to
land if one never arrives; it publishes whatever complete newline
is already buffered and lets the existing `fail_now(exit_diagnostic)`
path take over.

## Files

| File | Role |
|------|------|
| `src/editor/play_transport_worker.hpp` | Public class + Receipt struct |
| `src/editor/play_transport_worker.cpp` | Worker thread implementation |
| `src/editor/play.hpp` | PlaySession (uses the worker for IO) |
| `tests/play_transport_worker_tests.cpp` | Real-process regression test |
| `tests/sdk_play_editor_transport_tests.cpp` | Real-runtime SDK transport test (includes malformed-protocol rejection) |
| `docs/transport.md` | This file |
| `docs/decisions/012-threading.md` | Thread and task ownership decision |
| `manual/editor/play-mode.md` | Play manual owner (UI-facing) |
| `changelog/20260926/README.md` | Release note |
