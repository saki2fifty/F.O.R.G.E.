# Scene distance fog

The optional `rendering.distance_fog` section in Scene rendering version 1 owns
`enabled` (false), linear HDR `color` ([0.5,0.5,0.5]), `start` (0) and `end` (100).
Distances are radial world metres from each camera position, including orthographic
views. Start is nonnegative; end is 0.001–1000000 and strictly greater than start.
Color has exactly three finite channels in 0–65504. Full double-range checks precede
float conversion; collapsed GPU start/end values reject even while disabled.

Existing `scene.rendering.set` patches nested fields, preserving omitted fields
and opaque extensions. Scene validation rejects the complete candidate atomically;
Undo/Redo, document saving, frame extraction, MCP and standalone content retain
that same owner. Asset-reference traversal recognizes these scalar/color fields
and reports unknown extensions. No new asset type, entity component or gameplay ABI
is added.

## Geometry and composition

For camera-relative fragment position P, the amount is
`clamp((length(P)-start)/(end-start),0,1)`. Successful linear HDR surface RGB is
interpolated toward the fog color. Alpha, mask discard and nonfinite diagnostic
returns retain their existing semantics. Norm evaluation scales by the largest
absolute component and returns full fog early beyond end, avoiding squared-distance
overflow for distant geometry. Zero amount and disabled fog return the original
color without interpolation.

The same helper serves built-in unlit/PBR, Sprite and engine-generated custom/graph
surface color wrappers. Existing Mesh geometry also supplies Tile Maps, particles,
terrain, skins, morphs and instanced draws; their geometry ownership does not change.
Fog precedes alpha blending, HDR bloom, exposure, grading, tone mapping and sRGB.
Each camera uses its own camera-relative geometry. Sky/background, shadow/depth
passes, editor overlays and game HUD remain outside geometry fog. Material previews
use default disabled settings. This is linear geometry distance fog, without height
layers, light scattering, volumetric shadows, weather or temporal history.

## Resources and cooked wrappers

A private two-float4 `ForgeDistanceFog` buffer consumes 32 payload bytes per prepared
color draw that reflects the binding. Three parity bindings share that buffer.
The Object 15-float4 uniform buffer and Mesh 7-row / Sprite 10-row instance layouts
are unchanged. No new
render target, depth sampling, texture allocation, CPU readback or submission wait
is introduced. These bytes are buffer payload, not a physical VRAM bound.

Shader recipe provenance includes the shared helper. Current cooked surfaces carry
`surface_wrapper_version:2` in the existing Shader envelope and layout identity.
Version 1 is implicit for older artifacts; its layout digest and serialized bytes
remain unchanged. The engine admits only versions 1 and 2. This is engine-owned cook
metadata, not a project HLSL declaration or a second capability database. It is
necessary because optimization can legitimately remove a current wrapper's fog
binding, especially for a constant diagnostic return. Optimized-out resources do
not require fabricated bindings and do not turn a current cook into a legacy cook.

Existing version 1 surface cooks remain usable when fog is disabled. Enabling fog
with a legacy surface reports an actionable draw diagnostic: reimport its Shader
and rebuild its Material. That incompatible draw is omitted until rebuilt or fog
is disabled. Embedded last-good Material snapshots remain authoritative;
a Shader publication alone does not replace a retained Material's bytes. Do this
before exporting scenes with fog. General stage programs retain version 1 semantics.
Use a matching engine to read new surface cooks; no older-reader support is claimed.
The installed C++ value headers/fingerprint and C ABI are unchanged.

## Examples and evidence

Gallery 98 is an original avenue with twelve colored depth markers, a floor,
directional light and fixed-aspect camera. It reuses one existing Material and
registers one new Scene without changing existing catalog records or asset IDs.
Native fixtures cover numeric surface distance, both projections, large origins,
Mesh/Sprite/direct/instances, alpha/mask/disabled behavior, custom-wrapper diagnostic
pixels and graph source compilation. Scene/MCP/UI/history and source-free export
use the existing normal regression consumers. Windows/D3D12 full-editor and physical
GPU acceptance remain separate from software Vulkan evidence. Generated custom
wrapper pixels do not imply a Vulkan adapter for D3D12-cooked Shader assets.
