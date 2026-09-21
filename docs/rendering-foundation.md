# Rendering foundation contract

Design for later authorized implementation; no production mesh/material renderer
is delivered by this document. Current Primitive/Tint blockout rendering remains
supported. See [ADR008](decisions/008-rendering-assets.md) and
[ADR009](decisions/009-coordinates.md).

## Asset and runtime boundaries

An ECS renderable references one Mesh asset and material slots. Visibility, shadow
participation and render-layer flags are explicit values. It never owns a Diligent
buffer/texture pointer. Model containers reference separately identified Mesh,
Material, Skeleton and Clip outputs. Built-in geometry will generate the same Mesh
artifacts as imported content. Migration of Primitive/Tint requires a versioned,
undoable authored conversion and a default-material mapping; it is not automatic.

| Asset | Durable contract | Transient realization / deferred trigger |
| --- | --- | --- |
| Mesh | Versioned vertex streams: positions, normals, tangent.xyz/sign, named UV sets, optional vertex colors; bounded index buffer, topology, submesh ranges/material slots, local bounds; optional joints/weights and inverse-bind association | Diligent uploads validated immutable CPU data. Initial topology is triangle lists; reject unsupported topology rather than reinterpret. LODs extend revision metadata when their consumer is authorized. |
| Material | Shader/material-model reference; stable parameter keys and typed scalar/vector/linear-color/texture values; opaque/masked/blended mode, alpha cutoff, cull/double-sided and depth intent; explicit defaults | Renderer owns compatible bindings/pipeline state. One built-in default material supplies missing assignment. Future material graphs compile to this contract; instances/variants add parent dependency plus explicit overrides and cycle checks. |
| Texture | Source image, dimensions, declared linear/sRGB/data/normal semantic, mip policy, compression intent, channel interpretation and platform variants; sampler filtering/wrap remains explicit binding data | CPU decode and GPU texture leases have independent lifetime. D3D formats are cooked variants, not universal source identity. Thumbnails are derived editor artifacts, not runtime authority. |
| Shader | Source dependency graph, language/entry points, defines/permutation key, backend/profile and compiler version; reflected resource/parameter layout digest | Diligent owns compiled shaders/pipeline objects. Material compatibility checks reflection before publication. Keep last-good pipeline on compile/link failure; future Shader Graph emits source/artifacts through the same path. |

Mesh CPU retention is a declared usage policy: editor modeling/navigation/collision
consumers retain or request validated CPU data; a shipping render-only mesh may
release CPU streams after upload. Collision geometry is an explicit derived asset,
not an untracked pointer into a render buffer. Source editable topology is distinct
from optimized triangulated runtime streams; modeling/sculpting/UV tools require
domain topology identities and remapping, not ECS entities per vertex.

## Coordinates and interoperability

Existing transform math is authoritative: +Y up, meters, normalized xyzw quaternion,
row-major 3x4 affine storage applied to column vectors, parent*local composition.
Current editor view looks along positive view Z and uses D3D depth [0,1]. World
translation and affine evaluation are double precision; mesh-local/render upload
values are bounded floats, with a future camera-relative upload boundary for large
coordinates. A right-handed world-vector cross product and a positive-depth camera
projection are separate conventions; do not infer one from a matrix's memory order.

Imported outward triangles use counterclockwise object-space winding. UV(0,0) is
top-left; tangent w stores basis sign. Converters own coordinate/UV/bind-pose changes
once, including determinant/winding and normal/tangent correction. Keep linear
lighting values distinct from sRGB color texture sampling. These choices align with
the relevant data conventions in the [official glTF2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html);
FORGE camera controls remain its own convention. Source names are not unique IDs,
so importer subasset reconciliation cannot rely on names alone.

Before enabling imported rendering, an asymmetric basis/winding/UV/normal-map/color
fixture must verify Diligent, Jolt, Ozz and Recast adapter consistency. The signed-scale blockout revision uses parity-specific culling and an explicit
double-sided singular/planar path. It cannot alone prove imported face winding,
normal-map handedness or skeletal rendering.
Do not label an untested conversion as supported.

## Cameras, lighting and effects

Native reflected Camera and Light components and their validated CPU adapters are
implemented during Phase7. See[cameras and lights](cameras-lights.md) for projection,
units, imported basis, numerical admission and history ownership. This is not yet
production rendering: Game still uses the preview camera until that consumer is
connected. Scene's editor navigation camera remains personal transient state.

Directional, point and spot lights provide authored intent. Environment/sky is an
asset plus scene-level settings; skybox and image-based lighting remain renderer
resources. Shadow formats, passes and budgets belong to renderer profiles. The CPU
components alone do not establish rendered shadows or lighting capability.

VFX is a logical asset with dependencies; an emitter component references it and
contains instance parameters. Its editor is a central document with local graph
selection. The renderer consumes VFX outputs through the render extraction boundary;
CPU/GPU simulation ownership is declared per effect/profile. No particles or universal
graph VM are implemented by reserving this boundary.

## Skeletal bridge

Model → mesh skin binding → Skeleton AssetId/revision + joint layout digest → inverse
bind matrices and weights → Animator/Ozz pose → model-space joint matrices → skinning
palette → renderer. Mesh and animation must agree on skeleton identity, joint order,
rest pose and conversion provenance. Reject incompatible bindings before GPU upload;
show a diagnostic bind-pose/debug skeleton fallback when a dependency is missing.
Ozz remains the pose evaluator and owns its existing runtime state.

Initial GPU skinning consumes an immutable per-frame palette, with an initial admission limit of256 joints per draw and4 influences per vertex.
Larger skins require validated mesh partitioning or a separately declared profile;
never truncate joints/weights silently. A CPU skinning path is a deliberate fallback,
not assumed delivered. The first implementation must prove these numeric limits
against its buffer/shader profile before enabling that profile. Palette index/weight
bounds and normalization are validated, not trusted. Morph targets/retargeting and
advanced animation tools extend asset contracts in later authorized work.

## Failure and publication rules

Candidate artifacts and GPU realizations validate before selection changes. Shader
reflection changes that invalidate a material block publication of that candidate
set. Old resources remain alive through CPU leases and GPU fences. Render extraction
reads a stable presentation snapshot, never editor draft memory. Rendering hot reload
must not restart the editor or reinterpret stale handles as new resources.

## Phase7 CPU geometry checkpoint

[Cooked meshes](mesh-assets.md) and their [typed runtime CPU leases](runtime-resources.md)
are implemented and tested through the pinned native glTF decoder. Full production
GPU mesh/material/skin realization remains in progress. This does not alter the
separate authored TRS or presentation ownership contract.

