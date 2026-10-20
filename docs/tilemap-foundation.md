# Tile Set and Tile Map source foundation

This development source includes the source, cooked-data, asset pipeline, native
renderer and central Tile Set/Tile Map document tools. Focused Linux checks cover
the pipeline and renderer; headless ImGui interaction checks cover the document
controls at three interface scales. The complete Editor Play projection, original
Feature Gallery scene and moved standalone game gate remain unfinished. This is
not yet a completed delivered tilemap workflow or Windows acceptance claim.

## Source documents (development)

Content creates and opens `.tileset.json` and `.tilemap.json` in central document
tabs. Tile Set images reference ordinary Materials. Grid dimensions and selected
tile definitions use the Inspector; definition keys remain local to their Set.
Each document uses the existing source writer, revision, history and publication
owners. Save / Publish cooks through the ordinary worker; failures retain the last
working publication. Scene Undo and source-document Undo remain separate.

The Map document selects a layer, tile and orientation, then paints or erases in
an isolated XY canvas. Release commits a complete bounded stroke as one Undo
step. Escape, loss of focus, close and locking cancel its temporary candidate.
Mouse samples interpolate across cells; a full stroke retains its last accepted
segment and asks the user to release. Status and loading messages occupy fixed
space so they cannot move the grid beneath a captured pointer. Middle mouse pans
and the wheel zooms; painting captures navigation. Map/layer settings and selected
cells use the Inspector. A missing Tile Set still permits incremental erase and
Undo without rebuilding every cell on each pointer sample.

Published maps can be placed through the ordinary Content action/Scene drop path
or assigned to a Tile Map Renderer. `render.tile_map` uses the existing entity
recipe catalog and reflected defaults. Placement commits one Scene Undo step;
neither the canvas nor placement creates an entity for each occupied cell.

## Scene and instance integration (development)

The development `forge.tile_map_renderer` component stores a typed map reference,
enabled/visible flags, a uint32 camera-layer mask, signed int32 ordering offset and
linear RGBA tint in [0,1]. The ordinary Scene owns entity identity, transforms,
history, prefab composition and unknown source fields. Cell dimensions, layer
definitions and cells remain in the referenced map; no per-cell entities are
created. Native Meta checks the actual component member offsets. Render extraction
produces a typed map item with the ordinary visibility/selectability policy and
double affine transform; the shared native renderer consumes its admitted cells.

Private runtime instance components own immutable cell snapshots and retained
Map/Tile Set leases. Their state is not inherited, reflected into Scene source or
stored in a second map database. Independent entities can share an immutable asset
while changing their own snapshots. Initial asset adoption and unedited replacement
can progress at the existing presentation/resource boundary while paused.

Native and exact-SDK cell requests copy at most4096 ordered writes, bind a realized entity
and adopted generation, and validate the whole batch. The queue bounds observations
to64/module and256/world and pending copied cells to4MiB. Applied observations
release their copied input; releasing a pending observation cancels it. Publication
occurs at the fixed tick after resource synchronization and before gameplay. Paused
presentation never publishes cell requests. Requests apply in accepted token order;
each validates again against the then-current instance, and a failure changes no
cells. A removed/readded component or replaced binding invalidates older requests.

An edited instance retains its adopted binding when a new revision of the same
map is published. It reports requested/adopted revisions separately; a fixed-boundary
reset explicitly discards runtime edits and adopts current source assets. Assigning
a different logical map root is also an explicit replacement. Stop destroys the
world's transient state; this is not an automatic save or C++ recovery format.

Focused Linux tests validate these scene/instance paths, including Scene history,
prefabs, real fixed/paused requests, source replacement/reset, removal and bounded
queues. Scoped ASan/UBSan/leak checks and matching shared-library SDK regressions
pass. A maximum-profile allocation probe validates4096 writes across1024chunks/
262144cells: preflight requests335872heap bytes, while constructing the changed
snapshot requests17203270bytes in Linux GCC12. These are requested allocation
totals, not peak/RSS/GPU memory or frame-time evidence.
Play transport projection and the complete exported-game workflow remain unfinished.

Scene preparation now progresses the existing runtime map synchronization and
waits for the requested map binding before freezing a candidate. This does not
tick gameplay or publish queued cell edits. A retained older root cannot satisfy
readiness for a newly requested root.

The private copied-presentation codec admits Map/Set headers through their existing
cooked decoders. Pages contain at most four chunks and 256KiB; unchanged shared
arrays produce no delta. Shrinking chunks precede growing chunks so a paged move
at capacity remains valid. Full maximum-profile transfers, wide keys, malformed
packets and scoped sanitizer checks pass locally. The codec alone does not wire
either Editor Play process path or publish a runtime frame.

