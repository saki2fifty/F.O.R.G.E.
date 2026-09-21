# Runtime timing and presentation

## Ownership and stages

The headless runtime owns a `std::chrono::steady_clock` driver. The editor controls it through protocol 2; editor frame deltas and message arrival intervals are never simulation deltas. The runtime main thread owns Flecs mutation, control handling, sampling and extraction. There is no simulation thread added for transport, SDL runtime dependency, window or GPU requirement.

`RuntimeClock` accumulates monotonic elapsed time. Each completed tick calls `RuntimeSimulation`, which runs an explicit Flecs pipeline selecting `FixedSimulation` systems and ordering their Phase/DependsOn relationships. Current stages are **Input → Gameplay → Animation → Navigation → PrePhysics → Physics → PhysicsAdoption → PostPhysics → Transforms**. Animation applies explicit model-node channels; physics synchronizes, simulates and adopts before final world evaluation. RuntimeInput latches an immutable snapshot immediately before this pipeline. These immediate systems operate on the owning world; no Flecs worker threads are enabled. Effective local-pose capture follows completion. Presentation is an explicitly separate extraction stage on snapshot requests, not another gameplay tick. Flecs `progress(fixed_dt)` updates its frame/time metadata using the same explicit delta passed to the native callback.

The pipeline is intentionally restricted to tagged simulation systems. General native system registration, timer/rate-filter integration and a public gameplay SDK are not exposed by ABI v1. Presentation never runs simulation systems. Fixed timestep guarantees a stable interval and ordered execution, not cross-platform or lockstep determinism.

## Clock policy

| Setting | Value |
|---|---|
| Default frequency | 60 Hz |
| Tick delta | 1 / simulation_hz seconds; float conversion shared by module and Flecs |
| Maximum elapsed contribution | 250 ms per outer iteration |
| Maximum catch-up | 8 ticks per outer iteration |
| Remaining complete debt | Dropped, counted; fractional remainder retained |
| Simulation tick | uint64, incremented once after each successfully completed tick |
| Time scale | Not implemented |

`forge_runtime --simulation-hz N` supports finite 1..240 Hz. This is centralized runtime configuration, not a scene property. The editor currently uses 60 Hz. A future project setting belongs in project/runtime configuration, not every scene; there is no scene-format migration for timing. Probe and live editor processes use the same default configuration. Python tooling also uses 60 Hz.

Startup is paused for scene/module setup. Play/Resume rebases the monotonic reference and clears the accumulator. Pause discards debt. Repeated Resume while running does not reset the clock. Step requires Pause, executes exactly one fixed tick and stays paused. Stop terminates the process; no runtime edits merge into authoring. A restarted process starts tick zero with a new transient session. Reload/recovery never treats paused elapsed time as debt.

Status reports paused, simulation_hz, fixed_dt, tick, ticks_this_iteration, accumulator, alpha, dropped_ticks and clamped_seconds. Clamped seconds and dropped whole ticks report different losses; neither is silently replayed. They are not a full profiler.

## Derived presentation

`PresentationPoses` stores previous/current effective local translation, quaternion rotation and scale, keyed by generation-bearing Flecs handles. `WorldContext::transform_nodes()` supplies the same effective spatial graph used by Phase 3, including generated prefab interiors. This is a transient read snapshot, not another authoritative ECS hierarchy.

While running, alpha = accumulator / fixed_dt. Translation and scale use linear interpolation; normalized rotation uses shortest-path quaternion slerp (normalized linear limit for nearly equal rotations). The shared transform evaluator composes interpolated local poses through the hierarchy exactly once. It supports FollowStructure, World and membership-qualified Explicit bindings; unresolved parents stay unresolved. Affine WorldTransform matrices are never interpolated.

Snapshot `effective_scene.world_affine` carries this derived render output. The other reflected fields describe the current completed simulation state; this is not an independently editable pose document. Interpolation never writes LocalTranslation/LocalRotation/LocalScale or simulation WorldTransform. Authoring gizmos remain immediate.

Initial samples are identical. Newly sampled entities start previous=current. A changed resolved parent/availability resets that entity's local history. Explicit discontinuities (teleport/snap, scene replacement, recovery and resumed presentation) use `reset_presentation()` to collapse the current graph's history; `PresentationPoses::snap` can collapse an individual sampled local channel set. ABI v1 exposes continuous translation only, not a teleport API. Large distance alone is not used to guess a teleport. Deleted samples are retired; recycled Flecs generations are fresh samples. While paused, alpha=1 and Step immediately presents its completed state. Resume collapses samples to prevent rewinding from the paused pose.

Snapshots are sampled when requested. JSON transport remains temporary and can add visual latency; the editor does not extrapolate old snapshots. Sampling and JSON serialization still consume runtime CPU time. This is not a production high-density renderer or frame-sharing transport.

## Protocol 2 and backpressure

Use newline-delimited JSON on redirected stdin/stdout pipes. Windows requires pipe handles; direct console/file handles are rejected with a diagnostic. `--version` needs no pipes. POSIX uses nonblocking file descriptors; Windows uses bounded PIPE_NOWAIT polling on inherited anonymous pipe handles. This is polling, not overlapped I/O. Runtime simulation does not wait for reads or for output-buffer space. Writes use bounded 1 KiB fragments so two polling peers can progress within the default anonymous-pipe quota. Pending output also drains between completed catch-up ticks (up to32 one-KiB attempts per flush), so an eight-tick simulation batch does not monopolize response progress. This only transmits an already captured reply; commands and new snapshots remain at the outer request boundary. Each outer iteration performs bounded byte work and yields briefly; actual monotonic elapsed time, not the yield duration, drives ticks.

