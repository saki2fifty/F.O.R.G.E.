# Entities and hierarchy

Entities are the things you author in a scene. World lists them in a tree; Inspector edits the selected entity. An entity with Position appears as a built-in primitive in the viewport, defaulting to a cube.

## Add and select an entity

Select **Add entity** in the top toolbar. FORGE creates a cube with a unique authored ID, transform, and color. Use the Create menu for other shapes. Click its row in **World**, or left-click its visible block in **Scene**, to select it. Viewport selection chooses the nearest block under the pointer.

## Rename an entity

Select the entity, edit **Inspector → Name**, and press Enter to commit. Renaming keeps the entity's ID, so the name can change without changing its identity.

## Organize a hierarchy

Select an entity and choose another entity in **Inspector → Parent**. Expand the parent's row in World to see its children. Choose **Scene root** to remove the parent relationship.

Positions remain world-space values. Moving a parent does not move its children yet. A parent cannot be placed under one of its descendants; a rejected operation leaves the hierarchy unchanged and reports the problem in Console.

## Duplicate or delete

**Duplicate subtree**, or Ctrl+D, copies the selection and its descendants with new IDs. Copies start at the original positions, so move them in Inspector to see them separately.

**Delete subtree** removes the selection and its descendants. Delete also works when World has focus. Undo restores the deletion. Deleting a prefab that is referenced outside the subtree is rejected; full prefab authoring controls are not available yet.

See [Inspector](inspector.md) and [Undo and redo](undo-redo.md).

## Search and expand the tree

Use **Search names or IDs...** in World to filter the hierarchy. Matching descendants keep their ancestor rows visible, and matching paths open automatically. Matching is case-insensitive for ASCII text. Clear the search to show all entities again.

Entities are listed alphabetically within each parent. **Expand all** opens all branches; **Collapse all** closes them. An active search keeps matching paths open even after Collapse all.
