# Build checks and Windows packages

Pushes and pull requests run Linux/Windows core tests, cache-invalidation tests, and formatting. They do not compile the D3D12 editor or publish a ZIP. Superseded push/PR runs on the same ref are cancelled; manual builds are not cancelled by a code push.

The fixed Ninja build profiles continue independent compilation after an error,
collecting remaining compiler diagnostics in the same job. A failed build still
fails the gate; its test steps do not run against older executables.

The editor input, Sprite/sequence frame, root-motion, mixer, audio preview,
sequence cue and Save image fixtures keep the full editor window and controls at
their requested scale while limiting Scene/Game raster images to 512 pixels along
the longer axis. The limit preserves aspect ratio and is recorded as
`maximum_view_extent` in capture metadata. These fixtures test controls, routing
and actual image readback on the hosted software adapter; they do not establish
full-resolution or physical GPU performance. Dedicated renderer checks retain
their own render dimensions. The shipping editor's 4096-pixel viewport limit,
explicit output sizes and five-second GPU backlog watchdog remain unchanged.
The Save image fixture also leaves 500 milliseconds between submissions on the
hosted software adapter. Its metadata records `fixture_capture_frame_pause_ms`.
This functional command/readback test retains all three UI scales, the 512-pixel
raster bound, actual encoded-image comparisons and authored-source preservation.
The input walkthrough leaves 100 milliseconds between ordinary submissions and
1000 milliseconds while awaiting its isolated graph publisher. Its recorded state
includes `fixture_workflow_frame_pause_ms`, `fixture_shader_wait_pause_ms` and the
number of graph-wait frames. All 968 actions and input/readiness assertions remain
required. Input retains its 12-second deadline; capture preparation uses the
ordinary staged-capture 45-second budget because a cold native frame can outlast
an input gesture. Compiler, worker and GPU guards retain their individual limits;
the complete walkthrough has an 1800-second aggregate allowance. It resolves a
recovery prompt after its intentional Reload through the real **Keep saved scene** button when present, verifies the
saved scene afterward, and records whether a choice was needed. This includes
all prior actions plus that explicit recovery decision. The sequence-frame
fixture also paces submissions 100 milliseconds apart and records `frame_pause_ms`;
all three scales, fixed-output/modal checks and original-source assertions remain.

For a focused Inspector correction, `editor-audit.yml` accepts `inspector_only`
with both `workflow_only` and `capture_only`. It builds the same native fixture
owners and runs `root_motion_inspector`, `editor_play_input`,
`editor_animation_root_motion`, `editor_view_images`, `editor_input_workflow`,
`asset_source_document` and `material_graph_layout_editor`.
Its optional `inspector_group` selects `captures` for the CPU Inspector, native
root-motion and Save image checks, or `input` for the CPU input and native input
walkthrough plus exact source-baseline and repeated-publication regressions.
The default `all` retains all seven cases. The recorded audit scope
identifies the exact diagnostic selection. It does not
replace the complete editor or Windows package gate; unchanged renderer evidence
can retain its original source and environment attribution.
Selections containing the input walkthrough install the matching standalone
runtime kit before testing so its real export steps use the built host. The
`captures` Inspector group omits this unused prerequisite.

Root-motion captures reveal the complete required control range before accepting
stable layout. Their timeout report retains geometry and readiness from the last
completed UI draw, before the next frame clears observations. The CPU regression
uses the packaged editor font and exercises recovery from a partially clipped
control range. Game captures also require an observed simulation tick.

The `shader_mesh_graph_worker` check compiles the default Mesh graph through the
normal isolated worker without an editor render loop, records elapsed wall time,
and verifies the complete native output family and unchanged authored source.
It uses the same worker resource limits as editor publication. This separates
compiler acceptance from rendering contention; it is not a physical GPU benchmark.

Fixed gameplay input recording uses `input_recording` for byte/tick/kind bounds,
empty Finish, replay/cancel/exhaustion progress and rebinding guards.
`input_recording_process` covers correlated normal-host admission and changed
source rejection. `input_recording_gallery` and `input_recording_gallery_sdk`
exercise the shipped Scene and original native message receiver.
`input_recording_sdk_process` records running input and replays through a fresh
relocated SDK project, requiring both runtime children to quit with exit0.
`input_recording_editor` clicks real record/save/replay controls in both hosts at
three widths and checks cached-document project affinity.
`mcp_input_recording_stdio` exercises both protocol envelopes, grants, stale
scope, duplicate JSON, replacement and redirected-path rejection. Saved takes
also pass through `sdk_play_runtime`, `game_recording_editor`, `mcp_image_stdio`
and the production recording/capture owners. Software graphics/control fixtures
remain separate from complete Windows Main and physical acceptance.

Related Prefab source contributions use `prefab_source_layers` for explicit
source8 admission, monotonic ordering, shared-ancestor sparse values, origin
collisions,64 contributors, admitted/opaque custom data, scoped references,
duplication and dependency impact. `authored_migration` covers source8 ordered
inputs and retained local values. Native `prefab_source_layers_editor` covers
actual add/order/remove/lock/publication controls at100/150/200% in wide/narrow
layouts; `mcp_prefab_source_layers_stdio` uses both envelopes and the shared draft,
review/publication/history owners. `runtime_prefab_source_layers` exercises real
physics/render consumers, private recovery and source-hidden relocated data;
`native_sdk_prefab_source_layers` and `native_sdk_prefab_variants_installed` cover
direct/SDK Play and an installed-only relocated SDK. Gallery208 uses the existing
composition fixtures with `--source-layers` for independent silhouette coverage
and exact authored/exported production Game image comparisons. Source8 preserves
source1–7/Scene1–6/native layouts; unrelated-root composition is unsupported.
These checks establish neither Windows execution nor physical-device acceptance.

Request a Windows package using **Build and test → Run workflow**, with **windows_package** enabled. From the GitHub CLI:

```sh
gh workflow run build.yml --ref forge/windows-build -f windows_package=true -f build_id=YYMMDD-NNNNNN
```

Reserve the build identifier with the release coordinator before dispatch; the example above is a placeholder. The coordinator maintains one persistent, monotonically increasing counter and never reuses failed reservations. Use your intended branch in place of `forge/windows-build`. GitHub's Run workflow UI requires the workflow on the default branch; branch-specific dispatch can also be requested through the API/CLI. The complete user-facing artifact is FORGE-Windows-x64; it includes the editor, exact Native SDK, shared runtime kit and reference game. No new package is needed merely to validate documentation or portable code edits.

## UI Material effect validation

`ui_effect_profile` checks native typed Material references and dimensions.
`ui_gradient_presenter` injects actual main/camera shader-callback faults, checks
recovery without document reload and preserves failed source diagnostics while
the prior native contexts render. The Windows Texture failure fixture uses an
unsigned generation and requires the real Texture-request failure prefix before
checking retained pixels and recovery.
`material_pipeline` covers UI-domain publication, inheritance, retained Shader
snapshots, source-hidden Material packages and corrupt/cancelled inputs.
`runtime_ui_package` runs the disposable non-drawing inspector with UI Materials,
missing references and wrong-domain refusal. `material_editor` checks native UI
parameter controls, source Undo and routing away from Mesh preview. The focused
`mcp_ui_effect_sources_stdio` exercises both protocol formats, descriptor identity,
source text, domain/stage validation, atomic refusal and write-grant discovery.

`ui_source_material_effects` drives the shared decorator Material picker at
1180/700/400 pixels: publication/domain refusal, staged assignment, actual Content
UUID drop, exact no-op, source Undo/Redo, Clear/Remove and Save isolation. The
existing image-property case also exercises the shared drop fixture. The native
Material control case is selected explicitly by full Windows editor acceptance
and has a 120-second limit.
`mcp_ui_materials_stdio` exercises both protocols, read-only discovery, source
guards, source-hidden/retained Shader selection, refusal without mutations and
single/group/rule writers with replay, Undo and CRLF-preserving Save.
The same native controls and guarded MCP queries/writers also exercise a textured
UI Material after the logical Shader changes to another domain; procedural
compatibility remains checked. CPU publication markers never enter graphics.
`material_asset` validates definition4/5 boundaries, ordinary2D admission, copied
Texture/sampler/UV reflection and old17/new18 cooked round trips. `material_pipeline`
checks retained textured UI Shader snapshots and exact source-free Texture closure.
Runtime compatibility admits prior effects1 records only through the matching
effects2 kit and rejects older/incompatible kit values.

Capture provenance is recorded in [the screenshot record](images/readme/README.md).

Windows `shader_worker` includes a real single-entry UI effect, copied reflection,
cache reuse and source-free Shader relocation. `ui_effect_render` compiles through
that same worker and imports its Material before checking display/HDR pixels,
UV/size/presentation time, clipping, opacity, parameter replacement,
failed-publication retention, isolated candidate readiness and relocated
source-free rendering. Its additional textured path uses original2x2 pixels through
the same compiler/Material/pool/render owners, checks signed UV transforms, samplers,
independent Texture publication with unchanged Material bytes, old-bundle sampling
while pending/failed, recovery and relocated source-hidden image content. The native
fixture's portable template is syntax-checked locally with the real graphics flags;
that does not establish Windows compilation or execution. Both full editor and shader-only acceptance
select this case; the audit artifact retains `ui-effect-render` outputs. Local
CPU markers and built-in software-Vulkan pixels do not establish native project
shader execution. Windows/physical acceptance requires its own actual evidence.

`ui_material_gallery_source` admits Scene213/214's real HLSL/graph Shader and
Material declarations, shared imported Badge Texture, Camera/HUD and explicit
Runtime Dependencies. `ui_material_gallery_export` and
`ui_material_graph_gallery_export` each copy the Gallery privately, cook their
Shader with the matching Windows worker, publish both Materials, inspect native
UI and package exactly six runtime assets. Each moves its source project offline, relocates
the package and reinspects the cooked UI without Shader/Material sources. Both
full editor and shader-only acceptance select these checks; source-checkout
inspection is distinct from native cooking and physical GPU review. Windows
packaging uses the same preparer on its existing private Gallery staging copy.
The shared Gallery publication helper also serves the existing lightmap sample.
No new authoring operation is introduced; existing Shader, Material, UI source and
dependency automation/MCP operations serve these sources unchanged.

The Gallery preparer compiles/publishes its UI Materials, obtains a real native
document/stylesheet snapshot and records the explicit dependency review through
declare_runtime_dependencies before preparing the runtime catalog. It preserves
the stale-review guard: linked stylesheets participate in the reviewed source set,
and no raw metadata override or dependency validation bypass is used.

UI graph compiler tests admit numeric/textured UI outputs and context fields,
located domain errors, functions (including uncalled bodies), duplication and
source round trips. `material_graph_geometry_editor` captures the real UI Output
canvas at1180/700. `mcp_ui_graph_stdio` exercises discovery, creation, source
history, stale revisions and wrong-owner refusal through both protocol formats;
full source acceptance retains that workflow. The existing protocol_cases owner
selects explicitly requested framings without duplicating a default both-format run.
Native ui_effect_render additionally lowers a connected UI graph through the real
worker and applies its existing pixel/Texture/recovery/source-free relocation
oracles. Its graph capture path is separate from the unchanged HLSL outputs.

Graph lifecycle sanitizer runs instrument the parent and use an explicit normal
fault-worker executable. That worker intentionally blocks for the cancellation
oracle and remains under the production memory/CPU limits. Its forced cancellation
is not child sanitizer/leak acceptance; instrumented parents must still exit
cleanly with full leak/error checks. A self-executing instrumented worker would
conflict with the bounded virtual-address profile. Ordinary fixture invocations
keep their existing self-worker default and project/cancellation assertions.

## Test profiles

Preview navigation extends the native canvas case at1180/700pixels with1:1/2x
viewing, typed lower/upper zoom bounds, horizontal/vertical native wheel travel,
unchanged source/selection/history/preview guards, zoomed drag/pick/Undo, gesture
scroll lock, full-native select-all and complete image export while zoomed.
An SDL backend error check and displayed purple-pixel oracle catch blank composed
images independently of geometry/input assertions. Visible-quad UV composition
avoids the observed software triangle-area bound; see [dependency limits](dependency-known-issues.md).
Existing pixel-source MCP schemas remain unchanged, with focused current and
sanitized source-host protocol checks. Full Main/Windows/physical review remains
separate from softwareVulkan/SDL component evidence.


The source canvas fixture also performs actual focused Ctrl+A/Escape/Ctrl+D/Delete
at1180/700pixels. It verifies ordered selection without source history, unique
copied native IDs/geometry, atomic group deletion, one-step Undo/Redo, queued
Escape/task-lock cancellation and native select-all/Delete in a different text
panel. Existing selection, dragging, keyboard nudging, image and placement checks
run in that same fixture. Agents use existing guarded selection/duplicate/removal
operations; no keyboard-specific tool or runtime input path is added.



`mcp_ui_source_selection_stdio` checks both protocol envelopes against the actual
source selection owner: source-only selection, fully enclosed native rectangles,
replace/add ordering, replay without reapplication, zero/partial/hidden/ambiguous
boxes, the32-member bound, atomic refusal, stale source/preview, write grants and
unchanged source/history/catalog/Scene. The native softwareVulkan canvas fixture
uses real mouse gestures at1180/700pixels for reverse rectangles, additive order,
click/Ctrl-click, busy admission and Escape/application-focus/task-lock cancellation.
The existing layout, keyboard, image capture and placement cases remain in that
fixture. Selected CPU producer/host instrumentation is distinct from normal GPU
fixture execution; neither establishes Windows or physical-hardware acceptance.


