# Game configuration, persistence and session ownership

Phase8 is in progress. These are implemented internal runtime building blocks,
not a claim that a graphical standalone exporter or complete game is delivered.
The existing editor Play worker now uses the shared `RuntimeWorld` composition.

## Configuration ownership

Project version2 optionally contains `game`, validated by
`validate_game_settings`. Older projects remain valid without it. The block is
version1, with a stable lowercase reverse-domain `application_id`, display title,
development/shipping profile, game-owned save schema, display defaults, audio
master volume and mouse sensitivity. `default_game_settings` constructs defaults.
The application key is a user-data namespace, not an AssetId or DocumentId.
Renaming the display title must not change it.

Startup scene identity remains the existing project `startup_scene.asset`; the
game block does not duplicate that authority. Export must eventually carry that
AssetId into its startup manifest. Physical display/device support is resolved by
the host, not promised by validating stored width/height or display preferences.
Dimensions1..32768 and display ordinals0..255 are admission bounds, not graphics
device capability claims. The host must query actual capabilities.

`resolve_game_settings` admits only the supported user display/audio/input fields.
It produces an effective copy and never changes shared defaults. Binding overrides
use existing ActionIds and the ordinary InputMap validator; action names/kinds and
IDs remain project-owned. Missing actions and unsupported controls reject the
candidate. Sensitivity is a preference value; applying it to mouse input belongs
to the forthcoming platform adapter. Validation does not apply window/audio state.

## User data and save slots

`GameStorage` requires an absolute OS user-data base supplied by the host. It uses
`game-APPLICATION_ID` beneath that base, separate from source/installation/cache
paths. The OS directory adapter is not part of this portable library yet. Never
pass the game installation or project folder as the production base.

One cooperative writer lease protects the application's user directory. It reuses
the existing OS-handle lease; no PID guessing or stale-lock-file deletion. Operations
require the owning thread. Marker files may remain after exit; the OS handle, not
their existence, determines ownership. Noncooperating external writers are outside
this guarantee. Redirected owned paths are rejected, not followed.

Save slots use bounded lowercase ASCII keys (avoiding Windows case aliases) and distinct `slot-KEY.json` files. The storage
envelope version1 is independent of the game's payload schema, scene format,
native SDK fingerprint and build number. It carries application identity, kind,
SHA256 of canonical payload bytes, scene AssetId and explicitly admitted game data.
Checksums detect accidental corruption; they are not authentication.

Every save/load needs a `GameSaveSchema` validator supplied by the game. It owns
supported fields, durable-reference resolution and content compatibility. The
engine does not serialize arbitrary ECS state, pointers, live leases, physics
recorder bytes or native globals. A future game save adapter must identify its
supported data rather than reusing an editor scene or recovery snapshot.

Migrations are explicit N→N+1 pure value transforms. Missing migration steps,
newer versions, invalid results and invalid current data reject. Loading/migrating
never rewrites the source file or changes a live world. Only an explicit successful
save writes the upgraded value. Activating that value remains a separate candidate
world operation; storage does not claim world/file transaction atomicity.

Writes reuse the existing unique-sibling staging, file flush and atomic replacement
implementation. POSIX additionally flushes the parent directory. Readers are closed
before replacement. Pre-publication failures preserve the old file. A directory
flush failure after rename can mean the replacement is already visible but durable
completion is uncertain; it cannot honestly be reported as rolled back. Interrupted
staging files are never enumerated as save slots. Hardware/filesystem power-loss
guarantees are not stronger than their underlying flush/rename semantics.

Admission bounds:8MiB save envelope,1MiB settings envelope,64 JSON nesting levels,
262144 JSON values,1024 migration steps and4096 directory inventory entries.
Settings carry the same application/kind/integrity envelope and require a validator;
an absent settings file yields validated empty overrides. Corrupt settings are
reported rather than silently overwriting them with defaults.

## Runtime session

`RuntimeWorld` owns EngineContext → Scene → RuntimeSimulation in construction order;
destruction reverses it. Module code outlives all worlds. Editor Play preserves its
existing process protocol, checkpoint format and native-module replacement policy.

`GameSession` owns one active RuntimeWorld, at most one unpublished candidate, an
existing RuntimeClock and copied module/configuration values. Prepare restores a
scene snapshot, optionally invokes a game-owned supported-state restoration callback,
then validates physics with candidate audio paused. The callback receives only the
unpublished Scene; a rejected restore cannot mutate the active world through that
reference. Failed preparation
leaves the active world, input and clock unchanged. Starting another preparation
retires the previous candidate. Generation tickets reject stale activation/cancel.

Preparation is currently **synchronous structural/physics preparation**. It is not
a declaration that all CPU/GPU/audio/UI assets are loaded or that preloading and
progress UX are complete. The visual host must establish required resource readiness
before activation; that integration remains open.

Activation completes throwable pose preparation before replacing the old world,
stops the old world before new gameplay ticks, and resets presentation/input debt.
The session tick remains monotonic across transitions. Optional paused activation,
Pause/Resume/Step and unload use the existing fixed clock. A failed fixed tick faults
the session; resuming that partially executed world is rejected. A validated new
candidate can replace it. Reentrant lifecycle and foreign-thread calls reject.

Native module startup happens during candidate world construction and can have
external side effects. Those are not rolled back. A post-commit activation failure
faults the new session; it does not resurrect a destroyed old world. Borrowed
`active()` references are invalid after replacement/unload and must never be stored
as persistent gameplay identity.

## Remaining Phase8 integration

Graphical standalone host, full runtime asset packaging, async resource-ready
scene transitions/progress, game SDK save/session access, OS data-directory adapter,
character/collision assets, input contexts/platform settings, independent diagnostics
and the reference-game acceptance flow are still required. Additive scenes/streaming
and advanced authoring tools are deferred. No new dependency pin, scene format,
persistent identity, ABI1 or public binary plugin API is introduced here.

See [runtime ownership](runtime-foundation.md), [input](input.md),
[content packaging](runtime-content-packaging.md), and [physics](physics.md).
