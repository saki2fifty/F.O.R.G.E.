# ADR 017 — Coordinated Apply to Prefab

Date: 2026-09-28. Authorized in the complete Phase 9 work package.
Implementation and acceptance evidence are pending.

## Ownership and scope

PrefabLibrary prepares source and scene intent. Scene retains the one scene Undo
history. SceneDocument coordinates durable publication with the existing
AssetFileTransaction journal and project writer lease. No general cross-document
transaction framework or additional history owner is introduced.

Apply transfers admitted component/property and name override intent from the
selected instance into its existing prefab members. Equal-value intent transfers
as well. Independent local TRS channels remain independent. Opaque/unadmitted
values stay on the instance; they are never interpreted or rewritten. Structural
attachments, member creation/removal, spatial binding and unknown payloads are
not inferred from a scene instance. Their authoring remains in the source editor.

## Publication and recovery

A named, existing scene is required. Apply explicitly saves the current scene,
including dirty edits. Review shows this before confirmation. Prepare immutable
source revision and reconcile/validate every affected instance before any write.
Extend the existing file journal with a strictly typed two-file prefab-apply
record: prefab first, scene last as durable commit point. Recovery before scene
load rolls back pre-commit or validates the complete committed pair. External
conflicts are preserved and reported; neither file is overwritten on conflict.

## History

One Scene history entry identifies the coordinated operation. Undo/Redo prepares
and publishes both documents through the same path, preserving source revision
monotonicity rather than rewinding revision numbers. Current scene history target
is reconciled with the fresh revision. Ordinary scene Undo remains memory-only.
Direct source Publish retains its existing history boundary and clears affected
scene history. An external source or scene change makes coordinated Undo/Redo
reject without consuming the history entry. History is session-local, never a
persistent plugin protocol. Dirty edits after Apply can be undone first normally;
reaching Apply writes the pair again. Scene switches discard session history.

## Compatibility

No scene, prefab, identity, ABI or dependency format changes. A narrowly typed
version 2 recovery record extends the private existing asset-file journal.
Version 1 catalog-operation recovery remains supported. New Apply behavior
supersedes the deliberate Phase 5 deferral; direct-source editing is unchanged.
