# Inspector

Inspector shows properties for the entity selected in Hierarchy. Its current editable controls cover the name, hierarchy, position, rotation, scale, primitive shape, and color.

## Change a position

1. Select an entity in Hierarchy.
2. Find the Position fields in Inspector.
3. Drag X, Y, or Z, or Ctrl-click to type. The preview updates; release commits one undo step. Escape cancels a drag.
4. Use Undo if you want to reverse a change, and Save to keep it on disk.

Coordinates use world units: Y is vertical. Parenting does not turn these into local coordinates. The camera's movement does not change these values.

During Play, Inspector is read-only. Stop Play to edit the authored scene, then start Play to use the changes.

## Identity and hierarchy controls

**Name** commits when you press Enter. Expand **Details** to see **ID**, which shows the stable identity and is read-only. **Parent** and the **Object actions → Duplicate subtree / Delete subtree** commands operate on scene organization; their behavior is explained in [Entities and hierarchy](entities-hierarchy.md).

## Position commands

Right-click **Reset transform** to find **Reset position**, which sets X, Y, and Z to zero. **Object actions → Place on ground** moves the lowest point of the transformed mesh to Y=0 while keeping X and Z. It does not query terrain or collisions. **Object actions → Snap position** rounds all three coordinates to multiples of the Scene **View → Snap spacing**. Each command is one undoable edit.

For direct manipulation, use the [Viewport move handles](viewport.md). Inspector shows the move preview but disables its controls during the drag; releasing the mouse commits it.

See [Transforms](transforms.md) for Rotation/Scale fields and Copy/Paste/Reset transform. See [Primitives and color](primitives.md) for Shape and Color controls. Materials, arbitrary component editing, and multi-selection are not implemented.
