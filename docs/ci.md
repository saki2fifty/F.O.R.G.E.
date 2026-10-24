# Build checks and Windows packages

Pushes and pull requests run Linux/Windows core tests, cache-invalidation tests, and formatting. They do not compile the D3D12 editor or publish a ZIP. Superseded push/PR runs on the same ref are cancelled; manual builds are not cancelled by a code push.

Request a Windows package using **Build and test → Run workflow**, with **windows_package** enabled. From the GitHub CLI:

```sh
gh workflow run build.yml --ref forge/windows-build -f windows_package=true -f build_id=YYMMDD-NNNNNN
```

Reserve the build identifier with the release coordinator before dispatch; the example above is a placeholder. The coordinator maintains one persistent, monotonically increasing counter and never reuses failed reservations. Use your intended branch in place of `forge/windows-build`. GitHub's Run workflow UI requires the workflow on the default branch; branch-specific dispatch can also be requested through the API/CLI. The complete user-facing artifact is FORGE-Windows-x64; it includes the editor, exact Native SDK, shared runtime kit and reference game. No new package is needed merely to validate documentation or portable code edits.

## Test profiles

The combined Windows package also prepares **Examples/Movement2D** with its exact
installed SDK. The existing asset importer publishes its five original FORGE
Sprite/Map dependencies before Play. The compiled consumer checks both real Play
hosts, input edges, speed/jump tuning and support on painted/ordinary geometry.
SDK Play uses its own running fixed clock for input tests: Step deliberately
releases gameplay input. The relocated export check deletes the editable project
and temporary kit, runs the production startup verifier, then injects native SDL
D/Space input through the compiled game fixture. It records the painted landing,
jump/no-repeat result and three rendered rest/jump/landed images. This gate is
automated acceptance, separate from the user's physical Windows/GPU test.

The controller extension adds **Examples/Character2D** to the same installed-SDK
build, normal five-asset import, combined-package and relocated-game acceptance.
Both examples share the input/tuning/painted-and-box-floor test and native SDL
game observer; the latter records whether the player is a native controller and
rejects an authored body/collider on that player. Character evidence uses a separate
`character-2d` directory. The installed-only SDK also compiles allowed/denied
character modules and exercises copied state, checked placement, command order,
sizes/contexts/threads and permissions through both actual process hosts.

The existing 2D viewport case also captures the connected-body Gallery76 Game
poses and actual kind-specific joint Inspector/Scene guides at100/150/200%.
The shared-game gate runs the installed joint Client fixture through both Play
hosts, exports Gallery76 without a module, removes source/SDK dependencies and
launches it after relocation. The separate clean runner repeats those startups.
These are required executable checks, not physical Windows/GPU acceptance.

The core and native-sdk presets leave `FORGE_BUILD_ASSET_TOOLS` disabled.
`static_mesh_navigation` still exercises editable-Mesh publication, resource
lifetimes, routing and source-free export in these profiles. Its imported-glTF
extension, model worker source and model-import library links are enabled together
only when asset tools are enabled. Editor/package builds exercise that extension.
Core tests must not link optional model-import targets that do not exist in their
configuration.

`terrain_tools` exercises the import CLI enabled by `FORGE_BUILD_ASSET_TOOLS`;
register it only in that configuration and include it in both full native editor
acceptance selections. Terrain source/cook library tests remain available in core
profiles. Gallery runtime immutability checks compare against the admitted Scene
before ticking; the Gallery source round-trip independently checks every expected
authored value, including additive builtin defaults.

Full numbered editor builds and full editor source audits build CMake's default
target before running their acceptance selection. This compiles newly registered
test executables without maintaining a second list in the workflows. The early
process-controller gate and explicitly focused shader/input audits retain their
limited target selections.

The shared full-editor acceptance selection includes existing `cpu_profile`
collector/process/transport/editor checks. GPU duration lifecycle and actual Game
on/off image checks run inside the existing D3D12 viewport acceptance. The input
walkthrough exercises default-off GPU collection, Start/Stop/Clear and captures
at 100/150/200%. Software WARP results do not establish physical GPU performance.

Both full editor jobs allow 150 minutes overall. A cold Windows source audit
measured 6 minutes of configuration, 63 minutes of compilation and 20.5 minutes of
acceptance before standalone/benchmark work, exhausting the previous 90-minute
job limit. That job-level increase does not change test deadlines or selections.
Core/SDK matrix jobs allow 75 minutes: a cold Windows core run completed all 145
tests and Runtime installation just before its former 45-minute cleanup deadline,
but the overall job was still cancelled. The additional budget covers complete
cold compilation, testing, installation and cleanup without relaxing any test.
The Tile Map source fixture keeps all 262144-cell capacity, sixteen-stroke
Undo/Redo, identity, Save/reopen and four-million-event checks. The complete
[Windows Debug diagnosis](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39480854995)
passes in 538.90 seconds: dense creation 72.43s, initial document admission 286.75s,
reopen 141.98s, sixteen strokes 11.54s, and each disk comparison/Undo/Redo stage
below 0.05s. This fixture allows 900 seconds in Debug for measured unoptimized
admission headroom and retains 120 seconds in other configurations. The full
Windows SDK Release fixture passes in 13.49 seconds at the unchanged implementation.
These are stress-fixture timings, not gameplay performance or frame-pacing results.
For a focused diagnosis, dispatch `build.yml` with `windows_package=false` and
`tile_source_only=true`. The reusable Tile source audit builds the actual fixture
in the selected Windows Debug configuration and measures its complete workload
outside the aggregate CTest cutoff. Detailed dense-history stages flush to the
retained log; source-byte comparisons use the existing bounded block reader.
This diagnostic has a 35-minute job limit and does not replace the full matrix,
editor, export or numbered-package gate.

The expanded native animation capture has 43 scaled images. A Windows WARP run
completed the first 39 captures in 208.15 seconds, then the old 230-second Python
limit killed the process before its per-stage guard could report a stall.
Animation, Save-image and sound-cue captures now retain the fixture's 600-second
whole-process bound, with Python at 630 seconds and CTest at 660 seconds so the
fixture can save failure evidence first. Save-image and sound-cue startup allow
180 seconds for the normal Gallery import queue; their subsequent controls still
have 45-second guards. Periodic fixture diagnostics record the active import
stage and correlated Play command. These allowances do not bypass import or
presentation readiness. The focused native workflow audit also builds and runs
the audio-mixer and viewport captures, including Tile Map document controls.

For a bounded Windows metadata diagnosis, dispatch `editor-audit.yml` with
`workflow_only=true` and `publication_only=true`. This builds the existing
publication fixture and runs only `asset_publication`, retaining its normal unit
and interruption-recovery assertions and flushed reader/replacement stages. It
cannot be combined with shader, capture or performance modes, records its limited
scope and does not replace editor acceptance, the core/SDK matrix or a release gate.

Both full editor routes read `tools/editor_acceptance_regex.txt`; the unnumbered
source gate must exercise the same acceptance selection before allocating a new
delivery number. The shared selection includes `mcp_`: native stdio/configuration
checks and the GPU-independent editor document/MCP process test. These tests are
built with the default editor targets; project files stay in temporary directories.
The 941-action native input walkthrough has a 480-second aggregate CTest budget.
One full WARP run completed all 941 actions in 297.56 seconds; a slower runner
completed 877 before the former 360-second aggregate cutoff, with normal actions
still progressing. The driver's 12-second action/readiness and 180-second compiler/
graph checks remain unchanged. The additional aggregate allowance removes no
actions, pixel/source checks or acceptance requirements. Focused shader/input scopes remain explicitly separate. Numbered
failures retain the full capture diagnostics and CTest logs, including the stalled
state JSON, so a screenshot is not the only available readiness evidence.
The sequence-frame input walkthrough waits for the real export callback to accept
its request before entering settings. A consumed click during an asynchronous
layout or tab change is retried within the original workflow deadline.
The sequence-frame input walkthrough retains its 90-second phase guards and gives
all three scales one 240-second action budget after initial source admission. In
[run39698636222](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39698636222),
initial importing consumed about 80 seconds. Ordinary and fixed exports
at 100% and 150%, both resize checks and ordinary export at 200% completed before
the old combined 240-second cutoff interrupted fixed export setup at 200%. Initial admission
remains bounded by its original 90-second phase guard. Python allows 340 seconds and CTest 360
to cover that initial phase, the complete action budget and failure cleanup. No
input, output, pixel, resize, source-preservation or request-timeout assertion is removed.

The shared selection includes `hdr_bloom` settings/control/Gallery checks and the
`feature_gallery` CLI admission check. New display-color pixel checks also execute
inside the existing native viewport fixtures. Final combined-package acceptance
exports Gallery69 with the shipped tools and starts it with its working sources offline.

