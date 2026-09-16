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

- Grid review refinement: interpolate both fine/major grid pairs across distance levels so an existing line does not jump in brightness when it changes classification. This affects grid appearance only.

### Follow-up validation and delivery

- Delivered **Build 260916-000016**, source `4d709a8640830818bac26d014aca087c3d57755e`, from [Windows run 35124840824](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35124840824). All four jobs passed. The editor runner missed its dependency/build cache and completed a fresh build.
- Windows production-renderer checks passed using D3D12 WARP (0.57s), including navigation alignment, grid extent/fading, occlusion and grid-level brightness continuity. Native iteration (6.94s), editor/process/scaled UI (6.96s), authoring API (0.16s), live automation (0.78s) and CLI (0.19s) passed. Four embedded shaders and all three executable build identities passed.
- Visually inspected final rendered grid/pan/top/cube and adjacent grid-level images. Build 260916-000015 passed the initial renderer baseline but was superseded before delivery by the smooth grid-level refinement.
- Verified ZIP CRC, manifest hashes, source/build identity, three x64 executables, 23 matching manual sources and 23 offline HTML build IDs. ZIP SHA-256: `50986bca77b5c7e9ac17baa611651ca6d805a5531ad6af99295acf6d8e772b99`. Test executables and render-check images are excluded from the user package.
- Real desktop interaction on the user's GPU remains an acceptance check: look/pan/orbit around fixed objects, inspect grid alignment, then select a cube and switch Move (W)/Select (Q). Performance investigation remains paused.

## Grid appearance — Blender source review

- Reviewed Blender's actual released 4.5.0 grid fragment shader, draw setup and shared line-filter definitions at source `8cb6b388974a817afedf1317ce26f0c75aa5f181`, plus its current grid implementation at `0d06dbf12428041baec34f93952b660acca0f4fe`. The released shader is the closest match to FORGE's existing procedural ground pass.
- Replaced broad line coverage with a thin pixel filter; muted the gray and axis palette, with subtle major divisions. Replaced the largest-ground-derivative density rule with projected horizontal pixel scale and smoothly disappearing detail levels.
- Added progressive angle-based fading across the ground and colored axes. Removed the camera-height-driven fade boundary; final distance fading now follows the camera clip range. World-zero alignment, infinite extent and opaque-object occlusion remain.
- Added rendered-image regressions for line width at two viewport sizes and zoom distances, progressive horizon contrast, and both grid-level transitions. Updated the viewport manual. Windows validation and desktop appearance review remain pending.

### Grid appearance delivery

- Delivered **Build 260916-000017**, source `80f5e5e0080c61cd91b3f9a55da734497d64ff6e`, [run 35132589498](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35132589498). All jobs passed; the compatible Windows cache restored successfully.
- Actual D3D12 WARP image tests passed (0.71s), including narrow line coverage at 640×400 and 1280×800, zoom, gradual horizon contrast, both grid-level transitions, navigation alignment and occlusion. Windows native/editor/API/live/CLI tests passed (7.41/7.22/0.19/1.33/0.25s), as did all four shader compilations and three executable identities.
- Visually reviewed actual grid, horizon and panned images before delivery. ZIP CRC, manifest hashes, reserved source/build, x64 executables and 23 matching manual sources/offline HTML identities verified. SHA-256: `727a4d2063f8dad2b899cbd5c5614f587f5a80053e7cb9b5db5d23f025b68006`. User desktop appearance acceptance remains pending; no performance claim is made.

### Dependency modernization — Flecs

- Upgraded Flecs 4.1.0 to stable 4.1.6, pinned `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`. New hierarchy/reflection/callback capabilities are available upstream, not adopted as new FORGE features.
- Compatibility fix: explicitly request Position reflection-member entities, which Flecs no longer creates by default. Preserve their documentation; added a regression check. Scene formats, hierarchy, transforms, prefab interpretation and module ABI are unchanged.
- Fresh GCC Debug configure/build and all seven core/authoring/runtime/native/plugin suites passed. Initial tests detected the member-entity startup failure; the explicit registration fixes it. Windows validation follows in the combined upgrade build.

### Dependency modernization — SDL

- Upgraded SDL 3.2.20 to stable 3.4.16, pinned `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c`. Includes upstream Windows input/device startup and file-dialog fixes. No FORGE API migration or input-contract changes.
- Built the new SDL in a separate Linux headless harness with the previously pinned ImGui; editor process/scaling/input/layout and native iteration tests passed. The local harness disables unavailable Linux desktop backends; Windows native event delivery, dialogs and mixed-monitor DPI still require platform/desktop validation.

