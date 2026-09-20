# Navigation foundation

Phase 6E adds static navmesh generation and bounded headless path queries. Recast builds navigation geometry; Detour loads the admitted result and supplies projection, route finding and surface traversal. FORGE owns identity, authoring, lifecycle, validation, movement policy and presentation.

## Upstream selection

RecastNavigation **v1.6.0**, exact commit `6dc1667f580357e8a2154c28b7867bea7e8ad3a7`, is the latest official stable release reviewed on 2026-09-18. License: zlib. The [official release](https://github.com/recastnavigation/recastnavigation/releases/tag/v1.6.0), [documentation](https://recastnav.com/) and [pinned sources](https://github.com/recastnavigation/recastnavigation/tree/6dc1667f580357e8a2154c28b7867bea7e8ad3a7) govern integration. Current main was also reviewed; it does not supersede this tested stable pin.

Only Recast and Detour are compiled. The upstream integration guide permits directly compiling their source directories; FORGE uses private CMake targets to avoid the demo's SDL2/OpenGL dependencies and old root-CMake policy requirements. DetourCrowd supplies steering/avoidance, DetourTileCache supports obstacle-driven tile updates, and tiled meshes support larger worlds. Those libraries and sample frameworks are not integrated here. The sample's native `MSET` serialization is not FORGE's asset format.

## Authored configuration

`NavigationSurface { enabled }` marks static primitives for inclusion. `NavigationAgent` stores an optional typed NavMesh AssetRef, enabled/destination flags, speed, stopping distance and a world-space destination. Flecs owns both components, their reflection and component/property prefab inheritance. Scene version and persistent identity formats are unchanged. Routes, polygon references and query scratch are never scene or prefab state.

Six settings belong to a scene's generated navigation asset: radius, height, climb, slope, cell size and cell height. Successful publication preserves its AssetId and records the settings. There is one navigation asset/profile per scene in this foundation. Multiple profiles later require explicit selection; they do not require replacing AssetId.

## Geometry and coordinates

The geometry bridge uses actual FORGE CPU primitive triangles and evaluated Phase 3 WorldTransform values. Coordinates are world-anchored, +Y up, in metres; slope uses degrees. Source triangle winding is normalized using primitive normals in a navigation-only copy. Zero-area sphere-pole triangles are omitted. Renderer meshes are unchanged.

No imported mesh pipeline, moving navmesh or runtime regeneration is added. Included geometry must be static and have supported resolved spatial transforms. Geometry changes make the navmesh stale and require an explicit rebuild. Unrelated names, colors and agent positions do not form part of the geometry digest.

## Asset publication and admission

1. Capture an evaluated source snapshot and the current catalog revision.
2. Run Recast in the dedicated `forge_nav_build` process, with fixed arguments and an isolated staging directory.
3. Validate the returned envelope and exact Detour subset before runtime initialization.
4. Prove projection and a path query against the candidate.
5. Recheck geometry and catalog revisions on the authoring owner thread.
6. Move an immutable artifact into `Assets/Navigation/Generated/<AssetId>/` and atomically select it through the catalog.

Errors, cancellation or stale candidates preserve the previous catalog and artifact. The project writer lease applies to editor publication. Asset builds have their own ownership boundary: scene Undo does not undo navmesh builds. Old immutable revisions remain available; this phase does not introduce asset garbage collection.

The FORGENAV v1 envelope records logical asset/source-scene identity, included EntityIds, geometry digest, settings, exact Recast revision, binary-layout/subset marker, bounds and tile digest. The catalog also verifies the entire artifact digest. Checksums establish consistency; they do not replace structural admission.

Detour's native loader trusts section counts and offsets. FORGE checks sizes, full consumption, finite bounds, counts, vertex/detail indices, convex clockwise polygons, reciprocal neighbors, detail ranges and boundary edges before initializing Detour. Only little-endian, 32-bit polygon references, one static tile, one walkable flag/area, no BV tree, no off-mesh data and fresh zeroed link storage are supported. Arbitrary Detour/sample binaries are rejected. The admitted bytes are copied into Detour-owned memory; assets shared by agents remain immutable.

## Runtime ownership

`forge.navigation` uses EngineModule lifecycle. Authoring/validation compositions receive schemas; runtime compositions provide a world-scoped Navigation service. No ImGui, renderer or audio device is required. Owner-thread queries return owned FORGE waypoint arrays with explicit success, missing/stale, outside, no-path, partial, invalid, unavailable and limit statuses. Partial paths are diagnosed and not followed. Query scratch is private to the runtime world; immutable meshes are shared within its cache.

The fixed pipeline runs gameplay → navigation → physics synchronization/step/adoption → final transforms. Nonphysics agents move incrementally over the admitted surface. They change LocalTranslation only; inherited rotation and scale remain inherited. PhysicsBody agents are rejected, as are effective spatial chains through moving physics bodies. Structural parenting alone does not determine spatial ancestry; World binding ends traversal. Kinematic targets, Dynamic motion, collision response and character controllers are deferred.

Pause performs no movement; Step advances one fixed tick. Presentation reads do not advance the agent. Reconstructed worlds reload the selected asset revision and recompute paths from checkpointed scene transforms/destinations. The recovery section contains AssetId/digest pairs and bounded per-agent semantic configuration/local translation with ownership flags. This preserves gameplay changes hidden by an authored prefab property mask. Restoration occurs in an unpublished candidate world and preserves inherited rotation/scale. It contains no Detour pointers or corridors. A different selected revision rejects recovery.

## Bounded policy

| Resource | Initial bound |
| --- | --- |
| Included entities / triangles | 512 / 16,384 |
| Geometry coordinate magnitude | 4,090 metres; generated bounds within 4,096 |
| Horizontal grid | 512 × 512 cells |
| Vertical voxel span | Less than 8,191 cells |
| Polygons / vertices | 4,096 / 16,384 |
| Native tile / metadata | 4 MiB / 64 KiB |
| Active world cache | 8 assets, at most 32 MiB tile bytes |
| Authored agents | 64 |
| Query nodes / corridor / waypoints | 2,048 / 128 / 64 |
| Worker | 512 MiB, 30 seconds wall time, 32 MiB total staged files |

Exceeding these bounds returns a diagnostic rather than silently truncating a path or publishing a partial build. These limits are a small-scene foundation, not a large-world performance claim.

## Exact gameplay SDK

`NavigationAgent` is available to exact-version direct-Flecs modules. `FORGE_SDK_NAVIGATION` exposes `navigation_query` for projection and path finding during an owner fixed tick. Results use a caller-owned buffer with capacity 1–64 and explicit status/count. An undersized buffer returns the limit status with no points. No Recast/Detour type crosses the boundary. SDK fingerprints include the new component/header contract; modules must be rebuilt together. Legacy gameplay ABI1 is unchanged. Generic AssetHandle remains deferred: the private AssetId cache is sufficient.

## Deferred work

Crowd avoidance, TileCache, dynamic obstacles, multiple agent profiles, navigation volumes/cost editing, off-mesh links, moving platforms, world streaming, character controllers, generic cooking and save-game formats are deferred. Phase 6F runtime UI is not part of this phase.

See the [Navigation user guide](../manual/editor/navigation.md) for the no-code workflow.

### Query timing inside gameplay systems

Navigation captures source geometry at the writable start of a fixed tick and refreshes it after gameplay before moving agents. A query inside a read-only Flecs system uses that captured validation state and never writes derived transforms. Deferred gameplay edits become visible when Flecs merges them; subsequent agent movement validates the updated geometry. Inherited NavigationAgent components are collected through a normal Flecs query, not an ownership-only component iterator.

## Empty navigation geometry

Runtime synchronization uses a retained native Flecs query for effective enabled
NavigationSurface components before copying the scene for geometry extraction.
With no enabled surfaces, it clears the previous digest and reports unavailable
source geometry without serializing unrelated scene data. Re-enabling a surface
returns through the existing complete geometry/hierarchy validation; a cached
NavMesh cannot be reused against a removed or disabled source. This optimization
does not skip validation when any enabled surface exists.
