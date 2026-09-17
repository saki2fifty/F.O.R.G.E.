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

## Validation

Implementation and combined Phase 2 validation are in progress. This entry will be updated with executed results before delivery; no Windows build is claimed yet.
