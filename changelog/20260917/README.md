# September 17, 2026

## Persistent identity — foundation

- Added distinct 128-bit UUIDv4 EntityId and AssetId value types with canonical parsing/serialization and checked OS randomness.
- Added typed EntityRef, non-inherited PersistentEntityId, and membership-scoped WorldContext resolution with missing/unloaded/ambiguous outcomes and reverse lookup.
- A scene AssetId is its durable document identity. Editor sessions and loaded membership scopes remain separate. No DocumentId, loaded AssetHandle or dependency upgrade.

## Validation

Implementation and combined Phase 2 validation are in progress. This entry will be updated with executed results before delivery; no Windows build is claimed yet.
