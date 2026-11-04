# CPU and GPU profiling

Window > Profiler opens one dock with Editor CPU, Play runtime CPU and Graphics GPU tabs.
Status Details also opens it. Visibility is a machine workspace preference; it
does not change a project, Scene, saved game or exported runtime UI.

## Collection and interpretation

Collection starts off. Start/Stop collection controls the existing EngineServices
Profiling capability for that owner. Stop retains completed samples. Clear removes
completed samples and invalidates unfinished scopes, preserving the enabled policy.
An older scope cannot appear after Stop/Restart/Clear. Closing the dock stops its
optional Play observations; collection continues until explicitly stopped or that
owner is retired. A new editor process starts with collection off.

CPU records contain category, scope name, duration in seconds, count1,
fixed tick and monotonically increasing owner-local sequence. Rows group by category
and name, order by total retained duration, and show count, arithmetic mean, maximum,
latest duration and latest tick. Filtering changes only the view. Eviction follows
completion order; it is a recent sample window, not all time since collection began.
Editor frame sections use tick0. This is inclusive CPU wall time: nested scopes
overlap, elapsed waits can be included, and summing rows is not total frame cost.

### Measured intervals and trace export

Completed host `ProfileScope` records also retain their real `start_seconds` from
the same owner clock already used to measure duration. Host-only
`record_profile_span(category, name, start_seconds, seconds, tick)` admits a producer's
observed interval without reading another clock. `record_profile` and the installed
SDK duration callback have no measured start and remain duration-only. The SDK
`ProfileScope` helper passes only its duration through that callback; no versioned
C function table or installed SDK fingerprint input changes.

Performance retains actual Scene submission boundaries and records Scene,
UI submission and Present intervals using its existing clock reads. Exclusive
UpdateAndEditorUI subtracts Scene work and is fragmented, so it remains untimed.
A frame without an observed Scene call retains a duration-only Scene record.
The four summary records and Performance averages retain their existing meaning.

`cpu_profile_trace` is a private bounded mapper shared by the native CPU Timeline
view, Copy trace JSON and Play automation/MCP. It validates at most512 copied
records, sorts measured spans by start/descending duration/sequence, and normalizes
starts to the earliest retained measured interval. It does not fabricate starts for
SDK, explicit duration-only, GPU or native ECS lifetime records. Invalid starts,
duplicate/nonpositive sequences and unrepresentable windows are rejected.

