# 2026-09-25

## Cursor-release cleanup for prepare/load

A failing `platform.cursor(false)` during `prepare`, or during `load` with
`activate:true`, now cancels the still-pending candidate from that request,
leaves the active world unchanged, and preserves the save file bytes. The
original exception is rethrown so the request receipt remains inspectable in
the original endpoint. A subsequent host pump cannot activate the canceled
candidate.

Regression coverage in `tests/game_host_control_tests.cpp` exercises the
`std::runtime_error` path for `prepare` with `activate:true`, `prepare` with
`activate:false`, and `load` with `activate:true`; the catch-all path with a
non-`std` exception; and a successful `prepare`/`load` after the adapter is
restored. The test uses the real `GameSession`, `GameControlQueue` and
`GameStorage` execution path.

## Bounded polled host dispatch

`GameControlQueue::poll(Poll)` is added alongside the synchronous
`drain(Execute)` and uses a single shared dispatch loop. Each pending
entry runs at most once per host call; newly queued or requeued work
waits the next call. Pending entries are polled again on subsequent
calls until they complete, fail, or are dropped by world replacement,
module revocation, `release()`, or endpoint retirement; only completed,
failed, retired, revoked, or released tokens are not re-invoked. The
polling callback returns `std::optional<Json>`: `std::nullopt` keeps
the entry pending, a populated value (including an explicit JSON
`null`) completes the receipt in the same poll, and a thrown exception
fails the receipt in the same poll. Callback serialization and the
1 MiB size bound are performed inside the same try as the invocation so
a malformed payload becomes a failed receipt instead of escaping the
host pump. The in-flight slot reserves one entry's worth of capacity
during the callback so that a callback can attempt to enqueue new
work without violating the bound; a full queue still rejects.

`GamePlatformControls` gains optional `cursor_poll` and `navigation_poll`
adapters used only for the `cursor` and `ui_navigation` operations.
They receive the request token plus the desired payload and return
`true` for completed, `false` for still in flight, throw for failure
or cancellation. The adapter owns idempotency: one physical dispatch
per token; `false` is reserved for legitimately pending effects, while
cancellation and stale requests MUST throw. Configuring `cursor_poll`
requires a synchronous `cursor` callback so `release_cursor()` can
force a physical release on pause/unload/prepare. A `navigation_poll`
adapter alone does not require a synchronous `cursor` callback.
Dropping a queue receipt does not undo a delayed physical effect; the
embedding adapter continues to own its own lifecycle.

The polling path is wired through `GameHostControls::execute()` for
`cursor` and `ui_navigation`. All other operations, and the existing
synchronous platform adapters, are unchanged and remain supported.
Coverage in `tests/game_control_tests.cpp` and
`tests/game_host_control_tests.cpp` exercises pending across pumps,
explicit JSON null success, oversized and invalid-UTF-8 results,
callback exceptions, foreign threads, world replacement, module
revocation, `release()` while pending, callback enqueue of new work,
the 256 bound via tombstones, synchronous drain regression,
cross-module fairness, deferred cursor and navigation with explicit
ack, release-while-pending with delayed ack, polled adapter failure,
and the constructor validation that polled cursor requires a
synchronous cursor.

## Lifecycle release gating on polled cursor

