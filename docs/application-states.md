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

## Recovery and limits

Ordinary Play's exact-build recovery2 stores an optional private state checkpoint1:
semantic domain/active/requested/revision rows bound to admitted asset and complete
recognized definition digest. Restore validates everything before mutation, precedes
initialization and emits no lifecycle replay. A missing checkpoint is compatible
only with an unconfigured World. SDK Play retains its existing `recovery:null`
policy. These records are not a durable game-save schema.

Stop/new Play restores initial states. Keep values does not author runtime state.
Automatic physics/render/audio/entity gating, automatic state-scoped entity
destruction, general event-driven transition scripting and durable state saves
are outside this bounded profile. Platform acceptance and current test evidence
are recorded separately; this document does not assert physical Windows testing.
