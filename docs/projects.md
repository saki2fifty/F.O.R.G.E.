# Project and document formats

End-user instructions live in the separate [Projects](../manual/editor/projects.md), [Scenes](../manual/editor/scenes.md), and [Saving and recovery](../manual/editor/saving-recovery.md) manual pages. This document describes the storage and controller contracts.

## Project manifest

`forge.project.json` version 1 contains a display name and a project-relative startup scene:

```json
{"version": 1, "name": "MyGame", "startup_scene": "Scenes/main.scene.json"}
```

The startup path must resolve to a `.json` file inside the canonical project root. Scene destinations cannot be the root manifest or files beneath `.forge`. A missing manifest selects the legacy `main.scene.json` path. Initial startup can allow an empty legacy folder; opening a project through the normal project command requires its startup scene.

Project creation stages `Scenes/main.scene.json`, `Assets`, `Native`, and the manifest in a sibling directory before renaming it into place. Existing destinations are rejected. This is not a cross-process project locking protocol.

## Authoring document state

`SceneDocument` owns the active project/path, saved JSON baseline, persisted-file flag, and cached dirty state. `Scene` owns Flecs state and bounded undo/redo history. A successful validated replacement advances the scene revision; a document reset also clears history. Dirty comparison only recomputes after revision changes.

Open validates before replacing active state. Save checks the active file against the saved baseline and rejects external changes or deletion before writing atomically. Save As commits the new filename/baseline only after a successful write. These checks detect conflicts but do not provide cross-process compare-and-swap or merging.

## Recovery envelope

Recovery files reside in `.forge/recovery`. A deterministic 64-bit FNV-1a hash of the relative scene path selects the filename; untitled scenes use one project-local slot. The version-1 envelope contains:

```json
{"version": 1, "scene": "Scenes/main.scene.json", "base": {}, "document": {}}
```

`base` contains the saved JSON baseline and `document` the unsaved scene. Untitled records use an empty scene path and null base. A named recovery must match the current path and baseline before it becomes one undoable edit. Untitled recovery resets into an unsaved document. Malformed or mismatched records are retained for inspection.

Autosave writes changed dirty revisions at approximately 30-second intervals. Successful saves remove matching recovery files. An explicit discard removes the old snapshot only after the requested transition succeeds.

## Editor coordination and validation

`EditorFiles` coordinates asynchronous SDL dialogs and Save/Discard/Cancel transitions on the main thread. Dialog callbacks copy path/error data into synchronized shared state; callbacks do not mutate scene or UI state. Project switches stop play and recreate the native controller. Scene/project changes are blocked during native builds.

`tests/document_tests.hpp` exercises project creation, dirty state, undo-to-baseline, failed opens, destination constraints, external conflicts, recovery, and guarded transitions. Native file-dialog interaction remains a desktop acceptance check. Layout/preferences are global; last active scene and per-project camera persistence are not implemented.
