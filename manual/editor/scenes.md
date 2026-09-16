# Scenes

A scene stores your authored entities, their hierarchy, and component values. You can keep multiple JSON scene files inside a project and work on one at a time.

## Create a scene

Choose **File → New scene**, or press Ctrl+N. The scene starts empty and is named **Untitled** until you save it. Select **Create → Cube** to begin authoring.

## Open a scene

Choose **File → Open scene...**, or press Ctrl+O, and select a JSON scene inside the current project. If you have unsaved edits, choose whether to save, discard, or cancel before opening it.

A successful open clears the previous scene's undo history, stops play, and restores its saved view bookmark, or resets the viewport camera if none exists. An invalid or unreadable scene produces a Console diagnostic and leaves the current authored scene available.

## Save another copy

Press Ctrl+Shift+S or choose **File → Save As...**, then choose another filename within the project. After saving, the new file becomes the active scene. Extensionless selections receive `.scene.json`.

Scene destinations must end in `.json`. The project manifest and `.forge` working folder cannot be used as scene destinations.

## Reload a disk file

Choose **File → Reload from disk** to reopen the active file, resolving any unsaved edits first. This is useful after editing a scene file externally. An untitled scene has no disk file to reload.

See [Saving and recovery](saving-recovery.md) for conflict handling and [Undo and redo](undo-redo.md) for reversing edits.

## Browse project scenes

The **Content** panel lists project-relative recognized JSON scenes. Filter the list, select a file and choose **Open selected**, or double-click it. Opening uses the same unsaved-change guard as File → Open scene. See [Content browser](content-browser.md).
