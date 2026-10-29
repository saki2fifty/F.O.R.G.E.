# Screen-space ambient occlusion

## Authoring and ownership

`SceneAmbientOcclusion` is a Scene rendering value: default disabled, radius0.5m,
strength1, bias0.01m and normal influence0. Radius admits0–10m, strength0–1, bias0–1m
and normal influence0–1, all finite.
JSON checks double-precision bounds before narrowing. Disabled fields still
validate. Disabled, zero radius or zero strength bypasses the feature.

`scene.rendering.set` accepts a partial `ambient_occlusion` object. The established
Scene operation retains extension fields, enforces expected revision, creates one
Undo record and persists through Scene Save. Discovery, the authoring API, MCP and
Scene lighting controls use this operation; no separate agent implementation exists.
Controls are locked during Play. Failed inputs keep the document and recover through
the existing timestamped Problems owner when corrected.

## Lighting contract

This original spatial pass reduces the current indirect contribution on
depth-writing opaque/masked Mesh geometry. It preserves punctual lighting, emission,
additive fog color and alpha. It does not multiply the final HDR image by a darkening
factor. The existing native PBR API resolves a copied lighting structure with punctual
terms removed and copied shading with emission removed. The result includes the
native base, sheen and clearcoat layer factors; FORGE applies its existing distance
fog transmittance before writing the separate indirect target. Unlit and custom
final-color functions write zero indirect radiance.

Enabled [screen-space reflections](screen-space-reflections.md) replace the base
specular term and update the same indirect radiance before occlusion.

The operator reconstructs positions from sampled D32 depth and the actual full
projection, including jitter offsets. Closest-side depth differences supply geometric
normals. Optional `normal_influence` blends these directions toward the current
view-space base material normal, including its normal map. Zero keeps the existing
geometric result. The normal must be finite, nonzero and face the same geometric
hemisphere; invalid, missing or backward per-pixel data falls back to geometry.
The geometric hemisphere rejection remains active after blending, preventing a
tilted shading normal from treating its own flat plane as an occluder. This changes
sampling directions and cosine weights, not depth or actual geometry. Separate
clearcoat/sheen normals are not separate AO inputs.

Sixteen deterministic hemisphere positions gather nearby visible geometry
within the world-space radius. Cosine and distance weights estimate occlusion with
a metre depth bias. Only that fraction of the separate indirect RGB is subtracted
from current HDR, clamped at zero; current alpha is copied unchanged.

The bounded spatial sample pattern can be visible near contacts.
This profile has no hidden/off-screen geometry, multi-bounce
lighting, temporal occlusion history or ray tracing. It is a bounded screen-space
approximation. It cannot infer surfaces absent from the current depth image.

## Existing view and queue integration

TemporalView owns optional indirect/current-result targets and the spatial operator;
the operator owns only pipeline, constants and bindings. The same view can run with
AA and motion blur enabled or disabled. Spatial-only operation has no jitter,
motion attachment, color accumulation or separate clock. Host camera/resource/reset
and successful-frame ownership remain unchanged.

MeshDrawBundle caches admitted ordinary/motion/indirect/combined specializations of
the same immutable part. MeshSceneRenderer retains culling, sorting, posed geometry,
instancing, material values, resources and the existing draw-part budget. All eligible
parts are prepared before changing targets. Unsupported depth, shader capability,
queue order or budget leaves the complete view in ordinary rendering and emits an
existing `render.temporal.unavailable` warning. It never reorders current layers.

The order is opaque/masked Mesh → optional reflections → ambient occlusion → optional AA → optional motion
blur → optional depth of field → current-frame layers → bloom/display. Transparent,
transmissive, Sprite, Tile Map, particle, sky and editor overlay paths retain their
established current-frame rendering. Each Game camera must clear color and depth;
its local rectangle is composed with depth before later layers/cameras. Scene uses
its own navigation camera and can retain a completed static image.

At most eight Game views share the existing512MiB surface texture payload limit;
Scene has its own512MiB limit. Geometric spatial-only mode uses28bytes/pixel: HDR8, D32depth4,
indirect8 and occluded HDR8. With reflections disabled, occlusion adds16bytes/pixel to active AA/motion modes:
AA64, motion52, AA+motion72 and frozen AA+motion capture92. When reflections
already own indirect radiance, occlusion adds only its8byte/pixel result image;
the [reflection contract](screen-space-reflections.md) lists combined estimates.
With reflections disabled, active normal influence adds one8byte/pixel RGBA16F
normal/roughness image: spatial-only36, AA72, motion60, AA+motion80 and frozen
AA+motion capture100. Reflections already own this exact image, so their combined
estimates do not increase. The existing view stages, shares and releases this
attachment; no independent material-buffer owner is introduced. Native resources,
retirement, staging overhead and separate depth-of-field payload are additional.
Required extent/mode targets are staged before adopting a new mode. Bypassed
occlusion allocates no occlusion targets or operator pipeline.

## Cooked Shader compatibility

Wrapper capability4 appends `surface_indirect_color` and
`surface_temporal_indirect_color` to the existing color/depth/temporal roles.
Their targets are respectively color+indirect and color+motion+indirect. Motion
remains RGBA32F; indirect and HDR color are RGBA16F. Generated helpers use explicit
out parameters. Old1/2/3 archives remain readable with stable logical Shader and
Material IDs. Reimport/rebuild older surface Shader assets and dependent Materials
to opt into occlusion. Ordinary rendering remains available before recooking.

Capability4 provides these five roles; capability5 adds the two
[reflection roles](screen-space-reflections.md). Capability6 appends
`surface_normal_color` and `surface_temporal_normal_color`, producing respectively
color+indirect+normal and color+motion+indirect+normal: three or four MRTs. Current
cooks contain nine Pixel entries. All capabilities1–5 retain their readable roles
and bytes. Active normal refinement without reflections requires these new roles;
when reflections are active their existing shaded normal image is reused. Built-in
materials use the same generated functions. PBR graphs produce the actual base
normal; final-color custom functions output zero normals and indirect radiance,
with geometric fallback. The existing derived per-part cache retains these variants.
New cooks require the matching current engine. Existing recipe fingerprints include the
shared indirect and fog emitters. Material/Shader publication, last-good retention,
DDC and runtime loading remain with their established owners. Cooked Windows shader
acceptance and physical rendering remain separate from local software Vulkan proof.
