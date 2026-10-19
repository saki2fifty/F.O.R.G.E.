# World sprites

## Development status

Typed Scene authoring, shared world rendering, framing/geometric picking, bounded
Sprite instancing, actual editor controls, discrete frame animation and Gallery
content have normal/sanitizer and native rendering checks. The full Windows editor
selection and standalone-only checks pass at source
`a7df8bddd8285f378c62d1e0025b3b828b1513e3` in
[audit39267449500](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39267449500).
The benchmark comparison completed. Build261019-000193 passes the full numbered
source matrix, combined-package relocation, actual Gallery export/startup and final
editor/SDK acceptance; see [delivery status](status.md). Hosted software
rendering does not establish physical GPU performance or desktop acceptance.

## Ownership

`SpriteRenderer` is an optional Flecs component, addressed in authored documents
as `forge.sprite_renderer`. It is independent of `MeshRenderer`; an entity may
contain both. Scene/Prefab ownership, property intent, Undo and typed reference
traversal use the existing built-in component machinery.

The component references an ordinary Material asset. That material owns its image
texture, filtering, alpha mode and depth/culling settings. Atlas selection and
appearance are numeric object values. There is no separate Sprite asset identity,
texture registry, world, renderer, animation clock or persisted GPU handle.

## Coordinates and values

The image occupies the entity's local XY plane. Width and height are positive
meters, from `0.0001` through `10000`, before the existing world transform. The
normalized pivot is measured from the image's top-left corner: `(0,0)` anchors
the top-left, `(0.5,0.5)` centers it and `(1,1)` anchors the bottom-right. Parent
rotation, scale, reflection and translation continue through the existing affine
transform owner. Flips change the image UVs while preserving its geometry/pivot.

Tint is a finite linear straight RGBA multiplier in `[0,1]`. The material continues
to determine whether alpha is opaque, cut out or blended. Enabled/visible flags
and camera layer masks remain ordinary component intent; structural visibility
and selectability policies also apply.

Grid frames start at the top-left, advance across each row, and use an integer
index. Columns/rows are positive, at most 16384 each. Margins are symmetric outer
pixel borders; spacing is measured between cells independently on each axis.
Every frame must contain whole positive pixels in the selected texture, and the
frame index must fit the grid. A 1×1 grid selects the whole image. Texture-dependent
admission belongs to the resource selection, rather than the authored Scene codec.

### Image fill modes (development source)

`image_mode` selects Simple (0), Sliced (1) or Tiled (2). Simple retains the
existing affine whole-cell stretch and is the default for legacy documents.
`pixels_per_meter`, default100, defines density before the entity's world scale;
it is independent of viewport DPI or camera zoom. It must be finite in
`[0.0001,1000000]`.

Sliced uses `border_left`, `border_right`, `border_top` and `border_bottom` in
whole source-cell pixels, each in `[0,16384]`. Each opposing pair must leave a
positive source center. Borders keep their natural meter size; the center and
middle edges stretch. If the target is smaller than its opposing borders, both
compress proportionally and the target center collapses. Texture-dependent border
admission uses the selected cell and retains the complete last-good draw on error.

Tiled repeats the whole selected cell from the top-left, cropping a partial final
tile. Flips mirror the complete fill, including the partial tile's position.
The authored width and height times density must each be at most1048576 source
pixels, bounding float repeat phase; ordinary component/property validation
rejects excessive combinations. Border fields are retained while inactive.
Repeated filtering stays inside the existing cell clamp/LOD0 contract; it does
not add periodic cross-edge filtering, mip-safe atlas gutters or atlas mipmaps.

These values use one existing Quad/Sprite profile and its existing nine-row
instance stride. The unused unlit basis rows carry normalized mapping parameters;
flat varyings carry the atlas and piecewise/repeat data. Sprite normal-frame and
legacy-lighting work is omitted deliberately. Standard Mesh lighting and its
seven-row layout remain. Numeric modes create no material, bundle or resource
identity, and compatible mixed-mode draws still instance in order. An Entry owns
the complete admitted configuration under the existing numeric-value budget.

