# Scenes

A scene stores your authored entities, their hierarchy, and component values. You can keep multiple JSON scene files inside a project and work on one at a time.

## Create a scene

Choose **File → New scene**, or press Ctrl+N. The scene starts empty and is named **Untitled** until you save it. Select **Create → Cube** to begin authoring.

## Open a scene

Choose **File → Open scene...**, or press Ctrl+O, and select a JSON scene inside the current project. If you have unsaved edits, choose whether to save, discard, or cancel before opening it.

A successful open clears the previous scene's undo history, stops play, and restores its saved view bookmark, or resets the viewport camera if none exists. An invalid or unreadable scene produces a Console diagnostic and leaves the current authored scene available.

## Save another copy

Press Ctrl+Shift+S or choose **File → Save As...**, then choose another filename within the project. Choose a filename that does not already exist. After saving, the new file becomes the active scene. A copy of a saved scene receives a new scene identity and new object identities; its object hierarchy and prefab links still point to the corresponding copied objects. The old file stays unchanged, and the copy starts a new undo history. Extensionless selections receive `.scene.json`.

Scene destinations must end in `.json`. The project manifest and `.forge` working folder cannot be used as scene destinations.

## Reload a disk file

Choose **File → Reload from disk** to reopen the active file, resolving any unsaved edits first. This is useful after editing a scene file externally. An untitled scene has no disk file to reload.

See [Saving and recovery](saving-recovery.md) for conflict handling and [Undo and redo](undo-redo.md) for reversing edits.

## Browse project scenes

The **Content** panel lists project-relative recognized JSON scenes. Filter the list, select a file and choose **Open selected**, or double-click it. Opening uses the same unsaved-change guard as File → Open scene. See [Content browser](content-browser.md).

## Scene identity and older scenes

Each scene and authored object now has a permanent identifier. Renaming objects, changing their parent, editing properties, saving, and reopening preserve these identifiers. Undoing a deletion restores the original object's identifier. Duplicating objects creates new identifiers.

Opening an older scene keeps its original file intact. FORGE writes a companion `<scene filename>.forge-identity.json` record beside it so the assigned identifiers stay the same if you close and reopen before saving. Keep this record with the older scene, including in source control. If you move or rename an unsaved format-1 file, move or rename its companion to match; saving to format 2 before moving is simpler. The folder must be writable to establish these identities.

Your next **Save** writes scene format 2 and keeps the original format 1 file as `<scene filename>.v1.backup`. Older FORGE builds cannot open format 2; the backup preserves the older format. Neither file is an extra scene shown in Content.

Once saved in format 2, the scene carries its identifiers inside the file. Moving or renaming that file within your project preserves its identity. A saved view bookmark still uses the old filename and may need to be saved again. Update the project's startup scene path if you move that scene.

Use **Save As** to create an independent copy. Copying files with your operating system also copies their identifiers; that is a copy of the same asset, not a new asset. The asset metadata API rejects duplicate registrations; a full project-wide conflict browser is not available yet.

Unknown plugin data is preserved exactly as JSON values. FORGE updates the hierarchy and prefab references it understands, but does not guess at reference strings inside unavailable plugin data.