The source-side transfer adapter has focused tests for immutable observations,
frozen pages, matching cursors/tokens, cancellation and acknowledged baselines.
It retains at most 16 baselines and 64MiB of copied values; shared arrays count
once. Retained history copies pure values and does not pin asset-provider leases.
A retained unedited baseline with matching adopted Map/Set revisions can also
supply another actor's initial delta. Eviction falls back to a full paged transfer.
The read-only world bridge uses existing synchronized instances and never starts
loads or ticks. Neither runtime process nor the editor receiver is wired yet.

The in-process graphical host now binds typed map presentation values to the
same immutable snapshots read by native gameplay. This shares cell storage and
admitted definition rows; it does not put cell arrays into `effective_scene` or
copy the map each frame. An older captured frame keeps its earlier cells. The
snapshot retains pure definition values independently of retiring resource-lease
storage, so detached values remain valid after provider shutdown. Pending asset
loads are distinguished from terminal admission failures. `GamePresentation`
passes this typed view through the existing `FrameRenderer`. The separate Editor
Play process projection remains unfinished.

## Copied C++ cell interface (development)

The exact SDK appends CPU map callbacks to the existing host table. Its changed
table size and fingerprint require rebuilding modules with the matching SDK;
ABI1 does not make an older table compatible. No private asset or snapshot pointer
crosses the boundary. The header-only `sdk::Client` provides:

- `tile_map`: copied requested/adopted map and Tile Set UUIDs, revision hashes,
  readiness/current/edited flags, adopted generation, cell revision, cell dimensions,
  layer count and diagnostic. A valid actor waiting for synchronization returns
  success with `ready == 0`; reading never starts a load or creates an instance.
- `tile_layer`: enumerate a layer by index, returning its stable scoped key,
  name, visibility and signed ordering. The index is not an identity.
- `tile_cells`: copy 1–4096 cell values at scoped layer keys and signed int32 local
  addresses. Tile zero and flags zero mean an empty cell. Every address is checked
  before any successful result is copied; a bad layer rejects the entire query.
- `write_tiles` and `reset_tiles`: submit bounded copied commands at a fixed tick.
  Their nonzero token observes Pending, Applied or Failed. Inspect/release through
  `tile_edit`/`release_tile_edit`; releasing Pending cancels it, while releasing
  Applied retains the already published cells.

Queries target the existing Scene/entity UUID pair and require the inspected
adopted generation for layer/cell access. Results are 1 success, 0 unavailable or
not ready, -1 invalid arguments/context and -2 stale generation. Valid-size outputs
are cleared on failure; invalid-size outputs and invalid-count arrays are untouched.
Output structure versions are 1. Cell arrays have a maximum of 4096 entries;
write flags are 0–7 and reserved fields must be zero. Diagnostics are bounded to
1023 bytes plus terminator. These are trusted native callbacks, not pointer-safety
checks for arbitrary C++ code.

Intrinsic `TileMaps` capability discovery permits owner-thread runtime reads and
observation release while paused. `TileMapEdits` additionally requires the fixed
tick context. Both require the module's existing Resources permission and live
provider; they are not new descriptor permission bits or GPU readiness. Stop
releases all owned edit observations and cancels queued commands. A token belongs
to its module and world and must never be persisted. The existing queue performs
its ordinary generation/entity/definition recheck at publication.

Focused Linux tests pass through an actually loaded matching module and an
installed-only compiled module, including relocation. An ordinary saved Scene
roots Map → Tile Set → Material closure; a moved package realizes the cells after
authored tile sources are removed. Tests cover paused reads, fixed submissions,
copied inputs, atomic rejection, two-instance isolation, reset generations,
quotas, owner-thread/module checks and Stop cancellation. Scoped ASan/UBSan/leak
checks instrument the bridge, instance queue, module and fixture; other linked
CPU libraries and the child import worker remain normal. The shared Windows CI
gate includes these new consumers; its execution and GPU/editor acceptance are
still pending. This does not complete the visual tilemap workflow.

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
controls. Source admission checks structure; publication additionally checks
Material compatibility, published image readiness and actual texture extents
through the existing Sprite image candidate. Neither check promises GPU readiness.

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
this is not a process RSS or cross-platform allocator guarantee. Publication
separately admits the Material profile and actual selected image extent.

## Internal Tile Map profile

