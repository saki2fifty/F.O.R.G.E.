# Persistent identity and minimal asset metadata

## Ownership and reference types

Core owns `EntityId`, `AssetId`, `PersistentEntityId` and `EntityRef`. All use UUIDv4 generated from system cryptographic randomness. `EntityRef` serializes as `{ "scene": "<AssetId>", "entity": "<EntityId>" }`. These are identifiers, not loaded-resource handles. Missing targets retain their exact identity; no name/path matching is used.

A Scene borrows a long-lived WorldContext. The context owns membership-scoped maps from EntityId to transient Flecs handles. Each authored entity owns a DontInherit PersistentEntityId component: Flecs neither inherits nor copies prototype identity. Ordinary authoring preserves surviving handles; delete/undo may change the transient handle while restoring the same EntityId. Identity components are managed by scene reconciliation, not arbitrary native mutation APIs.

`WorldContext::resolve` distinguishes Available, Missing (loaded scene, absent entity), Unresolved (scene not loaded), and Ambiguous (multiple loaded memberships with that AssetId). An explicit transient membership scope resolves one loaded copy. `reference(handle)` provides reverse resolution for live authored entities. Session tokens, revisions, WorldContext and loaded memberships are not persisted document identities.

Subtree duplication creates new IDs and remaps known parent/base/spatial references inside the copied set. External links remain unchanged. Whole-scene duplication creates a new AssetId and new EntityIds, remaps known internal structural and spatial references, and remaps the explicit legacy compatibility table. `remap_entity_ref` serves known typed reference fields only; it does not scan arbitrary JSON. SpatialBinding contains a typed EntityRef for Explicit targets; only this known field is remapped. Unknown payload internals are copied unchanged; no plugin reference repair is claimed.

## Asset service

`forge_assets` depends on `forge_asset_build` and core. Core remains independent of the catalog; runtime subsystems may use it. These services do not link editor UI or a graphics backend. `AssetCatalog` is project scoped and explicitly populated; no importer, background scanner, resource loader or renderer is involved.

Records contain AssetId, type, project-relative source locator, source schema version, and known AssetId dependencies. `AssetRef<T>` serializes only its AssetId; the consuming C++ type supplies the expected type. SceneAsset and PrefabAsset are the original type tags; later subsystem tags use the same reference contract.

The version-2 metadata JSON stores these records and optional typed dependency edges; version 1 remains readable without automatic disk writes. The first explicit save retains the v1 bytes as `.v1.backup`. Loading prepares a candidate and swaps only after validation. Duplicate IDs/locators and escaping source paths are rejected. Scene records can be rebuilt from their embedded scene-v2/v3/v4 identity; for other source formats the saved metadata record is the identity authority and must be kept in source control. Do not treat that metadata as a disposable cache. The typed graph, build-input digest and cache APIs below provide infrastructure; discovery, watching and automatic rebuild are not connected yet.

Resolution returns Available, Missing, Unresolved or Incompatible with diagnostics. Available means the metadata matches the expected type and a source file exists, not that a runtime resource has loaded. For scene records the resolver additionally validates source schema and embedded AssetId, so a different scene placed at the same path cannot silently satisfy a reference. `relocate` validates the new locator before updating the record; IDs stay unchanged. Missing records/sources do not bind similarly named files. Prefab resolution also validates the source document identity and schema.

The Content panel browses registered assets and discovers saved scenes; see the user manual for its implemented workflows and scan limits. A general import/cook pipeline and typed runtime ResourceLease consumers remain Phase7 work in progress. No additional persistent DocumentId or runtime SceneInstanceId is introduced.

## Official implementation references

