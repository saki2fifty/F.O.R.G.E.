# Application states

[ADR030](decisions/030-application-states.md) defines runtime ownership. Application
states describe a game's logical mode independently of its fixed clock, animation,
navigation, input contexts and entity lifetimes.

## Definition and source

An optional **Application States** component contains one typed `definition` asset
reference. One configured non-Prefab controller is admitted per runtime Scene.
An unassigned component makes no resource demand. Each newly constructed World
uses the admitted definition; changing the controller or definition requires a new
World. Runtime state never replaces authored initial values.

Editable `*.appstates.json` files use format `forge.application-states`, version1,
and an `asset_id`. `domains` is an ordered array. Each domain contains `key`,
optional `label`, `initial`, `states`, optional `computed`, optional `enabled_when`
and optional `rules`:

```json
{
  "format": "forge.application-states",
  "version": 1,
  "asset_id": "72ea6436-b13c-451f-a25a-c11210890aa3",
  "domains": [
    {
      "key": "app",
      "initial": "menu",
      "states": [
        { "key": "menu" },
        { "key": "game" },
        { "key": "playing", "parent": "game" },
        { "key": "game_over", "parent": "game" }
      ]
    },
    {
      "key": "hud",
      "initial": "hidden",
      "computed": true,
      "states": [{ "key": "hidden" }, { "key": "visible" }],
      "rules": [
        { "state": "visible", "all": [{ "domain": "app", "state": "game" }] }
      ]
    }
  ]
}
```

Keys are semantic identifiers, not EntityIds: 1–48 ASCII characters, beginning with
a lowercase letter, followed by lowercase letters, digits or underscores. Labels
are optional literal UTF-8, at most96bytes. Domains1–16; states1–64/domain,
512total; parent depth16. Source512KiB, JSON depth10/events24000. Duplicate keys,
unknown references, hierarchy/dependency cycles, NUL, invalid UTF-8 and nonfinite
numbers reject before publication. Unknown extension fields remain opaque.

A direct domain accepts queued requests. Its active leaf also belongs to all its
ancestors. Selecting a parent directly activates that named state; it does not
choose a child automatically. A computed domain evaluates ordered rules and uses
the first matching rule, otherwise `initial`; direct requests reject. A rule has
1–8 AND conditions. There are at most64rules/domain and256total. Each condition
matches another domain's active leaf or ancestor. `enabled_when` makes a domain
subordinate: while the condition is false it has no active state; when reactivated
it starts at `initial`. Dependencies are evaluated before dependents.

`ApplicationStatesDocument` uses AssetSourceDocument for revisions, edit validation,
Undo/Redo, disk baselines and publication. Semantic editor renames update recognized
references inside that source only. External C++ and RML strings are not rewritten.
Importer `forge.application.states`, recipe `forge.application.states.v1`, publishes
one `application-states.json` through the shared catalog/import/cache owners.
Metadata `forge.application_states` and exact selected artifact/source/provenance
must agree. Runtime and packaging share this admission. Runtime reads selected
cooked data rather than the editable source. Resource kind9 uses the existing
RuntimeResourceService and a lazy4MiB typed pool.

## Native state and update boundaries

`ApplicationStateActive` is an Exclusive pair on each internal domain entity.
`ApplicationStateMember` derives leaf/ancestor membership. Exclusive pending
`ApplicationStateRequested` pairs merge last-wins per domain. The immutable compiled
configuration lives in the same World; no second clock, scheduler or game session
is introduced. World retirement is silent.

The runtime advances pending state before fixed gameplay and at paused host-control
boundaries. All active/membership pairs and revisions publish before explicit
`ApplicationStateExited`/`ApplicationStateEntered` notifications. Exit walks from
old leaf toward the common ancestor; entry walks down to the new leaf. Observers
see the complete new configuration. Their requests wait for the next boundary.
Recursive host transitions reject before mutation. An unchanged leaf does not
increment its revision or emit lifecycle notifications.

Host-only `application_states_status(world, choices)` returns owned copied values;
`control_application_states` validates domain/state and optional expected revision
before sharing the normal transition owner. Immediate host requests overlay the
copied pending plan; complete derived-state and revision-counter validation occurs
before native mutation. Rejected input preserves prior pending intent. The native module API installs
only value/ECS helpers, not unexported host functions. Project code treats runtime
pairs as read-only.

The exact SDK includes `application_state_ecs.hpp`: semantic schema entities,
readonly `application_state_current`/`application_state_is`, relation tags and
owning lifecycle event values. Read helpers resolve existing native relations
without creating entities or depending on a module-local C++ registration cache.
Ordinary fixed-query terms can use a constant domain source and a Member target to
condition systems. These queries do not disable other engine subsystems.

## Stock activity conditions

