# September 17, 2026

## Persistent identity — foundation

- Added distinct 128-bit UUIDv4 EntityId and AssetId value types with canonical parsing/serialization and checked OS randomness.
- Added typed EntityRef, non-inherited PersistentEntityId, and membership-scoped WorldContext resolution with missing/unloaded/ambiguous outcomes and reverse lookup.
- A scene AssetId is its durable document identity. Editor sessions and loaded membership scopes remain separate. No DocumentId, loaded AssetHandle or dependency upgrade.

## Scenes, migration and authoring

- Scene-v2 embeds AssetId and canonical EntityIds. Known parent/base links migrate without changing world-space transforms or prefab semantics.
- Legacy file opening validates a detached migration and records UUID assignments in a companion identity record; the source stays intact. Save retains the original backup and atomically writes v2. Conflicting records/sources fail with preserved data.
- New objects and subtree copies use fresh UUIDs. Whole-scene Save As allocates fresh scene/object IDs, remaps known internal links, and starts separate history only after saving succeeds. Save As requires a new destination; ordinary Save retains identity.
- Kept opaque/plugin payload internals unchanged and added explicit legacy aliases. Reads and authoring targets now expose canonical identities, while legacy API inputs remain accepted through the compatibility adapter.
- Added user instructions for migration, backups, Save As and identity conflicts. No editor layout, renderer, transform, clock or native ABI changes.

## Asset references and metadata

- Added a UI-independent forge_assets target with typed AssetRef<T>, explicit asset metadata registration/persistence, source relocation, and type-checked resolution.
- Missing, unregistered and incompatible assets have distinct results. Duplicate IDs/locators and escaping paths are rejected; replacing a scene file at the same path with a different AssetId cannot satisfy the old reference.
- Added identity/migration/duplication/scope/asset tests, including actual Windows blocked-file replacement. Windows editor CI explicitly builds/runs the new suite.
- Asset loading handles, importers, cook pipelines and a project-wide conflict browser remain deferred. The current five numeric components do not contain arbitrary typed reference fields; known hierarchy/base links are remapped, and a typed helper serves future known fields without scanning opaque payloads.

## Validation

Local Linux core/API/runtime/native/plugin suites: 9/9 passed. Targeted ASan/UBSan/LeakSanitizer identity/lifetime/API/core suites: 4/4 passed. Manual tests: 3/3 passed; format/whitespace and workflow checks passed. Windows-target syntax checks passed; this is not Windows execution. Local editor/process/input/migration/native iteration suites: 2/2 passed. Full Windows/WARP validation remains pending before delivery.
