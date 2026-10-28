# World sprites

## Delivery and validation

Build261022-000208 includes image regions, Simple/Sliced/Tiled fill, animation and
Gallery78 with the matching C++ SDK. Its complete source/editor/SDK/export/package
gates pass; see [delivery status](status.md). Actual Windows region images and
source-free/fresh-runner exports were reviewed. Physical GPU/FPS acceptance remains
separate. The records below also describe earlier Sprite milestones.

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
appearance are plain object values. There is no separate Sprite asset identity,
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

### Image regions

`region_enabled` selects an optional whole-pixel rectangle before grid subdivision.
`region_x` and `region_y` are offsets from the material image's top-left corner;
`region_width` and `region_height` are extents. Each numeric field is in `[0,16384]`.
A zero extent reaches the corresponding image edge, so enabling the default region
keeps the whole image. Disabling it preserves its stored values.

The region must fit the current texture. Grid counts, symmetric margins and spacing
apply inside that region, and frames retain their relative top-left row-major
indices. One checked calculation supplies rendering, chooser outlines and pixel hit
tests. The Tile Set grid has no new crop fields. The chooser closes when its active
region or published image changes; outside pixels cannot choose a frame.

Regions remain per-object numbers under SpriteRenderer. Material/texture ownership,
resource keys and the existing private instance path remain. Different regions
can instance together. Width, height and pivot still define world geometry; cropping
does not resize it automatically. Simple, Sliced, Tiled and flips operate on the
selected frame. Invalid selection or a replacement image that cannot fit it retains
the complete last-good image and geometry until a valid candidate is admitted.

Frame animation uses the grid within the crop. Live crop changes use the existing
runtime-property path, remain independent of the animated frame value and disappear
on Stop. Gallery78 reuses the original signal clip, images and materials to show
static, animated, to-edge, sliced and mirrored repeated selections. It introduces
no atlas asset, source registry, renderer or animation clock.

### Ordered frame regions

SpriteRenderer.frame_regions is an optional ordered vector of up to512
SpriteFrameRegion values. The original x/y/width/height fields describe physical
texture rectangles. An empty vector preserves the existing
uniform grid. Positive whole-pixel extents and offsets0..16384 use the current
crop's top-left; frame selects a zero-based list entry. Grid fields remain authored
but inactive with a bank. No Sprite/atlas asset identity or image registry is added.
The installed value header participates in the matching SDK fingerprint. Added
metadata changes the C++ element layout from16 to36 bytes: modules must rebuild
with the current matching SDK. ABI1 C tables stay unchanged; old exact-profile
binaries are not compatible with this changed C++ value.

The existing codec/Meta/Scene/Prefab/history owners admit and preserve the list.
A native member annotation narrows the reflected collection bound; copied schema
reconstruction preserves it, so UI, C++ inspection and automation agree. The native
Inspector reuses paginated collection Add/Remove/Move-up controls. Its integer
draft seeds retain integer JSON, including exact64-bit bounds. Pixel fit is a
texture-dependent whole-bank check through shared sprite_frame_region; Sliced
borders must leave a positive center in every entry. Rendering keeps the complete
admitted picture/geometry/resource candidate on invalid entries or replacement.
Frame picking selects the first listed overlapping rectangle and ignores gaps.

Timeline/fixed/presentation/recovery share the existing integer frame binding and
clock; whole-clip admission uses bank count or old grid capacity. Runtime tuning
uses the same field path in ordinary and SDK Play. The512-entry vector has the
explicit128KiB/8192-container-element envelope; unrelated reflected values retain
64KiB/4096. Native sequence annotations are preserved through copied Meta;
container envelopes derive from annotated descendants. Native layout/schema limits
remain unchanged. No new frame-edit automation operation is needed; existing
property batches/history/Play/MCP discovery expose the data. Source-free original
Gallery87 demonstrates unequal frames, animation, crop, Sliced and Tiled fill.
Frame metadata supports Simple, Sliced and Tiled fills described below. Atlas-aware mip
generation remains outside this workflow. Automatic packing uses the existing
source/import owners described below.

### Logical canvases and stored quarter turns

Each frame optionally declares rotated_clockwise (false), source_width/source_height
(0 each), and trim_x/trim_y (0 each). Width/height remain physical stored extents.
A clockwise-stored frame resolves its retained logical width from physical height
and its retained logical height from physical width. Each zero source axis infers
that retained logical extent independently. Positive resolved canvases and offsets
must fit0..16384; retained content must lie wholly within its canvas. Old four-field
JSON and four-field C++ aggregate initializers retain their defaults.

The shared private codec/canvas owner supplies whole-bank validation and detached
draw interpretation. Explicit native missing-member defaults preserve legacy data
through Meta/native reconstruction, opaque-extension traversal and entity-reference
remapping; ordinary required members remain required. Trimming and stored turns
work in Simple, Sliced and Tiled modes. Actual trim additionally requires Blend with depth writes off,
checked for the entire bank against both initial and numeric-only candidate paths.
Invalid replacements retain the complete last-good geometry, resources and picture.

