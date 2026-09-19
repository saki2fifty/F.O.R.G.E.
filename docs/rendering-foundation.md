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
fixture must verify Diligent, Jolt, Ozz and Recast adapter consistency. Existing
blockout uses culling disabled and therefore cannot prove imported face winding.
Do not label an untested conversion as supported.

## Cameras, lighting and effects

Camera is future authored data: perspective/orthographic projection, vertical FOV or
orthographic size, finite positive near/far range, aspect policy, viewport/target,
clear policy and deterministic priority. A runtime camera-selection system chooses
active cameras; multiple targets/cameras use explicit ordered composition. No active
camera produces a diagnostic/fallback view, not a silent substitution of Scene camera.
Editor navigation cameras remain personal transient state. Current Game camera is a
preview convenience copied at Play start, not this production camera system.

Directional, point and spot lights become ECS data with units, color/intensity,
range/cone/shadow intent. Area lights extend the same typed lighting interface after
renderer support exists. Environment/sky is an asset plus scene-level ambient/exposure
settings; skybox and image-based lighting are renderer resources. Shadow formats,
passes and budgets belong to renderer profiles. No fake lights are exposed now.

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
