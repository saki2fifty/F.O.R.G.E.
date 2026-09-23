# 2026-09-23

## Phase8 graphical host and preparation — source checkpoint

- Add opt-in Windows `forge_game`, with SDL display/window modes, VSync, authored
  cameras through the shared Diligent renderer, RmlUi, saved action bindings,
  mouse sensitivity, gameplay master volume, OS user settings and startup logs.
- Extract shared graphics target composition from editor CMake. The standalone-only
  configuration disables editor/ImGui and import-tool dependencies; raw D3D12/WARP
  initialization stays in a narrow backend adapter.
- Add read-only AssetId-based scene bootstrap and structured-prefab source closure,
  preserving supported scene3/4/5 and prefab1/2 identity/inheritance without migration.
- Gate GameSession activation on host resource readiness. Pending, failed, cancelled
  or superseded candidates cannot publish; activation rechecks readiness. The visual
  adapter checks animation/physics/audio/navigation and real render/UI preparation.
- Add unpublished native RmlUi contexts and publication tickets. Retiring one context
  no longer releases another context's fonts through global render-manager cleanup.
- Add zero-time initial animation-pose preparation and actual offline master-volume
  regression coverage. Reuse SDL key mapping between editor and standalone adapters.
- Add portable failure/identity/prefab/UI tests and a Windows/WARP SDL workflow with
  captures, Pause/Resume clicks, rejected missing-mesh replacement and valid replacement.
  Add standalone-only build/link validation to the existing unnumbered audit workflow.
- Update technical contracts and the function-based manual. Full export, all runtime
  asset adapters, character/collision, complete input/menu contexts, SDK save/session
  access, logging/crash completion and the reference game remain open. No new ZIP.
- Validation: focused shared SDK5/5, strict ASan/UBSan/LSan5/5, native model
  pipeline1/1, portable preparation/UI/animation/audio and corrected bootstrap pass;
  manual3/3, formatting, source syntax and workflow lint pass. Windows execution
  and visual acceptance remain pending the unnumbered audit.

### Native host verification follow-up

- Correct service access for pinned Diligent's Windows Release `/GR-` profile.
  RuntimeWorld installs the concrete UI/audio owners; use the same ownership
  contract as the existing Play worker rather than RTTI-based discovery.
- Check SDL executable-directory failures before filesystem conversion.
- Extend the real SDL host fixture with resize/render-target and scene-retention
  assertions plus a resized capture. Focused native audits now include the game
  fixture when the selected source has the standalone target.
- Full rebuilt Linux core72/72 and shared SDK83/83 pass at c8474e5. The first
  Windows audit found the RTTI compile error before tests; its cached objects are
  reused for the corrected-source audit. At2a595bf, Windows compilation and76
  selected native tests pass; the new host fixture failed because its camera had
  no authored transform. Correct the fixture and retain real camera admission.
  Focused audits also exercise the editor-disabled standalone configuration.
  Standalone execution/visual acceptance remain pending; no new ZIP.

## Phase8 game foundation — in progress, not yet packaged

- Extract reusable RuntimeWorld composition from the Play worker, preserving its
  existing protocol, native module, physics recovery and subsystem ownership.
- Add owner-thread GameSession structural scene preparation, generation-checked
  activation/cancel, replacement/unload, fixed clock controls and fault rejection.
  Resource-ready asynchronous scene transitions remain under development.
- Admit optional project game defaults separately from user display/audio/input
  overrides. Rebinding retains ActionIds and uses the existing input validator.
- Add independent versioned game-save slots/settings with required game validation,
  explicit migration steps, checksums, bounded parsing, writer ownership and shared
  flushed atomic file replacement. Loading/migrating never rewrites a source save.
- Reuse file storage and writer leases through a UI-independent library instead of
  linking game persistence to editor authoring. Document implemented boundaries and
  remaining standalone/export work. Validation results are recorded below as run;
  these changes do not change the delivered Build66 artifact.

Phase8 foundation validation: local static-core70 tests pass (68 in the main run,
two loopback tests rerun outside sandbox socket restrictions); focused shared-Flecs
SDK8/8; strict ASan/UBSan/LSan3/3 without suppression; manual3/3, target dependency
boundaries, formatting and workflow lint pass. Separate processes save/relaunch and
restore supported player state into an unpublished Flecs scene without editing its
authored source. Both CI workflows build the new test target before selecting it.
Windows foundation/follow-up results are recorded below; graphical standalone
export is still in progress.