When `cursor_poll` is configured, the lifecycle-mutating operations
`pause`, `quit`, `unload`, `prepare` (including `activate:false`) and
`load` with `activate:true` remain queued until
`cursor_poll(token, false)` confirms the release. The host re-dispatches
the polled release once per pump while the request is pending, even
when the host's local `cursor_captured()` flag is `false`, because a
prior capture may still be in flight in the adapter. No requested
lifecycle mutation, scene preparation, restore, schema validation or
migration runs while the release poll is pending, and an adapter that
throws on the release poll fails the request before any of those
effects occur. Command fields and types are validated by the host
before any release poll is issued. After the polled release has
acked, the synchronous `release_cursor()` path still confirms and
performs the physical release and existing rollback rules on a
synchronous failure continue to apply. The read-only `load`
(`activate:false`) does not request a release and returns the
validated, migrated saved data without polling; each required
migration step and the schema validation execute once for that load,
not repeatedly while awaiting release. A multi-version migration can
have several steps. Standalone synchronous adapters remain supported
and the constructor check for a missing synchronous `cursor` is
unchanged.
The synchronous `platform.cursor(false)` path now confirms an editor-acknowledged
release; it never flips the captured state itself. Outstanding obligations from a
superseded capture must be cleared by the editor first. `SdkPlayRuntime`
configures `cursor_poll` and `navigation_poll` against the editor's
`SdkPlatformEffects` adapter; lifecycle-mutating operations gate on the
synchronous confirmation.
Coverage in `tests/game_host_control_tests.cpp` exercises pending
across pumps for each lifecycle-mutating operation, release-while-
pending with prior capture and late ack, read-only load, and malformed
`run` rejection before the release poll.

`game_request` originates from gameplay and runtime code.

## Runtime-half PlaySceneAdmission

A private `PlaySceneAdmission` channel and its `prepare` adapter
factory are wired into `SdkPlayRuntime` for the Editor Play path.
The channel reuses the existing `GameSession` adapter slot and the
existing `GameScenePreparation` interface. No second world, queue, storage, or
renderer is introduced; no public SDK type, no ABI1 change, no
dependency pin or scene-format change.

The adapter's `poll` calls the new
`RuntimeWorld::prepare_scene_resources()` helper until it reports
ready. The helper extracts the runtime-only readiness previously
inlined in `GamePresentation::Candidate::poll` — animation initial-pose
pending, evaluate transforms, physics sync0, presentation reset, audio
sync, audio failed/unavailable source checks, and enabled non-prefab
`NavigationAgent` dependency validation. It does not tick, run control
callbacks, publish worlds, or touch GPU; physics collision assets are
prepared by `GameSession` before the helper runs. `Candidate::poll`
keeps its existing GPU/UI pipeline exactly as before.

The audio and navigation checks run inside `flecs::world::each`
callbacks. Under the pinned Flecs 4.1.6 iterator, throwing from inside
the callback leaves its table lock held and aborts world teardown
under sanitizers. The helper therefore observes the failure inside the
traversal (a `bool` flag for the audio unavailable check, an
`std::exception_ptr` for the navigation dependency check) and throws
after the traversal returns, preserving the exact first-failure reason
without unwinding through a locked table. Rejection semantics are
unchanged; this is a narrow sanitizer-safe correction, not an
architecture change.

The channel keeps only the session string, the last accepted factory
ticket (used to validate that supplied tickets are strictly increasing
— `GameSession` generates the tickets, the channel merely checks them),
and at most one pending record. The adapter holds a `weak_ptr` to that
record; the channel holds no `RuntimeWorld` references. The adapter
publishes a copied immutable envelope `{session, ticket, scene, ui}`
(UI is `null` when the runtime UI service is not installed), bounded
to 8 MiB serialized bytes. Runtime readiness may take more than one
poll (for example when the animation initial pose is still pending);
the helper is called from the adapter until it reports ready, and
subsequent adapter polls reuse the captured snapshot. The candidate
stays pending until an explicit accepted acknowledgement arrives for
the exact `(session, ticket)`. Rejected candidates retain the active
world and its clock; the next adapter poll throws so `GameSession`
discards only the candidate. Cancellation discards the candidate
directly through `GameSession` and removes the channel's pending
record. Rejected records remain in place until the next poll or
supersession removes them; the channel does not perform any actual
remote or GPU readiness validation and trusts the explicit ack.

