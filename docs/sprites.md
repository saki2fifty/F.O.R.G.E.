# World sprites

## Development status

This family is in development. Typed Scene authoring and a private native draw
profile have focused tests. Scene-to-renderer integration, the complete editor
workflow, animation, Gallery content and exported-game acceptance are not yet
complete. This document records the implementation contract; it does not describe
a finished feature in the downloadable editor.

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

`order` is intended as a numeric priority in the existing draw queue. Transparent
camera depth remains authoritative before equal-depth priority; higher priorities
draw later. Renderer and picking integration of this field is still pending.

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

## Persistence and SDK

The new component uses the existing Scene/Prefab formats and unknown-extension
preservation. No existing component is migrated. Its public C++ definition changes
the exact SDK fingerprint, so native modules need a matching SDK rebuild. Native
ABI1 remains unchanged. Recovery is still an exact-build, private runtime domain.

## Validation boundary

Focused tests cover grid partitions/bounds/overflow, CPU material/texture admission,
typed component authoring/history/reopen/reference traversal, and direct native
pixel/bundle behavior. Software Vulkan pixels have executed; Windows Sprite render
cases are registered but have not executed. Whole engine/editor integration,
discrete frame animation, resource lifetime/scale measurements and relocated
source-free export remain required before this family is complete.
