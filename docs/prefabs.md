# Structured prefab architecture

## Identity and persisted intent

Prefab schema 1 uses `format: forge.prefab`, `version: 1`, `asset_id: AssetId`, positive monotonic `revision`, a root `PrefabMemberId`, and an array of member definitions. Each member has a persistent UUIDv4 `id`, display `name`, `components` and (except the root) a `parent` member ID. The prefab source root is an attachment anchor: its source binding is FollowStructure, and each scene instance root owns its scene attachment. Interior definition spatial bindings use `follow_structure`, `world` or `explicit` with a member ID. Validate structural and spatial cycles before realization.

PrefabMemberId names a definition member, not a scene entity or Flecs handle. `PrefabMemberRef` explicitly pairs prefab AssetId and member ID. `prefab_member_reference` resolves through one chosen instance mapping into an EntityRef. Removed-member mappings still identify the same, now unavailable entity. Known typed codecs remap references during duplication; unknown plugin JSON is never scanned for UUID-looking strings. Spatial bindings and source3 component references use this existing instance mapping.

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

Direct source publication affecting a loaded instance establishes a scene-history boundary: scene Undo/Redo is cleared. Asset edits are not entries in scene history. New member mappings make the scene dirty and are persisted by scene save/recovery. If an interrupted unsaved scene had not persisted a newly allocated member mapping, reopening allocates that new member's first durable identity again; already saved member identities remain stable. Missing/removed members retain mappings and diagnostics rather than retargeting references. Dynamic attachments to missing structural members are unresolved through transient derived availability state.

The current editor has one loaded authored scene. This service does not claim coordinated publication across several editor sessions or multiple authored documents. It is not a generic cross-document transaction framework.

## Isolated runtime

`Scene::snapshot` embeds validated `_prefab_sources` in transient protocol/checkpoint payloads. `restore_snapshot` realizes the same structured hierarchy in the runtime WorldContext. Disk scene serialization does not embed these sources. The runtime needs neither editor UI nor authoring/asset services to realize them. Native build probes, first-fixed-tick activation checkpoints and recovery carry the same definitions and mappings. Prefab authoring is disabled during Play.

## Coordinated Apply

Phase9 introduces a narrowly coordinated Apply for admitted instance overrides.
See [ADR017](decisions/017-prefab-apply.md). PrefabLibrary prepares the source and
reconciled scene; Scene owns the single history entry and candidate ECS handoff;
SceneDocument supplies durable two-file publication through AssetFileTransaction.
Unknown component/property payloads remain on the instance. No nested-prefab,
structural Apply, Unpack or generic cross-document transaction framework is added.

The private version2 journal requires an existing prefab followed by an existing
scene as the final commit point. Version1 catalog operations retain their original
contract. Before commit, recovery restores both before revisions; after commit it
verifies both after revisions. All paths/blobs are preflighted before recovery IO.
Project opening recovers this existing journal before source scan/scene load.

Apply, Undo and Redo validate every affected instance before IO. Replay advances
source revisions monotonically and rejects external source/scene changes without
consuming history. Direct source publication still clears affected scene history.
Untitled scenes require Save As; Apply explicitly saves dirty scene edits too.


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

## Portable component references

[ADR022](decisions/022-prefab-component-references.md) introduces prefab source3.
At an admitted `entity_ref` field, its source value is null or a
`PrefabMemberRef {prefab, member}` naming this source. Scene documents and native
C++ values retain `EntityRef {scene, entity}`. Versions1/2 keep their prior meaning;
there is no automatic migration or new SDK layout. Source3 also retains source2's
expanded scale domain. Older readers reject the explicit new source version.

Create from selection converts admitted internal links, including nested structs,
arrays and vectors. It rejects external Scene targets and reserved identity-key
collisions before publication. Exactly admitted custom envelopes participate;
unknown fields and unavailable schemas remain opaque. Source editing uses a member
picker, including None and a missing target's retained identity. Draft collection
selection changes no source until the property edit commits.

Shared immutable templates decode member links as null. Scene realization resolves
only the required instance values through its existing durable member map. Source
inheritance is retained on Save despite this native materialization. Explicit whole
and property overrides remain explicit, including equal values. Native changes to
declared reference components reach recovery, including after an optional source
link is cleared; existing opaque mask data is retained. Unused null references
alone do not require an owned native component.

References to removed members keep their map entry. New instances allocate the
missing target once, without inventing a member row; the existing16384-entry map
bound still applies. Duplicate instances/scenes remap those tombstones as well.
Returning the same member ID restores each instance's own target. Rename, reorder
and source moves do not change the reference identity.

Apply converts only admitted links to members of the selected instance, before
the existing coordinated source/Scene transaction. External targets reject the
candidate without changing either durable file. Undo/Redo retains the existing
monotonic revision policy. Unrelated scalar/name Apply preserves older source
versions and their existing absolute Scene links. Transferring a non-null reference
activates conversion; an unchanged legacy link that cannot be mapped safely then
rejects the candidate rather than being guessed or silently retargeted.
Export and `load_game_scene` admit source3 through the
same catalog and checked typed dependency inspection; internal member links are
not external Scene dependencies.

Spatial and physics ownership is unchanged. Dynamic bodies still require World
space; their initial world poses are authored explicitly rather than driven by a
structural parent. Gallery63 demonstrates two portable sequence/joint instances
with independent clocks, body links and initial placements. Nested prefabs and
variants are outside this change.
