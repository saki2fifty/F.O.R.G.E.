# 2026-09-26

## Phase 8 SDK acceptance fixture audio opt-in

The editor SDK acceptance fixture on Windows runners previously launched the
runtime with `--audio device`. The runner has no WASAPI endpoint, so
`AudioOutput::Device` failed `ma_context_init`; the audio module's tolerated
catch (`src/audio.cpp:419-441`) then aborted scene preparation at the
non-prefab `AudioSource` carried by the reference level scene.

The fixture now opts the runtime into the existing `AudioOutput::Offline`
selection only under the build-83 fixture branch. Concretely:

- `PlaySession::set_headless_audio(bool)` / `headless_audio()` is a new
  opt-in distinct from the existing `probe_` flag (`probe_` ties
  `--ui on/off` to its offline-audio choice and is reserved for
  transport-only tests; `headless_audio_` selects offline audio alone and
  leaves the UI capability intact).
- `PlaySession::launch()` selects `--audio offline` when
  `probe_ || headless_audio_`, otherwise `--audio device`. UI gating
  remains `!probe_` so existing probe-mode tests are unchanged.
- Under `#ifdef FORGE_UI_FIXTURE`, the SDK play launch path calls
  `play.set_headless_audio(true)` inside the existing
  `if (fixture.sdk_play)` branch alongside the existing user-data
  override. Production callers never reach this branch.

### Behaviour

- UI capability is preserved — the SDK play fixture continues to drive
  the full menu → new-game → play → save → quit → restart flow.
- Production / physical-audio callers leave `headless_audio` false and
  the runtime receives `--audio device` as before.
- Required-source validation is unchanged: a non-prefab `AudioSource`
  pointing at a clip the catalog cannot resolve still records
  `failed_sources == 1` and `prepare_scene_resources` throws
  `Scene preparation: required audio sources failed`
  (`src/runtime_world.cpp:48`).
- The "audio was never requested" path is unchanged: a non-prefab
  `AudioSource` without an audio module still throws
  `Scene preparation: audio output is unavailable`
  (`src/runtime_world.cpp:61`).

### Coverage

- `tests/audio_tests.cpp` extends the existing `audio` ctest with three
  focused paths on the `RuntimeWorld::prepare_scene_resources` contract:
  (a) Offline + working source → success, (b) Offline + dangling clip
  → required-source throw with `failed_sources == 1`, (c) no audio +
  non-prefab `AudioSource` → unavailable throw. Path (c) pins the
  contract the rejected `audio_attempted` bypass would have suppressed.
- `tests/editor_sdk_tests.cpp` adds a four-line accessor round-trip
  (`verify_headless_audio_opt_in()`) at the start of `main()`. It pins
  only the public surface; it does not invoke `SDL_CreateProcess` and
  does not exercise the active-session guard.

The Windows CI build-83 SDK workflow fixture rerun is the end-to-end
acceptance; no execution or claim is made in this changelog entry
itself.

### Hardware scope

The fixture's offline-audio routing is a runner-side accommodation for
the missing WASAPI endpoint; it is **not** a physical-audio acceptance
signal. The acceptance gate reports `hardware = "WARP rasterizer on
Windows CI; physical GPU acceptance requires manual local execution"`
and the audio-path equivalent: the fixture's audio is offline, not
physical-audio proof.

## Phase 8 UI acceptance fixture selection

### Bug

The previous `tests/editor_sdk_workflow.hpp` fixture hard-coded
`reference::ui_id` as the UI document selector. The level scene's
Game UI entity carries that id; the menu scene's Main menu UI entity
is authored with a per-regeneration UUID. Filtering snapshots by
`reference::ui_id` therefore read the menu scene's published model
as empty and the SDK acceptance fixture timed out at the first
stage.

### Fixture fix

`ui_model_from_snapshot` now selects the active reference UI
document by the snapshot's own descriptor fields using a "single
visible document" rule:

* 0 visible documents -> empty model (snapshot not ready, e.g. mid
  scene-swap or before the first publish).
* exactly 1 visible document -> that document's model (the contract
  for the authored reference fixture, where each scene carries
  exactly one UiDocument).
* 2+ visible documents -> empty model (ambiguous; a future scene
  that legitimately carries more than one visible reference
  document is surfaced explicitly instead of silently narrowed).

No authored-asset id is rewritten. Runtime snapshot selection, the
authored reference project, and `tests/reference_project_fixture.cpp`
are unchanged.

### Regression coverage

`tests/editor_sdk_workflow_probe_tests.cpp` exercises the same code
path through the static `ui_model_from_snapshot` seam with
synthesized snapshots:

* menu scene snapshot (arbitrary entity id) -> page="main";
* level scene snapshot (canonical `reference::ui_id`) ->
  page="play" with non-zero interactions and a real prompt;
