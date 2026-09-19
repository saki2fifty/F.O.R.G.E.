# Command palette

The command palette lets you find common authoring actions without navigating several panels.

## Find and run an action

1. Finish the current drag or text edit.
2. Press **Ctrl+Shift+P**, or choose **Tools → Command palette**.
3. Type part of an action name, such as `sphere`, `ground`, `color`, or `reset`.
4. Use Up and Down to choose a result, then press Enter. You can also click a result.
5. Press Escape to close without running an action.

Search ignores letter case. Actions that require a selection are disabled when nothing is selected. Authoring actions are disabled during Play or another active operation. Undo and Redo require an available history entry.

## Available actions

- Create any supported entity recipe: Empty Entity, blockout primitives, Audio, Navigation or UI. The shared Placement setting applies.
- Duplicate or delete a subtree, or move an entity to the scene root.
- Select/Move tools or start Rotate/Scale, with the established gesture commit/cancel rules.
- Reset all transforms, just rotation, or just scale.
- Place on ground or snap to the Scene View menu’s Snap spacing.
- Switch primitive shape or apply a blockout color preset.
- Undo or redo an edit.
- Open Scene diagnostics or Component schema.

Palette actions use the same authoring operations as the corresponding editor controls. Creation and property edits can be undone. Changing shape preserves transform and color.

The palette currently contains common scene actions. File operations and custom shortcut editing are not included yet.

Continue with [Transforms](transforms.md), [Primitives and color](primitives.md), and [Scene diagnostics](diagnostics.md).
