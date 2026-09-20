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
