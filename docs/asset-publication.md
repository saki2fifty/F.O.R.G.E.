# Asset import publication

`AssetPublisher` is the authoring-side publication boundary for immutable import
outputs. It reuses `ProjectLease`, `AssetCatalog`, `AssetImporter`, the derived
cache, typed dependencies and subasset reconciliation. It is internal Phase7
infrastructure; a complete model/texture import dialog and runtime adoption path
are still being integrated. The runtime does not link this authoring service.

## Ownership and stored data

The caller keeps the project writer lease alive. Capture, publication and recovery
run on the thread that constructed the publisher. A worker returns candidate
bytes and metadata; it cannot publish a catalog or mutate a world/device.

A ticket captures the exact catalog and adjacent `<source>.forge-import.json`
sidecar bytes, distinguishing an absent file from an empty one. The sidecar is a
source-controlled `forge.asset-import` version1 document containing typed importer
settings, the durable member mapping, build inputs and retained unknown fields.
Do not put it in the disposable cache. AssetId remains logical identity; source
paths and importer-local array addresses never become persistent IDs.

The catalog retains its extensible version2 metadata format. The reserved
`metadata["forge.import"]` version1 selection records the build key, output-byte
manifest digest, importer revision, source/settings/dependency digests, output
format/version and target profile. A monotonic publication generation makes the
catalog commit point distinct even when only equal-value setting intent changes;
it is not persistent asset identity and never enters the content/cache key.
A removed member retains a tombstone and is never silently reassigned. Root and member records must agree with every durable
mapping entry; a prior entry cannot disappear merely because its source was
removed. Existing metadata absent from the new candidate is retained.

## Candidate checks

Before selecting a revision, the publisher checks:

- The exact importer implementation, output format, settings schema/effective
  values and supported platform/backend/profile.
- The source and every captured raw dependency, using bounded current file reads
  and content hashes. Up to512MiB/file,1GiB combined,4096 additional files.
- Required logical dependency type and selected revision; optional dependencies
  with an empty revision represent a declared fallback that the importer and
  compatibility preflight must validate.
- Complete root/member ownership, unique identities, mapping keys and tombstones.
- The artifact's sizes/hashes/manifest and the importer's format validator, using
  the same immutable DDC publication admission as other cache consumers.
- Runtime compatibility through a mandatory preflight callback. That callback
  prepares/checks candidate compatibility without switching a live resource.
- Source inputs, catalog and sidecar again after the compatibility callback.

Failed or stale candidates leave the selected catalog and sidecar unchanged.
They can leave an unselected immutable cache entry, which is safe for later
verified reuse or pruning. Runtime leases are adopted separately at the consumer's
safe frame/tick boundary; this publisher cannot promise arbitrary native rollback.

The current DDC enforces identical output bytes for identical inputs. A
nondeterministic provider cannot silently replace an entry under an existing key.
A concrete provider must make every output-affecting input part of its key,
including any baked logical reference bindings. This is not an exemption to the
stable subasset mapping rules.

## Commit point and interruption recovery

Publication is one narrowly scoped asset operation, not scene Undo or a generic
cross-document transaction framework. It coordinates exactly the import sidecar
and `forge.assets.json`:

1. Publish and close validated immutable cache files.
2. Prepare and flush `.forge/asset-publication.json`, recording bounded exact
   old/new metadata bytes. Preserve a v1 catalog migration backup when needed.
3. Durably replace the sidecar.
4. Durably replace the catalog. **This is the selection commit point.**
5. Remove the completed recovery record.

Staging uses unique exclusive-created files. Linux flushes files and parent
directories; Windows flushes files and uses write-through replacement. Metadata
paths must not redirect through symlinks/junctions. Reserved catalog, backup,
sidecar and `.forge` source names are rejected across platform case conventions;
catalog aliases are rejected too. The existing lease and content
checks coordinate normal local writers; they are not an adversarial filesystem
sandbox or a distributed filesystem transaction.

Recovery runs under the same writer lease before new imports:

| Observed files | Recovery |
| --- | --- |
| Previous catalog, previous sidecar | Remove unused recovery record |
| Previous catalog, candidate sidecar | Restore previous sidecar, or remove it if the first import was interrupted |
| Candidate catalog, candidate sidecar | Keep committed selection and finish cleanup |
| Any different bytes/unsupported identity | Report conflict; preserve files and recovery record |

Recovery validates both document identities and catalog-selection/input agreement.
It does not guess that a newer timestamp proves ownership. Cleanup failure after
commit is returned as a diagnostic alongside successful selection. A failure
before commit attempts recovery and leaves the journal available if recovery also
fails. Cancellation is checked before commit, including after sidecar replacement;
after successful catalog replacement the operation is committed.

The journal is bounded to256MiB and each metadata document to64MiB. Oversized
candidates are rejected before metadata writes. Process-interruption fixtures exit
at each durable boundary and recover in a fresh process; those intentional exits
are separate from normal-path leak checks. Windows has an additional real open-file
replacement test. Observed platform results belong in the daily changelog.

## Remaining integration

The coordinator is tested with generic failure/recovery fixtures and actual isolated
texture recipes, including simultaneous color/data resources. This is not yet
proof of a complete production importer, cooked package, resource manager or GPU
retirement path. Concrete providers must supply actual format/compatibility
validation and sidecar bindings. The editor's import commands must invoke recovery
before accepting new jobs and expose conflicts with their actual paths.

## Shared application service

`AssetImportService` supplies the concrete UI/headless orchestration boundary.
It retains the project writer lease, recovers interrupted publication before new
work, captures a source/settings draft and submits source hashing/discovery,
cache lookup and supervised cooking to the existing bounded `AssetBuildQueue`.
Probe prefixes are limited to64KiB; full source hashes/decoding do not run in the
UI draw path. The owner drains prepared candidates and invokes importer-specific
family preparation plus the mandatory compatibility callback before publication.

Jobs may defer their content key until discovery finishes. Unkeyed jobs never
coalesce by an unknown key; a successful result must supply a valid content digest.
Existing keyed jobs retain exact-key verification/coalescing. New generations
supersede older candidates, cancellation after cooking still prevents publication,
and service shutdown cancels/joins jobs before releasing writer ownership.
The service's status distinguishes a cooked candidate waiting for publication from
an imported asset; publication failures are retained in its bounded job receipts.

Unrelated catalog changes may be accepted while a candidate builds. The service
compares this owner's complete root/member records and exact sidecar against its
captured baseline before refreshing the publisher's whole-catalog ticket. It also
checks source ownership. Changed owner records, conflicting copied sidecar IDs and
changed settings reject. The publisher then rechecks every source/dependency and
its exact new ticket around compatibility validation. This permits independent
queued imports without treating an unrelated asset publication as permission to
overwrite the selected asset.

The texture provider supplies its existing single-root mapping and typed validator.
The CLI has no live world/device, so its compatibility preflight has no live resource
to replace. The current editor texture workflow likewise does not yet expose GPU
texture/material consumers; those consumers must add their compatibility preflight
when wired. No cross-document scene Undo, arbitrary native rollback or general
plugin ABI is implied.

## Publication receipts for source observation

Publication returns transient exact content-digest receipts for its catalog and
sidecar writes, prepared before the commit point. The watcher uses them to avoid
self-write rebuild loops. `forge.import.sidecar_digest` records the exact committed
sidecar bytes alongside the existing source/importer/profile metadata. This is
extensible import metadata, not another identity, scene format or catalog version.
Older selections without this field undergo one verified reimport before their
unchanged sidecars can be recognized at startup. A rejected candidate changes none
of these selections.
