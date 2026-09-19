# ADR 006 — Asset candidate pipeline

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Freeze source→detect/register→import→validate→process/cook→immutable artifact→catalog
selection→runtime load. Importers declare source/output types, immutable tool version,
settings schema, dependencies, determinism, platform support and resource limits.
Import, processing, cooking, validation and publication are different roles. A failed
or stale candidate cannot replace last-good output or advance the catalog selection.

## Current evidence and implementation boundary

AssetCatalog, ProjectPaths and existing animation/navigation/Script worker supervisors
provide concrete containment/candidate patterns. The full importer registry and cooker
are deliberately not implemented in this package.

## Consequences

Derived cache lives under project .forge/cache. Key includes source/settings/tool/
transitive dependency digests, platform and backend/profile where relevant. Hash output,
validate its type/format before reuse; quarantine corrupt entries and rebuild on miss.
Retain in-use revisions during eviction. Nondeterministic outputs cannot pretend to
be reproducible shared cache entries.

## Deferred work and exact trigger

Phase 7 implements one dependency graph, reverse invalidation, cycle rejection,
topological rebuild and generation-checked publication. Workers declare timeout,
file/memory/output bounds and cancellation; stage under .forge/jobs and publish only
after exit/validation. Trusted trivial pure transforms may stay in-process. No new
importer may invent a private identity or dependency graph.
