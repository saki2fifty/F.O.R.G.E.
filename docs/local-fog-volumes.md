# Local fog volumes

`forge.fog_volume` is an optional native Rendering component, available through
the `render.fog_volume` entity recipe and existing component/property commands.
Scene, Prefab, variants, history and runtime snapshots retain their existing
authority. It has no asset, hidden helper entity or separate volume database.

## Authored values

The installed `forge/fog_volume.hpp` owns profile 1, the shape values and the
16-volume draw limit. Defaults are enabled, Box (0), linear RGB (0.5,0.5,0.5),
density 0.15 per world metre, local half extents (2,2,2), all layers and priority 0.
Sphere (1) uses those half extents as ellipsoid radii. Ordinary transforms,
including inherited rotation, nonuniform scale and reflection, place the volume.
Colors are finite 0–65504; density is finite 0–1000; half extents are finite
0.001–1000000 metres. Layers use uint32 and priority int32. Source admission checks
numeric bounds before float conversion. Invalid edits leave source and history
unchanged. Native producers receive scoped presentation diagnostics for invalid
copied values instead of invalidating the whole frame.

Native Meta supplies shape choices, defaults, labels and ranges to the Inspector,
automation and MCP. All eleven fields can be tuned on an existing runtime
component through the shared live-tuning owner; adding/removing the component
remains authored work. Live values are transient. Keep values reviews selected
fields and applies them after Stop through ordinary authored history. Component
keyframe tracks are not admitted for these fields; ordinary transform animation
can move or reshape a volume.

## Selection and geometry

Inactive, hidden, disabled, zero-density and zero-layer sources are omitted.
Frame extraction uses resolved world transforms. Per-view preparation subtracts
camera origin in double precision and uses the existing affine inverse and
reciprocal-condition policy. Admitted world half axes are 0.000001–1000000 metres;
relative center components are within ±1000000 metres, inverse axis norms at most
1000000, and the float-quantized basis must remain invertible under that policy.
An invalid source emits `render.fog.invalid` or `render.fog.view` and does not
prevent other sources from rendering.

Sources sort by descending priority, then durable EntityId. Camera, volume and
receiver must share at least one layer bit. Filtering precedes the 16-source cap
for each receiver layer mask. Extra compatible sources produce `render.fog.limit`.
Priority controls selection under overflow; it is not a paint order.

For each fragment, analytic box slabs or ellipsoid chords give the world-metre
length of the camera-to-surface segment inside each selected volume. Perspective
rays start at the camera. Orthographic rays are parallel along the view forward
direction, starting on the camera plane at that pixel's lateral position. Ray
length is bounded at 10000000 metres, beyond all admitted volume geometry.

For source i, optical depth is `tau_i = density_i * inside_length_i`.
Transmittance is `T = exp(-sum(tau_i))`. The medium color is the
optical-depth-weighted mean of contributing colors; output RGB is
`input*T + medium*(1-T)`. This is a homogeneous artistic mixture, without ordered
transport between differently colored media. Global radial distance fog runs
first, local fog second. Alpha is unchanged; indirect targets use the combined
transmittance. Sky, background, UI, overlays, shadow and depth passes are outside
this geometry effect. Light scattering, volumetric shadows, weather and general
volumetric rendering are not implemented by this component.

## Rendering and packaging

The shared surface helper serves built-in Mesh/PBR/unlit, Sprite, graph and
custom color wrappers. Existing Mesh geometry ownership also covers terrain,
particles, Tile Maps, posed and instanced draws. A private `ForgeFogVolumes`
buffer has 81 float4 rows (1296 payload bytes): one count/ray-mode row and five
rows for each of sixteen sources. Object and instance layouts remain unchanged.
An immutable neutral buffer serves empty draws; an active buffer is allocated
lazily and uploaded for active samples. Switching back to empty updates all parity
bindings. There is no additional render target, texture, readback or submission wait.
These payload sizes are not physical memory or hardware performance measurements.

Surface wrapper 16 introduces the reflected binding and exact cook recipe inputs.
Older wrappers remain usable without local fog. An affected legacy custom draw
reports a recook diagnostic and is omitted until its Shader and retained Material
are rebuilt or local fog is removed from its receiver layers. Optimized-out
resources do not require fabricated bindings. Source-only shader probes do not
claim a Vulkan adapter for Windows-cooked Shader archives.

Saved Scene/Prefab/variant component or property-override intent, including disabled
Fog components, declares `runtime_features.fog_volumes:1`. The existing runtime-kit
manifest advertises `engine.runtime_fog_volumes:1`; export admission rejects a
missing, malformed or mismatched profile. Legacy documents without Fog acquire no
new requirement. Surface 16 cooks separately require a supporting surface runtime
profile. The C ABI stays version 1; the installed header closure and SDK fingerprint
change, so project modules must use the matching SDK.

Gallery 160 supplies both shapes, depth markers, a floor, camera and light using
built-in geometry. No project C++ or custom Shader cook is required. Software GPU
checks and offline authoring checks do not establish Windows/MSVC, full-editor,
physical GPU, external-client or graphical exported-game acceptance.
