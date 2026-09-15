# Inspector

Inspector shows properties for the entity selected in World. Its current editable controls cover the name, hierarchy, position, rotation, scale, primitive shape, and color.

## Change a position

1. Select an entity in World.
2. Find the Position fields in Inspector.
3. Edit X, Y, or Z. The authored block updates in the viewport.
4. Use Undo if you want to reverse a change, and Save to keep it on disk.

Coordinates use world units: Y is vertical. Parenting does not turn these into local coordinates. The camera's movement does not change these values.

During Play, Inspector still edits the authored scene. Use Restart to copy those changes into a fresh play session; the live preview may otherwise show the runtime's different positions.

## Identity and hierarchy controls

**Name** commits when you press Enter. **ID** shows the stable identity and is read-only. **Parent**, **Duplicate subtree**, and **Delete subtree** operate on scene organization; their behavior is explained in [Entities and hierarchy](entities-hierarchy.md).

## Position commands

**Reset position** sets X, Y, and Z to zero. **Place on ground** moves the lowest point of the transformed mesh to Y=0 while keeping X and Z. It does not query terrain or collisions. **Snap position** rounds all three coordinates to multiples of the Scene panel's snap Step. Each command is one undoable edit.

For direct manipulation, use the [Viewport move handles](viewport.md). Inspector shows the move preview but disables its controls during the drag; releasing the mouse commits it.

See [Transforms](transforms.md) for Rotation/Scale fields and Copy/Paste/Reset transform. See [Primitives and color](primitives.md) for Shape and Color controls. Materials, arbitrary component editing, and multi-selection are not implemented.
