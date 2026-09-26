# Build checks and Windows packages

Pushes and pull requests run Linux/Windows core tests, cache-invalidation tests, and formatting. They do not compile the D3D12 editor or publish a ZIP. Superseded push/PR runs on the same ref are cancelled; manual builds are not cancelled by a code push.

Request a Windows package using **Build and test → Run workflow**, with **windows_package** enabled. From the GitHub CLI:

```sh
gh workflow run build.yml --ref forge/windows-build -f windows_package=true -f build_id=YYMMDD-NNNNNN
```

Reserve the build identifier with the release coordinator before dispatch; the example above is a placeholder. The coordinator maintains one persistent, monotonically increasing counter and never reuses failed reservations. Use your intended branch in place of `forge/windows-build`. GitHub's Run workflow UI requires the workflow on the default branch; branch-specific dispatch can also be requested through the API/CLI. The package artifact remains `FORGE-Windows-x64`. No new package is needed merely to validate documentation or portable code edits.

## Cache behavior

Requested Windows builds restore the dependency checkout/build directory and CMake/Ninja build directory under the runner's short `AgentFiles` paths. Configure, build, tests, and packaging always execute even on a cache hit. Product source is freshly checked out. Verified source-content records preserve timestamps only for unchanged inputs, allowing Ninja to reuse compatible outputs; changed inputs rebuild.

The compatibility key includes the runner image, architecture, MSVC version, Windows SDK, CMake/Ninja versions, absolute checkout path, and CMake configuration/dependency pins. The source commit is appended to the cache entry key; a compatible prior commit can supply a restore fallback. There is no fallback across different compatibility keys. Successful native compilation can save an editor cache even if a later test fails; the shared-game job saves its cache after its successful validation. Cache reuse never bypasses required tests or delivery gates. Cache upload failures do not prevent artifact delivery; a failed restore is discarded before a fresh configure.

Enable **clean_build** to bypass both cache restore and cache save for a clean verification. Cache misses after runner/toolchain updates or cache eviction are expected. The first build has to populate the cache.

