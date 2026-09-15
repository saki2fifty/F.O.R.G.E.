# Projects, scenes, and recovery

Use **File > New project** to choose an existing parent folder and a new project name. FORGE creates a new subfolder containing `Scenes`, `Assets`, `Native`, and `forge.project.json`. Existing folders are never overwritten. Use **Open project** to select an existing project folder; the last eight successful opens appear under **Recent projects**. The last project is reopened at startup.

The version 1 manifest names the project and its startup scene:

```json
{"version": 1, "name": "MyGame", "startup_scene": "Scenes/main.scene.json"}
```

A project opens its startup scene, rather than the last scene you edited. To change startup selection, edit the manifest while the editor is closed. Legacy folders with `main.scene.json` remain supported. A fresh launch in a folder without a manifest or scene begins with an empty scene.

## Scene commands

| Command | Shortcut | Behavior |
| --- | --- | --- |
| New scene | Ctrl+N | Start an empty untitled scene |
| Open scene | Ctrl+O | Choose a JSON scene within this project |
| Save | Ctrl+S | Save active scene; untitled scenes ask for a filename |
| Save As | Ctrl+Shift+S | Save under another filename within this project |
| Reload from disk | File menu | Reopen the active scene file |
| Undo / Redo | Ctrl+Z / Ctrl+Shift+Z or Ctrl+Y | Undo or redo authored changes |
| Duplicate subtree | Ctrl+D | Duplicate the selected entity and descendants |
| Delete subtree | Delete, with World focused | Delete selection and descendants |

Keyboard commands are suppressed while typing or interacting with modal dialogs. Opening or creating a scene clears its undo history. Switching scenes/projects stops play and resets the editor camera. Switching is blocked during native compilation. Project switches recreate the native build controller for the new root.

The title bar prefixes unsaved scenes with `*`; World also shows saved/unsaved state. New/Open/Reload/project switching and closing the editor offer **Save and continue**, **Discard changes**, or **Cancel** when edits are unsaved. A failed save or open preserves the current scene. Save rejects externally changed or deleted active files: use Save As with another filename to preserve both versions. This is conflict detection, not concurrent editing or merging.

Scene files must use `.json` and stay inside the project. The manifest and `.forge` working area are not scene-save destinations. Save As adds `.scene.json` to extensionless selections. Native OS dialogs handle file selection; diagnostics appear in Console or the active modal.

## Recovery

Every 30 seconds, changed unsaved scenes receive an atomic recovery snapshot under `.forge/recovery`. **File > Create recovery snapshot** writes one immediately. Snapshots do not overwrite scene files. Successful saves clear the corresponding snapshot; explicitly discarding edits clears it after the requested operation succeeds.

Opening a scene with a snapshot offers recovery. **File > Recover current scene** restores a matching snapshot as one undoable edit. The saved baseline must still match; a mismatch or malformed snapshot produces a diagnostic and preserves the recovery file. Save after inspecting the restored scene.

Untitled scenes have one recovery slot per project. **Recover untitled scene** can restore it after restarting, including when the project startup scene is opened first. Choosing **Later** preserves a snapshot for manual recovery. There is no recovery browser or merge tool yet. Open other named scenes individually to discover their snapshots.

## Validation scope

Automated document/controller tests cover project creation, scene round trips, dirty/undo state, external-write/deletion conflicts, invalid opens, protected destinations, named and untitled recovery, and pending Save/Discard/Cancel transitions. Headless tests do not interact with native OS dialogs. Desktop checks should include selecting folders/files, cancelling a Save As dialog, and recovering a snapshot after terminating the editor without a normal save.