Export uses documented Trace Event complete `X` events and `M` process/track metadata,
with `ts` and `dur` in microseconds and `displayTimeUnit=ms`. Complete events must nest
within a track, so crossing intervals are assigned explicitly labelled logical
overlap lanes within their measured stage. Untagged spans belong to the caller;
strict `native_stage`1..15 tags are admitted only on measured `ecs.system` records.
Workers use distinct tracks; crossing intervals on one stage receive logical
overlap lanes. Native stage/track identifiers are not OS IDs. Owner-only captures
retain their existing envelope. See the official
[Perfetto external-format guide](https://perfetto.dev/docs/getting-started/other-formats).
No viewer implementation or parser is integrated; external viewer acceptance is
separate from the format/unit/nesting checks.

`otherData` records source generation/epoch/sequence, retained measured and
duration-only counts, owner/lane semantics, origin and page metadata. Capture
date/time/UTC offset uses the existing editor event-time formatter at the read/copy
boundary. It is not a correlation between monotonic event times and UTC.
Native capture also exports `native_systems_dropped`, the cumulative count since
Clear of dispatches skipped beyond the bounded nesting depth. The page cursor
includes that counter as well as epoch/sequence so metadata changes invalidate it.
Editor and isolated Play clocks are never merged. UI filtering changes only the
view; copying emits all retained measured records. The table virtualizes rows and
uses the existing window/table scrolling at small sizes or high UI scaling.

`play.profile.trace` / `forge_play_profile_trace` is read-only under `play.read`.
Collect and use the existing profile refresh first. Pages default to and allow at
most128 measured spans, with offset0..512. Every page keeps the full snapshot's
origin, lane layout and total count. The returned `profile_state` is derived through
the existing content digest from launch/session/world/epoch/sequence; providing it
on later pages rejects a changed snapshot. Pause/Stop collection to retain stable
pages. Cold or mismatched-world reads report not_ready. Clear invalidates old
cursors even for empty histories. No new collector, cursor database or authoring
history is created. The machine-wide Editor collector can span projects and is
intentionally UI-only; project-scoped MCP exposes the isolated Play owner.

Existing Performance frame sections feed the same recording owner without adding
frame clocks: UpdateAndEditorUI, SceneSubmission, UISubmission and PresentCall.
The existing Performance comparison/redraw controls and half-second averages remain
available. FPS in the profiler is the existing Editor loop measurement, not game
simulation FPS or GPU execution time. Present/driver waits can affect CPU sections.
Runtime scopes include FixedSimulationTick, Animation, Navigation, NavSourceCapture, physics
synchronization/step/adoption, audio synchronization and PresentationExtraction;
existing transform/prefab/native-module measurements remain their own scopes.
RuntimeEntityPublication separates queued ordinary/Prefab creation at the fixed
boundary, including detached preparation, validation and publication. It is recorded
even when the queue has no pending requests. ResourceSynchronization measures the
existing resource synchronization call when that capability is available;
PresentationPoseCapture includes gathering native transform nodes and retaining
the interpolation poses after the pipeline. All three use the current fixed tick
and remain included in FixedSimulationTick. A Ready entity request does not imply
that asynchronous resources or physics are ready. These are CPU scopes, not GPU
timings, completed asset counts or an extra simulation step.
The Navigation controls record NavigationStatus in the editor category when their
advisory check runs; they consume native source inputs and the existing asset snapshot.
Opted-in navigation agents record NavAvoidance in the navigation category for
velocity sampling and bounded surface movement. NavAgentUpdate remains inclusive.

## Native dispatch intervals

In Play runtime > CPU scopes, enable **Trace ECS systems** as well as collection.
The default-off option emits category `ecs.system` through the existing scope ring,
Timeline, trace copy and paged Play MCP. A scope brackets an actual native dispatch
inside `RuntimeSimulation::tick`'s fixed pipeline, after native timer gating. It
includes query/setup/defer work, not a separate record for every entity callback.
Explicit nested native runs inside this boundary are included. Runtime-owned
long-running ECS stages also support dispatch capture with the default monotonic
service clock. Calls outside this boundary and unrelated foreign threads are not
captured. Worker execution is controlled by saved `ecs_workers`; tracing does not
resize a pool or create a scheduler. The native stage, not an OS ID, identifies
each measured worker track. Logical overlap lanes stay within that stage.

Each tick uses a scoped bridge. Caller scopes retain their weak owner service.
Workers copy detached names/starts/durations into preallocated per-stage buffers;
they never read a custom clock or invoke a service. Completed records publish
through the existing owner store with the captured tick and collection epoch.
Clear during a tick rejects older worker records. Custom clocks and external
native stage configurations retain owner-only capture. `system_trace_scope` in
the ECS plan reports supported capture; `native_stages` and `measured_threads`
in trace metadata report only observed retained records. One collector owner
remains authoritative even when several native stages are measured.

Up to64 dispatches nest per thread. Each worker can copy512 completed records per
capture; depth/buffer overflow increments the existing `native_systems_dropped`,
reset by Clear. Unmatched scopes are discarded; worker TLS resets older nesting
on its first push in a new capture. Owner exception unwind disables that domain
until retirement, preventing late callbacks from acquiring a later tick. Slot
handoff remains mutex-safe during unwind. Arbitrary exceptions escaping native
gameplay callbacks are not made resumable by instrumentation.

The shared512-record history can evict older records quickly, including earlier
stages in a busy tick. This eviction is distinct from skipped dispatches. Observed
thread counts may therefore be lower than the configured count. Native paths are
copied with at most128 display bytes, UTF-8-safe shortening and invalid-byte output
replacement; they are display labels, not persistent entity identifiers.

The native library privately enables pinned Flecs `FLECS_PERF_TRACE`. FORGE composes
its perf callbacks and a delegating thread-start adapter at OS bootstrap, before
worlds/threads. The selected native creator, callback, opaque argument, handle and
join remain authoritative. Only a scoped binding around RuntimeSimulation's
explicit stage creation attaches a detached domain to those threads. There is no
world registry or replacement thread scheduler, and no runtime OS-table rewrite.
An already configured external callback/API remains authoritative; unsupported
hosts report `native_systems_available=false`, reject enable and retain ordinary
host profiling. The isolated Script worker skips this initialization so its
existing confined file callback can be installed. Script evaluation fails closed
if the native OS table does not accept that callback. The macro is not propagated
to SDK consumers; native structs, public C tables, ABI1 and SDK fingerprint inputs
are unchanged.

Capture adds clock, copied-label and ring-record overhead. A local Linux GCC12
Release test on a pinned QEMU/KVM shared-runner CPU used64 actual fixed systems,
100warm-up ticks and2000 measured ticks per round. Alternating before/after binaries
with verified native libraries gave median disabled cost27.82 to30.04us/tick
(+2.22us,7.99%). Host-scope collection varied37.63 to35.76us/tick; this is not evidence
of a speedup. Opt-in native capture cost322.52us/tick,286.76us above the after
host-scope case. These are instrumented synthetic CPU workloads, not physical
hardware, Windows, game FPS or cross-engine performance claims.

## Native runtime systems

The Play runtime tab contains CPU scopes and ECS systems. RuntimeSimulation owns
one bounded copied native plan. An optional profile observation requests a sample
at the next successfully progressed fixed tick while the same Profiling capability
is enabled. Without observations, collection does not repeatedly enumerate or
serialize the plan. Inspection never progresses the authoring world or runs a tick.
Stop retains the last copy. Clear's existing service epoch invalidates it; a new
world begins empty. Observation failures report unavailable instead of faulting
gameplay. A partial native pipeline is never sampled after a failed progress.

The `cpu_profile.ecs` object includes available, tick, timing
`native_system_lifetime`, capacity256, total_entries, truncated and entries. A row
is either kind=merge or kind=system with generation-bearing native id, bounded
display name/phase, cpu_seconds (nullable), immediate and multi_threaded. IDs are
world-local diagnostic handles, not persistent EntityIds. Ordered entries and zero
merge markers come directly from pinned `ecs_pipeline_stats_get`; vectors are
finalized immediately. No pipeline history reduction or parallel scheduler exists.
The captured native plan is not an execution trace; conditional systems can skip
individual invocations and native topology can rebuild during progress.

CPU values copy `ecs_system_t::time_spent`. These are native lifetime CPU wall-time totals,
including measurements outside the collection window and any explicit native
runs of the same system. Clear never writes native counters. Flecs Stats already
enables native timing at WorldContext startup; this view adds no independent
measurement clock. A missing/nonfinite native timing is null. CPU scopes retain
their existing recent-window semantics. Neither view is total frame/GPU cost.
Native waits can contribute to these durations. Mode describes configuration,
not measured concurrency or worker utilization.

With multiple stages, RuntimeSimulation disables the pinned shared non-atomic
native per-system timing counter, restoring its former flag on retirement.
CPU lifetime values are null. Independent completed dispatch records use the
profiling store above; enabling them does not re-enable native lifetime timing.
`ecs_workers` reports actual stages, including caller0. The trace observes wall
intervals and waits, not scheduler CPU running time or hardware utilization.

Only the first256 plan entries are copied; truncation is explicit. Each displayed
path has at most160 source bytes, shortened at a UTF-8 boundary with `...`; invalid
native display bytes use the existing JSON output replacement policy. Canonical
native names remain unchanged. Snapshot IDs are scoped by the existing outer
world_generation/session/launch. No scene/schema/ABI/SDK fingerprint change.
The pinned native stats count/rebuild fields are not used: the implementation at
fb55f3c populates its ordered vectors, not those struct summary fields. Native
stats remain their own authority; FORGE does not invent history or totals.

## Service and lifecycle

ServiceAccess provides profile_status(), profiling_enabled(), clear_profiles(),
profiling(bool), profiling_systems(bool), native availability/preference queries,
ProfileScope, explicit record_profile() and record_profile_span(). Access remains typed,
weak and owner-thread bound. Status reports compiled/enabled/capacity/sequence/epoch/samples
and native_systems/native_systems_available/native_systems_dropped.
Invalid explicit records are rejected; invalid clocks, scope names or allocation
failures cannot escape automatic scope construction/destruction or editor frame
instrumentation. Disabled automatic scopes read no clock. Disabled editor sections
do not create profile names or samples. FORGE_DISABLE_PROFILING removes collection;
the UI explains that collection is unavailable and exposes no controls.

## Optional Play protocol2 extension

An optional boolean profile_watch on a correlated request asks for cpu_profile in
the reply; it never starts collection. Unwatched ordinary replies omit those records.
Malformed watch values are rejected before clock/input controls run. The cpu_profile
object includes available, world_generation and the native ECS observation alongside
service status. An SDK host without a live world reports available=false.

The profiling command requires generation equal to the current positive live world
generation and a boolean enabled. Optional clear/native_systems must be boolean. All fields are
validated before changing collection; existing session/ID checks still apply. It
does not advance simulation, write authored Scene values or change Pause/Step policy.
Rejected controls are nonfatal editor feedback; pending controls are disabled.
Active world replacement preserves collection and native-capture preferences but starts a
fresh world-owned ring. Preparation candidates do not inherit active collection,
and stale generation controls cannot affect a replacement world. Records remain
bounded inside the existing response-size and transport policy.

## Repeatable runtime CPU reports

From a source checkout with Python3 and a configured single-configuration build:

```sh
python tools/benchmark_runtime.py --runtime /path/to/build/forge_runtime --output /path/to/evidence/run.json
python tools/benchmark_runtime.py --runtime /path/to/build/forge_runtime --baseline /path/to/evidence/run.json --output /path/to/evidence/next.json
```

On Windows use the matching `.exe`; native Windows execution remains separately
queued. `--cache` selects the matching CMakeCache.txt if it is not beside the
executable. The tool reuses the bounded source `tests/runtime_client.py`; this is
developer source tooling, not a separately distributed editor or in-game profiler.
The existing Windows startup/idle/shader benchmarks retain their own scope.

The first workload revision uses flat and four-way hierarchical transform scenes
at64/512/4096 entities,32warmup and32measured ticks, three fresh-process repeats.
Override `--counts` (1..6 distinct counts in1..4096), `--warmup`/`--samples` (1..128)
and `--repeats` (1..5). Stable synthetic canonical UUIDv4 fixture IDs never enter a
user project. Construction/load/IPC/serialization/readback validation stay outside
measured scopes. Only real FixedSimulationTick and PresentationPoseCapture records
are used; profiling overhead remains included, native dispatch tracing is off.
This default profile does not measure gameFPS, renderer/assets/animation/physics
throughput or physical hardware. It is a representative transform CPU subset.

Reading cpu_profile schedules a native lifetime-plan capture on the next tick.
The runner consumes that tick outside each at-most8-tick measured chunk, then
steps without watch and reads afterward. Missing/duplicate/evicted scope records,
wrong generation/epoch, nonfinite values or unexpected clock progress fail the
run. Every final derived affine matrix/resolved flag is checked against independent
integer parent sums; the authored snapshot must remain unchanged.

Reports retain raw microseconds, arithmetic mean/median/nearest-rank p95/max, tick
IDs, exact fixture hashes, UTC, binary/tool/client/compiler/cache hashes, SDK info,
configuration and OS/CPU/affinity where available. Git checkout metadata is only a
source hint; it does not verify the binary's source origin. Retain the matching
build receipt for source attribution. No process-wide environment dump is captured.

Each output path must be new. Baselines are bounded to4MiB and validated against
their raw samples, summaries, fixtures, counts and policy before timing. Known
workload/configuration/environment mismatches reject comparison; zero medians have
no percentage. Matching metadata does not prove matching host load, power policy
or physical hardware. Compare on the same controlled host and keep raw runs. The
report is an evidence artifact, not another engine collector or authoring database.
No game-creation API is added: automation/MCP already expose the consumed owner;
existing revision, permissions and Play isolation remain authoritative.

## Validation boundary

Core tests inject clocks for collection/lifetime/boundary cases and exercise actual
fixed runtime workloads. UI tests drive actual ImGui input at100/150/200%; production
pipe and SDK-host tests verify optional responses, rejected controls, paused stepping
and replacement isolation. These CPU checks do not prove Windows/GPU performance,
visual review or physical desktop acceptance. No hardware counters, allocation profiler,
sampled flame graph or exported in-game editor is implemented here.
Host and opt-in native dispatch starts/copy and paged Play trace reads are implemented;
native dispatch intervals remain distinct from native ECS lifetime observations.

The profiling service is internal to the host and is outside the installed SDK
header/value boundary. This batch changes no installed SDK values, fingerprint
inputs or versioned C entry; ABI1 remains unchanged. See
[native modules](native-modules.md) and [runtime timing](runtime-timing.md).

## Graphics GPU

Graphics GPU is default-off, editor-local duration acquisition. It includes the
editor's Scene, Game, asset/material/terrain/particle previews and UI composition.
It does not collect remote standalone GPU data. Optional duration-query support is
requested from the pinned Diligent device; unsupported devices keep rendering and
explain why collection is unavailable. There is no CPU-clock fallback.

DiligentPresentation owns at most64 lazily created duration-query objects. Flat,
non-overlapping scopes surround frame clears, shadows, camera geometry, enabled
Bloom, display resolve and Scene grid work. Query results use native Duration /
Frequency, with zero frequency rejected. Results arrive on later editor frames;
the dock shows latest frame delay, pending capacity, skipped scopes and discarded
old results. Frame is the presentation submission index, not the simulation tick.
Rows aggregate pass observations; multiple cameras can contribute several samples
in one frame. GPU queue elapsed time does not establish total device utilization,
and summing rows does not establish complete frame time.

Beginning a frame polls completed queries with nonblocking GetData, using the
existing frame fence to detect failed completed results. Acquisition adds no Flush,
WaitForIdle, fence or busy wait. In-flight queries remain retained through Stop,
Clear, owner retirement and rebind; only completed objects can be reused. Failed
query objects remain bounded and retained until presentation teardown, with no repeated polling/submission. Capture
and collector epochs reject delayed data from older captures or cleared records.
Switching projects stops GPU acquisition and invalidates unfinished results;
completed records remain until Clear. CPU Start/Stop is independent. Clear in
either editor view clears both editor clocks and invalidates their unfinished records. Closing the dock keeps collection
running; a new process starts off. Cached Scene frames remain cached. Use normal
Scene interaction or the existing Performance redraw comparison when measuring it.

GPU-tagged records share the existing512 total editor history with CPU records;
there is no second profile database. CPU profiles()/profile_status() and protocol2
cpu_profile remain CPU-only. Host-only gpu_profiles(), profile_epoch() and
record_gpu_profile() preserve weak owner-thread access and validate completed
samples without reading a CPU clock. No installed SDK header, versioned native
function table or ABI fingerprint changes. FORGE_DISABLE_PROFILING removes both
collectors. Native backend query heaps may be allocated when the device is created;
FORGE creates no query objects or timing submissions until GPU collection starts.

### Dynamic 3D physics workload

Select workloads explicitly to include isolated free-falling boxes:

```sh
python tools/benchmark_runtime.py --runtime /path/to/build/forge_runtime --workloads physics --counts 64 512 4096 --warmup 8 --samples 16 --repeats 3 --output /path/to/evidence/physics.json
```

`--workloads` accepts distinct `flat`, `hierarchy` and `physics` names. The default
keeps the original two transform workloads/revision1 reports. A custom selection
uses workload revision2; it compares only against the same selection, fixture and
sampling policy. Selecting physics additionally collects the existing
PhysicsSynchronization, PhysicsStep and PhysicsAdoption wall-time scopes for each
case, alongside complete tick and pose capture. These stages are included in the
complete tick: do not sum them with that parent scope. PhysicsStep joins native
workers; the report does not measure each worker's utilization.

The physics fixture places unit boxes on a 3 m grid, at 100 m height, with zero initial
velocity, zero damping and sleeping disabled. At 60 Hz, every final local position
and derived world affine matrix must match independent symplectic Euler free fall:
`y = 100 - 9.81 * tick * (tick + 1) / (2 * 60 * 60)`. The 0.0002 m numeric tolerance
accounts for native float velocities/timestep and double positions. Identity,
rotation, scale and body controls remain unchanged; active body counts, tick and
gravity must match. Only the expected simulated translation may change. Identical
motion preserves box separation; no floor, contacts, constraints, characters or
rendering are exercised. These costs are not a claim about collision-heavy games.

The same 1..4096 entity and bounded sampling limits apply. After validated final
readback, the tool unloads its disposable fixture through existing world replacement
before requesting the small shutdown reply. This cleanup is outside measurement;
it preserves the normal transport limits and avoids repeating a large final world
inside the runtime's bounded Quit interval.

Physics reports require the unmodified configured Jolt Git checkout, found through
`FETCHCONTENT_SOURCE_DIR_JOLT` or the build's `_deps/jolt-src`. They retain its source
revision and native CMake options, and reject differing native provenance in baseline
comparisons. Source/runtime/cache/tool hashes and the existing explicit unverified
binary-origin label still apply: selected checkout metadata does not independently
prove which source produced an arbitrary executable. Keep its matching build receipt.
Native Windows execution, physical performance and broader contact/game workloads
remain separate acceptance.
