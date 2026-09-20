# Scene documents: v3 transforms and legacy compatibility

```json
{
  "version": 3,
  "asset_id": "44444444-4444-4444-8444-444444444444",
  "entities": [{
    "id": "11111111-1111-4111-8111-111111111111",
    "name": "Player",
    "spatial": {"mode": "follow_structure"},
    "components": {
      "forge.local_translation": {"x": 0, "y": 1, "z": 2},
      "forge.local_rotation": {"x": 0, "y": 0, "z": 0, "w": 1},
      "forge.local_scale": {"x": 1, "y": 1, "z": 1}
    }
  }]
}
```

AssetId is the scene's durable authored-document identity. EntityId and AssetId are distinct UUIDv4 types serialized as canonical lowercase strings. No DocumentId or persistent runtime-instance identity is introduced. `parent` and `base` are scene-local EntityId strings; base must identify a prefab. Names need not be unique. Structural, inheritance and effective spatial graphs must be acyclic.

## Authored local channels

Only owned components are serialized. Missing local channels can inherit independently from `base`: owning translation never materializes inherited rotation or scale. Effective translation makes an entity transform-capable; missing rotation/scale default to identity/unit scale. Local translation is finite double XYZ; rotation is normalized float XYZW (squared-length tolerance 2e-6); visual scale is finite signed float XYZ, -10000–+10000, including zero and tiny magnitudes (extended values require scene5 on output). Tint and Primitive are unchanged. Rotation display/edit adapters use Euler degrees; ordinary saves preserve canonical quaternion fields directly.

`spatial.mode` is `follow_structure`, `world` or `explicit`; absence means follow_structure. Explicit requires `target: {"scene": "<AssetId>", "entity": "<EntityId>"}`. Other modes cannot contain target. Missing/non-transform/cross-scene targets remain unresolved in Phase 3. See [transform semantics](transforms.md).

`WorldTransform`, `world_affine` and `spatial_resolved` are derived presentation data, rejected as authored v3 input. Effective read snapshots additionally expose legacy Position/Rotation/Scale display adapters; they must not be saved as authored documents. Persist `Scene::document()` / `scene.read`, not `effective_document()` / entity-query rows.

Unknown envelope, entity and component fields survive. Known channels are read from Flecs. Spatial mode/target are live typed state; extra spatial fields remain opaque. No raw handles, pointers or matrix ABI blobs are persisted.

## v1 and v2 migration

Legacy v1 uses document-local string IDs and `forge.position`, `forge.rotation` (XYZ degrees), `forge.scale`. Version 2 adds durable UUID identity but retains those world-space transform semantics. Phase 3 accepts both and normalizes into v3 before world mutation:

1. For v1 only, allocate/reuse the explicit legacy UUID assignments. Version 2 UUIDs remain byte-for-byte unchanged.
2. Convert only owned Position into LocalTranslation, preserving the original effective float value as double.
3. Convert only owned Euler Rotation using the existing Rz * Ry * Rx order into a normalized quaternion. Inherited rotation stays absent/inherited.
4. Rename only owned Scale to LocalScale. Independent ownership of every channel is preserved.
5. Set migrated rows to World binding. Structural ChildOf links remain intact but do not suddenly propagate motion.

Unknown Position/Scale extras remain with their renamed component. Unknown legacy Rotation fields are retained under `forge.local_rotation.legacy_euler_fields`; the known XYZ values become quaternion fields. Unknown plugin payloads and opaque references are never recursively rewritten. Reserved v3 field/type collisions are diagnosed with the source untouched.

Opening an old file never replaces it. v1's neighboring `.forge-identity.json` journal retains the original and v2 identity assignment, then the deterministic v3 transform migration is applied. Conflicting source/journal data is rejected; never discard the journal to retry. v2 already has durable identities and needs no new identity journal. In-memory compatibility replacement retains current scene identity/known aliases; callers retain the returned v3 document across processes.

The first Save validates v3, atomically publishes an exact-byte `.v1.backup` or `.v2.backup` of the old source, and atomically replaces the scene. Existing backup mismatch rejects the save. Blocked backup/replacement preserves source and assignments; retry after releasing the blocker. Readers are closed before replacement. This is one-scene protection under the project writer lease, not a project-wide migration or power-loss durability guarantee.

## Identity, duplication and history

Ordinary Save, rename, transforms and reparent retain identity. Subtree duplication creates new EntityIds and remaps known internal parent/base/spatial targets. Whole-scene duplication creates a new AssetId and fresh EntityIds, remaps known internal EntityRefs and legacy aliases, and preserves opaque payloads. Save As to a distinct filename for a persisted scene uses this new-asset operation. OS copying retains identity.

Undo/redo patches authored channels and bindings inside the long-lived Flecs world. Derived matrices are recomputed, never stored as history authority. Invalid candidate graphs or unrepresentable world-to-local operations leave state and history unchanged. See [world ownership](world-lifetime.md) and [identity and assets](identity-assets.md).

## Scene 4 structured instances

Scenes containing first-class prefab instances use version 4, with stable instance-member maps and explicit override intent. Ordinary new scenes remain version 3; legacy versions retain their prior migration/compatibility behavior. See [Prefab architecture](prefabs.md) for the independent prefab schema, member provenance, missing states and runtime snapshot envelope.

## Scene5 numerical compatibility

Scene5 keeps the scene4 structure and identities while permitting signed, zero and
sub-0.001 LocalScale values. Documents only promote when owned scale needs that
range; positive-only scene3/4 remains unchanged. Older editors reject version5.
Known negative-zero scale is written as positive zero; opaque values are untouched.
See[transform semantics](transforms.md#numerical-compatibility).
