# World sprites

## Development status

This family is in development. Typed Scene authoring, shared world rendering,
framing/geometric picking and bounded Sprite instancing have focused tests. The
complete editor workflow validation, animation validation, Gallery content and exported-game acceptance
are not yet complete. This document records the implementation contract; it does
not describe a finished feature in the downloadable editor.

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
tint and order through the existing live-value panel. Stop restores authored
values; explicit Keep values review uses the existing mechanism. Material/grid
resource changes are outside that live whitelist.

Atlas frame animation extends the existing Timeline property owner with
`forge.sprite_renderer.frame`. It holds whole frames between keys and rejects
curve/tangent settings. See [transform animation](transform-animation.md#sprite-frame-binding-development).
Full editor workflow and animation acceptance remain in progress.

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

Windows Sprite render cases are registered but have not executed. Complete editor
workflow/visual review, discrete frame animation, representative performance
measurements and relocated source-free export remain required before completion.
