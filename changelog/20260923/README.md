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
- Initial validation: focused shared SDK5/5, strict ASan/UBSan/LSan5/5, native model
  pipeline1/1, portable preparation/UI/animation/audio and corrected bootstrap pass;
  manual3/3, formatting, source syntax and workflow lint pass. Windows execution
  and visual acceptance were pending; completed native results are recorded below.

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
  Standalone interaction and editor-disabled build/link checks pass at4053f3b.
- Opening the native captures found two fixture defects that input assertions
  alone missed: its camera also inherited legacy implicit-cube rendering, and
  absolute HUD labels had insufficient layout width. Match existing non-mesh
  authoring semantics, add an authored light/material, correct HUD layout and
  assert that the rendered cube differs from its background in every capture.
  Final corrected visual acceptance is recorded below; no new ZIP.

### Graphical-host checkpoint — verified

- Final native source057d324 / Windows run35837706448: editor/standalone actual
  input workflows2/2 pass (51.46s); editor-disabled standalone1/1 passes (4.29s),
  with final link checks excluding editor/ImGui/import tooling from forge_game.
- Opened all five final running, paused, rejected-load-retained, replaced and
  resized captures. The authored scene is visible, UI text stays readable and
  output adapts from960×540 to800×600. Assertions and visual review are recorded
  separately; physical GPU/audio/display behavior is not claimed.
- Reuse76 passing native regressions from2a595bf: subsequent changes corrected
  the fixture, not production rendering. Local core72/72, shared SDK83/83, strict
  focused sanitizers5/5, model pipeline1/1 and manual3/3 pass.
- Keep the game target explicitly opt-in when restoring the current editor-package
  profile and when cleaning audit configuration for cache reuse; workflow lint passes.
- Technical status/host docs and the separate end-user manual reflect this source
  checkpoint. Full export, shared-SDK graphical-host Windows acceptance, remaining
  input/character/collision/session SDK work and the reference game are still open.
  Build260923-000066 remains the current download. No numbered ZIP was produced.

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

## Phase8 export closure — source checkpoint, not a numbered delivery

- Extend the shared reflected-reference inspector with unfiltered typed collection:
  missing IDs, nested/partial prefab intent, spatial EntityRef scene scope and
  environment textures are included; unknown payloads remain explicit diagnostics.
- Add bounded Scene/Prefab adapters to the existing content packager and catalog
  graph. Preserve authored identity/inheritance and source files; copy admitted
  document revisions and load their prefab closure after relocation.
- Extract the shared reference inspector into a runtime-independent library without
  pulling editor/application services into its consumers.
- Add missing/opaque dependency, source preservation, repackage, environment
  reference and prefab inheritance regressions. Rebuilt Linux static2/2 (25.46s),
  shared-SDK2/2 (23.69s) and strict ASan/UBSan/LSan2/2 (29.00s) pass. Manual3/3,
  target-boundary checks, formatting and diff checks pass. This is focused source
  validation, not full Windows game-export/shared-SDK-host acceptance.
- Reproduce a pinned RmlUi6.3 export-completeness gap: a missing hover-only decorator
  image passes initial native preparation and fails only after hover. Full UI/game
  export is not claimed; dependency-declaration contract review is required.
- Update technical packaging documentation and the content-packaging manual.
  Build260923-000066 remains the current download. No new ZIP.

## Phase8 declared runtime dependencies — implementation in progress

- Adopt the approved single-graph contract: reliable static dependencies remain
  automatic; finite typed declarations are required Runtime edges; observations
  remain advisory. Retain old observation semantics instead of promoting them.
- Add source-revision review, conflict checks and single-catalog declaration saves.
  Preserve declarations across reimport and keep cooked binding validation separate
  from declared conditional dependencies.
- Add native isolated UI inspection and raw UI/legacy Ozz package admission, with
  manifest-gated runtime reads and explicit undeclared-resource diagnostics.
- Add the selected-asset Inspector dependency draft with typed search/drop, reasons,
  module ownership, observed-resource promotion, remove/reveal/open and save/discard.
  Native Windows visual validation is pending; this is not a shipped editor claim.
