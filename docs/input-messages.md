# Action-triggered typed messages

`forge.input_message` is an optional Gameplay component. Its `enabled` flag,
`action` UUID text, `payload` component key and `trigger` unsigned value are
ordinary reflected authoring values. Trigger0 is Pressed,1 is Released and2 is
Changed. Missing trigger values admit as Pressed; existing documents keep their
identities and format versions. Scene, Prefab, generic property commands and their revision/history/file
owners remain authoritative. Project Settings owns input actions. A missing
selection is preserved and reported; selecting another action never rewrites IDs.

## Delivery and ownership

RuntimeSimulation latches RuntimeInput before the fixed pipeline. The immediate
`forge.runtime.InputMessages` system runs in `forge.runtime.Messages`, after
InputMonitor and before `forge.runtime.Gameplay`. The same SDK input borrow spans
these phases. Pressed and Released require digital actions; Changed also accepts
fixed Axis1/Axis2 actions. Control-frame actions never qualify. Pressed sends on
`pressed`, Released on `released`. Changed digital sends once when either edge
occurs or held changes; a quick press/release within one tick coalesces to one
message. Changed analog compares the conditioned fixed `x,y` value to its last
observation. Equal consecutive values do not repeat. Returning to neutral sends
one change, including relative input returning to zero; equal relative impulses
on consecutive ticks do not count as different values. RuntimeInput remains the
conditioning/latching authority. Inactive input contexts produce neutral input.
Pause/presentation/control-only frames do not send messages. Step runs the normal
fixed pipeline. Capture release, Pause and Resume retain existing neutralization.

The system considers active Scene members with Input Message, in durable EntityId
order, excluding native disabled/Prefab query rows and applying the existing
ApplicationStateActivation conditions. It resolves builtin schema keys through
their registered native types and project keys through WorldContext's admitted
authored codecs. The payload must already be attached, owned or inherited; the
emitter itself cannot be the payload. Unregistered opaque/runtime-only project
types are outside this authoring profile.

When triggered it reads the existing value codec, checks the bounded value,
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

## Changed-route lifetime

The first observation starts at neutral, so a new neutral route sends nothing.
The same durable source with a different native generation, action, kind, context
or payload begins a new observation. Trigger changes also begin a new route.
Inactive, removed, invalid-mode and omitted
sources lose their prior observation at the next fixed tick. Reactivation or
reattachment while input is nonneutral can send again. A missing payload reports
an error while still observing valid input; restoring the payload does not replay
a change that already passed. There is no callback retry or event retention.
Input-map replacement and rebinding use the existing input owner's neutralization;
Changed observes its resulting values. A new Play world starts neutral.

Each selected valid source retains its route identity and saturating dispatch
counter; Changed additionally retains its last fixed input value. A successful
dispatch retains the last tick, encoded byte count and at most2048UTF-8bytes of
its encoded payload as a diagnostic string. The existing UTF-8 range helper
preserves the boundary; JSON encoding still owns UTF-8 admission. The prefix has
its own bounded string instead of retaining the full serialization allocation.
Entries are pruned every tick and after an empty source query; persistent
observations fit the existing256-source selection. The map can transiently hold
old and newly selected rows until that tick's prune, at most512 entries. It keeps
no native payload copies or borrowed event pointers from earlier dispatches.
The retained encoded prefixes total at most512KiB after pruning, or1MiB in the
transient old/new map. Copied status/pending previews have separate lifetimes.
These are encoded-byte bounds, not native allocator or execution-time guarantees.

## Bounds and observations

At most 256 sources are considered per tick, including inactive or unassigned
ones. Extra sources are reported in `overflow`. Each admitted native-storage size
plus encoded known value must fit 64 KiB; aggregate captured sizes fit 4 MiB per
tick. These are value-envelope bounds, not a measurement or bound on native
allocator/observer internal memory or execution time. Invalid values and routes
produce per-source diagnostics without sending that source. Other admitted
sources continue. There is no asynchronous dispatch or observer-time preemption.

`RuntimeSimulation::input_status().messages` reports `tick`, `emitted`, `emitted_total`, `last_sent_tick`, `overflow`
and bounded `sources`, each with its durable EntityRef, configured action/payload/trigger,
state and optional diagnostic. `Sent` means the event was dispatched; it does not
prove that a receiver existed. Console > Gameplay input displays this observation.
The cumulative sent counter saturates at uint64 maximum and survives idle ticks,
so a brief dispatch remains observable between editor frames. It resets with the
runtime, retains no event value and cannot replay a callback. The same copied
status is available through existing Play transport and
`forge_play_input`. Generic authoring tools plus `forge_play_input_submit` cover
configuration and testing; no separate agent implementation is added.

Each source row additionally publishes `name`, valid `action_name`, its own
`emitted_total` and nullable `last_sent`. A last-sent record contains `tick`,
`encoded_bytes`, `preview` and `truncated`; the top-level `preview_limit_bytes`
is2048. A complete prefix is ordinary codec JSON, including a custom component's
schema envelope. A truncated prefix is text, not a parseable value or replay
record. The preview is captured alongside native storage before observers run,
then published only after dispatch returns. Missing/invalid payloads cannot
publish a new successful preview. A valid unchanged input route can retain its
earlier preview while reporting a payload error.

Console Gameplay input expands these same copied source rows. No presentation
read advances time or modifies source/history. Counts/previews reset on route
identity change, native source generation replacement, deactivation/pruning or a
new runtime. Input action renaming retains identity. Global sent totals continue
to describe the current runtime. Neither source diagnostics nor preview text
persist in Scene/Prefab, native recovery or game recordings.
The existing exact SDK contract includes the message implementation, so this
diagnostic extension changes its fingerprint without changing public value layouts
or the callback table. Rebuild project modules against the matching kit.

Runtime delivery never edits the separate authoring World or its history. A new
runtime starts with no message history; recovery and scene replacement restore
the authored configuration, not a pending event or a past callback invocation.
The added installed C++ value header and semantics participate in the private SDK
fingerprint; rebuild modules against the matching kit. C ABI1 and existing scene
and prefab versions remain unchanged.

See [the usage guide](../manual/editor/input-messages.md) and the original
Gallery `CodeExamples/InputMessages.cpp` and `InputMessageTriggers.cpp` receivers.
The latter reads `sdk::Client::action` inside the typed observer to use the same
fixed value. Payload fields are not automatically replaced with input values.
Input access is an intrinsic fixed-phase capability queried at runtime; it is
not a module descriptor permission bit.

Reachable Scene/Prefab/variant declarations with nondefault triggers require
`forge.runtime_document.runtime_features.input_message_triggers:1` and runtime-kit
`engine.runtime_input_message_triggers:1`. The package owner derives this fact
from saved components and property overrides; admission rejects missing, malformed
or unsupported profiles before using a kit. Absent/Pressed0 values add no new kit
requirement. Exact SDK fingerprints separately protect module layout/semantics.
