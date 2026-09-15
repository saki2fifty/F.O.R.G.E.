# Undo and redo

Undo returns the authored scene to a previous edit. Redo reapplies an undone edit.

Use **Undo** in the toolbar or Ctrl+Z. Use **Redo**, Ctrl+Shift+Z, or Ctrl+Y to reapply it. Keyboard shortcuts yield to text fields and modal dialogs.

## What is recorded

Adding, renaming, reparenting, duplicating, deleting, and editing supported properties enter scene history. A subtree duplicate, deletion, completed viewport move, Inspector position command, Rotation/Scale drag, color-picker drag, or transform paste/reset is one undoable operation. Restoring a named scene recovery snapshot is also undoable.

Saving does not clear history. Undoing back to the saved contents clears the unsaved indicator. A new edit after Undo clears the redo branch.

## History boundaries

New/Open/Reload clears the previous scene's history. History contains at most 100 scene snapshots and does not survive closing the editor. Camera movement, interface scaling, panel arrangements, native compilation, and runtime gameplay are not scene undo operations.

See [Saving and recovery](saving-recovery.md) for restoring work across restarts.
