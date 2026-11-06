# ADR 007 — Resource handles and lifetime

## Implementation checkpoint — 2026-09-23

Phase7 implements typed immutable CPU resource leases, async coalesced requests, last-good replacement, bounded residency and Diligent fence retirement. See [runtime resources](../runtime-resources.md) for the actual owners, APIs and limits. The private cache described below is the decision-time baseline, not the current loading contract.

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Implementation checkpoint — 2026-10-13: AudioRuntime now owns a typed immutable
PCM pool because its previous first-use file admission ran inside fixed audio
synchronization. This concrete blocking consumer uses the existing pool/lease
contract; no persistent loaded handle or parallel asset authority was introduced.
See [audio loading](../audio.md#loading-boundaries) for bounds and lifecycle.

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

## Tile Map decoded-chunk boundary

Keep the complete validated sparse cooked revision in an immutable owned archive.
Resource profiles may decode individual dense chunks through the existing pool's
asset/variant and lease rules. Both profiles retain the Map root identity/type;
local chunk coordinates are neither catalog entries nor durable AssetIds.
Workers receive pure owned values, not resource leases or pointers that owner
shutdown can revoke. Byte backing survives cache removal independently of paths.
Bound preparation inputs separately from adopted dense-page memory.

Logical cell lookup must remain exact regardless of decoded residency. Render
visibility must not silently remove physical geometry, authored cells, runtime
edits or neighbor-query evidence. Existing full-loading behavior remains supported.
The archive/pool foundation does not itself establish a complete streaming workflow.

## Mutable camera output realizations

A saved RenderTextureAsset revision owns only an immutable validated configuration.
Its pixels are a mutable view/device-local realization in the existing typed GPU
residency owner, distinct from immutable TextureAsset image data. Shared CPU pools
and read-only leases serve rendering and editor guides. Native draw bundles hold
leases beyond bindings; real completion fences and bounded physical payloads govern
retirement across resize and retained candidates. A same-frame produced set, not
lease existence, authorizes sampling. This deliberately extends the loaded-resource
boundary without adding persistent pixel identities or another resource manager.
See [the output contract](../render-textures.md).

## Mutable runtime probe realizations

A ReflectionProbe component owns capture intent; it is not a TextureAsset revision.
Derived view/device-local cubemaps reuse GpuResidency's existing strong leases,
physical payload reservations, thread/context scope and real completion fences.
A private entity/generation/slot key identifies a native configuration inside that
scope, never a catalog UUID, source location or CPU asset selection. Two payload
slots stage complete nonrecursive captures; only completed six-face convolution
becomes the ready lighting selection. Runtime maps do not overwrite immutable
TextureAsset pixels, generate source files or require another resource registry.
See [the bounded probe contract](../local-reflection-probes.md#runtime-gpu-capture).