The private discriminator is `forge.tile-map`, version 1. Root metadata contains
UUID `asset_id` and `tile_set`, monotonic uint64 `next_id`, two-axis `cell_size` in
meters and `layers`. Each layer has an allocated local uint64 `id`, `name`, boolean
`visible` and signed int32 `order`. Layer names and local keys follow the same
display/identity distinction and high-water behavior as Tile Sets. A painted map
cannot change its Tile Set identity without first clearing its occupied cells.
Import discovery and cooking resolve occupied tile keys against the selected
published Tile Set; the source parser alone does not load that dependency.

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
an exact positive uint64 `tile` and integer `flags` in 0–7. Bit0 mirrors the
frame's horizontal texture axis; bit1 mirrors its vertical texture axis; bit2
transposes the frame-local UV axes before those mirrors. These eight combinations
include quarter-turns and reflections, without changing the cell's geometry.
The numeric adapter lowers them to the existing Sprite atlas mapping. Its simple
image profile uses one previously unused instance lane for transpose, preserving
the instance stride. Native map scene drawing uses this same profile.

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

### Ordinary asset publication and loading

Value-only `TileSetAsset`/`TileMapAsset` tags belong to the common AssetRef contract
and use the existing root AssetCatalog and ResourcePool contracts. The copied C++ gameplay interface is described above. `tile_asset_import_registry` supplies the two source
importers to `AssetImportService`; no parallel catalog, dependency graph, history
or loaded-handle database is introduced. Source bytes and selected dependencies
are recaptured before cooking, and stale or failed candidates retain last-good
publication.

Tile Set discovery uses the existing complete Quad/Material/Texture candidate and
`sprite_source_texture` admission, then checks sheet division against the actual
selected texture extent. Custom surface shaders, fallback resources and texture
profiles outside the current Sprite interpretation reject. Tile Map discovery and
cooking verify all occupied tile keys against its selected Tile Set.

The existing `forge_asset_build` fixed-command process owns both tile recipes.
Its normal supervisor provides cancellation, a 120-second deadline, 1GiB memory,
64MiB plus header per file, and 128MiB total staging bounds. Tile Set input is one
owned source snapshot; Tile Map also receives the exact owned cooked Tile Set.
Requests bind expected UUIDs, source/dependency digests and a recipe fingerprint
from the actual selected source/toolchain. The worker cannot publish a catalog
or edit authored files.

Publication stores one ordinary root record, immutable file and paired typed
Build/Runtime dependency edges: Map → Tile Set → unique Materials, continuing
through their normal image dependencies. Build edges order cooking against the
selections it reads; Runtime edges define the shipped closure. The pairs bind the
same revision. Runtime catalogs omit Build edges while their immutable artifact
manifests retain that provenance. Local keys never become subassets. Loading and content packaging
share the same record/recipe/generation/edge/file/manifest admission. These loads
inspect selected records rather than requiring authored source-file existence.
CPU admission does not promise GPU/draw readiness.

Focused tests exercise real-worker publication, cached candidate reuse, invalid
actual grid/missing-tile rejection, old strong leases across replacement, stale
dependency rejection/recook, relocated content packages and offline source files.
This is content-package backend evidence; it is not a running exported tile game
or a completed editor workflow.

### Discovery, import and file tools

Ordinary source discovery recognizes `.tileset.json` and `.tilemap.json`, including
uppercase suffixes. Published assets appear in the existing Content index. The
editor's source-update service uses its existing dependency queue, dirty-document
blocking and last-good publication behavior; it does not automatically allocate
identities for unregistered authored sources.

The normal `forge_tools --assets import-target PROJECT SOURCE TARGET_JSON` path
reads the source-owned root UUID and uses the same tile importer/worker. Plain
`import` uses the existing desktop target. Map packaging rejects a stale selected
Tile Set revision until that map is recooked.

File duplication creates a new root UUID, retains source-local image/tile/layer
keys, cell addresses, unknown payloads and external Material/Tile Set references,
and clears the copied cooked-selection claims. The copy needs its own import.
Moving preserves identity and requires the complete tile source suffix. Map copies
use their admitted 64MiB source profile through the ordinary file transaction,
including sources larger than the catalog's unrelated default document limit.

Reference review reports image Materials and a map's Tile Set as typed references.
Unknown root, image/grid/tile, layer, chunk and occupied-cell fields remain opaque
coverage warnings, never guessed references or UUID replacements. Malformed
recognized tile JSON blocks review instead of silently losing reference coverage.

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

### Native rendering and grid selection (development)

