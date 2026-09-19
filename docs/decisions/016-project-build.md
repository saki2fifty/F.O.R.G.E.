# ADR 016 — Project, source control and build profiles

Date: 2026-09-19. Decision frozen for review in Build260919-000063; automated package
validation is complete. Future implementation requires its own authorized scope.

## Decision

Shared project files contain gameplay intent, source assets, sidecars and stable
module/asset references. Personal layout/tool/SDK paths remain machine-local. .forge
contains reproducible disposable caches/jobs/native outputs; source control excludes
these. Build identity is UTC date plus monotonically allocated counter, independent
of schema/IPC/SDK version.

## Current evidence and implementation boundary

ProjectPaths, project.hpp, SceneDocument and native build staging already separate
shared data from session state. CMake distinguishes static ABI1 and shared exact SDK;
SDK package tests verify relocation and shared-library dependency identity.

## Consequences

Profiles are editor development, exact SDK development, shipping visual runtime
and headless server. Current implementation supplies the first two plus headless
runtime foundations, not a production exporter. Shipping strips editor/compiler/
import utilities and disables development REST unless explicitly required.

## Deferred work and exact trigger

Asset move/rename preserves AssetId and updates contained locators atomically;
delete checks reverse dependents, produces missing diagnostics and retains recoverable
metadata/revisions. Phase 7 implements this through the common catalog transaction,
not per-importer filesystem tricks. Source-control integration later reports external
conflicts and never silently overwrites a newer disk revision.
