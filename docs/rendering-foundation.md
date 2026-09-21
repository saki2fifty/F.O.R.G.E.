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
Native shading and complete refraction/dispersion consumers remain in progress;
admitting a material parameter does not prove that it is rendered.

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

The initial adapter deliberately rejects skin/morph bindings and extended material
effects whose consumers are not yet wired. Environment/shadows, complete extended
PBR, deformation, GPU resource adoption and Scene/Game/standalone integration remain
Phase7 work. This is an internal implementation checkpoint, not a shipped renderer
completion claim. Native fixtures cover reflection, singular planes, projection
flips, large origins, explicit light/no-light behavior; execution is pending.

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
Transmission, volume/dispersion and the HDR/IBL/shadow pass connections remain
ongoing work in the authorized batch.

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