Windows compile follow-up: explicitly extract the user-document kind string before
comparison. MSVC rejected the JSON/string_view overload accepted by GCC; the fix
retains the same envelope, validation and runtime behavior. Revalidate the changed
storage path and Windows matrix; no dependency or format change.
Add an opt-in Windows-only core dispatch for focused platform corrections. It still
runs both static-core and native-SDK profiles; normal pushes and numbered package
builds retain the full platform matrix. Earlier Linux results remain attributed to
their actual source rather than relabeled as results for another commit.
Correct the new session's initial physics synchronization to pass zero elapsed
seconds, and reset the simulation/input clock with each fresh physics world.
Regressions compare clock and solver checkpoint ticks immediately after scene
activation and after Step; world time is not an application/session identity.

## Phase7 hardening

- Correct idle CPU resource eviction after a failed replacement: released last-good
  bytes can be reclaimed under the explicit memory budget policy. Live leases and
  pending requests remain protected; failure diagnostics survive eviction without
  claiming the old resource is still loaded. Add regression for budget recovery and retry.
- Extend the native editor input workflow through external glTF file drop, import
  review/publication, Content search, Model inspection/reimport/placement, scene
  Undo/Redo/Save and corrupt-source rejection and valid external source reimport, with captures at 100/150/200% scale.
  Geometry observation and input-driving code are test-only.
- Reconcile historical architecture decisions and current resource, camera, renderer
  and user-manual wording. Add the existing in-editor cache controls to the resource guide.

## Validation and delivery

The new resource regression reproduced the original failure; strict ASan/UBSan/LSan
passes after correction. Rebuilt portable validation passes 91/91 (315.29s), with
the approved exact Flecs managed-include leak separately signature-checked as an
expected upstream issue, without sanitizer suppression. Editor process/scale tests
pass normally (17.24s) and under strict sanitizers (46.35s). The Vulkan shared-build,
device, sampler and readback probe passes (3.40s); this does not claim a full Vulkan editor.

All four static/shared core profiles pass: Linux and Windows each run 68 static
tests and 79 SDK tests. The complete native rendering gate passes, including
inside-volume optical assertions and 103 prepared UI stages. The expanded real-input
workflow passes 221/221 steps (61.80s); changed import, recovery, Material and high-zoom
captures were retrieved and opened. Manual generation/link tests pass.

Baseline reviewed: Build 260922-000065. The consolidated correction gets its own
numbered package after the complete release gate; the embedded build identity is
authoritative. Physical acceptance remains PARTIAL / PENDING. No Phase 8 work,
dependency pin, scene identity/format or ABI1 change. Exact SDK consumers must
rebuild against the paired package because the resource contract header changed.

## Asset diagnostics

- Automatic reimport failures now retain the affected AssetId and source path in
  Problems. Selecting the entry navigates to the asset; supported text sources use
  the existing contained source viewer. Successful publication clears the same error.
- The native corrupt-source workflow checks that this context exists and that
  selecting the diagnostic inspects the asset without changing scene state.

- Correct transform-guide contradictions about negative scale, flattened mesh
  picking and delivered prefab tools. Apply to Prefab stays deferred.
- The first expanded Windows input run exposed a test-observation error at startup:
  absent AssetIds must be represented as null rather than serialized as valid UUIDs.
  Corrected the fixture; that run supplies no successful UI acceptance.

## Renderer verification

- Add native readback/captures for the existing inside-facing volume exit path:
  absorption, two-sided-flag independence, mirrored parity and total internal
  reflection above the critical angle. These exercise the current screen-space
  transmission approximation; they do not add nested-volume ray tracing.

## Connected authoring verification

- Extend actual input through creating a reusable Material, editing roughness,
  document Undo/Redo/Save, and assigning it through the typed scene picker.
  Assert that material history/publication leaves scene state untouched, while
  the scene assignment has its own Undo/Redo and Save. Capture the authored
  Material workspace at 100/150/200% scale.

- The expanded Windows run passed the isolated optical checks, including volume
  exit absorption, mirrored parity and total internal reflection. Opened the
  resulting captures. Its input extension exposed missing inactive-tab observation;
  the fixture now observes dock tabs before testing their content visibility and
  selects Content before the file drop.
