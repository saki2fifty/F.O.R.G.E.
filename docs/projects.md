# Project and document formats

End-user instructions live in the separate [Projects](../manual/editor/projects.md), [Scenes](../manual/editor/scenes.md), and [Saving and recovery](../manual/editor/saving-recovery.md) manual pages. This document describes the storage and controller contracts.

## Project manifest

`forge.project.json` version 2 stores a display name, simulation frequency, startup scene reference and input map:

```json
{"version": 2, "name": "MyGame", "simulation_hz": 60,
 "startup_scene": {"asset": "11111111-1111-4111-8111-111111111111", "source": "Scenes/main.scene.json"},
 "input": {"version": 1, "actions": []}}
```

Startup AssetId is authoritative; the source is a locator hint. Resolution follows moves and rejects missing/duplicate identity. A null startup opens an untitled empty scene. Source paths stay inside the canonical project root; scene destinations cannot overwrite the manifest or `.forge` control files. A missing manifest retains legacy main.scene.json behavior. Version-1 manifests with a startup path remain readable; explicit settings Save upgrades with an original backup. No opening-time manifest write occurs. See [Core services](core-services.md) and [Input](input.md) for validation, ownership and migration policy.

Project creation stages `Scenes/main.scene.json`, `Assets`, `Native`, and the manifest in a sibling directory before renaming it into place. Existing destinations are rejected. Opening the result subsequently acquires project writer ownership.

## Authoring document state

`SceneDocument` owns the active project/path, separate disk and normalized authored baselines, persisted-file flag, and cached dirty state. `Scene` manages world-owned Flecs content and bounded undo/redo history. A successful validated replacement advances the scene revision; a document reset also clears history. Dirty comparison only recomputes after revision changes.

Open validates before replacing active state. Save checks the active file against the saved baseline and rejects external changes or deletion before writing atomically. Save As to a new filename for a saved scene creates fresh scene/entity identities and resets history only after a successful write. Ordinary Save and the first save of an untitled scene retain identity. These checks detect conflicts but do not provide cross-process compare-and-swap or merging.

## Recovery envelope

Recovery files reside in `.forge/recovery`. A deterministic 64-bit FNV-1a hash of the relative scene path selects the filename; untitled scenes use one project-local slot. The version-1 envelope contains:

```json
{"version": 1, "scene": "Scenes/main.scene.json", "base": {}, "document": {}}
```

`base` contains the saved JSON baseline and `document` the unsaved scene. Untitled records use an empty scene path and null base. A named recovery must match the current path and either the normalized authored baseline or original disk baseline before it becomes one undoable edit; the latter supports recovery saved by a v1 editor. Untitled recovery resets into an unsaved document. Malformed or mismatched records are retained for inspection.

Autosave writes changed dirty revisions at approximately 30-second intervals. Successful saves remove matching recovery files. An explicit discard removes the old snapshot only after the requested transition succeeds.

## Editor coordination and validation

`EditorFiles` coordinates asynchronous SDL dialogs and Save/Discard/Cancel transitions on the main thread. Dialog callbacks copy path/error data into synchronized shared state; callbacks do not mutate scene or UI state. Project switches stop play and recreate the native controller. Scene/project changes are blocked during native builds.

`tests/document_tests.hpp` exercises project creation, dirty state, undo-to-baseline, failed opens, destination constraints, external conflicts, recovery, and guarded transitions. Native file-dialog interaction remains a desktop acceptance check. Layout/preferences are global; last active scene persistence is not implemented. Explicit per-scene camera bookmarks are stored in `.forge/editor-views.json` (version 1), keyed by relative scene path or an untitled slot. Invalid bookmarks leave the camera unchanged.

## Exclusive writer ownership

`ProjectLease` in the UI-free authoring library holds `.forge/writer.lock` open for the lifetime of the active project. Windows uses `CreateFileW` with sharing disabled and a non-inheritable handle; Linux uses nonblocking exclusive `flock` on a close-on-exec descriptor. The OS releases ownership on process death. The marker is deliberately never unlinked on normal shutdown, avoiding a second-lock inode race. This is cooperative coordination for compatible FORGE instances on local filesystems, not access control against arbitrary other programs or a distributed lock.

A candidate project lease is acquired before scene replacement. Failed acquisition, parsing or validation leaves the old lease and document active. Save, recovery and view bookmark writes validate current ownership. Discard-and-switch retains the old lease until successful old-recovery cleanup completes. Linux additionally checks that the lock path still identifies the held inode. Control-folder redirects are rejected; Windows scene paths also reject alternate data streams and case variants of reserved control paths.

Document generation changes on successful open/reload/new/untitled recovery and filename changes. It does not replace Scene revision: it invalidates scene-scoped external sessions after file lifecycle transitions even if scene bytes are identical. Live automation checks scope before another request: scene grants stop, while an explicitly granted document connection refreshes its scene target inside the same project. Regular saves retain the generation. The project manifest uses version 2; scenes save as version 3, with protected migration from older formats.


## Automation document operations

The [MCP/live document methods](mcp.md#document-tools) borrow this same
SceneDocument/EditorFiles/ProjectLease authority. A separately enabled file grant
permits Save, Save As, New and Open within the current project. Its connection
survives scene generations inside that project, refreshes routing sessions and
retains transport receipts; ordinary scene grants still expire on generation
changes. Project changes and lost ownership revoke these current-project grants. A separate
explicit Projects-folder grant can instead retain access across approved projects;
see the workspace contract below.

Synchronous file methods use a noninteractive owner preflight, reject unsettled
work and other dirty source drafts, and never enqueue a later popup/action. New
and Open require a clean scene or explicit discard. Successful discard uses the
normal recovery cleanup; failures preserve the old scene/recovery. Saving under
a changed filename emits the normal file-change event, so editor selection/view
activation follows the existing scene lifecycle. File publication is not scene
Undo.

A separately selected MCP Projects folder permits creation/opening inside that
canonical workspace and return to the initial project. Other project changes
revoke the connection. Scene targets refresh on approved transitions and receipts
survive, so an uncertain Create/Open must reuse its original sequence rather than
create another project. The file controller still owns all transitions and leases.

Project settings tools borrow the visible settings draft, validate admitted fields
and use its ordinary saved baseline. Their content digest rejects stale edits.
They preserve native/build fields for the corresponding native-code owner.
Recovery tools review a bounded snapshot digest, then call the existing restore /
discard paths. Current-scene recovery remains undoable; untitled recovery resets
routing/history as before. Busy controls can resolve the recovery owner's own
popup, while foreign UI, pending tasks and unsaved source guards remain intact.
