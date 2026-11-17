# Authored cameras and lights

Native reflected Camera/Light components feed editor creation recipes, the
Inspector and shared Scene/Game rendering, including admitted light/shadow budgets.
The [rendering contract](rendering-foundation.md) records native WARP coverage and
backend limits. Scene navigation retains its separate personal editor camera.

## Ownership and coordinate contract

Camera and Light are authored Flecs components. They use ordinary component and
property prefab intent, including equal-value overrides and Revert. Independent
LocalTranslation/LocalRotation/LocalScale remain the only authored transform
channels. World pose, view matrices and GPU data are transient derived values.
Neither component contains device pointers, process handles or another transform.

FORGE retains local +Z camera-forward. `ViewBasis` explicitly selects the FORGE +Z
or imported glTF -Z adapter; both use local +X right and +Y up. Importing a glTF
camera/light does not rotate its node or create a hidden child. Children, animation
channels and mesh transforms therefore retain the original source TRS.

The exact [glTF camera contract](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/specification/2.0/Specification.adoc)
requires a nonzero, proper normalized world orientation. FORGE diagnoses collapsed,
reflected or sheared camera frames before projection. It does not forbid those
visual object transforms globally or change authored hierarchy to repair a camera.
Nonuniform positive scale is removed from camera orientation and does not alter
FOV or clipping distances. Explicit orthogonality and normalized determinant checks
use a1e-5 tolerance, consistent with float-authored rotation and double evaluation.

## Camera Follow

`forge.camera_follow` is optional native `CameraFollow` in the installed
`render_components.hpp`. It uses enabled/EntityRef target/XYZ offset/weight and
`CameraFollowSpace` (World0, Target1). Defaults are enabled, unbound, offset(0,2,-6),
weight1 and World. Offset-space JSON must be integer0or1; fractional, negative,
unknown and narrowing-wrap values reject before mutation. Other finite/target/
weight validation shares the existing Camera Aim value owner.

The shared snapshot resolver validates the native Camera frame, follows position,
then applies Camera Aim. World offsets add metres to the target's resolved node
location; Target offsets transform by its complete resolved frame. `std::lerp`
mixes each native eye coordinate toward the desired eye by Weight. Camera Shot
endpoints consume this completed view before their existing interpolation. There
is no motion integrator, damping, obstruction solver, transform writer, new clock
or dependency recursion. Source TRS and other producers keep their native poses.

Inactive/unbound/zero-weight following bypasses target lookup. Active invalid
targets/nonfinite desired eyes produce `render.camera_follow.invalid` with
`forge.camera_follow` and the original camera identity. Private
`camera_follow_error` carries endpoint failures; existing `camera_aim_error`, Aim
category/property and payload shape remain unchanged. A shared private failure
base routes either kind without duplicating camera-authoring behavior.

Typed defaults/enum/help/reference adapters share the normal Scene/Prefab/history
owners. Native Inspector, live scalar/space review, generic automation and MCP use
the same operations. Target assignment stays Edit-only. `play.cameras` includes
both modifiers using its existing bounded projection owner and explicit extent.
Matching SDK modules rebuild for the expanded public header; Camera/Aim layouts
and ABI1 tables are unchanged. Gallery220 reuses existing animation and validates
source/hidden relocated runtime; stopped frame sampling uses the same resolver.

## Camera Aim

`forge.camera_aim` is the optional native `CameraAim` value in the installed
`render_components.hpp`: enabled, stable `EntityRef target`, target-local
`offset_x/y/z` and finite `weight` in [0,1]. The target defaults to null, offsets
to zero, enabled to true and weight to one. It uses existing reflected defaults,
typed reference remapping, Prefab intent, Scene history and opaque-field retention.
No Camera layout, C ABI table, node transform ownership or identity format changes.
Public-header admission requires modules rebuilt with the matching SDK fingerprint.

