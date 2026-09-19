# ADR 001 — Flecs ownership

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Keep Flecs entities/components/tags, pairs, ChildOf/Parent, IsA, queries, observers,
Meta, module imports and fixed-pipeline systems as the gameplay authority. FORGE
owns persistent UUIDs, asset revisions, authoring candidate transactions and
cross-domain validation; these are not replacement ECS mechanisms. WorldTransform
is instance-owned derived state, never inherited or authored. Native module code
uses the runtime's single shared Flecs instance, never a private static copy.

## Current evidence and implementation boundary

src/world.cpp, src/scene.cpp, src/runtime.cpp and src/native_sdk.cpp; Flecs exact
pin include/flecs.h, src/entity.c, src/instantiate.c and addon pipeline sources.
World lifetimes and borrowed SDK ownership are already implemented.

## Consequences

Do not turn editor tabs, mesh vertices, graph nodes or GPU resources into a second
GameObject hierarchy. Do not replace working native behavior merely for uniformity.
Keep current custom spatial evaluation: effective spatial parenting is deliberately
independent of structural ChildOf and includes World/Explicit semantics.

## Deferred work and exact trigger

Add parallel system execution only after access declarations and subsystem safety
proofs; synthetic speedup is not permission. No generic ECS wrapper redesign is needed
for Phase 7. New native consumers must pass owner/staging/lifetime tests.