- The combined renderer test reached its old 60-second CTest limit after completing
  the new optical assertions. Earlier successful runs took 56–57 seconds. Give this
  cumulative device-lifetime test 120 seconds; retain all assertions, separate case
  limits and production GPU-wait diagnostics. This does not change runtime waits.
- Replace the manual's open inside-volume test note with the verified exit-interface
  behavior and its explicit screen-space/nested-volume limits; update the render matrix.
- The fresh configuration passes 71 of 72 native audit checks, including the combined
  renderer. Correct the remaining input recorder: observe the Content child region
  after EndChild supplies its rectangle, and use the compact Actions menu to reach
  Create / Register. The product's Content interaction is unchanged.

- The corrected input audit passes all 213 steps (71.37s); opened import,
  placement, rejected/valid reimport, Material and assignment captures at
  100/150/200%. The valid replacement visibly changes the retained model from
  orange to green. Failed reimport keeps the prior orange geometry and selection
  of its Problems entry inspects the affected asset.
- That visual review found Hierarchy's Collapse all button clipped at 200% in a
  narrow dock. Use the shared responsive button placement so it stacks below
  Expand all. Add a native geometry assertion and scroll to the edited Material
  roughness field for additional captures at all three scales.
- Correct the hierarchy guide's obsolete default-cube and unavailable-prefab
  statements to match current Mesh Renderer, camera/light and prefab behavior.
- The high-zoom Hierarchy geometry assertion and opened capture pass. The extended
  Material input driver must expand Roughness in the stacked layout's own
  disclosure state and scroll at the document edge, avoiding the preview's
  intentional wheel-to-camera-zoom input. Correct the fixture; no authoring
  state is injected to bypass those interactions.
- Final focused Windows input run passes all 221 steps; opened the scrolled
  Roughness value and its contextual help at 100/150/200%. Both Hierarchy actions
  remain reachable at 200%. Correct the manual's sibling-order description to
  match authored Flecs child order rather than obsolete alphabetical ordering.

### Phase8 OS user-data adapter and validation

- Added a separate SDL OS-directory adapter for game saves/settings, with no video
  initialization or editor linkage. Windows uses its native Roaming AppData path;
  Linux uses the SDL XDG location. Invalid Linux directory environments reject
  before entering the pinned native implementation. No install/project fallback.
- Added actual save/load through that adapter and Linux invalid-environment
  regression tests; the portable simulation/storage libraries remain SDL-free.
- Windows CI35820752072 passed70 core and81 shared-SDK tests at source d2f6ceb,
  including blocked save replacement and separate-process restoration. These
  results precede the subsequent world-clock correction and OS adapter changes.
- The world-clock correction passes focused static/shared-SDK/strict sanitizer
  regression tests (2 each). No new numbered package or Phase8 completion claim.

### Navigation delivery

- Cooked-content packages now include baked NavMesh assets using the existing
  admitted navigation envelope and ordinary AssetCatalog. Source scenes remain
  build dependencies; Recast and editor code are unnecessary for package loading.
- Added a real baked-level relocation/query/agent regression with the original
  project unavailable, plus corrupt packaged-navigation rejection. Existing
  fixed-clock, asset identity and geometry-staleness contracts remain unchanged.
- Updated the command-line packaging manual and technical format documentation.

- Follow-up local validation: navigation/package/link tests3/3; shared-SDK
  navigation/package/process/SDK/link tests5/5; strict sanitizer navigation/package
  tests2/2. OS path tests pass in static, shared SDK and strict sanitizer profiles.
  Manual3/3, formatting and workflow lint pass. Windows follow-up results appear below.

### Documentation status reconciliation

- Refreshed the README's outdated primitive-preview/import wording to match current
  implemented rendering/import/navigation/UI features and distinguish internal
  Phase8 services from the still-unfinished graphical game/export workflow.
- Recorded delivered Build260923-000066/source794a88a3/run35815081663 in the product
  status page. Earlier Phase7 evidence above remains unchanged; current Phase8
  work has separate source/test attribution and has not replaced that ZIP.

### Phase8 Windows foundation follow-up — verified

Source `b7558c14c706856647722763b1000054f5c9d50b` passed
[run35823097419](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35823097419):
**Windows core71/71 and shared SDK82/82**, with runtime installation checks. This
includes OS user-data save/load, independent process saves/blocked replacement,
world-clock/solver reset and relocated navigation query/agent/corruption tests.
Formatting passed. The graphical host and complete export/reference-game workflow
remain open; this source-only foundation bundle creates no new numbered editor ZIP.