`model_large_family` exercises the complete 260-mesh publication, cache reuse and
520 durable member IDs in an independent project, through the existing model test
executable. The normal profile uses the real worker; sanitizer tests use the direct
recipe and configured unsanitized converter. Keeping that scale catalog separate
prevents unrelated animation reload fixtures from repeatedly validating its records.
Both full native selections retain this stress case and the model migration test.

The isolated optics fixture compares the same bloom pass against its independent
pixel oracle with FXC and DXC, each with default and disabled optimization. Every
profile must pass; failures include the selected radius and intensity. Audit
artifacts retain FXC disassembly and shared-frame camera captures for diagnosis.
Bloom captures compare fresh and warmed targets and expose filtered pixels,
sampling coordinates, effect/rectangle values and CPU/device size observations.
They supplement the unchanged numerical oracle; they do not replace it.
Camera bloom isolation compares the entire display image with independently
supplied HDR solids passed through the ordinary display resolve. Its tone mapper
intentionally desaturates bright highlights, so unchanged individual display
channels are not an isolation oracle.
Stalled editor captures retain the runtime status and worker log as well as the
backbuffer. The initial Inspector's unassigned Audio Source placeholder is removed
before the staged fixture enters Play, preserving normal audio asset admission.

The presentation profile disables Diligent's unused super-resolution targets.
The pinned optional DLSS-NGX archive uses the dynamic MSVC runtime, which conflicts
with FORGE's static runtime when CMake's full default target builds that unused
DLL. Preserve the complete FORGE acceptance target build and the selected CRT;
no FORGE code consumes this optional backend.

The graph layout editor fixture links the material resource validator directly;
its inline preview consumer requires that library even when a fixture disables
shader compilation. Both full native selections execute this input fixture.

Full native gates also execute material-animation source/runtime/Gallery checks,
the material Timeline and existing transform/property/sequence Timeline fixtures,
and the native C++ material-animation probe when that SDK profile is enabled.
The installed SDK package test compiles and runs its delivered
`material_animation_gameplay` sample from the relocated host. Shader-only audit
scope remains focused on shader/material compilation and rendering.
The same installed SDK gate runs `sequence_gameplay` through Gallery62 and 68,
checking typed camera-shot references, copied camera selection, sequence controls
and fresh-worker recovery. The native editor input workflow creates camera shots
through Entity/Timeline controls and inspects stopped Game preview at 100/150/200%
scale, including unchanged authored content, gameplay ticks and input capture.
The Vulkan material probe also checks Gallery68's actual camera/geometry pixels
against independently enabled native cameras, with gap, loop and recovery cases.
Final assembled-package acceptance exports Gallery68 through shipped tools and
starts its production EXE after deleting the copied source project and runtime kit.
Source-offline export fixtures end project writer/importer ownership before moving
their source project. Gallery admission failures include the helper's output and
the authored-document difference instead of hiding it in a subprocess exception.

## Cache behavior

Requested Windows builds restore the dependency checkout/build directory and CMake/Ninja build directory under the runner's short `AgentFiles` paths. Configure, build, tests, and packaging always execute even on a cache hit. Product source is freshly checked out. Verified source-content records preserve timestamps only for unchanged inputs, allowing Ninja to reuse compatible outputs; changed inputs rebuild.

The compatibility key includes the runner image, architecture, MSVC version, Windows SDK, CMake/Ninja versions, absolute checkout path, and CMake configuration/dependency pins. The source commit is appended to the cache entry key; a compatible prior commit can supply a restore fallback. There is no fallback across different compatibility keys. Successful native compilation can save an editor cache even if a later test fails; the shared-game job saves its cache after its successful validation. Cache reuse never bypasses required tests or delivery gates. Cache upload failures do not prevent artifact delivery; a failed restore is discarded before a fresh configure.

Enable **clean_build** to bypass both cache restore and cache save for a clean verification. Cache misses after runner/toolchain updates or cache eviction are expected. The first build has to populate the cache.