Native UI source authoring uses `ui_markup` for source ranges, preserved bytes,
escaping/bindings and rejected structural candidates; `ui_source_file` checks
contained raw-file admission, conflicts and atomic storage. `ui_source_editor`
drives the production Source/Elements controls at1180/700/400pixels, including
attribute/text/child/order/removal, history, CRLF, invalid drafts, conflicts,
read-only state, close/cancel/discard, moves/deletion and long-text protection.
The C++ history fixture checks the shared widget replacement helper.
`sprite_atlas_source_editor` also verifies hook-only document entries are excluded
from typed-source discovery and dispatch. `mcp_ui_source_stdio` discovers40 actual
native-owner tools through both MCP envelopes, grants, stale guards, source-owned
raw-write rejection, history, conflicts, RCSS and dirty-source Play rejection.
It includes native draft geometry, source/preview guards, unsupported layouts and
atomic pixel/group receipt replay. Group movement, alignment and distribution use
the actual native owner, with duplicate/unsupported/stale rejection, complete Undo/
Redo, source/Scene isolation and grant discovery. The read-only grant includes
preview metadata but excludes group writes.

Static subtree coverage includes collision-checked prefix IDs, escaped internal
label targets, anonymous roots, original-byte preservation, dynamic/protected/
ambiguous rejection, bounded automatic naming and admitted source/tag overflow.
Native controls exercise prefix input, duplication, copied selection, sibling
removal and one-record Undo/Redo at all three widths. Preview group removal remains
available when a two-member distribution is disabled. Both MCP envelopes exercise
duplicate/remove_group guards, replay, atomic rejection, selection, full history
and read-only discovery omission. `ui_layout_gallery` saves a cloned static form,
clicks its label through the actual presenter to focus the copied input, then
packages, relocates and repeats with the source project hidden. Canonical Gallery
assets and IDs remain unchanged by that scratch fixture.

Hierarchy coverage additionally checks source-ordered groups, forward/backward
append/before placement, retained IDs/comments, exact no-op, cycle/protected/mixed/
nested/dynamic/ambiguous rejection and initially admitted nesting overflow. Native
Elements destination/placement/group controls run at1180/700/400, with full one-step
Undo/Redo and a new authentic component capture. Both MCP envelopes cover the same
owner, no-op source revision/history, stale source guards, atomic rejection, replay
and read-only discovery. The scratch Gallery form's copied label/input move into
a new container; native parent observations and actual label focus are checked
before and after source-hidden save/package/relocation.

Inline style tests cover full optional declaration ranges (including terminators),
quoted delimiters/functions/comments, preserved unrelated CSS/bindings, existing/
missing/empty attributes, quote selection/conflict, one-value injection rejection,
matching duplicates, exact no-op and whole-tag/numeric budgets. The same reader
serves native controls and read-only style_read. Native property controls retain
edited selection, read/set/remove and one Undo; real outline group/Shift drops,
hover isolation, Escape/stale/lock/application-focus/view cancellation and Save/
history gesture locks run at1180/700/400. Both MCP envelopes cover property read,
guarded set/remove/replay/no-op/atomic rejection and read-only discovery. Saved and
source-hidden relocated native forms verify the authored background color as well
as parents/label focus. ui_filter_profile and ui_draft_resources retain runtime
no-collector profile/admission and immutable resource-scope regression coverage.

Stylesheet source tests cover top-level rule ranges, quoted delimiters/comments,
nested/at-rule eligibility, preserved values/rule order, selector/add/remove
candidates, injection/size/duplicate rejection and optional outline caps without
changing runtime admission. Native Rules at1180/700/400 checks read/set/remove,
selector/add/remove, explicit Save, source history, locks, incomplete drafts and
oversized-value protection. Both source MCP envelopes exercise rule discovery,
read-only access, current guards, property replay/no-op, atomic invalid/unsupported
rejection and one-step history. The scratch linked stylesheet authors a selector/
color; native styling is checked after Save and source-hidden package relocation.

Reusable styling tests cover ordered mixed/absent reads,32distinct-member source
patching, literal classes/entities/equal-value preservation, bindings/IDs, partial
member/value/whole-tag rejection and class/rule limits. Native group/Classes and
flat Copy/Move controls exercise mixed feedback, explicit input, retained selection,
barriers/no-ops and one Undo at1180/700/400. Both MCP envelopes cover the six matching
methods, read-only discovery/dispatch, stale guards, replay and atomic history.
The combined source MCP case has a300-second suite budget; individual request
timeouts remain unchanged.
Scratch linked rules prove the copied class rule wins#aabbcc, then reordering
restores#6ac98a; authored classes/backgrounds and the actual native cascade survive
Save/source-hidden relocation. The shared input fixture names absent controls
and display widths while retaining its real input/clipping checks.

`ui_draft_resources` covers immutable draft admission, correction of invalid saved
bytes, scope/budgets and package rejection. `ui_source_preview` checks native
geometry/padding/borders/density, unique-ID picking, request/failure caching,
last-good state and runtime handover with both font modes. `ui_source_canvas`
drives actual move/resize, single-record history, Save, source/focus/lock/close
cancellation and native box-clamped candidate rejection at1180/700/400pixels.
It additionally drives real Ctrl multi-selection, uniform group dragging, Escape
cancellation and native group alignment/distribution controls, including padded
border/source size differences and one-record complete-group Undo/Redo.
Preview errors use the existing timestamped, deduplicated Problems owner.

`ui_layout_gallery` previews Gallery211's original UI sample, saves a changed
group layout and verifies all three sibling rectangles before and after package relocation
and source-folder removal. The optional `ui_source_preview_vulkan` and Windows
`ui_source_preview_render` profiles exercise opaque native preview pixels,
last-good candidate failure, saved stylesheet refresh, exact restore/relocation
and image lifetime through native/backend retirement. The Linux probe includes
both font modes and420/1000pixel widths. This is software rendering evidence.

`ui_source_canvas_capture` renders the production source widget with native
UiPresenter/UiDiligentRenderer pixels uploaded unchanged to its SDL software
surface. It uses the same original Gallery sample and actual panel selection at
1180/700pixels. Real Ctrl clicks also capture the selected three-panel group and
its native operation controls. It clicks Save preview image, compares the production image task's
file against an independent native readback, checks same-owner automation source/
preview guards, output grants and image projection. `mcp_ui_source_image_vulkan`
serves the actual native preview through the existing AutomationWorkspace and MCP
bridge in both protocol envelopes, with read-only/output grants, stale guards,
receipt replay, exact saved-file/image-block PNG and source/Scene isolation. Native
parents must exit cleanly. No detached pixel producer stands in for that image.
These MCP image/source scripts share `FORGE_MCP_TEST_PROTOCOL=both|current|legacy`
(default both). The fixed-project image workflow reuses its observed target for
reads, asserts target stability at every discovery and still discovers revision/
sequence before each mutation. Its host120s and CTest180s guards are unchanged.
Optional Vulkan UI probes enable the existing SDL dummy/software profile;
X11/Wayland/KMS dependencies remain disabled. The existing pinned source widget
is reused. These are component captures, without full Main/physical/DPI
acceptance; see [capture provenance](images/readme/README.md).

`ui_source_gallery` runs Gallery210 through native RmlUi bindings, actual pointer
commands, linked styles and last-good reload. It retires contexts while RmlUi is
still initialized, then checks renderer resource release. It packages a three-record
closure, relocates it, hides the source project and runs the same consumer again.
This is native CPU presentation/content proof, without a GPU or an exported
Windows executable run. Optional source authoring links Core only in the UI
presentation profile; runtime-only configurations retain no presentation dependency.
Full Main/MSVC/D3D12, physical input/DPI and external-client acceptance are separate.

Fast approximate anti-aliasing uses focused Scene-setting/source/history/reference
checks, six actual-input layouts, both MCP framings, actual Game/Scene View/cache /
Undo, temporal combination, unchanged linear HDR and native post-display UI pixels.
Independent analytic display-edge coverage includes neutral/blue and HDR values,
transpose/negative/steep slopes, borders/resize/alpha and pixel-budget retention.
Gallery 143 supplies source-hidden relocated export and four exact moving software
Vulkan display pairs. Native renderer units receive separate strict compile proof;
normal software rendering does not establish a clean native GPU sanitizer exit or
physical/Windows acceptance.

MCP sanitizer fixtures must request graceful host shutdown and verify its own
successful exit. A passing Python supervisor after killing its child proves
functionality only. Source-host tests use the shared portable stop marker and an
ImGui context owner. Focused document/source workflows run both framings with
unsuppressed native exits. The full multi-MiB boundary stays on normal native
consumers because instrumentation exceeds the unchanged production bridge deadline;
no transport limit is relaxed to make an instrumented timing fixture pass.

Single-base Prefab variant coverage includes source1–4 compatibility, typed base
closure, explicit equal-value intent, reviewed stale bases, Rebase, variant-only
Apply/history, missing-member identity, custom scalar/reference masks and migration.
Actual editor input covers six narrow/wide100/150/200percent layouts. Runtime-only
creation, a4096-member atomic request boundary, animation checkpoint continuation,
current/installed relocated SDK, both MCP envelopes and bounded inherited reads
exercise the shared owners. Gallery142 exports with editable sources hidden;
four moving software Vulkan source/export display pairs must match. Selected
changed FORGE units and actual consumer links run strict sanitizer exits with
unsuppressed leaks; other units, dependencies, unchanged workers and SDK modules
remain normal. The multi-MiB MCP boundary fixture runs on normal native consumers;
its instrumented admission exceeds the unchanged production bridge deadline.
Instrumented MCP still covers the ordinary variant workflow over both framings.
Full Windows Main/physical and graphical EXE acceptance are separate.

The packaged Feature Gallery README rewrites its repository manual links to the
included offline HTML manual. Its manifest hashes the derived bytes; authored
Scene, asset and cooked-resource bytes remain unchanged.

Physics preflight reuse is checked through actual 3D/2D bodies, controllers,
joints/loads/pose commands, rejected configuration, recovery, both SDK hosts,
relocated installed SDK and MCP Play consumers. The load-only regression changes
mass/damping, then rejects a disabled target and verifies retained force/velocity
on retry. Strict ASan/UBSan/leak coverage compiles the changed FORGE physics and
actual test units; unchanged native libraries remain normal. Compare the checked
physics workload with identical sampling/native/configuration policy; stage wall
time is not per-worker or physical/game performance.

`runtime_cpu_benchmark` includes selected dynamic 3D physics fixtures, independent
all-body local/world free-fall proof, active/gravity/tick invariants, physics scope
coverage, native provenance and compatible legacy/custom reports. Eight functional
methods cover negative admission, raw summaries, real subprocess comparisons and
immutable outputs; the actual large quiet workload remains a separate measurement
gate. This source-tool change reuses the existing runtime/collector/client and
changes no native code/ABI/game-creation operation; unchanged attributable native
sanitizer/manual/package evidence remains reusable.

Pose-history ownership uses `runtime_clock_and_interpolation` for both legacy
copying and owned-value admission: local interpolation, parent/resolution snaps,
sparse insertion/removal, generation recycling, empty captures and unchanged caller
data. `cpu_profile_process` and `runtime_cpu_benchmark` cover actual isolated ticks
and source/world correctness. Relevant SDK Play, root-motion, material-animation,
hierarchical-transform, physics/recovery and runtime protocol checks cover consumers.
Native capture-only before/after evidence requires matching driver/source/binary/
native/configuration and quiet paired runs; it is separate from full-tick costs.
Strict sanitizer proof covers changed Runtime/Clock FORGE units; unchanged native
libraries remain normal. Public SDK/ABI/format/automation operations are unchanged.

The original transform subset of `runtime_cpu_benchmark` covers: independent fixture parent
sums/canonical IDs, statistics, rejected world/source changes, report/baseline
admission, and actual small runtime reports with comparison/immutable-output
preservation. It is a correctness gate, with no noisy performance threshold.
The tool consumes existing Profiling/protocol2 and changes no engine/SDK code;
reuse attributable owner/native sanitizer evidence while those inputs remain
unchanged. Representative default-size timing runs belong at a quiet milestone,
with raw reports and source/build attribution. Broader renderer/asset/game workloads
and physical/native Windows execution remain separate evidence.

Single-touch classifications reuse `touch_input` for independent exact timestamp/
coordinate boundaries, native queued early-UP handling, out-and-back movement,
cancel/multicontact/device/clock/reset and both latch domains. Its real GameInput/
Play fixture also exercises post-drain long-press and quick Tap forwarding.
`runtime_ui_input` retains UI/capture separation. Native Settings tests include
`settings_lifecycle_single`, plus existing touch/relative gesture modes; selectors
derive navigation from actual discovery rather than a hard-coded tail offset.
`touch_gestures_process` and `native_sdk_touch_gestures_installed` exercise Gallery89
and91 in both ordinary/SDK hosts, public pressed-edge transforms, invalid-batch
preservation, restart and relocated installed SDKs. `sprite_gallery` packages and
loads Gallery91 without project/image sources. `mcp_touch_gestures_stdio` discovers,
binds/saves and consumes relative and classification controls through both envelopes.
These remain portable tests; physical touch/latency, full Main/game and native
Windows graphical executable acceptance require their separate queued gates.