The shared detached snapshot resolver borrows one bounded row index. It reads the
camera's resolved node frame and the target's resolved node frame, transforms the
local offset by the target frame, then rotates the camera frame through the
smallest angular turn toward that point. Weight multiplies that angle. Authored
up selects the antiparallel axis; there is no horizon lock, damping, independent
clock, transform writer or recursive target-view graph. The camera's position and
other producers retain their node poses. Existing positive-scale removal and
camera frame validation apply before aiming; reflected, collapsed or sheared
frames are not repaired. Both forward bases and projections share this owner.

Disabled, unbound and zero-weight aims bypass target lookup. A bound active target
must be a same-Scene actual instance with a resolved finite node pose. Coincident
points and invalid bindings produce `render.camera_aim.invalid` with the original
camera identity and `forge.camera_aim` property, scoped to that view. Shot endpoint
resolution applies aim before interpolation and carries bounded transient
`camera_aim_error` metadata if an endpoint fails. Extraction consumes a successful
`camera_world_affine` once; it never aims an already blended view a second time.
Other cameras, Meshes, Lights and children retain their existing owners.

Native Inspector, generic source automation/MCP and live tuning share these typed
values. Target is Edit-only; Enabled, offsets and Weight support the existing
runtime tune/review flow. Scene frustum helpers use the same view resolver. The
read-only `play.cameras` operation exposes prepared CPU views for an explicitly
requested extent/output; see [MCP](mcp.md). It does not render a framebuffer.

## Projection and composition

Perspective uses a full vertical FOV in radians, strictly between zero and pi,
and a positive near plane. Finite far must exceed near. An explicit infinite-far
mode implements valid glTF perspective cameras with omitted `zfar`; the retained
finite-far field is ignored in that mode. Orthographic uses positive full height,
optional full width, nonnegative near and finite far greater than near. Infinite
orthographic projection is rejected.

The original design's finite-far-only wording was expanded because the exact glTF
source explicitly permits infinite perspective. No previous authored Camera format
or camera component identity is being migrated by this addition.

Aspect zero follows the viewport. Positive aspect fits inside it without stretching;
explicit orthographic width selects width/height aspect instead. Normalized viewport
rectangles must fit inside the target. Pixel conversion must produce at least one
pixel in each dimension; aspect fitting centers the reduced rectangle. Negative
source orthographic magnifications become positive sizes plus explicit horizontal/
vertical projection flips. They are not silently rejected or applied to node scale.

Enabled cameras compose by ascending order, with EntityId as the deterministic tie
breaker. Each camera declares clear-color/depth policy, linear background RGBA and a
render-layer mask. `Camera.render_texture` optionally selects a saved
RenderTextureAsset; nil selects the main presentation. Cameras sharing a target
compose at that configuration's dimensions. The bounded output graph, Material
sampling, same-frame dependency order and lifetime are described in
[Scene camera render textures](render-textures.md).

Derived projection storage is row-major4x4 applied to column vectors. View depth is
positive and D3D clip depth is[0,1]. Double world/view evaluation is distinct from
explicit float GPU conversion. Nonfinite, overflowed or underflowed projection
coefficients reject before upload. `orientation_reversed` includes the imported
view-basis and image-flip parity for the render owner; it is not authored truth.

## Camera target validation and captures

`camera_aim_math` uses independent angular/coordinate oracles for both forward
bases, weights, roll, opposite/vertical targets, extreme finite directions and
invalid camera frames. `camera_aim_scene` covers scoped diagnostics, forward row
references, opaque fields, duplication, Prefab instances, history and save/reopen.
`camera_shot_transition` adds aimed endpoint/curve/recovery checks;
`camera_aim_gallery` checks the animated target and source-hidden relocated export.
`camera_transition_frame_source` samples the same Gallery through stopped Timeline
frame export and checks its direction without changing Scene history.

`camera_aim_editor` drives the actual target picker and numeric Weight widget,
including Undo/Redo at interface scales 1, 1.5 and 2. Its `--capture image.png`
option writes the canonical manual image `manual/images/camera-aim-controls.png`.
The 780 by 360 capture uses the actual ComponentInspector and Scene context,
native ImGui draw data, the packaged font and SDL's software renderer on Linux.
It is unretouched and shows a restored authored target, Offset Y 0.5 and Weight 1.
This is native widget evidence, rather than a full Windows desktop or physical
GPU acceptance claim. `camera_output_process` and
`native_sdk_camera_output_installed` exercise matching public layout/field access;
the latter builds from the installed SDK and runs after relocation. The two
`mcp_camera_aim_*` routes exercise ordinary/SDK Play over both framing formats.