This uses the official [cache restore/save actions](https://github.com/actions/cache), pinned to v6.1.0 commit `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`. Cache matching and scope follow [GitHub's dependency-cache rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).

The repository name ends in a period, so Windows cache actions use a scoped Node preload to set their working directory to the short checkout. The preload affects only those cache processes. **Cache transport check** verifies a real save/delete/restore round trip whenever that shim or its diagnostic workflow changes; it can also be dispatched manually.

For a focused model-reimport investigation, dispatch **Cache transport check** with
`model_reimport_source_run` set to a successful complete Windows package run. This
skips the cache probe and runs the packaged tools twice against an independent
Gallery copy with a restricted PATH. It verifies the original package manifest and
source identity, retains selected/candidate indexes and structured conflicts, and
checks stable member identities or last-good metadata retention on failure. It
does not rebuild the engine, change the accepted package, or replace delivery gates.
Set `model_reimport_legacy_run` to an older successful package run to use its Gallery
with the current package's shipped tools. Both original manifests and source identities
are verified independently; neither archive is modified or relabeled. This checks the
real format3/4-to5 migration, durable member keys and all original Gallery asset IDs.

## Measured results

On September 15, 2026, the [cold Windows editor job](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34989524125) took **8m50s**. The [next-commit cached job](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34990556960) took **3m00s**, approximately 66% less time, including setup, cache transfer, tests, and packaging. Ninja executed 14 build steps instead of 579; configuration fell from 3m24s to 27s and compilation from 4m10s to 35s. Both editor test suites passed. This measures dependency reuse across a documentation-only commit with freshly checked-out product sources; timings vary with changes and runner conditions.

A [routine push](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34987648929) completed its core/format checks in **1m52s**, with editor packaging skipped. These are separate workflows in practice: fast validation on pushes and an explicit package build when a Windows ZIP is needed.


## Build identity and manual

Product identity is `yymmdd-counter`: UTC date plus a counter padded to at least six digits. It has no release-channel suffix, and the counter never resets with the date. A packaging attempt consumes a reserved identifier; re-downloads retain it. The existing sequence continues after build 000005. Scene formats, module ABI, and IPC retain independent compatibility versions.

The `build_id` workflow input is required for Windows packaging. CMake embeds it and the source commit in both binaries and writes `build.json`. CI checks both `--version` outputs before packaging. Unassigned local builds cannot be packaged by the release script. The final Windows ZIP contains one manifest covering the editor, SDK, runtime kits, reference game, manual, examples and license notices. Publication verifies the reserved source and exact artifact bytes before accepting a numbered delivery.

End-user source pages live in `manual/`, separate from these technical documents. `python tests/manual_test.py` checks supported formatting, navigation, escaping, and identity validation. Packaging renders the current pages with `tools/build_manual.py` into a standalone offline HTML manual and includes the Markdown sources. The renderer intentionally supports headings, paragraphs, flat lists, fenced code, bold/inline code, and local page links; unsupported block forms fail validation. No web service or extra documentation dependency is required.

For an unpackaged local editor build, generate the manual beside the executable with `python tools/build_manual.py --output /path/to/build/manual --build-id unassigned`. Help opens `manual/index.html` through the OS handler. A browser-launch success only confirms dispatch to that handler; desktop opening remains an interactive check.

## Complete Windows package delivery

The final Windows ZIP is assembled only after core/static, shared SDK, editor
and formatting jobs succeed. CI builds the editor, SDK, shared graphical runtime
and reference game separately, then verifies their hashes and exact source/build
identity before producing one user-facing archive and one manifest.

The ZIP keeps Run-Forge.cmd and Run-Forge-Dev.cmd at its root. Executables and
their DLLs live under bin/; NativeSdk/ contains C++ headers, libraries and its
matching runtime. The shared graphical host lives in
runtime-kits/shared-native-sdk/, while runtime-kit/ supports the static export
profile. ReferenceGame/ is the compiled example; Examples/ contains editable
projects. The manual, resources and license notices remain at the root.
Source builds retain their executable-relative layout. The app resolves the
installation root from the parent package manifest when launched from bin/.

The package job extracts the one archive into a private relocated test
directory, verifies every manifest hash, runs the reference game and installed
SDK, imports through packaged workers with a restricted PATH, and executes the
real editor SDK workflow. The editor fixture EXE is copied beside
bin/forge_editor.exe and uses only shipped DLLs/resources. A failed acceptance
gate prevents ZIP upload. The test-only fixture/project/private user-data
directory are not shipped. WARP validation does not claim physical GPU or
audio acceptance.

Every numbered editor ZIP includes the complete editable project under
`Examples/FeatureGallery/`. Packaging fails if its manifest, README, scenes or
project are missing, and rejects links. Its complete regular-file inventory,
including cooked audio/navigation and UI source/images, is copied without an
extension allowlist. A ZIP-writer regression checks every curated Gallery file's
bytes and manifest digest, an unfamiliar extension, and link rejection retaining
the previous archive. Other sample projects use the source-extension filter.
The same final-package test copies
the extracted gallery into a writable project, exports its saved startup scene
using the packaged static runtime kit, and runs the resulting standalone
executable in startup-verification mode. The source-level `feature_gallery` test
admits every gallery scene through the normal authoring API and checks durable
identities and key components.

The shared-runtime audit exports Gallery73's Sprite fill, Gallery78's image regions
and Gallery74's Tile Map through the same saved-project export helper. It deletes
each authored copy and copied kit before two relocated production launches with
restricted PATH. The region game and Tile Map game travel to the separate runner
with no checkout, build or SDK, where two more launches per game and exact inventory
checks run. The shared build explicitly
produces the tile Gallery and real Play-transport test executables before running
them; an old cache cannot supply unbuilt acceptance fixtures. The installed-only
Tile Map module uses the matching public SDK and exercises private runtime edits.
These startup checks complement independent Sprite/Tile Map native pixels,
document input and C++ value tests; they do not establish physical GPU acceptance.
The fresh-runner job has 20 minutes for its thirteen bounded host launches and
transfer/cleanup; each host keeps its existing 120-second process deadline.
Final combined-package acceptance repeats these Gallery exports with the tools,
Gallery content and shared runtime kit extracted from the delivery ZIP.

Sprite region coverage reuses `sprite_region`, `sprite_scene`, `sprite_gallery`,
`sprite_editor` and the existing native renderer/frame-chooser fixtures. The
installed-only Sprite C++ probe writes all five region fields through public
component storage; its process check retains cold recovery and exercises both
ordinary and prepared Play hosts, live values, numeric/identity rejection and
authored-source isolation. No additional Sprite process protocol or resource owner
is introduced.

The official artifact download action is pinned to v4 commit
`d3f86a106a0bac45b974a628896c90dbdf5c8093`. Modified files, path escapes
or mismatched builds fail assembly before replacing a usable output. Installed
SDK consumer tests still verify shared linkage, compiler compatibility and real
gameplay modules before assembly. A package-only retry may reuse an unchanged
validated compiled-source run while retaining its build/source identity.
The editor fixture routes audio through `AudioOutput::Offline` for the runner;
this checks authored clip decoding without claiming a physical audio device.
A package-only retry does not rebuild or relabel previous delivery binaries.
Independent Gallery export cases assign unique application IDs to their temporary
copies so one scene's fixture save cannot become another case's initial state.
Each observer checks its saved Scene identity and a distinct OS storage root;
its evidence is retained with the package checks. The dedicated relocation tests
still move and reopen the same application and require its saves to persist.
Gallery observers retain their native motion checks and publish case-specific
reports: carriage poses, connected-pair positions, swing angles, spring gaps or
animation states. Package acceptance checks each report's matching fields.
The C++ onboarding workflow scrolls the live Inspector to Rotator Speed before
requiring the complete enabled field to be visible and testing runtime tuning.
The SDK reference workflow tolerates an unpublished scene snapshot while recording
startup diagnostics. It retries an unaccepted initial toolbar click at most twice,
500ms apart, only with a stopped untouched session and an enabled visible button.
Reported startup failures are retained; stage and overall deadlines are unchanged.
The native editor input walkthrough observes entry into the existing Play action
callback in fixture builds. It repeats a physical toolbar click at most twice,
500ms apart, only when that callback was never entered. The per-step deadline is
unchanged. Earlier
session logs or unrelated earlier workflow errors do not hide a missed click.
An entered callback is never replayed, including one that reports a startup error;
the following runtime-readiness check must still pass. Audio preview captures wait
for initial source-import and Content catalog refresh readiness before clicking
the real waveform control. Selected-revision admission remains unchanged.

The native editor workflow records a personal Duplicate shortcut through the
ordinary Preferences route and exercises its effective command at 100/150/200%.
Final SDK package acceptance retains only that fixture's private preference bytes,
starts a fresh editor process with a new project and restricted system-only PATH,
then checks the loaded binding, old-key suppression and Reset/default dispatch.
It retains before/after captures. This uses the existing fixture/input workflow;
production editor command-line arguments and project input remain unchanged.

## Content-verified incremental Windows inputs

The audit and package editor jobs store `forge-source-stamps.json` with the build
cache after configuration/build work. It records SHA-256, byte size and modification
time for tracked regular source files. On compatible cache restore, unchanged bytes
recover their original input times, so a fresh Git checkout does not force Ninja to
recompile the whole project. Changed/new inputs are newer than the cached completed
build even if a file copy preserved an older timestamp. Deleted files remain deleted;
symlinks and paths outside the checkout are not modified.

This does not change dependency/toolchain/CRT compatibility keys or skip configure,
build or tests. An older cache without a manifest uses normal rebuild behavior and
records a manifest for subsequent runs. `clean_build` bypasses this optimization.
`tests/ci_cache_test.py` runs a real Ninja dependency check, including modified header
bytes with a deliberately preserved timestamp. Cache reuse is not validation evidence.

Curated projects under `samples/projects/` retain exact source and cooked bytes
through Git checkout. Their selected artifact metadata records raw source digests;
automatic LF/CRLF conversion would mark otherwise unchanged assets for rebuild
and alter packaged examples. The repository attributes disable text conversion
for these projects. A real checkout test enables Windows-style conversion, proves
that project bytes stay unchanged, and checks conversion still applies elsewhere.

## Input-driven editor review

`editor_input_workflow` launches the real editor fixture on Windows/D3D12 WARP
with a disposable empty project and preferences. Unlike the staged presentation
fixture, it performs authoring exclusively through queued mouse/key/text input:
Entity → Create menus, Name and transform fields, Undo/Redo, Save, Delete,
Reload from disk with unsaved-change Cancel/Discard, Camera/Light creation,
interface zoom, Play/Pause/Step/Stop.
The production SDL event loop handles interface-zoom events; Dear ImGui handles
widget input. Test-only probes observe the actual submitted widget rectangles.
They do not execute actions, force menus open, or change scene data.

Assertions check resulting authored values, saved/reloaded data, preservation on
Cancel and restoration on Discard, entity counts,
camera output, paused state, and exactly one simulation tick after Step. The
workflow opens the Problems tab after Play, records diagnostic text/severity, and
rejects unexpected error/fatal domain diagnostics. Warnings and the separate
renderer console log still require review; a passing assertion set does not
certify a clean renderer log.
Screenshots are read from the actual rendered backbuffer at workflow checkpoints.
Missing/disabled controls and failed assertions have bounded timeouts, a failure
capture, and a JSON action/state trace. `workflow.json` records source/build,
backend, window size, UI scale, target rectangles, and capture filenames.
Evidence lives in the `FORGE-Editor-Source-Audit` artifact under
`editor-workflow` (or `grid-test-images/editor-workflow` after extraction).

Both the source-audit and Windows package test selections include this test.
For a source-only run, dispatch **Build and test** with `audit_source` set to the
full commit SHA and `windows_package=false`. For changes confined to native
input/capture fixtures, set `audit_workflow_only=true` to build their required
editor/runtime targets and run the process, input, physics, root-motion,
sequence sound-cue, Scene/Game image-export and standalone workflows. Image-export
failures retain changed files and a hash receipt that detects additions/removals.
The image fixture waits for the normal startup import owner to settle before
freezing its complete source/catalog baseline and issuing any Save image input.
That initial source-preparation phase has a 90-second deadline and records its
elapsed time. Once the baseline is frozen, the ordinary 45-second control-stage
budget starts afresh; readiness, exact pixels and preservation checks are unchanged.
The existing 180-second process and 210-second CTest budgets remain in force.
The separate `editor_sprite_frames` workflow opens Gallery72 in the actual editor,
selects its static Sprite and opens the published image sheet at 100/150/200%.
It captures the real native UI, confirms pending keyboard selection, checks one
Scene Undo/Redo edit and verifies that Cancel preserves redo history. Saved
project, Scene and asset files remain byte-identical. This fixture has its own
210-second CTest budget and appears in both full acceptance and focused input
selections. Physical desktop/GPU acceptance remains separate.

The separate `editor_sequence_frames` workflow drives the actual Timeline action,
Game focus, frontmost export modal, configuration Cancel/restore, settings input,
folder-picker adapter and current/fixed four-frame PNG batches at 100/150/200%.
Fixed mode selects an actual preset, enters custom 320×180 dimensions, resizes the
real SDL window during rendering/saving, checks unchanged target dimensions and
restored ordinary Game sizing, and compares exact PNG bytes across all three UI
scales. Each resize also verifies that the whole centered progress modal and its
Cancel control remain inside the actual window; modal bounds travel with the receipt. Its dedicated fixture mode bypasses legacy scene seeding/capture stages.
Normal source import
must settle before its protected baseline; source/catalog bytes remain unchanged
during all six exports. It retains modal images, frame receipts and failure
state, with a 240-second internal deadline, 250-second process timeout and 270-second
CTest budget. Only the OS folder choice is injected. The separate
`editor_render_sequence-frames` case compares actual Gallery 70 Timeline/Game
pixels at 0/1.5/3 with independent synchronous GPU reads, including the camera cut,
current size, all three presets and custom landscape/portrait sizes. The offscreen
host completes each native graphics frame just as swap-chain presentation does;
Flush alone is not graphics frame cleanup.
Both cases belong to the full editor acceptance selection; workflow-only audits
include the native modal workflow without claiming the additional renderer case.
The separate sound-cue Timeline walkthrough also waits for normal source imports
and the Content catalog scan to settle for three frames before opening its clip
snapshot. Startup preparation is bounded at 90 seconds and recorded in capture
metadata; its ordinary 45-second control deadline starts after that preparation.
It retains the same ready-controls, stable-layout, foreground and source checks,
with a 180-second process timeout inside a 210-second CTest budget. This prevents
startup publication from invalidating the test's clip snapshot before cue input.
Original scenes, project settings and raw asset sources must remain byte-identical
through startup and capture; only importer-owned sidecars/catalog may refresh
before that baseline, preserving every asset ID/source/type. No source/catalog
change is allowed across image export. Synthetic legacy cube/prefab setup belongs
only to the legacy capture starting at stage0. The root-motion fixture waits for an object-valued animation
status before checking availability; startup may legitimately have no observation. Root-mode
Inspector/document captures require their controls to be ready and the panel
bounds/scroll unchanged for three consecutive frames. Target-guide captures also
require the actual Scene/Game marker to remain visible across those ready frames.
Other Game captures retain their separate settling requirement. Captures scroll
required live IK and source-control groups into view at large UI scales and
verify that the intended document owns foreground focus. Each image records its
readiness, focus and domain diagnostics in chrome JSON; the harness verifies all43
images and retains partial logs on timeout. Its 170-second process budget stays
inside the existing 180-second CTest limit; the fixture retains its 45-second
per-stage stall check. Hosted WARP measurements require this aggregate allowance
for all43 images, including the first isolated Play startup.
The default remains the full source audit. The artifact records the selected scope; focused success is not a
full-suite result. Use the full audit when shared behavior or rendering changes
require its additional coverage. Locally on a configured Windows
build, use `ctest --test-dir <build> -R '^editor_input_workflow$' --output-on-failure`.
The input-driven source audit also runs the graphical standalone fixture and its
package workflow. The package workflow deletes source/kit inputs, restricts PATH,
moves the exported folder twice, and asserts graph-material pixels after each
launch. The numbered editor gate includes both standalone tests too.
The sheet workflow waits for normal source publication and browser discovery to
settle before beginning input. It uses the same source-baseline helper as image and
sequence captures. Raw source bytes and existing AssetIds/source/type associations
are checked from pre-launch; importer-owned catalog/sidecar refreshes may finish
before that baseline. Every admitted source/catalog/sidecar byte must then stay
unchanged through the full picker workflow. Per-action and process deadlines
remain; startup preparation has a separate90-second bound.

For sheet-picker or waveform regressions, `audit_capture_only=true` selects only
`editor_sprite_frames` and `editor_audio_preview`, using their actual editor host,
asset worker and converter. The root workflow also sets `workflow_only=true`;
this remains a partial audit in the artifact and cannot replace full acceptance.
Direct calls to the reusable workflow must set both flags. Shader and benchmark
scopes cannot be combined with this capture mode. The complete source gate remains
required before integration or delivery.

No package/build-number allocation is needed for a source audit.

A passing test establishes only the listed interactions and assertions. Review
the images separately for readability, clipping, hierarchy, selection feedback,
and useful camera/light presentation. `visual_review` deliberately remains
pending in machine output until an actual reviewer examines the images. Extend
input/capture coverage for changed workflows; this scenario is not exhaustive
coverage of every editor control, OS dialog, drag/drop gesture, GPU, or display.
The existing staged fixture continues to cover broader visual states and scales.


## Editor SDK final-package acceptance

The `FORGE-Windows-x64` package job runs the editor SDK acceptance gate
after extraction. The acceptance helper:

- extracts the final ZIP into a private scratch and verifies the manifest
  (`build_id`, `source_commit`, `files`, `native_sdk`);
- locates the shipped `NativeSdk/` via `manifest["native_sdk"]["path"]` and
  asserts the extracted `bin/forge_runtime.exe` is present;
- copies the `FORGE-Editor-SDK-Fixture` fixture executable beside the
  extracted `forge_editor.exe` (the executable only; matching DLLs and
  resources are reused from the shipped package so a missing packaged
  dependency fails verification rather than being masked by a parallel
  bundle);
- copies the dedicated ordinary editable project (the
  `FORGE-Editor-SDK-Project` artifact, with hidden files such as `.forge/`
  preserved) into a separate private scratch;
- launches the fixture against the extracted editor + `NativeSdk` + project
  with an isolated private user-data directory and a bounded watchdog,
  tearing the process tree down on a stall;
- asserts `complete=true`, that `source_commit`/`build_id` match the
  package manifest, and that `scene_round_trip`, `save_load`,
  `binding_persisted_same_process`, `binding_persisted_restart`,
  `focus_gated_routing` and `sdk_play` are all `true` (boolean, not
  integer `1`); the focus-gated routing boolean requires the editor
  fixture's stage-2 surrender sequence (Escape → pause → two-frame
  split-click on `ui_targets["hierarchy:expand-all"]` → record Rml
  focus → send Tab → wait fresh snapshot → assert focus UNCHANGED +
  `game_input_captured()==false` → two-frame split-click on
  `ui_targets["button:Capture gameplay input"]` → wait fresh snapshot
  → assert `game_input_captured()==true` → send Tab → wait fresh
  snapshot → assert focus CHANGED → activate "Resume" → Enter) to
  have run end-to-end with the existing-owner
  `game_input.captured()` observer flips between the captures, and to
  have retained its `sdk-outside-surrender` / `sdk-outside-regain`
  PPM captures only after the Tab assertions;
