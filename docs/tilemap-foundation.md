# Tile Set and Tile Map source foundation

These are internal source codecs and document tests. They are not registered
importers or user-facing Tile Set/Tile Map tools. There is no completed painting,
rendering, C++ runtime or export support from this foundation alone. The first
complete workflow must integrate those consumers and the Feature Gallery before
claiming a working tilemap feature.

## Ownership

`TileSetSource` keeps its JSON document authoritative. `TileMapValue` keeps
immutable root metadata and immutable chunk JSON authoritative; its typed metadata
and cell arrays are admitted read-only projections. Copying a map shares chunks.
A paint candidate copies only touched chunks and never changes its baseline.

Both document specializations use `AssetSourceDocument` for the project writer,
revision checks, Undo/Redo, external byte baseline, identity checks and atomic Save.
There is no second document, history or identity manager. Painting changes a draft;
Save changes source bytes. Import/publication remains a separate integration.

Unknown fields survive at root, image, image grid, tile definition, map layer,
chunk and occupied cell levels. Painting an occupied cell updates its known fields
and retains its other fields. Erase removes that cell. A plain empty chunk is
pruned after its last occupied cell is erased; an empty chunk with unknown metadata
is retained. Invalid candidates leave the adopted value unchanged.

## Internal Tile Set profile

The private JSON discriminator is `forge.tile-set`, version 1, with a UUID
`asset_id`, monotonic uint64 `next_id`, `images` and `tiles` arrays. Zero is reserved
in the shared image/tile local-key space. Every entry key is unique and below
`next_id`; names are display values. Edits cannot reuse retired keys, move the
allocator backward or change a key from an image to a tile. Undo preserves the
allocation high-water mark. Full uint64 values never pass through a floating point
conversion.

An image entry has `id`, `name`, a non-null ordinary Material asset reference and
`grid` (`columns`, `rows`, `margin_x`, `margin_y`, `spacing_x`, `spacing_y`). A tile
definition has `id`, `name`, `image` and an integer `frame`. It must reference an
existing image and a frame within that grid. Grid math reuses `sprite_grid_region`;
sheet frames start at the image's top left and proceed across each row.

Admission limits are 16 images, 4096 tile definitions, 2MiB source bytes,
100000 JSON events and depth 16. Names contain 1–128 UTF-8 bytes without ASCII
controls. Material compatibility, published image readiness and actual texture
extents require the later resource/publication integration; a structurally valid
source is not evidence that its images can render.

### Cooked Tile Set data

Source and cooked data use the same row admission for scoped IDs, Material
references, sheet layouts and frame ranges. The source retains array order,
unknown fields and its allocation high-water mark. Cooking sorts image/tile rows
by numeric key and omits unknown fields and allocator metadata. Reordering source
rows or advancing the allocator alone does not change cooked runtime bytes.

The cooked envelope uses eight-byte magic `FTILESET`, version 1, at most 2MiB of
metadata and no binary payload. Root fields are exactly `asset_id`, `images` and
`tiles`; their known row/grid fields match the source value profile. Extra cooked
fields reject. The decoder checks the expected root UUID and the same combined
image/tile key space, frame references and limits as source admission.

`TileSetData` retains shared immutable typed rows sorted for local-key lookup.
Copies share that storage. Material dependency enumeration deduplicates Material
references without collapsing separate image-entry IDs. A cooked-only consumer
follows a map cell's tile key through the Tile Set definition and image entry to
its typed Material reference without linking map/set source, cooker or document
code. This checks values and linking, not image publication or GPU readiness.

The 16-image/4096-definition fixture, with maximum-length tile names, cooks to
706285 bytes. Its retained allocation estimate is 759440 bytes in Linux GCC12;
this is not a process RSS or cross-platform allocator guarantee. Material profile
and real image-extent admission remain part of the pending asset integration.

## Internal Tile Map profile

The private discriminator is `forge.tile-map`, version 1. Root metadata contains
UUID `asset_id` and `tile_set`, monotonic uint64 `next_id`, two-axis `cell_size` in
meters and `layers`. Each layer has an allocated local uint64 `id`, `name`, boolean
`visible` and signed int32 `order`. Layer names and local keys follow the same
display/identity distinction and high-water behavior as Tile Sets. A painted map
cannot change its Tile Set identity without first clearing its occupied cells.
Resolving tile keys against a published Tile Set is a later admission boundary.

The first internal limits are 8 layers, 1024 chunks, 262144 occupied cells,
4096 ordered cell writes per candidate, 64MiB source bytes, 4000000 JSON events
and depth 16. Immutable metadata/chunks cache their admitted event counts; each
candidate checks the combined source budget without scanning unchanged chunks.
Cell sizes must be
finite and between 0.000001 and 1000000 meters, inclusive. These limits bound the
source profile; they are not a renderer, frame-time, total-memory or SDK capacity
promise. Save also checks the actual formatted byte size.

`chunks` is an object keyed by the canonical decimal string `layer:x:y`.
For example, `1:-1:0` is layer 1, chunk X -1, chunk Y 0. Leading zeros, negative
zero, missing fields, overflow and chunks outside the signed cell range reject.
Each chunk holds exactly 1024 `cells` slots. A slot is null or an object containing
an exact positive uint64 `tile` and integer `flags` in 0–7. These transform flags
are only stored/validated here; rendering their transforms remains unimplemented.

