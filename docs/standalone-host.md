# Standalone graphical host — Phase8 checkpoint

`FORGE_BUILD_GAME=ON` builds `forge_game` on Windows. It links the shared SDL,
Diligent renderer and RmlUi presenter, simulation and OS persistence adapters.
It does not link ImGui, editor authoring or source asset importers.
`FORGE_BUILD_EDITOR=OFF` is supported; upstream GUI tools are disabled in that
configuration. Shared rendering composition is in `cmake/presentation.cmake`.

This is the Development standalone host. The shared
[export operation](runtime-content-packaging.md) assembles supported runtime
content, the executable and admitted native-module deployments. Windows complete
distribution acceptance is still in progress. No new numbered editor package
accompanies this source checkpoint.

## Startup and ownership

`forge_game` with no arguments admits `forge.standalone.json` beside its executable,
never from the current working directory. `--project <directory>` explicitly uses
a prepared development project's `forge.project.json` instead. The admitted
configuration supplies game defaults, clock, physics, input, module declarations
and startup Scene AssetId. A persistent
`game.application_id` is required. GameStorage reads user overrides from the OS
user-data directory without changing shared defaults. Native SDK modules require
the exact compatible shared-SDK host/profile. Legacy string module declarations
are rejected explicitly.

`load_game_scene` resolves the registered AssetId, checks matching source identity,
reads supported scene3/4/5 and prefab1/2 documents, and gathers declared prefab
dependencies into the existing `_prefab_sources` snapshot envelope. It never
migrates, writes sources, imports, scans directories or invents entity IDs.
Admission bounds are64MiB per document,256MiB total and16,384 prefab identities.
Scene::restore_snapshot remains the Flecs candidate realization authority.

One process owns SDL → window → Diligent device → presentation → GameSession.
Destruction reverses that order; module leases outlive worlds. GameSession owns
simulation; FrameRenderer consumes its presentation extraction. There is no
parallel game-object hierarchy or editor camera in the game view.

## Required-resource activation

The graphical adapter performs these steps before activation:

1. Pump/admit animation resources and apply the initial sample to the unpublished
   world, then revalidate physics. No gameplay tick or animation-time advancement.
2. Admit audio sources and enabled navigation dependencies through the existing
   runtime owners and geometry/revision checks.
3. Prepare actual FrameRenderer draw bundles, textures, environment and pipelines
   at the output size. Wait for pending work; reject terminal diagnostics, including
   missing meshes, fallback draws and a missing valid authored camera.
4. Create a second native RmlUi context with admitted documents, fonts, images and
   geometry. It neither draws into the current framebuffer nor receives input.
5. Recheck readiness and publish prepared frame/UI owners with the new world.
   Retire old consumers/world before the next gameplay tick.

Old gameplay/presentation can continue while a replacement prepares. Failure or
cancellation releases only the candidate. UI tickets reject stale activation;
old commands cannot cross generations. Output-size changes require a new UI
candidate. Progress counts represent stages, not bytes or estimated duration.

GPU readiness means native resources and pipelines were admitted and queued on
the owning Diligent context with normal resource transitions. It does not require
device-idle waits each frame or guarantee against subsequent device failure.
Native module external side effects and post-commit device faults are not rollback.
Future resources requested dynamically by gameplay are outside this initial set.

The host currently drives initial loading. Window-title stage feedback appears
while the window remains responsive. Initial loading has a60-second wall-time
timeout. Application transition/menu/SDK orchestration and authored loading screens
remain open.

## Window, UI and input

Windowed size uses SDL client coordinates. Borderless uses desktop fullscreen;
exclusive fullscreen requests SDL's closest supported pixel mode. An unavailable
saved display falls back to the first enumerated display with a log message.
Output pixels come from SDL_GetWindowSizeInPixels; FrameRenderer queries Diligent
device limits. Existing RmlUi integration bounds still apply. VSync controls swap
presentation; minimized windows skip rendering.

Keyboard, text/IME and mouse motion/buttons route through RmlUi before gameplay.
UI coordinates convert client coordinates to output pixels; gameplay deltas use
saved sensitivity. Focus loss and UI key consumption release held gameplay state.
Existing action bindings, including user overrides, feed the fixed-tick consumer.
UI Pause/Resume/Step use the validated command gate. Complete cursor capture,
gamepad/menu contexts, wheel routing, rebind/settings UI and scene/save SDK access
remain required Phase8 work.

Master volume controls the existing miniaudio gameplay group. Offline fixture
results do not claim physical audio output. Startup failures print diagnostics
and return nonzero. `runtime.log` under the game user-data directory records
startup/preparation/shutdown. Rotating structured logs and crash bundles remain
open. Shared runtime/storage core continues to build without SDL/presentation.

## Exact-source notes and validation

RmlUi6.3 `Core.cpp::ReleaseRenderManagers` also releases font resources and updates
surviving contexts. Retirement calls it only after the last context is gone, so
another scene's prepared fonts survive. One presenter still owns RmlUi's globals;
this adds contexts, not competing global interfaces.