World geometry, pivot, picking and culling remain the full logical Sprite rectangle.
Logical flips mirror retained bounds as well as content. Simple retains its
existing transpose/inverse-crop arithmetic and identity fast path. Sliced/Tiled
receive untransposed logical coordinates, recover the logical flips from stored
orientation and physical region signs, then apply piecewise/repeated fill in the
original canvas. The fragment shader restores the unflipped retained rectangle,
discards transparent margins, normalizes inside retained pixels, restores clockwise
storage and applies the ordinary physical atlas clamp. Sliced borders use each
original canvas, including unused frames; Tiled density/partial tiles retain its
transparent margins. Storage dimensions never become logical border dimensions.

One explicit retained-crop row extends only the private Sprite instance layout
from9 to10 float4 rows (144→160 bytes, at most64 instances/10240 bytes per batch).
Buffer allocation, input descriptors, CPU staging and generated vertex/fragment
interfaces share that layout. Ordinary Mesh retains its7-row submitted layout.
This transient renderer change introduces no persisted field, asset, installed
SDK value/fingerprint or C ABI change. Existing C++ frame elements stay36 bytes.
Existing valid documents remain valid; older engine builds reject the newly
supported fill/metadata combinations. Use a matching engine build for this feature.
No alternate geometry/resource/material/atlas owner is introduced.

Original Gallery95 compares full source canvases with trimmed, clockwise-stored
frames, including a flipped pair. The installed Sprite sample writes metadata
through typed C++, and existing live tuning/recovery consume the same values.
Gallery97 compares full originals and packed Sliced/Tiled fills with asymmetric
borders and partial repeated canvases. The installed sample continues through
Sliced/Tiled metadata writes, and both hosts tune512 transformed entries through
those modes without advancing simulation during tuning.

Portable core/UI/installed-SDK/MCP/content checks and actual software-Vulkan pixels
are separate from native Windows/full Main/physical acceptance. See the canonical
[screenshot provenance](images/readme/README.md); new native Inspector capture is
pending.

### Image fill modes

`image_mode` selects Simple (0), Sliced (1) or Tiled (2). Simple retains the
existing affine whole-cell stretch and is the default for legacy documents.
`pixels_per_meter`, default100, defines density before the entity's world scale;
it is independent of viewport DPI or camera zoom. It must be finite in
`[0.0001,1000000]`.

Sliced uses `border_left`, `border_right`, `border_top` and `border_bottom` in
whole original-canvas pixels before trimming/storage, each in `[0,16384]`. Each opposing pair must leave a
positive source center. Borders keep their natural meter size; the center and
middle edges stretch. If the target is smaller than its opposing borders, both
compress proportionally and the target center collapses. Texture-dependent border
admission uses the selected cell and retains the complete last-good draw on error.

Tiled repeats the whole selected original canvas from the top-left, cropping a partial final
tile. Flips mirror the complete fill, including the partial tile's position.
The authored width and height times density must each be at most1048576 source
pixels, bounding float repeat phase; ordinary component/property validation
rejects excessive combinations. Border fields are retained while inactive.
Repeated filtering stays inside the existing cell clamp/LOD0 contract; it does
not add periodic cross-edge filtering, mip-safe atlas gutters or atlas mipmaps.

These values use one existing Quad/Sprite profile and its private ten-row
instance stride. The unused unlit basis rows carry normalized mapping parameters;
flat varyings carry the atlas and piecewise/repeat data. Sprite normal-frame and
legacy-lighting work is omitted deliberately. Standard Mesh lighting and its
seven-row layout remain. Numeric modes create no material, bundle or resource
identity, and compatible mixed-mode draws still instance in order. An Entry owns
the complete admitted configuration under the existing numeric-value budget.

Development Gallery73 compares the modes using original panel art. Local normal
and sanitizer checks cover mapped pixels, shared-resource retention/recovery,
actual HDR Game rendering, Source/live Inspector controls and installed-only C++
field access/cold recovery. Build193 does not include these modes or Scene73.
The combined full Windows editor/native gate and shared SDK/export/fresh-runner
checks pass; see [validation status](status.md). Physical GPU and desktop
acceptance remain separate.

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
follows the configured grid or ordered frame count; actual cell size still requires the
selected texture. Temporary Play fields expose frame, width/height, pivot, flips,
tint and order through the existing live-value panel. Fill mode, density, sliced borders, crop and frame regions use the same temporary
owner in ordinary and SDK Play.
Stop restores authored
values; explicit Keep values review uses the existing mechanism. Material/grid
resource changes are outside that live whitelist.
An animation continues to control its bound fields; manual live Frame changes
are useful on sprites whose frame is not currently driven by a track.

### Visual sheet selection (development source)

`Choose frame...` accompanies the numeric frame field in Scene Inspector,
temporary live fields and Prefab source drafts. Timeline offers the same chooser
for a SpriteFrame binding with a valid Sprite preview target. It uses the selected
Sprite's Material and grid or ordered regions; Timeline confirmation only changes its pending
key field. Existing property commands, runtime submission, draft publication and
clip history remain authoritative. Numeric inputs and their context/reset actions
are preserved. The chooser uses the same component values and one pending integer frame; it adds
no tool-specific asset or animation clock.

