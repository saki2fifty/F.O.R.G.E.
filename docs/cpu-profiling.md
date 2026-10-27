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
overlap lanes. All lanes represent one owner thread; pid/tid values are layout
identifiers, not OS IDs or measured worker concurrency. See the official
[Perfetto external-format guide](https://perfetto.dev/docs/getting-started/other-formats).
No viewer implementation or parser is integrated; external viewer acceptance is
separate from the format/unit/nesting checks.

`otherData` records source generation/epoch/sequence, retained measured and
duration-only counts, owner/lane semantics, origin and page metadata. Capture
date/time/UTC offset uses the existing editor event-time formatter at the read/copy
boundary. It is not a correlation between monotonic event times and UTC.
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
profiling(bool), ProfileScope, explicit record_profile() and record_profile_span(). Access remains typed,
weak and owner-thread bound. Status reports compiled/enabled/capacity/sequence/epoch/samples.
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
generation and a boolean enabled. Optional clear must be boolean. All fields are
validated before changing collection; existing session/ID checks still apply. It
does not advance simulation, write authored Scene values or change Pause/Step policy.
Rejected controls are nonfatal editor feedback; pending controls are disabled.
Active world replacement preserves the process collection preference but starts a
fresh world-owned ring. Preparation candidates do not inherit active collection,
and stale generation controls cannot affect a replacement world. Records remain
bounded inside the existing response-size and transport policy.

## Validation boundary

Core tests inject clocks for collection/lifetime/boundary cases and exercise actual
fixed runtime workloads. UI tests drive actual ImGui input at100/150/200%; production
pipe and SDK-host tests verify optional responses, rejected controls, paused stepping
and replacement isolation. These CPU checks do not prove Windows/GPU performance,
visual review or physical desktop acceptance. No hardware counters, allocation profiler,
sampled flame graph, worker-thread tracing or exported in-game editor is implemented here.
Host scope starts/copy and paged Play trace reads are implemented; per-system
invocation tracing remains distinct from native ECS lifetime observations.

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