Native dispatch tracing reuses the CPU/compile-out/UI/ordinary/SDK/transport and
both MCP profile tests below. Core checks include actual nested native dispatches,
timer skips,64-depth overflow, invalid/long UTF-8 labels, cancellation, Clear epochs,
foreign-thread exclusion and zero disabled clock reads. `cpu_profile_native_api`
retains external native OS callbacks; `cpu_profile_script_boundary` verifies
fail-closed file confinement after a conflicting OS installation, while
`cpu_profile_script_worker` verifies a fresh worker's successful confined evaluation.
The existing Script/known-include tests cover production process isolation.
The native trace macro is PRIVATE to Flecs; SDK definitions/tables remain unchanged.
Strict sanitizer evidence must include the actual native tracing compile
configuration. A normal native library or an older nontracing sanitizer build
cannot prove that path. Actual native Windows Main/startup and external-client
interaction remain separate from these portable checks.

Source echo reuses `audio` for independent nonzero PCM recurrence, combined filters,
disabled/zero-feedback bypass and exact echo-buffer bounds. An original impulse
checks post-EOS one-shot retention, repeat gain, source/game/full pause, independent
game-pause policy, Stop/restart and finite tail retirement. Native start rewinds an
EOS sound; Resume tests must prove tail-only output without replaying that clip.
`audio_editor` drives saved echo fields, Undo/Redo/reopen and tail-only Pause/Resume
at100/150/200%. `audio_process`/`native_sdk_audio` and
`native_sdk_audio_filters_installed` cover ordinary/SDK/relocated installed values;
`mcp_audio_pause_stdio` uses the same schema/control/observation owners and both
framings, including live bad-value preservation. `audio_falloff_gallery` includes
Gallery90 in derived export roots and plays its relocated cooked content after
removing WAV/project sources. `feature_gallery` checks additive defaults and saved
source settings. Physical listening/MSVC/full editor and new captures stay separate.

Analog conditioning uses `analog_input` for independent math/routing/settings and
an original public C++ callback. `native_sdk_analog_input_installed` rebuilds that
consumer from installed headers, checks shared Flecs and repeats after SDK
relocation. `analog_input_process` tests both actual Play hosts; the existing
host/settings/service checks cover defaults, last-good publication and rebind.
`settings_lifecycle_analog` drives real ImGui numeric controls at100/150/200%.
`mcp_analog_input_stdio` edits/saves/rejects settings and observes actual Play input
through both MCP framing modes. Native Windows/controller feel/capture stay queued.


Native ECS profile checks reuse `cpu_profile`, `cpu_profile_service_compile_out`,
`cpu_profile_process`,
`sdk_play_runtime`, `cpu_profiler_transport` and `cpu_profiler_editor`.
The compile-out fixture compiles the actual service with collection disabled and
links the same simulation; it is not a second full compile-out engine build.
The ImGui fixture also builds in the GPU-independent MCP editor-owner profile;
it tests actual controls at100/150/200% without claiming native window acceptance.
`mcp_ecs_profile_stdio` drives both actual MCP framing modes through Play, including
stale revision/generation, execution grants, Stop/Clear and unchanged authored Scene.
The public named C++ RateTick system is inspected by `native_sdk_simulation_speed`
and repeated after installed SDK relocation by `native_sdk_unscaled_time_installed`.
Native Windows/MSVC,
external-client interaction and authentic new table captures remain separate gates.

CPU interval coverage reuses those owners/tests and adds
`mcp_cpu_scope_trace_stdio`. The core/compile-out checks cover measured starts,
duration-only exclusion, clock-read counts, bounded eviction, nested/crossing layout,
unit conversion, malformed data and paged stable origins. Native ImGui input tests
exercise Timeline, clipboard JSON and scrolling at100/150/200%, including a small
window. Real ordinary/SDK Play transport retains starts and collection epochs;
the public SDK duration callback remains untimed. Both MCP framing modes traverse
multiple128-span pages, reject invalid ranges/stale cursors and preserve authored
Scene state through Stop/Clear. These checks do not establish external trace-viewer
acceptance, full native Windows Main integration or physical performance.


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

Full numbered editor builds and full editor source audits allow 360 minutes.
A cold numbered build measured approximately 140 minutes before acceptance began;
a later complete gate reached 455 of 571 results before the former240-minute
limit cancelled it. The full test selection and individual guards remain intact.
Combined rendering/capture diagnostics retain240 minutes; other focused source
audits retain150 minutes. Compiler host metadata is retained with the numbered
viewport diagnostics so native compile measurements can be attributed to their
runner hardware and image. Windows Debug/core and both native SDK jobs
allow 180 minutes; Linux Debug/core retains 120 minutes. The same Debug run used
about 52 minutes for setup/build and 68 minutes for 310 of 398 tests before its former 120-minute
job limit expired. These overall budgets cover compilation, complete testing,
installation and cleanup; the test selection and individual guards remain explicit.
The core/SDK CTest invocation gives otherwise unbounded tests a default of 600 seconds.

The combined `hdr_bloom_gallery` gate retains all fourteen Scenes, the exact
24-record export closure and source-hidden relocation checks within a 120-second
aggregate budget. Individual material-resource waits remain five seconds.
Explicit per-case limits retain precedence, including the 900-second Debug Tile Map
stress case. A stalled native process therefore fails its case rather than consuming
the entire remaining matrix-job budget.
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

The Tile Map navigation Gallery fixture registers four independent CTest scopes:
`navigation_tile_sources` (floor), `navigation_tile_obstacles`,
`navigation_tile_colliders` and `navigation_tile_dynamic`. Each retains its full
900-tick authored/source-hidden playback and provenance/recovery assertions within
90 seconds. The 2D Gallery fixture similarly registers `physics_2d_gallery` (bodies),
`physics_2d_joints_gallery` and `physics_2d_one_way_gallery`, each within 120 seconds.
All original playback ticks, source-free relocation and exact package-closure
checks remain. Direct invocation without a scope still executes the entire fixture.
The HDR/Bloom fixture shares one read-only catalog snapshot per unchanged authored
or relocated runtime root, verifies unchanged catalog bytes, and retains all Scene
inspections and the exact 24-record source-free package closure within 60 seconds.
Its flushed stage timings distinguish catalog admission, inspection and packaging. A single serial
before/after run on a local Linux QEMU virtual CPU with GCC12 measured the unchanged
HDR workload at 48.54→42.91 seconds in Debug and 8.12→6.91 seconds in Release. These
fixture observations include catalog and package work; they do not establish
Windows timing, gameplay frame rate or physical hardware performance.
The Windows Debug `animation_sequence` and `navigation` cases allow 120 seconds
after measured passes at 88.81 and 81.51 seconds against their former 90-second
limits; other configurations retain 90 seconds. These are functional fixture
watchdogs, not gameplay performance thresholds.

For the explicit portable-owner regression group, dispatch `editor-audit.yml` with
an immutable `source_commit`, `workflow_only=true` and `core_regressions_only=true`.
The existing Windows Release configuration and compatible compilation cache are
reused. `tools/run_core_regressions.py` requires all 25 named cases in the configured
CTest registry, builds their native executables and required workers, and executes
the complete selection. Missing or duplicate registrations fail before execution.
The selected cases and targets are retained under `grid-test-images/core-regressions`.
This scope cannot combine with other diagnostic modes and does not replace the
Debug/core, installed SDK, graphical, editor or complete-package gates.
The publication fixture also checks unchanged metadata bytes through native Windows
extended and short path spellings, missing-path admission without directory creation,
and directory-link refusal. Link creation restrictions are reported explicitly.
The shared IO guard compares host spellings without changing persisted locators or
asset identity; Windows short-name expansion uses
[GetLongPathNameW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getlongpathnamew).

Both full editor routes read `tools/editor_acceptance_regex.txt`; the unnumbered
source gate exercises the same acceptance selection before allocating a new
delivery number. The shared selection includes source publication, reviewed
source-file and material-graph lifecycle checks. Navigation lifecycle and the
registered static-Mesh navigation UI fixture use the same portable owner test
group and are also included in full editor acceptance.

GPU-independent ImGui fixtures for material graph geometry, mesh refinement,
Navigation Agent progress/avoidance, particle appearance and Prefab member picking
are registered in that group. Keep each fixture's executable, CTest registration
and applicable acceptance selection together; a source file alone is not an
executed check. These owner/input tests complement native viewport rendering and
physical desktop review.

The portable `document_editor` fixture reuses the native document lifecycle suite,
including project/scene ownership, recovery, migration, Save/Discard/Cancel and
failed publication. Its JSON record cases cover malformed trailing content,
retained Scene/history/snapshot state, repaired-file retry, raw terminators and
UTF-8 whitespace. Both full editor routes select this fixture.
Native dialogs and recovery UI remain desktop acceptance checks.

`cache_tools_editor` drives the actual Derived Cache controls and asynchronous
statistics task. It checks the shared project-switch guard, completed-result
lifetime across a closed-window switch, queued cancellation and a fresh result
in the new project without altering either catalog or Scene history.

`automation_picker_editor` drives the actual automation, project-parent and
export path controls with long UTF-8 values, typing and picker cancellation. Only
the native OS picker result is supplied by the existing fixture boundary. It
checks complete values and export-request handoff without starting a listener,
creating a project at the chosen path or exporting. Native Windows picker and
filesystem acceptance remain separate.

`editor_log` executes the shared timestamped log owner with fragmented output,
repeated unterminated chunks, oversized complete lines, UTF-8 boundaries, large
multi-line appends and Clear/reuse. It checks bounded recent text and timestamp
continuity. It does not launch a compiler/game or replace native log-panel review.

`automation_log` launches a real SDL child that writes invalid diagnostic bytes
to stderr. The actual Play owner receives them, its registered `play.log` method
returns a value, and strict JSON serialization/round-trip must succeed without
changing the source log or scene. It also checks valid Unicode, incomplete byte
sequences, compiler text-page offsets and an unstarted export's empty log. The
fixture is not a live MCP connection, successful runtime game or Windows export.

`export_lifecycle_editor` exercises a real background export rejection, the shared
EditorFiles busy guard, the actual Close Export control and project switching.
It checks same-project retention, clearing old errors while closed, the existing
automation scope guard and preserving a matching result completed before drawing.
It does not claim successful Windows assembly or exported-game execution.

`selection_editor` drives actual Hierarchy plain/Ctrl/Shift clicks, filtered
ranges, primary retention and legacy single-selection writes. It also executes
the existing batch-authoring assertions for spatial roots, atomic invalid edits,
Undo/Redo, prefab overrides and the batch Inspector. Selection must leave authored
state/history unchanged; owning selection snapshots must remain independent.
It also draws actual SceneTools overlays: reversing scene-row order must preserve
vertices, UVs, colors, indices and draw-command ranges for the same selection.
Cases include single/multiple selections, missing IDs, duplicate requests and
unresolved/non-positioned/helper rows. Drawing must retain selection order and
leave its borrowed projection unchanged.
The shared framing assertions also run here: multi-root expansion covers
overlapping/repeated roots, structural versus spatial ancestry, cycles, row-order
independence, empty selections, invalid IDs, the 10,000-row profile and mixed
mesh/helper positions. Native viewport compilation and actual camera fitting
remain separate from these portable selection-expansion checks.
Selection reconciliation checks intact/pruned ranges, primary retention, legacy
writes and non-entity selection. The actual cached caller is exercised through
delete, Undo, Redo and changing Scene owners. Existing snapshot tests also run for
direct native writes, prefab data, reset, preview invalidation and retry. The
native editor loop still requires its separate build/input review.
This portable fixture is included in the shared Windows selector. Native input,
large-selection frame timing and the full editor entry point remain separate checks.

`dependency_lifecycle_editor` uses copied projects with identical asset IDs and
different saved declarations. It checks UI-only, read-only and closed-panel poll
switches, fresh sessions, stale review rejection, preserved dirty/busy guards and
a real asynchronous catalog save. It leaves scene history and the other project's
catalog unchanged. Registered methods are invoked directly; MCP transport and
native Inspector rendering remain separate validation routes.
The same executable's `dependency_review_reload`, `dependency_review_retry` and
`dependency_review_ui` cases exercise malformed catalog reads, a real conflicting Save, a failed Discard, repaired-catalog recovery,
clearing obsolete failures and retaining successful operation attribution. The UI
case draws the actual Inspector and retries after repair; exceptions must stay
inside the panel. All three cases are in the shared native acceptance selector.

`settings_lifecycle_defaults`, `settings_lifecycle_diagnostic` and
`settings_lifecycle_queued` draw the registered Project Settings owner through the actual DocumentWorkspace. They verify automation-created defaults
survive a frame, stay unsaved until explicit Save and retain their application ID;
Cancel keeps failed drafts; actual mouse input on Discard clears them; accepted
close cancels obsolete Save requests. Project switching and Scene history remain
protected. Native window/scale review and full MCP protocols are separate checks.

`native_sdk_configuration` interprets the installed CMake package with explicit
Windows/Linux toolchain profiles. It checks exact-profile acceptance, detailed
rejections and the Windows deployment CMake-version preflight, without compiling
or loading a native module. The shared Windows selection includes it. The full
`native_sdk_package` check still builds/loads real installed consumers and also
checks links in every installed SDK guide.