- asserts every expected `editor-<label>.ppm` capture is present and
  non-empty, and that each matching `sdk-<label>.json` sidecar (including
  the `sdk-restarted-persisted-binding` sidecar) exists;
- retains the fixture stdout/stderr log, all PPM captures, all sidecar
  JSONs, the final `workflow.json`, and a summary into the
  `FORGE-Editor-SDK-Acceptance` artifact under `fixture-output/`, so
  screenshots and failure traces survive on every outcome (success,
  failure, exception).

The acceptance fixture covers the actual menu / play / rebind / save /
scene transition / Quit / restart / load flow through the fixture's own
captures. The acceptance helper, the dedicated editable project, and the
isolated private user-data directory never enter the shipped ZIP.

The acceptance gate is the only Windows CI step that runs the shipped
`forge_editor.exe` against a matching extracted `NativeSdk/` with a
real project, real input, real save files and a real Quit / restart /
load round-trip. It asserts the resulting `workflow.json` plus
`editor-<label>.ppm` captures; it is not a Windows visual review and
it does not claim physical-device verification. The fixture is built
by the editor job and copied beside the extracted editor so a missing
packaged DLL fails verification rather than being masked.

A failed gate blocks the `FORGE-Windows-x64` upload. The gate runs against
the Windows runner's WARP rasterizer; physical-device verification on real
hardware is a separate concern and is not asserted by this CI step.


## Matched editor and shader timing evidence

The full source audit builds disposable `forge_editor_benchmark` executables for
the current source and accepted pre-Phase-7 source
`a98be9a672394d3d91c2b9067331d0252f9b4313`. Both use their revision-local WARP
fixture adapter at device creation; the normal editor event loop and source remain
unchanged. These executables are test artifacts, never numbered deliveries.
The ordinary hardware-adapter startup path cannot be assumed to work on hosted
Windows runners. Neither rebuilt binary is described as the shipped baseline ZIP.

A separate job runs both executables on the same Windows runner, alternating their
order for three repeats each of an empty and one-cube project. It records launch
until a responsive named editor window, then five one-second process CPU and memory
samples after settling. This is not a first-GPU-frame or physical-GPU FPS measurement.
Window dimensions, preferences, image/toolchain, source and executable hashes travel
with the evidence. OS and driver caches remain uncontrolled. The benchmark uses an
isolated runner and restores its previous editor preferences after completion.

