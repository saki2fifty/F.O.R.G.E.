# ADR031 — Authored links in the existing navigation owner

Date:2026-11-02. Decision accepted and implemented. The affected local integration,
SDK, automation, export and software-rendering checks pass. Windows, full-editor
and physical-hardware acceptance remain pending.

## Ownership and source

Extend the existing navigation profile with bounded authored connections between
walkable parts. Scene entities retain logical link identity and local endpoints;
normal reflected data, transforms, prefab conversion and source capture own the
authoring contract. The existing build/candidate/catalog/resource/Query/Agent
owners remain authoritative. There is no parallel route graph manager, movement
writer or progress clock.

Endpoints enter the normal explicit3D or XY coordinate boundary. XY ignores source
visual depth and preserves the agent's own visual Z. Derived descriptors have
unique sorted source identities, canonical endpoint coordinates, a finite endpoint
attachment radius and travel direction. At most32links are admitted. These links
request automatic straight traversal; they do not drive physics characters,
simulate jumping or bind moving platforms.

## Native integration and compatibility

Use the exact pinned dependency's public builder parameters and path flags. Link
ordinal1..32 is native user data within one immutable selection; the admitted
source identity remains authoritative. Call-lifetime builder buffers are bounded
owned values, shared by static building and dynamic tile callbacks.

Keep old positive envelopes and no-link source/settings/cooked outputs exact.
Extend the existing envelope explicitly for nonempty link descriptors. Static
tiles must admit their off-mesh sections structurally before native initialization,
including section counts, two-vertex polygons, endpoint/ordinal/direction/radius
agreement and zero runtime storage. The legacy profile continues to reject that
data. Metadata carries the single admitted link list; dynamic raw heightfield
cells retain their existing format and reconstruct connections through the
existing per-query tile owner. Do not duplicate descriptors in an independent
authoritative binary or catalog.

The pinned native mesh connects dynamic off-mesh endpoints only within the same
or adjoining tile coordinates. Enforce this before constructing a dynamic query,
with a diagnostic to shorten/reposition the connection or choose coarser cells.
Tile width remains32cells; no silent pin change, dependency patch or second route
graph circumvents that bound. Static single-tile links retain their admitted
world/grid bounds. Unsupported spans require an explicit rejection test alongside
actual supported dynamic routes and obstacle rebuilds.

The existing native-corner path owner must preserve connection identity and
direction through route construction. Merely returning a point outside the
walkable surface is insufficient: ordinary ground-constrained movement cannot
cross that segment. Traversal must explicitly use the Agent's existing fixed-tick
speed budget, position owner and arrival policy.

## Runtime lifecycle and integration requirements

Define and verify boundary entry/exit, pause/disable/no-destination handling,
retargeting, avoidance, dynamic changes, stale selections and cancellation before
claiming runtime support. Keep continuation with the existing Agent state; private
recovery must validate any captured traversal against the exact admitted asset
and source identity. No arbitrary native state or stable ABI promise is added.
Any expanded public SDK value contract requires matching modules.

Inspector, source guides, normal history/prefabs, existing build/status operations,
automation/MCP, behavior intent, source-free export and an original playable
Gallery example belong to the same implementation batch. Both planes and supported
static/dynamic profile combinations need actual movement tests. Native metadata
or a descriptor helper alone is not completion. Full editor/Windows/physical
acceptance remains distinct from focused portable tests and software rendering.
