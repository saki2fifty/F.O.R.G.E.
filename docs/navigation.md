# Navigation foundation

Phase 6E adds static navmesh generation and bounded headless path queries. Recast builds navigation geometry; Detour loads the admitted result and supplies projection, route finding and surface traversal. FORGE owns identity, authoring, lifecycle, validation, movement policy and presentation.

## Upstream selection

RecastNavigation **v1.6.0**, exact commit `6dc1667f580357e8a2154c28b7867bea7e8ad3a7`, is the latest official stable release reviewed on 2026-09-18. License: zlib. The [official release](https://github.com/recastnavigation/recastnavigation/releases/tag/v1.6.0), [documentation](https://recastnav.com/) and [pinned sources](https://github.com/recastnavigation/recastnavigation/tree/6dc1667f580357e8a2154c28b7867bea7e8ad3a7) govern integration. Current main was also reviewed; it does not supersede this tested stable pin.

Recast, Detour and its obstacle-velocity sampling source are compiled. The upstream integration guide permits directly compiling their source directories; FORGE uses private CMake targets to avoid the demo's SDL2/OpenGL dependencies and old root-CMake policy requirements. DetourCrowd supplies steering/avoidance, DetourTileCache supports obstacle-driven tile updates, and tiled meshes support larger worlds. The pinned TileCache module supplies the opt-in dynamic obstacle profile. The full Crowd manager and sample frameworks are not integrated here. The sample's native `MSET` serialization is not FORGE's asset format.

## Authored configuration

`NavigationSurface { enabled }` marks static shapes and Mesh leaf entities for inclusion. `NavigationAgent` stores an optional typed NavMesh AssetRef, enabled/destination flags, speed, stopping distance and a world-space destination. Flecs owns both components, their reflection and component/property prefab inheritance. Scene version and persistent identity formats are unchanged. Routes, polygon references and query scratch are never scene or prefab state.

An explicit plane, optional dynamic-obstacle/collection flags, collection layer mask, build profile name and six numeric settings belong to a Scene's generated navigation asset: radius, height, climb, slope, cell size and cell height. Plane defaults to Spatial3D; PlanarXY is opt-in. Successful publication preserves the selected profile's AssetId and records its settings.

`NavigationSettings.profile` defaults to `default`, which is omitted from JSON to
retain legacy settings and cooked representations. Names are case-sensitive
ASCII tokens of 1–64 characters, beginning with a letter and continuing with
letters, digits, `_` or `-`. The existing catalog derives the Scene/name→NavAsset
selection. Duplicate names reject before resource loading or publication. New
names publish separate ordinary Nav assets; rebuilding or changing plane replaces
only the selected profile's revision, retaining its ID and prior artifacts.
Agents continue to select typed NavMeshAsset references; source validity, query
state, recovery and exported content remain with their existing owners. A runtime
world retains its existing eight-asset/32MiB tile-data cache limit.

NavigationTools selects saved profiles or starts an unpublished named settings
draft. Selection loads saved settings/triangles before replacing its current view;
failed selection preserves the prior view. Content and NavMesh pickers identify
names, with the name first in a narrow picker field. Runtime overlay paths use the
selected asset's plane; other profile paths are filtered. Profile selection and
unfinished settings do not mutate Scene history or publish assets. Automation/MCP
status, select and build share this owner, permission and Play-lock behavior.


The editor's regular navigation poll clears cached selection, settings, metadata
and overlay triangles when the project root or Scene identity changes, even with
the Navigation content panel closed and no build running. Scene UUIDs alone do
not distinguish copied projects. Opening Navigation loads the saved current-scene
selection through the existing catalog/admission path; this is read-only and does
not rebuild or publish. Same-project, same-scene polling retains usable previews
and unfinished settings. Background builds retain their separate captured project,
scene and cancellation checks before publication.

## Dynamic obstacle profile

`NavigationObstacle { enabled=true, radius=.5, height=2 }` is ordinary reflected
Navigation-family data. It marks a centered upright cylinder in 3D or a circle in XY,
independent of visible geometry and physics. Radius is 0.05–5 local metres; height is
0.2–10 local metres and is ignored in XY. Resolved scale determines world size. Spatial
cylinders accept circular XZ scale and upright Y, including mirrors and yaw. XY accepts
circular projected XY scale and ignores visual Z. Elliptical, sheared, collapsed or
spatially tilted shapes fail explicitly. Carving adds the baked agent radius to the
obstacle radius. Moving physics poses are sampled at navigation's existing fixed boundary;
a later physics step is observed at the next boundary. This read does not add another
physics or transform writer.

Opt into `dynamic_obstacles:true` when baking. Default settings omit this field, retaining
the old static profile. The existing candidate/worker/catalog owner publishes
envelope/catalog 4 with geometry_version 2 (spatial) or 3 (XY); it preserves AssetId and
prior immutable artifacts. Payload `le-cells32-tiled-v1` stores FORGE raw heightfield
cells and bounded descriptors, never native mutable caches/links. Admission checks
lengths, integer coordinates, finite anchored bounds, exact settings, areas, heights and
safe reciprocal cell connections before native work. The producer covers emitted portal
heights in each layer bound and reserves the original voxel span plus climb below 255 to
prevent byte wrap. Old envelope/catalog 1/2/3 admission is unchanged. See
[ADR027](decisions/027-navigation-obstacle-tiles.md).

Static source capture and digests exclude dynamic obstacle components/poses. Moving,
resizing, disabling or removing an obstacle updates only its per-query carved tiles.
Obstacle entities cannot simultaneously be enabled navigation sources or agents.
Bodies/moving ancestors are allowed: this consumer reads the resolved pose without writing
a transform. A static asset with enabled obstacles requires an explicit dynamic-profile
rebuild. Invalid snapshots prevent queries/movement rather than exposing an outdated
route.

Every Query owns a private native TileCache/NavMesh derived from its immutable Mesh
layers. Updates finish at the writable owner boundary; read-only calls with an unapplied
snapshot return unavailable. A requested cache entry is synchronized at the next writable
boundary even without NavigationAgent, so a C++ controller can query while retaining sole
transform ownership. Failed snapshots retain their error and retry at most once per
boundary instead of once per agent. Unchanged snapshots perform no tile rebuilds. Changes
remove old footprints and add new ones; the local query generation invalidates cached
agent routes even for paused agents whose settings/position did not change. Failed native
updates retire the derivative; a later valid snapshot reconstructs it. Recovery rebuilds
routes from current Scene poses and exact asset revisions, without persisting native tile
references or cache state. Dynamic-obstacle geometry is excluded from live tuning.
Agent/avoidance values and a Navigation Behavior's enabled flag use the explicit
shared whitelist below.

The finite profile uses 32 × 32-cell tiles, at most 64 heightfield layers, four per
coordinate, eight enabled obstacles and eight touched layers per inflated obstacle.
Preflight uses the pinned public tile query with capacity for all 64 admitted layers,
matching registration's coordinate and boundary policy. Four layers per coordinate stay
below its separate 32-entry lookup limit. Reject more than eight touched layers before
native submission. Footprints may span at most 16 tile positions on each axis, bounding
empty-coordinate scans independently of touched layers. FORGE drains one request before
submitting another, remaining below its 64-request/64-update limits. Off-mesh shapes need
no native slot: the pin does not retire zero-touched removal requests. A complete
replacement costs at most 128 affected tile rebuilds at this boundary; this is a work
bound, not a frame rate guarantee. The 4096-total-polygon, 4 MiB payload/derived tile-set
and existing query/source/worker bounds still apply. Layer generation can reject a large
grid before reaching 512×512 because the 64-layer bound also applies.

This carves existing walkable heightfields. It does not build new floors, rebake moving
geometry, push agents out of newly covered areas, sweep collisions or alter physics. An
agent covered by an obstacle has no valid start until a valid route is available. Static
overlay triangles show the saved base; runtime route lines show the current carved path.
`NavObstacleUpdate` profiles changed snapshots only.

## Geometry and coordinates

The geometry bridge uses actual FORGE CPU primitive triangles and evaluated Phase 3 WorldTransform values. Coordinates are world-anchored, +Y up, in metres; slope uses degrees. Source triangle winding is normalized using primitive normals in a navigation-only copy. Zero-area sphere-pole triangles are omitted. Renderer meshes are unchanged.

Static editable Mesh assets and imported model Mesh leaf entities are accepted through the existing cooked Mesh resource path. The root Model/group is not itself triangle geometry. Moving baked source geometry and general runtime geometry rebaking remain unsupported; dynamic obstacle carving uses the separate opt-in profile below. Included geometry must be static and have supported resolved spatial transforms. Geometry changes make the navmesh stale and require an explicit rebuild. Unrelated names, colors and agent positions do not form part of the geometry digest.

Enabled CharacterController and CharacterController2D components also make a
source or its resolved spatial ancestry ineligible for static baking, even before
native realization or without a Physics provider. Native source capture copies
these enabled flags into the same JSON validation owner used by authored builds.
Inherited values count; disabled controllers retain ordinary static eligibility.
World spatial binding ends ancestry traversal. Adding an enabled controller makes
existing source validation fail; disabling/removing it can reuse unchanged valid
geometry. Failed builds keep the last catalog selection/artifact. No source digest,
asset identity, envelope version, SDK or checkpoint format changes are needed.

### Explicit XY region profile

`NavigationRegion2D { enabled=true, obstacle=false, width=10, height=10, points={}, source=0 }`
is ordinary reflected Navigation-family data. Empty `points` selects a centered
local rectangle; width/height remain0.01–1000metres. A nonempty vector selects a
complete simple counterclockwise contour of3–64 `NavigationRegionVertex {x,y}`
values. Local coordinates must be finite and within±1000metres. Repeated vertices,
adjacent collinear edges, crossings and self-touching edges are rejected. Holes
use separate obstacle regions. Each property operation validates its complete
candidate before changing source, revision or history. Unknown per-point source
metadata uses the existing reflected extension owner.

`navigation_region.hpp` owns the resolved world-XY outline. It admits signed XY
scale and zero visual-depth scale, rejects collapsed/out-of-bounds contours, and
normalizes mirrored winding only in a detached copy. Source capture, bake,
Inspector validation and editor guides share this footprint. Sprites/colliders/
Tile Maps are not inputs under the default marked-source policy; explicit collision
sources and profile-scoped collection are described below. The shared pure polygon predicates and ear
clipping also serve the existing tile polygon owner; tile-specific8-point/unit-cell
admission remains separate.

Walkable contours contribute deterministic upward triangles in native XZ.
Concave obstacle contours decompose into convex triangle masks; rectangles retain
their original four-vertex mask. The pinned `rcMarkConvexPolyArea` applies masks to
the compact heightfield before agent-radius erosion. Overlapping walkable regions
form a union. At least one walkable region is required; all sources share the
512-entity /16,384-triangle limits and at most512derived convex obstacle masks.

One `navigation_space.hpp` boundary maps world XY to native XZ and native Y to an
internal flat altitude derived from admitted tile vertices. Mesh/Query projection,
paths, surface moves, avoidance and triangles all use it. Projection preserves the
input visual Z; paths/moves preserve starting Z; velocity Z is0. Destination Z is
validated for finiteness/bounds but does not select a floor or drive visual depth.
Flatness of every base/detail vertex is checked before Detour initialization.
No second authored height, coordinate database or transform writer is introduced.

Native capture maintains independent derived validation state for each plane.
Unselected 3D source errors cannot poison XY and vice versa. Only3D owns external
Mesh resource subscriptions. XY hashes ordered scene/source IDs, the six affine
coefficients contributing to world XY and obstacle roles. Empty contours retain
exact legacy rectangle dimension hashing, triangles and cooked bytes. Nonempty
contours hash the actual native float coordinates and exclude unused width/height;
point edits invalidate the selected geometry through the existing staleness owner.
Visual Z, materials and unrelated authored state are excluded. Both profiles retain
static/body/controller/spatial ancestry rules; agents cannot also be enabled bake
sources. XY avoidance uses its NavMesh selection or an explicitly shared group in the same plane, without a visual-height test.

### Opt-in static Collider 2D sources

Source0 preserves rectangle/Points geometry and legacy identity. Source1 selects
exactly one Box, Circle, Capsule or Tile Map Collider2D on the marked entity.
Unused rectangle dimensions/Points remain authored but do not enter its geometry
digest. Existing static, controller, enabled-agent and spatial-ancestry checks
apply; each local rotation in a collider source chain must stay in XY. Physical
TRS admission rejects shear/tilt/collapse. VisualZ and zero visual-depth scale
remain independent. Physics body enable/sensor/filter/one-way configuration does
not decide the explicitly authored walkable/obstacle role.

Shared pure physical values own scaled dimensions, direct quaternion rotation,
native float box/hull admission and Map collision projection. Bake creates no
native physics world. Bounded convex inline contours prove actual float-rounded
containment: walkable contours are subsets of the physical shape; masks contain
it. A deterministic conservative roundoff margin covers coordinate conversion.
Rounded contours use at most64points; unsupported numerical collapse or true
physical bounds outside±4090m reject instead of inventing geometry.

Map preparation requests selected compact Map/Set values through existing Tile
resource pools and resolves existing neighbor variants. `tile_map_collision_geometry`
supplies merged rectangles and convex pieces for the selected stable layer key,
including mirrored cells, custom concavity, islands, holes and no-collision cells.
At most32distinct Map roots and64MiB of conservatively estimated retained inputs
are admitted; each Map source admits512pieces and the existing total triangle/mask
limits remain. No partial geometry is silently truncated.

Geometry5/static envelope5 (or dynamic envelope4/geometry5) separates `tile_inputs`
from external `mesh_inputs`. Each canonical selected Map revision is recorded in
metadata and a `navigation.tile_map` Build edge. Root Map selection already binds
Tile Set revisions through its ordinary dependency records. Metadata-only status
uses that same selected-revision owner; mismatched/stale dependencies reject.
Versions1–4 and default geometry3 remain readable, without identity migration.

Runtime uses current `RuntimeTileMapSnapshot` bindings and status. Navigation-only
Map sources participate in ordinary candidate readiness even when their renderer
is disabled. Loading/current-revision mismatch prevents movement. Runtime-edited
selected instances invalidate the baked route until Reset restores published
cells. There is no per-tick cell hashing/triangulation, separate Map subscription
or runtime rebake. Existing exact-revision recovery and SDK restart policy apply.

### Region Scene interaction

The pure region footprint lives in `navigation_region.hpp`; baking and detached
editor consumers use the same calculation. NavigationRegionView reads
one current effective/preview row with separate contours and rejects invalid/unresolved geometry without
inventing a substitute shape. SpatialHelpers owns cached region outlines alongside
its existing Camera/Light/Audio helpers, using the same preview generation and
hierarchy policies, plus selected Map revision/readiness. Readonly Map selections
share existing render-host CPU pools and are bounded to32LRU entries. Island
contours have no bridge edges. Navigation overlay shows all visible region edges; selection
keeps that region's guide available independently. Only edges participate in
region picking, with scaled tolerance and deterministic distance/depth/identity
ties. Camera/light/audio icon routing remains ahead of region-outline picking.
Shared clipped-line projection keeps drawing and picking consistent.

Selection framing adds the full projected XY footprint, including when the actor
has retained render bounds. Art keeps its actual bounds/depth. The fallback camera
can frame a region without a rendered Mesh; a helper position is not substituted
for its dimensions.

SceneTools composes NavigationRegionGesture with Move/Rig/modal input ownership.
Four edge-midpoint handles edit one centered local extent in rectangle mode only. Explicit gold squares
draw and receive clicks above overlapping Move axes; existing active/rig/modal
ownership is preserved. The projected-axis
calculation is shared with rig targeting; Ctrl snap is in local metre dimensions.
Preview uses the ordinary detached authoring candidate; commit uses one property
command/history operation. No authored transform, source role or other extent is
written. Neutral/rejected/cancelled releases do not commit. Scene revision/selection,
edit locks, viewport changes and existing focus/project guards retire the gesture.
Prefab property inheritance and Save/reopen retain the normal Scene owner.

Inspector **Create polygon** seeds four valid rectangle corners in one ordinary
property/history operation. **Use rectangle** clears the contour; Undo restores its
points and extension metadata. Structured point editing uses the existing
collection drawer. Polygon mode hides unused width/height and disables rectangle
extent handles; ordinary object transforms, outline selection and framing remain.
Prefab inheritance, partial point overrides, source publication, revert and history
retain the existing Scene owner and native owning-vector lifecycle.

Automation/MCP exposes width/height and the bounded structured `points` property,
including discovery/default/element schema, validation and history operations; this viewport convenience adds no pixel-only game-creation
capability, separate commands or persistence. Bake/runtime/asset/SDK formats and
navigation movement semantics are unchanged by these editor interactions.

### Static Mesh profile

Bake uses LOD0, all its triangle parts, float3 POSITION and indexed or sequential vertices. Imported winding is preserved; a negative world-transform parity reverses triangle winding in a navigation-only copy. Collapsed/zero-area triangles are skipped. Built-in normal-based winding correction and old v1 hashing are unchanged. Entire Mesh admission observes the bounded profile before geometry extraction; decoded candidates share a64 MiB job pool. Model-family/cook admission reuses existing immutable selection validation. An independent build job may use that same bounded ResourcePool loader; live-world runtime data belongs solely to RuntimeResources.

Points/lines, joint/weight streams, prepared joint palettes, morph targets/names and Mesh model-node skin/morph bindings are rejected. This profile conservatively rejects a Mesh with deforming node bindings even if another instance currently appears static. Mark an undeformed static Mesh instead. No source JSON/glTF/FBX parser runs in NavigationRuntime. Scene/ancestor static-physics and spatial-resolution rules apply to every accepted source. An admitted selection has one30-second preparation deadline before the existing30-second native build-worker limit; cancellation is checked between resource waits.

## Asset publication and admission

1. Capture an evaluated source snapshot and the current catalog revision.
2. Run Recast in the dedicated `forge_nav_build` process, with fixed arguments and an isolated staging directory.
3. Validate the returned envelope and exact Detour subset before runtime initialization.
4. Prove projection and a path query against the candidate.
5. Recheck geometry and catalog revisions on the authoring owner thread.
6. Move an immutable artifact into `Assets/Navigation/Generated/<AssetId>/` and atomically select it through the catalog.

The candidate holds the shared [worker staging lease](import-settings.md#worker-staging-ownership-and-recovery)
through publication/discard. Cleanup removes only a recognized, unowned stage;
active or unfamiliar contents remain for guarded maintenance.

Errors, cancellation or stale candidates preserve the previous catalog and artifact. The project writer lease applies to editor publication. Asset builds have their own ownership boundary: scene Undo does not undo navmesh builds. Old immutable revisions remain available; this phase does not introduce asset garbage collection.

The retained FORGENAV v1 envelope records logical asset/source-scene identity, included EntityIds, geometry digest, settings, exact Recast revision, binary-layout/subset marker, bounds and tile digest. New spatial3D builds use explicit FORGENAV envelope/catalog schema2 and geometry bridge2. They record up to32 unique external Mesh AssetIds and exact immutable pool selection keys, with typed Build dependencies on the source Scene and selected Meshes. Geometry SHA256 hashes ordered source identities, evaluated affines, shape kinds/Mesh IDs and selected revisions. Source names, materials and runtime agent positions are excluded. Mesh bytes are admitted only during bake; runtime compares small descriptors and selection stamps. Selected v1 assets retain their original triangle-based digest and bytes; envelope/catalog versions must agree. Known Mesh provenance cannot be downgraded into v1, and future versions are rejected.

XY builds use envelope/catalog/geometry3 with required settings `plane:1` and an
empty Mesh input list. A shared provenance validator is used by the worker, writer,
reader and version-aware source checks. Versions1/2 require Spatial3D; version3
requires PlanarXY. Header/catalog/metadata versions must agree. Malformed worker
masks, unsupported basis/version combinations and nonflat tiles are rejected,
even with consistent checksums. XY requests require bounded four-vertex masks;
spatial requests reject planar mask payloads. Failed requests retain previous
output/catalog selection.

Default spatial settings retain exactly the old six-field JSON representation;
only XY adds `plane`. Existing v1/v2 files need no rewrite or migration. Switching
plane explicitly rebuilds the one Scene-owned logical asset, preserving AssetId
and old immutable artifacts. The recovery-supporting runtime path remains version2: exact asset revisions
already bind coordinate semantics, and routes are recomputed through their admitted
Mesh. Production SDK Play continues to restart from authored content and rejects
recovery requests; arbitrary native state is not migrated. Matching direct-Flecs modules rebuild against the changed public headers.

The catalog also verifies the entire artifact digest. Checksums establish consistency; they do not replace structural admission.

The worker request and envelope metadata must each contain one complete JSON
record. Raw NUL bytes, trailing non-whitespace bytes and additional values are
rejected before building or admitting a tile. Request and metadata limits remain
4 MiB and64KiB respectively, with nesting limited to 16 levels. The metadata check
covers only its declared range; binary tile bytes are validated separately.
An invalid request leaves any existing worker output intact.

Detour's native loader trusts section counts and offsets. FORGE checks sizes, full consumption, finite bounds, counts, vertex/detail indices, convex clockwise polygons, reciprocal neighbors, detail ranges and boundary edges before initializing Detour. The legacy static profile admits little-endian, 32-bit polygon references, one tile, one walkable flag/area, no BV tree, no off-mesh sections and fresh zeroed runtime link storage. Version6 adds only the explicitly described and structurally validated authored off-mesh sections; it retains those other bounds. Arbitrary Detour/sample binaries are rejected. The admitted bytes are copied into Detour-owned memory; assets shared by agents remain immutable.

## Read-only runtime feedback

The existing NavigationTools viewport overlay can show the selected agent's
requested destination before Play. It reads the current effective component and
world affine, uses the selected saved NavMesh metadata's plane and requires that
asset reference to match. XY uses the agent's visual Z; 3D uses requested XYZ.
Hidden/unresolved/prefab-source/invalid/cleared requests have no invented guide.
This detached cyan guide is distinct from the amber calculated runtime path and
performs no query, asset IO, tick, pose write, history or publication. It remains
project/Scene scoped through the existing overlay owner. The normal checkbox and
navigation.overlay automation operation change the same transient view flag.

The explicit Scene destination tool reuses that copied view and the existing
SceneTools gesture owner. It edits world X/Z for3D or X/Y forXY through a detached
`property.set_many` preview, preserving the other authored coordinate. Release
submits one ordinary authoring/history operation; equal-value clicks create none.
Current scope/asset/plane/configuration, Scene revision, selection and editability
remain checked. Snap uses world metres; no parent inverse or entity pose write is
needed. Inspector callbacks carry the requested entity, including pinned inspection.
Shared axis layout/hit and ray/plane math also serve existing rig/region consumers.
`ImGuiIO::AppFocusLost` cancels shared SceneTools gestures before cleared input can
leave a preview captured. No source/schema/SDK or runtime navigation state changes.
Automation/MCP uses the same ordinary destination field batch and history commands;
no independent screen-drag or gameplay authoring path is introduced.

Private `navigation_debug` adds `diagnostic`, a copied human-readable reason only
for the current Failed observation/configuration. It reads the existing per-agent
error map, or its current global agent-limit entry, without inserting/loading or
changing history/ticks/recovery. Disabled/Idle/Pending/successful samples return an
empty diagnostic. A live configuration change cannot expose the previous config's
reason. The last failed sample remains attributed to its navigation tick until
another tick processes non-configuration changes.

The copied reason takes at most2048 native input bytes; JSON output-text replacement
makes truncated/malformed UTF-8 readable without altering the owning log/error buffer.
Replacement can expand byte count. The same lightweight `json_output_text.hpp`
helper serves the existing automation text wrapper; structured/source parsers keep
strict admission. No SDK observation/POD or Scene/checkpoint schema changes.
Ancestor ownership errors name the authored parent and persistent EntityId, with
a bounded display copy of the name, instead of exposing a raw Flecs handle.

Inspector shows failure details and a valid-sample-only World coordinates disclosure
using the existing position/projected_destination fields. Compact progress remains
available at narrow scales; expanded fields use normal Inspector scrolling. The
values describe the fixed navigation sample, not interpolated rendering. Blocked
feedback permits local avoidance congestion without promising collision response.

The existing global limit dedup entry is retired when that episode resolves, so
another over-limit episode emits a fresh ordinary timestamped diagnostic. No new
log, diagnostic owner, persistent history or game-authoring operation is introduced.
MCP reads the same runtime snapshot/diagnostics through existing Play permissions.

## Live agent tuning

NavigationRuntime::tune owns temporary NavigationAgent, NavigationAvoidance,
NavigationLinkCollision, NavigationAreaFilter and enabled-only NavigationBehavior
edits; tune_avoidance remains a compatibility wrapper. RuntimePropertyTune provides
one shared type/property predicate for the runtime, live Inspector and frozen
keep-values review. Agent fields are enabled, has_destination, speed,
stopping_distance and destination_x/y/z. The NavMesh AssetRef, movement selector, bake sources and
geometry are excluded. Movement selection is Edit-only. Behavior graph/target/anchor are excluded too. Live tuning
does not change the baked asset. Area policy uses the recovery profile described below.

Both ordinary and SDK Play hosts retain their generation/scene/entity/role/thread/
writability checks and dispatch through that owner. A copied candidate uses the
existing component codec and reflected range/type validation before one component
write. Missing components, Prefab templates and forbidden properties reject; no
arbitrary property registration or second runtime API is created. Edits do not tick,
move, save or create authoring history. Existing configuration comparison invalidates
a copied progress sample/path as necessary; the next fixed tick performs ordinary
routing, ownership and movement checks. Numeric admission is not route reachability.

Live inherited values materialize only temporary runtime component state; the authored
Prefab source is unchanged. Recovery records semantic configuration and ownership,
including these live values, through the applicable private profile. Production SDK Play continues
to reject recovery and restarts from authored content. Keeping values instead uses
the existing frozen review, property allowlist, Stop lock and ordinary property history/
Prefab intent/Save owner. Transforms and the NavMesh reference are never reviewed.
The sixteen area costs are reviewed and kept as one array field.

## Character movement selection

ADR052 defines the opt-in `NavigationAgent.movement` profile: zero/absent is
ordinary Navigation; one is CharacterController; two is CharacterController2D;
three is GroundedCharacterController2D as defined by ADR054.
The reflected uint32 selector has
an explicit default and enum, is not animatable or live-tunable, and uses the same
Scene/Prefab/history/automation/MCP property owner. Canonical source omits zero.
The added public value changes exact SDK identity; rebuild modules against the
matching SDK. No C callback table or pinned dependency changed.

Movement1 requires an enabled upright world +Y 3D CharacterController, World
spatial binding and no other enabled body/controller. Spatial3D ground routes and
the physical jump and descending-link profiles below are admitted. The existing route/avoidance owner
computes steering. A host-only PhysicsService::move_character_once request joins
the existing bounded character command queue, applies for one positive physics
step, and expires without changing actual velocity observations. Zero-time
synchronization does not consume it. Persistent manual movement, jump or placement
in that boundary wins regardless of queue order. Ordinary move_character remains
persistent. Disabling/removing navigation or clearing its destination stops renewal;
character gravity, step/slope and physical collision remain physics-owned.

Movement2 requires a World-bound Floating CharacterController2D with
gravity_factor0 and a PlanarXY NavMesh without explicit links. PhysicsService::move_character_2d_once joins the
same FIFO, with its lifetime stored in the existing Native2D Body. Manual XY
movement or placement wins in either order, including a prior armed request.
The next positive step consumes autonomous intent while retaining actual clipped
XY velocity/support observations. Zero-time synchronization and capsule
replacement preserve an armed request. Neither navigation nor this profile
supplies platformer gravity, jumps or floor following. Visual Z remains independent.
Blocked feedback excludes shallow-recovery-only corrections when the copied
requested XY velocity is zero; the actual corrected position remains observable.

Movement3 selects a World-bound Grounded CharacterController2D with positive
native automatic gravity and downward vertical XY Project gravity. The shared
reflected controller validator runs before layer shifts, radius or slope queries.
It uses a PlanarXY route with static walkable physical support and optional
positive-lift jumps or descending zero-lift links as specified by ADR055/056.
Navigation preserves actual Y momentum while requesting horizontal
one-step steering; Native2D alone integrates gravity and clips movement. Optional
physics-owned availability handles the first realization without inventing a pose
or changing the required getter's unavailable error.

Each remaining route corner requires geometric static support (at most64corners).
A bounded32-sample forward support guard holds horizontal steering at unsupported
ground. It excludes the character and sensors and checks walkability, mutual masks
and current static body ownership. This is conservative ground following, not a
platformer jump/drop reachability graph. Native step/floor response remains
physical; higher ledges can still block a route. Arrival requires actual static
walkable support and actual remaining distance, with the existing0.01m minimum
stopping distance and1mm arrival tolerance. Corner admission reuses the native1cm
support contact band so collision slop cannot strand a supported waypoint. The
actual observation is never replaced by a projected or requested pose.

Private navigation recovery10 requires movement3, carries no character translation
and can retain existing physical3D jump markers in mixed worlds. Earlier profiles
retain their meanings; downgrade, empty10 and fabricated character pose/link data
reject. Automatic2D gravity reuses the existing mover intent and exact-runtime
configuration; no second velocity payload or public save-game format is added.
See [ADR054](decisions/054-grounded-planar-navigation.md).

Navigation never sets the character's LocalTranslation. After the existing
PhysicsAdoptionSystem, it reads CharacterState and reconciles actual corner
progress, arrival, blockage and copied position/velocity. Ground projection is
route geometry only; it never substitutes for an actual physical pose. Behavior
frames capture their observation lane: ordinary agents finish before physics,
character agents finish once after adoption. Pending feedback prevents another
navigation tick or recovery capture. Unavailable or incompatible profiles fail
without another pose writer.

At an eroded mesh edge, steering and route distance retain the physical centre's
actual planar offset. A one-point projected route still requires an actual
approach within Speed and Stopping distance. Spatial routes use projected ground
Y with actual XZ; XY routes use actual XY and the current visual Z. Projection
alone cannot establish corner progress or arrival.

Private physics recovery11 carries only queued/armed one-step requests; otherwise
recovery10 stays unchanged. Navigation recovery7 identifies character selection
and explicitly records no navigation-owned character translation. Scene/physics
restore owns the actual controller pose; navigation replans its ground route.
Mixed recovery7 retains ordinary link/arc/collision records under their existing
validation. Downgrades, empty extensions and fabricated character pose/link
records reject. This is exact-runtime recovery, not a public save-game format.
SDK Play continues to restart from authored content.

XY armed/queued intent requires private physics recovery12 and navigation
movement2 requires recovery8. Mixed worlds retain their existing3D one-step and
ordinary-agent link/collision records. Unused profiles retain recovery10/11 and
navigation2–7. The optional Native2D flag is admitted only by the matching outer
profile; false/malformed flags, downgrades and empty extensions reject before
candidate publication. Actual XY pose remains Scene/physics-owned.

### Physical jump links

[ADR053](decisions/053-navigation-character-jumps.md) extends movement1 to
positive-lift 3D links between static physical supports. No new authored field,
pose owner, command queue or SDK callback is introduced. Floating XY and
same-height/upward zero-lift character links remain unsupported. Direct movement retains its
prescribed arc and optional link-query collision behavior.

Navigation reads configured gravity from PhysicsService, including the current
CharacterController gravity factor. Only downward world-Y gravity is admitted.
Incompatible gravity rejects when the linked route is selected, before approach
or avoidance can keep the character cycling near takeoff.
For lift h and effective downward acceleration g, duration T=sqrt(8h/g) and
takeoff speed=(exitY-actualY+4h)/T+g*dt/2 compensate the native velocity-first
step. A takeoff must rise and descend before the exit, have at least four fixed
samples and fit horizontal span/T within Agent Speed. Static takeoff support and
a static landing ray at the admitted exit are required. The character's own
collision masks remain authoritative; optional direct-link query settings never
disable physical collision. Actual flight need not lie on the geometric arc.

PhysicsService::jump_character_once copies combined planar movement and jump
into the existing FIFO. CharacterRecord retains its automatic origin across
zero-time synchronization and capsule recreation. A manual movement, jump or
placement cancels armed automatic movement/jump and wins in either queue order.
The next positive step consumes the request once. Physics recovery13 is canonical
only with queued kind5 or an armed jump_once flag; unused profiles remain10–12.

The existing Agent crossing retains the admitted source/endpoints/lift and marks
physical_jump. Native jump acceptance, copied position/velocity and actual
descending static support determine progress. Landing outside the admitted exit
tolerance or falling below the landing region fails; it never reports arrival
from projection alone. Speed0 stops steering, not gravity. Cancelling releases
steering; destination changes replan after landing. Navigation recovery9 retains
the link marker without a character translation or on-curve pose constraint and
requires matching restored native physics. Older profiles remain unchanged.
SDK Play uses authored restart. Gallery226 exercises physical blockage, a raised
landing and source-hidden relocated export through the same owners.

[ADR055](decisions/055-planar-character-jump-links.md) extends this same flight
branch to movement3 and PlanarXY. A temporary copied observation adapts native2D
support, pose, velocity and actual takeoff receipt to the shared admission and
landing math. Horizontal distance is absolute X; visual Z remains independent.
`jump_character_2d_once` uses the existing FIFO kind5 with its planar tag. Native2D
consumes the pending impulse only on actual static walkable support, before its
rigid-body step, and retains ordinary vertical momentum on refusal. The regular
native mover then integrates gravity and collision. Manual full-XY movement wins
in either pending order, including after zero-time arming or capsule recreation.

Physics recovery14 identifies queued/armed XY takeoff or a native receipt;
navigation recovery11 identifies an active XY physical jump. Mixed worlds retain
their physical3D jump intent under the same profile. Empty, malformed or downgraded
extensions reject without publishing a candidate; physics remains the pose owner.
Gallery228 supplies editable two-way positive-lift links, raised static landings,
actual wall blockage, source-hidden relocated export and installed-only C++ SDK
Play with authored restart. This remains bounded explicit-link traversal, not a
general platformer reachability graph or moving-platform planner.

### Physical descending links

[ADR056](decisions/056-navigation-character-drops.md) admits an oriented link with
Arc height0 and exitY below entryY for movement1/3. The existing authored values,
cooked navigation format, C callback table and installed component layouts are
unchanged. Recommend one-way drops; reverse travel on a bidirectional zero-lift
link is rejected when its oriented exit is no longer lower.

The shared physical traversal branch retains Drop intent and reads copied native
observations. Static walkable launch/landing, upright World-bound mechanics,
downward automatic gravity and nonzero horizontal span are required. Estimated
fall duration sqrt(2*(entryY-exitY)/g) must allow at least four fixed steps. It is
an admission bound, not a prescribed flight trajectory. Steering is bounded by
Agent Speed and remaining horizontal gap/dt. No upward impulse or jump-acceptance
receipt is produced. Native support may continue on the approach or settle before
the endpoint; gravity, collision and floor response remain physics-owned.

Cold native realization uses optional physics-owned availability, then a bounded
static-support query while actual grounding settles. Queries reuse native rays,
walkable normals, mutual collision masks and static body ownership. They do not
invent a pose or freeze vertical motion. A wall before walk-off reports blockage
from actual adopted displacement, including when native requested velocity remains
nonzero. Clearing the wall can resume progress. Arrival requires actual supported
position near the lower exit; falling below or missing that region fails.

Speed0 and cancellation release steering while gravity continues. Pause freezes
simulation; manual movement wins at the existing command boundary. Stopping
distance is checked before traversal. Changed destinations replan after completion.
XY visual Z remains independent. The existing one-step physics state suffices for
recovery: navigation12 identifies active physical_drop intent without storing a
character pose or enforcing a straight chord. Mixed 3D/XY positive jumps retain
their markers, and earlier profiles remain canonical when no drop is active.
Empty, downgraded, malformed, conflicting or non-descending extensions reject
before candidate publication.

Gallery229/230 provide editable 3D/XY static descents, physical walls, published
NavMeshes and Cameras. Their source-hidden relocated exports and matching
installed-only C++ consumers use the same navigation/physics owners. Automation
and MCP use the existing reflected fields, source history and Play controls;
no parallel authoring operation or agent-only movement implementation is added.

## Runtime ownership

`forge.navigation` uses EngineModule lifecycle. Authoring/validation compositions receive schemas; runtime compositions provide a world-scoped Navigation service. No ImGui, renderer or audio device is required. Primitive-only compositions remain usable without a Resources provider. External Mesh geometry lazily requests builtin CPU Resources after bootstrap and holds world-scoped subscriptions; it never creates a competing runtime mesh pool. A selected Mesh load holds the agent Pending (no sample/tick0); missing providers or failed/stale selection prevents movement. Retaining last-good bytes does not authorize following a NavMesh built against a different selected revision. Provider shutdown revokes CPU leases. Synchronization reads retained data/revisions, without re-decoding cooked Mesh files on each tick. Owner-thread queries return owned FORGE waypoint arrays with explicit success, missing/stale, outside, no-path, partial, invalid, unavailable and limit statuses. Partial paths are diagnosed and not followed. Query scratch is private to the runtime world; immutable meshes are shared within its cache.

The fixed pipeline runs gameplay → navigation → physics synchronization/step/adoption → final transforms. Nonphysics agents move incrementally over the admitted surface. They change LocalTranslation only; inherited rotation and scale remain inherited. The default movement selector rejects PhysicsBody and PhysicsBody2D agents, enabled CharacterController/CharacterController2D agents and effective spatial chains through either domain's moving physics bodies or enabled controllers. Source geometry must also remain static in both domains. Structural parenting alone does not determine spatial ancestry; World binding ends traversal. 2D physics does not imply XY navigation. Navigation-driven Kinematic targets and Dynamic body driving remain outside the direct-movement profile. The explicit character profile below delegates movement to physics. C++ controllers may use read-only path queries without assigning NavigationAgent as another pose writer.

Pause performs no movement; Step advances one fixed tick. Presentation reads do not advance the agent. Reconstructed worlds reload the selected asset revision and recompute paths from checkpointed scene transforms/destinations. The recovery section contains AssetId/digest pairs and bounded per-agent semantic configuration/local translation with ownership flags. This preserves gameplay changes hidden by an authored prefab property mask. Restoration occurs in an unpublished candidate world and preserves inherited rotation/scale. It contains no Detour pointers or corridors. A different selected revision rejects recovery.

## Bounded policy

| Resource | Initial bound |
| --- | --- |
| Included entities / triangles | 512 / 16,384 |
| XY source shape | Rectangles or3–64point simple CCW contours; world XY; obstacle subtraction |
| External Mesh inputs / retained CPU data | 32 / 64 MiB |
| Per-Mesh bake profile | 32 MiB scalars, 65,536 vertices, 49,152 indices, 512 parts, 16 LODs |
| Geometry coordinate magnitude | 4,090 metres; generated bounds within 4,096 |
| Horizontal grid | 512 × 512 cells |
| Vertical voxel span | Less than 8,191 cells |
| Polygons / vertices | 4,096 / 16,384 |
| Native tile / metadata | 4 MiB / 64 KiB |
| Active world cache | 8 assets; each payload/base/derived tile set is bounded to4 MiB |
| Dynamic profile |32-cell tiles;64 layers,4per coordinate;8obstacles,8touched layers each |
| Authored agents | 64 |
| Logical areas / per-query costs | 16 / finite1–1,000,000; sixteen-bit allowed mask |
| Query nodes / corridor / waypoints | 2,048 / 128 / 64 |
| Worker | 512 MiB, 30 seconds wall time, 32 MiB total staged files |

Exceeding these bounds returns a diagnostic rather than silently truncating a path or publishing a partial build. These limits are a small-scene foundation, not a large-world performance claim.

## Exact gameplay SDK

`NavigationAgent`, `NavigationRegion2D` and its owning point vector are available
to exact-version direct-Flecs modules. Region layout/Meta offsets and vector copy/
removal are verified against the matching installed SDK. The planar sample reads
both four-point floors and concave obstacle values through ordinary typed access.
`FORGE_SDK_NAVIGATION` exposes `navigation_query` for projection and path finding during an owner fixed tick. Results use a caller-owned buffer with capacity 1–64 and explicit status/count. An undersized buffer returns the limit status with no points. No Recast/Detour type crosses the boundary. The area-policy extension appends `navigation_query_filtered`, as described below; the original callback remains available. SDK fingerprints include the component/header contract; modules must be rebuilt together. The movement-only ABI1 path mentioned in the original phase record has since been retired. Generic AssetHandle remains deferred: the private AssetId cache is sufficient.

## Area classification and query policy

`NavigationArea` classifies selected walkable geometry and authored links on that
entity with a logical class0–15. Effective/native capture shares the source owner;
nondefault classes participate in source digests and stale-candidate validation.
Absent/default0 preserves earlier canonical digests and native bytes.

Classified assets use envelope9 with sorted nonzero `area_sources` provenance and
optional nonzero link `area`. Static polygons carry the logical area and exactly
its `1 << area` flag. Raster1–16 maps logical0–15 while preserving null0; generic
walkable63 cannot erase overlapping classes. Higher class wins an eligible merged
span. Dynamic raw layers use `FNTILE09` with occupied raster1–16 and the same tile
callback conversion after carving. Older envelopes/layers retain strict admission.
Worker profile/provenance matching rejects silent classified downgrades.

`NavigationAreaFilter` stores a uint32 mask bounded to sixteen bits and sixteen
fixed float costs. Defaults permit all classes at unit cost. Costs are finite
1–1,000,000, preserving the distance heuristic's lower bound. Mask0 permits no
surface. Query constructs call-local filters for projection, paths, movement,
avoidance walls and retained connection/endpoint checks. Assets and another
Agent's policy stay immutable. Costs belong to queries rather than build profiles.

Agents replan at the next legal boundary. Active crossings retain their admission
policy until completion. Private navigation recovery13 preserves configured
values, ownership and retained crossing policy; absent policy keeps older profiles
canonical. Physics retains character pose ownership. Malformed, empty, conflicting
or downgraded policy extensions reject before candidate publication.

The matching SDK exposes typed components, filtered NavigationService overloads
and an appended `navigation_query_filtered` C callback. Its sized/version1
`ForgeSdkNavigationFilterV1` contains a sixteen-bit mask and sixteen bounded costs.
Initialize each cost to1; zero-initialized costs are invalid. The typed
`sdk::Client::navigation` overload builds the descriptor through the same provider,
thread, fixed-tick and result-buffer contract. The earlier callback/default queries
remain available. Rebuild modules with the matching private fingerprint; there is
no stable cross-version C++ ABI claim.

Inspector, live tuning, Keep review and automation/MCP use existing component,
property, source and Play owners. The Scene overlay reads triangle classes from
the same admitted mesh. See [ADR057](decisions/057-navigation-areas-and-filters.md).

## Deferred work

Full crowd management, general runtime geometry rebaking, moving-platform routing and world streaming are deferred. Named profiles, explicitly selected 2D collision sources and opt-in static collision collection use the existing build owner. Character driving supports upright 3D ground routes and bounded positive-lift jumps or descending zero-lift drops between static supports, or Floating XY routes without explicit links. Grounded XY movement supports continuous static physical ground and the same explicit jumps/drops with native automatic gravity. General XY platformer reachability planning remains separate work. General save-game formats remain separate work. Current cooked navigation/export uses the existing runtime package owner.

See the [Navigation user guide](../manual/editor/navigation.md) for the no-code workflow.

## Copied agent observation

NavigationRuntime remains the owner of agent paths/movement. `agent_state(EntityRef)`
returns an optional owning value without asset IO, synchronization or world writes.
No observation is returned for a foreign owner thread, retired/unregistered reference,
missing NavigationAgent, unbound Scene or stopped service. Effective prefab values
are observed without creating component overrides.

| Phase | Meaning |
| --- | --- |
| Disabled | Enabled is false. No sampled metrics. |
| Idle | No destination. No sampled metrics. |
| Pending | No completed navigation tick for the current configuration. |
| Moving | A valid route advanced on the sampled tick. |
| Paused | Speed is zero on a valid route that has not arrived. This is separate from pausing the whole game. |
| Arrived | Remaining waypoint-route distance is within stopping_distance plus1mm tolerance. |
| Blocked | A valid route made no advance on this tick despite positive speed. Not a latched stall timer or crowd collision result. |
| Failed | Query/admission/ownership failed; independent NavStatus identifies the cause. |

`sampled && has_path` gates position, net world velocity(m/s), projected destination
and remaining waypoint-polyline distance(m). Tick identifies the completed navigation
update; failures also carry that attempted tick but no valid pose/path metrics.
Disabled/Idle/Pending have tick0. Configuration changes immediately invalidate the
prior observation. Unchanged observations describe the last completed tick, rather
than promising that gameplay has not subsequently moved the entity.

Stopping uses the remaining route around obstacles, rather than Euclidean distance
through them. It may stop before the final corner for a large stopping distance.
Arrival is relative to Detour's projected endpoint, not the unprojected requested
point. Velocity is endpoint displacement divided by fixed delta, not instantaneous
corner speed. Limits, static geometry and movement ownership remain unchanged.
With over64 authored agents, enabled agents with a destination fail with Limit
and no stored per-agent sample. Deleted states prune
at the next tick. Recovery discards samples and reconstructs routes from semantic
configuration/translations/revisions, so the candidate starts Pending.

The existing detached presentation carries these fields under `navigation_debug`;
Live gameplay Inspector reads them without authored history changes. The exact C++
SDK adds `navigation_agent_state`/`Client::navigation_agent`, with fixed owner-thread,
Navigation permission, UUID references and copied exact-size outputs. Gameplay
runs before movement and sees the previous completed tick; PostPhysics sees this
tick. No between-tick presentation read is a new simulation sample. This changes
the private SDK fingerprint; rebuild native modules with the matching SDK.

### Query timing inside gameplay systems

Navigation captures source geometry at the writable start of a fixed tick and refreshes it after gameplay before moving agents. A query inside a read-only Flecs system uses that captured validation state and never writes derived transforms. Deferred gameplay edits become visible when Flecs merges them; subsequent agent movement validates the updated geometry. Inherited NavigationAgent components are collected through a normal Flecs query, not an ownership-only component iterator.

## Runtime source capture

Runtime synchronization copies marked and selected physical sources and the spatial ancestors needed by
the existing geometry validator directly from native Flecs values. Scene's identity
lookup restricts those values to the bound instance, including when another instance
has the same Scene AssetId. Partial prefab masks remain effective native values.
The transform owner evaluates its existing dirty epoch; navigation never authors
matrices or replaces hierarchy ownership. No full effective Scene, unrelated opaque
extensions, custom components or unselected geometry are serialized for this capture.

The retained persistent-ID query includes disabled entities and implicit prefab
children so capture matches the existing source-document validator. Scene supplies
only the relevant owned intent for removed prefab members: an unresolved enabled
NavigationSurface or selected physical body still fails the same group/static/transform checks. This narrow
projection copies no unrelated member extensions. These temporary descriptors use
the same validator and canonical v1/v2 digest paths as baking. Existing selected
Mesh leases/revisions, resource readiness and stale-generation policy are unchanged.

A normal native NavigationSurface query first checks for an active surface in the
bound Scene. With none, synchronization clears the previous digest without capturing
unrelated data. Re-enabling one returns through complete source validation; a cached
NavMesh cannot be reused against a removed or disabled source.

`NavSourceCapture` records inclusive CPU cost of the source-capture calls through
the existing Profiling service. Additional selected linked-profile views use the
same label; native obstacle tile updates remain outside this scope. Disabled profiling collects no samples. A stable transform epoch
avoids reevaluation; when transforms change, their owner's existing scene-wide
evaluation is still required. Capture benchmarks do not establish game FPS, GPU
cost or Windows acceptance.

### Editor status

The Navigation controls use the same native source capture and selected-revision
hashing for their advisory status. The editor passes its existing immutable project
asset catalog snapshot; no second catalog owner or per-status disk/index validation
is created. A snapshot replacement immediately rescans selection and revalidates,
while preserving settings drafts that differ from the previous baked settings.
Scene/settings changes and the existing250ms cadence also invalidate the advisory
result. Missing or foreign-project snapshots wait for the existing asset owner.

`NavigationStatus` exposes this CPU work through the editor profiler. Explicit bake
preparation and publication still use full frozen scene/catalog validation. The
status shortcut never publishes, cooks or decodes Mesh inputs and does not certify
that a pending build will publish successfully.


## Opt-in local avoidance

`NavigationAvoidance { enabled, horizon, shared_group, layers, mask, priority }` joins an existing NavigationAgent to
predictive local steering. It is optional and absent/disabled retains legacy path
following. Horizon is 0.25–5 seconds (default 2). Radius and height come from the
selected baked navigation profile; this does not add a competing runtime footprint.
The existing NavigationRuntime owns identity, paths, observations and lifecycle.
Flecs owns semantic configuration, and the existing transform owner applies local
translation. No full DetourCrowd agent/corridor manager or second ownership database
is introduced. Only the unchanged, attributed DetourObstacleAvoidance.cpp from the
same Recast1.6.0 pin supplies velocity sampling.

All enabled participants use one copied tick-start pose/previous-velocity set.
Default Shared group0 retains per-asset isolation: only participants on the same
NavMesh selection influence each other. Equal nonzero shared_group1–32 additionally
joins peers on other assets in the same admitted navigation plane. Same-asset peers
still interact regardless of this value. Each neighbour contributes its own baked
radius; no separate footprint/configuration owner is introduced. Spatial3D
also requires overlapping vertical height; XY ignores visual Z. Idle, paused and arrived participants remain neighbours;
disabled, deleted, invalid and unavailable participants are excluded. An agent
without NavigationAvoidance does not become a predictive obstacle. Geometry/stale
asset, physics and spatial restrictions remain in force. Avoidance additionally
requires stationary spatial ancestry; World binding ends this traversal. Moving
navigation parents cannot add another movement authority to a participating child.

Layers defaults to1, Mask toUINT32_MAX and Priority to0.5. The existing copied
Participant set includes the complete policy. Neighbor admission requires
`(own.mask & peer.layers) != 0` and `peer.priority >= own.priority` in addition to
the existing plane/profile/group/height checks. Equal priorities retain mutual
steering; higher priority ignores lower priority. Masks are asymmetric and use
all32bits. Zero Mask removes peers from that query, retaining surface walls and
physics collision. These memberships do not use the physics collision-layer
namespace. Priority is finite0..1; neither it nor membership/mask is animatable.
This adds no crowd manager, pose owner, persistent corridor or reservation system.

Steering uses the selected plane. Spatial3D Detour movement follows surface height;
XY movement preserves visual Z. A bounded
adjustment of up to 8 surface queries keeps full world displacement within the
fixed-tick speed budget on slopes/steps. An initial height offset that cannot fit
that budget is rejected: align the agent's movement anchor with the navmesh.
Routes are recomputed from displaced positions by the existing bounded path query.
The world remains bounded to 64 agents, with up to 63 copied neighbours, 32 nearby
polygons and 192 wall segments per steering query. Overflow returns Limit; upstream
silent circle/segment truncation is never accepted. Scratch is allocated lazily per
cached query and retired with that query/world. NavAvoidance exposes the steering
CPU scope through existing profiling; it does not advance during presentation reads.

This is predictive local avoidance, not physical collision response or a guarantee
against overlap/jams in dense or blocked configurations. Character movement uses
the same desired steering; actual collision remains physics-owned. Named build profiles supply their
own baked footprints through the existing Nav asset references.

The Live gameplay Inspector tunes enabled/horizon/layers/mask/priority through the existing private
property_tune transport, with owner-thread, scene/entity and world-generation checks.
It never advances simulation time or writes authored history. Invalid edits retain
the current value. Both ordinary and SDK Play use this boundary; C++ gameplay can
read/set the same reflected value through the matching installed SDK header.

Navigation recovery metadata 2 copies optional avoidance values and ownership, also
covering gameplay changes hidden by an authored prefab property mask. Recovery 1 is
accepted only for worlds without avoidance components. No solver pointers, raw Flecs
IDs or velocity-sampling scratch persist. This is ephemeral exact-runtime recovery,
not a save-game format. The installed navigation_components.hpp changes the SDK
fingerprint; rebuild native modules with the matching SDK. Existing NavigationAgent
layout and the experimental 1 C table remain unchanged. Scene/prefab versions remain
unchanged; older engines retain this unknown component but do not execute avoidance.

Shared group is authored through ordinary Inspector/property/history/prefab and
MCP operations. It is not animatable or live-tunable. Missing fields default to0
through existing reflected-member evolution; old files need no ID/format migration.
The appended native component member changes the exact SDK fingerprint; rebuild
modules against the matching kit. C callbacks and pinned dependencies are unchanged.

Nondefault Layers/Mask/Priority select private navigation recovery14; optional
area policies and retained-link filters use the same recovery document. Default
peer policies keep earlier canonical recovery profiles and authored source bytes.
Legacy recovery cannot carry the new fields, and recovery14 requires a nondefault
peer policy. Scene/Prefab codecs omit the new fields at their declared defaults;
native runtime snapshots preserve semantic changes behind Prefab property masks,
including resets to omitted defaults. No Scene, Prefab, Nav asset or callback-table
version changes. The appended native component members require the exact matching
SDK fingerprint and rebuilt project modules.


## Authored links

[ADR031](decisions/031-navigation-links.md) extends the existing Scene/build/Nav /
Query/Agent owners. `NavigationLink` is a static reflected component with enabled /
bidirectional flags, explicit plane, case-sensitive named profile, local Start/End
float3 and world attachment radius. The shared local/world shape validator admits
0.05..5 m radius, endpoints within4090 m and at least0.01 m separation. XY requires
local Z0, derives canonical world Z0 and preserves the querying actor's visual Z.
Selected source identities remain sorted; a link may share an existing floor ID.
At most32 links belong to one plane/profile. Source/ancestry and prefab rules are
those of static geometry; no second identity or transform owner is created.

Link coordinate/direction/radius fields are not animation channels. The existing
native Meta projection reads `forge.meta.no_animation` on these member entities;
detached schema/discovery projects that policy instead of inferring eligibility
from float storage alone. The annotation is applied only to this new component;
existing numeric component animation behavior is unchanged.

The capture/digest owners add `links` only for a nonempty selected list. No-link
source hashes/settings and positive older cooked formats remain unchanged. A
selected link changing on an already-included floor still changes the digest,
even though the sorted source-ID set is unchanged. Disabling a link or moving it
to another profile changes the corresponding selection; other profiles are not
poisoned by unrelated link data. Editor status passes both plane and profile to
the same native capture. Runtime validity reuses the plane's floor-only state and
adds a derived state only for selected linked profiles, bounded by the existing
8-asset cache. New writable selections capture on demand; a first read-only linked
selection waits for the next writable boundary.

Zero-height links use envelope6, or envelope7 with static collision collection,
with base `geometry_version`2/3/5 unchanged. One
metadata `links` list owns source EntityId, start/end float3, radius and direction.
The binary envelope header, catalog and runtime-package admissions accept6.
Static layout `le-polyref32-detour7-links-single-v1` admits matching off-mesh
polygon/vertex/section counts, endpoint/radius/direction/ordinal data and zero
runtime adjacency before native initialization. Both native endpoints must attach
to ground. Legacy callers without descriptors still reject off-mesh bytes.
Dynamic layout `le-cells32-tiled-links-v1` retains the existing raw-cell encoding;
the existing TileCache callback reconstructs descriptors with call-lifetime owned
builder buffers. Ordinals1..32 address the immutable sorted list; source EntityId
remains authoritative. The pinned library connects dynamic endpoints only in the
same/adjoining32-cell tile coordinates. Preflight rejects longer spans with an
explicit reposition/shorten/coarser-cell diagnostic. No dependency patch, alternate
graph or implicit format downgrade circumvents this limit.

Query's existing path owner now retains oriented link source and adjacent entry /
exit indices. Ordinary path queries delegate to it. A native straight path starting
exactly at an entry can omit its off-mesh flag; the first matching native corridor
connection recovers that boundary using admitted native endpoints. The existing
64-waypoint cap includes inserted exits. Native pointers/references do not escape.

For ordinary movement0 zero-height links, Agent state retains the active span and applies linear movement through the same
fixed-tick world-distance speed budget and sole LocalTranslation writer. Avoidance
runs on ground; active crossers are excluded from its ground-plane participant
sampling. Speed0, disabled and no-destination hold an active crossing. Retargeting
finishes the span before replanning; stopping distance does not strand an active
span or signal arrival before that replan. A dynamic query revision revalidates the
oriented connection identity and current ground attachments directly, independently
of which parallel connection a shortest-path search might choose; disconnected endpoints hold it as Blocked until usable.
Stale source/admission failure prevents movement and retains a matching held span.
Changing Nav asset, directly repositioning or removing the Agent cancels its old
continuation. This direct movement does not simulate physics jumps or moving
platforms. Movement1/3 instead delegates eligible links to the physical jump/drop
profiles above, retaining native gravity and collision ownership.

Read-only debug adds `link`: null or `{source, entry, exit}`, also for held spans.
Public Agent observation and experimental C-table layout are unchanged. The added
public component header changes the exact SDK fingerprint; native modules rebuild
with the matching SDK. Scene/prefab formats remain additive.

Ephemeral navigation recovery4 is emitted for active straight crossings. It retains
source identity and oriented endpoints alongside the existing semantic pose /
configuration/ownership and exact asset revisions. Restore validates the admitted
oriented span, revision and finite in-span world pose, then continues to the exit
and replans. Downgrades, mismatched identities/directions/endpoints/revisions and
off-span poses reject the unpublished candidate. No-link recovery2/3 behavior is
unchanged. This remains private exact-runtime recovery, not a save-game format;
SDK Play's authored restart policy remains unchanged.

Gallery140 uses an existing Patrol behavior on a dynamic two-way profile and a
separate static one-way profile. Normal Scene publication/resource preparation,
actual1200-tick movement and source-hidden relocated content export use the same
owners. Manual captures, matching SDK and platform acceptance have separate
provenance; portable logic/export tests do not establish Windows or physical GPU
acceptance.

### Authored arc traversal

[ADR049](decisions/049-navigation-link-arcs.md) adds optional
`NavigationLink.arc_height`, finite0..100m of world +Y lift. Zero is omitted from
canonical source and derived descriptors. The complete builtin authoring-default
table still declares the C++ default, including for sparse Prefab overrides;
reflected missing-value metadata serves Inspector reads. No default source or
positive old cooked profile is rewritten. Height is static and non-animatable.

Nonzero arcs use envelope8 and the same native endpoint-link tile layout, including
static/dynamic obstacle and collision-collection profiles. Versions6/7 reject arc
fields; version8 requires at least one nonzero descriptor. Query retains the
admitted height and validates the native attached horizontal span. Source and
attached endpoints must admit the bounded curve; XY retains the caller's depth.

The existing Agent writer follows `a+(b-a)*t+(0,4*h*t*(1-t),0)`. Horizontal span is
at least0.01m and curve coordinates stay within4096m. Interval lengths integrate
the affine derivative norm using a factored primitive difference; tiny heights and
short advances avoid cancellation. At most48 bisection iterations invert a
distance budget. No progress clock is added: horizontal projection derives `t`
from the current pose. Remaining distance includes arcs. The old straight path is
retained; tiny ground approaches consume budget to reach the exact arc entry.

Existing holds, post-exit retargeting, avoidance exclusion, stale-source blocking
and direct reposition cancellation remain. Active arcs revalidate oriented
identity/height against the current native query. Recovery5 is emitted only with
an active arc; it carries admitted height and rejects downgrade, changed revision,
height or off-curve pose. Straight recovery2/3/4 remains unchanged. SDK Play retains
authored restart and copied pose/velocity observation; its C table is unchanged.
The expanded public component requires matching exact-SDK modules.

Inspector, Scene intent curves, existing source/build MCP operations and Gallery218
share these owners. The Gallery has ready two-way and one-way profiles and uses
the existing Patrol behavior. These arcs prescribe navigation movement. Optional
crossing queries are described below; gravity, physics-character driving and
moving-platform attachment remain outside the profile.

### Optional link collision queries

[ADR050](decisions/050-navigation-link-collision.md) defines
`NavigationLinkCollision`, an optional reflected Agent component. Its enabled
flag, world radius, capsule straight height, clearance, exact uint32 layer mask
and sensor flag use the existing builtin/Meta/Scene/Prefab owner. Absent, disabled
and zero-mask profiles retain the old movement path. Agent, Link, observation and
C-table layouts remain unchanged; the new POD requires matching exact-SDK modules.

Explicit crossings query the existing PhysicsService. Spatial3D sweeps an upright
capsule whose bottom is clearance above the navigation foot. PlanarXY sweeps a
circle and preserves visual depth. Queries see the currently realized physics
world at the navigation-before-physics boundary; they never publish pending body
edits. No extra body, synchronization phase, movement writer or route owner exists.
Initial overlaps block. No sliding, physical response or gravity is simulated.

For [a,b], NavigationLinkCurve bounds maximum chord deviation by h*(b-a)^2.
Bounded bisection keeps that within half the configured clearance. The query
radius includes the margin and rounds outward at float precision. At most64
casts per Agent/fixed tick are admitted. Hits commit the last checked on-curve
point and report ordinary Blocked observations/diagnostics. Unavailable physics
or exhausted work fails without partially publishing movement. An existing active
crossing retains its original continuation even if a later link fails. The next
tick queries blockers again.

All six fields share live tuning, Inspector, automation/MCP and selective runtime
review. Semantic recovery6 carries collision values/ownership, including temporary
Prefab overrides, alongside link/arc data. Invalid values, changed ownership and
downgrades reject. Worlds without this component retain recovery1–5 contracts.
Gallery222/223 demonstrate both planes and straight/arced blocking; normal
content catalog/export owners carry the selected closure.

## Profile-scoped static collision collection


`NavigationSettings.collect_static_colliders`, default false, enables collection;
`collision_layers`, default `UINT32_MAX`, selects physical body layers. Both default
values are omitted from JSON, preserving the original six-field representation.
The flag admits only a boolean and the mask an unsigned32-bit integer. Each named
profile persists its own settings through normal navigation publication.

`navigation_collision_collection.hpp` owns selection for effective Scene JSON,
native capture and unresolved prefab-member intent. The selected plane requires an
enabled Static nonsensor `PhysicsBody` or `PhysicsBody2D` with a layer in the mask.
Dynamic/Kinematic, disabled, sensor and masked bodies are skipped. An explicit
navigation marker in that plane overrides collection, including `enabled=false`
as an exclusion. Marked-source behavior is otherwise unchanged. Existing active
state, Scene/prefab membership, resolved transforms, static followed ancestry,
controller and agent restrictions remain authoritative. Selected unresolved intent
fails through ordinary source validation rather than disappearing from native
capture.

XY collection reuses ephemeral collider-source obstacle descriptors and the
existing physical Box/Circle/Capsule/Tile Map contour adapters. It requires explicit
walkable regions. Spatial3D captures exactly one Box/Sphere/Capsule/Cylinder/Asset
Collider and hashes its canonical physical values instead of render geometry.
`navigation_collision_mesh` reuses `physics_debug_pose` and `prepare_physics_debug`
for native shape triangles, with a canonical Static body: mass, damping, contact
material and contact mask do not determine bake geometry. Scale is baked into local
triangles once; only the resolved rigid pose is applied afterward. No physics world
is created. Empty or truncated geometry fails the candidate.

The existing Collision resource provider admits and pins selected immutable shapes.
Build retains at most32 distinct Collision roots and a combined64MiB of Mesh/Collision
source leases, within the shared512-source/16,384-triangle bounds. Collision metadata
uses `collision_inputs` and typed `navigation.collision` Build edges. Metadata-only
staleness reads the selected revision through the same selection owner; resource
readiness uses the ordinary runtime Collision bridge. No native shape triangulation
occurs during writable synchronization. Old subscriptions are released on source
removal, profile change, Scene bind and destruction.

Envelope/catalog version7 identifies collection. Spatial7 requires a canonical
`collision_inputs` array, empty for inline-only geometry; XY7 prohibits it and retains
its existing Map provenance. Each input has a unique valid AssetId and64lowercase
hex revision. Optional authored links and static/dynamic layouts use the existing
native builders and query paths. Versions1–6 remain readable and cannot claim the
collection flag. Collection settings, physical sources and selected revisions enter
the existing digest; old explicit-only positive representations remain unchanged.
Changing included geometry/eligibility or a selected revision invalidates the
profile and holds movement until matching sources are restored or rebuilt. Recovery
retains the existing route/checkpoint owner.

Content exposes **Collect static colliders** and the shared named **Collision layers**
picker. Status uses the selected saved settings and preserves unpublished drafts.
`navigation.build` discovery/schema and MCP projection use the same fields and build
owner; no independent agent implementation exists. Component edits retain normal
validation, revision and history behavior. Build settings/publication add no Scene
Undo entry. Gallery141 contains an inline floor, editable Collision wall and Patrol
behavior with a ready NavMesh; ordinary export follows the selected resource closure.
The curated Gallery uses the supported Windows CPU Collision recipe. Its native
preparer checks the current recipe key and actual resource admission before reuse;
Windows packaging runs both Tile/collision and physical-collection preparers in one
private stage. Temporary project/cache locks are validated for links but excluded
from packaged files. CPU cross-target/export fixtures do not establish native
Windows or graphical executable acceptance.

See [ADR032](decisions/032-navigation-collision-collection.md) and the
[collection workflow](../manual/editor/navigation.md#collect-static-collision-geometry).
