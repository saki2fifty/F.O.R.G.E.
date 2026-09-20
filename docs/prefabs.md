# Structured prefab architecture

## Identity and persisted intent

Prefab schema 1 uses `format: forge.prefab`, `version: 1`, `asset_id: AssetId`, positive monotonic `revision`, a root `PrefabMemberId`, and an array of member definitions. Each member has a persistent UUIDv4 `id`, display `name`, `components` and (except the root) a `parent` member ID. The prefab source root is an attachment anchor: its source binding is FollowStructure, and each scene instance root owns its scene attachment. Interior definition spatial bindings use `follow_structure`, `world` or `explicit` with a member ID. Validate structural and spatial cycles before realization.

PrefabMemberId names a definition member, not a scene entity or Flecs handle. `PrefabMemberRef` explicitly pairs prefab AssetId and member ID. `prefab_member_reference` resolves through one chosen instance mapping into an EntityRef. Removed-member mappings still identify the same, now unavailable entity. Known typed codecs can remap references during duplication; unknown plugin JSON is never scanned for UUID-looking strings. The existing explicit spatial-binding codec is the current reference-bearing consumer.

Scene schema 4 is introduced when structured instances are created. A root stores `prefab_instance: {asset, revision, members: {memberUUID: entityUUID}, status}`. Addressable member rows store `prefab_member: {root: rootEntityUUID, member: memberUUID}`, authored override intent and resolved structural/name metadata. The map survives member removal; retained rows have `missing_member: true`. All authored entity UUIDs, including instance roots and mapped children, are unique within the scene. Duplicate instances allocate new entity IDs; duplicate assets allocate both a new AssetId and new member IDs. Whole-scene duplication remaps every instance member and known intra-scene reference.

Instance root names follow their source until an explicit `name_override` is authored; rename/duplicate can set it and `prefab.revert_name` removes it.

`components` on instance rows contains whole-component overrides. `property_overrides` records explicit built-in scalar field intent separately, including equal-value writes. Color and primitive fields support property masks; local TRS channels are independent atomic components. Materialized Flecs values combine current source fields with explicit masks. Serialization retains the mask, not the materialized inherited values. Revert removes intent and rebuilds/removes the owned component. Ordinary scene history owns instance creation, duplication, edits, deletion and Revert.

No inherited values, WorldTransform, Flecs IDs, table IDs or TreeSpawner pointers are durable scene authority. Unknown asset/member/component/field data is preserved where its owner survives. Create from selection rejects nested structured instances and legacy prefab constructs instead of flattening them implicitly. Legacy scene-local `base`/ChildOf prefabs remain unchanged. Opening legacy scenes performs no automatic prefab conversion or asset creation.

## Flecs realization and lifetime

`CompiledPrefab` compiles immutable, world-local Prefab/IsA template revisions. Structured interiors use the documented `world.entity(flecs::Parent{parent})` constructor. Direct `.set<Parent>` addition can expose an uninitialized parent to OnAdd observers and is deliberately avoided. Dynamic scene attachments and instance roots use ChildOf where applicable. A member never has both storages. Shared logical parent traversal uses Flecs parent/children APIs, which support both storages.

Every live structured member inherits from its corresponding template member. Independent local translation/rotation/scale ownership remains native Flecs ownership. WorldTransform is derived, instance-owned and non-inherited. Persistent member rows address actual Flecs children rather than an editor-only shadow hierarchy.

Templates are shared across instances in a loaded scene. They live through all instances using them. Pinned Flecs 4.1.6 retains TreeSpawner caches that guard structural template deletion even after instances are gone. Retirement clears EcsTreeSpawner only after all using instances/candidates retire, then deletes the private template hierarchy. No live populated template is structurally edited.

Official pinned references: [hierarchy storage and mixed traversal](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/docs/HierarchiesManual.md), [tree spawner implementation](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/src/tree_spawner.c), and [entity parent lookup](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/src/entity.c).

## Single-asset publication

`PrefabLibrary` provides UI-independent create, duplicate, refresh, source read and publish operations. It uses the existing AssetCatalog/type validation, bounded project scans and project-relative paths. Source payload identity is authoritative; the catalog is rebuilt from recognized `.prefab.json` files, so a move does not change identity. Callers serialize access on the scene owner thread and hold the project writer lease. The editor checks its lease before file mutations.

Publication validates the complete candidate, compiles a new immutable template and realizes a temporary candidate hierarchy in the existing WorldContext. No simulation, presentation or application notification runs against candidates. All persistent mapping, transforms, override projection, parent attachments and commit actions are prepared before durable replacement. Unrelated surviving scene entities retain their Flecs handles. Affected instance/member handles may change; their persistent EntityIds and EntityRefs do not.

The atomic single-file writer runs after preparation and before activating prepared handles. A validation, compilation, reconciliation or file-write failure destroys only candidates and preserves source, current instances, scene revision and history. The owning thread then hands off prepared content without fallible I/O or parsing, retires old instance/template state and invalidates read caches. Native allocation failures or crashing trusted callbacks are process failures, not recoverable transaction errors. After interruption, the complete old or new source file is loaded and the scene's retained override intent is reconciled; no claim is made that source and scene files were saved atomically together.

Successful publication affecting a loaded instance establishes a scene-history boundary: scene Undo/Redo is cleared. Asset edits are not entries in scene history. New member mappings make the scene dirty and are persisted by scene save/recovery. If an interrupted unsaved scene had not persisted a newly allocated member mapping, reopening allocates that new member's first durable identity again; already saved member identities remain stable. Missing/removed members retain mappings and diagnostics rather than retargeting references. Dynamic attachments to missing structural members are unresolved through transient derived availability state.

The current editor has one loaded authored scene. This service does not claim coordinated publication across several editor sessions or multiple authored documents. It is not a generic cross-document transaction framework.

## Isolated runtime

`Scene::snapshot` embeds validated `_prefab_sources` in transient protocol/checkpoint payloads. `restore_snapshot` realizes the same structured hierarchy in the runtime WorldContext. Disk scene serialization does not embed these sources. The runtime needs neither editor UI nor authoring/asset services to realize them. Native build probes, first-fixed-tick activation checkpoints and recovery carry the same definitions and mappings. Prefab authoring is disabled during Play.

## Deliberately deferred Apply

There is no Apply button, command, advertised API capability or partial Apply workflow. Future Apply must define and test coordinated prefab-plus-scene ownership, publication order, interruption recovery, Undo/Redo, later source edits, dirty/untitled scenes and conflicts. The candidate/revision boundary is reusable; no future cross-document history format is frozen here. Nested overrides, structural per-instance edits, Unpack, resource handles/import/cook and general plugin/gameplay SDKs remain deferred.

## Expanded scale compatibility

Prefab2 retains the same member identity, revision, structure and override model
as prefab1. Its version permits signed, zero and tiny LocalScale values. Source
candidates automatically promote only when known scale needs the expanded range;
positive-only documents remain prefab1. Publication validates and reconciles the
normalized candidate before writing it, updates catalog schema metadata, and
retains the previous source/instances on failure. Scene-owned signed/zero scale
overrides use scene5 and remain independently undoable/revertible. Unknown payloads
are untouched; direct source publication still has its existing separate history
policy and is not an Apply operation.
