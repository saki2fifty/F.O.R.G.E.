# Entities and hierarchy

Entities are the things you author in a scene. Hierarchy lists them in a tree; Inspector edits the selected entity. An entity with a resolved transform appears as a built-in primitive in the viewport, defaulting to a cube.

## Add and select an entity

Select **Create → Cube** in the top toolbar. FORGE creates a cube with a unique authored ID, transform, and color. Use the Create menu for other shapes. Click its row in **Hierarchy**, or left-click its visible block in **Scene**, to select it. Viewport selection chooses the nearest block under the pointer.

## Rename an entity

Select the entity, edit **Inspector → Name**, and press Enter to commit. Renaming keeps the entity's ID, so the name can change without changing its identity.

## Organize a hierarchy

Select an entity and choose another entity in **Inspector → Parent**. Expand the parent's row in Hierarchy to see its children. Choose **Scene root** to remove the parent relationship.

Choosing a Parent preserves the object’s world placement, then makes it follow that parent’s transform. Moving, rotating or scaling the parent now affects the child. Older scenes retain **Space → World** until you explicitly change Space or reparent them. A parent cannot be placed under one of its descendants; a rejected operation leaves the hierarchy unchanged and reports the problem in Console.

## Duplicate or delete

**Inspector → Object actions → Duplicate subtree**, or Ctrl+D, copies the selection and its descendants with new IDs. Copies start at the original positions, so move them in Inspector to see them separately.

**Command palette → Delete subtree** removes the selection and its descendants. Delete also works when Hierarchy has focus. Undo restores the deletion. Deleting a prefab that is referenced outside the subtree is rejected; full prefab authoring controls are not available yet. A surviving object explicitly attached to the deleted target detaches while keeping its world placement. If that requires unsupported local shear, deletion is rejected without changing the scene.

See [Inspector](inspector.md) and [Undo and redo](undo-redo.md).

## Search and expand the tree

Use **Search names or IDs...** in Hierarchy to filter the hierarchy. Matching descendants keep their ancestor rows visible, and matching paths open automatically. Matching is case-insensitive for ASCII text. Clear the search to show all entities again.

Entities are listed alphabetically within each parent. **Expand all** opens all branches; **Collapse all** closes them. An active search keeps matching paths open even after Collapse all.

## Structured groups

The Hierarchy marks prefab roots **[prefab]**, structured children **[member]**, and unavailable members **[missing]**. Duplicate or delete the whole instance from its root. Edit an interior member's name or hierarchy in its [prefab source](prefabs.md).

## Context commands and dragging

Right-click an entity for Rename entity, Duplicate subtree, Delete subtree, Add Component and Move to scene root. F2 focuses its Name field in Inspector.

Drag an entity onto another hierarchy row to reparent it. FORGE previews validation before accepting the drop. Valid reparenting preserves world placement and then follows the new parent; cycles, fixed prefab interiors and unrepresentable transforms are rejected without rewriting the hierarchy. Empty searches explain that the filter can be cleared.