The existing MeshSceneRenderer derives one scoped image draw owner per map actor
and used image key. Image keys remain local uint64 values, including values beyond
exact float representation. Each owner uses the shared Quad, ordinary unlit
Material/Texture selection and Sprite draw profile. Per-cell matrices and numeric
UV/color values enter the existing render queue and 64-instance batches. Cells
receive no entities or UUIDs. Equal resources can share one complete draw bundle
across image keys and independently edited map actors.

The renderer retains a complete old image/bundle set while a new selection loads
or fails grid admission. A map never adopts just some of its replacement images.
Cell/definition snapshots remain shared immutable values; steady updates do not
clone arrays. Chunk culling omits off-camera chunks and hidden layers. Sparse
occupied bounds drive framing; explicit selection can frame hidden layers too.
Renderer and layer order add in int64, preserving both signed int32 ranges.
The current scene profile is at most 256 map actors and 262144 visible cell
instances per camera, within the existing pose/material payload budgets.

Sprite instance uploads use ordered default-buffer updates through the backend's
paged upload owner. They do not retain a DISCARD allocation per cell batch in a
fixed dynamic heap. Shader reflection suppresses unused light-list uploads for
unlit draws. All mapped MeshDraw uploads check allocation success before writing.
No device heap setting or dependency revision changes for this path.

`pick_tile_grid` intersects the transformed local XY plane using the existing
camera projection in camera-relative double precision. It returns local meters,
the half-open signed cell address and ordinary projected depth. It does not
invert the unused Z axis: a collapsed Z scale still allows selecting a valid XY
surface. A collapsed XY plane, parallel ray, clipped intersection, invalid input
or address outside int32 returns no hit. This grid query also permits empty cells
for future paint tools. Actor picking looks up that single cell in each visible
layer, respects selectability/camera masks, and compares depth/order with ordinary
Mesh/Sprite selection. It does not expand or scan every tile triangle.

Focused Linux tests include actual software-Vulkan readback for negative seams,
wide scoped keys, independent instances, layer order, camera masks, sparse framing,
far origins, image replacement failure/recovery and runtime cell changes. The
maximum dense case renders all 262144 cells across steady frames and an edit.
These checks establish correctness/capacity, not physical GPU FPS. Native typed
snapshots and ordinary cooked asset selections are supported here. Editor Play
uses the explicit copied runtime projection described below.

### Copied Editor Play presentation (development)

Both process hosts advertise private `tile_projection: 1` support. Ordinary
snapshots carry only a bounded manifest of actor identities, requested/adopted
roots, instance generations, cell revisions and counts. They never inline a
whole cell map. The existing correlated Play transport retrieves one frozen
transfer at a time using begin/page/ack/cancel commands. A page contains at most
four chunks and 256 KiB. A complete header, explicit full-transfer chunk keys,
counts and all pages must validate before the receiver acknowledges and publishes
an immutable frame. Partial packets cannot become visible map cells.

The existing PlaySession owns separate active and prepared receiving state; the
hosts retain corresponding derived sending state. These adapters own neither a
simulation world nor a catalog, file loader or gameplay edit queue. The existing
fixed-step map instance remains authoritative. An acknowledged compatible baseline
allows structural chunk deltas. Eviction falls back to full pages. A full transfer
can retain equal arrays from already received compatible values; it cannot load
an authored asset as replacement for runtime data. Explicitly unedited initial
instances with matching Map and Set revisions can share received storage.

The sender retains at most 16 acknowledged baselines within a 64 MiB copied-value
budget. The receiver admits at most 64 MiB of complete copied values, with one
bounded candidate transfer in progress. Shared chunk arrays count once. These
budgets cover the adapters' retained payload estimates, not total process RSS or
external frames retained by a renderer. The manifest is limited to 256 actors.
Full-transfer key lists permit dropped chunks to be removed before replacement
chunks are admitted at the maximum map capacity.

SDK candidate manifests are included before the existing 8 MiB envelope freeze;
the ordinary 16 MiB process response limit remains unchanged. The staged renderer
uses the candidate's exact copied scope before its presentation verdict. On
activation, prepared receiving/sending state moves into the active scope, retaining
its acknowledged baseline. Supersession, cancellation, scene/world replacement,
instance-generation changes and Stop invalidate the affected data. The Game view
always marks its maps as runtime instances. No authored-data fallback is permitted
while a copied view is missing. Source documents and Scene view previews continue
through their existing authoring paths.

