# Exact-SDK game control services

The experimental exact-version SDK exposes `FORGE_SDK_GAME` through the existing
world/module capability boundary. Legacy gameplay ABI1 is unchanged. Exact-SDK
modules must rebuild against this SDK: descriptor/host sizes and the fingerprint
include the new callbacks. No `GameSession*`, `GameStorage*`, SDL window, graphics
context or filesystem owner crosses the boundary.

## Execution and ownership

`controls` runs once per host control frame, including while simulation is paused.
It reads `read_control`; fixed systems read `read_action`. Its sequence is not
simulation time. `scene_ready` runs after scene restoration and before physics and
visual admission, followed by optional explicit `restore` state. Neither candidate
callback receives input or may request active-session mutations.

`game_request` accepts a bounded copied JSON object during a fixed/control callback.
It returns a module/world-owned token. The host drains requests after callbacks
return; requests added during a drain wait for another boundary. Publication retires
the old world's outstanding requests. Failure preparation keeps the current world.
A post-publication device/module fault is a fault, not a rollback claim.

`game_query` and `game_inspect` copy JSON. A sizing call returns required bytes,
including the terminator; an undersized buffer is not partially written. Query state
includes session/clock/preparation, current scene, resolved settings, input device,
cursor state, rebind state and diagnostic. Inspection returns `state` (`queued`,
`succeeded`, `failed`), `value`, and `error`. Release completed tokens explicitly.
Foreign threads, stale tokens and another module's tokens are rejected.

Limits are 1 MiB per request/result, 128 retained requests per endpoint/module SDK
bridge, and 256 pending requests per host. These are admission bounds, not throughput
or multiplayer guarantees.

## Polled host dispatch (`GameControlQueue::poll`)

`GameControlQueue` exposes both the synchronous `drain(Execute)` and a polling
`poll(Poll)` variant. `poll()` is the host boundary for gameplay operations
that may take more than one frame to complete on a non-engine side. A single
shared dispatch loop processes pending entries; each entry runs at most once
per host call, and work enqueued during a poll waits the next call. Pending
entries are polled again on subsequent calls until they complete, fail, or
are dropped by world replacement, module revocation, `release()`, or
endpoint retirement. Once an entry has completed or failed, its callback is
not invoked again.

The polling callback receives the request token plus the same command,
schema and module arguments used by the synchronous executor. It returns:

- `std::nullopt` to keep the entry pending. The queue requeues the entry
  behind any newly queued work; its receipt remains queued.
- `std::optional<Json>` populated with any value to complete the receipt
  in the same poll. An explicit JSON `null` is a successful null result
  distinct from `std::nullopt`. The serialized size is bounded to 1 MiB.
- A thrown `std::exception` to fail the receipt in the same poll with the
  diagnostic message (truncated to 8 KiB). Any other exception becomes a
  failed receipt with the generic diagnostic
  `Game operation callback failed`.

Serialization runs inside the same try block as the callback, so a value
that throws on `dump()` (for example malformed UTF-8) is converted to a
failed receipt instead of escaping the host pump and losing the receipt.
`status.value` on a failed receipt is itself safely serializable so the
host can dump it for diagnostics.

The request token is globally monotonic within the owner and identifies
the entry across polls. The queue uses the token to skip stale work after
world replacement, module revocation, `release()`, endpoint retirement,
or completion. The same pending token may be observed again on a later
poll; only completed, failed, retired, revoked, or released tokens are
not re-invoked.

The 256-entry pending bound and the 128 retained-request bound are
admission limits enforced before a request is enqueued. The in-flight
slot reserves one entry's worth of capacity during the callback so that
a callback can attempt to enqueue new work without violating the bound;
a full queue still rejects. The reserved slot ensures that a
still-valid pending entry can be requeued without exceeding the bound.

## Optional polled platform adapters

`GamePlatformControls` gains narrow optional `cursor_poll` and
`navigation_poll` adapters. `cursor_poll` is used for the `cursor`
operation and also gates the required release for lifecycle-mutating
operations (`pause`, `quit`, `unload`, `prepare` — including
`activate:false` — and `load` with `activate:true`). `navigation_poll`
handles `ui_navigation`. Each adapter receives the request token and
the desired payload (`bool` capture, or the direction string) and
returns:

