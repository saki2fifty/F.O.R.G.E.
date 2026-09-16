# Scene document v1

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

IDs are persistent document identifiers, independent of Flecs entity IDs. Names need not be unique. `parent` references another entity ID; `base` references an entity marked `prefab: true`. Relationship graphs must be acyclic. Missing targets and duplicate/empty IDs are rejected before changing loaded scene content.

Position fields must be finite float-compatible numbers. Unknown component objects and additional document fields survive load/save so missing plugins do not erase authored data. Position, Rotation, Scale, Tint, and Primitive have live Flecs reflection and scene integration. The native module ABI remains Position-only.

Undo/redo replays immutable snapshots as typed changes inside the same long-lived world. Invalid replacements preserve current content, history and registrations. File saves write a neighboring temporary file and atomically replace the destination; this is not a general multi-writer locking or power-loss durability guarantee.

## Primitive transforms and appearance

Scene v1 accepts optional reflected `forge.rotation` (XYZ degrees, each within ±360000), `forge.scale` (XYZ, each 0.001–10000), `forge.tint` (RGB, each 0–1), and `forge.primitive` (`kind`: 0 cube, 1 sphere, 2 cylinder, 3 plane). All numeric fields must be finite. Missing appearance fields render with zero rotation, unit scale, teal tint, and cube geometry. Unknown fields remain preserved.

The preview applies local scale, then Euler X/Y/Z rotation, then world Position. ChildOf still organizes entities without propagating parent transforms. Supported prefab fields inherit through Flecs; live preview snapshots read effective Flecs values without modifying authored ownership. Inspector edits create owning overrides.

The renderer and picker share immutable triangle meshes. Normals use inverse-transpose scale/rotation; fixed directional lighting modulates opaque RGB tint. Nine float4 constants (144 bytes) replace the original position-only shader layout. Framing uses transformed mesh bounds. Rotation/scale are Inspector controls; viewport handles remain translation-only. See the separate [Transforms guide](../manual/editor/transforms.md).

World ownership, opaque-data merging and in-place mutation are described in [World ownership and scene authoring](world-lifetime.md). Scene-v1 files and world-space transforms are unchanged.