- [RFC 9562, UUIDv4](https://www.rfc-editor.org/rfc/rfc9562.html#section-5.4): byte layout, variant/version bits and standard textual form.
- [Microsoft BCryptGenRandom](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptgenrandom): system-preferred randomness on Windows.
- [Linux getrandom](https://man7.org/linux/man-pages/man2/getrandom.2.html): checked interruption/partial-read handling.
- [Pinned Flecs 4.1.6 component traits](https://github.com/SanderMertens/flecs/blob/fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8/docs/ComponentTraits.md#dontinherit): identity excluded from copying/inheritance. No dependency upgrades.

## Phase6D animation extension

Records now optionally preserve a bounded `metadata` object. Animation source,
SkeletonAsset and AnimationClipAsset use the same AssetCatalog and persistent IDs.
A versioned animation provenance object identifies the source, exact converter/settings,
content digests and exact clip-to-skeleton binding. Catalog publication selects the whole
validated candidate set last. This subsystem-specific bridge does not introduce a
second database, generic AssetHandle or full importer/cooker. See [Animation](animation.md).

## NavMesh assets

`AssetRef<NavMeshAsset>` uses expected type `navmesh`. Rebuilding a scene's logical navmesh retains its AssetId while selecting a new immutable artifact path. Provenance binds it to the source scene AssetId, included EntityIds, geometry/settings and exact library/subset. Candidate publication verifies current source/catalog state; a failed build never selects partial data. A duplicated logical scene needs a new navmesh built for its own identity. Runtime paths and polygon refs have no persistent identity. See [Navigation](navigation.md).

## Runtime UI assets

A schema-1 `ui_document` catalog record gives RML a durable AssetId. UiDocument stores its typed reference plus enabled/visible/layer. Supporting RCSS/fonts/TGA use bounded project-contained locators; they are not a generic texture/font asset pipeline. DOM instances and command generations are transient. See [Runtime UI](runtime-ui.md).


## Typed dependencies and derived cache

`AssetCatalog` owns the typed dependency graph and reverse index. Source/build/subasset
edges constrain build order and reject cycles with an identity path. Runtime/optional
reference cycles remain representable; visited-state bounds invalidation traversals.
Existing untyped dependency lists migrate as explicitly `legacy-untyped` runtime
edges; no build/type semantics are guessed. New importers provide expected type,
role, dependency kind and revision. The legacy target list is a compatibility
projection, validated against typed edges. Failed edits preserve both indexes.

Raw source files are indexed in the **same graph** without inventing logical
AssetIds for them. Each catalog record contributes its primary source with reserved
role `forge.primary`; importers add project-relative include/buffer/image edges with
roles and captured digests. Source referrers and transitive invalidation use that
shared reverse index. Windows locator comparison is ordinal and case-insensitive;
Linux retains distinct case-sensitive locators. Containment is validated when
records enter the catalog. Graph document2 reads document1, while catalog-v2 gains
an optional `source_dependencies` list. Rejected updates retain the old graph.

Full catalog loading uses a detached batch candidate: normalize individual records,
index locators/OS file identities, validate cross-record types and the complete
graph, then swap once. It avoids repeated whole-graph copies and pairwise filesystem
comparisons during load. Missing files still retain logical registrations. Duplicate
locators/hardlinks, wrong target types and cycles reject the entire candidate.
Incremental single-record edits retain their existing candidate-copy behavior.

The shared index byte limit is64MiB, with100k records,64 JSON nesting levels and
4million parser events as additional bounds. Animation/navigation/UI index snapshots
use the same byte constant. This permits readable10k-record saves that exceeded the
old4MiB limit; it does not guarantee every100k-record catalog fits the other bounds.

Source discovery and the read-only asset CLI are described in
[Asset discovery](asset-discovery.md). Importer publication and automatic watcher
adoption are still required before this graph can trigger real asset rebuilds.

`forge_asset_build` supplies deterministic build-input identity and
`DerivedDataCache`. All source/settings/importer/dependency/format/platform/backend/
profile inputs participate in its versioned key. Output manifests carry byte lengths
and SHA-256 digests. Cache hits and new outputs require the same caller-supplied
format admission. A cache hit alone never publishes a catalog selection.

The cache uses a separate OS-held local-filesystem lock with a five-second busy
timeout. It serializes publication/verification/eviction, and releases on process
exit. Immutable entries appear only after output files and manifest are flushed.
Corrupt entries are quarantined; identical keys producing different validated bytes
are rejected while retaining the prior entry. Eviction takes an explicit protected
key set from the owner. Returned artifacts contain owned bytes and survive disk
eviction. Default per-file/aggregate/file-count limits are 256 MiB/512 MiB/256;
these are admission bounds, not a performance claim.

This foundation does not yet replace subsystem-specific import/adoption owners or
provide the complete Phase 7 importer/resource/editor workflow.


`AssetBuildQueue` owns bounded background build work, with configurable workers,
priority/FIFO ordering, explicit prerequisite jobs, identical-request coalescing,
source/settings generations, progress, cancellation, and drained completion receipts.
A superseded or cancelled worker result cannot become Ready. Failed prerequisite
jobs diagnose dependents without running them. Completed artifact bytes have a
separate budget; overflow fails the result for explicit retry after draining.
Shutdown requests cancellation and joins workers. Tasks must honor cancellation
or delegate native parsing to a bounded process; the queue does not forcibly
terminate C++ threads or act as a security sandbox. Progress callbacks use weak
owner/job references so late calls cannot mutate a finished or destroyed queue.
The queue never mutates an ECS world, device or catalog selection.
