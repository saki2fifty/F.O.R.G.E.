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
preserves prior layout/bytes; explicit version 2 participates in the existing layout digest.
Reimport/rebuild uses existing asset IDs, dependency publication and embedded
last-good Material snapshots. There is no separate renderer capability registry,
new authored HLSL interface, entity identity or material ownership system.


Temporal anti-aliasing extended this same wrapper capability to version 3.
Capability3 provides ordinary color, depth and temporal color roles with strict
role/buffer validation; implicit1 and explicit2 retain their ordinary read behavior.
An engine-owned virtual temporal interface and shared Mesh motion implementation
preserve existing Shader assets, recipe provenance, Material snapshots, publication
and resource ownership. Changing per-frame GPU texture inputs uses Diligent dynamic
bindings through the same presentation owner, without a parallel resource database.
The [temporal contract](../temporal-antialiasing.md) governs profile admission,
view/reset history and validation limits. This extends the accepted asset boundary;
it does not replace it or change the installed C SDK interface.

## Indirect-light outputs — accepted extension

Ambient occlusion uses the existing generated surface/draw/view owners to
produce current indirect radiance separately from current full HDR. It must not
darken punctual light, emission, fog color or current-frame layers. This extension governs renderer, authoring and validation together; Windows
cooking, full editor and physical acceptance remain separate from local software
rendering and authoring evidence.

Wrapper capability 4 extends the existing three roles with
`surface_indirect_color` (color/indirect targets) and
`surface_temporal_indirect_color` (color/motion/indirect targets). The generated
function uses an explicit indirect output parameter. Existing capability 1/2/3
artifacts retain their readable roles, bytes and ordinary behavior. Participating
older surfaces require a new cook through the existing Shader/Material publication
and last-good owners, with the same logical asset IDs; an unsupported view retains
ordinary draw order with a diagnostic. No parallel capability registry or authored
HLSL input interface is introduced.

The existing pinned lighting API resolves the final environment contribution from
copied lighting state with punctual terms zeroed and copied shading state with
emission zeroed. The existing fog owner supplies transmittance for that contribution.
FORGE adds the generated data routing and spatial operator independently; native
lighting remains attributed to its selected dependency. A bounded view-owned
attachment and operator run before accumulation/motion/focus and current layers,
without a second lighting manager, pose database or clock. The first occlusion
profile may reconstruct geometric normals from current depth; this does not imply
material-normal or hidden-surface reconstruction support.

## Base screen-space reflection outputs — accepted extension

The next bounded reflection profile reuses the surface, part, queue and view owners
above. It replaces only the base SpecularIBL contribution, retaining broad indirect
multiple-scattering energy and independent clearcoat/sheen lobes. Native public
lighting APIs resolve the isolated old contribution and unit-radiance response;
FORGE does not copy the BRDF. The response is independent of environment intensity.
Actual shaded normals and roughness belong to the same current material evaluation.
An unproved or unsupported material response retains its environment contribution.

Capability5 appends ordinary and temporal reflection roles while retaining all
five capability4 roles and readable1–4 artifacts. Reflection roles carry color,
optional motion, total indirect, view-space shaded normal/roughness, base response
and replaceable base environment specular: five or six MRTs. They require the
matching current cook/reader through existing publication and last-good owners,
with unchanged logical IDs. Artifact role count can expand to eight; ordinary
non-surface stage admission remains six. Actual role/buffer/MRT tests govern
admission; reflection metadata does not invent output semantics.

The existing view supplies bounded staged inputs/results. An original current-depth
trace updates both full color and total indirect before ambient occlusion, followed
by the existing optional AA/motion/focus and current-frame layers. Misses preserve
environment lighting. No new lighting, pose, view, clock or agent-authoring manager
is introduced. The [reflection contract](../screen-space-reflections.md) defines the implemented
bounded profile. Local renderer/authoring evidence is separate from queued
Windows cooking, full-editor and physical acceptance.


## Material-normal occlusion outputs — accepted extension

Optional material-normal refinement extends the existing spatial AO owner. Its
Scene setting defaults to zero, preserving geometric AO for existing content.
Mapped base normals come from the current material evaluation in view space;
invalid or backward normals use geometry, and geometric-hemisphere rejection
remains active to prevent flat-plane self-occlusion. No normal-map resampling or
second lighting implementation is introduced in the AO pass.

Capability6 deliberately appends `surface_normal_color` and
`surface_temporal_normal_color` to the seven capability5 roles. The artifacts now
admit nine Pixel entries; the ordinary non-surface stage limit remains six.
Minimal three/four MRTs carry HDR, optional motion, total indirect and shaded
normal/roughness. Old1–5 role IDs, layouts, archive reads and bytes are retained.
Older selected surface cooks need a normal-role recook only when opting into the
new normal-only profile; existing reflection roles already provide that image.
Logical asset IDs and publication/last-good ownership remain unchanged.

The existing TemporalView shares its normal image between reflections and AO.
AO-only refinement adds8bytes/pixel within the same staged payload limit; enabled
reflections need no second normal image. The existing per-part specialization
cache, queue, posed/instanced draw path, Scene/Game hosts and authoring operation
remain authoritative. The [AO contract](../ambient-occlusion.md) describes the
profile and limits. This does not change the installed C SDK or imply hidden
geometry, temporal AO history, Windows cooking or physical-hardware acceptance.

## Bounded roughness-aware reflection sources — accepted extension

Optional source filtering stays within the existing current-depth reflection and
TemporalView owners. A default-off Scene boolean uses the shared partial authoring
operation. TemporalView owns, stages, reuses and releases at most six image pairs;
SurfaceReflections owns only reduction/lookup pipelines, bindings and constants.
Source images contain premultiplied current HDR/geometry coverage and valid depth
bounds. They are built after local probes and before screen-reflection replacement,
so reflected output never feeds its own source. The existing checked Game/Scene
payload admission counts exact ceil-sized levels within the same 512 MiB budgets.

No material surface roles, artifact capabilities/bytes, logical asset IDs or
installed SDK interfaces change. The bounded footprint approximates visible source
averaging; it does not imply hidden geometry, new reflection lobes or full physical
BRDF convolution. The [reflection contract](../screen-space-reflections.md) owns
limits and stage behavior. No parallel lighting/resource/authoring manager is added.

## Editor-authored reflection snapshots — accepted extension

Explicit reflection-map capture remains an operation of ViewImageTask. Its scoped
native job borrows the existing MeshResourceHost, FrameRenderer and single
presentation readback/fence owner. A detached authored snapshot supplies six
world-aligned linear-HDR faces; it introduces no world, image manager, asset
registry or clock. It excludes recursive reflection and display/history effects.

The original restricted DDS source encoder is a pure forge_texture operation.
SourceImport/SourceCopy retain review, raw-byte/catalog guards, NEW-folder copying
and ordinary texture-worker publication. A new map receives its identity from that
publisher. Scene map/yaw assignment uses the existing atomic command/history owner
at the original project/document/Scene/revision; stale results remain unassigned.
UI, automation and MCP share this workflow and its grants. This does not authorize
an overwrite writer, runtime recapture or automatic stable-ID rebaking. See the
[probe contract](../local-reflection-probes.md#editor-authored-map-snapshots).