Pinned SDL3.4.16 display APIs and Diligent device/swap/CopyTexture interfaces are
reused. Raw Win32/D3D12/WARP setup stays in `game_device_d3d12.cpp`; shared rendering
uses Diligent interfaces. The pinned Windows Release build settings disable RTTI;
service access follows RuntimeWorld's known concrete UI/audio ownership, matching
the existing Play worker, rather than performing runtime type discovery. No
dependency pins or scene formats change.

Portable regressions cover bootstrap identity/prefab inheritance/relocation,
preparation failure/cancel/supersession, stale readiness, UI candidate retention,
initial animation time and actual offline master-volume samples.
`standalone_game_workflow` exercises the SDL host on Windows/WARP, captures frames,
clicks UI controls and tests failed/valid graphical replacement and window resize.

Checkpoint evidence (2026-09-23): local core72/72, shared SDK83/83, strict focused
ASan/UBSan/LSan5/5 and native model pipeline1/1. Windows source2a595bf passed76
existing native tests; the new fixture required authored camera setup corrections.
At057d324, run35837706448 passed editor/standalone interactions2/2 and the
editor-disabled standalone workflow1/1, including final link-boundary inspection.
All five final game captures were opened:960×540 running/paused/rejected-load/
replacement and800×600 resized output show visible scene content and readable HUD.
An added center/background pixel check rejects an obscured scene. This is a
diagnostic acceptance scene, not the reference game or physical-device acceptance.
The Windows shared-SDK graphical-host profile remains a release gate; the shared
SDK result above is the portable Linux runtime matrix.

## Exact runtime kit delivery

The editor distribution carries two separate runtime kits: `runtime-kit` for
static-ABI gameplay and `runtime-kits/shared-native-sdk` for exact C++ native
modules. The latter and `NativeSdk` are installed from the same shared graphical
build, ensuring the module's imported Flecs DLL is the same byte revision that
the graphical host carries. Assembly checks source/build identities, file hashes,
and that shared DLL. It never merges different Flecs libraries into one process.
The shared Windows workflow tests the installed SDK, creates module deployment
kits, exports a game, and sends only the resulting folder to a fresh runner.
Source inspection and local mixed-content admission are not Windows visual
acceptance; pending native checks remain explicit in release evidence.

## Loading and standalone diagnostics

`GameSession::loading_state()` returns a copied, owner-thread `LoadingState`.
Its request ticket is temporary correlation only. Supersession records the prior
ticket; terminal cancellation/failure persists until the next request/unload.
Structural preparation and resource admission have separate failure codes. A
post-publication activation failure is faulted, never reported as rolled back.
`UiPresenter::loading` binds bounded `forge_loading_*` host values independently
of the gameplay model protocol and its value budget. Native RmlUi BindFunc and
DirtyAllVariables drive updates; a `cancel_loading()` event queues one copied
ticket only from a live visible document. Reset/publication/supersession retires
stale requests. GamePresentation supplies state; the standalone loop owns cancel.

Service diagnostics carry an owner-local monotonic sequence. Standalone writes
new records once per world generation and reports a gap if the bounded 256-entry
service ring overflowed before collection. Deferred UI errors are logged too.
The log is opened before reading user settings, so corrupt settings are reported
there as well as stderr. File rotation and OS crash bundles remain subsequent
Phase8 hardening, not guarantees of this checkpoint.

The gameplay SDK currently supplies resources, entity creation, simulation and
UI actions, but no session/save host pointers. The session/save bridge still needs
a module/world-scoped request queue, safe host-boundary execution and deliberate
game-owned save-schema/migration registration. Existing GameStorage already owns
versioned slots, atomic writes and validation; it must not be replaced by automatic
ECS dumping. That bridge remains in the authorized later Phase8 SDK block.

### Frame completion and software verification

The primary Diligent swap chain ends the immediate-context frame during Present;
the host does not call FinishFrame a second time. The native fixture asserts
one frame-number increment per presentation. WARP/offline diagnostic mode waits
for its GPU work before presenting, so slow software execution does not overrun
the pinned DXGI half-second frame-latency wait. Physical-device gameplay retains
asynchronous presentation. Acceptance checks keep process logs and reject native
Diligent error messages even if the process otherwise exits successfully. Software
verification is a correctness check, not a gameplay throughput measurement.

## Export acceptance —2026-09-23

Source75bfb73413e3e190432989e0a3356af679627cfd passes Windows shared-SDK
workflow35917936862: six focused regressions, real mixed-content export, production
startup, captured UI interaction, six production rejection cases, fresh-runner
startup without the source project/build/FORGE SDK, and a second move/relaunch
with retained OS saves/settings. Primary and fresh-runner successful logs contain
no Diligent ERROR; shader compiler warnings remain. Software D3D12/offline audio
are explicit acceptance modes, not physical-device or full-reference-game claims.