The in-progress[texture data/import contract](texture-assets.md) records actual CPU
capabilities and pending GPU/editor work.

[Cooked material values](material-assets.md) and immutable typed CPU material
selections are now implemented. Their layout checks do not replace the pending
Diligent reflection adapter or GPU material implementation.

## Authored mesh component checkpoint

`forge.mesh_renderer` now stores the typed `MeshRenderer` Flecs component: a Mesh
AssetRef, a sparse material-assignment collection, enabled/visible and cast/receive
shadow flags, and a32-bit layer mask. These are authored values; GPU objects, resource
leases, bounds and resolved draw-slot ordinals remain derived presentation state.
The component is not yet advertised in Add Component: its production renderer and
collection Inspector consumer are still being integrated. Primitive/Tint remains
unchanged until the explicit conversion workflow is ready.

Each material assignment has a mesh-qualified `slot` key and typed Material AssetRef.
An absent entry follows the mesh's current default; an entry with a null Material reference
explicitly chooses FORGE's built-in default. Keys are unique,1–255 ASCII bytes from
letters, digits, underscore, hyphen, dot and colon. The initial authored list limit
is4096 entries within the shared reflected-value envelope. Removed mesh-slot keys
remain unresolved intent, not a request to assign another physical slot. See
[mesh resources](runtime-resources.md) for the runtime resolution boundary.

Native Meta describes the component, references, nested entries and engine-owned
string/vector adapters. Registration verifies physical member offsets against the
C++ types. Flecs IsA ownership supplies component inheritance. Existing structured
prefab field intent treats the entire material list as one property: an equal-value
edit still creates intent, unrelated fields continue following source publication,
and Revert removes that list intent with scene Undo/Redo. There is no per-entry
prefab merge or Apply-to-Prefab operation.

Unknown nested entry fields survive scene round trips and list reordering by their
existing unique slot key. Engine-owned metadata marks that key on the native Meta
member; it is not a new asset/entity identity or reflection registry. Internal
unknown fragments never retain copies of known native field values. Native values
win on serialization; removing an entry does not transfer its opaque fields to a
new key. Other reflected collections without a declared key use positional unknown
fragments and make no semantic identity claim.

The in-progress[shader asset contract](shader-assets.md) records the current
source/permutation/cooked reflection boundary and outstanding production integration.

## Native presentation resources — Phase7 implementation

The private `forge_presentation_diligent` target owns per-device native
`IRenderStateCache` resources without any Flecs world or ImGui dependency.
Scene/Game blockout views share shaders and full graphics pipelines through
Diligent's content-based cache. Their camera/object constants are mutable SRB
bindings owned by each viewport, never shared pipeline static variables.
Active PSOs/SRBs retain native references across cache reset and viewport destruction.
The cache resets after256 newly created wrapper-managed shader/PSO entries;
this bounds retained entry history, not GPU memory bytes. Native PBR creates a
bounded, lazy utility resource set once per device. No serialized cache file or
unvalidated native live shader reload is enabled. Asset candidate validation and
cooked-resource provenance remain separate requirements.

`forge_diligent_pbr_native` compiles the unmodified pinned FX `PBR_Renderer.cpp`
and native memory shader-source factory, with upstream shader-header generation.
The full FX umbrella stays disabled: its CMake file unconditionally fetches EnTT
and publicly links ImGui/AssetLoader. FORGE needs its PBR algorithms and utilities,
not another scene owner. This composition adds no dependency pin or vendor patch.
The native PBR utility supplies default textures, GGX/sheen lookup textures and
environment convolution. FORGE's signed/zero-safe mesh shader and actual material,
light/shadow and skin/morph bindings remain required before production PBR is
considered integrated. A native utility test is not that completion claim.

Windows fixtures cover two independently moved views sharing native pipeline
states, live rendering after cache reset, fallback texture pixels and constant
radiance preservation across every face/mip of native IBL convolution. Validation
of this new native subset is pending until its source-only Windows audit passes.

### Cooked mesh and texture upload

The same backend-private target admits cooked CPU data before creating a detached
native GPU candidate. It keeps every mesh LOD, indexed topology, vertex channel,
integer joint index, tangent sign, prepared palette and morph channel. Base streams
are interleaved without numerical conversion; morph streams retain named byte
offsets in a raw immutable buffer. Material slot numbers remain revision-local.
No source parser, gameplay entity or catalog mutation is available to upload code.

Texture upload covers every admitted FORGE format through one mapping shared with
native import, including sRGB and block-compressed formats. Complete mip chains,
2D/array/cube/cube-array/volume subresources use explicit row and depth strides.
Adapter dimension, array and sampled-format capability checks precede allocation.
Sampler binding preserves filter, wrap, comparison, anisotropy, LOD and border
settings; it is a separate native resource so two materials may sample one texture
differently. Unsupported capabilities diagnose rather than silently substitute.
This does not by itself wire imported materials or skinning into Scene/Game.

Local importer/recipe regressions pass after extracting the format mapping.
Compiler syntax checks cover the native upload adapters and GPU test code on the
exact Linux headers; Windows byte-readback execution remains a separate gate.

### Signed and singular surface frames

The engine `ForgeSurface.fxh` utility transforms normals through scaled cofactors,
removing determinant sign for ordinary reflected transforms and retaining oriented
area normals for useful rank-two cases. Tangents are transformed and orthogonalized;
the transformed source bitangent establishes UV/reflection handedness. Collapsed
normal/tangent directions are explicitly marked invalid, never normalized through
zero or replaced by an invented arbitrary axis. The normal-map adapter retains the
base normal when tangent space is undefined. The consuming renderer still needs
its geometric-face fallback and diagnostic policy.

A native compute fixture compares these GPU frames against FORGE's double CPU
normal transform for reflection, nonuniform scale, shear, rank-two collapse,
complete collapse and tiny/large uniform magnitudes, with both source tangent signs.
This is a mathematical adapter and pending native fixture, not completion evidence
for imported skinned rendering or scene normal-map appearance.

The punctual shader adapter composes native PBR with a source-verified spotlight
correction and a finite-output boundary; see[the dependency issue](dependency-known-issues.md).
Its boolean result must feed the eventual renderer's numeric diagnostics, rather
than silently claiming an unrepresentable contribution rendered correctly.

### Visibility and mesh detail selection

Private CPU bounds preparation transforms all eight mesh-bound corners through the
derived affine matrix. This preserves conservative extents for reflection, shear
and zero scale. Visibility uses the actual admitted camera projection, including
image flips, imported camera basis, orthographic and infinite-far modes. The six
D3D clip-plane tests reject only bounds entirely outside a plane; camera-relative
double arithmetic avoids first converting distant world positions to GPU floats.

