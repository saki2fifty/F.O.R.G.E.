# Project Code build and runtime contract

FORGE has one supported project gameplay model: exact-SDK C++ code registers Flecs
components and systems in a separate runtime process. Components carry data;
systems query matching entities at the registered lifecycle phase. A system is
not attached to one entity. `Native/` is project source shown under **Content → Code**. It is not an AssetId asset or a scene Hierarchy entry. The [user guide](../manual/editor/native-gameplay.md)
shows the editor workflow.

## Runtime Prefab instances

Exact-SDK C++ can request an admitted Prefab with
`sdk::Client::request_prefab(prefab_uuid, name, scene_uuid)`. It shares the existing
runtime entity Pending/Ready/Failed/Gone queue, cancellation and observation
lifetime. Publication precedes the next fixed tick; Ready returns the root's
borrowed world-local handle. Native gameplay may then set components through its
Flecs stage. Source spatial policies and inheritance remain intact, and internal
portable references resolve independently for every instance. Spawning writes no
authored file or editor history. Add code-selected assets to the saved Scene's
Runtime Dependencies so Play and export admit them. See the
[extension guide](extension-guide.md) and [ADR023](decisions/023-runtime-prefab-creation.md).

The new tail callback deliberately changes the exact SDK fingerprint/table size;
rebuild modules against the matching installed SDK. Native ABI1 remains unchanged.
Observation tokens and arbitrary C++ state are not checkpoints. Supported created
host-owned values use existing private recovery; exact-SDK Play recovery remains
unsupported. Runtime snapshots retain admitted unused definitions so a host
recovery can create the first instance later.

## Binary and ownership boundary

The exact SDK uses a versioned C-shaped entry descriptor, an exact fingerprint,
shared Flecs, descriptor size checks and module identity/dependency admission.
That compatibility mechanism does not make arbitrary native code safe. The editor
never loads project gameplay libraries. The isolated runtime validates and owns
registrations, callbacks and world-scoped services. Module contexts and code leases
outlive Flecs teardown; callbacks cannot run after their library retires. The
[engine module contract](engine-modules.md) and [extension contracts](extension-contracts.md)
describe the detailed ABI and lifecycle rules. No rich C++ in-place reload exists.
Code and schema iteration is **Stop → Save → Build → Play**.

Runtime protocol 2 carries correlated JSON-line requests. The runtime owns its
fixed clock; Step means one fixed tick. Editor FPS and transport polling never
supply simulation time. Source save and scene save have separate document history.

## Fixed ECS worker execution

Project setting `ecs_workers` is a strict JSON integer in 1..16, including the
calling owner stage. An absent value means 1; reading a legacy manifest does not
rewrite it. Project Settings and `project.settings.edit` share the settings draft
and validation owner. Save/restart applies the value to ordinary runtime hosts,
exact-SDK Play and standalone `GameSession` construction. It is not a live worker
resize control. `RuntimeExecutionConfig` exposes the same bounded value to C++.

`RuntimeSimulation` configures native stages after composing its fixed pipeline.
Only explicitly `multi_threaded()` systems distribute their declared-access
queries. Immediate systems, providers, Scene/world ownership and external
services retain their contracts. ECS-only callbacks must use iterator stages,
owned writable query terms and safe local/read-only data. They must not mutate
the root host world, call owner-thread services, share unsynchronized mutable
state or throw through native worker dispatch. A read-only query does not make
an arbitrary captured callback safe. Worker count and callback cost determine
whether parallel execution helps; no automatic workload policy is introduced.

Native workers join before runtime-owned systems/providers retire. World
replacement constructs and retires complete worlds using the configured count.
An explicit multi-stage request refuses an existing external thread/task-thread
configuration owner. The default C++ config leaves external native configuration
alone; callers must not reconfigure it while simulation is active. Existing
fixed-clock Pause/Step and owner merge boundaries remain authoritative.

The exact pin's per-system accumulated timing uses a shared non-atomic counter.
Parallel runtime suppresses that measurement, restores its previous flag on
retirement and reports `cpu_seconds:null`, `native_system_timing:false`, and
the actual `ecs_workers` in the ECS plan. `system_trace_scope` identifies supported
capture as `native_stages` or `owner_thread`; collection must still be enabled.
The default monotonic clock and runtime-owned native stages support detached
worker dispatch records, published through the owning service after progress.
Custom clocks, externally configured stages or unavailable bootstrap hooks retain
owner-only capture. Actual trace metadata lists observed stages in its retained
window. Native stage identifiers are not OS thread IDs or hardware utilization.
Whole-tick scope timing still includes synchronization. These limits also apply
through automation/MCP. See [CPU profiling](cpu-profiling.md) and
[known issues](flecs-known-issues.md).