- `true` — the physical effect has actually completed and the host can
  commit the change (`cursor_captured()` set on the host for capture).
- `false` — the effect is still in flight on the embedding side. The
  receipt stays queued and the host does not commit the change.
- throw — the effect failed or is stale. The receipt is failed with the
  exception's diagnostic; `cursor_captured()` is not changed.

The adapter owns idempotency: a physical effect is dispatched at most
once per token. When the polling callback returns `false`, the host
continues to invoke the adapter with the same token until it returns
`true` or throws. `false` is reserved for legitimately pending effects;
cancellation is signalled by throwing, not by returning `false`.

A polled cursor adapter MUST be accompanied by a synchronous
`platform.cursor` callback. The synchronous path is used by
`release_cursor()` to force a physical release on `pause`, `unload` and
`prepare`. Configuring only `navigation_poll` does not require a
synchronous `cursor` callback. The host constructor validates this and
throws `std::invalid_argument` if `cursor_poll` is supplied without a
synchronous `cursor`.

When `cursor_poll` is configured, lifecycle-mutating operations
(`pause`, `quit`, `unload`, `prepare` — including `activate=false` —
and `load` with `activate=true`) stay queued until
`cursor_poll(token, false)` confirms the release. The host re-dispatches
the polled release once per pump while the request is pending, even
when the host's local `cursor_captured()` flag is `false`, because a
prior capture may still be in flight in the adapter. No requested
lifecycle mutation, scene preparation, restore, schema validation or
migration runs while the release poll is pending, and an adapter that
throws on the release poll fails the request before any of those
effects occur. Command fields and types are validated by the host
before any release poll is issued. The synchronous `platform.cursor(false)`
call now confirms an editor-acknowledged release; it never flips the
captured state itself and requires no outstanding release obligations.
Existing rollback rules on a synchronous failure continue to apply.
The `SdkPlayRuntime` host is the active user of `cursor_poll` and
`navigation_poll`; other host embeddings can opt in independently.
The read-only `load` (`activate=false`) does not request a release and
returns the validated, migrated saved data without polling; each
required migration step and the schema validation execute once for
that load, not repeatedly while awaiting release. A multi-version
migration can have several steps. Standalone synchronous adapters
remain supported and the constructor check for a missing synchronous
`cursor` is unchanged.

The synchronous `platform.cursor` callback also performs the host's
forced-release path. Calling it from `release_cursor()` invalidates any
pending polled cursor capture: the embedding adapter MUST throw on the
next poll for that token so the host does not falsely claim a delayed
backend ack is canceled. Dropping a queue receipt (via `release()`,
module revoke, or world replacement) does not cancel a delayed physical
effect; the adapter continues to own the lifecycle of any work it has
dispatched.

Existing synchronous `platform.cursor`, `platform.navigation`,
`platform.settings` and `platform.input_device` callbacks remain
supported. `GameHostControls` runs without polled adapters exactly as
before, and `poll(Poll)` and `drain(Execute)` continue to coexist on the
same queue.

## Editor Play integration status

Editor Play uses the separate-process shared `SdkPlayRuntime` defined
in `src/sdk_play_runtime.{hpp,cpp}`. The editor owns the staged
presentation, the native Rml focus tree, and the SDL/window surface;
the runtime owns `GameSession` / `GameStorage` / world mutation,
candidate tickets, snapshot publication, and its own readiness poll.
There is no shared world, queue, storage, renderer, or `GameSession*`
across the boundary. The editor sends bounded checked outcomes
(`submit_editor_observation`, `submit_sdk_candidate_ack`,
`submit_sdk_cancel_loading`, `submit_sdk_editor_epoch`, plus
activation / root release) and reads back published snapshots through
the IPC transport; it never owns the candidate or the active world.

The transport envelope carries `protocol`, `session`, `id`,
`runtime_contract`, `loading`, `platform_effects`, `release_required`,
`module`, `recovery`, `timing`, `activation`, plus the world body and
an `ok` / `error` pair. It is not the module SDK `game_request` /
`game_query` surface; the runtime-side adapter collects those into the
envelope and the editor reads the resulting JSON.

