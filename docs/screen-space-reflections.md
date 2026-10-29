# Screen-space reflections

## Authored values and shared access

`SceneScreenSpaceReflections` belongs to Scene rendering settings. Defaults are
`enabled=false`, `max_distance=25` metres, `thickness=0.1` metres, `strength=1`
and `max_roughness=0.5`. Finite bounds are respectively 0–100, 0.0001–1, 0–1
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
This first profile traces one base-normal ray; it has no roughness convolution,
independent coat/sheen rays, hidden/off-screen geometry or multi-bounce GI.
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

## View, queue and resource ownership

TemporalView owns the optional three material attachments and two result images;
the reflection operator owns only pipeline, bindings and constants. The existing
mesh queue prepares ordinary or temporal reflection variants before changing
targets, preserving part budgets, culling, depth/mask behavior, poses, instancing,
material parameters, camera rectangles and current-layer ordering. Unsupported
shader/depth/order/capacity retains ordinary rendering with the established
`render.temporal.unavailable` warning. Game cameras must clear color and depth.

Order: opaque/masked Mesh → optional local probes → screen reflections → ambient occlusion → optional temporal
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

## Surface artifact compatibility and evidence

Capability 5 appends `surface_reflection_color` and
`surface_temporal_reflection_color` to the five capability-4 roles. Their MRT order
is color, optional RGBA32F motion, indirect, normal/roughness, response and old
base specular. Other attachments use RGBA16F. Current surface compiler source
emits nine roles at capability6, including the two minimal normal-only AO roles.
Readers retain capabilities1–5 and their original bytes;
logical Shader/Material IDs and existing immutable publication/DDC/last-good
ownership remain unchanged. Reimport Shader and rebuild dependent Materials to
participate with older cooks. Ordinary source-program stage admission remains six;
surface artifacts admit up to eight entries.

Local software Vulkan checks cover analytic geometry, mapped/rotated normals,
material/layer/fog response, actual posed and instanced MRT producers, Game/Scene,
camera isolation, zero paths and combined capture. Actual UI/MCP and source-free
Gallery106 checks exercise existing authoring and package owners. These do not
establish Windows/MSVC/FXC, cooked D3D12, physical GPU performance or graphical
Windows acceptance; those reviews remain separate.