First request: `{"protocol":2,"id":1,"command":"hello"}`. The response supplies a fresh transient `session` token. Subsequent requests include that session and a strictly increasing positive unsigned `id`. Responses echo both. Wrong sessions and stale IDs cannot execute controls. Accepted-session command failures consume their ID; clients must not blindly retry mutations. Protocol 1 and any caller `seconds` field are rejected. Runtime protocol, native ABI 1, authoring API 1 and scene format 3 are separate versions.

Commands: hello, play/resume, pause, step, snapshot, ping, schema, replace(scene), load_module(absolute path), save(path), quit. Replace and load_module require paused state. Successful replies include uninterpolated `scene`, derived `effective_scene`, schema, module identity, timing and activation state/generation/tick. Errors contain ok=false and an error diagnostic.

The controller permits one request in flight. Runtime stores one bounded response, preserves partial byte offsets, and finishes a transmitted JSON line before starting another. No unsolicited snapshot stream or obsolete snapshot queue exists: a new request samples the newest state when serviced. The runtime stops accepting further commands until the previous response drains, but keeps simulating. Thus backpressure bounds control throughput, not simulation speed. A client that stops reading cannot expect its next control acknowledgement until it drains the previous response. Reliable acknowledgements are not dropped to make room for presentation.

Frames are capped at 16 MiB; oversize input terminates the worker and oversize output produces a bounded diagnostic. Editor clients time out outstanding requests after five seconds. End-of-input/broken pipes terminate the session; orderly quit has a bounded output drain. There is no network listener. Disk saves, native callbacks and serialization are synchronous owner-thread operations; blocked native code remains a process failure/stall, not a schedulable task.

## Transactional native activation

See [native modules](native-modules.md). Build/probe does not mark a live candidate active. Probe uses representative current checkpoint data and one real fixed tick. The live runtime acknowledges a safe pause boundary and an uninterpolated checkpoint before load. Activation state becomes LoadedPendingFirstTick. Only a completed live fixed tick changes it to Active; loading/ABI checks alone do not.

Running sessions resume automatically after load. Paused sessions wait for Step or Resume, with controls available. First-tick/load failure restarts the previous known-good artifact and boundary checkpoint, restores the pre-reload run/pause policy, resets samples/debt and creates a new session. The old DLL need not remain loaded. Stop cancels pending activation without a tick. A newer pending candidate supersedes the old transaction by restarting from the original known-good checkpoint; pending transactions never stack. Later gameplay crashes use explicit user recovery from the last acknowledged checkpoint.

Recovery covers the existing supported host-owned scene state only. Generated-child private runtime edits, arbitrary native static state, external effects and resources are not general rollback/save-game guarantees. No generalized reload or callback-bearing SDK is introduced.

## Gameplay input boundary

There is no gameplay input transport/action consumer yet. SDL events currently drive editor interactions only. The future contract is platform events → queued runtime input state → immutable input snapshot at a fixed-tick boundary. Held state may apply to every catch-up tick; pressed/released edges are consumed once, not repeated for all eight ticks. Future input integration must test timestamp/order, focus loss and edge consumption with a real consumer. Phase 4 does not claim physical gameplay input validation or deterministic replay.

## Evidence

Pinned Flecs custom_pipeline/custom_phases examples, pipeline builder and `ecs_progress` implementation were reviewed; pinned SDL Windows process backend demonstrates inherited-pipe/nonblocking parent handling. Microsoft documents [anonymous-pipe handle mode changes](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-setnamedpipehandlestate) and [partial byte writes in nonblocking mode](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-type-read-and-wait-modes). Dependency revisions are unchanged.

Project simulation_hz and input map are provided at protocol2 Hello. Omitting these retains60Hz/empty-map behavior; an explicit runtime CLI frequency takes precedence. See [Input](input.md).

## Physics and recovery

The fixed pipeline now includes pre-physics synchronization, Jolt Update, adoption and PostPhysics before final transforms. Private physics-aware recovery restores a completed tick and resets interpolation/elapsed debt. See [Physics](physics.md).

The current complete order is Input → Gameplay → Animation → Navigation →
PrePhysics → Physics → PhysicsAdoption → PostPhysics → Transforms. Animation
advances and applies explicitly animated model-node local channels before physics
synchronization, using the same fixed delta. Presentation interpolates the captured
local transforms without a second ECS animation write. Legacy standalone Animator
debug sampling remains a read-only presentation consumer.

## Runtime UI presentation

RmlUi uses the reusable presenter's monotonic presentation clock and can update while fixed simulation is paused. Semantic Pause/Resume/Step commands are validated in the runtime process; custom gameplay UI actions wait for the next fixed tick. No UI update cadence controls the runtime clock. See [Runtime UI](runtime-ui.md).

### Pending model animation assets

Cooked model skeleton/clip loading is asynchronous. Paused presentation may adopt
ready CPU resources, but it never advances playback or simulation time. While an
Animator binding is pending or invalid, runtime responses carry `recovery: null`;
a partially realized world must not advertise a complete recovery snapshot.

Native replacement requires a complete checkpoint both before probing and at the
paused replacement boundary. Editor and tooling callers retain the previous module
and report that loading must finish if the checkpoint is unavailable. A crash before
a complete checkpoint requires fresh Play; it is not reported as recovered playback.
Recovery of a complete checkpoint reconstructs resources in an unpublished world,
with a bounded wait shared across its animation dependencies.