Readiness/UI-effect transport follows the existing adapter model:
`SdkPlayRuntime::configure_platform()` wires `cursor_poll` and
`navigation_poll` to the editor's `SdkPlatformEffects` adapter in
`src/editor/sdk_platform_effects.hpp`. The editor dispatches exactly
one physical setter per effect, and `RuntimeUiInput` in
`src/editor/runtime_ui_input.hpp` only routes native menu keys while
`game.captured()` is true. The initial `cursor=false` →
`capture_checked(relative_mode=false)` menu-routing acquisition is
gated on `game_panel_visible && game_focused_ && play.ready() &&
!game.captured()`; `game_panel_visible` follows the existing
`main.cpp` `game_visible && input_allowed()` predicate (visible
Game, SDL window input focus, no text input, no popup/file busy),
and `game_focused_` is sampled each frame from
`ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)` immediately
after the Game window's `Begin()` returns visible. A hidden release
acquires no routing and is rejected with
`play.platform_effects.cursor.hidden`; physical capture additionally
requires `play.ready()` and a visible panel. Outside-panel surrender
(a visible Game alongside a focused Scene / Console / Hierarchy
panel) keeps `game_focused_ = false`, so the routing flag stays off
until the user re-focuses the Game panel; OS focus return alone does
not re-arm it. Synchronous `platform.cursor(false)` is a confirmation
that the editor has acknowledged the release — it never flips the
captured state itself, and outstanding obligations from a superseded
capture must be cleared by the editor first. Lifecycle-mutating
operations still gate on that synchronous confirmation; the polled
adapter is the active transport for the SDK host.

`PlaySceneAdmission` is the private runtime-half channel inside
`SdkPlayRuntime`. It reuses the existing `GameSession` adapter slot
and `RuntimeWorld::prepare_scene_resources()`; no second world,
queue, storage, or renderer is introduced. It is not a public SDK
type, not part of ABI1, and does not change scene identity,
dependency pins, or formats.

The channel is scoped to one session string (1..128 bytes); its
constructor captures only the calling thread as the owner. Pending
candidate ownership stays on `GameSession`'s adapter slot; the channel
retains only the session string, the last accepted factory ticket
(used to validate that supplied tickets are strictly increasing), and
at most one pending record.

The adapter's `poll` calls `RuntimeWorld::prepare_scene_resources()`
until it reports ready; subsequent adapter polls reuse the captured
snapshot. That helper extracts the runtime-only readiness shared with
the standalone host (animation initial pose, evaluate transforms,
physics sync0, presentation reset, audio sync, audio failed/unavailable
checks, and enabled non-prefab `NavigationAgent` validation) and does
not tick, run control callbacks, publish worlds, or touch GPU.

### Safe traversal of readiness rejections

`RuntimeWorld::prepare_scene_resources()` runs its audio and
navigation checks inside `flecs::world::each` callbacks. The pinned
Flecs 4.1.6 iterator acquires its table lock for the duration of the
callback; throwing from inside the callback leaves the lock held and
aborts the world teardown under sanitizers. The helper therefore
observes the failure inside the iteration (a `bool` flag for audio, an
`std::exception_ptr` for navigation) and throws after the traversal
returns, preserving the exact first-failure reason without unwinding
through a locked Flecs table. Rejection semantics are unchanged. After
runtime readiness the adapter publishes a copied immutable envelope
`{session, ticket, scene, ui}` (UI is `null` when the runtime UI service
is not installed) bounded to 8 MiB serialized bytes. The candidate stays
pending until an explicit accepted acknowledgement arrives for the
exact `(session, ticket)`. Rejected candidates retain the active world
and its clock; the next adapter poll throws so `GameSession` discards
only the candidate. Cancellation discards the candidate directly
through `GameSession` and removes the channel's pending record. The
channel does not claim, validate, or otherwise certify the editor peer
— it trusts the explicit ack.

