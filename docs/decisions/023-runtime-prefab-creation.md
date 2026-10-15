# ADR 023 — Runtime Prefab creation

Date: 2026-10-15. Implemented and validated locally. Windows physical acceptance
and a packaged delivery remain deferred.

## Decision

Extend the existing runtime entity request queue with a Prefab AssetId. Keep its
module/world ownership, Pending/Ready/Failed/Gone observations, cancellation and
owner-thread fixed-boundary publication. The checked exact-SDK table gains a tail
callback; matching SDK/module rebuilds are required and ABI1 remains unchanged.
No second spawner, scene owner, identity registry or resource manager is introduced.
RuntimeEntities remains an intrinsic service, not a descriptor permission bit.

A request selects one loaded Scene using the existing selection rule. The Prefab
must already be in that Scene's admitted sources. Detached SceneDraft preparation
uses the existing member map, reconciliation and template projection; one Scene
replacement publishes the group after validation. Invalid preparation publishes
no members. Arbitrary trusted native observer faults are not rollback guarantees.
Each request reserves its root UUID before publication. Child UUIDs are generated
by the existing instance reconciliation. These are ephemeral runtime addresses,
not promotion into an authored file or a durable save-game schema.

Source transforms and spatial policies are retained. The request overrides only
the root display name. Once Ready, C++ may set root/native components in Gameplay,
before the existing animation/physics phases. World-space children retain their
explicit source-world poses; parenting never implicitly relocates dynamic bodies.
There is no arbitrary JSON/component injection or implicit transform conversion.

## Bounds and lifetime

Empty and Prefab requests share 64/module and 256/world observation limits. At
most 4096 new rows are prepared per publication boundary; excess work waits for a
later boundary in request order. Release cancels a pending request, or discards
observation after creation without destroying entities. Scene/Flecs own realized
members and templates. Scene unloading makes the root observation Gone; a later
Scene instance with the same AssetId cannot retarget an old request.

Ordinary native C++ destruction of the root retires its structural group. Copied
Scene views omit the retired root and its members, so a later append cannot
resurrect them or produce orphaned missing-member intent. Read-only effective
projection reconstructs admitted owned custom values from native storage before
validating stored extension/stamp fragments. Existing property-mask baseline
comparison also covers ordinary builtin/custom masks in source1/2/3, retaining
C++ changes outside a mask during unrelated creation and private snapshots.
Unchanged fields remain inherited and incompatible envelopes remain opaque.

## Content and persistence

The existing AssetCatalog Runtime dependency graph supplies runtime-only Prefabs.
`load_game_scene` includes Prefabs reachable through required typed Runtime edges,
alongside directly instantiated definitions, with bounded traversal and ordinary
source admission. `ui.observed` is excluded as in export. Developers declare C++
asset selections using existing Runtime Dependencies; code strings are not scanned.
Play snapshots and standalone share the same read-only content preparation. SDK
Play retains the caller's scene edits and explicit definitions, then admits
additional declared Prefabs before candidate preparation; it never reloads the
saved Scene over unsaved edits. The existing8MiB SDK Play envelope bound also
applies after enrichment. Legacy snapshots retain their migration path. No
runtime import, source scan or catalog write occurs. Private runtime snapshots
retain already admitted definitions even
before their first instance, using Scene's existing source collection.

Request tokens and arbitrary C++ state are not checkpointed. Created supported
host-owned members use existing Scene/physics/animation recovery; a fresh world
does not inherit request observation tokens. Exact-SDK Play recovery remains
unsupported. Authored Scene/Prefab versions and source files are unchanged.
