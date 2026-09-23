# ADR 007 — Resource handles and lifetime

## Implementation checkpoint — 2026-09-23

Phase7 implements typed immutable CPU resource leases, async coalesced requests, last-good replacement, bounded residency and Diligent fence retirement. See [runtime resources](../runtime-resources.md) for the actual owners, APIs and limits. The private cache described below is the decision-time baseline, not the current loading contract.

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Choose a combination: AssetRef<T> stays persistent; a future typed runtime resource
lease identifies a validated artifact revision in a subsystem-owned resource pool.
AssetId-only caches are insufficient once async loads, dependencies and GPU retirement
exist. Do not introduce a generic loaded AssetHandle into persisted ECS fields.
Resource slots carry generation and owner-world/device scope; revisions are immutable.

## Current evidence and implementation boundary

Current audio/animation/navigation caches are private, synchronously admitted
consumers. include/forge/assets.hpp has no loaded-resource handle. Phase 7 introduces
the first mesh/texture CPU/GPU consumer requiring explicit loading lifetime.

## Consequences

Requests are pending/dependency-pending/ready/failed/cancelled. A strong lease pins
one revision; weak references re-resolve and cannot prolong its life. Fallbacks are typed
and explicit. Publish a replacement at an owner boundary, notify subscribers with old/
new revision and retire old CPU/GPU data only after all leases/fences finish. Never
replace pointers in callbacks, audio mix code or an in-flight GPU command.

## Deferred work and exact trigger

Implement the minimal typed pool/lease at the first Phase 7 loaded rendering
resource. Preserve private subsystem ownership behind it; do not rewrite existing
caches solely for interface uniformity. Streaming adds partial residency to a revision,
not a new AssetId. Stale/cancelled completion cannot reactivate an unloaded request.