### Dependency modernization — Dear ImGui

- Upgraded docking 1.92.2b to the stable-release docking tag 1.92.9b, pinned `b48d1afbe8ee8b238e2961dc363a949dd7304e23`; no moving branch. Explicitly preserve the existing bitmap font and live numeric editing despite upstream default changes. No editor redesign or multi-window feature enablement.
- Existing Linux editor/native suites pass with the new Flecs/SDL/ImGui combination. Added SDL queue-to-ImGui keyboard/mouse/window-filtering/focus-loss coverage. Added Windows D3D12 WARP checks for dynamic font textures at three UI scales and external texture IDs; Windows execution is pending. Updated Windows editor/test sources pass local MinGW syntax checking.

### Dependency modernization — build provenance

- Retained current JSON3.12.0, CMake4.4.3 and Ninja1.13.2. Retained Diligent's coordinated snapshot/API256020: current upstream Core/Tools revisions are identical; only disabled FX/Samples differ. Documented all four superproject gitlinks.
- Explicitly select the existing VS2022 MSVC14.44.35207 and Windows SDK10.0.26100.0 in both Windows CI jobs. Recorded newer official VS/MSVC/SDK availability and reasons to retain this supported baseline; C++20/C17, static editor CRT and runtime OS targeting remain unchanged. Clean Windows validation follows.

### Modernized stack delivery

- Delivered **Build 260916-000019**, source `c80f0a4cd9abbc5bc4434ac138bec75733b1b894`, [clean run35152168546](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35152168546). All jobs passed. Toolset14.44.35207 selected compiler19.44.35228.0 with SDK10.0.26100.0. Number18 was cancelled after a source-reservation typo and remains consumed.
- Windows D3D12 WARP (0.66s), native iteration (6.60s), editor/process/input/layout (6.93s), API (0.14s), live (1.40s) and CLI (0.22s) passed; Windows/Linux core jobs and four FXC shaders also passed. New SDL event and font/external-texture rendering checks ran on Windows.
- All24 existing grid/scene PPM fixtures are byte-identical to accepted Build17. Visually checked new font/texture output. Interactive desktop startup, physical input, dialogs and mixed-DPI/docking acceptance remain user checks; no performance claim.
- ZIP CRC, all manifest hashes, reserved source/build, three x64 PE executables and23 manual source pages/matching offline edition verified. SHA-256: `8a7a689d38581ee9c71f2b4c9a6267f48cb7f7eec5fd1dc367e6cf850dbb1269`. Previous package archived; download folder contains only the current numbered ZIP and current extraction.

### Phase 1 — persistent worlds and authoring authority

- Applications now own a narrow EngineContext/WorldContext; Scene borrows it and represents loaded content. Register the existing five reflected built-ins once per context, preserving Position member documentation. Explicit scene membership scopes existing v1 IDs and allows one membership to unload without removing another or world registrations.
- Replace/edit/reset/load/undo/redo reconcile typed content in place. Unaffected authored handles survive; undo restores deleted v1 identities. Detached command preparation creates no validation worlds. Known values, names and relationships come from Flecs; unknown envelope/entity/component fields remain document fragments and survive history and round trips.
- Normal editor/API/diagnostic/render reads now extract effective values from Flecs. Runtime responses add a presentation snapshot while retaining the original owned checkpoint, externally stepped protocol and stateless v1 ABI. Corrected runtime shutdown order keeps module code loaded until scene/world teardown. Editor caches observe direct Flecs writes/removals.
- Preserve existing ChildOf prefab copies/IsA overrides, including implicit prefab flags, without adopting Parent, TreeSpawner assets or new file semantics. A private detached projection remains for uncommitted batch intent/gesture previews only. SceneDocument remains in its existing source location.
- Added focused lifetime, registration, entity-handle, multi-membership, failed/stale batch, opaque data, inherited-value and shutdown regressions. Linux test instrumentation counts real Flecs initialization calls without adding a product test API. WARP fixtures now render Flecs-derived snapshots through the unchanged renderer.
- Local validation: all eight Linux core/API/runtime/native/plugin/live/lifetime suites pass; all three targeted AddressSanitizer/UndefinedBehaviorSanitizer/LeakSanitizer suites pass. SDL/ImGui editor/native harness, format, manual, cache-invalidation and workflow checks pass. Windows/WARP/image comparison follows. No dependency, UI, scene-v1, transform, runtime-clock, UUID or broad SDK changes. No arbitrary OOM/native-hook rollback or performance claim.
