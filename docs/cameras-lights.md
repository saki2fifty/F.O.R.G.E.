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