LOD selection uses projected bound diameter divided by viewport height, clamped
to[0,1], with the cooked decreasing thresholds. Bounds crossing the eye plane keep
full detail. A two-unit box whose nearest face is four units from a90-degree camera
has25% vertical screen coverage. This library is tested with large origins and
singular transforms; connecting selected draw parts and deformed bounds to the
production renderer remains part of Phase7. Static bounds must not cull animated
geometry until the deformation consumer supplies conservative updated bounds.

### Built-in material model admission

`prepare_pbr_material` validates the three existing glTF-derived material models
against model-owned parameter declarations and texture roles before a selected
model material becomes a CPU resource. It checks names, exact scalar/color types,
ranges, workflow compatibility,2D texture semantics and ordinary surface samplers.
Generic custom Shader materials retain their independent reflected-layout boundary.
The selected exact MaterialLayout comes from these declarations after validation;
copying arbitrary input names/types into a layout is not compatibility evidence.

Default expansion creates a derived copy and never creates authored overrides.
Absent attenuation distance stays absent. The adapter retains negative normal-map
strength, alpha cutoffs above one, HDR specular-color factors, IOR zero or at least
one, and arbitrary preserved UV-set numbers. Iridescence thickness endpoints may
be reversed, explicitly permitted by the exact Khronos extension. Double evaluation
of the IOR reflectance ratio avoids unnecessary intermediate float overflow.
Native shading is connected to the shared Scene/Game mesh path. Optical transport
validation is tracked below; admitting a parameter alone never proves rendering.

### Built-in vertex fetch

The native input-layout contract declares16 elements, while cooked meshes preserve
up to64 streams and material bindings can address higher UV-set numbers. The
built-in shader adapter uses Diligent raw shader-resource views of immutable vertex
buffers with native indexed draws. It reads only the selected named channels,
preserves integer joints and tangent handedness, and maps requested UV-set numbers
to transient shader slots without substituting UV0. Ordinary vertex-buffer binding
remains available to other shader consumers. This adds no source parser or second
renderer API. Missing channels and invalid byte ranges reject before shader use.

The generated record contains position, optional normal/tangent/color, optional
four-influence skin inputs and the material's used UV sets. Reading skin inputs is
not yet the deformation operation. An indexed Windows draw fixture checks UV19,
uint joint values, tangent.w and color3-to-color4 defaults with a vertex stride above
2048 bytes. Native GPU execution of this new adapter remains pending.

### Material shader bindings

The built-in adapter generates a uniform block for validated parameter values and
UV transforms, with separate native textures and samplers for each role. Changing
factors, UV offsets/scales/rotation or sampler settings preserves the shader source
and native pipeline cache key. Texture transforms follow offset + rotation * scale
* UV. Native sRGB views perform color conversion exactly once. Explicit texture
gradients are evaluated before conditional failure handling. Nonfinite UVs,
gradients or texels report failure to the consuming shader, which must present its
numeric diagnostic rather than treating zero as a successful sample.

CPU tests cover layout stability, transformed UV rows and invalid slot maps. The
Windows indexed sampling fixture checks UV19, repeat versus clamp on a shared
sRGB texture, native pipeline reuse and finite diagnostic output for UV overflow.
This fixture's execution is pending; production material draws are still in progress.

### Prepared mesh draw integration

The backend-private `MeshDraw` composes cooked vertex access and material sampling
with camera-relative transforms, depth/alpha state and the pinned native PBR BRDF.
Native MR inputs adapt the admitted IOR reflectance through the five-argument
reflectance function; FORGE does not copy that BRDF. Its initial punctual pass
accepts an explicitly bounded light list and diagnoses unrepresentable light
packing. A draw list/pass owner must perform visibility, layer selection and
transparent sorting before calling it. No implicit game light is added.

Culling combines authored affine parity with camera projection orientation;
rank-deficient surfaces use the explicit two-sided path. Normals use the existing
cofactor adapter and a geometric-face fallback. Texture-space derivatives are
evaluated before fragment rejection. Per-material texture views/samplers and
mutable draw constants remain scoped to their prepared native bindings.

The adapter currently rejects skin/morph bindings until a pose consumer is admitted.
Environment, HDR and shadow passes connect to Scene/Game with retained GPU resource
ownership. The following sections record validation separately for each consumer;
this ongoing Phase7 source is not a shipped renderer completion claim.

Mesh draw shader source and binding preparation are separate from native device
allocation. This permits local compilation of the actual generated programs before
Windows execution, including all three workflows with and without textures. Local
DXC shader-model6 checks are supplementary; selected FXC5.1/D3D12 acceptance still
requires Windows and is not inferred from another compiler's success.

The prepared draw carries the authored tangent and bitangent through its signed
surface frame into the pixel stage. Normal mapping prefers that explicit basis,
including tangent.w. It reconstructs from selected UV derivatives only when the
authored frame is absent or collapsed; supplied tangents are ignored when source
normals are absent. Double-sided shading reverses the complete perturbed normal.
A constant-UV native fixture uses opposite tangent signs under directional light
to expose an implementation that silently substitutes screen-space derivatives.

`ModelDrawCandidate` coordinates CPU preparation against one copied catalog and
caller-owned publication epoch. It captures exact mesh/material/texture revision
leases and exposes them only after all dependencies are ready. The presentation
owner must keep its previous GPU bundle until complete GPU construction succeeds,
then adopt only if that epoch still matches. Cancellation releases this consumer's
references without cancelling shared pool requests. Changed catalog epochs reject
unpublished candidates; unresolved authored material-slot names remain diagnostics.
This internal coordinator is not yet the Scene/Game render owner and does not claim
that CPU readiness is GPU publication. Its epoch is process-local publication
tracking, not a new persistent identity or catalog authority.

`MeshDrawBundle` performs detached native construction for the complete candidate,
including every LOD/material part and its texture bindings. It retains GPU leases
before native draw members so destruction releases SRBs first. Each draw validates
owner lifetime and marks leased resources for fence retirement. CPU scopes may close
after upload without invalidating that physical bundle. Failed construction never
changes the caller's previous bundle. Host integration, production pass ordering,
extended materials and deformed draw support remain separate acceptance work.

### Scene host connection — validation in progress

The private `MeshResourceHost` shares the existing typed CPU pools and physical
residency owners within one project/device/context. A copied catalog publication
advances its process-local epoch. Each `MeshSceneRenderer` retains complete GPU
candidates per extracted entity without owning a second authored hierarchy.
Per-view caches retain prior draws on preparation failure, report entity-specific
Problems, filter light/camera layers, and use full-affine bounds and LOD thresholds.
The Scene viewport invalidates a retained EDIT image on resource adoption. Model
components take precedence over legacy Primitive/Tint for that entity.

