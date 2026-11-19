# ADR059 — Live recording across Scene activations

Status: accepted2026-11-19.

## Ownership and publication

Keep GameSession as the simulation/Scene owner and AudioRuntime as the per-World
mixer, callback and capture-buffer owner. Existing SDK Play state owns the one
live recording lease; GameRecordingTask owns media/output progress. Add no global
mixer, capture manager, simulation clock or project-state database.

A paused audio group does not imply a stopped device callback. Never move or swap
a live buffer pointer based on gameplay pause. Prepare the incoming World's
capture storage during candidate preparation, while allocation can still fail
without publishing a World. At activation, disable and quiesce the old capture,
copy its complete unread prefix into the prepared fresh buffer, preserve capture
generation/read counters and enable the incoming producer. Buffer addresses stay
stable; no allocation, source consumption or opaque native-queue copy belongs in
this publication step. Transfer failure preserves the source prefix and fails
only recording; ordinary game Scene adoption must continue.

Use caller-owned preallocated float storage with the existing pinned native ring
API. It makes wrapped retained spans available without depending on opaque queue
layout. Callback observation retains its allocation/locking/IO-free contract and
the same bounded queue. Normal single-Scene capture/restart stays compatible.

## Session observation and bounds

The opt-in live session profile must retain append-only Scene/generation/source
witnesses at actual cumulative PCM boundaries, including repeated Scene visits.
The initial recording identity remains stable across World replacement; current
World observations remain explicit. Reuse existing output grants, source/revision
checks, sequential reads, image-generation guards and partial receipt ownership.
Keep finite visit/PCM/image/file budgets. A changed World before an outstanding
Game copy is submitted fails capture.
Submitted private pixels and frozen provenance may finish GPU/file retirement
after Scene adoption; that image is never relabeled as another Scene. Do not
invent silence or claim physical sample-accurate audiovisual synchronization
across device/Scene changes.

Native controls and automation/MCP must expose the same implemented scope and
readiness. Default scene scope and existing offline session formats remain
compatible. Current host capability negotiation gates session discovery and
native controls. The shared complete workflow requires focused native/transport,
rendered media, warning, scoped sanitizer and documentation gates.
