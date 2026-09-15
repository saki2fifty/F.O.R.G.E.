# Entities and hierarchy

Entities are the things you author in a scene. World lists them in a tree; Inspector edits the selected entity. An entity with Position appears as a diagnostic block in the viewport.

## Add and select an entity

Select **Add entity** in the top toolbar. FORGE creates an entity with a unique authored ID and Position. Click its row in **World** to select it. Viewport clicking does not select entities yet.

## Rename an entity

Select the entity, edit **Inspector → Name**, and press Enter to commit. Renaming keeps the entity's ID, so the name can change without changing its identity.

## Organize a hierarchy

Select an entity and choose another entity in **Inspector → Parent**. Expand the parent's row in World to see its children. Choose **Scene root** to remove the parent relationship.

Positions remain world-space values. Moving a parent does not move its children yet. A parent cannot be placed under one of its descendants; a rejected operation leaves the hierarchy unchanged and reports the problem in Console.

## Duplicate or delete

**Duplicate subtree**, or Ctrl+D, copies the selection and its descendants with new IDs. Copies start at the original positions, so move them in Inspector to see them separately.

**Delete subtree** removes the selection and its descendants. Delete also works when World has focus. Undo restores the deletion. Deleting a prefab that is referenced outside the subtree is rejected; full prefab authoring controls are not available yet.

See [Inspector](inspector.md) and [Undo and redo](undo-redo.md).
