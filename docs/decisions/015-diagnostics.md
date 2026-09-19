# ADR 015 — Diagnostics and profiling

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Problems stores actionable current issues keyed by owner/location/revision;
Console records chronology; Profiler measures performance. Flecs Alerts represent
ECS-state predicates, Flecs Stats/Metrics represent native ECS telemetry, and FORGE
scopes measure external systems. Preserve the distinctions when presenting them.

## Current evidence and implementation boundary

ecs_tools.cpp uses native world/system statistics, metrics and alerts;
editor Problems/Console are separate. Status FPS is frame-loop throughput and CPU
submission timing is not GPU execution timing. Empty authoring pipeline reduction is
avoided under the pinned issue registry.

## Consequences

Diagnostics carry category/severity, asset/entity/component/property/source line,
job/module/session/tick where known. Navigation resolves stable IDs and reports stale
targets; clearing logs does not erase an unresolved domain validation failure.
Compiler/import errors retain structured location plus original output.

## Deferred work and exact trigger

GPU timings begin only with timestamp-query/fence support and explicit latency.
Import/build profiling attaches to job IDs. Do not duplicate native Flecs history
reduction or invent parallel ECS counters for dashboard appearance. Shipping telemetry
is an explicit opt-in profile, not an exposed development REST endpoint.
