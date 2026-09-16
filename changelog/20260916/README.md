# September 16, 2026

[All days](../README.md) · [User Manual](../../manual/README.md)

Entries are grouped by function. Earlier changes today were reconstructed from the committed implementation and delivery records. Additional changes and validation results are appended here throughout the UTC day.

## Authoring API and commands — Builds 9–10

- Added the UI-independent `forge_authoring` library and API version 1, shared by editor controls, a command palette and `forge_tools`.
- Added capability/schema discovery, stable document/session/entity/property identities, expected revision checks, structured failures and atomic batches of sixteen implemented scene operations. A successful batch is one undo step; failed/stale batches preserve state.
- Added headless automation examples, project diagnostics and command inspection. Scene editing controls now call shared operations. Runtime remains independent of editor application services and ImGui.
- Added local sanitizer checks and core/API/CLI/editor regression coverage. Fixed an MSVC ambiguity in string comparison after Build 9 failed compilation; its number remains consumed.
- Build **260916-000010** passed a fresh Windows build, native/editor/API/CLI tests, shaders, executable identities and package/manual verification. Desktop acceptance was received.
- Limits: one scene document per transaction; no cross-file transaction, general asset-document API or arbitrary native execution.

## Project ownership and local automation — Build 11

- Added exclusive project writer ownership through a retained OS lock. A second editor cannot acquire the same project. Failed project switches preserve the original owner and scene.
- Added opt-in loopback automation with separate read-only and scene-edit permissions, random session tokens, bounded requests and main-thread dispatch. Connection access is revoked on document transitions.
- Added correlated requests and sequenced replay receipts to prevent duplicate mutation after retries. Active UI gestures, Play and native builds block external edits. Disconnects do not undo completed commands.
- Added connection controls and a Python live-scene example, with process-lock, socket, authorization, retry and lifecycle tests. Build **260916-000011** passed Windows tests and package verification. The user confirmed the second-instance ownership message.
- Limits: this is a local FORGE protocol, not an MCP server. File writes and native execution are not exposed through it.

## Layout, transforms and navigation — Build 12

- Renamed World to **Hierarchy** and Native to **Gameplay Code**. Added **Window** visibility/recovery controls, layout reset and migration/backup of existing panel names and docking IDs.
- Consolidated Scene controls into a compact toolbar and **View** menu. Grouped Inspector Position, Rotation and Scale; moved duplicate/delete/ground/snap into **Object actions**. Parent selection shows names, with technical identity under Details.
- Added **R** to rotate and **S** to scale, then **X/Y/Z** constraints, numeric input, Enter/left-click confirmation and Escape/right-click cancellation. Gestures preview without editing authored state and commit one undo step. Rotation uses world/view axes; constrained scale uses local axes. Focus, selection, document and revision changes cancel stale gestures.
- Added the clickable, draggable six-axis orientation widget, exact top/bottom and side perspective views, view labels, opposite-view flipping and persistent visibility.
- Content filters out non-scene JSON. Fresh direct launches create a writable personal Scratch project and subsequent launches reopen the last project. Explicitly requested invalid projects report errors.
- Disabled authored editing during Play across UI and API. Moved local automation under Tools and added the API-on indicator.
- Build **260916-000012** passed compilation/tests but desktop launch exposed the workspace migration file-lock regression below. It is superseded by Build 13.
- Limits: no orthographic view, multi-selection, rotation/scale drag handles, negative scale, shear or inherited parent transforms. Native plugin loading remains planned; existing plugin support validates/stages restart-bound packages.

## Windows startup and workspace recovery — Build 13

- Closed the layout input stream before atomic replacement. The previous open stream prevented Windows from replacing `workspace.ini`, causing startup to exit.
- Preserved existing backups; optional migration failure now loads the converted layout in memory and disables both automatic and exit layout saves, preserving the on-disk original.
- Atomic replacement diagnostics now include both paths and the OS error. Added real filesystem migration/restart/backup/failure tests and Windows-only open-reader failure, preserved-original and retry coverage.
- Build **260916-000013** passed all Windows jobs, package checks and matching offline manual verification. The user confirmed that the editor opens successfully and looks correct.

## Viewport and performance — current bundle

