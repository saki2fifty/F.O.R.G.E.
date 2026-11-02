# ADR026 — Explicit navigation planes in the existing owner

Date:2026-10-26. Local implementation; native Windows acceptance is pending.

## Decision

Extend NavigationRuntime, NavigationCandidate, NavigationTools, AssetCatalog and
Mesh/Query with an explicit Spatial3D/default or PlanarXY profile. Retain the legacy
default navigation asset and admit explicit named build profiles as refined below.
Changing plane is an explicit rebuild within the selected profile which
preserves AssetId and prior immutable artifacts. No second navigation service,
asset database, transform writer or agent-specific authoring implementation exists.

`NavigationRegion2D` supplies centered rectangles or bounded simple convex/concave
polygon walkable areas and obstacle masks. Its resolved affine contributes world XY; visual depth is independent.
Sprite artwork remains independent. Explicit Collider 2D input is described in the additive refinement below.
Use the pinned Recast public convex-area API before radius erosion. Shared source
capture retains static/controller/spatial ancestry validation in either plane.

Mesh/Query owns one coordinate boundary: XY maps to native XZ. Flat native altitude
is derived from admitted tile vertices, never stored as another authored value.
Projection/path/movement preserve visual Z, while copied velocity has Z0. XY
avoidance ignores visual depth. Runtime keeps independent derived source validity
for each plane so unrelated source errors cannot invalidate the selected profile.
External Mesh resource subscriptions remain owned solely by the spatial profile.

## Compatibility and admission

Existing spatial settings keep their six-field JSON representation. New XY settings
add `plane:1`. Spatial envelope/catalog1/2 semantics and source digests are retained;
XY uses envelope/catalog/geometry3 with explicit plane and no Mesh inputs. A shared
provenance validator rejects mismatched basis/version combinations. Bounded tile
validation checks flatness before native initialization. Checksums prove consistency,
not safe structure or authority. Existing assets need no migration or ID change.

Public component headers change the exact SDK fingerprint; native modules rebuild
against the matching kit. NavigationAgent's layout and the callback table do not
change. The recovery-supporting runtime path already binds exact asset revisions in
recovery2 and recomputes paths through
the admitted basis, so it needs no parallel format or new state authority.
Production SDK Play retains its clean authored restart policy and rejects recovery
requests; this does not add arbitrary native-state migration.

## User and automation consumers

NavigationTools exposes Plane and relevant controls, selected-region guides and
the existing bake/status/cancel owner. Ordinary component reflection supplies
Inspector, prefab/property history and saving. Automation/MCP adds optional plane
selection to that same owner; no workflow is reimplemented for agents. The original
Gallery80 Scene exercises route following, local avoidance, ordinary Sprites and
an orthographic Camera. Cook/export uses normal runtime navigation admission.

## Evidence and limits

Focused native tests cover masks/holes/reflection, mirrored/zero-depth sources,
metadata/worker rejection, staleness/cancellation, movement/avoidance/progress,
fresh recovery and relocated cooked assets. Actual ImGui input covers plane
selection/readiness/Play locking at100/150/200%; both MCP protocols exercise the
native owner. The installed SDK sample compiles against installed headers and runs
from a relocated kit. These checks do not establish native Windows/GPU acceptance.

The finite profile excludes automatic art/collision inclusion, platformer/off-mesh
links, dynamic rebaking, cost editing and streaming.
It does not imply parity with another engine or claim a measured performance gain.
See [Navigation](../navigation.md) for the current contract and bounds.

## Scene interaction refinement

Region outlines/picking now use the existing SpatialHelpers generation/policy cache;
NavigationTools still owns overlay/bake controls. The pure footprint is shared by
bake/view/framing/handles. SceneTools adds a transient centered extent gesture via
ordinary preview/property/Undo, and projected-axis drag math is shared with rig
targets. There is no new source format, SDK shape, navigation owner or agent-only
workflow. Existing generic automation properties retain complete semantic access.

## Polygon contour refinement

An empty native owning point vector retains the exact rectangle geometry identity
and cooked representation. A nonempty3–64point finite simple CCW local contour
uses the same region/source/bake owners and envelope3 basis; dynamic layers retain
envelope4. Deterministic walkable triangulation and convex obstacle decomposition
reuse FORGE-owned pure polygon geometry, without changing tile-specific admission.
Unused rectangle extents do not enter polygon identity. Visual-Z and static source
rules are unchanged; no AssetId migration is required.