For a repair confined to test fixtures or diagnostic wiring,
an attributable preceding full run can supply unchanged-case evidence: rebuild
the affected producers and run the complete affected selection natively before
allocating again. This avoids repeating unchanged rendering/core work to diagnose
a fixture fault. The next numbered gate still runs the entire editor selection
and every required core/SDK/shared/package job; partial diagnostics cannot replace
that delivery gate. Product changes require a new proof for affected behavior.
The shared selection includes `mcp_`: native stdio/configuration
checks and the GPU-independent editor document/MCP process test. These tests are
built with the default editor targets; project files stay in temporary directories.
The 968-action native input walkthrough has a 1800-second aggregate CTest budget,
including three graph preparations through the native shader worker. Before native
runtime tuning it creates a Plane, marks Navigation Surface, builds the ordinary
Scene NavMesh and assigns that published asset to its idle Navigation Agent through
the Inspector picker. Scene/runtime admission remains enabled. The navigation
preparation check allows 75 seconds for bounded geometry loading, worker completion
and UI adoption. The [native input audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/43549352143)
took 1402 seconds to reach step 844 before this required
setup; the aggregate covers the remaining actions and additional preparation. Each graph
readiness check allows that owner's 300-second worker budget plus 15 seconds for
UI adoption; ordinary actions retain 12 seconds and C++ compiler checks retain
180 seconds. Worker and child-process watchdogs remain unchanged. The aggregate
allowance removes no actions, pixel/source checks or acceptance requirements.
While a graph readiness check waits on the external shader importer, the WARP
input fixture yields 1000 milliseconds between frames to leave CPU time for the
compiler. During that same wait, the fixture retains its existing graph preview
resources but suspends redundant 3D preview redraws on the software adapter.
Readiness requires an actual preview draw in the current frame after compilation,
as well as the existing current-publication and GPU-resource checks. Capture and
failed-source last-good checks use the complete preview. The report records
`fixture_graph_preview_pauses` and `fixture_graph_preview_drawn` alongside timing.
Ordinary authoring input keeps its existing pacing. The report records
`fixture_shader_wait_frames` and `fixture_shader_wait_pause_ms`; compiler deadlines,
all 968 actions and publication/preview acceptance remain required.
The input group also runs `material_graph_lifecycle` and `shader_pipeline` to
cover real preview-worker retirement before Save publication, publication-time
compile exclusion and shared cooked Shader admission. CPU transport fixtures
verify immutable program selection, preview values and last-good retention;
the native walkthrough separately requires actual compilation and GPU readiness.
The legacy 217-stage scaled rendering fixture yields 100 milliseconds per frame,
retaining its original viewport sizes and 600-second aggregate/45-second stage
guards. Its chrome metadata records `fixture_native_frame_pause_ms`. The focused
input diagnostic includes this legacy capture alongside the complete walkthrough;
its three CPU source/input regressions remain selected. Sequence-frame capture
at all three scales also runs in this group to verify the shared pacing report
through its actual consumer.
Focused shader/input scopes remain explicitly separate. Numbered
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
The complete installed-SDK test has an aggregate watchdog of 600 seconds for installation,
the installed sample build and relocated consumer probes; child deadlines remain
independent. Standalone graph preparation permits 330 seconds around the supervised
native cook (300 seconds) and Material publication. The two direct game fixtures
permit 420 seconds, with their gameplay watchdog (90 seconds) starting after
preparation; package workflows permit 600 seconds while retaining limits of
120 seconds for exported-game processes. These are test
workload bounds, not startup or runtime performance claims.
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

Requested Windows builds restore the dependency checkout/build directory and CMake/Ninja build directory under the runner's short `AgentFiles` paths. Configure, build, tests, and packaging always execute even on a cache hit. Product source is freshly checked out. Verified source-content records preserve timestamps only for unchanged inputs, allowing Ninja to reuse compatible outputs; changed inputs rebuild. Successful compilation records verified source stamps and saves the reusable
compilation cache before acceptance begins, so a later test failure or timeout
cannot discard that completed work. Cache admission never qualifies a package;
all required tests, installation and package checks still run.

Windows core and SDK jobs also place fetched dependencies under the short
`AgentFiles/d` directory. Both the base configuration and SDK MCP supplement pass
the same path explicitly so reapplying a preset cannot restore the longer default.
This keeps nested third-party object paths within the native compiler's path
limits. Linux jobs retain their profile-specific dependency directories.

The compatibility key includes the runner image, architecture, MSVC version, Windows SDK, CMake/Ninja versions, absolute checkout path, and CMake configuration/dependency pins. The source commit is appended to the cache entry key; a compatible prior commit can supply a restore fallback. There is no fallback across different compatibility keys. Editor and shared-game jobs can retain compilation after a later validation failure. Restored objects are reusable build inputs, not test evidence. Cache reuse never bypasses required tests or delivery gates. Cache upload failures do not prevent artifact delivery; a failed restore is discarded before a fresh configure.

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

End-user source pages live in `manual/`, separate from these technical documents. `python tests/manual_test.py` checks supported formatting, task navigation, escaping, local links/images and identity validation. Packaging renders the current pages with `tools/build_manual.py` into a standalone offline HTML manual and includes the Markdown sources and PNG screenshots. `tests/windows_example_packaging_test.py` verifies screenshot bytes and manifest inclusion through the actual ZIP writer. No web service or extra documentation dependency is required. Follow the [manual authoring and screenshot guidelines](manual.md) when updating user workflows.

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
The package command requires `--gallery-preparer` pointing to the matching built
`forge_navigation_collision_gallery_tests.exe`. Before writing the archive it
prepares a private Gallery copy with the adjacent asset/navigation workers: the
collision course's Tile Set, then Map, then dependent navigation. This binds the
selected inputs to the actual platform and exact compiler/source recipe without
editing the checkout. Existing asset IDs and prior immutable artifacts remain;
matching selections are retained. The private writer lease is released before its
lock file is removed. The preparer is not shipped. Preparation failure leaves an
existing archive intact, and the manifest hashes the prepared bytes.
The same final-package test copies
the extracted gallery into a writable project, exports its saved startup scene
using the packaged static runtime kit, and runs the resulting standalone
executable in startup-verification mode. The source-level `feature_gallery` test
admits every gallery scene through the normal authoring API and checks durable
identities and key components.

The shared-runtime audit exports Gallery73's Sprite fill, Gallery78's image regions
and Gallery87's ordered frame regions
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

The Windows environment key includes compiler/SDK/runner identity, paths, CRT and
linkage profiles, global configuration and dependency declarations/patches. Editor
consumer declarations in `cmake/editor.cmake` are reconfigured instead of changing
that environment key. Ninja rebuilds or relinks their changed inputs. The acceptance
fixture reads the editor's final direct link dependencies during generation, so
features composed later in configuration remain linked into the acceptance host.
The cache regression performs a real CMake library switch, including a preserved input
timestamp, and verifies changed execution without recompiling the consumer object.
The version2 key starts a new cache namespace; no older cache is treated as accepted
validation evidence. Every restored build still configures, compiles and tests.

The audit and package editor jobs store `forge-source-stamps.json` with the build
cache after configuration/build work. It records SHA-256, byte size and modification
time for tracked regular source files. On compatible cache restore, unchanged bytes
recover their original input times, so a fresh Git checkout does not force Ninja to
recompile the whole project. Changed/new inputs are newer than the cached completed
build even if a file copy preserved an older timestamp. Deleted files remain deleted;
symlinks and paths outside the checkout are not modified.
Changed inputs use the restoration time rather than a future offset, so CMake's
generated build manifest can become newer than its inputs. A recording time that
does not precede restoration is rejected before any input timestamps are changed.

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

`audit_capture_only=true` combines the20Shader/Material checks with native UI
rendering, the input-receipt and pending root-motion Inspector units, and eight
Main editor capture/workflow checks. The31-case union retains the original sheet-picker and waveform cases. Both
diagnostic routes share the Shader target/selection constants in the reusable
workflow. The root workflow also sets `workflow_only=true`; direct calls must set
both flags. This is a partial audit, cannot combine with the separate Shader-only
or benchmark modes, and does not replace complete acceptance before delivery.

Native capture wrappers stage the original Gallery sources, identities, declared
dependencies and caches needed by their workflow. Legacy Scene references are
retained conservatively without registering new identities. This avoids importing
unrelated examples before a focused capture. `gallery_project_fixture` verifies
the source bytes, records and edges of all seven capture projects.

Core CI explicitly disables editor-owner fixtures before its complete core suite,
including after restoring a cache used by the SDK supplement. Shared SDK CI then
configures the GPU-independent MCP owners after that suite,
then runs12Play workflows and `mcp_workflow_profile` against the real shared SDK
runtime. Six SDK-only UI/render variants are absent from static configurations;
audio runs in both profiles with an explicit fixture environment selector.
`mcp_workflow_profile` checks that routing and all six audio selectors. No profile,
source-identity or runtime admission guard is bypassed.

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

The numbered editor job allows 240 minutes for a cold native build plus its complete
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
frame, optics and lightmap checks. The lightmap case reuses the numeric source
rendering oracle, including negative/nonfinite irradiance and indirect-output
overflow, on the native backend. It uploads `FORGE-Editor-Source-Audit` with test logs and
captures. It does not reserve a build number or produce a delivery ZIP.
The focused gate stops after its first failure for diagnostic feedback; acceptance
requires every selected case to pass in one completed run. Unexecuted cases in a
failed run are not passing evidence.
`editor_render_shaders` owns the complete native stage/graph compile, reflection,
cooked reload, failed-candidate and device-reuse checks. It performs a GUI render
on that same device after compilation. `editor_viewport_render` retains the shared
device across the remaining renderer consumers. Both bounded cases are required;
shader assertions are not omitted from either the focused or full selection.

[The native acceptance run at f37df45](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/43133480312)
passes all17 selected cases, including the complete shader lifecycle, shared-device
viewport, numeric lightmap rejection/overflow, native worker relocation and project
UI pixels/source-hidden relocation. This establishes the selected Windows/WARP
fixture coverage; installed-package and physical GPU acceptance are separate.

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

### Complete multi-hit rays

`native_sdk_physics_rays` executes the actual appended callbacks and C++ clients
through ordinary and production SDK Play hosts in both domains. It checks copied
surface geometry, distance order, sensor filters, complete counts, output clearing,
wrong-size arrays, invalid/null/capacity inputs, aliases, foreign threads, fixed
scope and denied Physics permission. Native owner cases cover exact/overflow object
budgets, canonical equal-fraction ordering, retired generations, sleeping/queued
command purity, characters, compound/painted shapes and two-sided mesh geometry.
The closest-query and 2D mover consumers retain their existing regression checks.

`native_sdk_physics_volumes_installed` installs and relocates the matching SDK,
checks local guide links, builds the editable PhysicsVolumes code from installed
headers, prepares and relocates its project, then runs both original scenes through
both hosts. Independent box equations cover hit counts/nearest fractions, reversed
segments, misses, sensor selection, change-only diagnostics and ordinary-worker
recovery. Preparation discovers unregistered primary/secondary scenes through the
existing bounded source scanner with the explicit Scenes root. It preserves source
IDs and rejects mismatched schema stamps and preexisting destinations.

These tests are selected by the existing native SDK/editor acceptance patterns.
A local Linux pass is not MSVC, full editor interaction, physical GPU, new screenshot
or graphical exported-game acceptance. SDK Play uses authored restart; ordinary
worker recovery checks do not claim arbitrary SDK Play state recovery.

### Canonical physics-query exclusions

The ray SDK probe additionally executes all eight typed exclusion callbacks and
C++ helpers through both real hosts, with permitted/denied/fixed/foreign contexts.
Known box geometry checks skipped nearest targets, complete results, aliases of
canonical input/output rows, duplicate/max/invalid reference descriptors and raw
size/capacity failures. Original closest-ray aliasing and nonfixed output clearing
are regression cases. Native3D/XY owners check filters across rays/volumes/sweeps,
legacy2D single-ignore composition and exclusion before unique-result budgeting.

The installed PhysicsVolumes gate includes the two exclusion scenes and original
unfiltered scenes. Independent geometry accounts for enabled project-owned markers;
Live property changes and ordinary recovery use the existing runtime owner. An
ordinary volume query remains unaffected by the ray selection. Existing RayProbe
schema/data stays unchanged; the new marker is a separate game-owned concept.
No new global query manager, authority system, dependency revision or persisted
simulation filter exists. Matching SDK/module rebuilds are required; native Windows
and graphical acceptance remain separate from local Linux checks.

### Native ray subshape-work bounds

`native_sdk_physics_ray_work` creates an original glTF with 16385 separated
triangles, imports its Mesh and Triangle Mesh Collision through the real CLI,
then queries it through ordinary and SDK Play hosts. A segment intersecting
exactly 16384 triangles returns one object. Including the next triangle rejects
the entire read and clears caller outputs. Raw/C++ canonical exclusions and a
layer filter skip that object and reveal a farther box; optimized closest rays,
clean retries, ordinary recovery and source-byte preservation remain checked.
The fixture exercises the subshape-work branch separately from unique-body limits.

The XY owner admits at most 4096 native shapes per world, below its 16384 callback
work bound for one complete ray. `physics_2d_compound` queries a full 4096-part
object, excludes every part and compares native checkpoint bytes. It does not
claim an unreachable single-ray overflow or the separate shared character-work
budget. These are correctness workloads, not performance measurements or native
Windows/GPU acceptance. The public APIs, SDK fingerprint and authored formats are
unchanged; the new matching test module uses existing callbacks.

### Moving and trigger Tile Map collision

