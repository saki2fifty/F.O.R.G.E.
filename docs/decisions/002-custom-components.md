# ADR 002 — Custom gameplay component authoring

## Implementation checkpoint — 2026-09-23

Phase7 implements opt-in custom native Meta schema inspection, generic supported-property drawers, authored scene/prefab persistence and independent overrides/Revert, migration candidates and exact-SDK Play admission. See [custom component authoring](../custom-component-authoring.md) for supported types and limits.

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Adopt an opt-in plain-value authoring contract, detailed in
[custom components](../custom-component-authoring.md). Stable namespaced type and
property keys identify authored meaning; Flecs Meta defines structure and Doc/Units/
Ranges define presentation metadata. An isolated SDK inspection worker exports a
bounded copied schema; the editor reconstructs admitted native Meta values, never
loads project DLLs and never copies ABI-layout bytes between processes.

## Current evidence and implementation boundary

The freeze baseline admitted15 built-in codecs. Phase7 now admits17 with the
MeshRenderer and ModelSource CPU components; unknown
payloads survive but arbitrary SDK components are runtime-only. A fresh native Meta
probe transferred named values between different C layouts and proved equal-value
IsA ownership/Revert. Pinned member-offset issue requires physical-order registration
and layout verification. Runtime Inspector/persistence parity is not already delivered.

## Consequences

No second reflection system, arbitrary C++ serializer, resource pointer persistence
or requirement to change EntityId/AssetId. Preserve explicit equal-value/property
intent and independent local TRS channels. SDK compatibility and authored schema
compatibility are different checks.

## Deferred work and exact trigger

Implement schema export/admission, generic codec/drawer and prefab integration as
one bounded custom-authoring package before exposing Add Component for a project type.
Until then show runtime-only honestly. Asset pipeline code must accept opaque component
payloads and not depend on custom authoring being implemented.