Other limits are diagnostic text ≤ 8192 bytes, session ≤ 128 bytes,
strictly increasing tickets, and owner-thread enforcement on every
public method and adapter poll. `has_pending` reports only an
uncommitted record (which includes a record after ack but before
activation). Coverage is added in
`tests/play_scene_admission_tests.cpp` against the real `GameSession`.

## Native Editor SDK acceptance

The editor now drives the exact-SDK Play path through a separate-process
`SdkPlayRuntime`. The transport carries `{protocol:2, session, id,
runtime_contract, loading, platform_effects, release_required, module,
recovery, timing, activation, ...}` envelopes; the editor submits
observations, candidate acks, cancel-loading requests, monotonic epochs,
and activation/root-release outcomes; the runtime owns `GameSession` /
`GameStorage` and its world mutation. No `GameSession*`, `GameStorage*`,
SDL window, graphics context, or filesystem owner crosses the boundary.
`PlaySceneAdmission` is the private runtime-half channel that reuses
the existing `GameSession` adapter slot and the existing
`GameScenePreparation` interface — no second world, queue, storage, or
renderer is introduced.

Visible-and-focused `cursor=false` establishes logical menu routing
through `GameInput::capture_checked(relative_mode=false)` so
`RuntimeUiInput::event()` can route native menu keys to the Rml tree.
A hidden release still goes through `release_checked` and never
acquires routing. Physical capture requires `play.ready()` and a
visible panel; not-ready initial releases succeed because
`release_checked` tolerates the absence of a physical target.

### Input focus-gated menu routing acquisition

The `SdkPlatformEffects` adapter gates the initial
`cursor=false` → `GameInput::capture_checked(relative_mode=false)`
menu-routing acquisition on the Game panel actually holding ImGui
focus, not merely being visible. The host samples
`ImGui::IsWindowFocused(...)` immediately after the Game window's
`Begin()` returns visible and forwards the result to the adapter via
`set_game_focused(...)` (defaulting to `false`); the adapter's
`execute(...)` only flips routing on when the panel is visible,
focused, the runtime is ready, and `GameInput::captured()` is still
`false`. The pump itself was moved to AFTER the ImGui render block
in `src/editor/main.cpp` so the adapter sees the CURRENT frame's
`game_focused` and `game_visible` rather than the previous frame's
stale values — the previous-frame bool issue is fixed in this
batch. A hidden / unfocused capture offer is rejected with
`play.platform_effects.cursor.hidden` or
`play.platform_effects.cursor.unfocused` respectively.

The narrower rule preserves the existing owners: a visible Game
panel alongside a focused Scene / Console / Hierarchy panel does
NOT silently reclaim routing after the user surrendered it by
clicking elsewhere. Returning focus to the Game panel re-arms the
acquisition on a fresh focused frame; routing stays off until then.
OS focus return alone does not restore routing — the user must
intentionally re-focus the Game panel. Initial toolbar Play / menu,
Escape (SDK: keep-routing; legacy: pure release), hidden Game
release, OS focus loss, Stop, restart, legacy ABI1, the separate
runtime process, the SDK no partial recovery path, and the live DLL
reload restriction are unchanged.

The normal pointer UI surrender flow is Escape to pause, then click
an editor control that does NOT replace the Game panel. Scene /
Game are sibling tabs in the default dock, so clicking tab:Scene
hides Game and breaks the surrender flow; the live editor exposes
the Hierarchy panel's **Expand all** button (and the **Collapse
all** button + search bar) through `FORGE_UI_PROBE` markers, and
these stay visible alongside Game. After the click is processed
and a fresh snapshot is published, a Tab press through real SDL
MUST NOT navigate the live Rml focus tree — runtime menu keys
must not reach the menu while logical routing is off. Returning
to gameplay requires clicking the live pause menu's **Capture
gameplay input** button — clicking the Game tab while paused does
not re-engage relative mouse mode and does not enable menu
navigation; only the Capture button does. After the click is
processed, a Tab press MUST change the Rml focus element. The
pause menu's **Resume** button is then activated via the existing
`activate()` helper so a focused Resume selection exists, and
Enter resumes gameplay with relative mouse mode on.

