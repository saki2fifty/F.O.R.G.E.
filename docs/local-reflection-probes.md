# Local reflection probes

## Values and ownership

`forge.reflection_probe` is an optional entity component. The installed value-only
`forge/reflection_probe.hpp` defines `ReflectionProbe`. Defaults: unassigned typed
TextureAsset `texture`, `enabled=true`, `intensity=1`, local half extents 5 m,
`blend_distance=1` world metre, `rotation_degrees=0`, signed 32-bit `priority=0`,
`box_projection=true`. Finite intensity/fade bounds are 0–1000000; half extents are
0.0001–1000000. Yaw accepts finite doubles. Disabled values still validate.
Absent, disabled, empty-map and zero-intensity components are inactive.

The existing built-in codec, native Meta validation and Scene/Prefab owners retain
identity, unknown extensions, references, inheritance, Undo and saving. The
`render.reflection_probe` entity recipe is appended to the existing creation catalog.
Inspector, automation and MCP share this catalog and the ordinary `property.set`
operation. Discovery carries typed texture and field bounds. Permissions, expected
revision, exact replay and project scope are unchanged.

Live gameplay admits the nine scalar controls through the existing runtime
property-tuning owner. It cannot assign a texture or create a component. Generation,
Scene and entity checks precede complete-value validation; rejection leaves values
unchanged. Stopping restores authored values unless the existing tuning review is
used to keep selected fields. C++ systems can access the component through shared
Flecs. Adding this installed header deliberately changes the exact SDK fingerprint;
rebuild modules with the matching SDK. ABI1 tables and prior value layouts remain
unchanged. The installed `reflection_probe_gameplay` sample exercises this boundary.

## Volume and selection

Entity TRS shapes an oriented box. Existing affine inversion/conditioning produces
three unit world planes and half distances, including mirrored/nonuniform scale
and shear. World-metre inward distance determines edge fade; zero is a hard edge.
Unrepresentable/singular geometry skips the probe with a diagnostic. Camera-relative
centers subtract in double before float packing; a distant unrepresentable probe
skips only that camera. Map yaw is independent rotation about world Y.

Ready probes sort by descending priority, then stable EntityId. Each consumes its
fade weight from remaining coverage; uncovered weight keeps global base specular.
Optional box projection intersects the world reflection ray with the influence box
and samples from the entity's capture center toward the exit. Imported maps do not
capture the actual Scene. The probe does not rotate the sky or replace diffuse,
clearcoat or sheen lighting; anisotropic receivers retain environment lighting.

At most eight active assigned probes are admitted per Scene profile. More preserve
global lighting for all probes and issue an actionable capacity warning. This is
not a nondeterministically chosen subset. Per-entity candidates/last-good leases
reuse MeshResourceHost's texture pools and EnvironmentResidency. Changed catalog
epoch retries a failed request. Removing/disabling/clearing a probe releases its
selection. Failure of a replacement retains the prior usable map with an error.

## Rendering and limits

The private `SurfaceReflections` operator owns pipeline/bindings/constants, while
TemporalView retains view targets and the existing mesh queue retains ordering,
culling, layers, parts, poses and instancing. Eligible opaque/masked depth-writing
Mesh receivers must form the existing compatible queue prefix; conflicting order,
unsupported surface outputs, depth policy or view capacity preserves ordinary
rendering with `render.temporal.unavailable`. Cameras must clear color and depth.
Unlit/custom final-color surfaces and current-frame layers keep their existing path.

The material outputs are the same current mapped normal/roughness, unit base
response and replaceable base specular used by [screen-space reflections](screen-space-reflections.md).
The local pass calls pinned public cube/yaw sampling functions and updates HDR and
total indirect with the same base-specular delta. It runs before screen reflections
so their hit color and replaceable specular include the probes. AO then consumes
current indirect. Neither pass replaces punctual/emissive light, additive fog or
alpha. Unsupported or nonfinite/overflowing results retain original lighting.

Local-only targets cost 60 B/pixel; adding screen reflections costs 8 B/pixel for the
updated specular image. AO adds 8 B/pixel, AA 36, active motion without AA 24, motion
with AA 8 and frozen capture 20. Eight Game views share the existing 512 MiB surface
payload estimate; Scene preview has its own. Focus, native allocation, environment
residency and transient overhead are additional. Target/pipeline changes stage
before adoption. Spatial-only use adds no motion field, jitter, pose/color history,
clock or warmup.

No runtime capture/baking, irradiance volumes, per-Mesh probe masks, arbitrary
layered reflection, separate coat/sheen probe response or multi-bounce GI is claimed.
Windows/MSVC/cooked D3D12 and physical GPU/input/DPI acceptance remain pending.
See the [lighting guide](../manual/editor/lighting.md) for the editable workflow.
