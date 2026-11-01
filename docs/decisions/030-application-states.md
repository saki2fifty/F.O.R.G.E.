# 030: Application states use world-local ECS relationships

## Decision

Logical game states belong to the runtime World. They do not replace GameSession,
the fixed clock, animation states, navigation behavior or authored entity identity.
One optional application-state definition configures bounded named domains. Each
domain has named states, an initial state and optional parent-state relationships.
Names are semantic keys, not persistent EntityIds or native handles.

The exclusive `ApplicationStateActive` relationship on a world-local domain entity
is the active-state authority. Ancestor membership relationships are derived from
that pair. Project systems use ordinary Flecs query terms with a constant domain
source to condition their execution. No second scheduler or generic `run_if` API
is introduced. State conditions do not automatically disable physics bodies,
renderers, audio, entities or other engine subsystems.

Direct domains accept deferred requests. Computed domains choose the first matching
ordered rule, otherwise their initial state; they reject direct requests. A domain
may be subordinate to another domain/state condition. It has no active state while
that condition is false, and starts again at its initial state when reactivated.
Conditions match the active state or any of its ancestors. Dependency cycles and
invalid references are rejected before installation. Evaluation uses dependency
order, not declaration order.

## Boundaries and lifecycle

Deferred requests use an exclusive pending pair. For a domain, the last request in
native merge order wins; requests are not an event queue. Immediate host controls
overlay a validated request in the same copied transition plan. Complete transition
and counter validation precedes native writes, preserving existing queued intent
when input is rejected. An update snapshots pending
requests, computes the complete next configuration, clears those requests, then
publishes all active/membership pairs before notifying observers. Observer requests
remain pending until the next boundary. Reentrant updates/configuration are rejected.
An unchanged state does not emit another transition.

Exit notifications run from the old leaf toward its common ancestor; entry
notifications run from that ancestor toward the new leaf. Initialization emits
entry notifications. Domain declaration order determines notification order across
domains. Observers see the complete new configuration and receive the old/new leaf
and the particular entered/exited state. These notifications are explicit custom
events, not native removal hooks: world destruction is not a gameplay transition.

The existing runtime owns update boundaries. Pending requests are applied before
fixed gameplay, and paused host controls can apply them without advancing time.
Pause retains state; Step uses one fixed boundary. A newly constructed runtime
starts from its admitted definition. State transitions do not rewrite authored
initial values, and therefore are not captured by Keep values. Scene replacement
retires the old World's state. Private recovery captures semantic domain/state
values, revisions and pending requests, bound to the selected asset and complete
recognized definition digest. Restore validates the complete subordinate/computed
configuration before mutation and occurs before initialization, without replaying
lifecycle actions. Ordinary Play's exact-build checkpoint carries this optional
state; a missing field is compatible only when no definition is configured.
Prepared SDK Play retains its existing recovery policy. This checkpoint is not an
automatic durable save game; native IDs never cross a process boundary.

## Explicit runtime root lifetimes

An opt-in Exclusive `ApplicationStateLifetime` pair binds a root created by the
existing runtime entity/Prefab queue to a currently active state or ancestor.
The creation owner validates that the root is Ready, still belongs to the same
World/Scene incarnation, and is runtime-created. An existing binding is immutable;
repeating the same binding is idempotent. Authored entities and Prefab templates
are ineligible. Releasing creation observation retains both root and binding.

After publishing the complete next state configuration, the state owner snapshots
roots bound to exited states and invokes ordinary native structural destruction
before custom lifecycle notifications. Children and Prefab groups follow existing
Scene/Flecs ownership. Common ancestors retain their roots across sibling changes;
computed/subordinate exits use the same rule. World retirement remains silent.
This is one-way lifetime retirement, not automatic activation or reconstruction.

One host composition binding in the existing World state component connects this
boundary to the active RuntimeSimulation and follows its existing pipeline lifetime.
It runs outside native event/defer; consumer exceptions do not cross a native event
callback. Physics owns immediate dead-body/constraint/character removal,
its command cancellation and 2D retirement/contact bounds; it does not adopt
unrelated edits or move surviving bodies. Audio refreshes its existing source
owner. No fixed time advances, including during paused controls. Existing native
consumer/observer faults are not a general rollback guarantee.

Preserve ADR024's adopted native recovery boundary. 2D retired-shape/contact records
remain pending until fixed event adoption; readiness prevents an incomplete private
checkpoint from becoming a failed control reply. This does not discard events,
advance time, or change private physics formats.

The existing Game command queue accepts a module-owned Ready request token or a
current runtime root EntityRef. SDK uses the owned token; Play automation uses the
reference plus its normal generation/revision/permission gates. Private ordinary
recovery captures created-root references and optional semantic bindings separately
from request observations. It validates the whole list before restoring markers
and bindings; process-local request tokens are never restored. Older checkpoints
without this optional list remain compatible. SDK Play recovery stays unchanged.

## Scope and compatibility

The backend uses the existing pinned Flecs relationship, query and custom-event
APIs. Definitions and presentation observations contain owned values, with bounded
keys and graph sizes. Each World registers its own entities and configuration;
there is no process-global state store. Project code must treat active/membership
pairs as read-only and use the request API to change state.

Editor, UI and agent controls must use this same owner, source-document/history
paths and current runtime scope. Adding an optional controller and source format
preserves existing scenes. SDK-exposed C++ additions require the established exact
SDK fingerprint; the native C host table is not extended for this capability.

Implementation, validation and platform acceptance are recorded separately. This
decision does not assert that every consumer or persistence path is implemented.
