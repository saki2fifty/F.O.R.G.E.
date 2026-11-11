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

## Sprite atlas frame ownership — accepted additive extension

An authored atlas recipe remains one ordinary logical Texture asset. Its cooked
pixels may carry a bounded table of stable frame keys and current regions within
the same immutable TextureData revision. Repacking changes placement, rather than
frame identity. The existing asset pipeline owns input dependencies, rebuilding,
validation, publication and last-good selection; no separate atlas registry or
frame asset family is introduced.

Sprite use of texture-owned frames is explicit and defaults off. Existing raw
grid/crop/region values keep their current meaning. Frame resolution and image
selection must be admitted together from the same strong Texture lease, including
filtering and replacement failure. Raw rectangles must not be rewritten across
Scenes or Prefabs when source pixels move. Material ownership remains unchanged.

Ordinary texture encoding retains its previous representation when frame metadata
is absent. Atlas publication uses an explicit supported output profile so older
engines reject required frame semantics. Existing texture profiles remain readable.
Public value-header changes require the matching SDK fingerprint; this decision
does not change the C host table layout. The implemented recipe, editor, import,
rendering and automation workflow is documented in the current
[Sprite contract](../sprites.md).

## Project UI Material domain — accepted additive extension

2026-11-11: Procedural UI pixel effects reuse the existing Shader and Material
asset families, supervised compiler, publication graph and immutable Material
Shader snapshot. Surface definition4 explicitly selects `domain:"ui"`; wrapper17
contains one `SurfaceUiColor` Pixel role. No parallel effect asset registry,
runtime source compiler or agent-only authoring path is introduced.

The UI host shares its project-scoped MeshResourceHost Material pool. Native UI
owns element geometry, layout, clipping, opacity and handle retirement. The
renderer realizes the retained cooked program through the existing presentation
cache, with numeric Material parameters and per-element UV/size/presenter time.
Candidate readiness uses non-owning shader observations in the existing native
document set, so unrelated live replacement failures cannot block a new scene.
UI Materials reject Mesh/Sprite geometry. The first domain has no texture bindings
or authored vertex stage. See [surface interfaces](../surface-shaders.md).