The optional `forge.application_state_gate` builtin (`ApplicationStateGate`) has
`enabled:false`, `domain:"app"`, `state:"playing"` defaults. Inspector labels are
**State Condition**, **Use condition**, **Domain key**, **State key**. Key syntax is
always validated. An enabled runtime value also requires the configured domain
and state; a disabled value does not require semantic membership or a controller.
Computed and subordinate domains use the same existing Member relationships.

The host-only `ApplicationStateActivation` captures effective conditions, including
native Disabled rows, validates after releasing its native iterator, and resolves
ChildOf ancestry iteratively with AND. It supports native IsA inheritance and
owned overrides. It is an ephemeral owner-thread view, consumed before relevant
world mutation and discarded; membership pairs remain the authority. There are
at most 10,000 effective values and 10,001 structural traversal nodes. Authoring and
validation roles bypass runtime eligibility. There is no generic C++ system filter.

| Owner | Inactive condition behavior |
| --- | --- |
| Presentation | Preserve all effective Scene/transform rows, mark inactive runtime rows `state_active:false`, omit their render components, cameras and lights. Plain authoring projections stay editable. |
| Physics | Exclude bodies/controllers/joints from desired admission. Joints also require active endpoints. Tickless retirement removes ineligible admitted objects and their targeted commands while retaining survivor poses/commands and pending2D contact events. New eligible consumers realize on normal synchronization; solver velocity is not retained across removal. |
| Audio | Filter copied sources/listeners through the same view. Existing source retirement/recreation applies; detached one-shots remain independent. |
| UI | Compose snapshot admission and current input validation. Deactivation retires document incarnations; reactivation creates a fresh one. Global state/localization configuration is not gated by its controller entity. |
| Animation | Use existing effective enable semantics in skeletal playback and its recovery comparisons. Transform players, sequence clocks and dependent tracks use their existing disable/restart paths. Camera shots and cue admission also require eligible controllers/targets. Past cues are not buffered for reactivation. |
| Particles | Filter copied emitter candidates before active capacity checks; existing removal/restart behavior applies. |
| Navigation | Filter source geometry, obstacles, agents and behavior execution. Keep inactive spatial ancestor transforms while suppressing their movement authority. Existing behavior intent release and recovery rules apply. This does not rebake a NavMesh. |

Tile rendering follows the same admission; its animation retains the existing
global visual clock. Asset preparation/package closure retains future-active
dependencies. The condition does not intercept arbitrary project C++ code,
external animation targets, explicit cross-object writes, resource ownership or
the global simulation clock.

The existing host state-retirement callback runs on changed state configuration,
outside native event/defer and before lifecycle notifications. It composes narrow
physics retirement with audio source refresh. Physics keeps only an adoption
receipt when eligibility exceeds the admitted realization; ordinary recovery
returns null until normal synchronization completes. This does not serialize
condition truth or advance time to manufacture a checkpoint. Existing pending2D
event readiness still applies. Other stock consumers reconcile at their normal
capture/synchronization boundary.

`Scene::tune_runtime_property` validates a detached complete condition, including
enabled references, before native apply. `RuntimeWorld` then uses the same
tickless retirement. Both Play hosts, Inspector and `play.tune`/MCP use those
owners. Discovery exposes the reflected builtin, so component add/property edits,
history, save and Prefab overrides use normal authoring operations. Definition key
renames do not silently migrate strings stored in Scene/Prefab conditions.
The installed SDK exports the value header; the activation view is host-only.
Matching fingerprint rebuild is required; no C service-table field is added.

Gallery135 Activity Lab exercises direct/ancestor/computed conditions with stock
animation, physics, particles, audio and native UI, including tickless state
changes. Its test reruns a relocated export after the original sources are hidden.
This is local native execution, not physical Windows or full-editor acceptance.

## SDK state requests

`forge::sdk::Client::request_application_state(domain,state,optional_revision)`
queues the existing Game request operation and returns its receipt token, or0
when unavailable/invalid. Inspect/release the receipt through existing Game APIs;
submission is not completion. Game capability is available in shared-SDK Play and
the standalone GameSession host. Legacy exact-SDK Play has no Game service: its
native queries/events and host/editor state controls remain available, while this
queued helper returns0. No C host-table layout is added.

## Editor, automation and game UI

The source editor shares source-document/history/import owners. Scene attachment
uses one authoring command group and ordinary Scene Undo/Redo. Play locks authored
source edits. Game-view selectors use copied current choices/revisions, release
physical gameplay input through its owner and submit through PlaySession's existing
serial live-command slot. Computed and inactive domains are read-only.

`play.application_states` observes the current launch/session/World's copied state.
`play.application_state.request` requires execution permission, current project,
launch/session/generation, document revision/sequence and **domain_revision**.
The latter is separate from the document's `expected_revision`. Completion/error
is visible through Play status. Stale runtime rejections are nonfatal. Both private
Play transports use the same core generation/domain-revision validator.

