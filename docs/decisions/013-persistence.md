# ADR 013 — Persistence domains

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Authored Scene/Prefab/asset documents preserve UUIDs, unknown payloads, component
ownership and explicit override intent. Recovery checkpoints are ephemeral, exact
runtime-compatible state, not save games. A future save-game schema and network
replication schema each need explicit version/authority/remapping semantics.

## Current evidence and implementation boundary

scene-format.md, prefabs.md, transforms.md and runtime-timing.md describe current
formats. Phase3 TRS is separate Flecs components; WorldTransform is derived. Structured
prefab revisions are immutable and reconciled before publication.

## Consequences

No automatic legacy prefab migration. Revert removes intent and is scene Undo;
direct source publication is a separate history boundary. Future Apply requires
coordinated asset+scene publication, recovery and honest Undo semantics. Nested prefabs
and variants must define cycle/identity/override composition before enabling them.
Runtime-generated entities have no authored UUID unless explicitly promoted.

## Deferred work and exact trigger

Scene composition adds transient instance scope at simultaneous instantiation,
not persistent DocumentId. Save games begin when real resumable-game requirements
exist; networking begins with replication ownership/time/ID contracts. Neither may
serialize raw Flecs IDs or reuse checkpoint bytes as a durable public format.