The native shader tests additionally record six repeated compile and cooked-shader
realization samples with the same workload/compiler settings. End-to-end artifact
publication and DDC-hit timings remain separate in `shader_worker`. Native fixture
renderer diagnostics include frame, timestamp, SDL window flags and Present phase;
errors must be reviewed independently of successful input assertions.

If only baseline/comparison infrastructure fails, `audit_performance_source_run`
can reuse that audit's `FORGE-Editor-Benchmark-current` artifact. Supply its exact
`audit_source`; artifact provenance must match before execution. The retry rebuilds
the baseline and runs the comparison, while skipping current-editor compilation.
This reuse does not establish successful renderer/UI assertions from the earlier run.

`audit_baseline_source_run` similarly reuses the accepted baseline executable from
a completed baseline-build job. Its immutable source provenance must match before
measurement. This permits current-source corrections without repeating the older
engine build; the benchmark itself still runs both executables on one runner.

Source-audit captures and the CTest log upload immediately after native tests,
before the separate benchmark build and cache upload. This permits image review
while those later steps run. An available artifact is evidence to inspect; it is
not a successful-run claim. Check the test log and exact source identity.

The idle comparison waits ten seconds after window responsiveness before sampling.
The native WARP trace showed first-use GPU work stalled across several500ms frame
waits, exceeding the former three-second settling period. Retain older three-second
measurements as warm-up-sensitive observations, not steady idle evidence.