Project changes release scene bundles before replacing the host. Texture-import
publication feeds its admitted catalog snapshot to the host. External model reimport
watching and model import UI still require their shared publication connection.
The editor now selects linear RGBA16F/D32 targets and the display resolve described
below. Shadow/IBL, remaining material/deformation consumers, authored Game cameras
and standalone presentation remain unfinished Phase7 work. This connection does not declare the
production renderer complete or imply unvalidated Windows acceptance.

Punctual-light numerical admission first recognizes a valid zero contribution when
all contributing layers face away from the light. This avoids asking the native
BRDF to normalize a zero half-vector for ordinary opposite-view back lighting.
Undefined nonzero contributions remain failures; a zero contribution is not one.

The metallic-roughness draw now feeds the pinned native clearcoat layer, using its
IOR1.5 reflectance and layered composition. Red intensity and green roughness
texture channels multiply their respective factors. Clearcoat uses the unperturbed
geometric normal unless its own normal map exists; it does not inherit base normal
mapping. Clearcoat normal admission requires an authored normal/tangent frame or a
base normal map, matching the exact glTF extension. Both normal-map consumers share
pixel-frame orthogonalization and safe derivative fallback. Native visual acceptance
is still pending for this increment.

### Extended reflection layers

The metallic-roughness adapter now supplies the pinned native iridescence, sheen
and anisotropic BRDFs. These are shading consumers, not new material authorities.
Iridescence uses red intensity and green thickness interpolation, including
reversed authored thickness endpoints. Zero thickness removes the film. Its
incident dielectric reflectance includes the admitted material IOR, specular color
and specular weight; the pinned RenderPBR sample's fixed .04 input would lose those
combined-extension semantics. The native `EvalIridescence` spectral calculation is
used unchanged. No dependency patch or local replacement BRDF is introduced.

Sheen uses linear color after one sRGB texture decode and the roughness texture's
alpha channel. Its native directional-albedo compensation uses the presentation
owner's `PBR_Renderer` preintegrated sheen texture with linear/clamp sampling.
Construction receives the actual device context explicitly; bindings retain native
resources. Zero sheen color omits the layer and lookup binding. Clearcoat remains
above sheen in the native resolve order.

Anisotropy requires authored normal/tangent attributes or a base normal map, as
specified by glTF. Red/green encode tangent-space direction; blue multiplies
strength; authored rotation is in radians. Frame reconstruction preserves tangent
handedness and world reflection. The native anisotropic BRDF receives the required
strength-squared roughness interpolation. A genuinely collapsed directional frame
produces the existing diagnostic color, rather than choosing an arbitrary tangent.
No visual LocalScale is clamped or rejected by this shading rule.

Exact evidence: DiligentFX `aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`,
`Shaders/PBR/private/{RenderPBR.psh,Iridescence.fxh}`,
`Shaders/PBR/public/PBR_Shading.fxh`, `Shaders/Common/public/PBR_Common.fxh`, and
Khronos glTF `c18432787e6d545a1218c1926ccdcfaffd4c116b` extension specifications
for iridescence, sheen and anisotropy. Verified2026-09-21. Native FXC/WARP execution
of these new consumers remains pending; local compilation is not GPU acceptance.
The HDR/IBL/shadow connections and the optical transport adapter are described
below with their actual validation status.

### Queues and HDR display — native validation in progress

Visible mesh parts enter distinct opaque, masked and blended queues. Opaque/masked
parts group by reflection parity, material and mesh identity. Blended parts sort by
camera-forward depth of their current transformed bounds, descending, before state
locality. Persistent EntityId and part number break ties deterministically; extraction
or native entity allocation order is not the draw order. Per-part culling follows
coarse mesh culling and the selected LOD. Imported glTF BLEND disables depth writes,
retains depth testing and uses straight-alpha composition. Conventional part-level
sorting cannot correctly resolve every intersecting transparent surface or cyclic
overlap; order-independent transparency is not implemented.

Editor Scene and current Game preview color targets are linear RGBA16F with D32
depth. The reusable `DisplayResolve` calls the pinned Diligent `ToneMapping.fxh`
PBR Neutral operator, applies manual exposure as `2^EV`, then calls native
`LinearToSRGB` once. The resulting RGBA8_UNORM display image contains encoded sRGB
values; the current ImGui/UNORM swapchain presentation performs no second transfer.
The grid and other editor overlays are composed after this resolve. The legacy
LDR viewport construction remains available for existing regression comparisons.

The half-float lighting target represents finite magnitudes through65504. The
resolve diagnoses nonfinite lighting/exposure results with magenta instead of
propagating them into display output. Authoring values are not rewritten. This
is SDR display from HDR lighting, not an HDR-monitor output mode. Target resize
stages replacement resources before releasing the previous textures.

The Scene View exposure preference is independent of authored cameras and game
settings. It persists with the existing personal editor preferences and invalidates
the retained frame. Useful UI range is[-20,+20]EV; the backend separately validates
finite representability. Automatic eye adaptation is not enabled.

Exact mapper evidence: DiligentFX `aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`,
`Shaders/PostProcess/ToneMapping/public/{ToneMapping,ToneMappingStructures}.fxh`
and `Shaders/Common/public/SRGBUtilities.fxh`, verified2026-09-21. Local16-stage
shader compilation, native C++ syntax and normal/strict-sanitizer queue tests pass;
FXC/WARP validation of these changes is pending.

### Cached environment realization and material lighting

`EnvironmentResidency` specializes the existing GPU revision owner with an immutable
convolution policy. It consumes the same admitted TextureAsset CPU lease, retaining
its existing AssetId/revision/generation/variant identity. The policy is private to
one owner scope; no new persistent environment handle or parallel cache lifecycle
is introduced. Byte/count reservation, failure accounting, reuse, owner-thread
access and fence retirement use the same implementation as mesh/texture uploads.

Diligent's pinned `PrecomputeCubemaps` writes caller-owned diffuse irradiance, GGX
and Charlie cubemaps. The adapter accepts color/HDR cube or equirectangular2D inputs,
uses source rows with positiveY at the top for equirectangular maps, and counts the
source upload plus all output faces/mips in its payload budget. The native zero
MipLevels descriptor means the full chain and is accounted accordingly. Default
quality is64 diffuse/256 specular pixels; quality policy changes use a distinct
owner scope. Acquiring an unchanged revision reuses the generated maps.