The one transient preview resolves the published Material through
`request_model_pbr_material` and its actual base-color Texture through the existing
resource pools and `TextureAssetPreview`. Sprite material/texture profile admission
precedes GPU adoption. Selection requires the exact current resource identity;
retained last-good viewer pixels never authorize a failed replacement. Requests
remain asynchronous and bounded. Main retires the private preview after UI
submission on closure/project changes, using the existing GPU residency owner.

Hit testing reuses checked top-origin frame-region math: row-major grids or
ordered rectangles, no hits on margins/gaps, checked wide arithmetic. Visible grid edges have a fixed bound;
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
Actual Windows shell input and native capture review now pass at all three scales;
see [validation status](status.md). Build193 does not include this control. There is no
irregular slicing, atlas mip generation, tilemap or new animation clock in the chooser.
Automatic image packing is a separate Content source-authoring operation.

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
Image regions append a bool and four U32 fields to SpriteRenderer; old documents
admit default-off/zero values without identity or format changes. Matching experimental
SDKs and native modules must be rebuilt for that public C++ layout. No ABI1 callback
table changes are required by regions.

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

## Automatic atlas source packing

Content **Create / Register → Pack sprite atlas...** and automation
`asset.atlas.prepare` use one SourceImport job and its existing detached
review/copy/cook/cancel/close lifecycle. Input is1–512 distinct published root
Texture assets: sRGB RGBA8 Color, D2 single layer/depth, opaque or straight alpha.
Model members, compressed/HDR/data/normal/premultiplied images reject. Preparation
reads the current selected cooked data through `load_texture_selection`, the same
CPU loader used by resource requests. Aggregate decoded input storage, including
loaded mip levels, and output pixels have a64MiB limit. The PNG encoder's existing
4096-side/64MiB pixel/68MiB encoded bounds remain unchanged.

Independent deterministic shelf search orders candidates by descending height/
width and tests a bounded sequence of widths. Equal-size ties preserve input order;
reported frame order always matches the caller's input order. It chooses among
these candidates by area, longest side, then width; it does not claim optimal
bin packing. Padding0–64 applies on all sides. Optional nearest-edge extrusion
copies exact encoded RGBA bytes, including corner and transparent colors. Other
gaps are zeroed. Defaults retain whole-image, unrotated packing. Optional rotation
considers both orientations in the bounded shelf search, sorting by longer/shorter
side; tie rules remain deterministic. It does not claim optimality. Optional
transparent trimming scans exact alpha-zero pixels and retains one available
transparent ring for linear filtering. Every retained RGBA texel is copied exactly,
including the inverse clockwise pixel mapping and physical-edge extrusion. Fully
transparent images retain one real top-left texel and their full original canvas.
Input dimensions may reach16384 when trimming is enabled, within the same aggregate
decoded byte budget; output PNG limits remain unchanged. No atlas-aware mip
generation is performed.

The parent snapshots admitted base pixels. Fixed private worker operation
`forge.texture.atlas-source` uses the existing supervised asset worker, bounded
transport/cancellation and native PNG encoder. It has no importer registry entry
or persistent asset identity. Its revision covers its FORGE sources, settings
contract, pinned encoder/toolchain seed and configuration. The parent checks the
returned layout against the same shared original-image scan and layout owner,
and checks the bounded PNG header; the
ordinary PNG importer subsequently performs complete decode/cook admission.

SourceCopy owns immutable generated bytes and creates one new Assets directory
atomically under the existing writer lease/path rules. The review seals the PNG
bytes, ordered frame/input metadata, settings, destination and captured catalog
digest. Rechecks before staging and before final rename reject a changed input
catalog. No Texture ID exists until normal PNG publication allocates it. Results
return that actual ID. This is a one-shot normal PNG source, not a new atlas asset,
source registry, repack recipe or automatic original-source invalidation system.
Normal future PNG reimports preserve that PNG's existing identity.

The native review displays a wire layout and ordered source table, including
logical canvas/offset/turn details for transformed entries. A bounded128KiB
clipboard carries only format/version, Texture ID, dimensions and frame values.
New layout records use version2 with full metadata; version1 four-field clipboard
records remain readable. Worker settings accept optional trim_transparent and
allow_rotation booleans, both false by default.
Paste verifies the assigned published image identity/dimensions and the complete
frame bank. Existing property.set_many atomically replaces frame_regions, frame0
and region_enabled=false with Scene history/prefab intent. Prefab source editing
uses its existing draft/publication owner. MCP returns the same fields and clients
apply them through existing reflected property batches; no agent-only Sprite path.

Original Gallery94 contains the generated PNG, ordinary Sprite Material and
five Sprite comparisons. No C++ compiler or new SDK/ABI value is needed. The
installed SpriteRenderer/SpriteFrameRegion APIs already consume these values.
Local source validation is separate from Windows/full Main/physical acceptance.