Focused Linux checks cover full/delta transfers, maximum maps, shared/private
cache accounting, frozen updates, malformed/cancelled/stale transfers, actual
published-asset SDK-host preparation and fixed-step edits, actual PlaySession/worker pipes in both legacy and matching shared-SDK hosts,
published source replacement with a new instance generation, Stop/restart, and
native observation through copied values into software-Vulkan pixel readback. Pending/cleared scopes,
a black-cell edit and an immutable older magenta frame have pixel checks. Physical
Windows acceptance and the coherent exported Gallery/game gate remain outstanding.

### Authoring asset selection and isolated previews (development)

MeshResourceHost loads ordinary map references through the existing typed request,
resource-pool and immutable cooked-artifact admission paths. It requests the Map,
then its selected Tile Set, then the existing Material/Texture draw resources.
Loading runs on the resource workers. Catalog changes restart candidate selection;
stale dependency revisions produce a terminal diagnostic and retain the last
complete usable draw until a matching map recook is available. Runtime snapshots
are authoritative: a running instance with missing cells never falls back to an
authored map. A missing or changed runtime scope also retires the renderer's
complete cached tile draw; clearing only its requested data is insufficient.

The host owns bounded 64 MiB Map and 16 MiB Set CPU resource pools. Inspection
includes their requests, revisions and owned payload estimates. Pure view handles
retain their pool leases while an old complete draw still uses the shared rows or
chunks; candidate replacement cannot release the pool's accounting prematurely.
Root/row handles copy once per selection, with shared immutable cell arrays, and
remain safe independently of scope retirement. These are payload budgets, not RSS
or total renderer-memory guarantees.

An explicitly isolated preview host accepts one unsaved Map source and one Set
source as immutable pure values with matching root identities. Reusing the same
shared value is a no-op. Ordinary hosts reject draft injection; preview changes
never publish the catalog or write authored files. A Map draft may use the normal
published Set, or an isolated Set draft can replace it. The existing document,
writer, history and Save/Publish owners remain authoritative. These preview APIs
are groundwork for document tools, not a completed painting UI.

Focused Linux software-Vulkan tests publish real original sources through the
normal import worker, render ordinary asset references, inspect resources and
select cells. They also cover stale Set dependencies, last-good draw retention,
recook recovery, isolated Map/Set drafts, unchanged saved cells and missing-runtime
snapshot rejection. Scoped sanitizer checks instrument the draw owner, resolver,
upload/bundle and actual fixture; linked CPU/vendor archives and import workers
remain normal. Physical Windows/UI acceptance is still pending.

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

The internal native `with_cells` operation creates a detached cooked-data
candidate against an admitted matching Tile Set. It validates every written layer,
tile key and flag, accepts at most 4096 ordered writes, and uses the last write to
an address. It checks the final cell/chunk counts before allocating chunk arrays,
so a valid atomic move at capacity is allowed. Erasing the last cell removes the
runtime chunk; unknown authoring payloads are absent from this cooked value.
Unchanged and no-op chunks share their existing immutable arrays. Separate
candidates cannot change each other or the original asset snapshot.

This operation validates written cells; selecting a compatible baseline/Tile Set
revision remains the asset/instance owner's responsibility. It does not publish
to a world, queue a gameplay command, persist a game save or provide a public SDK
interface. The runtime instance owner uses this candidate at its fixed-step boundary,
as described above. `with_erased_cells` accepts only empty values and applies the
same bounded, structurally shared patches without a loadable Tile Set.

The maximum-profile test uses all 1024 chunks and 262144 occupied cells. Its
complete cooked artifact is 4215032 bytes. In the Linux GCC12 configuration its
retained allocation estimate is 16883912 bytes. That estimate includes typed arrays, container capacity and
conservative node/control storage; it is not a measured process RSS or performance
guarantee. Cooking/publication and explicit-root content packaging
use the ordinary asset pipeline. The complete rendered game-export gate remains
unfinished.

Focused source tests cover malformed inputs, duplicate JSON keys, finite/range
limits, negative/far addresses, exact uint64 keys, unknown data, detached paint,
erase, ordered writes, layer isolation, allocation history and Save/reopen.
The actual document owner also exercises a 262144-cell map with 16 paint strokes
and complete Undo/Redo. These source checks do not replace native-renderer,
matching installed SDK, editor interaction and moved-game gates.

The remaining complete feature gate includes moved-game validation, a fresh
matching installed SDK consumer and original editable Gallery content. The source documents and isolated canvas require the
coherent Windows gate before delivery. Native rendering/grid picking and bounded C++ queries/edits have
focused local evidence; they do not complete those workflows. No autotiling,
animated tiles, 2D collision or streaming is implied by this source foundation.