* absent / malformed snapshot -> empty model;
* only-hidden document -> empty model (no silent fallback);
* unrelated visible document alongside the reference one -> empty
  model (ambiguity, no first-match narrowing);
* unrelated hidden document alongside a single visible reference
  document -> the reference document is read.

The binary also covers the existing Windows79 Rml safety probe
(absent/false `alive_probe` returns `nullptr` without touching
RmlUi). CTest target: `native_sdk_workflow_probe`.

### Other

`forge::test::EditorSdkWorkflow::ui_model_from_snapshot` is exposed
as a narrow public seam so the probe binary can call it. The
surrounding helpers (`ui_model`, `model_page`, `model_interactions`,
`model_message`, `model_binding`, `player_position`, `scene_asset`)
remain private.

## Phase 8 Windows81 probe fixture construction

### Bug

`native_sdk_workflow_probe` compiled and ran 124/125 editor tests
green on Windows81 (run36204593880). The only failing assertion
was the synthetic ambiguity case (`tests/editor_sdk_workflow_probe_tests.cpp`
case (8), message: "ambiguous documents: ui_model_from_snapshot
must return empty object, not first-match an unrelated model").

The implementation of `ui_model_from_snapshot` was correct: with
two visible documents it returns an empty object via the
`ambiguous` flag. The fixture itself was broken. Case (8) (and the
case (9) hidden-unrelated variant) inserted the unrelated document
with:

```cpp
snap["documents"].insert(snap["documents"].begin(),
                         {{"entity", "..."},
                          {"visible", true},
                          ...});
```

The double-brace initializer `{{"k","v"},{"k","v"},...}` resolves
to `nlohmann::json::array::insert(iterator, std::initializer_list<json>)`,
not `insert(iterator, json_object)`. The compiler treats each
`{"k","v"}` as a single json value; `{"k","v"}` is itself a
brace-init for a 2-element json *array*, so seven 2-element JSON
arrays were appended to `documents` rather than one JSON object.
`ui_model_from_snapshot`'s `if (!doc.is_object()) continue;` filter
silently skipped those arrays, the documents array still held only
the reference document, no ambiguity was exercised, and the
implementation correctly returned the reference document's
non-empty model. The assertion
`model.is_object() && model.empty()` therefore failed on
`model.empty()`. The case was not a real regression test; it was
a fixture-construction mistake that masked itself as an
implementation failure.

Case (9) was symmetric: its hidden-unrelated document was also
inserted as JSON arrays, the test "accidentally" still passed
because the function still saw exactly one visible document.

### Fix

Cases (8) and (9) now build the unrelated document as an explicit
`nlohmann::json` object first, then insert that single value:

```cpp
const nlohmann::json unrelated_visible = {
    {"entity", "00000000-0000-0000-0000-000000000000"},
    {"visible", true},
    {"model", {{"page", "editor-toolbar"}}},
    ...};
snap["documents"].insert(snap["documents"].begin(), unrelated_visible);
```

This routes through `insert(iterator, const json&)` and lands one
JSON object in `documents`.

Case (8) additionally asserts the fixture shape before exercising
`ui_model_from_snapshot`:

* the unrelated document is an object with `visible=true`;
* the documents array holds exactly two entries after insert;
* a visible object carrying the unrelated entity id is present
  in the array.

A future change that re-introduces the brace-init-as-initializer-
list trap fails one of these `require()` calls first, with a
message that points at the fixture, not at the implementation.

### What was not changed

* `tests/editor_sdk_workflow.hpp::ui_model_from_snapshot` — already
  correct.
* `tests/editor_sdk_workflow.hpp::find_live_visible_context` and the
  `presenter_alive_observer_` gate — pre-init safety preserved.
* `tests/editor_sdk_workflow.hpp::ui_model`, `model_page`,
  `model_interactions`, `model_message`, `model_binding`,
  `player_position`, `scene_asset` — remain private; only
  `ui_model_from_snapshot` is exposed as a test seam.
* Authored scene ids, the runtime, the SDK, the renderer, the
  RmlUi host, and the menu/level/editor authored fixtures are
  unchanged. No authored id was rewritten.

## Phase 8 Windows pipe-contract probe (manual)

### Scope

The Windows SDK acceptance fixture for build83 fails stage2 with a
runtime snapshot timeout (`request 19, outgoing 0, incoming 8192
bytes`). Manager review challenged the earlier claim that
`WriteFile` on a full nonblocking anonymous pipe returns
`ERROR_NO_DATA`, citing the named-pipe contract at
https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-type-read-and-wait-modes
where the empty-read error does not transfer to full-write. Without
an executable Windows reproduction the branch in
`src/runtime_io.hpp` that conflates `ERROR_NO_DATA` with
`ERROR_BROKEN_PIPE` could not be accepted or rejected.

This entry adds an API-contract probe only. It does NOT rebuild
FORGE, the engine, or any dependency; it does NOT reproduce the
fixture failure. It pins what `WriteFile` / `ReadFile` and
`forge::RuntimeIo::send` / `flush` actually return on a nonblocking
anonymous pipe so the manager can decide whether the existing
runtime_io.hpp branch is correct.

### Probe

`tests/windows_pipe_probe.cpp` is a single-source MSVC probe
(`/std:c++20 /EHsc`) that includes `../src/runtime_io.hpp`
unconditionally — a missing header fails the compile, never a
silent skip. Three scenarios run in one process:

1. `scenario_a_fill_then_drain` — write 1024-byte chunks until
   WriteFile cannot make progress, then drain with 8192-byte reads.
   Pure API-contract observation. Reports every WriteFile and every
   ReadFile (full / partial / zero / false-return with last_error),
   not only the last call.
2. `scenario_b_disconnected_reader` — write into a pipe whose read
   end is already closed. Pure API-contract observation. Reports
   the WriteFile return value and last_error so we know whether
   disconnect surfaces as `ERROR_BROKEN_PIPE` (current
   `closed_=true` contract) or some other code.
3. `scenario_c_runtimeio_send_flush` — real production-path
   exercise of `forge::RuntimeIo::send` / `flush`. Redirects the
   process's `STD_INPUT_HANDLE` / `STD_OUTPUT_HANDLE` to private
   pipe ends via `DuplicateHandle` + `SetStdHandle`, instantiates
   `forge::RuntimeIo`, builds `payload + "\n"` (mirrors
   `src/sdk_play_runtime.cpp:1321`), calls `send(expected)`, then
   alternates `flush()` with a `ReadFile` drain (in place of the
   editor's pump) in a bounded 64-round loop. Records
   `io.pending()` / `io.closed()` before and after each `flush()`
   call (`flush_steps[]`) and every `ReadFile` return value
   (`read_calls[]`). Compares the collected bytes byte-for-byte
   against the sent payload for data integrity. The process exits
   with status 2 (and still emits the JSON evidence) if any of:
     * `RuntimeIo` throws
     * `io.closed()` flips before the payload is fully drained
     * the collected bytes do not match the sent payload

   The originals are restored and the duplicates are closed whether
   or not the try block threw. The "real production path" label is
   restricted to scenario C; A and B stay as API observations so the
   manager still gets raw `WriteFile` / `ReadFile` evidence.

The probe is bounded (`kMaxCalls = 4096`, total at most ~4 MiB if
every WriteFile returns the full 1024 bytes; `kRounds = 64` for
scenario C) and uses the exact chunk sizes runtime_io.hpp and the
editor's `SDL_ReadIO` use, so the contract observed is the
contract runtime_io.hpp depends on.

### Workflow

`.github/workflows/windows-pipe-probe.yml` is a manual-only
`workflow_dispatch` job on `windows-2022`, `timeout-minutes: 5`.
Pinned actions match the existing editor-audit and standalone-audit
conventions:

- `ilammy/msvc-dev-cmd@a102174a2b586eec2ea151a69e6fd14404a8ce7c`
  (`vsversion: '2022'`, `toolset: '14.44.35207'`,
  `sdk: '10.0.26100.0'`)
- `actions/checkout@11d5960a326750d5838078e36cf38b85af677262` (via
  the safe short-path pwsh pattern from `cache-check.yml`)
- `actions/upload-artifact@ea165f8d65b6e75b540449e92b4886f43607fa02`
  (the existing pinned upload action)

The upload step uses the existing `tools/cache_workspace.cjs` preload
via `NODE_OPTIONS` — the same pattern cache-check / editor-audit use
for their cache actions — because the repository's default
`GITHUB_WORKSPACE` ends in a trailing dot and the upload path goes
through Windows GNU tar under the hood. No new shim is introduced.

Permissions are `contents: read`. The basic-auth header is masked
with `::add-mask::` before any git call so no secret reaches the
workflow log. The probe binary (`probe.exe`) and its JSON output
(`probe-output.json`) are uploaded as the
`FORGE-Windows-Pipe-Probe` artifact regardless of pass / fail.
Non-zero exit from the probe is reported as a failed workflow step
but the artifact still ships, preserving the JSON evidence.

### What was not changed

* `src/runtime_io.hpp` — restored to its shipped state; no
  speculative patch is applied.
* The FORGE engine, runtime, SDK, renderer, RmlUi host,
  authored assets, the editor SDK acceptance fixture, the
  Windows runner pipeline, and the editor-audit / standalone-audit
  / cache-check workflows are all unchanged. No authored id was
  rewritten. No dependency was added. No CMake configure / build
  / install step runs in this workflow.