Prepared PBR draws now call native `ApplyIBL`, including clearcoat, sheen and
anisotropic reflection. Intensity and world-Y rotation are per-draw values. An
absent environment binds a shared black cube with zero IBL intensity; no implicit
game lighting is added. The owning caller retains the environment lease while its
bindings are live. Scene-level environment authoring, resource selection and sky
composition are still required consumers in the ongoing Phase7 batch.

Matching material sampler states share one shader binding. All native lighting
lookups share their compatible linear/clamp sampler. Different material states stay
independent. This reduces descriptor pressure without changing texture semantics.
Microsoft's [D3D12 binding tiers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/hardware-support)
define16 per-stage table samplers on Tier1 and2048 on higher tiers; the new native
fixture exercises all15 connected texture roles with distinct material samplers plus
one lighting sampler. No sampler count or hardware compatibility is inferred solely
from local shader compilation.

Exact source: pinned PBR_Renderer.hpp/cpp caller-owned output APIs and PBR_Shading.fxh
`ApplyIBL`, verified2026-09-21. The preceding a3cf2ee native audit passed37/37 in58.10s
for reflection layers; environment/HDR/queue native execution is pending separately.

### Scene environment selection and sky

Scene-owned `rendering` settings version1 contain environment TextureAsset identity,
intensity, Y rotation in radians, sky visibility, and game exposure in stops[-20,+20].
Missing settings preserve the previous default: no implicit environment lighting.
These are scene/document values rather than entity transform components. The shared
`scene.rendering.set` command patches only requested known fields, preserves unknown
metadata, validates before commit, and uses Scene Undo/Redo. Effective snapshots carry
those same detached values. Invalid native producer settings emit a structured
`render.settings.invalid` diagnostic.

Texture selection now accepts both standalone cooked texture assets and model
subassets. Copied catalog metadata selects an immutable DDC revision; worker loading
checks its complete format, file digests, importer recipe and typed variant. Color
selection prefers HDR when present, otherwise color; it never substitutes normal/data.
The import publisher and standalone runtime selection share the same cooked bundle
validator. No image decode or original-source read occurs on the presentation thread.

Environment GPU adoption retains the previous usable maps after a failed replacement.
Clearing the reference intentionally removes them. Mesh bundles replace **all**
parity/LOD bindings, including invisible parts, before releasing an old environment
lease. The sky releases its native cube/sphere binding cache before releasing that
lease. This keeps native references within residency accounting and fence retirement.

The exact FX`aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`
[EnvMapRenderer](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/Components/src/EnvMapRenderer.cpp)
and `Shaders/Common/private/EnvMap.psh` provide the sky PSO, cube/equirectangular
sampling, Y rotation, and far-depth testing without depth writes. FORGE disables
native sky tone mapping/sRGB output and resolves the complete HDR frame once.
Sky renders before transparent meshes. Existing foreground depth remains protected.

**Evidence-based adapter correction:** native `SampleEnvMap` divides its unprojected
far point by W. A true infinite-far projection gives W=0 at depth1; absolute float
world coordinates also lose small directions at large camera positions. FORGE passes
a transient camera-relative unit-depth ray-plane matrix to that native helper:
`right*x/P00 + up*y/P11 + forward`, W=1. Orthographic rays use constant forward.
This preserves perspective/axis-flip direction, ignores translation as a sky should,
and requires no authored transform inverse or fabricated scene hierarchy. CPU tests
cover infinite far, large origins, orthographic views, flips and invalid projection.
Native sky tests check radiance, depth coverage, foreground protection and visibility;
Windows execution of these additions remains required.

## Authored Game camera composition — Phase7 implementation

`FrameRenderer` composes detached `RenderScene` values without editor UI, an editor
camera, or ownership of an ECS world. Game uses enabled authored cameras in their
validated order with entity identity as a deterministic tie-break. No valid camera
produces a black frame and an actionable diagnostic. Projection, spatial pose,
viewport, fixed-aspect fitting, layer filtering and clear flags belong to each camera.

Native Diligent whole-view clears ignore viewport/scissor rectangles. Camera-local
color/depth clearing therefore uses a rectangle-constrained draw with explicit color
write masks and depth `ALWAYS`. A whole-frame initial clear defines uncovered regions;
HDR composition and scene exposure resolve once after all cameras. Infinite-far sky
sampling uses the existing finite camera-relative ray adapter. Debug overlays project
through the actual Game camera rectangles. Windows pixel acceptance is tracked with
the corresponding build; Linux syntax checking alone is not GPU execution.

## Engine asset provider and legacy blockout compatibility

`engine_assets.hpp` allocates fixed UUIDv4 identities for the established primitive
catalog and engine materials. The immutable table is separate from project records:
`AssetCatalog::resolve` can resolve and type-check an engine asset without a source
file, but project registration cannot replace its identity. Virtual engine metadata
has an empty filesystem source and an explicit `engine` marker; it is not a locator
that an importer may open. Project serialization contains no injected engine rows.

The provider feeds the existing CPU `ResourcePool` and complete draw-candidate path,
then the existing GPU residency and mesh/material pipeline. No second cache, entity
hierarchy, resource-handle identity family or graphics pointer is persisted. Recipe
versions have separate digest identities and must change when generated content
changes. The `surface` material binding is stable across these generated revisions.

The compatibility adapter derives a built-in mesh selection and legacy material from
existing Primitive/Tint values. Missing Primitive retains the existing default cube;
None suppresses geometry. An explicit MeshRenderer takes precedence, including when
it is disabled or invalid. Tint is a transient draw input to the legacy unlit material
adapter; its established directional blockout shading remains independent of newly
authored PBR lights. This does not materialize inherited authored overrides, dirty a
scene, create project materials, or claim to migrate a saved document. PBR engine
materials and the legacy compatibility material remain distinct. The legacy host is
retained for compatibility tests until migration/visual acceptance is complete.

Phase7 provides reusable frame composition and cooked runtime asset contracts. The
full standalone exporter remains future work under the explicit phase scope.


Live environment-map and display-input SRVs use Diligent **dynamic** resource
variables. Diligent **mutable** means assign once per shader-resource binding;
it does not permit ordinary live replacement. Fixed buffer identities remain
mutable while their mapped contents change. Do not use an unsynchronized mutable
`ALLOW_OVERWRITE` to replace an in-flight environment or resized HDR target.


## Shadow integration — native validation in progress

`ShadowRenderer` is shared by Scene and authored Game camera composition. It owns
only transient shadow resources and camera-relative receiver packets. Existing mesh
bundles provide the depth pass through the same vertex fetch, affine transform and
winding selection as their color pass. Alpha-mask materials share base-alpha/cutoff;
alpha blend does not pretend to provide opaque shadow depth. Cast/receive flags and
light/mesh/camera layers remain separate. Off-camera casters whose light-space XY
bounds overlap a directional cascade extend its depth extent.

