# ADR 005 — Asset and subasset identity

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Retain UUIDv4 AssetId for every logical asset, including Scene and Prefab. Paths are
locators; digests are revision/cache identities, never logical identity. Scene AssetId
is its durable authored-document identity. EntityRef=(scene AssetId,EntityId).
A Model is one logical container asset with separately addressable Mesh/Material/
Skeleton/Clip subassets, each with its own AssetId and explicit dependency edge.

## Current evidence and implementation boundary

include/forge/identity.hpp, asset_ref.hpp, assets.hpp and current asset catalog.
Source metadata and scene/prefab identity already persist separately from process handles.

## Consequences

Record a sidecar source-element-key to subasset-AssetId mapping. Prefer a stable
source identifier; otherwise generate a durable mapping entry at first import and
reconcile by declared semantic evidence. Do not silently match ambiguous elements
by array index or filename. Ambiguity preserves old output and asks for remapping.
Duplicating a scene creates new scene/entity UUIDs and remaps only known local refs.

## Deferred work and exact trigger

Phase 7 implements sidecar reconciliation and importer dependency edges. Retain
removed subasset tombstones until explicit cleanup so existing references diagnose
missing targets. No persistent DocumentId or SceneInstanceId is added now; concurrent
runtime scene copies later need a transient world-local instance scope.

## Accepted source-evidence migration —2026-10-17

The user approved stable source evidence and an AssetId-preserving migration after
actual Windows reimport showed two-double-ULP transform rounding changes for
unchanged source. Model evidencev2 hashes admitted source transform numbers without
normalizing quaternions, decomposing matrices or multiplying evaluated transforms.
Names remain display data. Cooked Model format5 declares the evidence schema;
formats1–4 remain readable without reinterpreting their legacy evidence.

The existing Model publisher owns the bounded migration. Automatic reuse of a
selected binding requires identical source/include bytes, effective settings,
build dependencies and target; validated selected cooked files/catalog/sidecar;
and the same typed member selectors, bindings, hierarchy and animation channel
structure. Legacy automatic migration is limited to formats3–4 with their existing
node-member contract. Non-transform content/clip curves must agree. For unchanged
v2 reimports across tool revisions, all evidence must agree as well. Only this
verified identical-source revision can reuse its selected candidate-local bindings;
there is no index/name matching across changed source revisions.

Unproven legacy migrations use the ordinary explicit same-type review. Legacy
hashes are cleared only in a detached migration candidate, never interpreted as
v2 evidence. Publication preserves logical IDs, durable keys, removed tombstones
and opaque root/entry data; tombstones discard obsolete generated digests while
retaining stable exporter keys and remain restorable only by explicit choice.
An already published model with a missing identity sidecar rejects reimport until
that sidecar is restored. Unknown evidence versions reject.
The existing complete-family validation, compatibility check, concurrency checks,
recoverable writer and last-good retention remain mandatory. No second registry,
identity database or generic migration manager is introduced.