This uses the official [cache restore/save actions](https://github.com/actions/cache), pinned to v6.1.0 commit `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`. Cache matching and scope follow [GitHub's dependency-cache rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).

The repository name ends in a period, so Windows cache actions use a scoped Node preload to set their working directory to the short checkout. The preload affects only those cache processes. **Cache transport check** verifies a real save/delete/restore round trip whenever that shim or its diagnostic workflow changes; it can also be dispatched manually.

## Measured results

On September 15, 2026, the [cold Windows editor job](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34989524125) took **8m50s**. The [next-commit cached job](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34990556960) took **3m00s**, approximately 66% less time, including setup, cache transfer, tests, and packaging. Ninja executed 14 build steps instead of 579; configuration fell from 3m24s to 27s and compilation from 4m10s to 35s. Both editor test suites passed. This measures dependency reuse across a documentation-only commit with freshly checked-out product sources; timings vary with changes and runner conditions.

A [routine push](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34987648929) completed its core/format checks in **1m52s**, with editor packaging skipped. These are separate workflows in practice: fast validation on pushes and an explicit package build when a Windows ZIP is needed.


## Build identity and manual

Product identity is `yymmdd-counter`: UTC date plus a counter padded to at least six digits. It has no release-channel suffix, and the counter never resets with the date. A packaging attempt consumes a reserved identifier; re-downloads retain it. The existing sequence continues after build 000005. Scene formats, module ABI, and IPC retain independent compatibility versions.

The `build_id` workflow input is required for Windows packaging. CMake embeds it and the source commit in both binaries and writes `build.json`. CI checks both `--version` outputs before packaging. Unassigned local builds cannot be packaged by the release script. The artifact contains `yymmdd-counter-FORGE-Windows-x64.zip`; manifest hashes cover the build metadata and manual. Publication verifies the reserved source and exact artifact bytes before accepting a numbered delivery.

End-user source pages live in `manual/`, separate from these technical documents. `python tests/manual_test.py` checks supported formatting, navigation, escaping, and identity validation. Packaging renders the current pages with `tools/build_manual.py` into a standalone offline HTML manual and includes the Markdown sources. The renderer intentionally supports headings, paragraphs, flat lists, fenced code, bold/inline code, and local page links; unsupported block forms fail validation. No web service or extra documentation dependency is required.

For an unpackaged local editor build, generate the manual beside the executable with `python tools/build_manual.py --output /path/to/build/manual --build-id unassigned`. Help opens `manual/index.html` through the OS handler. A browser-launch success only confirms dispatch to that handler; desktop opening remains an interactive check.

## Combined editor and exact SDK delivery

The final Windows artifact is assembled only after core/static, shared SDK, editor
and formatting jobs succeed. Editor and SDK builds run independently; a final package
job verifies both manifests and their identical source commit/build ID, adds the SDK
under `NativeSdk/`, regenerates the outer file hashes and runs its shared runtime from
a relocated path with a restricted PATH. The intermediate `FORGE-Windows-Editor-Base`
is not the final delivery. `FORGE-Windows-x64` remains the single complete numbered ZIP.
The download action is pinned to official v4 commit
`d3f86a106a0bac45b974a628896c90dbdf5c8093`; its name/path inputs select one validated source run's
artifacts. Modified files, path escapes and mismatched builds fail assembly before
replacing a usable output. Installed SDK consumer tests still verify shared linkage,
compiler compatibility and real gameplay modules before assembly.

After relocation, the package job also runs the editor SDK acceptance gate. The
`forge_editor_fixture` executable is shipped separately via the
`FORGE-Editor-SDK-Fixture` artifact (the executable only — its matching DLLs
and resources are reused from the shipped package so a missing packaged
dependency fails verification rather than being masked by a parallel bundle).
The fixture is copied beside the extracted `forge_editor.exe` in a private
scratch, launched against the matching extracted `NativeSdk/` and a dedicated
ordinary editable project (shipped as `FORGE-Editor-SDK-Project`), and the
resulting `workflow.json`, per-stage trace JSONs, and rendered PPMs are
asserted. Failure here gates the `FORGE-Windows-x64` upload.

The acceptance fixture, the dedicated editable project, and the isolated
private user-data directory never enter the shipped ZIP. Physical-GPU
acceptance on real hardware is a separate concern; the gate runs against the
Windows runner's WARP rasterizer and reports the hardware scope explicitly.
The fixture routes the runtime's audio through `AudioOutput::Offline`
(skipping `ma_context_init`; no device, no physical-audio proof) so the
workflow exercises authored clip decoding and scene preparation on runners
without a WASAPI endpoint. The fixture's offline-audio routing is not a
substitute for physical-audio acceptance, which remains a separate concern.

A test-only packaging failure can use `package_source_run` to reuse that run's
unchanged compiled artifacts. All four static/shared core jobs, editor and format
must already have succeeded. The package job checks that run's source SHA against
the package manifest and still executes relocation before upload. Build/source
identity stays embedded in the binaries; the packaging workflow commit is recorded
separately. This option does not rebuild or relabel a previous delivery.

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
full commit SHA and `windows_package=false`. For changes confined to this input
fixture, also set `audit_workflow_only=true` to build its required editor/runtime
targets and execute only `editor_input_workflow`. The default remains the full
source audit. The artifact records the selected scope; focused success is not a
full-suite result. Use the full audit when shared behavior or rendering changes
require its additional coverage. Locally on a configured Windows
build, use `ctest --test-dir <build> -R '^editor_input_workflow$' --output-on-failure`.
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
This reuses the editor audit's cache and capture machinery, compiling the material/
shader test targets and running patch, material, shader, viewport, morph, skin,
frame and optics checks. It uploads `FORGE-Editor-Source-Audit` with test logs and
captures. It does not reserve a build number or produce a delivery ZIP.

The separate HLSL profile workflow verifies debug/optimized FXC with warnings as
errors against the same staged dependency patch. The GPU differential reference
intentionally compiles the original upstream function with its original warning;
review production warnings separately. A focused audit does not replace the full
core/shared-SDK/editor/relocation/package gate for numbered delivery.