Development Gallery73 compares the modes using original panel art. Local normal
and sanitizer checks cover mapped pixels, shared-resource retention/recovery,
actual HDR Game rendering, Source/live Inspector controls and installed-only C++
field access/cold recovery. Build193 does not include these modes or Scene73;
Windows/export acceptance and the coherent final gate remain pending.

`order` is a numeric priority in the existing draw queue. Transparent
camera depth remains authoritative before equal-depth priority; higher priorities
draw later. Ordinary objects have priority zero. The comparison applies to the
whole queue, including stable draw-scope ties, rather than conditionally comparing
only two sprites. Material depth tests still determine opaque occlusion.

Framing and geometric picking use the same admitted Quad pose. Picking does not
test image alpha, matching the existing Mesh picking contract. At equal picked
depth, explicit priority ranks before the stable entity tie. Mesh and Sprite
draw scopes on one entity remain independent.

## Initial native material profile

The implemented private profile uses the immutable built-in Quad and an ordinary
`forge.gltf.unlit.v1` material with exactly one `baseColorTexture`. It requires:

- Color interpretation of a 2D RGBA8 or RGBA8 sRGB image with opaque/straight alpha.
- UV0, identity material UV transform, matching nearest/linear min and mag filters.
- Non-anisotropic, noncomparison sampling with no LOD shift.

The shader maps full-frame UVs, clamps to base-level texel centers and samples
LOD0. This deliberately avoids atlas mip sampling; it is not safe atlas mip
generation. Minification may alias. Compressed blocks, HDR, premultiplied/custom/
unknown alpha and custom shader surfaces reject in this initial profile. Sprite
bundles do not allocate shadow passes. Material culling/depth settings remain
material-owned; a material intended to show both sides should be double-sided.

The Sprite pipeline has its own transient draw-bundle key but shares existing mesh
and texture resources. Its instance data includes RGBA and atlas values. Ordinary
mesh instances retain their existing seven-row GPU layout. Frame/tint values do
not create resource keys or new material identities.

The existing renderer Entry owns the admitted Sprite configuration and numeric
values with its complete resource selection. Invalid frame/grid updates and failed
material replacements retain the usable selection. Corrected values retry without
an unrelated asset edit. Malformed transport components follow the existing
diagnose-and-omit policy. Fixed Sprite value storage counts toward the existing
scene material-value budget; native draw-part and pose budgets remain independent.

An Entry derives the image extent from its successfully admitted immutable
selection and retains that extent with the selected resources and Sprite values.
Numeric changes validate their grid against this admitted extent without repeating
material/texture profile validation. Failed resource replacement keeps the old
extent; successful replacement updates it atomically. The immutable unskinned
Quad pose is reused while its final world matrix is unchanged. Invalid poses
retain complete prior values, and correcting the world retries pending values.

Contiguous compatible translucent Sprite draws may instance in queue order.
Ordinary blended Mesh draws keep their existing policy. An unlit Sprite does not
consume the scene's 3D light list. These are extensions of the existing world
composition path; sprites have no separate overlay pass.

## Editor authoring (development)

Entity > Create > Rendering > Sprite creates an ordinary entity with the typed
SpriteRenderer component. To choose an image, use Content > Create / Register >
New material, select the Sprite / image preset and a registered color image, then
create and save the material. The preset creates an ordinary unlit, two-sided
Blend material with depth testing and no depth writes. Pixel filtering chooses
nearest sampling; the default is linear. The existing Material workspace owns
further alpha/depth/filter edits and normal publication. Assign that published
material in the Sprite Inspector.

Inspector fields are grouped into image material, sheet frames, size/pivot,
appearance and visibility. Scene property validation, Undo/Redo, prefab overrides
and saved references retain their existing owners. The displayed frame bound
follows the configured columns and rows; actual cell size still requires the
selected texture. Temporary Play fields expose frame, width/height, pivot, flips,
tint and order through the existing live-value panel. Development source also
exposes fill mode, density and sliced borders through the same temporary owner.
Stop restores authored
values; explicit Keep values review uses the existing mechanism. Material/grid
resource changes are outside that live whitelist.
An animation continues to control its bound fields; manual live Frame changes
are useful on sprites whose frame is not currently driven by a track.