The pinned FX `ShadowMapManager` allocates array textures and fits directional
cascades. Perspective fitting uses its stabilized extents; orthographic fitting uses
native inverse-frustum bounds and uniform depth intervals. Native logarithmic
splitting always evaluates far/near, so the adapter supplies a finite positive native
range then overrides orthographic intervals. This supports authored orthographic
near=0 without changing the camera. A positive normal float bounds the native first
fit near value; receiver interval semantics still begin at zero.

Native center callbacks receive a *previously rounded* center. To avoid double
rounding in a large world, FORGE first obtains the unrounded native fit and then
snaps once using the double-precision absolute light-space origin remainder. The
native fitting coordinates stay camera-relative. Twenty-five subtexel camera moves
are included in the GPU-host math fixture; native execution is pending.

Spot maps and six point faces use existing CameraView projection conventions.
Receiver transforms are composed in double precision before float admission. The
pinned PBR convenience shadow path divides XY by W but leaves Z undivided; that is
not valid for perspective spot/point projections. `ForgeShadows.fxh` divides all XYZ
by W and calls pinned native `FilterShadowMapFixedPCF` with a 3x3 filter. Punctual
visibility multiplies incident light before the existing native BRDF, without a
vendor patch. Depth bias is in normalized shadow depth; normal bias is in metres.
Neighboring cascades fit and blend a10% depth overlap. The last cascade fades over its
final tenth. Shadow maps are derived, never serialized or inherited authored truth.

Scene settings version1 adds an optional `shadows` object (enabled, resolution,
cascades, max_lights, distance). Existing documents retain defaults. Known-field
patches preserve unknown metadata and use the existing validation/history operation.
One camera selects up to8 shadow lights and256 MiB of D32 payload; texture dimensions
must fit device limits. These are logical resource-profile budgets, not a report of
total driver VRAM or in-flight allocation overhead. Failed preparation emits an
entity-addressed diagnostic and does not select an incomplete shadow. Native
allocation is not a guarantee of recovery from driver/device loss or process OOM.

Shadow SRV arrays use dynamic bindings. Every parity/LOD binding, including invisible
meshes, is refreshed so obsolete maps cannot remain owned by unused bindings. One
comparison sampler serves the maps; material sampler semantics stay unchanged.
The additional comparison sampler requires checking the complete device binding
profile rather than assuming the prior16-sampler worst case still applies.

Normal authoring/bounds regressions pass2/2. All28 generated HLSL stages pass the
supplementary DXC check, including masked depth programs. Native D3D12 tests exercise
all three light kinds, alpha rejection, mirrored casters, large origins, oversized
allocation rejection and camera fitting. Their Windows execution remains pending.


### Transmission background and optical transport — validation in progress

The shared mesh queue draws opaque/masked surfaces and the camera background first,
then captures that camera's HDR rectangle into a distinct mipmapped texture.
Transmission and alpha-blended parts sort together back to front. Transmission is
separate from alpha coverage: it does not turn the material into alpha blending.
The transmission factor's red texture channel modulates transmission; the thickness
factor's green texture channel modulates volume thickness. Fully constant metals,
zero transmission and the IOR-zero ideal reflector do not request a capture.
A metallic texture prevents the constant-metal shortcut because it can expose
nonmetallic texels.

Diligent owns texture allocation, copy, transitions and mip generation. Capture
requires single-sample RGBA16F, checked device dimensions and the native mip-generation
format flags. The logical mip payload limit is512 MiB per visual scene's retained
snapshot; this is not a total VRAM guarantee. A failed replacement keeps the previous
allocation, reports the error and omits the unavailable transmissive draw. It never
uses an old camera image as if it were the current background. Every parity/LOD SRB,
including invisible bundles, releases the old snapshot binding before reuse.

The optical adapter uses native diffuse-transmission reduction, reflected GGX/DFG,
sheen attenuation and clearcoat Fresnel. It adds transmitted radiance as a base-layer
lighting contribution, not authored emission. Thin surfaces sample the unshifted
background; roughness selects its mip level with the IOR adjustment. Volume uses
Snell refraction and an estimated path through the baked thickness, then Beer
attenuation in world units. Missing attenuation distance means no absorption;
zero and one attenuation-color endpoints avoid logarithm singularities.
Dispersion samples RGB paths with the exact extension's approximate IOR spread.

For affine linear transform A and unit world geometric normal N, normal thickness
scales by ||A^T N||. This equals1/||A^-T n|| for a corresponding unit local normal n.
It supports shear and reflection without division by determinant. A surviving
rank-two surface has zero normal thickness. The CPU admits the derived thickness
against a Frobenius-norm float bound before a draw; this may conservatively reject
extreme optical distances, and never clamps or rewrites authored LocalScale.
Derived dispersed IOR must also fit a GPU float. These are renderer-domain limits,
not scene-format changes.

This is an opaque-background raster approximation. Off-screen refraction falls
back to the unshifted sightline; it cannot recover hidden geometry or nested
transparent media. Transmission does not cast an opaque shadow depth, and colored
transmission shadows/caustics are not supplied by this pass. Nonzero volume thickness
ignores double-sided culling, as required by the exact extension. Volume topology
admission, camera-inside/exit-interface and total-internal-reflection reflected-energy
acceptance remain open validation work; the present path must not be described as
complete general volume transport. Slab fixtures exercise the numerical adapter,
not proof of arbitrary closed-volume conformance.

Evidence: pinned FX PBR_Shading.fxh, RenderPBR.psh and OIT shader source, and glTF
c18432787e6d545a1218c1926ccdcfaffd4c116b KHR_materials_transmission/volume/dispersion.
The native FX thickness is debug-only and its transmission output uses alpha;
FORGE supplies frame composition and optical transport without patching upstream.
Local CPU material tests and generated HLSL compile. New Windows pixel tests cover
camera crop/mips, clear transmission, attenuation, roughness, refraction, dispersion,
texture channels and signed/rank-two scale; execution is pending.

### Morph draw consumer — validation in progress

The shared vertex-fetch adapter now applies mesh morph defaults to POSITION,
NORMAL, TANGENT.xyz, COLOR_0 and every UV set selected by the material. Deltas are
applied before the object affine transform; tangent handedness is retained. Color
is clamped after addition, as required by the exact glTF specification. Unused
custom channels remain in the immutable resource for their own shader consumers.
They are not reinterpreted as positions or silently dropped from the asset.

