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