`tile_assets` uses a real published cross-chunk Map through all four ordinary
Static/Kinematic and solid/sensor configurations plus signed Static one-way cases. It checks native targets,
preserved cells/epochs/depth, rotated ray/overlap/sweep geometry, sensor inclusion,
canonical exclusions, Begin/End/nonresponse, empty/refill retirement and fresh
Map-before-physics recovery with continued steps. Unsupported geometry, global
shape budgets and source replacement still run through the existing Map preflight.
`physics_2d_debug` covers matching positive previews. Static nonsensor one-way
Maps use per-rectangle solid-face arrows; Dynamic and moving/sensor one-way Maps
remain rejected. `physics_2d_compound` includes inline/painted response, long-fall
contact continuity and exact recovery; `native_sdk_one_way_tile_maps` and its
installed variant cover Gallery86/public cell edits through both Play hosts.
`box2d_snapshot` exercises native empty/static/sensor/dynamic reconstruction;
`box2d_snapshot_guards` verifies exact-source rejection, LF/CRLF equivalence and
unchanged input/generation timestamps for the selected build repair.
`physics_2d_compound` admits4095 Map sensor rectangles plus one visitor at the
4096-shape ceiling, preserves all4095 native Begin records, rejects a4097th shape
and avoids replaying retained overlaps. A valid4096-shape Kinematic sensor case
with two sensor visitors exceeds the existing4096 pair-work cap and must fault;
no partial native step, event adoption or recovery capture is accepted.

`tile_gallery` executes original scenes81/82/86 before and after normal dependency
packaging, source removal and relocation. It observes the painted course following
its ordinary animation, trigger visitors reaching a solid receiver and native
transitions without writing saved scenes. This is headless source-free content
execution, separate from a graphical exported Windows executable.

`native_sdk_moving_tile_map` runs the public C++ Map target/state/query/exclusion/
event consumer through all four profiles in both real hosts. The installed variant
builds that consumer using only relocated installed SDK headers/libraries, checks
shared Flecs linkage on Linux and executes the same workflow. `mcp_map_profile_stdio`
combines existing native placement and Play owners in its fixture, using a copied
published Gallery. Both MCP protocols place a Map and author motion/sensor through
ordinary component/property operations, invalid/stale guards, one Undo/Redo, Save
and actual Play. Fixture template preparation is not claimed as MCP asset import.

`physics_tile_map_editor` is one shared GPU-independent owner fixture in full-editor
and portable owner-test configurations. It exercises the actual Sensor checkbox,
Kinematic preservation/Undo/Redo and async selected Map sensor/solid guide at three
scales. The normal Collision layer key picker/Prefab/history/stale tests remain.
Full native Main/MSVC/SDL/pixels and physical Windows captures stay separate.

### Requested navigation destinations

`navigation_lifecycle` builds real saved3D/XY assets and draws the selected agent's
requested point through NavigationTools. Actual ImGui geometry/coordinates and
wrapped caption bounds are checked at100/150/200% in ordinary and420px viewports.
The existing checkbox is exercised with real input. Invalid, cleared, hidden,
hidden-parent, nonfinite, unresolved, unselected, wrong-asset and wrong-Scene targets
do not invent a guide. Authored field changes, Undo, saved source bytes and reopen
are checked; existing project/Scene cache and region gesture tests remain active.
Camera/light/audio consumers exercise the extracted shared builtin value decoder.

`mcp_navigation_stdio` exercises the same overlay flag through both actual MCP
protocols: grant discovery/denial, strict boolean inputs, qualified revisions,
idempotent receipts, conflicting sequence reuse and unchanged Scene/settings.
An old receipt returns its cached result without reapplying the view change.
Existing navigation/XY/live-control workflows still run in that fixture.
These portable owner/UI checks are separate from the full native editor entry
point, MSVC, actual pixels and physical review; capture requested guides on Windows.

`navigation_lifecycle` also drives the actual destination Inspector action and
SceneTools plane/axis handles in those scale/viewport contexts. It checks pinned
target routing, unavailable/selection-locked targets, exclusive gesture ownership,
world snapping, detached previews, one Undo/Redo and no-op clicks. Escape, right/
middle buttons, edit lock, viewport change, selection change, hidden overlay and
application focus loss cancel without applying the draft. Committed values follow
normal Scene Save/reopen. Core cases cover mirrored/singular parents, stale Scene
revisions, copied scope/plane changes and inherited Prefab field masks/Revert.
Existing rig input tests include focus loss through the shared cancellation guard;
the native focus event's SDL delivery remains a separate Windows check.

MCP checks edit all three destination fields through the same property batch,
Undo/Redo and existing qualified target path. The native main entry point and
command-palette composition remain source-inspected in local editor-off builds;
portable class/input tests do not establish a full native user walkthrough.

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
toolchain/configuration cache, rebuilds all MCP owners selected by the editor
configuration, installs their real game runtime kit, and runs its full `mcp_`
selection. Native-SDK-only compiler/inspection cases remain in their separate
SDK configuration; this diagnostic does not enable a different linkage profile.
Native host stderr and shutdown reasons remain visible on failure; private stdout connection
handoffs are never logged. This unnumbered partial audit cannot qualify a delivery.
Fixture control files tolerate Windows sharing/lock races until their next poll;
only a completed removal consumes a requested action. A locked-handle probe checks
that behavior on Windows. All disposable MCP fixture controls use these helpers,
including document recovery and SDK probe replacement. Production file/lease
validation rules retain their own ownership and failure behavior.

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

## Unscaled time gate

`runtime_clock_and_interpolation` uses injected monotonic timestamps to distinguish
raw observations from fixed dt, Pause, zero/slow/fast rate, catch-up debt and Step.
`game_host_controls` checks copied clock freshness without losing settings/input/
rebinding metadata or executing queued operations early. `native_sdk_simulation_speed`
executes the public C++ timer during zero rate and Pause through SDK Play;
`native_sdk_unscaled_time_installed` repeats that consumer after an installed-only
build and SDK relocation. `simulation_speed_transport` covers both ordinary and
SDK Play clocks. It holds zero rate for at least 100 milliseconds, then uses the
existing bounded transport wait for a newer unscaled observation, checking that
fixed ticks stay unchanged throughout. A copied status is not assumed to arrive
within that hold interval. `mcp_unscaled_time_stdio` checks the same Play status through both
actual MCP protocols, stable fixed ticks and authored immutability. These portable
checks are not native Windows/full editor/physical GPU acceptance.

## Gamepad feedback gate

`gamepad_feedback` also covers sixteen independent logical slots and both input
latch domains with real SDL virtual controllers: scoped disconnect/remap/assignment,
stale revisions and other-host connection tokens, native Stop failures, independent
feedback and the sixteen-device ownership cap. Its public C++ fixture queries and
assigns a controller through the actual SDK effect/ack path, then pulses slot2;
full Play input queues reject before physical routing changes. Installed/relocated
`native_sdk_gamepad_feedback_installed` repeats that actual public client path.

`multiple_gamepads_ui` drives the real GameInput popup by mouse and keyboard at
1/1.5/2 scales, including scrolling to the last of sixteen controllers, transfer to occupied Player16 and bounded popup layout.
Its complete native software PNG is the manual's canonical assignment capture;
provenance is in the screenshot record. `settings_lifecycle_players` selects an
indexed stick and radial conditioning through the native Settings widgets.
`multiple_gamepads_process` verifies Gallery92's four and Gallery149's sixteen conditioned actions
and movement per fixed tick, scoped neutralization, whole-batch rejection and
restart in both running hosts. SDK Game defaults and ordinary authored bindings
are separate expected conditioning inputs. `native_sdk_multiple_gamepads_installed`
builds the public sample from installed headers/sources and repeats after relocation.
`sprite_gallery` includes92 in its actual prepared/relocated source-free closure,
preserving all four marker identities and Sprite values.

`controller_slots_gallery` validates all seventeen authored entities and packages
the selected Scene/Sprite resource closure, hides sources and reopens relocated
content with exact Scene identities/values. It is content-only export proof; the
process tests separately execute the optional code in both hosts. No graphical
EXE or physical controller acceptance is implied.

`mcp_multiple_gamepads_stdio` uses actual SDL virtual connections and both MCP
framings for query/discovery, live assignment/scoped input release, stale and invalid
requests, UI-busy rejection, document revisions, sequence replay and independent
read/execution grants. These tests establish owner/protocol behavior, not physical
controllers, full Windows Main/game_main/MSVC or graphical exported EXE acceptance.

`gamepad_feedback` executes the public C++ request/receipt example through the
real PlaySession → SDK platform offer → SdkPlatformEffects → GameInput → SDL
virtual-controller callback path. It checks native strength conversion, replacement,
Stop, missing/unsupported devices, native failures, device switching/removal,
duration expiry, teardown, focus/routing rejection and duplicate-offer suppression.
`native_sdk_gamepad_feedback_installed` builds its client using only installed
headers, then relocates the SDK and repeats that consumer. Existing host/SDK suites
check malformed values, absent routes and correlated/batch-atomic acknowledgements.

These editor adapter fixtures register only with the game platform and either
full editor or MCP editor-owner tests. The GPU-independent MCP profile enables
SDL joystick support for real virtual devices; ordinary minimal headless profiles
retain their smaller process-only SDL selection. This changes test configuration,
not a dependency revision. No video backend or fake rumble implementation is used.
A passing virtual-controller callback is not physical motor/driver/Windows/GPU
acceptance. Full native Main/game composition and source-free graphical exports
remain separate platform gates.

### Ordered Sprite frame-region checks

sprite_region covers full-bank crop fit, off-selection invalid rectangles, overflow
and first-listed overlapping hit selection. sprite_scene covers512 entries, native
Meta count limits/copied reconstruction, rejected authoring, Undo/Save and Prefab
Revert; property Revert retains its ordinary empty override container. transform_animation
checks bank counts in whole-clip preview/fixed/recovery admission. Shared portable
sprite_editor and animation_properties_editor run the same sources used by the
Windows editor fixtures. Native integer draft seeds preserve JSON integer types.

native_sdk_sprite_frames_installed builds the public relocated client and tests
both hosts,3-frame C++ writes and512 live entries. mcp_sprite_frames_stdio tests
both actual framings through shared discovery/property/history/Play owners, with
invalid/stale and source-isolation checks. sprite_gallery and feature_gallery
include original87 and source-free content. The existing standalone image-export
script also exercises87 when Windows acceptance runs; local execution is separate.

The actual software-Vulkan Sprite probe checks768 grid/bank fill/filter/alpha/flip
cases, mixed instancing and complete retained-resource behavior. Pass the Gallery
argument to include87's landscape/portrait animation images. Native Windows/full
Main/physical hardware and new Inspector captures remain separate acceptance gates.

### Touch input checks

`touch_input` injects the pinned native SDL queue and exercises bounded contact slots, viewport/window changes, invalid/canceled/overflow/foreign/emulated contacts, tick edges, rebind and actual GameInput/Play. `runtime_ui_input` drives actual Rml UI pointer ownership, synthetic touch ordering and neutral reset. MCP owner fixtures enable SDL's hidden dummy window backend without Linux desktop dependencies. Both fixtures are also available in native Editor builds.

`settings_lifecycle_touch` selects native touch controls and verifies unipolar presentation/Save/reopen at100/150/200%; the analog case retains existing conditioning coverage.

`touch_input_process` runs original Gallery88's public C++ marker in both real Play hosts, verifying independent coordinates/pressure and neutral restart. `native_sdk_touch_input_installed` builds the same module from relocated installed headers/sample/runtime. `mcp_touch_input_stdio` covers discovery/ranges, settings save, invalid/stale submissions and Play values in both framing modes. The Sprite Gallery content fixture includes88 in a relocated cooked closure after source removal. These are local desktop checks, not physical touch/Main/Windows graphical export acceptance.

### Touch gesture checks

`touch_input` includes queued native SDL centroid/log-distance/angle-aspect and branch-crossing cases, third-contact/pair replacement/coincidence/reset, quick-gesture edges and independent fixed/control one-latch consumption. Actual keep-routing release covers pending gesture neutralization. Settings touch/gesture cases select native controls at3scales; runtime_ui_input retains Rml capture ownership.

`touch_gestures_process` and `native_sdk_touch_gestures_installed` exercise original Gallery89/91's exact C++ canvases, one-time application, bounded pan/scale, both Play hosts, source isolation and neutral restart; the installed module builds the shared example sources. `mcp_touch_gestures_stdio` covers discovery/units, native settings rejection/save and exact paused Step observations in both framings. Sprite Gallery cooks/includes89 and tests its relocated source-free identity/values. Existing raw88 consumers remain covered. Native Main/game_main/MSVC, actual touch hardware and graphical EXE acceptance are separate pending gates.

## Desktop controller motion consumers

`gamepad_motion_values` admits copied physical SI data, complete uint64 nanosecond
timestamps, independent supported/enabled/measured states and atomic strict JSON
records. It rejects malformed/unknown fields, nonfinite/stale payloads and stale
tokens/slots; a fresher disconnected/empty row is safe, while resurrection is not.
`gamepad_motion_devices` uses real pinned SDL virtual accelerometer/gyroscope
queues, rates and native mode callbacks through SdlGamepads. It covers independent
devices, disabled/routed data, pre-capture boundaries, native disable
failure/retry, scoped remap/reassignment, malformed input, asynchronous disconnect
between observations, stale tokens and normal teardown disable.

