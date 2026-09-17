# Implementation status

**Phase 6B source implementation is under validation.** Jolt physics, primitive body/collider authoring, world-scoped service/SDK access and private physics-aware recovery are described in [Physics](physics.md). No Phase6C work is included. The previous verified package below remains the delivered baseline until the new Windows build succeeds.

**Current delivery: Build 260917-000040 — Phase 6A module / gameplay SDK foundation.** See [engine modules](engine-modules.md) and [verified delivery evidence](../changelog/20260917/README.md#phase6a-verified-delivery--build-260917-000040). Sections below retain the historical implementation record; later phases supersede earlier descriptions.

## Phase 6A — verified foundation

Source module lifecycle and a separate internal shared-Flecs native SDK profile are implemented. Flecs imports handle ECS registrations; FORGE validates dependencies, world roles, capabilities and exact native compatibility, then retains code/context leases through world destruction. Static ABI1/editor gameplay stays supported. Rich SDK registration changes require a fresh runtime process; the experimental workflow is headless and separate from ordinary editor Play.

Clean Windows and Linux static suites **15/15** and shared SDK suites **18/18** passed. Windows SDL/editor/native **2/2**, remaining module/services/render/runtime suites **13/13**, shaders and all build identifiers passed. All **29** Build38 viewport fixtures are byte-identical. Local static/shared/sanitizer/editor tests passed. Normal ZIP and both installed SDK archives are verified, including relocated client execution, PE/ELF shared linkage, Windows CRT/import library and Linux executable/SONAME preservation. Interactive Windows acceptance remains for the user.

No dependency upgrade, editor redesign, subsystem integration, AssetHandle or Apply to Prefab. The SDK remains internal/unstable. **STOP before Phase6B; Jolt requires separate authorization.**

## Phase 5.5 — verified foundation

Project-owned settings/input, project path consolidation, schema discovery, typed context services, structured diagnostics and bounded CPU instrumentation are implemented. Clean Windows core14/14, SDL/editor/native2/2 and remaining services/input/render/runtime suites12/12 passed, including D3D12 WARP. All29 accepted Build37 render fixtures are byte-identical. Local core14/editor2/sanitizer10, manual/format/syntax and profiling compile-out checks pass. ZIP/build/manifest/26-page manual verification and package cleanup are complete.

Dependency pins, native ABI1, protocol2, existing prefab/transform semantics and dock layout are preserved. InputMonitor consumes project actions without a C++ edit; gameplay DLL action access belongs to the later SDK. AssetHandle, Apply and Phase6 stay deferred. Interactive desktop acceptance remains for the user. STOP before Phase6.

## Verified on Linux and Windows
- C++20 core and C17 sample module build.
- Scene save/load, unknown data preservation, invalid-document rejection, undo/redo.
- Flecs reflection metadata and per-instance prefab position overrides.
- Runtime JSON-lines protocol, module invocation and rejection of missing modules.
- Generated C++ project compilation, failed-build retention, compatible code replacement, schema-change restart, crash-probe rejection, killed-runtime recovery.
- Plugin checksum checks, staged updates, startup ordering API, restart-bound disable and safe mode.

## Windows desktop foundation verified
SDL3/D3D12 editor; pinned ImGui docking; initial workspace; reflected Position Inspector; atomic Save; undo/redo; persistent Tooltips toggle; Diligent offscreen cube preview.
The preview renderer has passed a Linux C++ syntax check against the pinned Diligent headers. Windows compilation and linking passed in [run 34942452023](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34942452023). The user confirmed rendering, editing, undo/redo, saving, docking persistence, tooltips, and basic stability on Windows. Additional DPI/display configurations remain unverified.

## Current editor increment

Windows build and automated process/scaling tests passed in [run 34945457048](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34945457048), source commit `1ea9350dd8469396303eecaff8c25a0cbbc5e573`. The updated theme, shortcuts, and Play controls still need an interactive desktop check.

- Slate theme with consistent spacing, restrained blue accents, and rounded controls.
- Persistent interface scale, 65–200%: Ctrl+Minus zooms out, Ctrl+Plus/Ctrl+Equals zooms in, Ctrl+0 resets. Keypad equivalents work. Both fonts and widget geometry scale; viewport camera zoom is separate.
- Play/Restart/Stop launch the adjacent runtime through SDL3 nonblocking process pipes.
- Play uses a copy of the authored scene; the viewport previews runtime positions. Inspector edits remain authored changes; Restart applies them to a fresh play world.
- Bounded protocol responses, five-second timeout, runtime exit diagnostics, and recent stderr in Console. Stop/failure returns to authoring; Play starts fresh. Recover can resume the last completed checkpoint after a runtime crash.
- Automated process and scale tests run in Windows editor CI; local Linux tests also exercise the same controller without graphics.

The Native panel creates gameplay source, incrementally builds it, probes candidates, and transactionally activates them in the play runtime. Build-on-save and compiler output are integrated. The sample moves entities along X. The preview is rendered in the editor from runtime scene snapshots; it is not runtime-rendered frame transport.

## Not implemented
- IPC viewport frame transport.
- General reflected schema migration and in-place rich DLL retirement. The separate internal exact SDK supports trusted component/system/observer registration with process-restart updates; ABI1 stays limited to stateless callbacks over host-owned Position data.
- Actual native editor-plugin loading, registration APIs, automatic startup-crash recovery and package UI.
- Asset browser/import/caching/cooking; rotation/scale gizmos; complete prefab overrides; multi-edit.
- PBR game rendering, Jolt, audio, animation, navigation, game UI and standalone playable export.
- First-person reference game, advanced 3D tools, Linux graphics, dedicated 2D editing and multiplayer.

## Known foundation constraints
Scene edits reconcile content inside the persistent authoring world and retain at most 100 whole-document undo snapshots. Large-scene history/performance work remains deferred. ABI1 module tick callbacks must be stateless and must not retain host pointers, create unmanaged threads, or register external callbacks. The CLI pauses stepping while building; the editor continues play during background compilation. IPC is local JSON lines over inherited pipes, not the planned named-pipe/frame-transport service. Plugin packages are trusted local inputs; metadata validation is not executable safety validation.

## Windows downloads
The Windows CI job uploads `FORGE-Windows-x64.zip` containing Release executables, adjacent DLLs and dependency notices. Run 34942452023 produced the first successful Release package, from commit `0b4043142c24d0d237ecb891f0c81f072840a6ad`. Extract the entire ZIP on Windows; the editor executable depends on the packaged DLLs. Native D3D12 compilation requires Microsoft ATL, which is unavailable in the current Linux MinGW toolchain.

The Diligent Release defaults enable AVX2 CPU instructions. This development build requires an AVX2-capable x64 processor and a D3D12-capable graphics driver. Executable and DLL import tables were checked: the package does not require separate MSVC runtime DLLs.

## Native editor integration validation
Local headless integration tests exercise real CMake/Ninja builds, failed syntax retention, compatible replacement, probe crashes, activation-crash rollback, schema restart, source watching, and runtime checkpoint recovery. Windows compilation, both editor test suites, developer-launcher validation, and packaging passed in [run 34984715619](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34984715619), source `f2c1cca8227e8bc375047a5734e529e77960eb35`. Interactive desktop validation of the Native panel remains to be done. Rebuild after reopening an editor session to select a validated module; existing build artifacts remain cached.

## Scene organization increment

The World panel displays nested parent/child entities. Inspector supports rename (Enter to commit), reparent to an entity or scene root, duplicate subtree, and delete subtree. Each command is one undoable edit. Duplicate preserves unknown data and remaps internal parent/base links; opaque plugin-specific references are preserved as-is because their schemas are unknown. Deleting a prefab referenced outside the subtree is rejected. Parenting changes Flecs ChildOf organization; Position remains world-space, with no transform inheritance yet.

Core tests cover hierarchy save/load, stable IDs, duplicate collision avoidance, relationship remapping, invalid reparent rejection, protected prefab deletion, and undo/redo. Windows compilation, core tests, editor native/process tests, and packaging passed in [run 34992573961](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34992573961), source `894890765cc69b6d62249eeb6e03284fac5b4edc`. Interactive hierarchy controls require a Windows desktop check.

Desktop check for scene organization:
1. Add two entities. Rename each in Inspector, pressing Enter.
2. Select the second entity and choose the first in Parent. Expand the first in World.
3. Select the parent and Duplicate subtree; expand the copy and check its child.
4. Delete the copy, then Undo and Redo. Undo again, Save, close and reopen; check the hierarchy.
5. Try parenting the original parent under its child. The command should be rejected in Console and leave the scene unchanged.


## Viewport navigation increment

The Scene view has an editor-only orbit/fly camera: MMB-drag to orbit, Shift+MMB-drag to pan, RMB-drag to look around, and hold RMB with WASD to fly at 5 world units/second. While holding RMB, Space raises altitude and Shift lowers it along world Y; both together cancel vertical input. Wheel zooms; F while hovering the image frames the selected visible block. Drag left/down with MMB to reveal more of the right/top faces; Shift+MMB left/down moves the camera right/up (horizontal pan reversed per user preference). RMB look keeps the camera position fixed. Navigation acquires Scene focus on a press directly over the viewport, even when World was selected. Frame selected, Fit scene, and Reset view buttons provide equivalent framing/reset controls. Camera navigation leaves scene data and running gameplay unchanged. Ctrl+Plus/Minus still scales the interface. The original increment reset the camera when reopening; the later scene-authoring tools add explicit view bookmarks, picking, and translation handles.

Projection uses +Y up, +Z forward, a 60-degree vertical field of view, and D3D depth [0,1]. Five float4 constants explicitly carry object position/aspect and camera basis/projection data, avoiding matrix packing ambiguity. Framing uses the visible diagnostic cubes' bounding sphere and the narrower viewport angle; orbit pitch and zoom distance are bounded. This remains the block diagnostic renderer, not production game rendering.

Local camera geometry, process/scaling, and native integration tests pass. Windows compilation, both embedded HLSL shaders, core tests, and editor suites passed in [run 34995049508](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34995049508), source `6edca48aba1c4db538bd865516907a6af3d354b1`. Camera interaction on a real Windows desktop remains a user check. Desktop check: move blocks apart, Fit scene, orbit/pan/zoom, frame a World selection with F, resize the Scene panel, and verify Inspector positions stay unchanged. Check that mouse gestures over other panels do not move the camera and Ctrl+Plus/Minus still scales the UI.


Camera input correction: headless ImGui event tests exercise MMB, Shift+MMB, and RMB acquisition from World focus, requested motion directions, stationary look, held-RMB flight, release/focus-loss stopping, and rejection of drags begun outside the image. Camera math tests cover four flight directions and normalized diagonal speed. Windows compilation, shaders, core suites, and editor input/process/native tests passed in [run 35002353166](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35002353166), source `11c65cd15f6d490b2705b61690df067caf629c5e`. Corrected controls still need an interactive desktop check. The previous build's navigation was user-confirmed functional, with the control mapping and panel-focus changes requested above.


## Permanent performance status bar

The bottom status bar reserves docking space and wraps fields on smaller windows or larger UI scales. It displays editor FPS and mean frame time, editor-process CPU usage normalized across all logical processors, editor resident RAM in MiB, VSync-off status, Edit/Play state, and authored entity count. Contextual tooltips explain each counter. CPU/RAM exclude separate gameplay and compiler processes; unavailable counters display `--`. Sampling occurs approximately every half second. No serialized scene copy is required to obtain the entity count.

Rendering calls Diligent `Present(0)`, with no application FPS cap. The pinned Diligent backend requests tearing where supported in windowed mode; driver/compositor settings can still affect observed presentation rates. Frame-loop timing is not a GPU execution-time measurement. GPU utilization and runtime-worker telemetry are not implemented.

Local headless tests pass for altitude input and release behavior, reversed pan, telemetry arithmetic, and bottom-bar layout/docking separation at 65%, 100%, and 200% scale in 640- and 1440-pixel windows. Windows compilation, native CPU/memory counter validation, shader compilation, and editor suites passed in [run 35004329757](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35004329757), source `2fe63eeab60af141a04692db865c2e129fbdec20`. Live presentation rates and the final status-bar appearance remain desktop checks.


## Toolbar spacing and contextual help

The top toolbar reserves extra space above and below its controls, scaled with text size, while retaining regular button sizes. Docked panels begin below the full padded toolbar. Existing workspace layouts and interface zoom remain supported.

Shared tooltips now wait for approximately 0.4 seconds of hover, require a stationary pointer, wrap long text, and prefer placement outside the hovered control (below, above, then beside it). Large surfaces with no external space use an on-screen placement away from the pointer. Tooltips are suppressed during mouse-button gestures; disabled controls retain help and the global Tooltips toggle still applies. Local ImGui tests cover spacing, docking clearance, tooltip delay/non-overlap/wrapping/edge placement, disabled controls and toggle at 65%, 90%, 100%, and 200% scale. Windows compilation, shader checks, core suites, and editor UI/process/native tests passed in [run 35006382837](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35006382837), source `e518bbd01d117cea5dbb30b59e7245b49b5055c5`. The user confirmed toolbar spacing and tooltip appearance on Windows.


## Project and document authoring batch

Added project creation/opening, versioned manifests, recent/last projects, native file/folder dialogs, New/Open/Reload/Save As, dirty indicators, guarded switching/quit, keyboard editing shortcuts, 30-second autosaves, and named/untitled recovery. See [Projects, scenes, and recovery](projects.md) for behavior and limitations. Local core and editor suites and a Windows-header syntax check pass. Windows compilation, core suites, native iteration (7.06s), and editor/document/process/UI tests (6.15s) passed in [run 35009462534](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35009462534), source `e7e0a4ff39494331b94ccfd4137a98da2e0f6cbb`. Build 000005 passed ZIP CRC and manifest SHA256 verification. Native dialog interaction and recovery modal appearance remain desktop checks.


## Build identity and end-user manual

The product uses `Build: yymmdd-counter` with UTC dates and a globally increasing counter, continuing after 000005. No release-channel labels are used. Editor title/Help, executable `--version`, startup logs, package metadata, and the offline manual share the identifier. Scene, API, and IPC compatibility versions remain independent.

The separate [user manual](../manual/README.md) contains 15 function-based pages covering the implemented editor, with plain explanations and how-to steps. Packaging includes Markdown sources and a navigable offline HTML edition. **Help → User Manual** requests the default browser; **Copy build information** copies ID/source. Technical documents remain here in `docs/`.

Local core/editor regressions, Windows-header syntax, manual link/render/identity tests, release reservation rollover/re-download tests, and actionlint pass. Windows build **260915-000006**, source `4a0de76101959a89e8d6a8f4ade91b2d4215a7f1`, passed all jobs in [run 35012535659](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35012535659). Native iteration passed in 6.85s and editor/document/UI tests in 6.14s. Both executable `--version` checks matched the reserved ID. ZIP CRC, manifest hashes, source identity, and manual edition were verified. The offline manual was visually inspected in Chrome on Linux; launching it through Help on Windows remains a desktop check.


## Scene authoring tools

Added nearest-block viewport selection, selection outline, world-axis and viewing-plane move handles, snap/temporary Ctrl snap, one-command drag undo, cancellation, an XZ reference grid overlay, adjustable flight speed, per-scene view bookmarks, hierarchy filtering/expand-collapse/alphabetical siblings, a bounded project JSON scene browser, and Inspector reset/ground/snap commands. Scene/project opening restores a saved view bookmark if present. See the [Viewport](../manual/editor/viewport.md), [Content browser](../manual/editor/content-browser.md), and [Inspector](../manual/editor/inspector.md) guides.

Move tools support Position translation only and are disabled during Play. Grid and selection lines are editor overlays, not depth-tested against blocks. The Content browser discovers scene candidates; it does not import assets. Camera bookmarks are explicit Save view snapshots, not continuous camera autosaves. Build **260915-000007**, source `650bb9168e8da5d3ebeda0cee6e1e7acbf755470`, passed all jobs in [run 35020103984](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35020103984). Windows native iteration passed in 8.62s and editor/authoring/UI/process tests in 6.25s, including mouse selection, center/axis dragging, cancellation and snap at 65/100/200% scale. ZIP CRC, manifest hashes, source/build identity, and the 16-page manual were verified. Desktop movement feel, overlay appearance, Content interaction, and bookmarks remain user acceptance checks.

## Primitive blockout authoring

The user accepted build 260915-000007. This batch adds Cube/Sphere/Cylinder/Plane creation, reflected rotation/scale/color/shape, staged Inspector drags with one-step undo and Escape cancellation, transform copy/paste/reset, transformed ground placement, shared mesh rendering/picking, and transformed framing/outlines. The packaged Examples/Blockout project demonstrates all four shapes. The manual now contains 19 function-based pages.

Local core and headless editor tests cover inheritance, mesh intersections, bounds, invalid components, clipboard, and scaled Inspector gestures. Build **260915-000008**, source `5dde749111fbbfe2fdc5e9456edc8032d2343a6d`, passed all jobs in [run 35032778793](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35032778793). The compatible Windows cache restored; native tests passed in 7.06s and editor/process/UI tests in 6.33s. Both shaders and executable build identities passed. ZIP CRC, manifest hashes, reserved identity, 19-page manual, example project, and x64 PE were verified. The offline Transforms page was visually checked on Linux; new GPU appearance and interactive controls still require desktop acceptance. This remains blockout rendering: no imported meshes, materials, shadows, physics, or rotation/scale viewport handles.


## Shared authoring commands and inspection

Adds the UI-independent forge_authoring target, sixteen reusable operations, one-undo grouped edits, versioned memory-only CLI sessions with target/revision checks, query/schema discovery, scene diagnostics, and the keyboard command palette. Built-in reflection now includes stable qualified property IDs, defaults, units, ranges and binding metadata. Existing UI editing routes through these operations. The Windows package includes forge_tools and an automation example; the manual expands to 22 pages.

Local core/API/CLI and editor suites pass, along with opt-in Clang AddressSanitizer/UndefinedBehaviorSanitizer tests and Windows-header syntax validation. Build **260916-000010**, source `a951e194c16b6f9aa03107b3664b2d5cb103bde0`, passed all jobs in [run 35052237714](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35052237714). The Windows editor was built from a fresh dependency/build directory: native tests passed in 6.06s, editor/process/UI in 6.51s, authoring API in 0.16s and CLI/example in 0.19s. Shader compilation and all three executable build identities passed. ZIP CRC, manifest hashes, source/build identity, 22-page manual, examples and x64 executables were verified. The user subsequently accepted this build. See [Authoring API](authoring-api.md) for contracts and limits. At that checkpoint, live-project automation and writer coordination remained pending; the following bundle adds them. MCP, generalized asset documents and animation remain pending.


## Project ownership and live-authoring bundle

Adds exclusive OS-held project writer ownership and an explicitly enabled local editor connection with read-only/edit capabilities. The same shared authoring operations and undo history serve live scripts; tokens and session identities revoke on document transitions. Numbered replay receipts prevent duplicate commits, and busy UI/play/build activity blocks external edits. File/native execution remains unavailable through automation; MCP is still planned. Includes a Python example, connection controls, path/control-directory checks and process/socket/UI regression tests. Build **260916-000011**, source `cd38214b6f855043400208ea5e3e4df05c70280a`, passed all jobs in [run 35055914934](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35055914934). Fresh Windows editor build: native tests5.43s, editor/process/UI6.53s, authoringAPI0.14s, live transport/process-lock tests1.31s, CLI/example0.18s. Shaders and all three executable build identities passed. ZIP CRC, manifest hashes/source/build identity, x64 executables, both automation examples and23-page manual verified. Local ASan/UBSan core/API/CLI/live checks passed. User desktop acceptance for this new connection workflow remains pending.

## Layout and transform interaction bundle

Source now includes a compact Scene toolbar/View menu, Hierarchy and Gameplay Code panel names, Window visibility/reset controls with existing ini migration/backup, grouped Inspector transforms with staged Position editing, structural scene-file recognition, explicit project identity, and a personal Scratch project on fresh direct launches. Stop Play to edit through the Inspector, toolbar, shortcuts, palette or API. Local automation lives under Tools and displays an API-on indicator when active.

Viewport R/S modal tools support X/Y/Z constraints, numeric entry, transient previews and one shared authoring command per confirmation. Rotation composes world/view-axis rotation about the object origin; scale is uniform or explicitly local-axis to avoid unrepresentable shear. Exact six-axis perspective camera views and a clickable/dragging Y-up orientation widget are implemented. Scene-v1 storage, world-space organizational parenting, and native gameplay ABI remain unchanged. This does not add orthographic projection, rotation/scale drag handles, multiselection, tool/plugin registration, or hierarchical transform inheritance.

Local and Windows validation for this bundle is recorded at delivery. Desktop/GPU acceptance remains a separate user check.

Bundle verification: Windows Release [run35063310841](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35063310841), source `2030a567c66ef231c152c82e404d6a82ce5f989d`, Build **260916-000012**, passed editor/native/API/live/CLI tests, embedded shader compilation and all three executable build-identity checks. Linux and Windows core jobs and formatting passed. The downloaded ZIP passed CRC and manifest/source/build checks and contains the matching 23-page offline manual. New desktop interactions still require user acceptance on a real GPU.

## Build 12 startup regression

Desktop testing found that the workspace migration held its input stream open while replacing `workspace.ini`. Windows rejected the replacement and startup exited; prior tests covered the string migration but missed the real file lifecycle. The fix extracts startup preparation, closes the reader before replacement, retains an existing backup, and loads the converted layout in memory with automatic/exit layout saves disabled if preparation fails. Windows replacement errors now include source/destination paths and the OS error. Regression coverage includes real disk migration/restart, Build 12 backup reuse, backup failure, in-memory settings loading and a Windows-only open-reader reproduction/retry. Windows package validation is recorded below when completed.

Fix verified in Build **260916-000013**, source `bef18c9041199bb9f5835070343d67924c544081`, [Windows run35065027543](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35065027543). All jobs passed, including the Windows open-reader reproduction and migration/fallback/retry tests. Downloaded package identity, manifest hashes, ZIP CRC and matching manual were verified. The user confirmed successful desktop launch and appearance.

## Viewport performance and daily history

Adds an in-repository daily [changelog](../changelog/README.md), a linked README badge, smaller orientation controls and fixed world-origin X/Z axis clipping. Unchanged editor reads now share revision-aware snapshots; Inspector controls no longer serialize the whole scene independently. Preview and runtime snapshot generations invalidate correctly across editing, cancellation and Play. Unchanged static EDIT geometry retains its offscreen image; moving cameras, content/size changes and Play redraw it. Future time-dependent EDIT content must supply invalidation or opt out before activation. This is still the primitive preview renderer, not production extraction/culling/instancing.

Tools → Performance exposes CPU wall-time sections and a continuous-redraw comparison switch. GPU timing is not implemented. The Linux Release workload covering scene reads and Inspector UI (excluding viewport/GPU/Present) measured approximately58→22 microseconds with one selected entity and93→20 microseconds with two. These measurements confirm reduced CPU waste but do not explain the complete reported Windows frame-time difference or establish target GPU performance. Local Release core7/7 and editor2/2 suites, Windows-header syntax, formatting and manual checks passed. Windows delivery evidence follows after packaging.

Verified delivery: Build **260916-000014**, source `8acad03b639adc20f8d4e28446ed23b601b0a44b`, [run35120058853](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35120058853) passed every job. Windows Release native/editor/API/live/CLI checks, shaders and all executable build identities passed. Downloaded ZIP CRC, manifest hashes, reserved source/build, three x64 executables and23 matching offline manual pages verified. The real Windows/GPU performance comparison remains pending; see [the daily changelog](../changelog/20260916/README.md) for exact results and limitations.

## Infinite world grid and object tools

The previous camera-following finite grid plus separate axis overlays is superseded by a Diligent fullscreen ground pass. It intersects camera rays with world Y=0, derives gray/red/blue lines from the same world coordinates, antialiases with screen derivatives, blends decimal grid divisions and fades toward the horizon. It uses the scene depth buffer without writing depth; opaque geometry occludes it. Grid settings join the retained viewport frame key. This is editor reference rendering, not a scene floor or a new runtime renderer.

Explicit Select/Move buttons and Q/W shortcuts replace the persisted visibility checkbox; checkbox-era settings default to Move, then the named tool choice persists. Existing manipulation and flight contracts remain. New Windows WARP readback tests exercise the production renderer, complementing the existing CPU math and scaled UI tests; validation results follow at delivery. Performance work is paused.

Verified delivery: **Build 260916-000016**, source `4d709a8640830818bac26d014aca087c3d57755e`, [run 35124840824](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35124840824), passed every job after a cache miss/fresh editor build. Actual D3D12 WARP render/readback checks passed, including grid-level brightness continuity; native/editor/API/live/CLI tests, all four shaders and executable identities passed. Final grid/pan/top/occlusion and adjacent grid-level images were visually reviewed. ZIP CRC, manifest/source/build hashes, three x64 executables and 23 matching manual pages were verified. Build 15 was internal baseline validation and was superseded before delivery. User desktop/GPU acceptance remains pending; performance work is paused.

## Grid appearance refinement

Build 16's broad lines and late fade were rejected in desktop review. The next revision uses a narrow pixel-coverage filter, muted colors, horizontal projected-scale grid selection and gradual grazing-angle attenuation. The mathematical reference is Blender's released [grid shader](https://github.com/blender/blender/blob/8cb6b388974a817afedf1317ce26f0c75aa5f181/source/blender/draw/engines/overlay/shaders/overlay_grid_frag.glsl) and [shared line filter](https://github.com/blender/blender/blob/8cb6b388974a817afedf1317ce26f0c75aa5f181/source/blender/draw/engines/overlay/overlay_shader_shared.hh), read directly along with its draw setup. Current Blender source was also inspected; its newer line-geometry/overlay-AA approach differs from the released procedural implementation. FORGE retains its existing Diligent ground pass, Y-up convention, depth occlusion and independent palette; this is not exact Blender pixel parity. New rendered tests measure line width and progressive horizon contrast. Delivery results follow after Windows validation.

Verified in **Build 260916-000017**, source `80f5e5e0080c61cd91b3f9a55da734497d64ff6e`, [run 35132589498](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35132589498): all jobs passed with a restored Windows build cache. Production-rendered line-width/horizon/transition/navigation/depth tests passed on D3D12 WARP; images were visually reviewed. Native/editor/API/live/CLI, shader and executable identity checks passed. Package hashes, x64 executables and 23 matching manual pages verified. Desktop appearance acceptance remains pending.

## Phase 1: persistent world ownership

Implemented in `c0bd61f` with Windows-only fixture correction `68c59dc`; delivered as **Build 260916-000021** from [clean run 35159609066](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35159609066). See [World ownership and scene authoring](world-lifetime.md) for actual ownership, state authority, preparation/history, effective reads and limits.

Applications own WorldContext; scene edits/load/unload/history reconcile content without rebuilding worlds. Registration runs once per context. Flecs supplies all five known live component values and relationships; unknown fragments remain document-owned. Scene-v1 IDs/formats/world-space transforms, current prefab behavior, UI, caller-stepped runtime and v1 native ABI remain. SceneDocument's source location and whole-document drafts/history remain bounded debt.

Executed validation: Linux core 8/8; Linux editor/native 2/2; ASan/UBSan/LSan targeted 3/3; Windows core 8/8; Windows Release editor/API/lifetime/native/WARP suites 7/7; shaders/build identifiers, format/manual/cache/workflow checks. All 24 accepted Build 17 images and all 27 Build 19 images match byte-for-byte. Package/source/manual/hash verification completed. Build 20 failed compiling a new WARP fixture variable name and was superseded without delivery. Interactive desktop acceptance remains separate; no performance or full-engine-completion claim. At this Phase 1 checkpoint, Phase 2 and later architecture were not implemented; the Phase 2 result is recorded below.


## Phase 2: persistent identity and minimal asset metadata

Implemented typed UUIDv4 entity/asset identities, scene-v2, legacy alias and migration records, ordinary-save identity retention, whole-scene duplication with new identities, membership-scoped entity resolution, and the separate minimal forge_assets metadata service. Existing authoring, scene lifetime, transforms, prefab semantics, runtime clock/native ABI and viewport remain the baseline. No separate DocumentId or loaded AssetHandle is introduced. See [identity and assets](identity-assets.md) for exact scope and limitations.

Local validation: core9/9, targeted sanitizer4/4, manual3/3 and Windows-target syntax checks passed. Local editor/process/input/migration/native suites: 2/2 passed. Clean **Build 260917-000025**, source `d98c0a5387506caa5983ad76afb12a52da3b4c9e`, passed [Windows/Linux CI](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35167379024): Windows core **9/9**, Windows editor/native/process/identity/API/WARP **8/8**. All 27 WARP fixture images match Phase 1 Build 21 byte-for-byte. Shader compilation, executable build identifiers, ZIP/manifest hashes, three x64 executables and 23 matching manual pages were verified. The delivered ZIP is `260917-000025-FORGE-Windows-x64.zip`.

Build 22 was superseded before dispatch, Build 23 before delivery for exact scene-version validation, and Build 24 failed compiling a new MSVC test assertion; the explicit-string assertion correction is included in Build 25. No failed candidate was delivered. Desktop/GPU acceptance remains for the user. Phase 2 is complete; Phase 3 and later changes have not started.


## Phase 3 — hierarchical transforms

Current source uses independent authored LocalTranslation/LocalRotation/LocalScale and transient instance-owned WorldTransform. Spatial binding separates FollowStructure, World and Explicit from structural ChildOf. Scene-v3 migration preserves IDs, independent prefab channel ownership and old world-space parent behavior. New parenting preserves placement and follows the parent. Shared authoring conversion handles gestures, compensation, cycles, deletion and shear rejection; renderer/picker consume full affine transforms. See [technical contracts](transforms.md) and [user guide](../manual/editor/transforms.md). Earlier sections record historical behavior.

Local validation and final Windows delivery evidence are recorded in the daily changelog. Phase 4 timing, structured prefab work, physics/animation and dependency changes remain outside this phase.


### Phase 3 verified delivery — Build 260917-000031

Clean [Windows run 35173713866](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35173713866), packaged source `e2841c61006891b98b830c857ff7407022144ba3`, passed every job: Windows core10/10, editor/native/process/transform/identity/world/API/live/CLI/WARP9/9, Linux core and formatting. All four shaders compiled and all three Windows executables reported the reserved build ID. The new affine/shear WARP fixture passed projected-face picking and inverse-transpose lighting checks. All27 accepted Build25 render fixtures are byte-identical; the new hierarchy image was visually inspected.

ZIP CRC, manifest hashes, reserved source/build identity, three x64 PE executables and23 matching manual source/HTML pages were verified. Package promotion archived Build25 and left only the current ZIP and executable folder; cleanup dry run is a no-op. Interactive desktop acceptance remains the user's check. Phase3 stops here; fixed timestep, structured prefabs and physics/animation remain deferred.


## Phase 4 runtime timing

Runtime-owned 60 Hz fixed ticks, bounded catch-up/debt diagnostics, protocol2 nonblocking process control, Pause/Step/Resume, derived local-pose interpolation and transactional first-live-tick native activation are implemented. See [runtime timing](runtime-timing.md) and the [Play guide](../manual/editor/play-mode.md). Clean Build260917-000035 passed Windows core11/11 and editor11/11 (two process/native suites plus nine render/runtime/API suites), including D3D12 WARP. All28 accepted Build31 viewport images are byte-identical. Linux core11/11, local editor2/2, sanitizer6/6 plus runtime pipe1/1, manual and formatting checks pass. ZIP/build/source/manifest/manual verification and package cleanup are complete; interactive Windows desktop acceptance remains pending. See the [daily changelog](../changelog/20260917/README.md) for delivery evidence. No dependency or scene-format changes; no Phase5, physics or animation integration.


## Phase 5 structured prefabs — verified Build 260917-000037

Current source adds AssetId prefab documents, stable member/instance identities, immutable Parent/IsA realization, explicit component/scalar overrides, independent local TRS, Revert and direct source publication. Content/Inspector/source controls, project loading, missing-member diagnostics and isolated runtime snapshots use the same core model. See [technical architecture](prefabs.md) and [user workflow](../manual/editor/prefabs.md).

Apply is deliberately absent. Successful source publication affecting the scene clears scene history; it does not claim scene Undo can undo shared asset edits. Legacy prefabs stay compatible and are not automatically migrated. Nested/structural overrides and later engine phases are deferred. Windows core12/12, editor/native2/2 and remaining prefab/render/runtime suites10/10 passed in clean run35189296706. Linux, sanitizers and all28 previous render fixtures passed; the new prefab render equals the ordinary hierarchy. Package/build/manual verification and cleanup are complete. See the daily changelog for detailed evidence. Interactive desktop acceptance remains pending; stop before Phase6.