- New relocated native UI tests cover hover/focus/active states, hidden literal
  images, never-observed declarations, stale sources and type/missing rejection.
  Additional runtime family, shared-SDK, sanitizer and Windows validation continues.
  The complete standalone distribution/export work is still open. No numbered ZIP.

- Keep automatically discovered UI resource identities durable: authoring preflight
  registers them once; low-level immutable packaging rejects unknown UI identities.
  Scene/prefab bytes and their detached reflection preparation remain unchanged.
- Add the standalone manifest reader, read-only runtime configuration and strict
  default graphical-host startup. Runtime-kit installation resolves native DLL
  imports with CMake and inventories files/notices. Distribution production,
  recovery/export UI and native Windows kit acceptance remain in progress.

- Focused Linux validation: content/UI/manifest/material4/4 and strict ASan +
  UBSan + LSan5/5 pass. Native UI worker retains its512MiB process bound; sanitizer
  supervision uses the matching normal worker and also executes the native static
  inspector directly under sanitizers. No sanitizer suppression is added.
- Shared gameplay-DLL dynamic request test passes after content relocation; model
  recipe/reimport declaration regression and legacy Ozz relocated playback pass.
  These are focused Linux results, not full Windows graphical/export acceptance.

### Standalone export assembly and editor task — continued source work

- Added shared CLI/editor export service, verified runtime/module deployment inventories,
  exact SDK worker admission, standalone configuration, last-good staged replacement,
  cancellation and interruption recovery. Unknown output folders are preserved.
- Added Run/Command Palette Export Game task, progress/cancel/result location and Game
  defaults in Project Settings; global project/import locks cover active export work.
- SDK CMake helper collects exact module import dependencies and finite explicit dynamic
  libraries. Runtime-kit installation collects game dependencies and notices; editor
  packages now include the validated kit. Native Windows installation remains under test.
- Allowed already validated built-in module declarations in standalone startup. Corrected
  durable Windows metadata writes to use existing long-path conversion.
- Local standalone service tests pass assembly/rebuild, corrupt kit, cancellation,
  unrelated-folder rejection and interrupted replacement. Added input-driven dependency,
  settings and export capture steps; Windows execution/visual review is still pending.

- Added production `--verify-startup` package admission/render check and a separate
  packaged input/capture observer. Added exact shared-SDK standalone CI and a second
  fresh Windows runner with no checkout, original build, project or SDK. Execution
  evidence is pending; these are required acceptance gates, not claimed passes.

- Full regression found that the low-level `--assets package` command had acquired
  a source-project writer lock during automatic UI preflight. Restored its read-only
  contract: authoring preflight belongs to Export Game, while low-level packaging
  requires existing registered metadata. Strengthened conditional UI tests to assert
  actual texture loads, not merely absence of diagnostics.

- Export now rechecks native module bytes across worker inspection and final
  promotion, preventing a rebuilt module from replacing the exact inspected one.
  Added an actual SDK-worker regression for source replacement during export and
  incompatible deployment fingerprints; native Windows dependency collection
  remains separately validated by the shared standalone workflow.

- Validation: full rebuilt shared-SDK85/85 passed131.07s; new real-worker module
  export regression1/1 passed3.65s. Strict ASan/UBSan/leak checks for export recovery
  and actual conditional UI texture loading2/2 passed19.70s. Core74-test run's
  packaging regression was corrected; targeted UI/CLI/socket rerun3/3 passed6.13s,
  and the live-authoring socket test also passed with required local socket access.

- Windows compiler validation caught ambiguous C++20 JSON comparisons in raw
  animation/UI export admission. Compare explicitly decoded digest and byte-count
  values; retain the same rejection rules. Windows rerun remains pending.

- Expanded the standalone fixture with production-imported glTF material/texture
  data, cooked audio, converted Ozz animation and baked navigation. Added a shared
  native fixture source generator for portable admission checks. Local mixed-content
  export admission passed; the expanded Windows presentation test remains pending.
- Preserve unsaved Runtime Dependencies drafts across switch/close requests,
  returning to their Inspector owner with a Save/Discard instruction. Added an
  input-driven guard assertion. CLI UI preflight uses the checked runtime kit's
  font, so command-line tools need no unrelated installed editor font directory.