`sdk_play_runtime` exercises copied raw data and sensor configuration over the
existing scoped effect/ack path, including new revisions without cursor-epoch
cancellation, invalid metadata and release/clock traffic. `gamepad_motion_process`
runs the original public Gallery93 module: ordinary simulation is neutral without
Game, SDK Play handles mode offers/acks and consumes the accompanying raw reading
in the exact Step tick; paused reads do not advance simulation. The module opts
into supported Player1 sensors through unscaled Game requests. The installed/
relocated `native_sdk_gamepad_motion_installed` builds the same public example
without core input headers. `gamepad_motion_play` additionally drives actual SDL
sensor events, GameInput and the SDK platform-effects owner through native Play,
focus/recapture/Stop and continuous-sample clock scheduling.

`multiple_gamepads_ui` covers native mode checkboxes, reported rates, real mouse
and Tab/Space interaction, occupied-slot assignment and1/1.5/2-scale layouts using
the pinned ImGui SDL software backend. Captures preserve full original PNGs;
provenance is in [README screenshots](images/readme/README.md). The same real
native owner serves `mcp_multiple_gamepads_stdio` query/configuration, busy/grant,
revision/sequence/replay, unsupported sensor, schema and foreign-token checks in
both MCP framings. `sprite_gallery` includes Gallery93's source-free Sprite and
Scene closure/relocation. Manual/package assembly suites remain separate.

These are local CPU/native-library/software-renderer/virtual-controller checks,
not Windows/MSVC full Main/game_main compilation, physical controller calibration,
sensor latency, GPU performance or a graphical exported-game acceptance. The
canonical pending Windows queue remains in the agent audit report. No numbered
build or ZIP is created by this source-validation profile.

## Runtime UI animation checks

`ui_animation` runs the original Gallery100 RML/RCSS through actual RmlUi with a
recording render adapter. It checks linear/alternate width and opacity progression,
equal-time hold, paused and zero-rate gameplay, pointer commands, native hover/state
transitions, transform submission, invalid semantic/unsupported/malformed reload
retention, valid restart, owned resource retirement and a three-record relocated
content package with the original project offline. UI source bytes remain packaged
resources; this is not an export that removes RML/RCSS.

`mcp_ui_animation_stdio` uses both protocol framings and the normal workflow host.
It checks contained source writes, exact receipt replay, stale revisions/Play
identities, Edit-only write guards, successful live reload, failed semantic reload
with the prior command path intact, paused stepping, Scene isolation and execution
grant restrictions. It uses the source resource root for the packaged default font;
no Windows runtime kit or Windows execution is claimed.

The existing native UI rendering test additionally loads Gallery100 at wide and
portrait dimensions. It reads back exact animated coverage, premultiplied opacity
and translated marker pixels at two presentation times and captures each frame.
Software Vulkan evidence proves that renderer path, not physical GPU performance
or a full Windows editor/standalone acceptance. The normal package includes the
gallery through the existing examples assembly path.

## Component field animation example

The combined package builds the canonical ComponentAnimation/Native project with
its installed SDK, prepares its ordinary module deployment through the existing
example preparation owner and includes it in Examples/ComponentAnimation. Its
process regression consumes the same compiled sources in both runtime host modes,
checks270ticks per host against independent cubic-speed, held enable/direction
and quaternion equations and
reconstructs a fresh worker from a checkpoint. The test removes editable animation
and Native sources before running; Scene metadata and selected cooked bytes remain.
This proves native execution/content behavior, not graphical Windows acceptance.

The installed SDK's component_animation_gameplay target borrows the original
example files. The focused sdk_package_test.py --component-animation-only check
compiles installed-only, validates dependency imports and runs after SDK relocation.
Native field updates additionally exercise heap-owned state and an unreflected
padding counter through the shared ReflectedCandidate/AuthoredCodec owner.

`animation_component` also admits actual nested scalar/discrete/enum/flag members,
exact64-bit values, mixed root and sibling bindings, multiple copied struct
ancestors, readonly ancestors/leaves, wrong paths, whole-curve rejection,
pause/seek/recovery and opaque projection siblings. Source8 path promotion guards
both descriptor and key metadata. `animation_properties_editor` edits nested paths
through actual Timeline controls at six scale/window combinations and samples them
in copied sequence frame output without project callbacks. `mcp_source_stdio`
checks Source8 discovery, both framings, invalid path/key identity, exact values,
revision/history and publication. The canonical installed C++ example uses Settings
with schema4 and animated Axis array entries; its prepared scene/clip retain original asset/entity/key IDs.

The common material Timeline regression is registered from one CMake recipe for
both editor builds and the portable authoring test configuration.

`animation_component` extends the same gate with native fixed and compound arrays,
primitive and struct leaves, composite/single-key and unkeyed Meta vectors, actual
reordering, owned strings/hidden counters, whole-curve rejection, readonly/index/key
policy, deletion/clear/grow isolation and pause/seek/cold recovery. `core` separately
checks the admitted engine opaque-vector adapter's complete shrink/clear/grow with
heap-owned strings. The native writer uses the pinned public lifecycle-aware count
API; no dependency patch or opaque-project-container admission is involved.
Source9/cooked9/recipev12 retains legacy1–8 readers and guards opaque suffix metadata.
Real Timeline input and copied sequence output cover indexed float/bool/int64/enum/
flag controls in six scale/window combinations. MCP checks selector shape, exact
values, complete identity, revisions/history and publication in both framings. This tests actual
ImGui material editing through the shared Timeline owner; it does not enable or
claim a portable native editor host. Component controls also exercise exact64-bit
inputs, enum/flag choices, Undo/Redo and copied sequence preview at supported scales.


## Clustered projector regression gate

`render_bounds` checks conservative receiver-cell membership for 4000 deterministic
samples, float-boundary/overflow cases, large origins, collapsed axes and stable
index order. `shader_assets` preserves 1–10 archives and rejects unsupported packet/
storage metadata; `material_asset` checks typed capability reflection and existing
sampler budgets. Spatial transport capability 11 keeps 19 surface output roles;
capability 12 extends this profile with the light images described below.

`decal_vulkan` exercises the actual prepared-draw and scene/resource owners.
The focused `--clusters-only` path compares 128 projector spatial output against an
exhaustive storage list over affine, large-origin, dense, collapsed and outside-grid
cases and repeated frame/empty/small/storage transitions. `--channels-only` covers
shared normal/roughness maps; `--scene-only` checks scene admission,129 projector
rejection, resource recovery and two-instance/one-draw batching with a unioned grid.
The complete normal run also covers output variants and actual sampler reflection.

`decal_cluster_gallery_vulkan` opens Gallery 116 through ordinary authoring and content
owners, checks 64 projectors, preserves prior catalog records/settings, exports the
exact Scene/Material/Texture closure and compares four source-free relocated display
readbacks. `mcp_document_stdio` creates/saves 128 projectors through an atomic batch,
checks idempotency and whole-batch Undo/Redo and rejects invalid commands without
revision changes in both protocol framings. Lost responses recover the original
sequence receipt. Its 240-second test budget accommodates the instrumented owner
workload. No new agent-only authoring path exists.

These portable/software-Vulkan checks do not establish MSVC/FXC/cookedD3D12, full
Windows editor input/DPI, physical GPU performance or graphical exported-EXE acceptance.
Native sanitizer shutdown remains an independent unresolved/unsuppressed condition;
functional completion is separate from a clean process exit.

## Light image regression gate

`authoring_api` covers typed Light values, defaults, source/history validation and
image-only frame rejection while ordinary Point lighting survives. `material_asset`
checks Shader 12 packet/Cube reflection and readable old capabilities.
`reflection_probe_editor` also exercises actual Light image assignment, dimensions,
clear, Undo/Redo, save/reopen and contextual live availability.

`mcp_soft_shadows_stdio` includes all three image kinds, metadata, invalid atomic
edits, revision/replay, history, disk and read-only grants in both protocols. Its
180-second fixture budget accommodates the expanded instrumented workload;
production transport timeouts are unchanged. The
soft-shadow Play consumer checks live image dimensions/tiling and rejects texture
replacement. `native_sdk_soft_shadows_installed` compiles from the installed SDK,
relocates it, checks incompatible fingerprints and exercises public image values,
both hosts, live validation, unchanged legacy source bytes and cold recovery.

`light_images_vulkan` runs actual direct-radiance/image sampling, source mip/LOD,
shared slot admission and scene last-good/dimension/layer/replacement checks. The
full `decal_vulkan` gate includes these cases alongside existing output/resource
profiles. `light_image_gallery_vulkan` checks Gallery 117, each visible image, exact
Scene/three-Texture export closure and four source-free relocated displays. The
Gallery catalog retains every prior record. Private independent coordinate/color
oracles supplement these product tests with exact-input receipts.

Run the corresponding strict/unsuppressed consumers at the coherent batch gate.
Normal functional success is separate from known native sanitizer shutdown behavior.
Do not claim Windows/MSVC/FXC/cooked D3D12, full graphical editor/input/DPI or physical
GPU acceptance from these portable/software-renderer fixtures.

## Local diffuse irradiance regression gate

`reflection_probe_values` covers the appended scalar's native Meta/defaults/bounds,
independent diffuse-only extraction, history and saving. `material_asset` validates
capability 13's 41-row/eight-Cube packet and rejects missing, malformed or
old-capability bindings. `shader_assets` preserves 19-role archives and previous readers.
`reflection_probe_editor` includes actual diffuse input, invalid rejection, history,
source persistence and the enabled diffuse-only volume guide at six scale/size pairs.
Its `--capture-irradiance` mode records authentic current controls.

`mcp_document_stdio` checks discovery, values, invalid mutations, replay/history and
saving through the ordinary owner. `mcp_reflection_probes_stdio` adds live diffuse
controls/rejection while preserving paused time and authored state in both protocols.
`native_sdk_reflection_probes_installed` builds matching public values and tests
both hosts, native fixed writes, live validation, cold recovery and unchanged legacy
source bytes. `feature_gallery` admits all 118 sources through the shared API.

`local_irradiance_vulkan` exercises actual Mesh material layers, zero/nonzero global
environment, large origins, winding/clear/outside transitions and invalid inputs,
then actual FrameRenderer map adoption, last-good replacement, removal, eight/nine
capacity, Mesh particles and scene changes. `local_irradiance_gallery_vulkan` checks
both visible map effects, the Scene/two-Texture closure and four source-free relocated
display pairs. Private exact-input prerequisites additionally prove actual convolution,
independent face/volume/material oracles and the full sampler/image/storage workload.
Actual combined area/projector temporal-reflection and transmission sources reflect
10 pixel constant buffers, 16 samplers, 43/44 images and three storage views,
including all eight irradiance Cubes. The broader `decal_vulkan` test also compiles
all 19 physical graph roles and exercises complex material admission; its combined
software-renderer workload has a 300-second budget.

Run affected strict/unsuppressed consumers, previous lighting/view/Shader consumers
and manual/package checks at the coherent batch gate. These checks do not establish
Windows/MSVC/FXC/cooked D3D12, full desktop input/DPI, physical GPU performance or
actual graphical exported-EXE acceptance. Functional native completion is separate
from the existing unresolved unsuppressed sanitizer shutdown condition.

## Navigation collision collection regression gate

`navigation_collection_values` checks strict opt-in/layer values and preserved
default settings. `navigation_collection_sources` checks both source paths, layer31,
motion/sensor/disabled/marker filtering, unresolved prefab physical intent, physical
inline/prepared-shape bounds and truncated-input rejection. Curved native shapes
are checked against independent scaled physical extents.

`navigation_collection_pipeline` exercises actual worker publication/admission for
both planes and static/dynamic profiles, malformed version7 metadata, stale-candidate
retention, Agent distance budgets, layer-change holds and exact recovery. Directional
links compose with collected sources while another named profile remains isolated.
`navigation_collection_resources` replaces a selected Collision revision through
normal publication/physics refresh and checks that stale-route movement holds.
The four `navigation_tile_*` scopes cover marked and collected Map sources,
runtime cell invalidation/reset, recovery and source-hidden content relocation.

`navigation_link_editor` also checks collection/layer controls at100/150/200percent
through actual input, without changing Scene history. Its `--capture-collection`
mode records the saved Gallery141 profile's real controls. `mcp_navigation_collection_stdio`
covers both protocol envelopes, discovery/schema, invalid rollback, revisions,
Undo/Redo, actual Play/step routes, live values and read-only denial. The current
and installed/relocated `native_sdk_navigation_behavior` process checks include
Gallery141 through ordinary and SDK Play hosts.

`navigation_collection_gallery` runs repeated original Patrol behavior around the
physical wall, then hides sources and reopens relocated exported content. The
existing `forge_navigation_polygon_gallery_vulkan_probe` accepts Scene141 for four
exact moving source/export Game display pairs. Manual provenance lives with the
canonical screenshot record. Native Windows packaging invokes both
`forge_navigation_collision_gallery_tests` and `forge_navigation_collection_gallery_tests`
as repeated `--gallery-preparer` arguments against one private stage. Real ZIP
fixtures verify composed bytes/hashes, failure retention and exact runtime-lock
exclusion. The supported cross-target CPU Collision recipe in portable fixtures
does not establish Windows/MSVC/native package, full editor, physical GPU/input/DPI
or graphical exported-executable acceptance. Run those separate acceptance owners
before making those claims.

## Sequence pair consumer checks

