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
worlds do not supply a game host. Editor Play retains its separate capture controls;
its worker does not currently supply GameSession/storage requests or the standalone
control-frame callback loop. The reference game's complete menu/save lifecycle is
a standalone consumer.

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