### Visual sheet selection (development source)

`Choose frame...` accompanies the numeric frame field in Scene Inspector,
temporary live fields and Prefab source drafts. Timeline offers the same chooser
for a SpriteFrame binding with a valid Sprite preview target. It uses the selected
Sprite's Material and uniform grid; Timeline confirmation only changes its pending
key field. Existing property commands, runtime submission, draft publication and
clip history remain authoritative. Numeric inputs and their context/reset actions
are preserved. The chooser adds no public SDK, serialized asset or runtime code.

The one transient preview resolves the published Material through
`request_model_pbr_material` and its actual base-color Texture through the existing
resource pools and `TextureAssetPreview`. Sprite material/texture profile admission
precedes GPU adoption. Selection requires the exact current resource identity;
retained last-good viewer pixels never authorize a failed replacement. Requests
remain asynchronous and bounded. Main retires the private preview after UI
submission on closure/project changes, using the existing GPU residency owner.

Hit testing reuses `sprite_grid_region`: top-origin row-major U32 frames, no hits
on margins/gaps, checked wide arithmetic. Visible grid edges have a fixed bound;
large grids allocate no per-cell widget, texture, job or record. Preview outputs
reuse the existing crop renderer and stay within2048 pixels per axis. Wheel zoom
is anchored under the pointer; right-drag pans; keyboard navigation remains
pending until confirmation and reveals the highlighted cell. Context/revision,
read-only and disappearing-field transitions invalidate the chooser.

Local input/owner checks and native software-Vulkan chooser images pass normally
and with ASan/UBSan/leak inspection of the affected own units; pinned/CPU libraries
remain normal. Three scales, a small window, Scene Undo/cancel, prefab discard,
live restoration, Timeline pending-key application and image revision failure/
recovery are covered. Six native chooser captures were visually reviewed.
Actual Windows shell acceptance remains pending. Build193 does not include this control. There is no
irregular slicing, atlas packing, atlas mip generation, tilemap or new animation
clock in this change.

Atlas frame animation extends the existing Timeline property owner with
`forge.sprite_renderer.frame`. It holds whole frames between keys and rejects
curve/tangent settings. See [transform animation](transform-animation.md#sprite-frame-binding-development).
The delivered frame-animation gates are recorded in [status](status.md); visual
frame-chooser validation is separate.

## Persistence and SDK

The new component uses the existing Scene/Prefab formats and unknown-extension
preservation. No existing component is migrated. Its public C++ definition changes
the exact SDK fingerprint, so native modules need a matching SDK rebuild. Native
ABI1 remains unchanged. Recovery is still an exact-build, private runtime domain.

## Validation boundary

Focused tests cover grid partitions/bounds/overflow, CPU material/texture admission,
typed component authoring/history/reopen/reference traversal, and native draw,
bundle, scene and actual Game consumers. Software Vulkan checks include frame
changes, invalid-grid/material recovery, pivot/layer/selectability picking,
Mesh/Sprite coexistence, transparency against an independent alpha reference,
129 sprites in three draw calls, bounded numeric churn, payload admission and
large non-power-of-two single-pixel atlases through HDR/display at exposure 8.
This is correctness evidence, not a physical GPU throughput claim.

Sixteen additional native cases compare wide/tall views and pivot/reflected/sheared
poses against an independent XY polygon oracle, including geometric picking. Real
ImGui input tests cover creation, image-material publication, Inspector history,
save/reopen, live controls and Timeline frame authoring at three interface scales.
Gallery71/72 checks consume actual imported RGBA images and published materials,
then verify fixed/presentation frame timing and a seven-asset content closure after
relocation and removal of the authored copy. Content-package acceptance is separate
from executing an exported standalone game.

Windows Sprite render cases are registered but have not executed. Native editor
visual review, current-code sanitizer coverage, representative performance
measurements and relocated standalone-game execution remain required before completion.