Runtime dependency declarations include the selected Material; its cooked bundle
retains the exact Shader snapshot. UI source requirements record effects1, and
runtime kits advertise `runtime_ui_effects:1`. Unsupported kits reject before
publication/export. Old Mesh/Sprite interfaces and packet roles retain their
meaning; this extension changes neither Scene identities nor the C host table.
Native execution and delivery evidence remain separate validation gates.

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
UI, automation and MCP share this workflow and its grants. This new-map route never
implicitly authorizes an overwrite or runtime recapture. Explicit source updates
follow the reviewed extension below. See the
[probe contract](../local-reflection-probes.md#editor-authored-map-snapshots).

### Reviewed captured-map source revisions — accepted extension

An explicit **Update assigned map** request uses the same capture/image, generated
source review and texture import/publication owners. The target must be the probe's
independent imported capture DDS Texture. Ordinary SourceCopy remains new-folder-only;
its optional generated-revision plan names one registered source and exact catalog,
source and settings-sidecar byte revisions. Shared/model-family sources reject.

Only one source file changes, through the existing atomic file-write primitive.
Its sidecar and catalog stay unchanged until the separate ordinary reimport; no
new journal, fake catalog generation, persisted identity or source document is
introduced. An interruption leaves the old or complete new raw source and the
previous cooked selection. Source-commit receipts distinguish a completed write
from a later durability warning. Reimport retains the root Texture ID and saved
settings; failure preserves last-good cooked content. Explicit source replacement
is outside Scene Undo. Optional map/yaw assignment retains the original scoped
Scene history rules. Runtime capture uses the separate derived-GPU extension below; automatic source overwrite remains unsupported.

## Weighted transparency — accepted extension

An optional Scene boolean selects bounded weighted Mesh color accumulation through
MeshSceneRenderer's existing queue and MeshDrawBundle specializations. The same
per-part materials, parameter values, geometry, leases and shared depth remain
responsible for authored intent. One derived attachment/resolve owner introduces
no asset identity, material database, fragment list or parallel render queue.
Whole-view preflight retains sorted transport when any visible layer is incompatible.

Capability9 appends ordinary and area-lit weighted color roles (19 total), preserving
readable1–8 archives and their bytes. Current Shader/Material publication and recook
rules apply; authored surface interfaces and installed C SDK ABI do not change.
The opt-in boolean uses existing strict Scene/history/Save/automation/MCP ownership.
See [weighted transparency](../weighted-transparency.md) for the numerical profile,
stage order, budgets, fallback and validation boundaries.

## Local diffuse probe illumination — accepted extension

An optional default-zero diffuse multiplier extends the existing ReflectionProbe
map and influence volume. The current MeshSceneRenderer/EnvironmentResidency owns
both diffuse and specular Cube leases and one priority-ordered borrowed lighting
selection. Prepared Mesh draws and the existing specular operator consume that
value without depending on each other's implementation. No second probe registry,
texture manager, world, cache lifecycle, frame target or clock is introduced.

Native PBR composition retains direct light, emission, global specular lobes and
material layering. Uniform irradiance sampling precedes spatial coverage branches;
world planes and camera-relative centers reuse the existing geometry owner. The
eight-active-assigned-probe resource bound covers both modes, whose zero multipliers
independently omit their contribution. Persistent draw bindings clear before map
leases retire. Missing source fields preserve previous lighting and source bytes.

Private Shader capability 13 deliberately versions the bounded input while retaining
old readers/roles and ordinary-light fallback for old receivers. The appended
installed value requires an exact SDK rebuild without changing ABI1 tables or
logical asset identity. Explicit map capture excludes local diffuse contributions,
keeping its existing nonrecursive snapshot boundary. Sampled irradiance grids,
rebaking and multi-bounce simulation are separate unimplemented capabilities.
See [the current contract](../local-diffuse-irradiance.md).


## Display-space spatial filtering

The optional [fast approximate anti-aliasing profile](../fast-approximate-antialiasing.md)
extends the existing Scene settings and DisplayResolve owner. It filters the final
composed display-encoded image, after HDR effects/tone mapping and before UI or
editor overlays. It does not create material programs, a wrapper capability,
source identity or a new resource registry. Disabled output follows the original
path. The presentation owner prepares bounded optional targets before publication;
Game and Scene preview share the operation. Linear HDR output and upstream temporal
history retain their ownership. The installed SDK value boundary and fingerprint remain unchanged; this does not
add a native callback. Export uses a runtime kit containing the implementation.

## Camera image composition

Camera remains the output-routing authority through an optional RenderTextureAsset
reference. Material owns its typed image role and existing sampling intent; a
MeshRenderer never owns an image handle or replacement texture database. Explicit
Material source/bundle3 versions the discriminator while preserving opaque old
extensions and ordinary asset identity. Per-view pass order derives from selected
Material reads and Camera writes, with feedback rejected before current sampling.
Linear HDR producers and normal consumer display composition reuse FrameRenderer.
This supplies the concrete resource/composition consumer for authored targets.
See [Scene camera render textures](../render-textures.md).


## Runtime UI in camera images —2026-11-03

A UiDocument may select an existing Render Texture through an optional typed
reference. The existing presenter partitions its live/staged Set by output;
resources, copied models, command validation, localization and time retain their
owners. CameraRenderTextures borrows composition after scene Cameras and before
current-image copy/publication. The shared graphics adapter supports linear HDR
premultiplied UI blending; the consuming view retains tone mapping. No new output
registry, render graph, input authority or gameplay clock is introduced. Main-only
protocol1 remains supported; positive routing uses private protocol2 and matching
runtime-kit admission. Mesh UVs remain authored surface data. See [Runtime UI](../runtime-ui.md).


## Bounded native UI filters —2026-11-03

UiDiligentRenderer owns transient transparent layers and blur/color composition;
UiPresenter retains source/candidate, context-group and presentation-time ownership.
One native payload admission helper serves drawing and isolated export inspection,
with pure parameter/kernel rules in the existing UI assets target. There is no new
Scene output registry, render graph, authored effects format or simulation clock.
RML/RCSS and named UI Values remain the authoring path for UI, automation and MCP.
RGBA8 and already-linear RGBA16 layers use their destination color domain.
Existing cooked UI source metadata records the required filter profile, and the
shared export/distribution gate checks the kit independently of routing protocol.
See [Runtime UI](../runtime-ui.md) for limits and failure semantics.

## Morphological spatial extension

The [bounded morphological profile](../morphological-antialiasing.md) follows the
existing DisplayResolve spatial boundary. It reuses the optional display output,
adds bounded edge/coverage scratch, and stages all requested targets before
publication. Only one spatial method may be enabled; authoring switches both
flags in one validated history operation. Scene/Game, HDR/temporal history,
UI ordering, installed SDK boundaries and resource ownership above are unchanged.
No lookup asset, second renderer, material program or parallel registry is added.

## Runtime probe capture — accepted extension

Default-off ReflectionProbe values select bounded view-local GPU capture through
FrameRenderer. The existing MeshResourceHost owns source selections and the same
MeshSceneRenderer probe selection supplies both base specular and diffuse lighting.
Canonical cameras and source filtering are shared with reviewed editor snapshots;
runtime does not invoke source publication, readback, registration or rebaking.

GpuResidency accepts a private derived configuration/key as well as its original
CPU revision acquisition. Runtime entity/generation/slot keys remain scoped to the
view/device, create no logical asset IDs and reuse exactly the existing physical
payload reservations, strong leases, real submission fences and retirement. A
complete six-face staged cube is convolved before admission. Static maps remain
fallbacks; no immutable TextureAsset data is modified. Capture source layers reject
camera-output readers; completed maps then supply camera-output and main consumers.
No new world, registry, resource manager, persistent frame graph or clock is added.
The [probe contract](../local-reflection-probes.md#runtime-gpu-capture) owns bounds,
reset/failure behavior, exact-SDK rebuild and conditional runtime-kit requirement.


## Retained triangle surface input —2026-11-06

UI Surface Input adds an explicit Mesh geometry profile while retaining Quad as
its source default. MeshSceneRenderer borrows its admitted draw/LOD/part/Material
and current pose; its existing picker supplies original triangle coordinates
through homogeneous clipping. A short-lived borrowed vertex evaluator owns the
shared morph/skin/wind calculation used by picking and UV reverse projection.
There is no replacement mesh cache, ray caster, input queue, world or authored
identity. UiPresenter retains native capture/focus/IME and current-output routing.

Transient anchors use data identity and triangle selection, not native handles.
LOD/revision changes cancel capture; current deformation remains usable. Reverse
UV queries reject distinct posed points at the same image coordinate. Geometry
validation and picking share bounded query work and never accept partial results
on exhaustion. Quad-only documents retain runtime requirement1; Mesh requires2.
Source defaults and the same metadata/history/automation owners keep old documents
readable, while updated public native layouts require the matching exact SDK.
See [Runtime UI](../runtime-ui.md#interactive-scene-surfaces).

## Texture authoring through retained triangles —2026-11-06

Scene Mesh texture painting reuses retained draw geometry/current deformation and
existing geometric picking for visible LOD/part/Material selection. The shared
Material/UV mapper returns transient data coordinates; the existing paint source
document, asynchronous brush, history and publication remain authoritative.
Disconnected UV paths share one coverage/history operation. There is no additional
raycaster, mesh cache, world, document, image catalog or history manager.

Native Scene input and explicit published triangle samples share bounded UV0 and
image-slot admission. Source/Scene/view/camera/selection changes cancel held input;
normal document session/revision checks govern asynchronous result adoption.
Programmatic mesh inspection/projection borrows immutable model-resource selection
without source parsing or Scene-visibility claims. Runtime consumes ordinary cooked
Texture/Material assets; this authoring path changes no source format, public SDK
layout, C ABI table, artifact format or runtime-kit feature. See
[Texture assets](../texture-assets.md) and [MCP](../mcp.md).
