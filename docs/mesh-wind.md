# Static Mesh wind geometry

`MeshRenderer.wind` is an optional, disabled-default `MeshWind` value. It is owned
by the existing renderer component and copied by ordinary Scene, Prefab and
`mesh.scatter` operations. No population manager, wind asset, authoritative
transform stream or simulation accumulator is introduced. See the
[user workflow](../manual/editor/foliage.md#add-wind).

## Profile and admission

| Member | Default | Admitted value |
| --- | --- | --- |
| enabled | false | Boolean |
| amplitude | 0.2 | Finite 0–100 world meters |
| height | 1.6 | Finite 0.01–10000 world meters |
| direction_x, direction_z | 1, 0 | Each finite within ±1000000 |
| frequency | 0.5 | Finite 0–60 cycles per simulation second |
| wavelength | 8 | Finite 0.1–10000 world meters |
| phase | 0 | Finite -360–360 degrees |

Enabled nonzero amplitude requires a nonzero horizontal direction. Direction
normalization scales by the largest component first, retaining subnormal angles.
An active sample requires finite affine entries, object-origin coordinates within
±1e9 meters and time within 0–1e9 seconds. Disabled/zero-amplitude samples use
ordinary geometry. Active skin binding and morph targets reject nonzero wind
before replacing a retained pose. An imported Mesh used without active skin
binding can remain a static draw even if its source contains joint attributes.

Validation lives with the public value in `include/forge/mesh_wind.hpp`. Existing
render codecs and native Meta provide full values, units, ranges and descriptions.
A missing legacy wind property receives the declared disabled default; explicit
disabled Prefab overrides remain authored intent. Whole-property validation,
extension retention and Scene history remain with the existing owners.

## Geometry and surface frame

Let `d` be the normalized world X/Z direction, `r` the world object origin and
`t` the borrowed time. Sampling computes:

```text
cycles = dot(r.xz, d) / wavelength - t * frequency + phase / 360
D = amplitude * sin(2*pi*cycles) * d
h = clamp((world_vertex.y - r.y) / height, 0, 1)
deformed_vertex.xz = world_vertex.xz + D * h*h
deformed_vertex.y = world_vertex.y
```

Cycle reduction bounds the trigonometric input. The field is sampled at each
object origin; it does not create a persistent field grid. Vertices at/below the
root plane are pinned. Height and direction follow world axes, including on
slope-aligned, mirrored, nonuniform or singular authored transforms. Mesh edges
and triangles interpolate displaced vertices, so coarse geometry approximates
the bend with straight segments.

Inside the bend interval the displacement derivative is `D * 2*h/height` along
world Y. This is a horizontal shear with determinant one. Directions use its
forward Jacobian; normal covectors use its inverse transpose. The shared shader
normalizes/orthogonalizes the frame and retains tangent handedness. World/source
transforms and physics shapes remain unchanged.

`MeshInstancePose` retains the detached sample. Current bounds conservatively
include the complete signed displacement and camera-relative float padding.
Picking deforms admitted triangle/line/point vertices through the same numeric
owner. Color, depth/shadow, surface outputs and previous-position shaders use the
same generated geometry. There is no color-only deformation path.

## Time and retained ownership

`RuntimeClock::presentation_seconds()` derives the presentation interval from its
fixed tick, step size and alpha. A transient floor tracks collapsed pause/resume
or restored-pose intervals so resuming cannot rewind the sample. It adds no clock
or accumulated time. GameSession, SDK candidate/live snapshots and the headless
runtime project that value in `effective_scene.simulation_seconds` when enabled
wind exists. Legacy one-argument simulation presentation remains available to
callers that intentionally provide no procedural sample.

FrameRenderer uses an explicit `TemporalFrameSample.seconds` when supplied,
otherwise copied Scene simulation time, otherwise a frozen Edit sample of 0.5s.
Its existing wall-time parameter continues to serve presentation effects; it
does not drive wind. Render-texture views receive the same Scene/sample routing.
Pause and zero simulation rate hold time progression; Step advances a fixed tick.
Frequency zero freezes the time phase, while moving an origin can still change
its spatial phase.

Wind selects the existing static-Mesh draw variant; parameters and sampled
displacements do not enter immutable asset/resource keys. Compatible objects use
the established instance buffer: 8 rows for wind, 13 with previous motion.
Default-off layouts retain 7/11 rows and their generated geometry. Existing pose
history retains previous wind values. Mode changes invalidate per-object previous
correspondence; active wind otherwise supplies its actual previous displacement.
Failed candidates keep complete prior geometry and diagnostics through the
ordinary draw owner. Resource and frame retirement retain their existing owners.

## Authoring, agents and export

Inspector uses the reflected Wind struct. Live gameplay, scoped runtime tuning
and Keep Values use the existing complete-property APIs; Stop discards transient
values unless a reviewed field is applied to Scene history. MCP authors through
`forge_property_set` with component `forge.mesh_renderer`, field `wind`, and a
complete value from discovery defaults. Runtime `forge_play_tune` uses property
`wind`, current launch/session/generation and the `play.execute` grant. Inspect
the tune acknowledgement and actual snapshot; submission is asynchronous.
Paged `forge_play_snapshot` includes the copied procedural sample when present.

The installed SDK includes the public value header in its exact fingerprint
closure. Its typed MeshRenderer layout changes; the C host ABI remains unchanged.
Runtime document preparation derives `runtime_features.mesh_wind:1` from admitted
Scene/Prefab/variant component and override channels. Package admission verifies
those requirements against the saved document bytes. Export and standalone
admission require kit `engine.runtime_mesh_wind:1`; default-off documents remain
compatible with earlier kits. Runtime Mesh admission uses the existing pose owner
to reject unsupported active combinations before export.

## Coverage and limits

`mesh_wind`, `mesh_wind_authoring`, `mesh_wind_editor`, `standalone_manifest`,
`mcp_mesh_wind_play_stdio`, `mcp_mesh_wind_sdk_stdio` and `mesh_wind_gallery` exercise
numeric/value/clock, Inspector/live/review, scoped MCP, profile and package owners.
`mesh_wind_geometry_vulkan` compares raster motion/surface frames and independently
baked shadow depth/silhouettes, including mirrored/tilted transforms.
`mesh_wind_gallery_vulkan` covers actual Scene/Game, time selection, batching,
picking, failure/mode/owner replacement and source-hidden relocated displays.
Gallery157 uses existing blade/heightfield assets and ordinary saved objects.

This is bounded procedural rendering, without fluid/weather simulation, plant
collision deformation, skin/morph combination, surface painting, population
streaming or a general vertex graph. Software Vulkan, native input fixtures and
local MCP clients do not establish Windows/MSVC/D3D12, full-editor, physical GPU,
device/DPI, graphical-EXE or external-agent-client acceptance.