Ordinary private recovery records nondefault worker counts and rejects a
checkpoint at a different execution configuration. Legacy sequential checkpoints
retain their default meaning. Exact-SDK Play checkpoint recovery remains
unsupported. Export retains the saved value; counts above 1 require runtime kit
`engine.runtime_ecs_workers:1` at export and distribution admission. This is a
runtime feature profile, not a new Scene/manifest version or a C ABI revision.
The installed execution header participates in the exact SDK fingerprint.

The independently authored `ParallelMotion` example declares const data plus an
owned writable translation, opts into native stages, and uses the iterator's
staged entity for deferred `modified`. Its zero-based `.term_at(1).src().self()` applies
to the translation source, excluding entities with only inherited translation
storage. The original source is exercised with private and inherited storage at
one and four native stages. It has no host-service dependency. Installed SDK
sample target `parallel_motion_gameplay` builds the same canonical Gallery source.

## Typed 2D physics

The existing Physics permission includes distinct XY movement/load/velocity,
state, ray/overlap and current-tick collision callbacks. `sdk::Client` wraps the
copied C boundary; no vendor physics headers or native handles are required.
The additive box/circle/capsule sweep returns a copied closest hit and supports
canonical self exclusion and explicit initial-overlap skipping. It is a read
against realized bodies, not a native character controller. The editable
[Movement2D example](../samples/projects/Movement2D/README.md) uses it for support
checks while a Dynamic body owns motion.
Positions use meters and angular values use radians. Commands share the existing
body FIFO; reads do not flush it. The exact header/table fingerprint changes, so
rebuild project modules with the matching installed SDK. Native ABI1 remains.
The separate `character_2d`, `move_character_2d`, `place_character_2d` and
`crouch_character_2d` Client
helpers control Character Controller 2D through the shared character FIFO. They
copy feet XY, locomotion velocity, support, stance and independent placement/shape
blockage; C++ supplies
gravity and jump policy. `Examples/Character2D` is the editable controller project.
Its player does not have an authored Physics Body 2D or Capsule Collider 2D.
See [2D physics](physics-2d.md#matching-c-sdk) for sizes, limits, units, retirement
and process/recovery scope. Authored crouch height uses existing Inspector/MCP
properties; runtime stance requests use Physics permission and the fixed callback
boundary.0 selects automatic half-height. Stop restarts standing; save/export
preserves authored settings. Native Windows acceptance is a separate gate.

Complete 3D/XY ray queries return the nearest hit per canonical object, ordered
along the segment. Use the matching `sdk::Client::raycast_all` or `raycast_all_2d`
helpers with copied vector values and bounded output arrays. The
[SDK ray contract](extension-contracts.md#complete-physics-rays) defines capacity,
filters, native geometric limits and output clearing. The editable
[PhysicsVolumes example](../manual/samples/physics-volumes.md) includes both scenes.

The matching SDK also offers eight `*_excluding` helpers for closest/all rays,
overlaps and linear sweeps in 3D/XY. They take a bounded span of canonical SDK
references, snapshot it before output writes and preserve ordinary access for an
empty list. [Canonical query exclusions](extension-contracts.md#canonical-query-exclusions)
explain the 16-entry limit, lifetimes, sizes and query-only behavior. PhysicsVolumes
contains project-owned marker examples; no engine collision configuration changes.

## Managed source and registration

**Set up C++ code** copies the matching installed SDK template into
an empty `Native/` directory. It does not overwrite existing source. The template
still supports `Native/gameplay.cpp`; further `.cpp` and `.hpp` files can be
created in FORGE. The optional `Native/forge.sources.cmake` registers managed
translation units. User CMake is never silently rewritten.

**Content → Create / Register → C++ → Component** creates a reflected, authorable data scaffold in
`Native/Components/`; **Create C++ System** creates a Flecs fixed-gameplay system
in `Native/Systems/`. Their registrations are generated through
`Native/forge.registration.hpp`, with source provenance for the Inspector's
**Open C++ Definition** action. Unmanaged or hand-written registrations do not
claim a source mapping. C++ source and headers are browsed in Content → Code. Build owns compilation and component inspection.
The ordinary Inspector's generic schema and property drawer author component
values, including prefab intent, scene persistence and Undo/Redo.

## Build and source identity

`forge_tools --assets code-identity PROJECT SDK_FINGERPRINT` exposes the existing
managed C++ source/build identity for packaging consumers. It reads bounded saved
Native inputs and returns `source_identity`; it does not compile or load code.
The packaged PhysicsLoads example uses this same owner when preparing its exact
SDK module declaration. Native/Builds deployment is excluded from source inputs.

`SdkBuild` is the single managed compiler task. It uses the installed matching
SDK, selected CMake/Ninja/MSVC environment, incremental CMake build directory,
isolated metadata inspection and immutable candidate deployment. A failed or
cancelled candidate does not replace the last good project module declaration.
`Native/Builds` holds deployment kits, not editable source. The build log is
`.forge/sdk-build/build.log`; contained compiler file/line/column diagnostics open
in the built-in C++ document. **Test compiler tools** uses a disposable starter
and never publishes it. Externally maintained exact-SDK layouts retain their
external build workflow.

Native commands use the private `forge_tools --command-worker` mode, dispatched
before world initialization. The editor's existing `BuildCommand` sends a bounded
framed argument record and owns all SDL streams on its caller thread. The helper
inherits the selected environment/working directory; target stdin is null, and
target stdout/stderr share the log pipe. A separate receipt reports target exit
status and confirmed descendant retirement. No public automation/MCP method
exposes an arbitrary command launcher.

On Linux, only the isolated helper becomes a subreaper; it terminates and joins
adopted descendants, including new sessions. Windows attaches the suspended target
to a kill-on-close Job before it runs, then verifies Job retirement. Cancel and
timeouts keep the task busy while bounded pumping continues. Shutdown joins the
helper; sending a termination request or observing helper exit alone is not proof
that writers have stopped. OS teardown has no promised fixed latency. Other OS
families are not admitted by this supervisor.

Incremental storage uses `.forge/sdk-build/builds/<UUID>`. Before launch, SdkBuild
claims the bounded `cache-ready.json` pointer into its unique candidate directory.
Only confirmed retirement republishes a reusable pointer. A missing receipt or
interrupted editor leaves that generation/candidate retained, and retry selects
fresh storage. Normal completion, compiler failure and confirmed cancellation reuse
the same generation. Legacy `.forge/sdk-build/build` data is preserved; it is not
silently adopted. Private cache paths reject redirects into authored storage.

The installed SDK's configure gate reports expected and detected values for
compiler/version, platform/architecture/configuration, flags/toolset and CRT
without relaxing exact admission. Windows deployment rejects CMake older than
4.3 at configuration and install time, before its CMP0207-dependent DLL scan.
Source-only SDK consumers retain their existing platform/configuration rules.

For managed projects, the admitted module records a deterministic identity of
all saved ordinary files in managed `Native/` (excluding build/deployment directories) and the selected exact SDK fingerprint. This conservatively includes custom CMake inputs and nonstandard include suffixes.
Generated deployment and build caches are excluded. Files, paths and bytes are
bounded; redirected/special inputs reject. Source hashes are checked on revision
and a throttled interval in the editor, plus at build/Play/export boundaries, not
each frame. A build rejects source changed while it was preparing its candidate.
**Project Code: Current** means the admitted module exactly matches saved source.
**Source Dirty**, **Build Required**, **Building**, and **Build Failed** remain
distinct. Build on Save is optional convenience. A failed build retains Last Good,
but does not claim it is Current.

Play with dirty or unbuilt managed source offers **Save, Build & Play** and waits
for candidate admission; a failure leaves Play stopped. Export rejects dirty or
stale managed source. The standalone package contains the admitted compiled
module, matching runtime, declared dependencies and cooked content, not source,
editor or compiler. Normal export remains relocatable. An older project whose
`Native/gameplay.cpp` has no managed SDK marker is diagnosed as unsupported; its
source is preserved for deliberate manual migration, without unsafe automatic
C/C++ translation.

## Live values and recovery

Admitted reflected values can be tuned in Play's **Live gameplay** Inspector.
The editor sends session/generation, scene AssetId, EntityId, type, property and
typed value through the runtime transport. The runtime validates and applies on
its owner thread between simulation ticks, then acknowledges with a snapshot.
Both runtime hosts route these requests through `RuntimeWorld::tune_property`;
the Scene's existing authored codec validates custom fields, while animation,
navigation and particle services retain their own mutation rules.
Invalid/stale/unadmitted updates reject; authored scene and prefab overrides are
unchanged. Stop discards transient tuning. Schema or C++ code changes still need a
new build and fresh Play. No arbitrary native-object checkpoint or hot reload is
claimed. Runtime crashes cannot mutate the editor's authored scene; game Save
Game/Continue persists only the game-defined save schema.

An existing **State Condition** can also be tuned through the same Scene owner.
It validates complete enabled domain/state references before native apply, then
uses RuntimeSimulation's tickless physics retirement and source-audio refresh.
Authored enable fields remain unchanged. Newly eligible physics awaits normal
synchronization, with recovery unavailable in the interim; no tick or survivor
command is consumed to manufacture readiness. See [stock activity conditions](application-states.md#stock-activity-conditions).

## Editor plugins

Native editor plugins are trusted, validated and restart-bound. Staging and
manifest checks in `tools/forge_plugins.py` do not constitute a live editor DLL
loader. This is separate from C++ gameplay modules.

## Skeletal animation services

Matching C++ code can read fixed playback status and timing markers, or control an
admitted player through the existing Animation service. Play/Pause/Restart require
Gameplay before Animation; PostPhysics supports observations. The intrinsic runtime
entity-reference callback obtains the registered canonical UUID pair for ordinary
entity Systems. See [controls and identity](animation-playback-controls.md),
[status](animation-playback-status.md) and [markers](animation-markers.md).

The same status/control/seek callbacks support a Scene-owned AnimationSequence
controller and standalone TransformAnimator. A controller's status has an empty
clip UUID; its bound tracks expose status but reject independent control/seek.
See [sequences](transform-animation.md#coordinated-scene-sequences). The new native
component layouts change the exact SDK fingerprint; rebuild matching modules.
C ABI1 tables remain unchanged. AnimationSequence.sequence_rate is an installed
double, default 1, independent of the retained standalone playback_speed. Bound
positive rates multiply down the chain; public playback speed includes the result.
The matching sequence SDK probe checks default and 2× children, rejects their direct
controls and repeats exact fresh recovery. Rebuild against the current installed
headers; do not reuse an older component layout with the new host.

`Animator` and `TransformAnimator` also append default-false `sequence_multiway`.
Matching installed modules can copy/edit it through their existing typed component
access. It opts a bound collection into up to eight overlapping phases. C ABI1
playback records and callback tables stay unchanged: with more than two positive
contributors, their two clip slots and relative weight are only a dominant/runner
summary. Full local times/influences are available through the editor/MCP watch.
The installed-only sequence probe covers both modes, relocated execution and four
fresh recoveries per scenario. See [layered overlaps](transform-animation.md#layered-clip-overlaps).

Matching C++ code can also read copied rigid-body COM, linear/angular velocity,
activation and velocity at an absolute world point through the existing Physics
service during a fixed tick. PostPhysics observes that tick's completed native
integration; PreFixed observes previously realized state. Reads do not wake a body
or consume queued commands. See [body motion](physics.md). Angular observations
use radians/s; authored angular limits use degrees/s. The PhysicsLoads example
reports each cube's measured first-step speed through ordinary diagnostics.

### Music and UI sound during Pause

AudioSource.pause_with_game defaults totrue. Set it to false for sound that should
continue while gameplay is paused. In SDK control callbacks, existing
sdk::Client audio operations can target only active opted-out sources; ordinary
sources and suspended output reject. Fixed callbacks retain their queued audio
semantics. Audio capability discovery no longer marks the service FIXED_ONLY.
Rebuild exact-SDK modules for the appended value/fingerprint contract. The public
unpaused_audio.cpp sample uses Gallery84's persistent references and control input;
no native audio handle or new gameplay clock crosses the boundary. See [Audio](audio.md).


AudioSource's public values include optional low/high-pass filters and finite
20–20000 Hz cutoffs. `samples/native_sdk/audio_probe.cpp` demonstrates fixed-domain
component writes; its installed `audio_gameplay` target uses only public headers.
The existing unpaused example can play filtered UI sources during game Pause.
Native DSP nodes remain private; no new Audio command, callback layout or ABI
number is needed. Header fingerprint changes require a matching module rebuild.

## Light image values

The installed `Light` layout includes an optional Texture reference, Directional
image width/height and tiling. These appended values change the exact SDK header
fingerprint; rebuild matching C++ modules. ABI1 callback tables remain unchanged.
The installed `soft_shadows.cpp` example writes all three supported image kinds
with matching built-in 2D/Cube resources, then clears the image for area kinds.
See [light-image ownership and limits](light-images.md).

### Scene UI Values

The installed `<forge/ui_values.hpp>` exposes UiValue and UiValues with normal
owned C++ strings/vectors, finite-number/name validation and a16-entry limit.
The engine registers their native reflection and lifecycle. UI Values can be
read/updated through the same native component API as other installed values;
existing copied UI publications must use different names. Native playback and
ordinary/SDK hosts consume the same component. `ui_values_gameplay` is an original
installed sample that checks independent vector/string copying and publishes a
separate cpp_width value from the current native width. Exact SDK matching is
required. UI runtime/presenter/asset handles are not installed as a second owner.