Inspector seeds or clears a complete contour through existing property/history
operations. Guides, picking and framing consume all outline edges; rectangle resize
handles only apply to empty contours. Prefab and automation/MCP use ordinary
reflected collection values. The new native vector changes the exact SDK
fingerprint and requires a matching module rebuild; the C callback table is stable.

## Explicit collision-source refinement

`NavigationRegion2D.source` is appended after Points: 0 retains the exact old
rectangle/polygon behavior; 1 selects exactly one configured Collider 2D on that
entity. It uses the existing source/candidate/asset/history/runtime ownership.
Physics dimensions, quaternion projection, compound shape admission and Tile
collision projection are shared pure FORGE values; baking creates no physics world.
Box/Circle/Capsule inputs use physical native rounding and role-conservative
contours. Selected Tile Map collision pieces use the existing published Map/Set /
neighbor-rule owners, including cell reflection, concavity, islands and holes.
No sprite artwork is inferred and no global collider scan is introduced.

This opt-in XY source profile uses geometry5 and static envelope/catalog5;
dynamic obstacle layers retain envelope4 with geometry5. Separate `tile_inputs`
bind selected Map root revisions and Build edges; their existing Tile Set
revision dependencies stay authoritative. Default geometry3 digests/artifacts and
logical AssetIds remain unchanged. A rebuild preserves previous immutable artifacts.
Old source fields default through existing native/reflected metadata; the appended
component value changes the exact SDK fingerprint and requires matching modules,
without a C callback-table or dependency-version change.

Runtime reads current Map snapshot revision/readiness and edited status through
the existing Tile owner. It does not triangulate, hash cells or rebake each tick.
An edited selected Map invalidates the static route until its published cells are
reset. Candidate readiness includes navigation-only Map instances. Existing
recovery revision rules apply; unsupported stale source geometry must be repaired
or restarted before its baked route can be followed.

Detached editor views carry separate contours. Existing SpatialHelpers and
Viewport framing consume them; no bridge edges join islands. Published Map views
share the render host's CPU pools, with at most32 readonly LRU selections and
revision/readiness invalidation. Invalid/loading/stale inputs supply no invented
footprint. Ordinary reflected properties provide Inspector/prefab/history and
agent access. Rectangle handles remain exclusive to source0 with empty Points.

## Named build profile refinement

Explicit named build profiles extend the original one-asset-per-Scene restriction.
The runtime already owns independent queries and immutable Mesh selections by
NavMeshAsset ID. The builder and editor now use the same derived catalog lookup
keyed by source Scene and `NavigationSettings.profile`; no profile database,
runtime service or alternative agent reference is added. Duplicate names in a
Scene reject before asset selection or publication. Names are case-sensitive
ASCII tokens of 1–64 characters, beginning with a letter, then letters/digits/`_`/`-`.

`default` is the legacy key and is omitted from settings JSON. Old asset IDs,
six-field settings and default cooked data need no migration. A rebuild, including
a plane change, replaces only the selected profile's ordinary Nav revision and
preserves its ID/prior artifacts. A new name publishes another ordinary asset;
it is not a rename. Agents, prefabs, history, recovery and export retain typed
NavMeshAsset references. Envelope versions, geometry provenance and runtime
eight-asset/32MiB query-cache bounds do not change.

NavigationTools owns selection and unpublished settings drafts. Profile selection
loads saved settings/triangles before changing the current view; actual publication
still requires Build NavMesh. Content/pickers identify the profile name. Runtime
overlay paths are filtered by the selected asset so different planes are not
projected through one view's basis. Automation/MCP status, select and build use
the same owner, permissions, project checks and Play lock. This deliberately
supersedes only the earlier single-profile limit, as required by the accepted
multiple-profile capability; the other explicit-source/runtime limits remain.

Shared avoidance is an explicit opt-in refinement, preserving the original
per-NavMesh default. NavigationAvoidance.shared_group0 keeps different assets
isolated; equal nonzero values1–32 also admit same-plane peers across profiles.
Existing same-asset peers still interact. Each copied neighbour carries its own
admitted baked radius. Incompatible planes remain separate, and the existing
64-agent/63-neighbour bounds and query ownership remain. The optional appended
member defaults to0 for legacy data, changes the exact native SDK fingerprint,
and requires matching module rebuilds without changing C callbacks or scene/Nav
formats. Existing asset-isolation tests remain authoritative for default0.