`animation_sequence_collections` checks deterministic two-clip blends, gap and held
cuts, controller seeks/wraps and recovery against exact clip revisions, alongside
the multi-clip collection checks. `animation_sequence_retirement` performs all 67
successive asset replacements beyond the 64-entry resource-pool bound in its own
bounded case. The existing child-loop, placement and property cases own their
checks once; the main sequence case retains clocks, persistence, camera/cue and
nested-graph checks. The depth fixture admits the complete 64/65-node snapshots
through the runtime without repeatedly reconciling editor creation transactions.
`animation_sequence_editor` drives Timeline timing,
pickers, Undo, copied preview and frame samples at three scales and two sizes.
`animation_sequence_gallery` checks the original Gallery145 and source-hidden
relocated cooked content, including the next clip's dependency closure.

`mcp_sequence_clips_stdio` uses both framings and the ordinary Scene schema,
permissions, revisions, replay, history and persistence operations.
`native_sdk_sequence` exercises direct and SDK Play hosts;
`native_sdk_sequence_installed` builds the sequence client solely from the installed
SDK, relocates it and repeats those native controls and fresh-worker recoveries.
Headless sequence workers request the existing offline audio output explicitly.
SDK package tests accept `FORGE_SDK_TEST_JOBS` for measured local build scheduling;
the default remains two jobs. Select concurrency from available CPU and memory.
These checks do not establish Windows editor or physical GPU/audio acceptance.

## Nested sequence consumers

`animation_sequence` also covers three-level derived clocks, child control rejection,
same-Scene reference/cycle validation, 64-controller admission, ancestor truncation,
exact root cue boundaries/multiple periods, paired clips, portable Prefab member
links, duplication and cold recovery7. Root seeks stage all descendant poses.
`animation_sequence_editor` drives placement/start/navigation/detachment and Undo
in six layouts through actual production Timeline input. Its copied frame samples
use the same descendant provider and preserve Scene intent.

`mcp_nested_sequences_stdio` exercises both framings, registered schema/limits,
permissions, revision/replay, history and save/reopen. `native_sdk_sequence` and
`native_sdk_sequence_installed` include Gallery146 through direct/SDK Play hosts,
typed parent/start values, derived observation, rejected independent child controls
and four cold-worker recoveries per scene. The installed case uses only the
installed/relocated SDK, with its current component fingerprint.

`animation_sequence_gallery` adds Gallery146 to its source-hidden relocated content
closure. `animation_nested_cue_gallery` uses the existing real offline audio fixture
to verify seven pitched cues across parent loops, camera/geometry composition,
silence between cues and source-hidden relocated export. Ordinary audio-cue tests
retain their unnested profile. These CPU/software checks do not establish full
Main, Windows/MSVC/D3D12, physical audio/GPU/input/DPI or graphical EXE acceptance.


## Heightfield foliage consumers

`terrain_source` checks scalar/batch equality, triangle diagonals, bounds, missing
coverage and invalid input. `asset_build` checks immutable cache opening without
creating missing directories. `terrain_asset` checks actual published terrain,
transformed cooked-vertex height oracles, unchanged preparation state, source
selection, coverage/tilt/inversion rejection and ordinary Scene history.
`mesh_scatter` checks ordered resolved world heights, exact count/finite validation
and atomic Undo/Redo. `foliage_editor` drives planar and projected controls with
real input at three scales in two layouts, including Play locks and failures.

`mcp_mesh_scatter_stdio` covers raw resolved heights through the ordinary command.
`mcp_terrain_foliage_stdio` covers both framings, terrain creation/publication,
read-only discovery and permissions, shared preparation, revision/replay, placement,
Undo/Redo, stale/source errors and document saving. `foliage_gallery` checks original
Gallery144 and heightfield Gallery147, including 128 expected projected origins and
source-hidden relocated content. No project source reader is needed at runtime.

`terrain_foliage_vulkan` renders the same Scene View and Game consumers using the
existing foliage probe's optional Scene argument. It verifies compatible batching,
individual template picking, static frames, visible green geometry and four exact
source/relocated-export display pairs. A two-part terrain contributes two draw
instances; instance count is not Mesh entity count. Original `foliage_vulkan` keeps
its animated two-color scene. These software checks do not establish physical GPU
performance, Windows/MSVC/D3D12, full desktop, input/DPI or graphical EXE acceptance.


## Source reverb consumers

`audio_reverb` checks the independent private DSP's impulse, stereo separation,
callback-block invariance, in-place/NULL input, decay/damping, finite output and
admission/memory/tail bounds. `audio_reverb_runtime` and the normal `audio` suite
use the real offline provider for bypass, composed filters/echo/reverb, retained
one-shots, EOS, pause/resume, Stop/Restart, source/category mute and uninterrupted
state comparison. Authored defaults/bounds/atomic history and unchanged native
NaN failure caching/correction are covered. `animation_audio` retains actual gain
and pitch curves through the same mixer.

`audio_editor` drives all four reverb controls and Undo/Redo at four scales, plus
retained-tail Pause/Resume. Its optional `--capture-reverb` mode captures the actual
authored ComponentInspector on SDL software without mutating Scene intent or
inventing runtime status. `mcp_audio_pause_stdio` authors/tunes the same fields,
rejects invalid values and preserves authored content through both framings.
Its tone-only published closure avoids unrelated Gallery imports; the shared Map
fixture closure is preserved. Full-project imports have their own acceptance scope.

`native_sdk_audio` and `native_sdk_audio_filters_installed` check typed values,
commands, same-worker and fresh-worker recovery with matching configuration and
normal successful native exits. The installed case uses only the relocated SDK.
`audio_process` also checks legacy defaults and headless provider admission: an
audio-bearing runtime Scene requires output; a rejected candidate preserves the
previous silent Scene. No physical device is silently enabled.

`animation_audio_reverb_gallery` runs Gallery148's dry/bright/damped timed cues,
checks real tail intervals against the same Scene with reverb disabled and compares
11seconds of source/relocated-export PCM bytes exactly after hiding source files.
It also checks bounded voice turnover and unchanged saved content. These are
CPU/offline/widget tests, not full Main/Windows/MSVC/D3D12 or physical audio/input/DPI
acceptance, and they do not establish device deadline performance.

## Lossless WebP view-image output

`image_webp_encode` checks native exact RGBA (including RGB under zero alpha), odd
sizes and padded rows, bounded output callback failure, invalid extents/strides,
case-insensitive filenames, actual saved files, preserved previous output and
WebP-to-PNG protocol previews. `image_png_encode` retains the existing PNG writer
and atomic-storage checks; final fixture bytes remain attributable across the
shared callback-buffer extraction. `mcp_image_stdio` exercises both encodings
through the existing owner/granted root, both MCP framings, full-file MIME and
unchanged PNG image content. Its detached pixels are not rendered Game evidence.

`view_image_editor_vulkan` runs the same actual image-task editor fixture as the
Windows viewport gate at100/150/200% and wide/compact layouts, using native Vulkan
texture readback. It includes the file-picker adapter's WebP hint, appended-suffix
overwrite confirmation/Cancel and mismatched type/filename rejection. The native
picker itself remains platform acceptance. The normal `view_image_vulkan` keeps
its independent renderer/readback checks. These functional software-backend tests
do not establish physical GPU, full Main/Windows or graphical EXE acceptance.


## Input Message trigger checks

`input_messages` covers Pressed/Released/Changed, conditioned fixed analog values,
digital quick taps, inactive/removed/recreated sources, context/state activation,
payload unavailability without replay, rebinding, relative neutral transitions,
selection bounds and native snapshot lifecycle. `input_message_ui` drives real
Trigger/Action/Payload controls and history at100/150/200percent.
`mcp_input_messages_stdio` exercises shared discovery, range/default admission,
permissions/revision/replay/history and saved values through both envelopes.
`mcp_input_messages_play_stdio` adds actual project module dispatch, Released and
analog Changed through scoped Play input, clock neutralization and restart.

`input_messages_process` runs the original Gallery150 and trigger Gallery159
receivers through ordinary and SDK runtime hosts. Observers read the same fixed
input through the public SDK. `input_messages_installed` installs/relocates the
SDK, checks original shared source/header bytes, builds its sample and repeats
both hosts without developer library paths. `input_message_gallery` packages and
restores the saved scene from runtime content and checks kit compatibility.
`standalone_manifest` covers Scene/Prefab/variant declarations, both value channels,
legacy defaults, combined feature facts and malformed/missing kit profiles.
Local portable checks do not establish Windows/MSVC/full Main, physical input,
external MCP-client or graphical exported-game acceptance.

## Local fog volume checks

`fog_volume_values` covers native Meta/codec ranges, raw numeric admission,
camera-relative geometry/packing, masks/priority/bounds, Scene history, extraction,
snapshot recovery and live tuning. `fog_volume_editor` drives production controls,
Keep values/history, box/ellipsoid guides and framing in six wide/compact layouts.
`mcp_fog_volumes_stdio` covers both protocol framings, discovery/creation, permission,
replay/revision/history and save/reopen. `mcp_fog_volumes_play_stdio` and
`mcp_fog_volumes_sdk_stdio` exercise scoped ordinary/SDK Play, validation, stale
generation rejection, paused values, source isolation and review/apply/Undo.

`native_sdk_mesh_wind_installed` also compiles the public Fog Volume header and
values from a relocated installed SDK, checking header bytes and runtime identity.
`fog_volume_gallery` checks both authored shapes, hierarchical Prefab/chained
variants, explicit zero overrides, snapshot recovery, source-free content and kit
admission. `standalone_manifest` covers component/property-override channels in
Scene/Prefab/variant intent, disabled values and malformed/missing/future profiles.

Software Vulkan `fog_volume_source_vulkan` checks 24 independently derived analytic
cases through the production shader helper and packing. `fog_volume_frame_vulkan`
uses actual Game rendering, both projections, fog/empty/fog rebinding and layer
masks. `fog_volume_surface_vulkan` checks generated Mesh/Sprite raw/mip/temporal
color wrappers, alpha/discard/diagnostics and auxiliary targets; ordinary empty
volume surface checks remain separate. Generated source does not establish a
Vulkan loader for Windows-cooked Shader archives. Real Windows FXC/cooked surface,
full-editor/physical GPU and graphical exported-game acceptance remain separate.


## Shared bus impulse-response checks

`audio_convolution` compares the original private partitioned processor with an
independent time-domain sum across partition boundaries, dense mono/stereo inputs,
wet mixes, callback/in-place splits, maximum tails and silence/reactivation.
`audio_impulse_runtime` checks actual native PCM, shared/fanout/nested routes,
pause/mute, Source Stop with retained finite tails, typed asset impact, invalid
rate/length/gain, a valid cooked stream rejection and aggregate32MiB admission.
`audio_impulse_process` covers normal and SDK hosts, complete live faders and
source tuning, exact private recovery and changed-response rejection; accepted
native processes use graceful Quit and exit0. Installed SDK routing also includes
the impulse profile with only installed public headers/libraries.

`audio_impulse_export` copies the configured sibling sample, checks wet/bypass
and stereo output, rejects an old kit without replacing a last-good distribution,
then hides the source and relocates full exported content. All three records,
including the project-only response, are retained and48000frames of stereo CPU
PCM match exactly. Its Windows/D3D12 kit is a synthetic inventory fixture; its
executable bytes are never run. This proves content closure on Linux CPU, not
Windows executable or graphical acceptance. Package-writer fixtures assert cooked
`.fpcm` bytes and inventory hashes for sibling projects.

