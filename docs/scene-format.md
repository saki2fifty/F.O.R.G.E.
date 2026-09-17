# Scene documents: v2 identity and v1 compatibility

```json
{
  "version": 1,
  "entities": [
    {
      "id": "player",
      "name": "Player",
      "components": {
        "forge.position": { "x": 0, "y": 1, "z": 0 }
      }
    }
  ]
}
```

The example above is the legacy v1 format. Its IDs are document-local strings, independent of Flecs entity IDs. Names need not be unique. `parent` references another entity ID; `base` references an entity marked `prefab: true`. Relationship graphs must be acyclic. Missing targets and duplicate/empty IDs are rejected before changing loaded scene content.

Position fields must be finite float-compatible numbers. Unknown component objects and additional document fields survive load/save so missing plugins do not erase authored data. Position, Rotation, Scale, Tint, and Primitive have live Flecs reflection and scene integration. The native module ABI remains Position-only.

Undo/redo replays immutable snapshots as typed changes inside the same long-lived world. Invalid replacements preserve current content, history and registrations. File saves write a neighboring temporary file and atomically replace the destination; this is not a general multi-writer locking or power-loss durability guarantee.

## Primitive transforms and appearance

Scene v1 accepts optional reflected `forge.rotation` (XYZ degrees, each within ±360000), `forge.scale` (XYZ, each 0.001–10000), `forge.tint` (RGB, each 0–1), and `forge.primitive` (`kind`: 0 cube, 1 sphere, 2 cylinder, 3 plane). All numeric fields must be finite. Missing appearance fields render with zero rotation, unit scale, teal tint, and cube geometry. Unknown fields remain preserved.

The preview applies local scale, then Euler X/Y/Z rotation, then world Position. ChildOf still organizes entities without propagating parent transforms. Supported prefab fields inherit through Flecs; live preview snapshots read effective Flecs values without modifying authored ownership. Inspector edits create owning overrides.

The renderer and picker share immutable triangle meshes. Normals use inverse-transpose scale/rotation; fixed directional lighting modulates opaque RGB tint. Nine float4 constants (144 bytes) replace the original position-only shader layout. Framing uses transformed mesh bounds. Rotation/scale are Inspector controls; viewport handles remain translation-only. See the separate [Transforms guide](../manual/editor/transforms.md).

World ownership, opaque-data merging and in-place mutation are described in [World ownership and scene authoring](world-lifetime.md). Scene-v1 files and world-space transforms are unchanged.

## Version 2

```json
{
  "version": 2,
  "asset_id": "44444444-4444-4444-8444-444444444444",
  "legacy_ids": {"player": "11111111-1111-4111-8111-111111111111"},
  "entities": [{
    "id": "11111111-1111-4111-8111-111111111111",
    "name": "Player",
    "components": {"forge.position": {"x": 0, "y": 1, "z": 2}}
  }]
}
```

AssetId is the scene's durable document identity. EntityId and AssetId are distinct 128-bit UUIDv4 value types, serialized as lowercase canonical UUIDs; malformed, nil, other-version and noncanonical inputs are rejected. No DocumentId is introduced. New entities use UUIDs, never reusable entity-N keys. Human-readable default object names use a separate count and are not identity.

`parent` and `base` retain their field names and string representation, but in v2 contain canonical EntityIds, implicitly qualified by the owning scene AssetId. They remain scene-local structural references and must resolve; arbitrary EntityRef values can represent missing targets. No local transform, Parent storage, quaternion or prefab-member migration occurs.

`legacy_ids` maps old strings to EntityIds, including missing/deleted targets. Missing aliases do not bind by name; retained aliases do not make deleted entities exist. A legacy alias cannot shadow a different present canonical ID. The compatibility entry points accept explicit old aliases, while supported serialization and new commands use UUIDs. Opaque plugin values, unknown component fields, unknown entity fields and envelope fields are not recursively rewritten. Reserved v2 envelope-field collisions in v1 are diagnosed without overwriting the source.

In-memory v1 replacement is a compatibility adapter into the current scene: it retains the scene identity and known alias assignments. It has no filename and performs no disk writes. Callers must retain the returned v2 document across processes. Real file opening uses `read_scene_file`, which validates before activation and retains assignments in a neighboring versioned identity record. A new scene uses `empty_scene` and receives a fresh AssetId.

The companion record stores the original JSON and migrated JSON. Reopening the unchanged legacy source verifies the record and uses the same identifiers. A conflicting source/record is rejected. Opening never replaces the original scene. The first v2 Save copies original bytes to `.v1.backup`, checks any existing backup for agreement, then atomically replaces the scene. Failure retains the source and assignment record. These are single-scene operations under the existing project writer lease, not project-wide migration transactions or power-loss guarantees.

After successful migration, identity is embedded and independent of the file location. Save As to a distinct new filename for a persisted scene means a new logical scene asset: fresh AssetId and authored EntityIds, remapped known internal references, preserved opaque JSON. It resets history after successful publication. Ordinary Save and the first save of an untitled scene retain identity. OS-level file copying retains the same logical identity; use the duplication service for a new asset.

See [Persistent identity and asset metadata](identity-assets.md) for resolver scope and the asset service.