Native fixture graphics acceptance records unfinished work at a frame-wait timeout
and verifies full completion after the normal shutdown drain. Other graphics
errors and unclassified waits fail the fixture. Raw diagnostics remain visible;
see [backend limits](render-backends.md#hosted-warp-frame-wait-diagnostics) and
[source-attributed performance evidence](editor-performance.md).

The numbered editor job allows 90 minutes for a cold native build plus its complete
regression suite. After a successful compilation, reusable compilation outputs may
be cached even if a later test fails. Such a cache is not accepted delivery evidence:
subsequent runs still configure, rebuild changed inputs, and run all required tests
before packaging. Toolchain/profile/source-content checks remain unchanged.

## Focused shader/material native acceptance

For renderer shader corrections, dispatch `build.yml` with `windows_package=false`,
`audit_source` set to the full immutable source SHA, and `audit_shader_only=true`.
The shader-only selection also runs the native Sprite renderer, including cropped
image pixels. Its independent point-sampling fixtures keep samples away from
permitted fixed-point texel-boundary ties while checking every selected pixel.
This reuses the editor audit's cache and capture machinery, compiling the material/
shader test targets and running patch, material, shader, viewport, morph, skin,
frame and optics checks. It uploads `FORGE-Editor-Source-Audit` with test logs and
captures. It does not reserve a build number or produce a delivery ZIP.

The separate HLSL profile workflow verifies debug/optimized FXC with warnings as
errors against the same staged dependency patch. The GPU differential reference
intentionally compiles the original upstream function with its original warning;
review production warnings separately. A focused audit does not replace the full
core/shared-SDK/editor/relocation/package gate for numbered delivery.

## Phase 9 installed-SDK onboarding acceptance

The final assembled-package job also starts an ordinary empty project and drives
real widgets for gameplay source creation, successful build, failed-build retention,
rebuild, component admission/attachment and a fixed-runtime effect. It creates a
prefab, reviews/cancels/applies instance overrides, checks durable Undo/Redo, and
exports the authored game. The exported folder is relocated and verified without
the source project or developer tools on PATH. Compiler tools remain available
only during the authoring build, using the same pinned MSVC/CMake/Ninja setup as
the SDK. Onboarding traces, compiler diagnostics and captures are retained in
`FORGE-Editor-SDK-Acceptance/onboarding`. Observation probes expose widget geometry
only; mouse/key input performs authoring and scrolling. The unchanged New Project
menu is not claimed as tested merely because the fixture creates a project.

The package gate runs timed reference-game acceptance before compiler-heavy native
onboarding; both scenarios are mandatory. Build117's final package-only gate reused
its unchanged source-validated compiled artifacts. Earlier ordering produced two
reference snapshot deadline failures despite successful onboarding. The passing
isolation run does not prove a runtime cause or a performance correction; no
shipping timing, deadlines or assertions were changed.

### Editable Mesh UV acceptance

The UV batch adds analytic/max-profile source checks to `editable_mesh`, multi-corner UV source history/reopen to `editable_mesh_pipeline`, and actual selected Windows-cook/resource/camera/source-free relocated export checks in `editable_mesh_uv_gallery`. `editable_mesh_uv_editor` performs mouse/keyboard selection, drag/release/cancel, numerical rejection, navigation, lock and publication routes at65/100/200%. The editor fixture's stages115–120 capture UV canvas/tools at 100/150/200% using staged original source data and deliberate document layout/scroll; these images establish visual layout, not a performed authoring workflow. Final Windows package acceptance exports Gallery14 and starts its shipped standalone without Mesh source files.

### Brush symmetry acceptance

`editable_mesh` checks folded reflection against explicit stamps, overlap/pressure,
signed displacement, symmetric versus asymmetric normal fields, bounds and retained
source attributes. `editable_mesh_pipeline` exercises brush history/branch/reopen
and published streams. `editable_mesh_brush_gallery` validates original Gallery15
against independent stamps, exact cooked resources/camera visibility, selected
closure and relocated source-free export. `editable_mesh_brush_editor` performs
actual Tool/Mirror X/Y/Z input, ghost-ring drawing, hold/cancel/lock/release/history/
publication/reopen and sparse-pointer parity at65/100/200%. Native stages121–126
set up original source and drive combo/checkbox input for100/150/200% sculpt/paint
layout captures with explicit undocking/scrolling; they do not prove Content
creation or default layout. Final Windows package acceptance exports Gallery15 and
starts the shipped standalone without authored Mesh sources.

### Authored triangulation acceptance

`editable_mesh` verifies3–16corner fans, canonical IDs, complete UV/color/extension
retention, indexed flat triangle parity, no-op behavior and atomic bad-selection/
count/allocator rejection. `editable_mesh_pipeline` covers Undo/Redo/branch/reopen
high-water. `editable_mesh_triangulation_editor` performs actual face/Mesh actions,
lock, history, publication and reopen at65/100/200%. The Gallery16 suite validates
source/cook/resource/camera and selected source-free relocated export. Final
package acceptance exports and starts Gallery16 with shipped Windows executables.

Mesh shading authoring adds `editable_mesh_shading_editor` (actual mouse controls
at65/100/200%, mode/locks/history/publication/reopen) and
`editable_mesh_shading_gallery` (equal geometry/distinct normals, real root material
selector, resource/camera and exact three-record relocated source-free closure).
Native editor captures130–132 stage an original flat cube and scroll to shading
controls at 100/150/200%; these are layout evidence, separate from performed
workflow and physical acceptance. The combined package exports Gallery17 and runs
its shipped game with `--verify-startup`; source Mesh files must be absent.

### Animation key batch gate

`animation_key_batches` performs real scaled UI selection/drag/action/history/
publication/reopen inputs. `animation_key_batch_gallery` checks complete key data,
fresh copied IDs, analytic cubic vectors/slerp, selected cooked resources, camera/
render admission and relocated source-free export. Native layout fixture captures
133–138 stage original three-key data at 100/150/200% with controlled scrolling;
these captures alone do not prove performed edits or default docking usability.
The combined Windows package check exports Gallery18 and runs its actual EXE with
`--verify-startup`. The installed native SDK fingerprint inputs are unchanged by these authoring APIs.

### Mesh facing gate

Core `editable_mesh` checks connected/vertex-only surfaces, complete corner/UV/color
association, independently oriented cooked triangle sets and normal signs,
flat/smooth/double-reversal invariants and invalid partial winding/undefined smooth
normal failure atomicity. `editable_mesh_facing_editor` performs scaled Model/UV/
history/branch/locks/publication/reopen inputs. `editable_mesh_facing_gallery` uses
actual public material/resource/camera consumers and selected relocated source-free
content. Native fixture139–141 stages reversed original Cube controls at 100/150/200%
with controlled scrolling/undocking; capture increments the stage before exit142.
The final combined package route exports Gallery19 and runs its actual EXE startup.
These authored native routes are separate from executed evidence.

### Core and SDK job allowance

All Linux/Windows core and native-SDK matrix jobs have a 45 minute allowance for
configure, a clean build, the complete applicable test suite and installation.
Run 38780698543 exhausted the former 30 minute limits in three profiles; its Windows
SDK job completed in 32 minutes under the existing 45 minute allowance. Individual
test limits and acceptance requirements remain intact; a timed-out job never
qualifies as passing delivery evidence.

Static native layout captures apply their controlled mouse position after SDL's
platform update and before ImGui consumes input. Brush and vertex-selection setup
use finite scripted frame ranges; subsequent captures hide mouse and navigation
hover again. The `editor_capture_input` test reproduces a platform position
overwrite and proves a real widget receives mouse press/release. This fixture
transport is compiled only into the automated capture executable.

## Physics joint acceptance

Native editor CI explicitly builds/runs physics_joints, physics_joint_editor and
physics_joint_gallery. These cover solver behavior and recovery ordering, actual
reflected Inspector controls, and relocated source-free Gallery20 content. Joint
Inspector captures cover body references and hinge bounds at 100/150/200% scale.
The final assembled-package gate exports Gallery20 with the shipped runtime kit
and executes its startup verification alongside the existing Gallery scenes.
These automated results are separate from physical GPU/desktop acceptance.

Point/fixed acceptance extends the same targets with physics_joint_extended_gallery,
original Gallery21 export and real shipped EXE startup. Native151–156 captures
cover Point body references and Fixed reference-frame controls at 100/150/200%.
The numerical gate compares independent frame vectors and four-type native
swapped-index recovery rather than relying on test counts or labels.

Selected-joint guide acceptance adds `physics_joint_guides`: independent rotated
parent/signed-scale anchors, explicit reference diagnostics and actual clipped
ImGui draw without document/history or active-input mutation. Native157–162 show
Fixed guides and missing-body feedback at 100/150/200%; review the captured images
before claiming visual acceptance. Actual offset-COM native solver tests compare
these derived guides to body-origin anchors for Hinge, Point and Fixed.

Continuous collision extends existing `physics` / `physics_joints` tests and adds
`physics_continuous_gallery`. The full Inspector test exercises the saved checkbox,
Undo/Redo and mode-retained choice. The final assembled Windows package exports
Gallery22 and executes the shipped EXE startup verification with its saved CCD flag.
Native163–165 capture the actual Dynamic Physics Body checkbox at 100/150/200%.
These are Inspector render captures, not evidence that the Gallery animation was
physically reviewed on a desktop GPU. The ordinary-gravity Gallery motion oracle
and source-free relocated content test remain distinct gates.
## Compiled physics example gate

The combined package job builds `samples/projects/PhysicsLoads/Native` using the
same pinned MSVC/configuration and the SDK from that immutable source/build. The
existing installed SDK helper creates its Windows module kit. Preparation copies
the ordinary editable project, obtains the existing C++ source identity from
forge_tools, and inspects its actual schema in an isolated runtime. The combined
ZIP includes this complete project under `Examples/PhysicsLoads`, including the
matching deployment; it does not include the compiler.

The actual C++ runtime test checks independent mass/dt/inertia equations, timed
loads and one-shot recovery. Final relocation repeats the worker check, exports
with the shared native SDK runtime, removes the source project and kit, verifies
the production EXE, and runs the separately inventoried graphical observer. Its
six-actor motion results/capture are uploaded as `FORGE-Physics-Loads-Example`.
The observer never enters the final editor ZIP. Native image review and physical
GPU acceptance remain separate from those automated assertions.

## Body motion tuning gate

Existing physics and real-worker recovery tests include nondefault damping,
speed limits, sleeping and pending commands. The full Inspector test commits all
four numeric fields and the sleep checkbox with Undo/Redo/save/reopen at65/100/200%.
The existing Gallery executable adds `physics_motion_tuning_gallery`, explicitly
selected by native editor CI: six original actors, independent gravity/damping/cap
math, sleep policy, real render bounds and relocated source-free content.
Captures166–168 show the actual body tuning Inspector at 100/150/200%; image review
is required separately from widget probes. The compiled C++ PhysicsLoads consumer
also runs nondefault tuning and fresh recovery against the matching installed SDK.
The final ZIP acceptance exports Gallery23 and verifies its source-free startup.
These gates do not claim physical GPU acceptance or a new ZIP before CI succeeds.

## Copied rigid-body observation gate

The physics policy suite checks independent impulse/force/inertia/COM equations,
point velocity, true sleeping without wake or checkpoint mutation, pending command
timing, unavailable/retired identities, native replacement and fresh recovery.
The existing joint suite also checks signed cooked collision offsets against
actual native COM and the angular cross-product equation.

`native_sdk_physics` executes the matching compiled SDK's body/point callbacks,
including fixed/owner/output-size guards, failure clearing and input/output aliasing.
`native_sdk_physics_observation_denied` executes a separate compiled consumer with
valid realized targets during PostPhysics but no Physics capability. Its checks
must run; a diagnostic marker proves execution to the process test without
changing native body configuration after integration.
The installed PhysicsLoads module reports six actual first-step speeds; the
consumer checks those values independently under defaults and nondefault tuning,
alongside existing numerical motion and fresh-worker recovery.

The PhysicsVelocity exact-SDK probe checks copied velocity input, finite/channel/
identity/owner/fixed guards, ordering with impulses, and pending tags6/7 restored
in a new worker. The no-Physics fixture also calls both setters from an otherwise
valid fixed tick and must reject them. Private recovery is now version4.

The combined package compiles the original `PhysicsVelocity/Native` project with
the same installed SDK, prepares schema/source identity through the existing
example helper, and checks timed zero stops, preserved channels, caps/damping and
fresh-process recovery. Assembly verifies the module kit and ships the ordinary
editable project in `Examples/PhysicsVelocity`. Relocation uses the shipped exact
schema worker for export, deletes the copied project and test runtime kit, then
runs production startup and the separately inventoried graphical observer. The
observer measures four actual solver channels and independent stop positions and
captures `physics-velocity-running.ppm`. These are CI gates, not physical GPU
acceptance; inspect actual evidence before promoting a new delivery.

## Copied contact observations and original C++ consumer

The physics policy suite covers controlled Dynamic-sensor Begin/End without solid
response, canonical copied pairs/ticks, stable nonconsuming readers, bounds and
owner rejection, sleeping-solid cache End, failed-preflight retention, retired
and same-UUID generations, and native recovery without transient Begin replay.
`native_sdk_physics_contacts` compiles a real consumer and checks current-tick
filtering, repeated copies, UUID pairs, exact output/null/size/bounds, outside-fixed
and foreign-thread rejection, plus actual native motion/cache recovery in a new
worker. The no-Physics consumer must execute and reject these reads as well.

The combined package compiles `PhysicsContacts/Native` against the installed SDK,
prepares existing schema/source identity, and checks both default reversal and
tuned pass-through using independent trajectories. New workers restore before
contact and with the PostPhysics reverse command still pending, without repeated
launch, counters or Begin. Assembly verifies its module kit in
`Examples/PhysicsContacts`. The final ZIP gate exports with the matching shared
schema worker, deletes copied source/kit, checks production startup, then verifies
the actual running native reversal/counters and captures
`physics-contacts-running.ppm`. The readback checks both red actor and cyan marker.

Physics loads/velocity captures check cyan actor pixels across the actual image.
The original center-cube contrast assertion is scoped to its own scene; spaced
physics actors can leave the center empty. These retain numerical execution,
source-free export and production-startup checks. Saved captures require review.
A configured gate does not itself claim Windows execution, physical GPU acceptance
or a delivered ZIP.

### Copied C++ physics volumes

The exact SDK volume probe checks known sphere/box geometry, canonical sorted
copies, repeated reads, sensor/mask policy, caller capacity without partial
success, exact sizes, null/owner/fixed guards and fresh native worker recovery.
The capability-denial fixture exercises the same valid input without Physics.
Core policy covers all primitive shapes, rotation, static/sleeping/no-wake,
retired generations, actual character inner bodies and native body-budget failure.
Cooked compound publication tests deduplicate two hit children into one identity.

The combined package builds `PhysicsVolumes/Native` against its installed SDK,
prepares ordinary authored component metadata and verifies default/tuned movement,
counts and change-only diagnostics with fresh worker recovery. Assembly validates
its matching module kit and includes editable `Examples/PhysicsVolumes`.
Relocation exports through the matching SDK schema worker, deletes authoring input,
verifies production startup and runs a separately inventoried graphical observer.
The observer checks current count/native velocity/independent pose and saves actual
D3D12 pixels, requiring visible cyan geometry and a red actor. Physical Windows
GPU acceptance remains separate from CI and is not implied by these fixtures.

### Slider joints

Core joint tests check kind4 lower/upper endpoints, all principal axes, rotated and
structural-parent frames, signed/scaled anchors, mirrored cooked COM under steady
load, invalid-candidate atomic rejection and fresh recovery mixed with all previous
constraint kinds. The expanded ASan/UBSan joint suite has a120second test allowance;
its measured local execution was70.74seconds. Actual Inspector/guide fixtures check
kind selection, field visibility, numeric edits, Undo/save/reopen and clipped interval
rendering. Gallery24 runs through native motion, runtime-content export and relocation.

The native editor capture appends slider Inspector and authored guide evidence at
100/150/200percent scales. The combined package exports a separate editable Gallery
copy with Gallery24 as startup, inventories its graphical observer, deletes that
source and runtime kit, verifies production startup, and checks both independently
known carriage endpoints and locked orientations before saving the D3D12 image.
Visible cyan geometry is required across the actual image. These configured gates
are not claims of Windows execution or physical GPU acceptance.

### Bounded joint velocity motors

`physics_joints` checks independent slider F/m and hinge torque/inertia equations,
signed reversal, braking, coasting, sleeping wake, native constraint retention,
invalid-candidate rejection and fresh recovery. `native_sdk_physics_motor` compiles
typed C++ joint controls, observes actual native velocity after Physics, and compares
continued motion with a new worker restored from the synchronized checkpoint.
Inspector/guide fixtures cover enable, signed speed, bounded effort, Undo/save/reopen
and drive status. Gallery25 checks native endpoints and independently transformed
frame points before source-free export and relocation.

The native editor capture adds hinge/slider drive controls and guides at
100/150/200percent scales. Final package acceptance exports Gallery25, removes its
editable source and kit, verifies production startup, and runs the separately
inventoried `--packaged-motors` graphical observer. It requires both constrained
poses, two native joints and actual cyan rendered geometry. These are configured
gates; Windows execution and physical GPU acceptance require separate evidence.

### Distance-joint springs

The native spring gate independently measures implicit Euler trajectories for four
damping ratios and two masses, unequal-mass momentum/reaction, free intervals and
unilateral soft bounds, fresh recovery, native order retention, sleep/wake,
inactive-kind settings, hard compatibility and invalid atomicity. The public C++
probe changes frequency/damping during fixed-step gameplay; its tests compare
independent position/velocity and genuinely fresh workers before/after the change.

Actual Inspector and derived guide tests run at 100%, 150% and 200% scale. Original
Gallery 29 verifies low/critical damping numerically, render resources and
source-free relocated content. Its two numerical-demo bodies disable sleeping;
the ordinary sleeping default and inactive-control wake checks remain covered.
Native captures 211–216 configure Distance spring Inspector/guide views; the full
suite ends at stage 217 within the existing 600-second total and 45-second stage
budgets. Final combined package configures production startup and a
`--packaged-springs` observer requiring native joints, distinct damping responses
and cyan geometry after source/kit removal. Actual Windows execution/image review
and physical acceptance require separate evidence.

### Swing-and-twist joints

Joint tests independently measure native angular integration/unequal cone bounds,
asymmetric twist, locked/free cases, equal-inertia momentum reaction, six-kind
manager order/fresh recovery, atomic invalid candidates and signed-scale offset-COM
anchors. The expanded native joint gate allows 240 seconds for sanitizer execution;
its numerical assertions are unchanged. They exercise the enlarged typed component through physically ordered
Flecs descriptor batches with post-registration layout verification. The actual
Inspector enum/conditional fields/Undo/save/reopen and selected cone/twist guide
are checked at 100%, 150% and 200% scale. The compiled probe writes typed limits before
Physics, reads copied angular velocity and restores genuine fresh workers.

Gallery 28 tests gravity-driven wide/narrow motion, independent bounds/pivots,
render resources and source-free relocation. Native captures 205–210 and final
package `--packaged-swing` observer/production startup are configured; the graphical
observer requires actual native joints, distinct motion ranges and cyan geometry.
Windows execution, image review and physical acceptance require separate evidence.

The expanded editor capture fixture has a 600-second total deadline and a separate
45-second stage deadline; CTest allows 620 seconds including teardown. Windows build 167
completed stage 178 and stopped 0.6 seconds into stage 179 at the old 240-second total budget,
rather than exhausting that stage's deadline. The wider total permits the larger
suite; it does not weaken layout/visibility assertions or hide a per-stage stall.

### Connected-body collision policy

Joint tests cover full32-bit mutual masks, cached-contact on/off transitions,
unchanged-policy sleep and changed-policy wake, reversed/duplicate opt-outs,
retirement/body replacement, third-body floor response, CCD, query visibility,
atomic admission and fresh native recovery. The compiled SDK probe cycles the
typed policy and reports actual copied contacts/body positions; three genuinely
fresh workers restore its authored counter and solver state across transitions.
Actual Inspector checkbox/Undo/save/reopen and derived Scene guide status are
checked independently. Gallery27 passes native pair/floor motion, render resources
and source-free relocation; final Windows package runs production startup plus
the inventoried `--packaged-connected` graphical observer after deleting source/kit.

`forge_cpp_example_content_tests` accepts a prepared C++ project, matching inspected
schema JSON and scratch root. It uses the real content preparation/package/load
path, checks source-free relocation and preserved custom values/identity, and
injects unknown Tint alpha to preserve fail-closed export coverage. Run it against
fresh matching installed examples; it is content proof, not native graphical or
standalone executable proof. Windows166 exposed this previously missing consumer
coverage for PhysicsVolumes. No unsupported alpha feature is introduced.

### Bounded joint position drives

The joint suite checks independent implicit-Euler spring position/velocity for
critical and underdamped Hinge/Slider cases, finite effort and Dynamic reaction,
active-target/limit rejection, mode changes, nonlast native constraint retention,
sleeping wake, legacy velocity defaults and true fresh recovery. The compiled
`native_sdk_physics_position` fixture compares actual copied native velocity and
position with that independent recurrence and restores a new worker at tick20.
Inspector fixtures navigate the actual mode enum and edit targets/frequency/damping
with Undo/save/reopen at65/100/200percent scale. Keyboard input waits for confirmed
selected-row navigation focus after a resized/scrolled dropdown's layout completes.

Gallery26 checks signed targets, independently transformed hinge axes, settling,
render resources and source-free relocated export. Native editor capture adds
hinge/slider position controls and target guides at 100%, 150% and 200% scale. Final-package
acceptance deletes the copied source/kit before production startup and the
inventoried `--packaged-position` graphical observer, checking actual target poses,
two native joints and cyan geometry. Configured Windows gates and physical GPU
acceptance still require their own execution evidence.

## Timed animation exit gate

`animation_states` validates source/cooked compatibility, stable exit targets,
independent fixed-tick boundaries, Ozz poses/markers, interruption, failure retention
and fresh recovery including partial prefab overrides. `native_sdk_animation_exits`
executes a compiled public C++ observer, manual return and three fresh-worker
recoveries. `animation_states_gallery` evaluates Gallery30's real skins, exports
it and repeats the sequence after source-free relocation. The actual ImGui fixture
exercises exit fields and Undo at 65/100/150/200 percent scales.

`editor_animation_exits` separately captures the source document at 100/150/200
percent. These images are layout evidence, not physical desktop acceptance.
Final combined-package acceptance uses the shared SDK runtime kit for its matching
SDK-enabled observer (which imports flecs.dll). The ordinary static production
runtime is tested separately. Gallery30's exported observer requires all three
characters to visit all three states before successful capture/exit.

### Spatial-audio falloff gate

`audio` measures actual offline PCM against independent equations for three curves
and strength/range boundaries, including zero-strength panning, invalid native
configuration recovery, playback continuity and partial prefab intent.
`native_sdk_audio` compiles the matching public component fields and checks worker
recovery. `audio_falloff_gallery` exports Gallery31, removes the original project,
relocates cooked content and measures all three curves without the source WAV.
`audio_editor` exercises actual Inspector choices/strength, Undo/Redo and save/reopen
at65/100/150/200%. `editor_audio_falloff` captures the production Inspector at
100/150/200%; final combined-package acceptance also launches the cooked scene
with the production executable. Images/physical sound require separate review.

`audio` additionally compares48directional cosine/PCM cases, transformed parent/
World/Explicit direction, coincident references, invalid values, degenerate
forward recovery and cursor continuity. `audio_falloff_gallery` closes over both
31/32, measures source-free parent-directed front/back PCM after relocation.
`audio_editor` includes real directional controls/history/reopen and finite
227-segment guide checks. `editor_audio_cones` captures actual production controls
and Scene guides at 100/150/200%; final package starts exported Gallery32.

## Common audio mixer

The audio-mixer capture enables the existing `FORGE_SDK_DIAGNOSTIC_DIR` opt-in in
its retained evidence directory. Save-image captures use the same opt-in to retain
the first Play replacement's construction, Scene, Map, Physics, audio and publication
phases, followed by response queueing. The ordinary runtime records a separate
bounded bootstrap trace with UTC and monotonic elapsed milliseconds, covering
argument/project/module setup, world construction, pipe setup, session creation
and the first response queueing. The ordinary trace stops after its first replacement;
later commands cannot grow it. SDK-host bootstrap records stop at host creation;
its existing process diagnostics cover subsequent requests. This diagnoses early
hello delays without changing the five-second request deadline. A queued response
marker does not prove receipt. Default launches do not create these files.

`audio` measures independent per-channel/Master power, summed actual voices,
invalid atomic mix admission, missing-listener safety, mute/unmute sample time
and exact PCM continuity during route changes. Existing prefab masks retain an
equal Master override while siblings inherit a new channel. Game foundation/host
and SdkPlay tests cover old settings normalization, category preferences, failed
settings-write rollback and a new scene inheriting the current mix.
`audio_falloff_gallery` also exports Gallery33, removes the source project and
measures actual cooked PCM through its three routes after relocation.
`audio_editor` drives routing and four settings sliders at65/100/150/200%, including
independent settings publication and scene Undo/save/reopen.
`editor_audio_mixer` captures routing and project volumes at 100/150/200%; final
combined-package acceptance starts source-free Gallery33 in the production host.
Native capture configuration is distinct from actual image/physical acceptance.

### Animation Game capture readiness

The weighted-mask and blend-space Game images in `editor_animation_root_motion`
require all three runtime model animations to be ready, active playback after a
fixed tick, completed presentation resources and no current presentation errors.
The saved chrome metadata records these conditions. A responsive Play worker
alone cannot admit an image of the floor while its animated models are absent.
The43-image process has230seconds inside a240second CTest limit. The first Play
stage has75seconds for process startup and playback status; other stages have45.
Capture outputs are cleared before execution and every frame records its build
and source identity, which the test checks against the CI environment.
While Play is active, this capture fixture pauses50ms after each rendered frame
to leave CPU time for its isolated runtime on software-rendered CI hosts. The
pause is recorded in capture metadata; shipping frame pacing and the5second
transport deadline are unchanged. All readiness and settling checks still apply.
Root capture probes are refreshed each
frame, and live rig captures clear the authored component's automatic scroll
focus so the actual live controls must remain visible across consecutive frames.
Image review and physical GPU acceptance remain separate from these checks.
Focused native audits explicitly build the animation converter as well as the
editor fixtures. A restored cache must not supply a tool that a clean audit forgot
to request; numbered builds compile the full default target set.
Their focused selection also executes the real additive-overlay and weighted-mask
pipeline checks, with exact failed pose values retained in diagnostics. The complete
Gallery ZIP-writer regression runs before focused native compilation.
Independent additive and ordinary blend paths use estimated native quaternion
normalization. These two cross-path tests allow at most four float ULPs
(representable steps) per rotation component; translation, scale, morph weights
and other pose fields remain exact. Regression controls cover an observed one-step
Windows difference and reject five-step rotations, nonfinite values and one-step
translation/scale changes. Same-path recovery comparisons remain exact.

### AudioClip preview gate

`audio_preview` reads actual offline PCM through the normal provider, proving
volume/loop/end/restart, cursor continuity, stale/corrupt selected revisions,
source-free cooked audition and authoring/index isolation. Explicit null backend
teardown is a lifecycle test; it is never an automatic device fallback.
`audio_preview_editor` uses actual Play/Stop/Loop/volume widgets at65/100/150/200%
and verifies PCM and selection/project/Play-lock teardown. Typed gain clamps and
nonfinite input cannot change the last admitted output. `editor_audio_preview`
captures the real AudioClip tab at 100/150/200% on a copied original Gallery,
without requesting a device or autoplay. Physical listening remains separate.


Portable MCP editor-owner tests can be enabled with
`-DFORGE_BUILD_MCP_EDITOR_TESTS=ON -DFORGE_BUILD_ASSET_TOOLS=ON -DFORGE_BUILD_GAME_PLATFORM=ON`.
They compile the actual editor controllers against the existing SDL/ImGui pins
and use real local sockets/workers, without a GPU or graphical editor claim.
The ordinary editor build enables these fixtures automatically. `mcp_document_stdio`
and `mcp_asset_stdio` belong to the existing shared editor acceptance selection;
the portable flag defaults off for unrelated core/SDK consumers.


The expanded MCP owner gate includes `mcp_code_source`, `mcp_code_stdio`,
`mcp_code_inspection_stdio`, `mcp_workflow_stdio` and `mcp_image_stdio`.
Native SDK configurations use the CTest `mcp_sdk` fixture to install the real
selected NativeSdk component into `mcp-native-sdk`; no synthetic compiler/SDK kit
is accepted. Build `forge_runtime`, `forge_sdk_probe` and
`forge_sdk_schema_migrate` before running that fixture. Managed compiler tests
select Release, matching the existing editor recipe; Debug still runs isolated
inspection/migration checks.

Enable `FORGE_BUILD_UI_PRESENTER` for the workflow host. Set
`FORGE_TEST_MCP_RUNTIME_KIT` to an authentic prepared Windows runtime-kit directory
to register `mcp_workflow_stdio`; Windows defaults to the existing build
`runtime-kit` installed by both full editor routes before CTest. Its exported
engine identity must equal that selected kit. The image fixture reuses already selected Diligent headers, creates
no device and uses recorded pixels. It never enables/fetches another renderer.
Build all selected test targets before running CTest; an old executable is not
new-source evidence. MCP capture files can stream gzip evidence to avoid large
uncompressed schema catalogs. See [MCP validation](mcp.md#validation) for scope.

Run the focused owner gate with `ctest --output-on-failure -R '^mcp_'` in the
configured build. Existing authoring/live, UI presenter and PNG checks remain
relevant when their shared owners change. Portable success does not replace
native Windows editor/client execution, actual GPU capture or a complete compiled
C++ project-to-Windows-export execution gate. No new numbered package is implied.


### Packaged native MCP game acceptance

For focused Windows fault diagnosis, dispatch `editor-audit.yml` with an immutable
`source_commit`, `workflow_only:true` and `mcp_only:true`. This reuses the same
toolchain/configuration cache, rebuilds the actual project/workflow/image owners,
installs their real game runtime kit, and runs those three MCP tests. Native host
stderr and shutdown reasons remain visible on failure; private stdout connection
handoffs are never logged. This unnumbered partial audit cannot qualify a delivery.
Fixture control files tolerate Windows sharing/lock races until their next poll;
only a completed removal consumes a requested action. A locked-handle probe checks
that behavior on Windows. These are disposable test signals, separate from the
production file/lease validation rules.

The editor and MCP editor-owner fixtures use MSVC `/bigobj`: the combined source
editor adapters exceed the ordinary COFF object-file section limit. This option
is scoped to those targets; it does not change the game/SDK linkage profile.

The final ZIP gate also runs `tests/mcp_editor_package_test.py` with the separate
editor fixture EXE beside the extracted shipped editor. `--mcp-authoring` is
compiled only into that test fixture, never the production editor. Its explicit
project/code/execution/output grants borrow the ordinary editor frame pump and
owners. A private connection file outside project/preferences/evidence carries
the temporary token; the supervisor deletes it and checks retained text/protocol
captures for credential leaks.

The client uses the shipped `forge_tools --mcp-stdio`, SDK and runtime kits. It
creates a new project and material, creates/builds/inspects a C++ component/system,
attaches authored values, runs real SDK Play, steps and tunes the C++ component,
captures the actual Game render/readback as PNG plus MCP image content, keeps
values with Undo/Redo, and exports/runs the shipped graphical executable.
The exported run explicitly selects the existing finite startup-verification
mode through MCP: D3D12 WARP/offline audio, measured frame statistics and exit code 0.
It requires both verification messages and changing C++ rotation samples; it does
not treat a process launch or an audio/GPU initialization failure as acceptance.
Bounded stderr markers in the MCP-authored C++ prove that the saved speed and
changing rotation execute in the exported game. They do not use the Play stdout
protocol pipe, a substitute runtime, or a test-only game executable.

The package upload is gated on this result, with `acceptance.json`, compiler and
runtime logs, captured PNGs, authored source copies and the credential-free MCP
capture retained under `FORGE-Editor-SDK-Acceptance/mcp`. Native rendering and
execution still require image review; they do not establish physical GPU/audio
or a third-party AI application's UI acceptance. The fixture has a15-minute whole
bound; individual tools/jobs retain existing deadlines. The package job includes
that added bounded headroom without weakening other acceptance checks.
