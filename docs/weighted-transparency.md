# Weighted Mesh transparency

`rendering.weighted_transparency` is an optional strict boolean in the existing
Scene rendering object. Absence defaults to false. The ordinary rendering setter,
Scene history/Save, automation discovery and MCP command binding share this field;
no new component, resource identity, queue or material ownership is introduced.
The authored Scene setting is locked during Play. See the
[task guide](../manual/editor/transparency.md).

## View admission and ordering

The existing sorted part queue remains authoritative. Before drawing, the renderer
prepares the complete weighted candidate for every Blend Mesh and checks the actual
HDR/depth targets. Eligible surfaces test depth, do not write depth, use ordinary
Mesh scope/order and have a current surface cook when custom. Any visible Sprite or
Tile Map, transparent particle/ordered source, transmission surface, incompatible
material, unsupported format, budget or preparation failure retains ordinary sorted
rendering for the whole view. The warning identifies `rendering.weighted_transparency`.
No mixed partially weighted ordering is admitted. A view without Blend work uses
ordinary rendering and releases unused attachments.

Opaque/masked geometry, temporal accumulation and opaque surface effects retain
their existing owners. Those effects resolve before the transparent stage. Weighted
Mesh draws depth-test against the same opaque D32 surface and do not write it.
The weighted result composites into linear HDR before later display operations.
Transmission remains on the existing sorted HDR-background transport path.

## Accumulation and resolve

The transient owner allocates full-size single-sample RGBA32F and RG32F attachments,
24 bytes per target pixel, with a 512 MiB per-owner attachment payload limit. This
is neither a device-wide VRAM bound nor measured physical performance. Custom
viewports retain absolute target-pixel addressing; only their rectangle composites.
Candidate allocation/pipeline/bindings publish together after validation.

Both attachments use identical ONE/ONE additive RGB/alpha blending. Independent
blend feature enablement is unnecessary. For straight fragment color C and clamped
alpha a, the first attachment accumulates `(C.rgb*a*w, a*w)`. The bounded radial
weight is `max(1/256, 1/(1+dot(p,p)))`, where p is camera-relative position times
0.1 with each coordinate clamped to [-16,16]. It favors nearby fragments without
squaring unbounded world coordinates.

The second attachment adds `-log(1-a)` for a<1 and a separate exact-alpha-one
presence count. Small alpha uses the expansion of `2*atanh(a/(2-a))` to avoid
subtraction cancellation. Resolve recovers coverage with a stable small-argument
expansion of `1-exp(-sum)`; a positive alpha-one count gives exact full coverage.
An empty alpha-weight sum contributes zero. Weighted mean RGB times coverage uses
premultiplied ONE/INV_SRC_ALPHA blending into existing HDR RGB and alpha. Finite
fragment/resolve guards retain the renderer's diagnostic rejection behavior.

This is approximate color transport, not exact layered compositing, refraction,
per-pixel fragment storage, or a claim of bitwise order invariance. Floating-point
addition introduces rounding. Opaque-looking transparent layers still participate
in the weighted mean rather than physically hiding every deeper layer.

## Material and shader capability

Built-in Unlit and physical Mesh programs wrap their existing final color helper
after lighting/fog. Material values, textures, decals, geometry/deformation and
resource leases remain with the existing draw/bundle owners. Wrapper capability9
adds `surface_weighted_color` and `surface_area_weighted_color`, bringing the archive
bound to19 stages. The public authored surface interface is unchanged. Versions1–8
remain readable with their original bytes; older custom cooks retain sorted drawing
until Shader and dependent Materials are rebuilt. Logical asset IDs do not migrate.

Empty projected-color defaults use an immutable buffer. Dynamic DISCARD uploads
are bound only for the draw whose nonempty values were uploaded, and clearing
restores immutable defaults across all winding bindings. Frame-local upload memory
is not retained as a later frame's empty state.

## Validation scope

Focused fixtures exercise strict Scene/history/discovery/MCP persistence, shader
archive compatibility, actual Inspector controls, shared Mesh pixel composition,
lighting variants, boundary/fallback cases and source-free Gallery relocation.
Local native evidence uses software Vulkan with independent blending disabled.
Generated physical Graph source compilation is distinct from Windows cooked shader
execution. Windows/MSVC/FXC/cooked D3D12, full editor, physical GPU/input/DPI and
standalone executable acceptance remain separately queued. No physical performance
or clean native shutdown claim follows from these fixtures.