All256 admitted target slots are addressable. Packed immutable channel offsets use
four uint lanes per row; one copied float weight per target is packed into dynamic
float4 rows. A draw requires either the complete finite supplied vector or the
mesh's complete defaults. Negative weights are preserved. The native raw delta
buffer and per-channel offset/width/range are checked before shader binding.
The same vertex program is used for color and shadow depth.

Default-pose bounds add each position-delta interval with the sign of its weight.
They include an explicit float accumulation error bound and outward-rounded final
endpoints. GPU admission rejects unrepresentable morphed bounds without modifying
the asset. Whole-mesh culling and per-part queues use these derived bounds, not the
unmorphed source bounds. Per-frame animated weight extraction and its updated bounds
remain part of the model-animation bridge still in progress.

Evidence: exact glTFc18432787e6d545a1218c1926ccdcfaffd4c116b morph-target section,
including UV/color deltas; pinned FX RenderPBR.vsh's morph-before-skin order. The
FORGE raw-buffer adapter extends native's fixed position/normal/tangent slots to the
admitted UV/color contract. No vendor patch or dependency change. Local bounds tests
and supplementary HLSL compilation pass for1 and256 targets; Windows fixtures for
movement, signed weights, colors, UV19, normals and tangent handedness are pending.
This does not establish GPU skinning or complete animated model placement.

### Prepared skin draw (integration in progress)

The backend-private draw now accepts a complete copied world-space skin palette.
It applies normalized four-influence linear blend skinning after morph evaluation,
using a common positive linear normalization and camera-relative translations.
The source mesh-node transform is ignored. The existing cofactor surface-frame
adapter receives the complete blended linear matrix; no inverse of a zero-scale
joint or mesh-node matrix is evaluated. Color and shadow draws use the same
vertex deformation. GPU admission rejects invalid palette counts, nonfinite
matrices and derived arithmetic overflow before submission.

For triangular skins a geometry stage determines the orientation of the deformed
triangle map, rather than assuming that joint determinant signs determine the
blend. It uses actual deformed edges and the source-normal-direction derivative
averaged across the three vertex blends. A negative orientation reverses emitted
winding while preserving hardware back-face culling. A rank-deficient surviving
surface emits its camera-facing winding. The uniform-affine reduction is
`((A*u) × (A*v)) · (A*(u × v)) = det(A) * |u × v|²`.
This is a FORGE adapter, not a claimed native Diligent skin-parity implementation.

The shader source and CPU pose preparation have local compile/math tests. Native
Windows fixtures now cover asymmetric reflection, sequential zero crossing,
positive-joint/negative-blend orientation, ignored singular mesh nodes, large
coordinates, invalid palettes and matching shadow depth. Their GPU execution is
pending. Scene/model instance bindings and runtime-driven palettes were subsequently
connected; this section does not claim the complete animated-model workflow.


### Sampler-array admission correction

Adding shadow comparison to the 15-texture reflection fixture exceeded FXC's
scalar sampler declaration limit. A focused Windows SDK compiler probe verified
that a bounded 19-element sampler array compiles under SM5.1; 19 separate scalar
declarations failed even with separate register spaces or the unbounded-table flag.
The material adapter therefore uses one bounded array with deterministic indices.
Equal sampler states share an index; different wrap/filter/LOD settings remain
independent. Native SRB `SetArray` binds the checked reflected extent.

This does not change physical hardware limits: Tier1 still has a 16-sampler
per-stage descriptor-table limit. Broader combinations require a device supporting
the corresponding binding profile and successful native pipeline creation. The
mixed array/lighting compiler check also passes with the default FXC flags. Full
Diligent rendering regression remains pending; compiler success alone does not
establish native binding or hardware compatibility.


### Scene-to-mesh pose integration — 2026-09-21

The shared visual host now prepares each model mesh from its immutable CPU revision,
source-node binding, and detached scene snapshot. Required skin joints resolve by
`(ordinary model root EntityId, ModelNodeAssetId)`; a different instance is never a
fallback. Actual extracted joint WorldTransforms multiply immutable inverse binds.
The mesh-node world matrix applies to an unskinned node and is ignored for a skinned
node. Joint vertex attributes alone do not enable skinning. This follows the pinned
DiligentFX `GLTF_PBR_Renderer.cpp` node `SkinTransformsIndex` / `JointCount` contract.
No additional live hierarchy or authored matrix authority is introduced.

A runtime pose must match the mesh's model AssetId and immutable model revision.
Missing/duplicate bindings, unavailable animation, invalid weights or mismatched
revisions retain the previous complete draw and pose with a diagnostic. A pending
CPU candidate retries when its instance becomes valid; failed GPU realization waits
for a new asset selection/publication instead of recompiling every frame. Complete
CPU revision leases survive their GPU bundle and derived pose. Moving an object
while its pose is rejected does not partly move the retained draw.

Morph target POSITION intervals are prepared once from admitted vertex deltas;
each pose evaluates signed weights in O(targets), without rescanning vertices.
Color and shadow submission receive the same morph weights and per-part skin
palettes. Culling/LOD/sorting use deformed bounds, with skin floating-point padding
recomputed for the actual color or shadow camera origin. LODs share one admitted
pose, and material/mesh resource replacement remains a whole-candidate operation.

CPU regression coverage includes two instances of one source, a singular ignored
mesh-node transform, inverse binds, signed morphs, unskinned reuse of joint-bearing
geometry, revision mismatch, duplicate/missing joints and successful repair.
Native-header compilation succeeds; full native execution remains pending. Source
`abc5956` passed 36/37 Windows tests but failed the first zero-weight morph draw with
`GetDeviceRemovedReason=0x887a0005`. That HRESULT does not identify the cause.
Independent native morph/skin/frame/optics cases now supplement the complete viewport
suite so one device failure cannot conceal the other feature results.

### Animation debug overlay

Model-instance debug joints are prepared from the same extracted scene snapshot
used by the mesh host, resolved through its ordinary model root and durable node
mapping. This respects unanimated inherited TRS values and effective spatial bindings;
raw Ozz model matrices are not multiplied by the root again. Missing optional joints
are omitted, ambiguous/unready model poses are not drawn, and the legacy standalone
Animator still uses owner-world times Ozz-model translation. The prepared bones are
reused for all camera overlays, with double world positions projected relative to
each camera before conversion to UI pixels. No extra scene extraction occurs per
camera. Malformed optional debug data is omitted without terminating the editor.
CPU tests cover actual world positions at large coordinates, root isolation,
ambiguous/unavailable poses, legacy owner transforms and malformed parent/pose data.


### Derived scene pose payload admission

