# ADR 008 — Rendering asset model

## Phase104 object value clarification — 2026-10-15

Material assets remain the owners of shared defaults, textures, shader/pass
selection and immutable resources. `MeshRenderer.material_parameters` owns sparse
per-object numeric intent, addressed by the existing logical slot token plus the
material's stable parameter key. Scene/Prefab/history and C++ use the same typed
value; no new asset identity, registry, material manager or graph VM is introduced.

The existing renderer Entry admits values with its exact resource selection and
retains the last complete usable selection on replacement failure. Numeric data
does not enter shared bundle keys. Reusable dynamic buffers are prepared before
adoption, patched from immutable defaults for each draw, and restored between
objects. Compatible equal values retain instancing. Feature-selecting changes
remain material variations. See [material values](../material-assets.md).

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

## Phase 11 editable Mesh checkpoint — 2026-10-04

A project-authored `.mesh.json` source publishes one root Mesh AssetId through the existing candidate/cache/catalog path. The source owns stable polygon and corner-UV data; the cooked `MeshData` and existing Mesh Renderer/resource pool remain the runtime path. Imported Model Mesh subassets remain read-only. Scene transforms and material overrides remain scene-owned. See [editable Mesh contract](../editable-meshes.md).

## Phase10 material graph decision — 2026-09-29

Extend the existing Shader logical asset with authored graph source version3 and
surface metadata version2 opting into existing metallic/roughness PBR. The graph
is the only authored program; generated HLSL/interface are immutable projections.
Material variations retain MaterialSource sparse parameter/texture overrides.
Typed graph/source history uses AssetSourceDocument and DocumentWorkspace; compiler
projection is UI independent and has no runtime VM. Publication, resource admission,
export and shader ownership remain with their existing subsystems. Old stage
programs and final-color surfaces remain supported. See [graph contract](../material-graphs.md).

## Engine surface wrapper capability

Scene distance fog uses the existing generated Shader wrapper and Mesh draw owners.
The Shader artifact records `surface_wrapper_version:2` separately from reflection:
a compiler can remove an unused fog binding from a current diagnostic-only function.
Binding presence therefore cannot identify wrapper capability. Implicit legacy 1
preserves prior layout/bytes; current version 2 participates in the existing layout digest.
Reimport/rebuild uses existing asset IDs, dependency publication and embedded
last-good Material snapshots. There is no separate renderer capability registry,
new authored HLSL interface, entity identity or material ownership system.


Temporal anti-aliasing extends this same wrapper capability to version 3. New
surface cooks provide ordinary color, depth and temporal color roles with strict
role/buffer validation; implicit1 and explicit2 retain their ordinary read behavior.
An engine-owned virtual temporal interface and shared Mesh motion implementation
preserve existing Shader assets, recipe provenance, Material snapshots, publication
and resource ownership. Changing per-frame GPU texture inputs uses Diligent dynamic
bindings through the same presentation owner, without a parallel resource database.
The [temporal contract](../temporal-antialiasing.md) governs profile admission,
view/reset history and validation limits. This extends the accepted asset boundary;
it does not replace it or change the installed C SDK interface.
