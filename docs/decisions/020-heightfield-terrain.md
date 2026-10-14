# ADR 020 — Layered heightfield terrain source

Date: 2026-10-14. Accepted storage/ownership design. Local source implementation
and automated acceptance complete; no numbered delivery or Windows physical
acceptance is claimed.

Implementation checkpoint: source/editor/cook/resource/consumer integration is
present in local source. Normal and sanitizer terrain/Gallery/export suites and
scaled ImGui/native Vulkan checks and shared consumer regression review pass. See
[terrain authoring](../terrain-authoring.md) for the implemented contract.

## Decision

Extend ADR008's project-authored root Mesh with a specialized `.terrain.json`
source. The root AssetId owns sparse tiles and additive height layers. Tile and
layer UUIDs are allocated independently and scoped to that root asset. Moving,
resampling or editing retains these identities. Duplicating an asset allocates a
new root AssetId; its unchanged local UUIDs belong to the copied root and do not
refer to the original. Tile coordinates and transient cooked part indices are
not durable identities.

Version1 `forge.heightfield`, profile `heightfield_mesh1`, stores asset-local
meters with +Y up, a common tile size, a common samples-per-side resolution and
per-layer height/mask grids. Layer strength and masks are in [0,1]; enabled layers
compose additively. Every shared sample must agree in every raw layer and mask,
including disabled layers and diagonal corners. Source edits must preserve this
agreement or reject atomically. Opaque root extensions survive supported edits.

The bounded initial profile permits eight tiles, three layers, sides2–129,
tile sizes1–1000m, integer coordinates±16, raw/composed heights±10000m and source
files up to8MiB. Source sampling projects to float mesh vertices. Scene placement
uses existing double transforms. Height queries follow the actual cooked triangle
surface, including float coordinate rounding; they do not substitute a bilinear
surface with different collision heights.

## Derived data and existing owners

Cook through the existing source document, candidate, immutable derived cache
and AssetCatalog publication flow. Publish one MeshData with one part per tile,
one LOD and the existing stable `default` material binding. Shared-coordinate
normals and continuous UVs preserve borders. MeshRenderer, resource leases,
Inspector material overrides, picking, mesh collision, navigation input and export
retain their current ownership. Runtime packages load cooked mesh geometry and
do not load terrain source grids or source editor code.

Preview uses the shared renderer in an isolated source-document host. Draft data
may override its private selected mesh revision only; it is not a catalog and
cannot override ordinary game hosts. Source history owns complete recorded strokes
and resolution/tile/layer edits. The terrain source does not own a second world,
per-sample ECS entities, runtime simulation, material database or resource manager.

## Boundaries and consequences

This is a single-valued heightfield mesh. It does not represent overhangs, volumes,
holes, streaming worlds, spline tools, water or foliage. Existing triangulated
static mesh collision is the admitted physics path; this decision does not claim
an optimized native heightfield collider. A new terrain importer/recipe must be
explicitly admitted without weakening the editable-Mesh or Model importer checks.
The public native SDK ABI and scene ownership do not change.
