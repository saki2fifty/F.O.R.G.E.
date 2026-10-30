# Screen-space reflections

## Authored values and shared access

`SceneScreenSpaceReflections` belongs to Scene rendering settings. Defaults are
`enabled=false`, `max_distance=25` metres, `thickness=0.1` metres, `strength=1`
and `max_roughness=0.5`, with `roughness_filtering=false`. The optional filtering
value must be a JSON boolean, including when disabled. Finite bounds are respectively 0–100, 0.0001–1, 0–1
and 0–1. JSON checks double bounds before narrowing to float; disabled values
still validate. Disabled, zero distance, strength or roughness cutoff bypasses
reflection targets and processing.

`scene.rendering.set`, its authoring schema/discovery/reference inspection and
`forge_scene_rendering_set` use the same partial nested object. Existing Scene
permissions, expected revisions, replay, Undo/Redo and Save remain authoritative.
Scene lighting controls use that operation and the existing timestamped Problems
owner. Controls are locked during Play. Thickness editing retains decimal input
precision at its minimum bound before authoring validation.

## Material and lighting contract

The existing Mesh/PBR evaluator supplies the current mapped base normal in view
space, perceptual roughness, unit-radiance base specular response and replaceable
base environment specular. FORGE calls the pinned public PBR lighting API; it does
not implement another BRDF. Resolving a zeroed lighting structure with only base
specular isolates that contribution while retaining upper-layer attenuation.
Resolving a unit specular response with IBL scale one avoids multiplying local
reflected light by environment intensity. Both values include receiver fog
transmittance. The broad base indirect term, separate coat/sheen lobes, punctual
lighting, emission, additive fog color and current alpha remain intact.

Nonzero anisotropy retains environment reflection. Unlit and final-color custom
functions write zero receiver data; valid current opaque source color can still
be reflected. Transmission and current-frame layers keep their existing path.
This profile traces one base-normal ray. Optional roughness filtering averages
current visible source radiance; it is not a full physical BRDF convolution. It
has no independent coat/sheen rays, hidden/off-screen geometry or multi-bounce GI.
[Local imported probes](local-reflection-probes.md) are a separate pass before
this trace; misses preserve their base reflection and combined AO sees final indirect.

## Current-depth trace

The original spatial operator reconstructs current positive view positions from
D32 depth and the complete admitted perspective, infinite-far or orthographic
projection, including jitter. It traces 64 bounded quadratically spaced distances
along the reflected direction and refines an admitted crossing eight times.
Finite depth thickness, receiver normal, range, screen edges and roughness limit
acceptance and confidence. Misses retain environment specular.

The replacement is `(hitColor * unitResponse - oldBaseSpecular) * confidence`.
The same delta updates full HDR and total indirect radiance. Ambient occlusion
therefore consumes the updated indirect image instead of subtracting stale
pre-reflection environment radiance. Color and indirect alpha are preserved.
Typed view, extent, alias, thread and projection validation precedes processing.

## Optional roughness filtering

`roughness_filtering` defaults off, preserving the original point-source trace for
older scenes. It changes source lookup, not the traced direction, material BRDF,
ray count or roughness fade. Zero receiver roughness retains a point lookup.

After local probe composition, the view builds up to six ceil-sized 2× reductions.
Each source level contains RGBA16F premultiplied HDR radiance/geometry coverage and
RG32F valid positive view-depth minimum/maximum. Depth and finite nonnegative RGB
admit source pixels; authored color alpha does not determine geometry coverage.
Outside cells have zero nominal coverage. Invalid geometry does not enter depth
bounds. Explicit reduction preserves area/coverage at odd and thin extents.

A projected footprint grows with squared perceptual roughness and traced distance,
using the complete jittered projection, with radius capped at 24 pixels. Bilinear
source cells and adjacent levels interpolate premultiplied color/coverage before
normalizing. Coverage attenuates confidence. If valid source depth bounds exceed
the hit's thickness interval, the lookup retries a finer level and finally the
point source. Small coverage also falls back to the point. Depth fallback and
level selection can leave visible bands. This bounds work and
limits unrelated depth mixing; it does not solve arbitrary silhouettes or hidden
surfaces. The six 2–64 pixel scales cover the selected radius bound.

Filtered normalization is capped at the largest finite RGBA16F value 65504. The
color/indirect replacement delta shares their nonnegative and finite half-float
headroom, preserving the same feasible delta in both outputs rather than clipping
them independently. Storage rounding remains. This finite-output guard belongs to
the opted-in filtered path; the original point path keeps its prior behavior.

## View, queue and resource ownership

TemporalView owns the optional three material attachments, two result images and
source-level image pairs;
the reflection operator owns only pipeline, bindings and constants. The existing
mesh queue prepares ordinary or temporal reflection variants before changing
targets, preserving part budgets, culling, depth/mask behavior, poses, instancing,
material parameters, camera rectangles and current-layer ordering. Unsupported
shader/depth/order/capacity retains ordinary rendering with the established
`render.temporal.unavailable` warning. Game cameras must clear color and depth.

Order: opaque/masked Mesh → optional local probes → optional reflection source reduction → screen reflections → ambient occlusion → optional temporal
anti-aliasing → motion blur → depth of field → current layers → bloom/display.
Spatial-only reflection has no camera jitter, motion field, pose history, color
history, extra clock or warmup requirement. Scene can retain a completed image.

At most eight Game views share the existing 512 MiB texture payload budget;
Scene has its own budget. Bytes/pixel: reflection only 60, with occlusion 68;
Adding local probes and screen reflections together requires another8B/pixel.
AA plus screen reflection alone 96, with occlusion 104; AA/motion/reflection 104, with occlusion
112; frozen motion capture adds 20. Depth-of-field payload, native allocation,
retirement and transient staging overhead are additional. Required targets stage
before adopting an extent/mode; rejected growth preserves the prior mode.

Filtering adds the exact sum of 16 bytes times each ceil-sized source-level area,
up to six levels; it is not a constant bytes/pixel multiplier. Both Game's shared
budget and Scene's private view admission use that same checked planner. Disabled
or zero reflection paths and probe-only views allocate no source images. A 1×1 view
uses point lookup with no levels. Resize stages a complete replacement; stable
frames reuse images and disabling filtering releases them. The operator borrows
textures and owns no image allocation. No new history, clock or SDK boundary is
introduced.

## Surface artifact compatibility and evidence

Capability 5 appends `surface_reflection_color` and
`surface_temporal_reflection_color` to the five capability-4 roles. Their MRT order
is color, optional RGBA32F motion, indirect, normal/roughness, response and old
base specular. Other attachments use RGBA16F. Capability6 emits nine roles, including the two minimal normal-only AO roles.
Current capability7 appends eight area-light color counterparts and shares depth.
Readers retain capabilities1–6 and their original bytes;
logical Shader/Material IDs and existing immutable publication/DDC/last-good
ownership remain unchanged. Reimport Shader and rebuild dependent Materials to
participate with older cooks. Ordinary source-program stage admission remains six;
surface artifacts admit up to17 entries.

Local software Vulkan checks cover analytic geometry, mapped/rotated normals,
material/layer/fog response, actual posed and instanced MRT producers, Game/Scene,
camera isolation, zero paths and combined capture. Actual UI/MCP and source-free
Gallery106 checks exercise existing authoring and package owners. These do not
establish Windows/MSVC/FXC, cooked D3D12, physical GPU performance or graphical
Windows acceptance; those reviews remain separate.