`camera_follow` adds independent weighted World/Target-space eye oracles, both
forward bases, Aim composition, attached-producer isolation, self/mutual targets,
coincidence and bounded input/failure checks. Shared authoring/Prefab tests verify
both modifier references. `camera_follow_gallery` reuses the Gallery fixture's
`--follow` profile for Scene220, including recovery and relocated two-asset closure.
The shot and stopped-frame fixtures include composed Follow/Aim samples.

`camera_follow_editor` uses the same actual Inspector fixture with `--follow`,
including coordinate-space selection, target and numeric history at three scales.
Its `--follow --capture image.png` option produces the canonical
`manual/images/camera-follow-controls.png`, an unretouched 780 by 400 SDL/ImGui
capture with the real Scene context. The reviewed image shows Moving target,
offset(0,2,-6), Weight1 and World space after history restoration. The SDK process
checks include native Follow fields. Both `mcp_camera_follow_*` routes additionally
check composed eyes, space/weight/offset/enabled tuning, fractional-space refusal
and runtime value review in ordinary/SDK Play over both framing formats.

## Punctual lights

Directional intensity is lux; point/spot intensity is candela. RGB is a linear
multiplier in[0,1]. Intensity is nonnegative. Positive point/spot range is a hard
distance cutoff; zero represents an omitted/unbounded range. Spot cone angles are
half angles in radians with0≤inner<outer≤pi/2. World scale never multiplies range,
intensity or cone angles. This follows the exact
[KHR_lights_punctual source](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/extensions/2.0/Khronos/KHR_lights_punctual/README.md).

Directional/spot direction is the normalized transformed local forward vector;
a collapsed forward direction diagnoses an unavailable light. Point lights require
only their finite world location and can remain valid on a collapsed visual node.
Native GPU conversion rejects physical values outside float representation and spot
cones whose inner/outer cosine values become indistinguishable at float precision.
This admission is separate from authored double-value validity.

Shadow request, nonnegative depth bias, world-space normal bias and affected layers
are authored intent. Actual shadow types, quality and budget are renderer profile
capabilities. The current shared shadow owner supports Directional, Point and Spot
maps; Rectangle and Sphere requests produce an explicit unsupported diagnostic.
A single intensity field has type-dependent units; its help states lux/candela rather
than attaching an incorrect fixed native unit to both light kinds. Angle/distance
fields use native Flecs Units where applicable.

## Area emitters

`LightKind` retains Directional0/Point1/Spot2 and appends Rectangle3/Sphere4.
`emitter_radius`, `emitter_width` and `emitter_height` append to the installed Light
layout. Defaults are0.5/1/1; all three values validate as finite0.001–10000 metres.
Older documents acquire these defaults without rewriting their source files.
Rectangle width/height are full dimensions. Intensity is distant normal-axis
candela for Rectangle and distant omnidirectional candela for Sphere. Range uses
the emitter center; node scale never multiplies emitter size or intensity.

Rectangle extraction uses the existing forward basis and world X axis to form an
orthonormal frame, retaining roll and removing scale/shear. A collapsed forward
or unusable in-plane axis diagnoses an unavailable Rectangle. Sphere needs only
its finite position and stays valid on a collapsed visual node. Rectangle emits
from one face; Sphere is opaque and does not illuminate receivers on/inside it.
The renderer contract describes the bounded original integration and limits.
There is no glTF area-light import extension in this implementation.

All authoring paths reuse Meta, Scene property validation, history and persistence.
The three emitter dimensions and two punctual shadow-source dimensions support
live tuning. Light-image dimensions and tiling also support live tuning when
applicable; image assignment and other Light properties remain Edit-only. Matching C++ modules
must rebuild against the new installed SDK fingerprint. No C ABI function table
or separate light registry is introduced.

## Light images

