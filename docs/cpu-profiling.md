# CPU profiling

Window > CPU Profiler opens a dock with separate Editor and Play runtime tabs.
Status Details also opens it. Visibility is a machine workspace preference; it
does not change a project, Scene, saved game or exported runtime UI.

## Collection and interpretation

Collection starts off. Start/Stop collection controls the existing EngineServices
Profiling capability for that owner. Stop retains completed samples. Clear removes
completed samples and invalidates unfinished scopes, preserving the enabled policy.
An older scope cannot appear after Stop/Restart/Clear. Closing the dock stops its
optional Play observations; collection continues until explicitly stopped or that
owner is retired. A new editor process starts with collection off.

The512-record ring contains category, scope name, duration in seconds, count1,
fixed tick and monotonically increasing owner-local sequence. Rows group by category
and name, order by total retained duration, and show count, arithmetic mean, maximum,
latest duration and latest tick. Filtering changes only the view. Eviction follows
completion order; it is a recent sample window, not all time since collection began.
Editor frame sections use tick0. This is inclusive CPU wall time: nested scopes
overlap, elapsed waits can be included, and summing rows is not total frame cost.

Existing Performance frame sections feed the same recording owner without adding
frame clocks: UpdateAndEditorUI, SceneSubmission, UISubmission and PresentCall.
The existing Performance comparison/redraw controls and half-second averages remain
available. FPS in the profiler is the existing Editor loop measurement, not game
simulation FPS or GPU execution time. Present/driver waits can affect CPU sections.
Runtime scopes include FixedSimulationTick, Animation, Navigation, NavSourceCapture, physics
synchronization/step/adoption, audio synchronization and PresentationExtraction;
existing transform/prefab/native-module measurements remain their own scopes.

## Service and lifecycle

ServiceAccess provides profile_status(), profiling_enabled(), clear_profiles(),
profiling(bool), ProfileScope and explicit record_profile(). Access remains typed,
weak and owner-thread bound. Status reports compiled/enabled/capacity/sequence/samples.
Invalid explicit records are rejected; invalid clocks, scope names or allocation
failures cannot escape automatic scope construction/destruction or editor frame
instrumentation. Disabled automatic scopes read no clock. Disabled editor sections
do not create profile names or samples. FORGE_DISABLE_PROFILING removes collection;
the UI explains that collection is unavailable and exposes no controls.

## Optional Play protocol2 extension

An optional boolean profile_watch on a correlated request asks for cpu_profile in
the reply; it never starts collection. Unwatched ordinary replies omit those records.
Malformed watch values are rejected before clock/input controls run. The cpu_profile
object includes available and world_generation alongside service status. An SDK host
without a live world reports available=false.

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
visual review or physical desktop acceptance. No GPU counters, allocation profiler,
flame graph, remote tracing or exported in-game editor is implemented here.

The profiling service is internal to the host and is outside the installed SDK
header/value boundary. This batch changes no installed SDK values, fingerprint
inputs or versioned C entry; ABI1 remains unchanged. See
[native modules](native-modules.md) and [runtime timing](runtime-timing.md).