Each visual scene has a separate 512 MiB CPU derived-pose payload profile. It
accounts for retained pose vectors, per-part palette matrices and immutable morph
intervals. A replacement must fit while its previous good value remains alive;
ordered joint matrices and sorted duplicate-check IDs are included in candidate
admission. Checked arithmetic and explicit reservations precede payload allocation.
Actual retained vector capacities are counted. Bounds intervals transfer by shared
immutable ownership into the physical draw bundle, avoiding a second vertex scan
and duplicate interval allocation during adoption.

Deleting instances releases their payload before retries in the same update.
Admission failure preserves the previous complete draw/pose and emits a structured
entity diagnostic; a refused candidate can succeed after memory is released. This
is an engine capacity profile, not a geometric validity test or a promise that
process RSS stays below 512 MiB. It excludes allocator bookkeeping, stack frames,
snapshot/index metadata, other CPU resource pools and GPU resources, which have
separate ownership and limits. It never narrows the accepted LocalScale range.

On the current x64 build an AffineTransform occupies 96 bytes. Just 65,536 parts
with 256 palette matrices each would require 1,610,612,736 bytes, before container
costs; finite part/joint counts alone were therefore insufficient admission.
CPU tests exercise exact payload limits, rejection before malformed geometry is
read, temporary scratch accounting and last-good retention. A native scene fixture
also checks aggregate rejection, deletion and retry; Windows execution is pending.

### Isolated Windows audit follow-up

Source `1c0ae36` built successfully and passed 37 of 41 selected Windows tests.
Its independent frame test passed. The full viewport and isolated morph tests
failed at the first zero-weight morph draw with device removal; the isolated skin
shader failed FXC X4580 at indirect input-vertex emission. Geometry output now
uses explicit constant-index Append branches with identical winding semantics.
The morph loop now predicates its body instead of using continue; this is a
controlled equivalent-source experiment, not an established driver/compiler fix.
Neither change is accepted as native-correct until the rerun succeeds.

The transmission crop fixture incorrectly expected saturated 1.0 primaries to
remain saturated after the pinned PBR Neutral highlight desaturation. Its camera
isolation checks now use .5 primaries below compression and expect the one-transfer
sRGB value of 188 (one UNORM step allowed), with zero cross-channel leakage.
Display resolve also now sizes output from the source view's selected mip,
rather than always using the underlying texture's base dimensions. The last-mip
fixture requires exactly one pixel as well as its expected color.


### First model-pose publication

Loading an animation resource while paused prepares its sampler without writing
ECS transforms. The runtime distinguishes that state from a model pose which has
actually passed channel/physics validation and been applied at a fixed tick.
Until then, the model readiness marker is false and the presentation host retains
any previous complete draw. Legacy standalone Animator pose inspection continues
to work after resource loading, without advancing time.

The first successful application and recovery from a failed binding are
presentation discontinuities. Affected nodes snap through the existing
presentation-pose cache; morph sampling snaps to the same successful fixed time.
This changes transient interpolation history only; it creates no extra component
overrides and preserves channel-granular ECS writes.
Later compatible ticks interpolate normally. Recovery does not serialize this
renderer-local history; a reconstructed model waits for its next successful fixed
application. No presentation extraction writes authored/runtime TRS, and no second
world-transform authority is introduced. Normal and strict sanitizer model,
animation and physics regressions pass; Windows execution remains pending.

Disabling Animator removes its live drawing contribution: the model remains
visible with its current node transforms and the mesh's source-node morph defaults.
It does not rewrite transforms. Use **Pause** to hold the complete animated pose.
Re-enabling Animator waits for the next successful fixed tick and then snaps to
that tick's pose before normal interpolation resumes.


## Structural visibility and selectability

`NodeVisibility` (`forge.node_visibility.visible`) and `NodeSelectability`
(`forge.node_selectability.selectable`) are independent optional native Flecs
components. Both default to true. Their Meta bool members own validation, defaults,
Inspector discovery and persistence; IsA retains normal component/property override
intent. They own no resources or derived transforms and add no persistent identity.

An effective policy is the conjunction of the entity's value and all structural
ancestors. Spatial FollowStructure/Explicit/World does not change this chain. A
missing component contributes true. Prefab declarations are not rendered but may
still participate in an explicitly authored structural chain; native prefab
inheritance has already been resolved in the effective snapshot. The bounded
iterative resolver diagnoses malformed/cyclic/missing producer ancestry without
recursion or scene mutation. Invalid policy suppresses its affected drawing/picking.

Visibility suppresses meshes and lights, including their shadow contribution. It
does not disable cameras, animation, physics, audio or selectability. The detached
mesh record retains hidden geometry for selection; the shared renderer skips it in
color and shadow queues. Selectability independently skips viewport ray selection,
regardless of transparency/visibility; Hierarchy/API selection remains available.
This is not a transform lock or implementation of KHR_interactivity event graphs.
Imported false node flags become these authored components; source reimport does
not rewrite an already placed scene hierarchy or its local policy.

Rendering components Camera/Light/MeshRenderer and both node policies are marked
optional in the existing shared component catalog. Core registration family does
not imply that an authoring component is mandatory. ModelSource provenance stays
internal; all edits use the existing scene command/revision/history boundary.

### Geometric viewport selection

The Scene viewport selects from the same retained CPU mesh revision and complete
instance pose as the draw bundle. It chooses the rendered LOD, evaluates POSITION
morph deltas before joint-world × inverse-bind skinning, and clips actual
triangles/lines/points in homogeneous D3D clip space before perspective division.
No world inverse is required. Reflection and rank-two surfaces remain selectable;
collapsed triangle surfaces do not create a false solid hit. Selection is
geometric and two-sided: material alpha, visibility and raster culling do not
replace the independent structural selectability policy.

Camera-relative double arithmetic subtracts the camera origin from matrix
translations before applying local vertices. Triangle depth comes from screen
barycentrics; points and lines have a five-pixel pick radius scaled with the editor
interface. Nearest projected depth wins; EntityId breaks exact ties. Conservative
screen bounds reject unrelated objects before precise tests. Selection never
substitutes a cube for missing or still-loading imported geometry.

One click shares a 1,048,576-unit synchronous work budget across candidates.
A vertex costs one unit, each active POSITION morph another, and each skin
influence another. Fixed-size clipping storage adds no per-triangle heap payload.
Exhaustion rejects the entire query and preserves selection with an actionable
status; Hierarchy remains available. This is a current interaction bound, not an
asset rejection or a claim of bounded milliseconds. A 100,000-triangle fixture
(300,000 units) measured 35–40 ms in the local normal build and 162 ms under strict
sanitizers. Dense asynchronous/BVH selection remains future optimization.
