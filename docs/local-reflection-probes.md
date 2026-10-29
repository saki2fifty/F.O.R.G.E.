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

## Editor-authored map snapshots

The Inspector's **Capture reflection map…** and
`capture.reflection.start` / `forge_capture_reflection_start` use the existing
ViewImageTask. One scoped ReflectionCaptureTask owns setup, progress, cancellation,
Scene/document/revision selection and the associated SourceImport batch. Its
injected native driver borrows MeshResourceHost and owns one FrameRenderer; the
portable editor boundary replaces only native pixel delivery in tests. There is
no additional world, asset registry, texture pool, publication service or clock.
PNG, sequence and reflection operations cannot overlap in that image owner.

Capture accepts a resolved authored probe, including an unassigned/disabled one;
render-list admission is deliberately independent. Its world center selects six
canonical +X/-X/+Y/-Y/+Z/-Z 90-degree cameras with positive near and larger finite
far planes, a Camera layer mask and16–1024square faces (default256). Probe entity
rotation/scale shape the volume but do not rotate the world-aligned source map.
The detached authored RenderScene snapshot is static. Source lighting, shadows,
fog and the source environment remain. The source sky is included even when its
authored display sky is hidden. Recursive probes/SSR, AO, AA/history, motion/focus,
bloom, automatic exposure, grading and display resolve are excluded. Resource
readiness is asynchronous and bounded; completed rendering diagnostics fail the
capture instead of silently publishing missing producers.

LinearHdr is a private output selection on the existing FrameRenderer. Raw
RGBA16F readback uses the presentation owner's one staging ticket, source lease,
frame fence and cancellation/retirement path. The display API remains RGBA8.
Six CPU faces require at most48MiB; DDS encoding temporarily retains the cube and
an equally sized output plus148bytes. Encoding runs off the frame loop. One GPU
readback face needs at most8MiB, within the common64MiB readback policy. Existing
view/environment/native allocation budgets still apply separately.

The pure forge_texture encoder writes a restricted standard DDS DX10 source:
one cube, one mip, RGBA16Float/HdrColor/Opaque, finite nonnegative RGB and alpha1.
It does no IO or identity allocation. SourceImport.generated_source retains the
encoded bytes through the existing immutable generated SourceCopy plan and review.
That owner checks source/catalog digests and project ownership and atomically
creates a NEW Assets subfolder; existing sources are never overwritten. A missing
asset index can be guarded as absent; an appearing or empty index rejects commit.
The ordinary texture container importer/worker then owns publication and its new
TextureAsset identity. Copy, publication and Scene assignment are separate results.
No texture cook/schema version or installed SDK value layout changes.

After publication, automatic assignment requires the original project, document
generation, Scene ID and Scene revision, and Edit readiness. Typed map assignment
and resetting map yaw to0 use one shared Scene command batch/Undo step. Saving is
explicit. Changed scope leaves a published map available for explicit assignment;
there is no silent asynchronous rebase. Cancellation retires accepted GPU/encoding/
import work and suppresses assignment; completed copies/publications remain.
Source-only copying reports that result without inventing a published identity.
Per-file import errors remain visible. MCP uses the same owner, grants, schemas,
revision/replay guards and import controls; an output grant cannot cancel an asset
capture and an asset grant cannot cancel an output capture.

`reflection_capture_vulkan` checks known HDR faces, orientation/layers/translated
positions and accepted-ticket cancellation. `reflection_capture_editor` uses the
actual controls, worker/publication, history and save owners at three scales;
only GPU delivery is recorded. `mcp_reflection_capture_stdio` exercises both
supported protocols. `reflection_capture_workflow_vulkan` captures an actual lit
Scene, imports/assigns/saves the map, loads the ordinary Texture resource, then
compares source-free relocated reflection-on/off renders exactly. These are local
software-rendering/authoring checks, separate from full Windows/D3D12 and physical
acceptance. Runtime recapture and in-place stable-ID rebaking remain unavailable.

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
and samples from the entity's capture center toward the exit. A map is a fixed source snapshot; the component does not automatically
capture changes in the Scene. The probe does not rotate the sky or replace diffuse,
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

No runtime recapture, in-place rebaking, irradiance volumes, per-Mesh probe masks, arbitrary
layered reflection, separate coat/sheen probe response or multi-bounce GI is claimed.
Windows/MSVC/cooked D3D12 and physical GPU/input/DPI acceptance remain pending.
See the [lighting guide](../manual/editor/lighting.md) for the editable workflow.