- Deliver the shared graphical runtime kit and its exact SDK from the same build;
  final assembly checks both provenance and the shared Flecs DLL. Added integrity
  regressions for wrong-build/corrupt kits and updated export/standalone instructions.

- Added copied scene-loading outcomes, measured preparation progress, superseded
  ticket tracking, failure categories and RmlUi loading bindings with ticketed
  cancellation. Portable loading/conditional-UI tests3/3 passed8.94s; expanded
  native loading/cancel captures await Windows execution.
- Standalone now streams sequenced service diagnostics and deferred UI failures
  to its runtime log. Settings loading happens after log creation. File rotation
  and crash bundles remain later Phase8 hardening.
- The shared acceptance workflow explicitly enables CPU asset import tools while
  keeping the editor disabled; its previous configuration had omitted the worker
  target required to construct the mixed-content fixture.

- Reject gameplay publication into the reserved host loading namespace before
  presentation. Export task launch failures now stay in the task/Problems view
  and release the pending writer instead of escaping the editor loop.

- Follow-up Windows compilation found additional JSON/string-view comparisons in
  standalone manifest admission. Decode strings explicitly throughout the affected
  provenance and DLL checks, retaining validation behavior across MSVC and GCC.

- Final focused loading/UI/diagnostic core4/4 passed5.81s; strict ASan/UBSan/LSan
  4/4 passed16.31s. Rebuilt manifest/export recovery1/1 passed3.28s after explicit
  conversion corrections. Windows compiler and capture gates remain pending.

- Tightened distribution self-consistency: require the declared executable and
  Development profile, match game defaults to that profile, and cross-check each
  native module's deployment provenance against its actual packaged file inventory.
  Added missing/orphan provenance, incompatible module metadata and mismatched
  dependency-byte regressions.

- Windows evidence exposed a cache namespace collision: editor restore selected a
  shared-SDK cache with a different CRT profile. Shared-game keys now have a
  separate prefix and explicit SDK/CRT identity; editor configuration explicitly
  resets to its static profile and enables its required asset tools.

- Extended standalone acceptance to create game-owned save data and user audio
  preferences through the real storage service, move the installation a second
  time, restart and verify the same OS user-data scope and saved content. Both the
  build runner and fresh runner check this; no gameplay SDK save bridge is claimed.
  Fixture syntax checks pass; native execution remains a Windows acceptance gate.

- Full rebuilt Linux shared-SDK regression suite passes86/86 in73.89s, including
  the separately identified expected upstream Flecs include result. Fixed legacy
  cache lookup to retain Windows environment-key case handling; a plain copied
  dictionary had lost the case-insensitive ImageVersion lookup. Added actual
  production-host corrupt-preferences rejection/log/preservation acceptance.

- Native Windows kit installation exposed mixed Windows path separators bypassing
  the System32 filter, recursively scanning OS components and reporting a false
  compiler-DLL conflict. Both runtime and module installers now select pinned
  CMake's CMP0207 normalized-path behavior. Unresolved application dependencies
  still fail; no missing DLL is broadly ignored or copied from the OS.
- Final rebuilt Linux core74/74 passed124.81s; Windows kit and visual acceptance
  remain pending this installer correction.

- Native editor interaction caught a real declaration-save defect: Content's
  discovered Scene/Prefab view was incorrectly used as a persisted revision token.
  Drafts now capture the saved index, and the shared save operation admits only
  requested discovered documents into its validated single-catalog candidate.
  Added rejection/no-partial-registration, identity mismatch and stale-save tests.
- Runtime Dependencies fields now stack labels above full-width inputs so labels
  remain readable in a narrow Inspector. The existing input fixture scrolls the
  real panel and exercises Save; no test-only authoring bypass was added.
- Windows static standalone loading/cancel workflow passed6.10s at7f03c18. Agent
  opened loading and cancelled captures: readable state, usable Cancel button and
  retained scene. The editor declaration/export workflow still requires its rerun.

- Windows shared graphical host, native SDK consumer build/install, runtime UI
  package and standalone interaction passed at7f03c18. The manifest relocation
  tests exposed a fixture holding its own Windows project lock during directory
  rename; the fixture now releases that lease before moving the source. Product
  writer protection remains intact. Fresh-runner distribution acceptance is pending.

