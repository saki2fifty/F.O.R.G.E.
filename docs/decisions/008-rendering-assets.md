# ADR 008 — Rendering asset model

## Implementation checkpoint — 2026-09-23

Phase7 implements the shared Mesh/Material/Texture renderer, built-in mesh resources, PBR lighting, authored cameras, shadows, skinning/morphs and imported model placement. See [render features](../render-features.md), [asset formats](../asset-formats.md) and [backend capabilities](../render-backends.md). The CPU-only viewport below describes the decision-time baseline.

The original decision and its baseline/deferred descriptions below retain their
2026-09-19 context. This checkpoint and linked current contracts describe what has
since been delivered; historical future-tense text is not a current capability limit.

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Use an ECS rendering component with Mesh AssetRef, indexed Material AssetRefs and
visibility/shadow flags. Built-in shapes and imported meshes converge on this path.
Primitive/Tint remains supported blockout authoring until explicit migration exists.
Mesh/Material/Texture/Shader/Model are durable logical assets; Diligent buffers,
textures, bindings and pipeline objects are transient renderer resources.

## Current evidence and implementation boundary

Current editor/viewport.cpp draws CPU blockout geometry and has no production
mesh/material/camera pipeline. Ozz supplies poses but does not imply skinned rendering.
See the detailed [rendering contract](../rendering-foundation.md).

## Consequences

Do not store Diligent interfaces in durable assets or scene components. Material
owns surface parameters; future graphs compile to shader/material artifacts rather
than replace the ECS or renderer. Skinning binds skeleton identity, joint layout,
inverse bind matrices and palette revision explicitly.

## Deferred work and exact trigger

Phase 7 may implement the authorized first asset/rendering slice. Lighting, VFX,
graphs, LOD streaming and production export remain separately gated. Their extension
slots are defined in the contract, not shipped placeholder components. No parallel
permanent cube-only rendering path is approved.

## Phase7 material binding clarification — 2026-09-20

"Indexed Material AssetRefs" refers to resolving a cooked mesh's physical draw
slots. It must not make a source array index the durable identity of an authored
assignment. FORGE's actual glTF cooker uses source material index+1 for physical
slots, and its subasset reconciliation preserves MaterialAssetIds across reorder.
Persisting those raw slot numbers would therefore retarget overrides after a valid
source edit. The resource adapter now projects stable mesh-qualified binding tokens
from the resolved logical material IDs and retains physical indices only in the
current immutable revision. Sparse overrides use those tokens; removed bindings
remain unresolved instead of being reassigned. This preserves the original single
Mesh/Material path and ID families. It is a source-backed representation correction,
not a change to Flecs scene authority or authorization for a separate material graph.
