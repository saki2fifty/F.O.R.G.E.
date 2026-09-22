# Cooked mesh data

`forge_mesh` supplies validated CPU mesh
artifacts; `forge_gltf_native` converts the admitted pinned Diligent glTF document
into them. `forge_mesh_resources` loads an immutable cooked revision through the
[typed resource pool](runtime-resources.md). These targets build without ImGui,
a window or a graphics device.

## Representation

`MeshData` contains ordered LODs, material-slot count and named morph defaults.
Each LOD contains parts with its topology, vertex streams, indices, material slot,
morph streams and local bounds. LOD projected-size thresholds decrease from1;
zero is permitted for the final threshold. Shared Scene/Game rendering selects
LOD by projected bounds diameter divided by viewport height. LODs keep stable
logical material bindings; scene overrides do not depend on physical slot order.

Streams carry a semantic, component width and either float32 or exact uint32
scalars. The current cooked representation retains positions, normals, tangent
handedness, UV/color sets, joints/weights and application attributes. Integer
values never pass through float conversion. Point, line and triangle lists remain
explicit. Imported strips/fans are normalized by the existing native adapter
before cooking. Source material index i becomes slot i+1; slot0 is the engine
default, with logical material bindings supplied by the model container.

Morph deltas remain separate from base streams. Bounds describe the base geometry;
an animated/deformed consumer must compute its effective bounds. Skeleton identity and joint-layout compatibility remain binding
admission responsibilities. Prepared draw palettes carry at most256 entries and
normalized four-influence streams; source influence processing is explicit.
Unprepared positive float weights may exceed1 and require normalization before
a skin draw; this follows the source specification rather than clamping values. A successfully decoded CPU mesh alone does not establish
GPU skinning compatibility.

This is compiled geometry for runtime consumers. Future modeling topology and
editable vertex/edge/face identities have their own authored-document semantics.
Node transforms remain separate; negative/zero node scale is never baked away by
the mesh cook adapter.

## Artifact versions

The24-byte header contains eight-byte `FRGMESH\0` magic, four little-endian uint32
fields (version, JSON metadata length, scalar payload length, reserved zero), then
bounded JSON metadata and a contiguous little-endian scalar payload. Versions1/2
remain readable: version1 has ordinary meshes and version2 adds prepared skin
palettes; both use implicit uint32 indices. Version3 writes an explicit index type
per part: uint16 when the maximum referenced index fits, otherwise uint32. An
empty list uses `none`, meaning sequential nonindexed vertices. Earlier readers
reject version3 rather than misinterpreting its byte spans.

CPU geometry keeps exact uint32 working indices. GPU realization independently
chooses Diligent VT_UINT16/VT_UINT32 or nonindexed Draw; it does not assume a native
descriptor or graphics API index layout. List topologies have no restart sentinel.
Unused high-numbered vertices do not force wide indices. The native test exercises
65535/65536, actual buffer readback and equivalent 16-bit/32-bit/nonindexed pixels.
Decoded uint32 allocation remains bounded even when cooked indices occupy16bits.
No native pointer, compiler structure layout or host endianness is serialized.

Every stream/index span must begin exactly at the next payload byte. Admission
rejects overlaps, holes, out-of-range counts, truncation, trailing bytes, unsupported
versions/scalar encodings, duplicate semantics, inconsistent vertex counts,
non-finite values, invalid topology/index/material bounds and inconsistent LOD/morph
metadata. Known normal/tangent directions must be normalized and tangent handedness
must be±1. Local bounds must match the admitted positions. JSON admission rejects
duplicate keys and excessive nesting/events.

Default per-mesh limits are512MiB scalar bytes,8MiB metadata,16million vertices,
64million indices,65,536 parts,64 streams per part/target,256 morph targets and16LODs.
Aggregate counts include every LOD. Cache/importer profiles may impose smaller
limits. Encoding sorts semantic streams and normalizes scalar negative zero;
equivalent input stream ordering produces the same artifact bytes. A cooked file
also has the immutable artifact manifest/content digest checked by its provider.

`byte_size()` reports scalar storage. `resident_bytes()` estimates retained CPU
allocation using container/string capacities and structure sizes. It omits unknown
allocator headers and conservatively includes short-string capacity in addition to
its inline structure storage. Neither quantity is process RSS or GPU memory.

## Validation and consumers

