# Cooked mesh data

Phase7 implementation is in progress. `forge_mesh` supplies validated CPU mesh
artifacts; `forge_gltf_native` converts the admitted pinned Diligent glTF document
into them. `forge_mesh_resources` loads an immutable cooked revision through the
[typed resource pool](runtime-resources.md). These targets build without ImGui,
a window or a graphics device.

## Representation

`MeshData` contains ordered LODs, material-slot count and named morph defaults.
Each LOD contains parts with its topology, vertex streams, indices, material slot,
morph streams and local bounds. LOD projected-size thresholds decrease from1;
zero is permitted for the final threshold. Runtime LOD selection remains to be
connected to the production renderer.

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
bounded JSON metadata and a contiguous little-endian scalar payload. Version1
retains ordinary/unprepared CPU meshes. Version2 adds explicit prepared skin
palettes; an old version1 reader rejects these artifacts. No native
pointer, compiler structure layout or host endianness is serialized.

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

## Evidence and remaining consumers

Tests cover canonical round trips, exact integers, LOD/morph preservation, topology,
every truncated prefix, corrupt lengths/counts/offsets, NaNs and bounded rejection.
The unmodified official Khronos NegativeScaleTest is decoded, cooked and reloaded,
with geometry/hierarchy/parity checks. License and immutable provenance are adjacent
to the sample. Strict sanitizer and platform outcomes are in the daily changelog.

The CPU artifact is ready for subsequent provider integration. Model-container
publication, built-in primitive migration, GPU realization, full material/skin
bindings, Content workflows and packaged runtime integration are still being built.


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
unprepared data; a future GPU skin consumer must require a prepared palette.

The private model cooker now supports this representation. Whole-model animated
publication and the corresponding user-facing import setting remain integration
work; no unused setting is exposed by the current static model recipe.
