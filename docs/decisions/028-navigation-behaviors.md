# ADR028 — Navigation behavior graphs

Date: 2026-11-01. Accepted.

## Ownership

Navigation Behavior chooses Navigation Agent destination intent. It never writes
transforms or replaces pathfinding, animation, physics, Scene references or the
fixed pipeline. NavigationRuntime owns playback integration. Flecs owns authored
configuration and runtime-only cursor progress. Private records retain leases,
diagnostics and previous intent for release. This follows ADR001/012/013/022 and
the existing navigation asset/query decisions.

Graph sources use existing source-document history, import, immutable publication,
catalog and typed resource pools. Stable state/transition IDs share a monotonic
allocator. Recognized source-owned card positions are removed during cooking.
Target/anchor EntityRefs live in the Scene component, so graph assets never embed
Scene addresses. Existing prefab reference conversion remains authoritative.

## Execution and limits

The first profile supports wait, point movement and following a target. Ordered
transitions test elapsed fixed seconds, matching agent arrival, target distance or
absence. At most one transition executes per positive fixed tick; entry resets
elapsed without a catch-up loop. The navigation update snapshots transforms and
applies the current state's intent before agents move. It then evaluates transitions
using that tick's navigation arrival observation. A new state's intent is ready for
the next movement step. This keeps arrival authority in navigation and makes recovery
independent of an old observation cache. Disabled behavior holds its cursor and releases still-matching issued intent.
Removal or changed graph identity resets progress. Invalid inputs stop the affected
controller and report a diagnostic.

The profile bounds32states/128transitions/eight per state,64controllers and eight
configured graph identities per Scene. The existing RuntimeResourceService owns catalog selection, a lazy typed pool,
subscriptions and complete retained leases. Fixed ticks never wait for graph
preparation. Explicit resource refresh adopts cooked revisions; changed cooked
content resets progress, while layout-only publication preserves it. Failed
replacement retains the previous complete graph with a warning. There is no
implicit source import or second catalog. Startup/recovery use unpublished-world
readiness.

## Persistence and access

Existing Scene/Prefab formats retain their meaning; Navigation Behavior is optional
reflected data. Runtime cursor progress is not authored or inherited. Behavior-bearing
navigation checkpoints require a private extension with exact graph/configuration
validation; no-behavior checkpoint2 stays unchanged. Recovery is distinct from save
games and arbitrary C++ state.
Optional host recovery remains unavailable until required graph selections are
current and admitted. A retained predecessor may continue live playback after a
failed replacement; it cannot authorize an unrestorable checkpoint.

Editor and automation/MCP share source-document/component operations. Observation
extends runtime presentation while preserving path fields. The small public component/
cursor header enters the matching SDK fingerprint; engine-side graph parsing/cooking
is not an SDK binary API. No C host table, dependency pin, scheduler or movement
service changes. This first profile is a bounded navigation-state workflow;
the tree profile below extends the same owners. It is not a complete AI suite. Completion and platform acceptance
require actual consumers and evidence independently of this design decision.

## Additive tree profile decision

The accepted tree capability deliberately extends the state-only boundary above.
It uses the existing behavior asset, source-document, resource, intent and Flecs
progress owners. Source version 2 identifies tree nodes and ordered children;
version 1 retains its state/transition syntax, semantics and exact cooked bytes.
The unchanged cooked envelope version 1 can contain either admitted source version.
New source-version inputs have distinct content keys; extending formerly rejected
inputs does not replace any successful version-1 recipe output or asset identity.
Future unknown versions still reject. No existing graph is migrated automatically.

The finite tree profile has ordered memory Sequences, Reactive sequences, reactive Selectors, timed
Wait, Move to point, Follow target and target-distance/absence conditions. Nodes
have one parent, stable monotonic IDs, at most eight children and depth eight;
publication requires a connected tree of at most 32 nodes. Source drafts can
contain disconnected nodes and unfinished composites for ordinary canvas editing.
Cycles and ambiguous parents reject before publication.

Each positive fixed tick selects one running action before movement, then consumes
only that action's matching arrival/failure observation after movement. Composites
may examine at most 32 nodes at each boundary; a subsequent timed action receives
no elapsed time from the preceding action's tick. Selectors reconsider earlier
branches; Reactive sequences recheck their earlier conditions. A preempted branch
loses its continuation. Root completion restarts at
the next positive tick, without an in-tick repeat loop. Selection/presentation do
not advance the sole cursor clock. Disable holds progress and releases only
still-owned intent, following the existing lifecycle policy.

Tree continuation and outcomes belong to the existing runtime-only Flecs cursor;
no parallel playback clock or progress database is introduced. Its appended native
fields require a matching exact SDK. Recovery must validate the selected graph,
active path and continuation before adopting a candidate. This decision describes
the tree integration contract; parser scaffolding alone is not a shipped workflow.
