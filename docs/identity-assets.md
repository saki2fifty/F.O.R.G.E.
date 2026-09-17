# Persistent identity and minimal asset metadata

## Ownership and reference types

Core owns `EntityId`, `AssetId`, `PersistentEntityId` and `EntityRef`. All use UUIDv4 generated from system cryptographic randomness. `EntityRef` serializes as `{ "scene": "<AssetId>", "entity": "<EntityId>" }`. These are identifiers, not loaded-resource handles. Missing targets retain their exact identity; no name/path matching is used.

A Scene borrows a long-lived WorldContext. The context owns membership-scoped maps from EntityId to transient Flecs handles. Each authored entity owns a DontInherit PersistentEntityId component: Flecs neither inherits nor copies prototype identity. Ordinary authoring preserves surviving handles; delete/undo may change the transient handle while restoring the same EntityId. Identity components are managed by scene reconciliation, not arbitrary native mutation APIs.

`WorldContext::resolve` distinguishes Available, Missing (loaded scene, absent entity), Unresolved (scene not loaded), and Ambiguous (multiple loaded memberships with that AssetId). An explicit transient membership scope resolves one loaded copy. `reference(handle)` provides reverse resolution for live authored entities. Session tokens, revisions, WorldContext and loaded memberships are not persisted document identities.

Subtree duplication creates new IDs and remaps known parent/base/spatial references inside the copied set. External links remain unchanged. Whole-scene duplication creates a new AssetId and new EntityIds, remaps known internal structural and spatial references, and remaps the explicit legacy compatibility table. `remap_entity_ref` serves known typed reference fields only; it does not scan arbitrary JSON. SpatialBinding contains a typed EntityRef for Explicit targets; only this known field is remapped. Unknown payload internals are copied unchanged; no plugin reference repair is claimed.

## Asset service

`forge_assets` depends on core. Core/runtime do not depend on this metadata service or editor UI. `AssetCatalog` is project scoped and explicitly populated; no importer, background scanner, resource loader or renderer is involved.

Records contain AssetId, type, project-relative source locator, source schema version, and known AssetId dependencies. `AssetRef<T>` serializes only its AssetId; the consuming C++ type supplies the expected type. SceneAsset and PrefabAsset are the initial type tags; prefab authoring/compilation is not added.

The version-1 metadata JSON stores these records. Loading prepares a candidate and swaps only after validation. Duplicate IDs/locators and escaping source paths are rejected. Scene records can be rebuilt from their embedded scene-v2/v3 identity; for other source formats the saved metadata record is the identity authority and must be kept in source control. Do not treat that metadata as a disposable cache. There is no importer-generated index or digest/cache invalidation system yet.

Resolution returns Available, Missing, Unresolved or Incompatible with diagnostics. Available means the metadata matches the expected type and a source file exists, not that a runtime resource has loaded. For scene records the resolver additionally validates source schema and embedded AssetId, so a different scene placed at the same path cannot silently satisfy a reference. `relocate` validates the new locator before updating the record; IDs stay unchanged. Missing records/sources do not bind similarly named files. PrefabAsset is metadata-only until its file codec exists.

The service is currently library/API infrastructure. The Content panel remains the existing scene browser, not an asset database UI; it does not discover all duplicate identities in unopened files. AssetHandle<T>, SceneInstanceId persistence, DocumentId, importer/cook pipelines and general resource lifetimes are deferred.

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
