# Navigation foundation

Phase 6E adds static navmesh generation and bounded headless path queries. Recast builds navigation geometry; Detour loads the admitted result and supplies projection, route finding and surface traversal. FORGE owns identity, authoring, lifecycle, validation, movement policy and presentation.

## Upstream selection

RecastNavigation **v1.6.0**, exact commit `6dc1667f580357e8a2154c28b7867bea7e8ad3a7`, is the latest official stable release reviewed on 2026-09-18. License: zlib. The [official release](https://github.com/recastnavigation/recastnavigation/releases/tag/v1.6.0), [documentation](https://recastnav.com/) and [pinned sources](https://github.com/recastnavigation/recastnavigation/tree/6dc1667f580357e8a2154c28b7867bea7e8ad3a7) govern integration. Current main was also reviewed; it does not supersede this tested stable pin.

Recast, Detour and its obstacle-velocity sampling source are compiled. The upstream integration guide permits directly compiling their source directories; FORGE uses private CMake targets to avoid the demo's SDL2/OpenGL dependencies and old root-CMake policy requirements. DetourCrowd supplies steering/avoidance, DetourTileCache supports obstacle-driven tile updates, and tiled meshes support larger worlds. The full Crowd manager, TileCache and sample frameworks are not integrated here. The sample's native `MSET` serialization is not FORGE's asset format.

## Authored configuration

`NavigationSurface { enabled }` marks static shapes and Mesh leaf entities for inclusion. `NavigationAgent` stores an optional typed NavMesh AssetRef, enabled/destination flags, speed, stopping distance and a world-space destination. Flecs owns both components, their reflection and component/property prefab inheritance. Scene version and persistent identity formats are unchanged. Routes, polygon references and query scratch are never scene or prefab state.

Six settings belong to a scene's generated navigation asset: radius, height, climb, slope, cell size and cell height. Successful publication preserves its AssetId and records the settings. There is one navigation asset/profile per scene in this foundation. Multiple profiles later require explicit selection; they do not require replacing AssetId.

## Geometry and coordinates

The geometry bridge uses actual FORGE CPU primitive triangles and evaluated Phase 3 WorldTransform values. Coordinates are world-anchored, +Y up, in metres; slope uses degrees. Source triangle winding is normalized using primitive normals in a navigation-only copy. Zero-area sphere-pole triangles are omitted. Renderer meshes are unchanged.

Static editable Mesh assets and imported model Mesh leaf entities are accepted through the existing cooked Mesh resource path. The root Model/group is not itself triangle geometry. Moving navmeshes and runtime regeneration remain unsupported. Included geometry must be static and have supported resolved spatial transforms. Geometry changes make the navmesh stale and require an explicit rebuild. Unrelated names, colors and agent positions do not form part of the geometry digest.

### Static Mesh profile

Bake uses LOD0, all its triangle parts, float3 POSITION and indexed or sequential vertices. Imported winding is preserved; a negative world-transform parity reverses triangle winding in a navigation-only copy. Collapsed/zero-area triangles are skipped. Built-in normal-based winding correction and old v1 hashing are unchanged. Entire Mesh admission observes the bounded profile before geometry extraction; decoded candidates share a64MiB job pool. Model-family/cook admission reuses existing immutable selection validation. An independent build job may use that same bounded ResourcePool loader; live-world runtime data belongs solely to RuntimeResources.

Points/lines, joint/weight streams, prepared joint palettes, morph targets/names and Mesh model-node skin/morph bindings are rejected. This profile conservatively rejects a Mesh with deforming node bindings even if another instance currently appears static. Mark an undeformed static Mesh instead. No source JSON/glTF/FBX parser runs in NavigationRuntime. Scene/ancestor static-physics and spatial-resolution rules apply to every accepted source. An admitted selection has one30-second preparation deadline before the existing30-second native build-worker limit; cancellation is checked between resource waits.

## Asset publication and admission

1. Capture an evaluated source snapshot and the current catalog revision.
2. Run Recast in the dedicated `forge_nav_build` process, with fixed arguments and an isolated staging directory.
3. Validate the returned envelope and exact Detour subset before runtime initialization.
4. Prove projection and a path query against the candidate.
5. Recheck geometry and catalog revisions on the authoring owner thread.
6. Move an immutable artifact into `Assets/Navigation/Generated/<AssetId>/` and atomically select it through the catalog.

Errors, cancellation or stale candidates preserve the previous catalog and artifact. The project writer lease applies to editor publication. Asset builds have their own ownership boundary: scene Undo does not undo navmesh builds. Old immutable revisions remain available; this phase does not introduce asset garbage collection.

The retained FORGENAV v1 envelope records logical asset/source-scene identity, included EntityIds, geometry digest, settings, exact Recast revision, binary-layout/subset marker, bounds and tile digest. New builds use explicit FORGENAV envelope/catalog schema2 and geometry bridge2. They record up to32 unique external Mesh AssetIds and exact immutable pool selection keys, with typed Build dependencies on the source Scene and selected Meshes. Geometry SHA256 hashes ordered source identities, evaluated affines, shape kinds/Mesh IDs and selected revisions. Source names, materials and runtime agent positions are excluded. Mesh bytes are admitted only during bake; runtime compares small descriptors and selection stamps. Selected v1 assets retain their original triangle-based digest and bytes; envelope/catalog versions must agree. Known Mesh provenance cannot be downgraded into v1, and future versions are rejected.

The catalog also verifies the entire artifact digest. Checksums establish consistency; they do not replace structural admission.

Detour's native loader trusts section counts and offsets. FORGE checks sizes, full consumption, finite bounds, counts, vertex/detail indices, convex clockwise polygons, reciprocal neighbors, detail ranges and boundary edges before initializing Detour. Only little-endian, 32-bit polygon references, one static tile, one walkable flag/area, no BV tree, no off-mesh data and fresh zeroed link storage are supported. Arbitrary Detour/sample binaries are rejected. The admitted bytes are copied into Detour-owned memory; assets shared by agents remain immutable.

## Runtime ownership

`forge.navigation` uses EngineModule lifecycle. Authoring/validation compositions receive schemas; runtime compositions provide a world-scoped Navigation service. No ImGui, renderer or audio device is required. Primitive-only compositions remain usable without a Resources provider. External Mesh geometry lazily requests builtin CPU Resources after bootstrap and holds world-scoped subscriptions; it never creates a competing runtime mesh pool. A selected Mesh load holds the agent Pending (no sample/tick0); missing providers or failed/stale selection prevents movement. Retaining last-good bytes does not authorize following a NavMesh built against a different selected revision. Provider shutdown revokes CPU leases. Synchronization reads retained data/revisions, without re-decoding cooked Mesh files on each tick. Owner-thread queries return owned FORGE waypoint arrays with explicit success, missing/stale, outside, no-path, partial, invalid, unavailable and limit statuses. Partial paths are diagnosed and not followed. Query scratch is private to the runtime world; immutable meshes are shared within its cache.

The fixed pipeline runs gameplay → navigation → physics synchronization/step/adoption → final transforms. Nonphysics agents move incrementally over the admitted surface. They change LocalTranslation only; inherited rotation and scale remain inherited. PhysicsBody agents are rejected, as are effective spatial chains through moving physics bodies. Structural parenting alone does not determine spatial ancestry; World binding ends traversal. Kinematic targets, Dynamic motion, collision response and character controllers are deferred.

Pause performs no movement; Step advances one fixed tick. Presentation reads do not advance the agent. Reconstructed worlds reload the selected asset revision and recompute paths from checkpointed scene transforms/destinations. The recovery section contains AssetId/digest pairs and bounded per-agent semantic configuration/local translation with ownership flags. This preserves gameplay changes hidden by an authored prefab property mask. Restoration occurs in an unpublished candidate world and preserves inherited rotation/scale. It contains no Detour pointers or corridors. A different selected revision rejects recovery.

## Bounded policy

| Resource | Initial bound |
| --- | --- |
| Included entities / triangles | 512 / 16,384 |
| External Mesh inputs / retained CPU data | 32 / 64 MiB |
| Per-Mesh bake profile | 32 MiB scalars, 65,536 vertices, 49,152 indices, 512 parts, 16 LODs |
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

`NavigationAgent` is available to exact-version direct-Flecs modules. `FORGE_SDK_NAVIGATION` exposes `navigation_query` for projection and path finding during an owner fixed tick. Results use a caller-owned buffer with capacity 1–64 and explicit status/count. An undersized buffer returns the limit status with no points. No Recast/Detour type crosses the boundary. SDK fingerprints include the new component/header contract; modules must be rebuilt together. The movement-only ABI1 path mentioned in the original phase record has since been retired. Generic AssetHandle remains deferred: the private AssetId cache is sufficient.

## Deferred work

Full crowd management, TileCache, dynamic obstacles, multiple agent profiles, navigation volumes/cost editing, off-mesh links, moving platforms, world streaming, character controllers, generic cooking and save-game formats are deferred. Phase 6F runtime UI is not part of this phase.

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

Runtime synchronization copies marked surfaces and the spatial ancestors needed by
the existing geometry validator directly from native Flecs values. Scene's identity
lookup restricts those values to the bound instance, including when another instance
has the same Scene AssetId. Partial prefab masks remain effective native values.
The transform owner evaluates its existing dirty epoch; navigation never authors
matrices or replaces hierarchy ownership. No full effective Scene, unrelated opaque
extensions, custom components or unmarked geometry are serialized for this capture.

The retained persistent-ID query includes disabled entities and implicit prefab
children so capture matches the existing source-document validator. Scene supplies
only the relevant owned intent for removed prefab members: an unresolved enabled
NavigationSurface still fails the same group/static/transform checks. This narrow
projection copies no unrelated member extensions. These temporary descriptors use
the same validator and canonical v1/v2 digest paths as baking. Existing selected
Mesh leases/revisions, resource readiness and stale-generation policy are unchanged.

A normal native NavigationSurface query first checks for an active surface in the
bound Scene. With none, synchronization clears the previous digest without capturing
unrelated data. Re-enabling one returns through complete source validation; a cached
NavMesh cannot be reused against a removed or disabled source.

`NavSourceCapture` records inclusive CPU synchronization cost through the existing
Profiling service. Disabled profiling collects no samples. A stable transform epoch
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

`NavigationAvoidance { enabled, horizon }` joins an existing NavigationAgent to
predictive local steering. It is optional and absent/disabled retains legacy path
following. Horizon is 0.25–5 seconds (default 2). Radius and height come from the
selected baked navigation profile; this does not add a competing runtime footprint.
The existing NavigationRuntime owns identity, paths, observations and lifecycle.
Flecs owns semantic configuration, and the existing transform owner applies local
translation. No full DetourCrowd agent/corridor manager or second ownership database
is introduced. Only the unchanged, attributed DetourObstacleAvoidance.cpp from the
same Recast1.6.0 pin supplies velocity sampling.

All enabled participants use one copied tick-start pose/previous-velocity set.
Only participants on the same NavMesh selection with overlapping vertical height
influence each other. Idle, paused and arrived participants remain neighbours;
disabled, deleted, invalid and unavailable participants are excluded. An agent
without NavigationAvoidance does not become a predictive obstacle. Geometry/stale
asset, physics and spatial restrictions remain in force. Avoidance additionally
requires stationary spatial ancestry; World binding ends this traversal. Moving
navigation parents cannot add another movement authority to a participating child.

Steering is planar; existing Detour movement follows surface height. A bounded
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
against overlap/jams in dense or blocked configurations. Dynamic obstacle carving,
multiple footprint profiles and physics character integration remain deferred.

The Live gameplay Inspector tunes enabled/horizon through the existing private
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