- Reduced orientation widget bounds and endpoint circles by approximately 10%, preserving UI zoom, axis picking and drag navigation.
- Replaced camera-target-dependent colored grid segments with frustum-clipped world axes through `(0,0,0)`. The gray grid patch still follows the view; red X and blue Z no longer have moving finite endpoints. Perspective projection naturally moves their screen positions as the camera moves.
- Removed repeated whole-scene serialization/prefab resolution from unchanged editor frames and individual Inspector vector controls. Editor-only revision caches invalidate on shared authoring edits, undo/redo and document reset. Runtime values use an independent snapshot version, not the authored revision.
- Reused the offscreen texture for unchanged static EDIT views. Camera movement, resolution/content changes, transient previews and their cancellation redraw it. Play always redraws. Camera vectors are calculated once per scene submission. The UI still renders every frame; VSync remains off and FPS is uncapped.
- Added **Tools → Performance**: half-second averages for update/UI, scene render submission, UI render submission and Present CPU wall time; cumulative redraw/reuse counts; a session-only **Redraw static scene every frame** comparison switch. These are not GPU execution timings.
- Added cache invalidation, cancellation/retry and world-axis projection regressions. Existing scaled gizmo/property/move/rotate/scale tests remain part of validation.
- Added this dated changelog/index and a colored README link. Updated the feature manual and technical implementation notes. Daily updates are required by the external agent rules; those operating instructions remain outside the repository.
- Performance results and Windows package verification are recorded below when completed. Linux CPU measurements do not establish Windows GPU throughput or a guaranteed FPS gain.

### Local validation

- Linux Release: core/runtime/API/live/native/plugin suite **7/7**, editor/process/UI/native suite **2/2**. Includes cache source/preview/mode/cancellation/retry, prefab inheritance and world-axis frustum regressions, plus existing scaled interactions.
- Windows-target syntax checked for editor entry point, renderer and editor tests. Formatting, manual navigation/build and CI-cache configuration checks passed.
- Measured unchanged scene-read + Inspector workload in Release: one selected entity **58→22 µs/frame**; two **93→20 µs/frame**. The test excludes hierarchy, grid, GPU rendering and presentation; it is not a prediction of Windows FPS. Windows/GPU acceptance remains a separate check.

### Windows delivery — Build 260916-000014

- Source `8acad03b639adc20f8d4e28446ed23b601b0a44b` passed all jobs in [Windows build and validation](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35120058853). Warm build cache restored successfully.
- Windows Release: native iteration **7.68s**, editor/process/UI **7.17s**, authoring API **0.19s**, live automation **1.21s**, CLI **0.25s**; all five tests passed. Linux/Windows core jobs and formatting passed. Embedded vertex/pixel shaders compiled and all three executables reported the reserved build ID.
- Downloaded ZIP passed CRC, manifest file hashes, source/build identity, x64 executable checks and matching **23-page offline manual** verification. Prior numbered packages remain available.
- ZIP: `260916-000014-FORGE-Windows-x64.zip`; SHA-256: `d6c85ff7b7dae706caf0bf4b0bcf9acad5d9cf8ee635af3f6957b2d6249e0a3b`.
- Desktop/GPU validation of this bundle remains pending. Compare empty/one/two-cube scenes at the same layout and selection, both idle and orbiting; inspect Tools → Performance and its continuous-redraw switch. No specific Windows FPS improvement is claimed from Linux CPU measurements.

## Infinite grid and visible object-tool state — follow-up bundle

- Replaced the finite, camera-target-centered gray grid and separate colored line overlay with one Diligent ground-grid pass. Gray divisions and red X/blue Z axes are all derived from world coordinates on Y=0 using the same camera as scene meshes. This supersedes Build14’s finite-grid/independent-axis approach, which did not resolve the reported visual mismatch.
- The grid has no rectangular boundary. Antialiased lines fade toward the horizon and transition to coarser decimal divisions at distance. The grid works above/below the plane, respects opaque scene depth, and disappears when viewed edge-on or looking away. Grid visibility and spacing invalidate retained EDIT frames.
- Replaced the easily missed Move handles checkbox with explicit **Select / Move** tools. **W** selects Move; **Q** selects Select while hovering Scene. RMB+W flight, text entry and active gestures retain their input ownership. Existing checkbox-era preferences default to Move once; subsequent explicit tool choices persist. Bottom-of-view help identifies the active tool.
- Added camera-to-ground projection and grid cache invalidation checks, plus scaled Select/Move/flight interaction regressions. Added a Windows D3D12 WARP test that renders the actual viewport, reads its pixels and checks world-axis alignment through navigation, grid extent, resize, visibility, spacing, static reuse and occlusion. Its images are retained as a separate CI verification artifact. Embedded grid shaders are compiled alongside mesh shaders.
- Updated the viewport/shortcut manual. Performance investigation remains paused at the user’s request. Windows render-test and package results will be appended after validation.