`audio_mix_editor` drives the production Content selector, wet slider and removal
at1180/700/400 widths, validates incomplete drafts/default mix and checks visible
glyph/horizontal bounds. Canonical impulse and shared-effect captures are copied
without pixel changes from SDL software control fixtures; see
[Audio](audio.md#imported-bus-impulse-responses). Actual MCP Settings and routing
Play checks cover both protocol envelopes, stale settings, source Undo/Redo,
read-only grants, faders, rejection and authored-state isolation. Full Main,
Windows/MSVC/D3D12, physical listening/device deadlines/DPI and external-client
review remain separate gates.


## Native UI text shaping

`ui_font_shaping` checks source-byte clusters, ligatures, combining marks, explicit
RTL/Arabic, tracking, known-pair kerning, bounds and rejection recovery against
pinned fonts. `ui_shaped_font` exercises the actual UiPresenter option, admitted
file aliases, glyph/effect textures, public memory fallback, real input, stable
textures and two cold native lifetimes. The backend source is original FORGE code
using public interfaces; native sample backend implementation is not adopted.

`ui_text_shaping_project` loads the original example through Scene/UiRuntime,
checks model text, command routing, retained last-good reload and retirement,
packages its five-record closure, relocates it and runs with the source directory
unavailable. `standalone_manifest` also checks opt-in/default compatibility,
malformed kit capabilities, retention of the previous distribution and the saved
backend choice in relocated full distribution configuration. Its executable bytes
are a synthetic inventory fixture, not graphical executable acceptance.
`mcp_ui_text_shaping_stdio` checks both protocol envelopes, exact settings digest,
boolean admission, Save/discard, Scene isolation and grant-based discovery through
the same native settings owner. `runtime_ui_input` retains the existing input
consumer and compiles its RuntimeUiHost header.

`ui_text_shaping_render` uses the native D3D12 UI fixture; its optional software
Vulkan consumer is `ui_text_shaping_vulkan`. The focused profile compares baseline
and shaped rendering at1000x800 and420x800, then requires exact shaped/relocated
pixels with source files unavailable. These are text-only UI consumers, independent
of unrelated Sprite renderer acceptance. The local Vulkan check uses llvmpipe
LLVM15.0.7/API1.3.255. It establishes no physical GPU, display or Windows result.
Both sizes include the initial HUD and native scrolls to the weight and color comparisons.
Light/regular/heavy glyph pixel coverage must increase, and all four shaped
frames plus the two color frames must equal their source-hidden relocated counterparts.

`manual/images/ui-text-shaping-game.png` is the unretouched1000x800 native shaped
Game UI capture from that local software Vulkan fixture, converted losslessly
from its RGB PPM output. It is not a full-editor screenshot. The source and
relocated output pixels agree; original font ownership/notices remain intact.
The offline manual renderer checks local links/images; browser review at1180 and
700px checks decoded images and horizontal overflow. Windows acceptance selection
includes the new native/helper/project/MCP/render tests, and the relocated editor
package UI check verifies the shaping notices, font and native example rendering.

Variable weight checks use the original reproducible TrueType fixture in
`tests/fixtures/fonts`. Native font callbacks check separate weights/sizes,
clamping, interpolation, exact repeated handles/geometry, stable texture counts
and cold retirement. Metadata tests cover optional fvar records, signed values,
shared name IDs, malformed data, aggregate face-axis budgets and inspection
transport. The sample refresh preserves its existing four IDs, Scene and settings.
MCP font workflows register/read the same axes, author numeric weights through
ordinary source tools and exercise native fields in both font modes/Play profiles.
The prior shaping profile is rejected before last-good distribution replacement.

`manual/images/ui-font-faces-inspector.png` and
`manual/images/ui-variable-font-inspector.png` are unretouched400px captures from
the actual `draw_ui_font_details` widget in `ui_font_details`. The latter is
400x440 and uses the registered original weight fixture; the collection view
is400x330. These are software ImGui fixtures, not full desktop/MSVC/D3D12/DPI or
physical-display acceptance. The current Game image above includes the original
100/400/900 weight comparison. Its losslessly converted PNG SHA256 is
`7deb3ffdff14ac3fb71b8dd1ae8c9ae9a2b06bd74f697eddf9dd9bfc22d70703`.


## Native UI character boundaries

`ui_text_boundaries` checks all853unmodified official Unicode18 extended-grapheme
cases through FORGE's UTF-8/scalar-coordinate helper, verifies fixture/notice
hashes, and checks malformed text/ranges, bounds and temporary key selections.
The separate unchanged upstream selected test confirms the exact provider's
matching behavior; neither is a complete upstream suite claim.

`ui_text_input` uses actual native input/password/textarea widgets in both font
modes and with boundary editing disabled/enabled. It covers combining accents,
emoji sequences, flags, Hangul, collapsed/interior/partial selections, native
arrows/deletion, canceled events, modifiers, disabled/max-length fields,
composition forwarding, focus release, callback removal and cold reset/shutdown.
Oversized fallback stress uses the original font mode because the shaped font
has an independent glyph-run limit. The known nonempty custom-commit behavior is
compared with the unwrapped native path; it is not accepted as maxlength support.

`runtime_ui_input` supplies actual SDL key/text/preedit events through the
production RuntimeUiInput and TextInputMethodEditor_SDL. It verifies whole-character
movement/deletion, the real empty-preedit/committed-text path, native maximum
length and gameplay input isolation. `ui_surface_grapheme_input` runs the existing
surface-routing consumer with the option enabled, checks camera-output combining
characters, and preserves native pointer capture, focus/IME projection, output
removal and retirement. `ui_surface_input` retains the original disabled profile.

`game_foundation`, `standalone_manifest`, `ui_text_shaping_project` and
`mcp_ui_text_shaping_stdio` also cover the independent saved grapheme option:
boolean/default/player-override rules, exact kitcapability rejection/retention,
source-hidden relocation with four original example IDs, and both MCP envelopes'
settings digest, Save/discard and read-only grant discovery. Settings/source
access does not prove MCP native-field input; that remains an explicit gap.

The whole-character guide reuses the unchanged authentic native HUD capture
already documented above: it shows the same field/layout and is not a new
Windows/full-editor capture. Its steps, controls, limits and Project Settings
links are checked by the offline manual renderer, fixtures and browser layout.
The editor/runtime collectors preserve full utf8proc and Unicode data notices;
package-writer fixtures verify inclusion without asserting a Windows executable
was built or run. Full Main/MSVC, Windows/D3D12, physical keyboard/IME/display/DPI,
graphical exported EXE and external-client review remain separate pending gates.


## Native UI field automation

`ui_field_automation` uses actual RmlUi input/password/textarea controls and both
font modes. It checks bounded/unique/native observations, omitted password and
oversized text, focus/text and the shared native key catalog, native limits, hidden/disabled fields,
stale instances/presentation revisions, held-input rejection, callback removal,
reload/reset and unchanged authored source. `ui_surface_grapheme_input` additionally
rejects a fabricated absent surface mapping and reuses a real current native hit
for field focus. The original surface/default and SDL input consumers remain in
the focused gate. Static text rendering/resources reuse attributable unchanged
proof; no new GPU or physical typography result is inferred from these tools.

`mcp_ui_fields_stdio` and `mcp_ui_fields_sdk_stdio` exercise the real providers,
Play owners and native presenter with both MCP protocol envelopes. They create
and register UI source through existing owners, save the whole-character option,
run actual fields and verify native typing/deletion/selection, disabled/max-length
rules, password response masking, wrong focus, stale Play/UI revisions, exact
input receipt replay, reload and authored Scene/source/settings isolation. Read
connections discover fields but cannot invoke native input. The existing graceful
fixture shutdown requires every native MCP host to exit0; forced termination
is failure cleanup. These are independent legacy-runtime and SDK Play results,
not an external-client or full graphical editor acceptance claim.

The workflow runner defaults to both protocol envelopes. For bounded individual
jobs, set `FORGE_MCP_TEST_PROTOCOL=current` or `legacy`; invalid values fail
before native work starts. Run both selections for each required Play profile
before claiming equivalent coverage. Each selection retains the same actual
workflow and graceful native shutdown checks. Separate capture files keep these
executions attributable; splitting a job does not replace its missing envelope.

The manual's MCP and input guides use the existing authentic native field capture
with unchanged layout. Offline rendering verifies all local references/images;
focused browser review checks the new tool steps/limits at desktop/narrow widths.
Existing sibling-package fixtures verify the extended example README and its
manual links. No asset identity, source format, runtime-kit profile or installed
native SDK ABI changes accompany editor-only field tools. Full Main/MSVC,
Windows/D3D12, physical pointer/keyboard/IME/DPI, an external agent client and a
real exported graphical EXE remain separately queued.


## Collection-font UI consumers

`ui_font_collections` exercises TTC v1/v2 admission, malformed directories/tables,
uppercase resource paths, indexed native metrics and glyph draws in both font
backends, retained byte sharing and retirement. `ui_font_collection_project`
loads the four-asset UiFontCollections example, packages and relocates content,
hides the authoring source and verifies native presentation with unchanged IDs.
`standalone_manifest` covers absent/malformed/mismatched collection kit support.

`mcp_ui_font_collections_stdio` and its `_sdk_` variant use both protocol
envelopes, both saved font backends and actual native Play fields. Registration,
metadata/read-owner publication, stable IDs, malformed files, scoped native input
and authored isolation use existing owners. The fixture writer only assembles
provided font bytes for the test; it does not grant an agent arbitrary file or
font-conversion authority. Original Poppins/Cairo data and complete notices are
retained in the example; the default bundled Lato stays unmodified.

Manual/link/image and package-writer checks cover the collection guide, example,
font data/notices, uppercase collection extension and offline README links.
Strict/instrumented selected CPU consumers remain distinct from full Main/MSVC,
Windows/D3D12, physical input/display, external-client and graphical EXE acceptance.

The collection MCP workflows also provide a raw font fixture in the granted
project scope and copy it through the actual prepare/review/commit controller.
They assert `copy_only`, reject cooking without creating a destination, verify
exact copied bytes/no implicit publication, close the review and explicitly
register the copied font. Existing source-copy regressions retain containment,
digest, stale-source and no-overwrite coverage. Native/SDK consumers are rerun
when this controller integration changes; unchanged native font evidence remains
reusable only with exact executable/dependency/fixture matches.

Native color-font checks use independently authored geometric COLRv0 fixtures.
`ui_color_font` exercises palette/foreground/text-alpha values, color-independent
atlas reuse, intrinsic partial effect alpha, unique/nonunique effects, empty
base outlines,32-layer admission/33-layer rejection without partial geometry,
alternating atlas-page order and cold retirement. `ui_font_metadata` checks
optional malformed color headers/spans/indices while retaining basic admission.
`ui_font_details` captures the actual read-only color metadata widget and verifies
stable source refresh. The existing two MCP font workflows register/read the same
color fields and author a colored HUD through ordinary source tools in both Play
profiles and protocol envelopes. They require local IPC capability.

The text-only native software Vulkan profile renders18frames at1000x800 and
420x800, including palette, foreground and half-alpha/effect cards. Six shaped
frames must equal their source-hidden relocated copies. Hue checks verify the
color glyphs reached the render target; these do not establish physical hardware
performance, Windows or full editor/graphical executable acceptance.

`manual/images/ui-color-font-game.png` is the full1000x800 native shaped color
view, converted losslessly from the fixture's PPM. Its SHA256 is `10060a360e2b94466c7243bcff4f746374981d0c00a0b5e3300a4318cd19ff0b`.
`manual/images/ui-color-font-inspector.png` is the actual400x240 color widget,
SHA256 `3430fc84904d6c1ed9fc5177bf9cb0726d8009fafbaa15e8c40813a34605bc49`.
Neither image is cropped, resized, retouched or a full desktop screenshot.
The current initial Game image SHA256 is `f658e463fb1713edae754c0aaab2fba4bf6fe8e6c2aaa969c80bc194b036192d`;
the existing400x330collection and400x440axis captures remain byte-identical.
This sample refresh retains its five prior asset IDs, Scene/settings and existing
font/notice files, adding two original color-font assets. Enabled shaping uses
matching source-derived profile3 and rejects earlier profiles before replacing
a last-good export. Exact pins and native SDK ABI policy remain unchanged.


`ui_native_edit_keys` compares all16additional aliases with actual paired native
keys in text/password/textarea fields, both font modes and both saved editing
modes. The192comparisons and192canceled actions check value, selection, focus,
paired events, native change counts, password masking, unchanged source and cold
retirement. Preparation uses actual select-all/text/Home/Right input plus native
layout/rendering: bare SetValue/selection assignments do not establish identical
native visual-caret state for this comparison. A fixture font/layout check prevents
an empty unrendered control from counting as native navigation coverage.

The current catalog contains32keys. Camera-surface checks also exercise document
selection/native word deletion after a real current hit. Both MCP field workflows
verify all new keys are discoverable and representative key families execute,
multiline/document selection,
destructive word-delete receipt replay, grants/revisions/focus, reload and authored
isolation. Native word deletion can split a combined character. This batch exposes
existing native keyboard behavior; whole-character word deletion, wider vertical/
page/line grapheme semantics and full IME remain incomplete. Editing/shaping
profiles and SDK ABI policy are unchanged. No new shader, physical input, full
Main/MSVC/D3D12, external-client or graphical exported-executable result follows.
Existing unmodified UI captures retain their recorded provenance.


The fields-only workflow reuses its observed stable project/document target for
read requests. Each mutation still performs fresh discovery for revision/sequence,
and every fresh discovery asserts unchanged target identity. Other workflows keep
their original boundary discovery. This avoids repeatedly serializing full method
schemas during polling without caching permissions, revisions or Play/UI scopes.
The native test host retains its240s lifetime guard; the expanded dual-protocol
CTest cases have300s overall budgets. Sanitizer acceptance still requires fresh
current/legacy envelopes in both Play profiles and clean native parent exits0.


UI image-resource authoring checks extend ui_asset_catalog with validated relative
references, format/type/source identity and current-byte refusal cases.
ui_source_image_properties drives the existing native picker and Content UUID
drop payload, property staging, Clear, Undo/Redo and Save at1180/700/400 widths.
The control-properties MCP workflow checks read-only image references and their
ordinary src mutation in both protocols, including stale guards and invalid bytes.
ui_source_preview checks missing native document origin and explicit-head recovery.
The native source canvas fixture also accepts --image-resources-only to isolate
the same image-selection/rendered-pixel workflow at1180/700 without repeating
unrelated canvas edits. Its full mode retains the image workflow. Existing
source-hidden Gallery/package tests cover raw image relocation and sprite
precedence. These local software/native checks do not establish Windows, full
Main or physical-device acceptance.


Guided font source checks use ui_markup for preserved ranges/comments/other
declarations, unchanged quoted/numeric/default spelling, source-list quoting,
invalid aliases/paths/faces and targeted declaration removal. ui_asset_catalog
and ui_font_collections check current file/face admission and actual generated
collection declarations. ui_source_font_faces drives real native controls at
1180/700/400, invalid-alias recovery, one-step history/removal/Save and existing
flat-rule insertion. ui_font_details covers the reused metadata drawer.
mcp_ui_font_faces_stdio checks current/legacy guards, replay, read-only discovery
and denied writers, current file corruption/face refusal and source/Scene/catalog
isolation. ui_font_collection_project authors a collection alias and input style
through the same helpers, refreshes through the UI resource owner and verifies
actual native alias loading after source-hidden relocated content export, with
existing asset IDs preserved. The complete source editor also covers the changed
Rules consumer and supplies its current authentic capture.

Selected CPU sanitizer scope and exact unchanged evidence are reported locally;
software component/native checks do not imply full Main/Windows/physical review.