Runtime UI adds an optional copied `application_states` descriptor, with controller,
source identity, definition digest and bounded observations (16domains,24KiB).
It excludes the full editable definition/choices. The existing64KiB snapshot cap
still applies. One extra action slot is reserved for configured `forge.state`;
existing custom action/model capacities remain. A configured custom action or
`forge_state_`/`forge_rev_` binding collision rejects that configuration. Unconfigured
projects retain their existing custom actions and model bindings.

RML gets readonly `forge_state_<domain>` (leaf key, empty if inactive) and
`forge_rev_<domain>` (exact decimal string). For example:

```html
<p>{{forge_state_app}}</p>
<div data-if="forge_state_app | state_is('app','game')">In game</div>
<button data-event-click="command('forge.state', 'app:playing:' + forge_rev_app)">Play</button>
```

`state_is` tests copied leaf/ancestor membership. The command's bounded
`domain:state:revision` string passes the existing session/generation/receipt gate
and active document incarnation/visibility checks, then the same core owner.
Stale/computed/inactive/unknown requests reject without mutation. State-only changes
dirty native bindings while retaining context and focus. Controller/native-handle/
definition changes renew input scope and require complete candidate admission.
The presenter reports observations from its displayed set, independently of an
unpublished/failed replacement. UI controls work while paused without a fixed tick.

## Explicit runtime entity lifetimes

The existing runtime entity/Prefab queue can bind a published root to an active
state or ancestor. Use `Client::scope_runtime_entity(creation_request, domain,
state, optional_revision)` after `inspect_entity` reports Ready. It queues an
`entity_state_scope` Game request; inspect its receipt before releasing creation
observation. Pending, foreign, released or Gone tokens reject. The binding lives
on the native root as an Exclusive `ApplicationStateLifetime` pair and survives
observation release. A different binding rejects; repeating the same one succeeds.

The same owner accepts a current EntityRef for Play automation. Only roots
published by the runtime creation owner qualify; saved Scene entities, arbitrary
native entities and Prefab templates do not. Domain/state existence, active
ancestor membership and optional unsigned domain revision validate before writes.
Computed and subordinate states are eligible when active. This does not advance
time or change authored history.

On transition, all new state pairs/revisions publish first. Roots bound to exited
states are then snapshotted and retired using normal native structural destruction
before custom exit/entry events. Descendants and Prefab members follow their root;
common ancestors preserve roots across sibling transitions. Reentry does not
recreate anything. Later runtime publication uses Scene's existing projection of
live members and cannot resurrect the retired group. No entity registry or second
hierarchy owns this lifetime.

The runtime consumer boundary then retires destroyed native physics bodies,
constraints and characters through the existing physics owner, and refreshes
audio source voices. This also runs during paused controls, so physics recovery
does not retain a ghost body. Only commands targeting destroyed entities are
discarded; survivor commands, velocities and poses are not adopted or advanced.
The 2D owner retains its existing bounded retirement/contact-generation records.
Detached audio one-shots keep their independent lifetime. User exit/entry observers
run after this synchronization. Native consumer/observer faults retain the normal
runtime failure policy; they are not a general rollback transaction.

`play.application_state.scope` takes a runtime root EntityRef and the same current
Play scope/generation and `domain_revision` requirements as state requests. It
uses the same live-command slot and both runtime transports. Native Game requests
are available in shared-SDK Play and standalone; legacy Play has no Game service.

## Recovery and limits

Ordinary Play's exact-build recovery2 stores an optional private state checkpoint1:
semantic domain/active/requested/revision rows bound to admitted asset and complete
recognized definition digest. Restore validates everything before mutation, precedes
initialization and emits no lifecycle replay. A missing checkpoint is compatible
only with an unconfigured World. SDK Play retains its existing `recovery:null`
policy. These records are not a durable game-save schema.

Ordinary recovery also carries optional `runtime_entity_lifetimes` version1 with
at most 65,536 created-root EntityRefs and optional semantic scope objects. The
runtime creation owner validates all references, duplicates, active scopes and
fresh bindings before publication. Root markers restore even for unscoped roots;
request tokens and arbitrary C++ state do not. A missing list means an older
checkpoint has no recoverable root markers or lifetime bindings.

The existing adopted-physics checkpoint contract remains intact. A 2D retirement
retains native shape/End-event records until the next fixed adoption. During that
window `PhysicsRuntime::checkpoint_ready()` is false and ordinary Play reports
`recovery:null` while the valid control succeeds. Step or the next running tick
completes adoption and permits a current checkpoint. No partial record is emitted,
no transient event is discarded to manufacture readiness, and no time advances
merely to create a checkpoint. Other checkpoint validation still applies.

Stop/new Play restores initial states. Keep values does not author runtime state.
Implicit membership-based disabling, automatic reconstruction of retired
entities, general event-driven transition scripting and durable state saves
are outside this bounded profile. Platform acceptance and current test evidence
are recorded separately; this document does not assert physical Windows testing.
