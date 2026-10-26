# ADR026 — Explicit navigation planes in the existing owner

Date:2026-10-26. Local implementation; native Windows acceptance is pending.

## Decision

Extend NavigationRuntime, NavigationCandidate, NavigationTools, AssetCatalog and
Mesh/Query with an explicit Spatial3D/default or PlanarXY profile. Keep one logical
navigation asset/profile per Scene. Changing plane is an explicit rebuild which
preserves AssetId and prior immutable artifacts. No second navigation service,
asset database, transform writer or agent-specific authoring implementation exists.

`NavigationRegion2D` supplies centered rectangular walkable areas and obstacle
masks. Its resolved affine contributes world XY; visual depth is independent.
Sprite/collider/TileMap extraction and arbitrary polygons are separate capabilities.
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

The finite profile excludes automatic art/collision extraction, platformer/off-mesh
links, dynamic rebaking, multiple profiles per Scene, cost editing and streaming.
It does not imply parity with another engine or claim a measured performance gain.
See [Navigation](../navigation.md) for the current contract and bounds.

## Scene interaction refinement

Region outlines/picking now use the existing SpatialHelpers generation/policy cache;
NavigationTools still owns overlay/bake controls. The pure footprint is shared by
bake/view/framing/handles. SceneTools adds a transient centered extent gesture via
ordinary preview/property/Undo, and projected-axis drag math is shared with rig
targets. There is no new source format, SDK shape, navigation owner or agent-only
workflow. Existing generic automation properties retain complete semantic access.
