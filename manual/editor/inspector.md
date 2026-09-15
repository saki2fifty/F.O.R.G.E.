# Inspector

Inspector shows properties for the entity selected in World. Its current editable controls cover the name, hierarchy, and Position.

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

**Reset position** sets X, Y, and Z to zero. **Place on ground** sets Y to 0.5 while keeping X and Z, placing the unit block's bottom on the reference plane. It does not query terrain or collisions. **Snap position** rounds all three coordinates to multiples of the Scene panel's snap Step. Each command is one undoable edit.

For direct manipulation, use the [Viewport move handles](viewport.md). Inspector shows the move preview but disables its controls during the drag; releasing the mouse commits it.

Only the supported Position fields have property controls today. Materials, arbitrary component editing, multi-selection, rotation, and scale are not implemented.