Each cell's identity is its layer key and signed int32 local X/Y address. Cells
are not authored entities and receive no UUIDs. Chunks are derived storage rather
than additional catalog identities. A cell covers the half-open local XY area
`[x,x+1) × [y,y+1)` before multiplication by cell size. Y increases upward.
The shared helpers use floor division into 32×32 chunks and row-major slots:

| Cell X/Y | Chunk X/Y | Slot |
| --- | --- | --- |
| -1, -1 | -1, -1 | 1023 |
| -32, -32 | -1, -1 | 0 |
| -33, 32 | -2, 1 | 31 |

Splitting and rejoining preserve the entire int32 cell range. Rejoining arbitrary
out-of-range chunk coordinates rejects using int64 intermediate arithmetic.
Local meter-to-cell conversion uses double precision, finite positive cell sizes
and floor; non-finite or out-of-range results reject before integer conversion.
This adds no world-origin system and makes no claim about completed transformed
picking or GPU precision.

The existing 64-entry/8MiB history policy remains. Map history weight counts
encoded root metadata, snapshot references and old chunks replaced by an edit.
It is an encoded retention policy, not a process RSS cap. The immutable chunk
representation keeps unchanged data shared across saved values and history.

## Current evidence and remaining work

### Read-only CPU presentation view

`TileMapValue::for_each_chunk` exposes borrowed immutable cell spans to internal
engine consumers. Unchanged chunks retain the same cell storage after a paint
candidate; a changed chunk has separate storage. This is not an SDK API returning
resource pointers.

`visit_visible_tile_chunks` filters empty chunks and hidden layers, computes
double-precision world bounds through the existing affine bounds owner, and uses
the existing camera-frustum test. Multiple cameras form a union with one callback
per visible chunk. No cameras produce no callbacks. Chunk bounds preserve full
signed cell addresses, including the exclusive upper edge at 2147483648, without
passing through float mesh bounds. Tests cover reflection, shear, nonuniform
scale, negative chunks and a camera near a world origin of 1000000000000 meters.

This is a CPU view and culling foundation. It does not yet draw tiles on a GPU,
transport runtime changes to Editor Play or provide a runtime query/edit API.
The existing Sprite draw owner is keyed by entity and draw scope; emitting many
ordinary Sprites under one map entity would share that owner incorrectly. Tile
rendering must integrate its image/chunk instances with that existing owner and
the shared render queue, without creating authored entities for cells.

### Internal cooked map data

The map cooker projects admitted cell/layer values into the existing cooked
envelope. The eight-byte magic is `FTILEMAP`, envelope version 1. Header scalars,
chunk coordinates and cell records are little-endian. Metadata is bounded to
64KiB and contains only `asset_id`, `tile_set`, `cell_size`, `layers`, `chunk_count`
and `cell_count`. Layer records contain only their stable ID, name, visibility and
order. Authoring unknown fields, allocation high-water marks and empty chunks
are omitted. Unknown cooked metadata fields reject.

Each occupied chunk uses this payload:

| Field | Bytes |
| --- | --- |
| Layer key, unsigned | 8 |
| Chunk X and Y, signed int32 bit patterns | 4 each |
| Occupied cell count | 4 |
| Each occupied cell: slot, tile key, flags | 4 + 8 + 4 |

Chunks sort by numeric layer/X/Y key and cell slots strictly increase. The decoder
checks expected root identity, referenced layer keys, signed coordinate bounds,
exact lengths/counts, positive tile keys, supported flag values and strict ordering.
Truncation, duplicate keys/slots, extra bytes and unsupported headers reject.
The payload cap is `1024×20 + 262144×16 = 4214784` bytes.

`TileMapData` retains typed immutable chunks and copied layer metadata. Its decoder
and camera-view libraries have no dependency on the map source/cooker/document
libraries. The source-side view is a separate adapter to the same culling helper.
A runtime-only test links the cooked decoder/view and consumes a hand-authored
binary fixture; symbol/link checks verify that map authoring code is absent.

The maximum-profile test uses all 1024 chunks and 262144 occupied cells. Its
complete cooked artifact is 4215032 bytes. In the Linux GCC12 configuration its
retained allocation estimate is 16883912 bytes. That estimate includes typed arrays, container capacity and
conservative node/control storage; it is not a measured process RSS or performance
guarantee. Cooking/publication is not yet wired into the asset pipeline or export.

Focused source tests cover malformed inputs, duplicate JSON keys, finite/range
limits, negative/far addresses, exact uint64 keys, unknown data, detached paint,
erase, ordered writes, layer isolation, allocation history and Save/reopen.
The actual document owner also exercises a 262144-cell map with 16 paint strokes
and complete Undo/Redo. These tests do not replace the future native-renderer,
installed SDK, editor interaction and moved-game gates.

The remaining complete feature includes typed publication/dependency/cook closure,
ordinary resource ownership, GPU presentation consuming this immutable view, bounded C++ runtime
queries/edits, palette/painting/picking, source replacement behavior and original
editable Gallery content. No autotiling, animated tiles, 2D collision or streaming
is implied by this source foundation.
