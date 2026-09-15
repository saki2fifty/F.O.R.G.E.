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

IDs are persistent document identifiers, independent of Flecs entity IDs. Names need not be unique. `parent` references another entity ID; `base` references an entity marked `prefab: true`. Relationship graphs must be acyclic. Missing targets and duplicate/empty IDs are rejected before replacing the current world.

Position fields must be finite float-compatible numbers. Unknown component objects and additional document fields survive load/save so missing plugins do not erase authored data. Only the built-in Position component currently has live ECS/property integration.

Editing snapshots supports undo/redo. Invalid replacements preserve the current world. File saves write a neighboring temporary file and atomically replace the destination; this is not a general multi-writer locking or power-loss durability guarantee.
