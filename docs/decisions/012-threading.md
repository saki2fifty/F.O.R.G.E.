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

## Consequences

No worker retains a raw mutable world or editor document pointer across a job.
Every task has owner/generation, cancellation and bounded completion data. Publication
checks current source/dependency revisions. GPU upload fences and audio thread handoff
are subsystem boundaries, not ordinary ECS defer operations.

## Deferred work and exact trigger

Phase 7 uses existing process/task supervision first. Introduce an engine-wide
job scheduler only when measured contention or dependent runtime jobs justify it.
Enable parallel fixed systems per safe workload with order/lifetime tests; Jolt's
internal workers do not authorize concurrent arbitrary Flecs writes.