- Declaration/discovery correction passed rebuilt core1/1 (6.35s), strict
  ASan/UBSan/LSan1/1 (9.82s), shared consumer/export5/5 (22.50s), native editor
  source/fixture syntax, manual3/3, formatting and workflow checks. Updated native
  capture fixture verifies dependency input widths at100% and200% UI scale.

- Windows shared deployment now passes all six selected host/SDK/export regressions
  (30.10s at04afc93). The subsequent real CLI export exposed a borrowed JSON view
  whose temporary owner had expired while reading nonempty module-kit options.
  Retain the owner through iteration and reject non-object module maps clearly.
  Added a real CLI regression and explicit missing-reference rejection for all
  twelve supported graph asset families; no failed export is counted as acceptance.

- Strict ASan/UBSan/LSan export CLI and content-package regressions passed2/2
  (21.39s). Corrected stale manual/host introductions that still described export
  as unavailable, clarified the content-only versus executable workflows and the
  strict no-argument manifest startup. Manual generation/link checks passed3/3.
  Current source features remain explicitly separate from downloadable Build66.

- Windows mixed export reached destination admission and exposed ordinary 8.3 TEMP
  aliases being rejected as redirected paths. Export now canonicalizes its selected
  output boundary consistently with project roots, rejects symbolic-link output
  leaves and retains strict internal metadata checks. Shared manifest/module export
  regressions passed2/2 (5.88s), including symbolic-link rejection where available.
  Actual Windows short-alias acceptance remains in the deployment rerun.

- Full Windows core run at04afc93 passed73/74; the remaining resource-relocation
  fixture retained its importer-owned writer lease. The fixture now destroys the
  importer before moving its source. Product locking is unchanged. Rebuilt shared
  material_pipeline passed (11.81s); Windows rerun remains required.

- Real Windows mixed-content export now completes at2818446, with source and kit
  removal reached. Fixed the relocation fixture's SystemRoot lookup to use the
  Windows case-insensitive environment mapping before constructing its restricted
  PATH. Both primary and fresh-runner probes use that same correction; actual
  production launch/capture remains to be checked by the rerun.

- Native editor04afc93 now completes typed declaration editing, dirty-draft guard
  and Save. Opened actual100%/200% captures. Standardized the ordinary project
  save label to **Save Settings**, matching its close dialog/manual and input
  fixture. Large-scale evidence now scrolls the actual dependency fields into
  view before capture; offscreen geometry alone is not visual acceptance.
- Strict ASan/UBSan/LSan manifest and material-resource tests passed2/2 (33.42s),
  including the canonical output boundary and importer teardown corrections.

- Clarified the shader distribution boundary: cooked custom Shader assets load
  bytecode, while the shared Development renderer still compiles embedded engine
  and UI shader strings through Diligent's platform compiler runtime. No external
  editor/project HLSL files or FORGE SDK are required; the whole host is not
  advertised as free of runtime shader compilation.

- Windows/Linux core and shared-SDK full matrices now pass at8b3911c. Native editor
  workflow90295e8 completes declaration Save, project defaults and actual Export.
  Opened final captures.200% review exposed clipped long action labels in a narrow
  Inspector: dependency actions/groups now use compact labels with stable IDs and
  unchanged help/behavior. Reveal/Open wrap onto separate lines when needed.
  Fixture checks action widths and fully visible scroll targets, and moves away
  from the export button so its tooltip cannot cover the success capture.

- Relocated production launch exposed a missing Diligent Archiver DLL: the exact
  pinned factory explicitly loads it, so ordinary PE dependency scanning misses
  it. Runtime-kit installation now seeds the exact Archiver target alongside
  D3D12, scans both dependency closures and inventories the result. Added an early
  kit assertion and missing-Archiver production rejection probe. No arbitrary
  build-bin copy or hardcoded library output filename was introduced.

- Corrected the mixed standalone acceptance observer to read sampled animation
  from AnimationRuntime rather than a nonexistent authored-scene field. The
  production relocated executable now starts with the corrected runtime kit;
  full capture/fresh-runner acceptance remains pending. Mixed-mode assertions
  require all expected animation/navigation/audio/imported entities, record
  actual runtime values, and identify the stage/condition on timeout.
