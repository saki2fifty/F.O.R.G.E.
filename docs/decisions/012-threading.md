# ADR 012 — Thread and task ownership

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

World mutation belongs to the owning author/runtime thread. Flecs worker systems
use declared read/write access and native stages; merge at pipeline boundaries.
Rendering/presentation and GPU creation/retirement belong to their device thread.
Audio callbacks consume prepared runtime audio state without ECS/asset IO. Import and
Script candidates run in bounded workers; completion posts immutable results.

## Current evidence and implementation boundary

runtime_main.cpp owns the clock/requests, runtime.cpp owns fixed phase ordering,
engine services own external subsystem lifetimes, worker supervisors already isolate
converters. Fresh synthetic Flecs measurements show workload-dependent gains, not
permission to multithread every engine callback.

## PlaySession transport: narrow transport-only owner

The editor's `forge::PlaySession` runs an external FORGE runtime as a child
process. A dedicated background thread (`PlayTransportWorker`, implemented in
`src/editor/play_transport_worker.{hpp,cpp}`) exclusively owns the `SDL_Process*`
and its three `SDL_IOStream*`. The main thread keeps the protocol state machine,
JSON semantic validation, snapshot / candidate / ack application, Flecs / editor /
UI / GPU, and the scheduler.

This is forced by the pinned SDL3.4.16 doc contract:

- `SDL_GetProcessInput/Output/Properties` — "safe to call from any thread";
  main thread calls these once during launch to grab the IOStream pointers.
- `SDL_ReadIO` / `SDL_WriteIO` / `SDL_GetIOStatus` — "Do not use the same
  SDL_IOStream from two threads at once"; exclusive to the worker.
- `SDL_WaitProcess` / `SDL_KillProcess` / `SDL_DestroyProcess` /
  `SDL_ReadProcess` — "This function is not thread safe"; exclusive to the
  worker.

Single mailbox mutex + condition variable. Worker-LOCAL `in_flight_` is the
"am I busy?" flag; `mailbox_busy_` (under the same mutex) is the
"outstanding-request" gate covering queued + writing + awaiting + receipt-
ready. The mailbox mutex also protects `has_receipt_`, `receipt_payload_`,
and `receipt_received_at_ms_`. Two additional mutexes protect the failure
string (`failure_mutex_`) and the stderr tail (`stderr_mutex_`) as separate
narrow scopes. 5 s receive deadline uses `>` (strictly greater than) on
both the worker timeout check and the late-receipt check.

This is a narrow transport-only owner. It does NOT introduce an engine-wide
job scheduler; it does NOT change Flecs / Jolt / rendering / audio ownership.
See `docs/transport.md` for the byte-level contract and lifecycle pseudocode.

## Consequences

No worker retains a raw mutable world or editor document pointer across a job.
Every task has owner/generation, cancellation and bounded completion data. Publication
checks current source/dependency revisions. GPU upload fences and audio thread handoff
are subsystem boundaries, not ordinary ECS defer operations.

The PlaySession transport worker is a single std::thread per active session. It is
joined in `PlaySession::close_process()`; the worker thread is never detached. No
engine-wide scheduler is introduced; no parallel fixed systems are added.

## Deferred work and exact trigger

Phase 7 uses existing process/task supervision first. Introduce an engine-wide
job scheduler only when measured contention or dependent runtime jobs justify it.
Enable parallel fixed systems per safe workload with order/lifetime tests; Jolt's
internal workers do not authorize concurrent arbitrary Flecs writes.

The PlaySession transport worker is the only transport-bound background thread
in the editor. Future Play-session scalability work (e.g. snapshot pre-fetch)
should reuse this owner first; introducing additional editor-side background
threads requires a separate authorized scope.
