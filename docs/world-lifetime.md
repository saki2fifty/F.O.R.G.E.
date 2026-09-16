# World ownership and scene authoring

## Implemented ownership

```text
Application scope
  module/service owners (runtime Module declared first)
  EngineContext
    WorldContext
      one flecs::world
      five built-in registrations and cached schema
      scene-membership roots and scoped v1-ID maps
      internal change observer
  Scene(s) borrowing WorldContext
    opaque document fragments / authored row order
    immutable undo/redo snapshots
  SceneDocument, API sessions, editor read caches and external queries
```

Objects leave scope in reverse order. Scenes and external world users must leave before the context; callback code and services must outlive it. `Scene` has no default constructor, owns no world, and cannot be copied. `EngineContext` is a narrow application composition root, not a subsystem service locator. `WorldRole` records purpose; it does not implement a scheduler or activate future modules.

`WorldContext` creates its Flecs world and registers the current five component types once. Position retains explicit reflection-member entities and their descriptions. Internal identity, display-name and membership tags are not additional public component schemas. The internal descriptor list supplies codecs, validation limits, defaults and schema policy; field structure for schema discovery comes from Flecs Meta. No public registration SDK is introduced.

## Scene content and live authority

`Scene(WorldContext&)` allocates a scene-membership root. Each authored entity has a `SceneMember` relationship to that root. Membership and v1 ID lookup are world-owned and scoped per scene, so two scenes can contain the same v1 ID without aliasing. Closing a Scene deletes that membership's content, including generated descendants, while registrations and other memberships survive. This is an ownership primitive, not an additive-scene editor feature.

Flecs owns the supported Position, Rotation, Scale, Tint and Primitive values; display names; structural ChildOf/IsA links; and prefab state. A small authored-prefab marker distinguishes explicit v1 prefab declarations from the Prefab tag Flecs implicitly gives children. It prevents ordinary edits from inserting implicit declarations into existing files.

The Scene retains envelope/row metadata and unknown fragments, with supported component fields and live names/relations removed. `document()` merges owned live values into those fragments; `effective_document()` reads inherited values from Flecs. Unknown fields on inherited components are obtained from the owner selected by Flecs, not a JSON ancestry walk. Explicit component removal removes that component from serialization rather than resurrecting stale values. `SceneDocument` still owns disk baselines, paths, writer leases, dirty/recovery state and file generations; its source location remains temporarily unchanged.

Unsupported scene versions are rejected without changing the open scene. Unknown components and fields remain opaque JSON, including plugin payload/version fields. This phase does not introduce component-version envelopes, plugin hydration or format migration.

## Prepare, commit and history

Commands prepare a private, detached `SceneDraft`: no Flecs world, registrations, history or notifications. Each command validates its arguments and proposed scene relationships. The draft normalizes supported numeric values to existing storage precision. Limits are checked before commit. A successful batch calls `Scene::edit` once.

`Scene::replace` validates and decodes all intended built-in values before writing the world. It detaches survivors from obsolete targets, removes deleted content, creates new entities, updates changed typed components, and resolves relationships in the existing world. Unchanged typed values do not emit redundant OnSet events. Surviving authored entities retain their Flecs handles. Deleted-and-restored entities regain their v1 IDs with fresh transient handles.

A change observer tracks membership revisions, including direct native set/remove/modified operations. Mutable-reference writers must notify Flecs with modified after changing values; the current v1 host uses set. Editor caches therefore invalidate on live changes. The observer is internal bookkeeping, not an external subscription service. A batch publishes its application result/revision after commit. Failed or stale preparation produces no component events, history entry or revision change.

Undo/redo retains the existing bounded immutable document snapshots. Replaying a snapshot uses the same typed content reconciliation; it never creates/replaces a world. Scene reset/load clears history as before. No new general transaction or subscription API is supplied.

Flecs deferral is not rollback. The supported authoring boundary is single-threaded; built-in hooks must not fail or perform external effects. This code does not promise rollback of allocation failure, arbitrary throwing hooks, native crashes or external side effects.

## Effective reads and compatibility adapters

Normal Inspector, API, diagnostics and render reads use `Scene::effective_document()`. The JSON inheritance helper was removed from `geometry.hpp`. A private five-type inheritance projection remains only for detached batch intent and transient gesture previews: a pending base edit must affect a later command/preview before any live mutation. It is not stored as live state, used for normal reads, or published as a general prefab API.

Existing ChildOf prefab interiors still use Flecs instantiation. When a source prefab or its subtree changes, affected generated interiors are reconciled through Flecs while authored handles survive. Existing Prefab/IsA overrides and v1 file representation remain. Parent storage, TreeSpawner assets, stable member IDs and new prefab workflows are not introduced.

Runtime protocol 1 remains caller-stepped. Successful responses retain `scene` as the authored/owned checkpoint and add `effective_scene` as a Flecs-derived presentation snapshot. The editor renders the latter; recovery/reload still uses the former. Commands, step timing, native ABI v1 and process isolation are unchanged. The runtime declares Module before EngineContext, ensuring the world is destroyed before the final DLL unload. The existing bounded stateless v1 replacement rules remain unchanged; this is not general callback-bearing DLL reload.

## Verification

`world_lifetime` exercises surviving world/system/observer/query registrations, stable unaffected handles, restored v1 identity, scoped memberships, invalid/stale batch isolation with a redo branch, all five live codecs, native removals, unknown-field round trips, inherited/owned values and shutdown hooks/contexts. On Linux its test-only linker wrapper counts actual `ecs_init` calls and asserts zero additional worlds during ordinary commands/history; no product instrumentation API is added.

Editor tests check cached inherited reads after native writes. The D3D12 WARP fixtures now consume Flecs-derived effective snapshots through the unchanged production renderer, enabling comparison against the accepted images. Platform execution results are recorded in the dated changelog.

Remaining work: UUID/reference migration, asset identity, hierarchical transforms, structured prefab assets, fixed runtime timing, general registration/lifecycle SDKs and specialized authoring domains. SceneDraft/history still use whole-document data; there is no large-scene performance claim.