Tests cover canonical round trips, exact integers, LOD/morph preservation, topology,
every truncated prefix, corrupt lengths/counts/offsets, NaNs and bounded rejection.
The unmodified official Khronos NegativeScaleTest is decoded, cooked and reloaded,
with geometry/hierarchy/parity checks. License and immutable provenance are adjacent
to the sample. Strict sanitizer and platform outcomes are in the daily changelog.

The model publisher, built-in primitive provider, typed CPU resources, Diligent
GPU realization, material/skin bindings, Content inspection and cooked-content
packaging consume this representation. See [runtime resources](runtime-resources.md)
and [rendering](rendering-foundation.md) for the adoption and device contracts.
Final clean/package acceptance is tracked separately from CPU representation tests.


## Prepared skin draws

Each prepared `MeshPart::joint_palette` maps a draw-local joint index to an ordered
source skin-joint index. It contains1–256 unique entries. `JOINTS_0` indexes that
palette; `WEIGHTS_0` contains four finite nonnegative weights per vertex with a
normalized sum. Additional influence sets, duplicate positive joints and out-of-
palette indices reject. The actual Skeleton AssetId, mapping to admitted Ozz joint
order and inverse-bind matrices belong to the model skin binding, allowing one Mesh
to be used by several skins without baking asset identity into its geometry.

Cooking validates every skin using a mesh. The existing explicit Reject or
ReduceToFour policy handles more than four positive influences; reduction keeps
the strongest weights with deterministic joint-index tie breaking and reports
which vertices changed. It never silently drops influences. Normal/tangent
preparation, exact welding and vertex-fetch remapping retain the prepared streams
and palette. Unused skin attributes on a source mesh with no skin binding remain
unprepared data; the GPU skin consumer requires a prepared palette.

The model cooker publishes complete animated families using this representation.
The Model import document exposes the explicit influence policy; unsupported
counts or bindings reject the candidate while preserving the previous family.

## Runtime material bindings

Cooked physical material ordinals are local to one mesh revision. The runtime
[resource provider](runtime-resources.md) pairs admitted geometry with stable
logical binding tokens and typed material references. It includes used slots in
all LODs, supports sparse overrides and diagnoses removed bindings. Reordering or
renaming source materials does not retarget an override. The MeshRenderer Inspector and shared GPU draw preparation consume these same
bindings. Removed/invalid slot selections diagnose rather than silently retarget.

## Imported authored LODs

The model worker admits the geometry-only node subset of
[MSFT_lod at the reviewed glTF revision](https://github.com/KhronosGroup/glTF/blob/c18432787e6d545a1218c1926ccdcfaffd4c116b/extensions/2.0/Vendor/MSFT_lod/README.md).
The extension's node indices are candidate-local source addresses, never persistent
asset identities. Each owning node gets a separate combined Mesh member through
normal content/usage evidence and subasset reconciliation. Unrelated nodes reusing
the original mesh retain their original geometry and policy.

The owner and alternatives must be mesh leaves with identical local transforms,
skin bindings and node morph weights, without independent node animation. Lower
alternatives must have no structural parent, camera or light. Their visibility/selectability
must agree with the owner. Morph target names/order/defaults must match. A skin
uses the same source skin across levels; each part keeps its own validated draw
palette. Material slots retain their logical material AssetIds across levels.
All level bytes/counts, materials and required UV streams are validated together.
For a source without authored scenes, the compiled default scene excludes lower
LOD alternatives from its root list. They remain source nodes, not extra placed
objects. Authored scene selections are preserved.

Up to16 authored levels are supported. Optional MSFT_screencoverage hints supply
transition thresholds; absent hints use 1/2,1/4,1/8... projected size. The final
optional disappearance hint is deliberately not used: the lowest level remains
visible, and import diagnostics report this choice. No geometry is simplified.
Material-level LOD, nested/subtree node replacement, and material variants on an
LOD mesh currently reject with an explicit diagnostic before publication. Variants
on unrelated meshes remain admitted. This is a documented extension subset, not
full MSFT_lod node/material replacement or hierarchical scene streaming.

The Mesh document exposes each published level's threshold, part/vertex/index/
primitive/material counts and local base bounds. Its preview and Scene/Game draws
use the existing projected-size selection, complete resource candidate adoption,
posed bounds and material overrides. Inspection metadata is prepared off the UI
thread and owns no second geometry/resource authority. A rejected reimport leaves
the previous complete family and persistent IDs selected.