The native surrender / return regression is exercised by the
editor fixture itself in `tests/editor_sdk_workflow.hpp`. The
fixture drives the real `forge::SdkPlatformEffects` adapter via
the wire-fed `pump()` against a real running `PlaySession`
whose `play.ready()==true` is what `acquire_routing` requires;
this is the only way the regression can fail with the pre-fix
focus predicate and pass with the fixed predicate (a synthetic
test overload would never satisfy `play.ready()` and would
therefore pass by construction with both code paths). Stage 2 of
the fixture's state machine drives the surrender sequence
through nine durable substates with frame-counter gating +
late-recorded snapshot baselines + bounded phase deadlines:

1. gameplay active (page `play`, rel mouse) → press Escape;
2. pause + pointer → arm two-frame `mouse_down` / `mouse_up` on
   `ui_targets["hierarchy:expand-all"]`;
3. wait until the click is FINISHED (UP was sent on a
   previous frame's pump) AND at least 2 more editor frames
   have passed → record snapshot baseline → assert
   `game_input_captured()==false` → record Rml focus → send Tab;
4. wait at least 2 editor frames after the Tab so the runtime
   has actually processed the queued key → re-record snapshot
   baseline → wait for `snapshot_version` advance → assert Rml
   focus label UNCHANGED + routing still off → capture
   `sdk-outside-surrender`;
5. arm two-frame click on
   `ui_targets["button:Capture gameplay input"]`;
6. wait until the click is FINISHED + at least 2 more editor
   frames → record snapshot baseline → assert
   `game_input_captured()==true` → record Rml focus → send Tab;
7. wait at least 2 editor frames after the Tab → re-record
   snapshot baseline → wait for `snapshot_version` advance →
   assert Rml focus label CHANGED → capture `sdk-outside-regain`;
8. navigate to "Resume" via `activate(window, "Resume")`, send
   Enter;
9. wait at least 2 editor frames after Enter → re-record
   snapshot baseline → wait for `snapshot_version` advance →
   tap E to advance to the existing stage 3 (beacon
   interaction).

The split-click is pumped at the top of `frame_impl` exactly
once per editor frame (mirroring the toolbar pattern), so each
click is a single render tick long. Snapshot baselines are
recorded AFTER the click / Tab is fully processed by the
runtime — never at ARM time, because a runtime snapshot can
advance before mouse DOWN/UP are actually consumed and that
unrelated advance would otherwise let a Tab assertion fire on
unrelated snapshot activity. Each phase wait has a 4-second
bounded deadline before the phase is declared stuck. The fixture
reads `game_input.captured()` through a read-only
`std::function<bool()>` observer that main.cpp installs after
`forge::GameInput game_input(window.get());` is in scope under
`#ifdef FORGE_UI_FIXTURE` — no new input authority, no new
protocol field, no parallel OS probe. The acceptance helper
`tests/editor_sdk_package_test.py` asserts
`focus_gated_routing=true` AND that the
`editor-sdk-outside-surrender.ppm` /
`editor-sdk-outside-regain.ppm` captures exist as REQUIRED
acceptance checks; the helper refuses to set `complete=true`
without them, and the captures + flag are only written after
the Tab focus assertions have actually passed. The exact native
fixture acceptance step still belongs to the Windows
manager-driven gate.

The earlier `tests/sdk_platform_effects_tests.hpp` /
`tests/sdk_platform_effects_tests.cpp` headless adapter test
target and the `pump_with_effects(...)` /
`pump_internal(...)` test-only public seams were removed because
they could not reach `acquire_routing`'s `play.ready()`
predicate and would have passed by construction with both the
pre-fix and post-fix code paths; the CMake target was the only
entry under `forge_ui_diligent` and the freeze-clean-sdk
headless build could not build it. The fixture is the real
regression.

The hidden-tab revocation block in `src/editor/main.cpp` was
corrected to route the SDK profile through the centralized
checked helper (`submit_external_release_observation`) so the
runtime's release-guard epoch sees the visible→hidden
transition. The legacy profile still falls back to the existing
`release(play)` void path because it has no observation to
ship. The GameInput / PlaySession ownership boundaries are
unchanged; only the per-frame SDK vs legacy selection in the
moved block was audited.

The native SDK toolbar Play icon is now idempotent at the arming
layer; each stage makes one click attempt and waits for activation
instead of re-queuing DOWN before UP every frame. The pre-jump stable-Y
gate uses runtime tick time (`play.timing().tick` × `fixed_dt`) instead
of walltime so a paused or stalled runtime cannot advance the proof.
A small `StableYState`/`stable_y_settled()` helper is shared by the
post-Resume, post-Continue, and post-restart-Continue pre-jump sites
with a fresh baseline per session.

The fixture's `workflow.json` is written exactly once per run when
the workflow settles (`done` or `failed`) or an uncaught
`frame()` exception is observed. The full trace is preserved and
`failure` / `frame_exception` are added as fields rather than
replacing the trace object. Caught exceptions queue an actual
capture under the current backbuffer (the previous swap-chain
pointer capture across frames was replaced with a bounded queue of
capture requests) and set `result=1`.

The shipped `FORGE-Windows-x64` ZIP does NOT contain the editor
fixture executable. The fixture is shipped as a separate artifact
(`FORGE-Editor-SDK-Fixture`, the EXE only) and the acceptance helper
copies it next to the extracted `forge_editor.exe` in a private
scratch so a missing packaged dependency fails verification rather
than being masked by a parallel DLL bundle. The dedicated ordinary
editable reference project is generated separately by
`tests/reference_project_fixture.py` after the reference delivery
test removes its authored project, then never reused on a cache
restore. Failure of the new acceptance step gates the
`FORGE-Windows-x64` upload.

The acceptance helper extracts the final ZIP into a private scratch,
runs the fixture against the matching extracted `NativeSdk/`, retains
every native screenshot (`editor-<label>.ppm`), per-stage trace JSON,
and the final `workflow.json` under the evidence directory on every
outcome, and clears extracted package, copied project, and private
user data on success or failure. Native visuals are reviewed
separately; `visual_review` stays pending in machine output.

## EditorSdkWorkflow fixture observer access

`EditorSdkWorkflow::set_game_input_observer` was previously declared in
the class's default-private scope, so the
`FORGE_UI_FIXTURE`-guarded bind at `src/editor/main.cpp` failed MSVC
compilation with `C2248: cannot access private member` for the Windows
fixture build. The setter is now installed in the public section next to
the analogous `set_capture` / `set_failure_capture` fixture
configuration setters, with the same single-line `std::move(fn)` body.
The underlying `game_input_captured_observer_` member and the
internal `game_input_captured()` read accessor remain private; the
public setter writes the private observer and the read accessor is
still only invoked from within the class's own phase blocks. No new
input authority or protocol field is introduced — `main.cpp` continues
to wire `[&] { return game_input.captured(); }` only under
`FORGE_UI_FIXTURE` and only after the captured `game_input` is in
scope.

## Editor fixture startup breadcrumbs + crash capture

`tests/editor_sdk_package_test.py` observed the SDK acceptance fixture
exit `3221225477` (`0xC0000005`, Windows access violation) at startup
with only the Diligent graphics init messages and no stack trace
(`/work/logs/windows-package-36164350868.log:312`,
`/work/logs/windows78-sdk-evidence/fixture.log`). Two test-only
diagnostics are added to `src/editor/main.cpp` under
`FORGE_UI_FIXTURE` so the shipped editor is unaffected. The unhandled-
exception filter is one of several diagnostic mechanisms; the retained
breadcrumbs, the package test's exit-code assertion, the editor
regressions, and the suite log remain the primary signals, and the
unhandled filter does not bypass any acceptance assertion.

- `forge::test::fixture_breadcrumb(name)` prints
  `FORGE fixture breadcrumb <name> t=<SDL_GetTicks()>` to stdout,
  flushes, and records the name in a fixed-size `char[64]` so the
  most recent breadcrumb survives a hard crash. Breadcrumbs are
  emitted at the major boundaries only: `main-start`,
  `after-sdl-init`, `after-sdl-window`, `after-fixture-construct`,
  `after-input-workflow-construct`, `after-sdk-workflow-construct`,
  `after-engine-factory-load`, `after-swap-chain`,
  `after-imgui-create`, `after-preferences-load`,
  `after-scene-engine-input`, `after-game-input-observer-bind`,
  `before-files-start`, `after-files-start`, `before-main-loop`,
  `main-loop-first-entry` (logged once on first entry only — avoids
  per-frame synchronous stdout and log bloat).
- `forge::test::fixture_crash_filter(EXCEPTION_POINTERS*)` is
  registered via `SetUnhandledExceptionFilter` before `SDL_Init` and
  writes a single line to `<argv[1]>/fixture-crash.txt` via plain
  Win32 file I/O (`WriteFile` on a pre-opened handle): `ExceptionCode`,
  `FaultAddress`, owning `Module` + `Base`, relative `Offset`, and
  `LastBreadcrumb`. Module lookup uses
  `GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, ...)` +
  `GetModuleFileNameA` from kernel32. No `dbghelp.lib` dependency,
  no `SymInitialize` / `SymFromAddr` / `SymGetLineFromAddr64`, no
  `MiniDumpWriteDump`, no `CaptureStackBackTrace` (it would only see
  the FILTER's stack, not `ep->ContextRecord`'s fault stack), and no
  `std::filesystem` / `create_directories` / wstring conversion /
  heap allocation inside the filter — the path is pre-resolved into a
  fixed `wchar_t[MAX_PATH]` buffer and the file handle is pre-opened
  at normal startup so the filter does only `GetModuleHandleExW` +
  `GetModuleFileNameA` + `snprintf` + `WriteFile` + return. The
  filter returns `EXCEPTION_EXECUTE_HANDLER` so the CRT still
  terminates the process with the original status; existing
  acceptance assertions, required native flow, and the
  `focus_gated_routing=true` schema check are unchanged. Capture is
  best-effort first evidence, not guaranteed recovery; if the
  process is too corrupted to write the file the CRT still aborts.

`tests/editor_sdk_package_test.py` now surfaces
`<output>/fixture-crash.txt` (when present) and the existing
fixture stdout/stderr log path in the `non-zero exit code`
assertion message so the crash record is visible without a re-run.
The diagnostics are intentionally minimal, never read or send
credentials, and never swallow an exception or bypass an assertion;
they are only the narrow breadcrumbs + unhandled-exception filter
above.

### Build / delivery status correction

The acceptance failure here was on fullrun `36164350868` for
**reserved** build `260925-000078` at source `aedf372`. All 4 core
jobs, format, editor regressions, shared game build, and the fresh
relocation step PASS, but the **final PACKAGE** failed. The latest
**delivered/accepted** build is `260924-000076`. Build `260925-000077`
also failed Windows editor compile (`main.cpp:354` `C2248` private
member), addressed by the v5 access-fix patch that landed as `aedf372`.
The v6 diagnostics above change compiled source again, so the next
**new build number** must be `260925-000079` or higher against the
new source — there is **no** same-identity `package_source_run` path
for the v6 binary; the manager follows the real release coordinator
and numbers it accordingly. The fixture binary itself is unchanged
in semantics; only the diagnostic instrumentation is added.