Other limits are diagnostic text ≤ 8192 bytes, session ≤ 128 bytes,
strictly increasing tickets, owner-thread enforcement on every public
method and adapter poll, and `has_pending` reporting only an
uncommitted record (which includes a record after ack but before
activation). Rejected records remain in place until the next poll or
supersession removes them; the channel does not perform any actual
remote or GPU readiness validation.

The exact-version SDK still rejects partial live DLL reload and any
"exact SDK recovery" path that would re-attach the module after an
admission failure; legacy gameplay ABI1 modules remain a separate
target with their own host.

## Host operations

| Operation | Additional fields | Result / effect |
| --- | --- | --- |
| `pause`, `resume` | none | Existing session clock; pause releases capture |
| `prepare` | `asset`, optional `state`, `activate` (true), `run` (true) | Candidate ticket; optional state requires this module's save schema |
| `activate` | `ticket`, optional `run` | Publish an admitted candidate |
| `cancel` | `ticket` | Cancel the matching candidate |
| `unload`, `quit` | none | Retire the world / request host shutdown |
| `save` | `slot`, `scene`, `data` | Validate explicit game state and atomically save |
| `load` | `slot`, optional `activate`, `run` | Validate/migrate, prepare and restore; `activate:false` returns copied save data |
| `slots` | none | Names and validation metadata; invalid slots remain present |
| `erase` | `slot` | Delete this named game slot |
| `settings` | none | Copied resolved settings |
| `set_settings` | `values` grouped by display/audio/input | Validate, apply and persist user overrides; rollback on failure |
| `input_contexts` | `active` array | Replace active context set and neutralize old input |
| `cursor` | `capture` | Request host capture/release; capture requires an adapter and focus |
| `ui_navigation` | `direction`: next/previous/accept | Native RmlUi focus traversal/activation where supplied |
| `rebind_begin` | `action`, `index` | Listen for a candidate for one binding slot |
| `rebind_cancel` | none | Discard pending candidate |
| `rebind_commit` | optional `clear`, `allow_conflicts` | Validate and persist the chosen slot; sharing must be deliberate |
| `rebind_reset` | optional `action` | Restore project defaults and cancel the pending candidate |

The graphical standalone host installs these services. Provider/cursor/navigation
availability must be queried or handled as a rejected request; schema inspection
worlds do not supply a game host. The standalone reference game exercises the
complete menu/save lifecycle through its own `controls()` callback. Editor Play
runs alongside the editor using its own `SdkPlayRuntime` transport described above;
the runtime process invokes the project module's `controls()` callback through
the existing `RuntimeWorld` / `GameSession` owners. No separate queue, storage,
or session service is introduced for the editor side. The editor owns the staged
presentation, native input, and the Rml focus tree; it never owns the candidate,
the active world, or a `GameSession*` / `GameStorage*`.

A failing `platform.cursor(false)` during `prepare` (and `load` with
`activate:true`, which calls `prepare`) cancels the still-pending candidate from
that request, leaves the active world unchanged, and preserves the save file
bytes. The original exception is rethrown so the request receipt stays inspectable
in the original endpoint.

## Save schema lifetime

Register `ForgeSdkSaveSchemaV1` during module start. Validation receives `{scene,data}`;
only game-declared state is admitted. Migrations are pure N → N+1 steps with a
bounded output buffer. FORGE copies the migration-version list and function pointers,
and retains their native code lease. Callback userdata belongs to the module and
must remain valid until callbacks return. No asynchronous callback execution is
implied.

Module stop revokes callbacks and queued work before code retirement. A synchronous
operation holds its schema/code registration until it finishes, even if retirement
occurs at the surrounding host boundary. Scene/session identity is not serialized
as a save handle. Corrupt, newer or failed migration inputs preserve existing files.

## Runtime UI values

Existing semantic UI actions remain argument-free. `ui_allow_value_action` opts
one action into a single bounded string; reserved Pause/Resume/Step remain
argument-free. `ui_publish_json` copies protocol-supported values. `ui_poll_event`
returns copied `{entity,command,value}` and retains an event across sizing calls
until a sufficient output buffer is supplied.

The reference game is a consumer in `samples/reference_game`. FPS movement, camera
angles, interaction selection, menus and its save schema are project policy, not
engine-global behavior.