Light appends a nullable Texture reference, Directional image width/height and a
tiling flag. Directional/Spot use Color/HDR 2D textures; Point uses a Cube. Their
RGB modulates direct radiance; alpha is ignored. Source sampler intent, native
Meta/defaults, scene extraction and current resource owners remain authoritative.
Undefined image-only orientation retains ordinary lighting. See the
[light-image contract](light-images.md) for coordinates, private Shader 12 packets,
shared image budgets, exact SDK rebuild requirements and current evidence limits.

## Admission and history

Native Meta/Doc/Units/enums describe the actual component layout, checked against
C++ member offsets. FORGE scene/prefab candidate validation enforces cross-field
constraints before mutation or source publication. Unknown authored fields remain
opaque roundtrip data. Stable Flecs native writes are not a transactional veto;
presentation consumers must revalidate native producer values before GPU use.

Camera/light glTF placement uses the existing model selection, member identities,
source revision checks and one Scene edit/Undo step. Prefab-asset publication and
scene history remain separate. Derived matrices are not serialized or inherited.

## Copied presentation admission

`extract_render_scene` consumes the existing effective-scene presentation values.
It copies typed camera/light/mesh settings and derived matrices, keyed by EntityId
within the scene AssetId. It contains no Flecs or Diligent handles. Template rows
are excluded; disabled cameras/lights and hidden/disabled mesh renderers are omitted.
Duplicate/malformed identities reject the envelope. Invalid individual components
produce contextual diagnostics without rewriting the scene or discarding unrelated
valid components. Retention is256 diagnostics plus an explicit omitted count.

`prepare_game_cameras` validates target-dependent projection and preserves ascending
composition order. Its optional output argument selects that RenderTextureAsset;
the default nil argument includes only main-output Cameras. It reports absent valid cameras instead of copying the Scene
navigation camera. The renderer combines this copied subset with managed resource revisions,
skinning/morph pose data and camera draw composition; see [render features](render-features.md).
This extraction function alone does not own those resources or their lifetimes. Existing runtime interpolation supplies world poses before
this consumer runs, so camera/light/mesh motion uses the same presentation sample.

The selected FXC5.1 profile also rejects nonzero projection/range coefficients that
would become float32 subnormals. Direct3D arithmetic flushes those values to zero;
that would turn a tiny range into the special unbounded value or invalidate a near
plane. This applies at GPU conversion, not to the authored LocalScale domain. See
[Microsoft floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules).


## Scene authoring helpers

The editor derives camera/light icons and selected guides from the effective scene
snapshot. It creates no helper entities or persistent renderer components. Icons
respect structural visibility and selection locks; the Hierarchy remains available
for selecting a locked entity. Disabled components remain recognizable. Combined
Camera/Light entities retain both icons.

Camera frusta use the same `camera_view` projection/basis/viewport calculation as
the renderer, including orthographic, fixed aspect and imported negative-Z cameras.
Infinite or display-capped guides have open ends and a visible distance explanation.
Point-light guides show range; spot guides use radial range and inner/outer half
angles. Rectangle guides show the oriented emitting face and travel arrow; Sphere
guides show three radius rings. Shape guides and framing share the same extraction
and world dimensions. Unlimited/capped light guides say so. Directional arrows show light travel.

The Scene-only Preview light option replaces lights for that draw and restores the
retained extraction afterward. It affects neither authored components nor Game
rendering, history or scene serialization. Its preference and helper visibility,
size and guide distance are personal editor settings. See the [viewport manual](../manual/editor/viewport.md)
for the user workflow. Native source-audit tests cover preview illumination,
authored-light restoration and an unchanged Game frame; fixture captures are
reviewed separately from assertions.

Helper extraction refreshes after SceneTools input as well as before picking, so
a drag uses the same effective pose for mesh rendering and guides. Snapshot reads
are sequenced before reading their cache generation. Auto-aspect frusta use the
published render-texture dimensions for an assigned Camera, including its normalized
viewport. Unavailable output configuration hides the frustum. Main-output Cameras
use the last Game dimensions, falling back to the Scene area before a Game frame
exists. Fixed Camera aspect remains authoritative. Definitions come from the shared
CPU resource owner; no GPU-specific handle is read.
