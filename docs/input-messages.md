# Action-triggered typed messages

`forge.input_message` is an optional Gameplay component. Its `enabled` flag,
`action` UUID text and `payload` component key are ordinary reflected authoring
values. Scene, Prefab, generic property commands and their revision/history/file
owners remain authoritative. Project Settings owns input actions. A missing
selection is preserved and reported; selecting another action never rewrites IDs.

## Delivery and ownership

RuntimeSimulation latches RuntimeInput before the fixed pipeline. The immediate
`forge.runtime.InputMessages` system runs in `forge.runtime.Messages`, after
InputMonitor and before `forge.runtime.Gameplay`. The same SDK input borrow spans
these phases. Only digital actions outside control-frame contexts qualify. Held
input does not repeat; boolean pressed edges coalesce within a fixed tick using
the existing RuntimeInput contract. Inactive input contexts produce neutral input.
Pause/presentation/control-only frames do not send messages. Step runs the normal
fixed pipeline. Capture release, Pause and Resume retain existing neutralization.

The system considers active Scene members with Input Message, in durable EntityId
order, excluding native disabled/Prefab query rows and applying the existing
ApplicationStateActivation conditions. It resolves builtin schema keys through
their registered native types and project keys through WorldContext's admitted
authored codecs. The payload must already be attached, owned or inherited; the
emitter itself cannot be the payload. Unregistered opaque/runtime-only project
types are outside this authoring profile.

On a pressed edge it reads the existing value codec, checks the bounded value,
and copies native storage with public Flecs value lifecycle APIs. Every eligible
copy is captured before observers run. This is native value-copy semantics using
registered hooks, including admitted owned strings and native vectors; it does
not promise to clone arbitrary pointer graphs or resource owners. Plain-value
admission remains [the custom-component contract](custom-component-authoring.md).

The component type is the native event ID. `ecs_emit` uses the source entity,
the copied value as `const_param`, and its default `EcsAny` event ID filter.
Receivers observe that event with an Any term and read `iter.param<T>()` as a
const value. Filtering against an owned payload column would miss inherited
sources in the selected native event implementation. There is no second message
type database, event bus, persistent queue or scheduler.

Callbacks run synchronously. They must not throw, mutate the event value, retain
its pointer, destroy component type registrations or unload a provider. Copies
are released before the message system returns; the existing module/World
lifetime keeps native hooks loaded. Native IDs and pointers never cross process
or persisted-value boundaries. Receivers should check the source's durable Scene
identity when behavior applies only to a particular scene.

The immediate native system retains an outer defer scope. Structural changes
made with normal entity operations merge after the captured batch and before
Gameplay. A scheduled source deletion therefore does not cancel its captured
message. In-place writes through borrowed mutable storage follow normal Flecs
semantics; observers must not assume their unrelated query fields are snapshots.
The event value remains the captured copy. No observer-order guarantee is added.

## Bounds and observations

At most 256 sources are considered per tick, including inactive or unassigned
ones. Extra sources are reported in `overflow`. Each admitted native-storage size
plus encoded known value must fit 64 KiB; aggregate captured sizes fit 4 MiB per
tick. These are value-envelope bounds, not a measurement or bound on native
allocator/observer internal memory or execution time. Invalid values and routes
produce per-source diagnostics without sending that source. Other admitted
sources continue. There is no asynchronous dispatch or observer-time preemption.

`RuntimeSimulation::input_status().messages` reports `tick`, `emitted`, `emitted_total`, `last_sent_tick`, `overflow`
and bounded `sources`, each with its durable EntityRef, configured action/payload,
state and optional diagnostic. `Sent` means the event was dispatched; it does not
prove that a receiver existed. Console > Gameplay input displays this observation.
The cumulative sent counter saturates at uint64 maximum and survives idle ticks,
so a brief dispatch remains observable between editor frames. It resets with the
runtime, retains no event value and cannot replay a callback. The same copied
status is available through existing Play transport and
`forge_play_input`. Generic authoring tools plus `forge_play_input_submit` cover
configuration and testing; no separate agent implementation is added.

Runtime delivery never edits the separate authoring World or its history. A new
runtime starts with no message history; recovery and scene replacement restore
the authored configuration, not a pending event or a past callback invocation.
The added installed C++ value header and semantics participate in the private SDK
fingerprint; rebuild modules against the matching kit. C ABI1 and existing scene
and prefab versions remain unchanged.

See [the usage guide](../manual/editor/input-messages.md) and the original
Gallery `CodeExamples/InputMessages.cpp` receiver.
