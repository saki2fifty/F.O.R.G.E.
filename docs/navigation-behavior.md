# Navigation behavior graphs

[ADR028](decisions/028-navigation-behaviors.md) defines ownership and execution.
The [user guide](../manual/editor/navigation-behavior.md) covers the editor workflow.

## Source and identity

`*.behavior.json` is format `forge.navigation-behavior`, version1, with asset_id,
entry, next_id and states. Each state has positive uint32 id, unique 1–64-byte name,
action and transitions. Each transition has id, to and condition. State/edge IDs
share one monotonic allocator; document Undo/Redo retains its high-water mark.
Ordinary source-document atomic edit/history/disk-baseline rules apply.

Action kinds are wait, move_to_point (point[3]) and follow_target. Conditions are
elapsed (seconds), arrived, target_near/far (distance), and target_missing.
Bounds:32states/128edges/8outgoing/64KiB, nesting12/events12000; points±4096,
seconds0–3600, distance0–8192. Native DOM edits must survive bounded JSON round-trip.
Unknown extensions remain opaque. Recognized optional editor_layout has format
forge.navigation-behavior-layout/version1 and state/x/y positions; it is source-only.

`NavigationBehaviorDocument` uses the shared AssetSourceDocument owner. Importer
forge.navigation.behavior / revision forge.navigation.behavior.v1 publishes one
behavior.json cooked file through the ordinary import/publication/cache/catalog
owners. Metadata forge.navigation_behavior records version/file/hash/bytes.
Runtime and packaging share artifact admission: exact type/identity/recipe/selection,
source/artifact hashes, bounded content and metadata must agree. Runtime access
never reads the editable source. Graph assets embed no Scene EntityRefs.

## Runtime, resources and recovery

NavigationBehavior holds graph, target, anchor and enabled. Target/Anchor are ordinary
typed EntityRefs; existing Scene/Prefab reference conversion is authoritative.
Flecs owns runtime-only, DontInherit NavigationBehaviorCursor (state, last_transition,
elapsed, transitions). Private playback records retain resource leases, diagnostics,
issued intent and a transient frame candidate; they do not own another progress clock.

The existing RuntimeResourceService supplies kind8 and a lazy bounded typed graph
pool,8slots/1MiB. Matching subscribers retain one admitted graph; releasing a token
does not invalidate other subscribers or copied leases. No new worker is started
without graph demand. Explicit refresh adopts a complete selection. Changed cooked
content resets progress; layout-only publication retains it. Failed replacement
retains a complete predecessor with an observation warning.

NavigationRuntime integrates64controllers/eightconfigured graph IDs per Scene.
The begin boundary snapshots resolved target/anchor/actor transforms and applies
current-state intent before agent movement. The finish boundary consumes that
tick's actual agent arrival, advances the sole cursor once, then stages the next
state's intent. At most one ordered transition runs per positive fixed tick.
Evaluation never waits for resources and never duplicates navigation arrival logic.
Startup/unpublished recovery owns bounded readiness.

Adding a controller or resetting a graph inside a deferred fixed phase holds its
intent until Flecs merges the cursor storage. That merge, rather than a shadow
cursor or suspended defer scope, makes the new progress visible.

Disabled controllers hold progress and release only still-matching issued intent;
later native destinations remain intact. Graph identity change/removal resets
progress. Invalid poses/configuration/actions stop the affected intent while healthy
neighbors can continue. A global controller/graph limit prevents the navigation step.
XY ignores target visual depth and local point Z; final actor depth is preserved.

No-behavior private navigation checkpoint2 remains unchanged. Checkpoint3 adds
bounded behavior rows and exact configuration/ownership/progress/revision/hash/
issued-intent admission. A partially active frame cannot be captured/restored.
Optional host recovery is unavailable while a behavior resource is pending or
only a superseded last-good revision is retained. Live playback may continue on
that retained graph; no partial or unrestorable checkpoint is published.
Legacy recovery into a behavior-bearing Scene rejects. This is process recovery,
not a durable save-game schema or arbitrary native-state serialization.

## Editor and programmatic access

NavigationBehaviorEditor and AnimationStatesEditor share source-window lifecycle
and state-canvas gestures; each keeps its own action/transition semantics and
AssetSourceDocument validation. DocumentWorkspace owns routing, dirty guards,
Save/Undo/Redo and source automation binding. Content, CLI import, reference impact,
Duplicate/Move and source-free packaging use the established asset owners.

The same source adapter exposes template/create/read/patch/history/publication to
automation/MCP. asset.behavior.catalog describes actual bounded operations. Ordinary
component/property commands author graph/target/anchor/enabled with shared Scene
history and references. Readonly navigation_behavior_debug reaches Play presentation
and Inspector; enabled-only live tuning uses NavigationRuntime::tune and the existing
grant/session/generation/keep-values review. Runtime cursor is never authored data.

The matching SDK includes the small component/cursor header and resource-kind8
constant. The engine-side graph codec is not installed as an SDK binary API. C host
table layout, dependency pins, navigation movement and scheduler ownership remain
unchanged. This is a bounded navigation-state profile; behavior trees, generic app
states, arbitrary graph scripting and durable behavior save games are outside it.
