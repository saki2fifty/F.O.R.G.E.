# Asset foundation contract

The frozen asset architecture is now implemented by the shared import, publication,
cache and resource services. Full Phase7 delivery validation remains in progress.
[Identity](identity-assets.md) remains the current contract. Decisions005–007
in [the ADR index](decisions/README.md) govern this design.

## Five distinct objects

| Object | Identity and owner |
| --- | --- |
| Source | Project-contained user file and its content digest; source path is a locator |
| Logical asset | Persistent UUIDv4 AssetId, declared type and metadata, independent of filename and content |
| Import settings | Versioned reproducible typed settings; canonical encoding has a settings digest |
| Artifact | Immutable validated output revision; content digest plus format/platform/profile identity |
| Runtime resource | Process/world/device-scoped realization of one artifact revision, with explicit lifetime |

The pipeline is detect/register → import → validate → process/cook → publish immutable
artifact → switch catalog selection → request runtime realization. Import decodes source;
processing produces platform-independent derived data; cooking selects platform/profile
representation; validators check structure, limits, semantics and dependency compatibility;
publisher alone changes the selected revision. No worker writes the catalog directly.

## Identity, revision and subassets

AssetId survives a same-logical-asset move, rename and reimport. Source digest,
settings digest, importer implementation revision, dependency revision set, output format,
platform, graphics backend and capability profile identify a build request. Artifact digest
identifies bytes, not logical identity. Duplicate a logical asset to a new AssetId even if
it initially reuses cached bytes. Scene duplication also regenerates authored EntityIds and
remaps known intra-scene references; opaque unknown payloads remain untouched.

Model is a logical container with addressable Mesh/Material/Skeleton/Clip AssetIds. A
versioned source sidecar records source-element key → AssetId, type and reconciliation
provenance. Prefer exporter stable IDs; otherwise allocate a durable mapping entry and
retain semantic evidence. Source order/index and names alone cannot silently reassign IDs.
Ambiguous reimport reports a conflict and retains old outputs until explicitly remapped.
Removed subassets become diagnosed tombstones; reusing an old ID for an unrelated element
is forbidden. Imported scene-node structure is model data, not automatically authored
Scene EntityIds. Instantiation allocates those separately.

## Dependency graph

One catalog-owned graph stores typed forward edges and a reverse index. An edge identifies
consumer AssetId, dependency AssetId, required type/role, selected revision and build/runtime
requirement. Material → Shader/Texture; Model → Mesh/Material/Skeleton/Clip; Clip → Skeleton;
Prefab → referenced assets/prefabs; UI → style/font/image sources are ordinary instances.
Build-required cycles are rejected with an edge path; runtime reference cycles that require
no recursive build may be represented but never recursively loaded without visited-state.
Prefab inheritance/nesting cycles remain invalid regardless of generic runtime reference rules.

Changing source/settings/tool/dependency revisions marks reverse dependents stale and schedules
build-required edges topologically. A candidate captures the complete revision set; publication
compares it again. A stale candidate is discarded or requeued, never published against different
inputs. Missing required dependencies block publication; optional ones require a declared typed
fallback. Deletion first enumerates dependents and authored references. Accepted deletion leaves
explicit missing references/tombstones, never silent retargeting. A folder move updates locators
and sidecars as one recoverable authoring operation without changing logical identity.

## Cache and publication

Local cache: `.forge/cache/derived/<key>`. Key uses a canonical, versioned serialization of
source/settings/tool/transitive dependency digests, output schema and platform/backend/profile
where relevant, hashed with SHA-256. Never use modification time alone. Verify manifest, output
sizes and hashes and then format admission on reuse. Corrupt entries are quarantined and rebuilt;
a miss simply runs the same pipeline. Deterministic CI fixtures compare manifests and bytes.
Nondeterministic importers explicitly opt out of shared reproducible cache reuse.

Workers stage under `.forge/jobs/<job-id>`. Publish validated immutable files first, fsync/close as
required by platform, then atomically replace the catalog selection manifest. A crash before
selection leaves an orphan candidate, not a half-selected revision. Startup removes unreferenced
staging after checking no active job owns it. A failed catalog replacement leaves old selection
valid and reports the actual path/OS error. Cache eviction excludes selected/in-use revisions and
leases; use bounded LRU of unreferenced entries. Downloaded cache data gets the same validation
as locally generated data. Source-control checkout must not require the cache to survive.

## Extension and worker contract

Importer registration declares stable importer ID/version, supported extensions/source types,
output types/formats, settings schema/defaults, dependency discovery, determinism, supported
platforms, diagnostic schema and resource budgets. Processor/cooker/validator/publisher roles
remain separate even if one implementation supplies several. Conflicting importers require an
explicit project selection, not registry order. Registration is restart-bound with its native
module; jobs keep the module alive until joined or are isolated in a worker process.

Default initial worker budgets are120seconds,1GiB committed/process memory,512MiB total output,
4096 output files and project-contained paths; importer declarations may request different
bounded budgets reviewed with their fixtures before enablement. These are a design starting
policy, not a claim current converters share these exact limits. Supervisor enforces limits
using platform facilities, bounds diagnostics and validates outputs independently. Commands use
structured arguments, no shell interpolation. Worker gets an explicit input manifest and job
staging root; no arbitrary catalog write or editor world pointer. Result includes job ID,
input-generation digest, exit status, output manifests, dependencies and structured diagnostics.
Cancellation marks the request stale first, requests orderly exit, then terminates after a
bounded grace period and waits before deleting staging. A late result cannot publish.
Trusted bounded pure transformations may run on a background task without subprocess overhead;
parsers/converters with crash/resource risk use the established isolated candidate model.

## Runtime loading and handles

Choose AssetRef plus typed **ResourceLease**, not a persisted generic AssetHandle.
AssetRef<T> is durable AssetId+expected-type validation. Resource identity is owner scope,
pool slot, generation, type and immutable artifact revision. A strong lease pins that revision;
a weak reference may expire and must validate generation. Subsystem providers own physical
objects; the shared request/revision contract does not transfer Jolt/miniaudio/Ozz/Diligent
ownership to a generic void-pointer pool.

States: requested → dependency-pending → loading → ready, or failed/cancelled. Unload retires a
request and releases its leases; cancelled completions cannot resurrect it. Failure includes
cause/dependency chain and optional typed fallback, never a successful null object. Request
completion is adopted on the owning runtime/presentation boundary. ECS stores authored refs and
transient typed bindings where needed, never raw implementation pointers in authored state.
Streaming later adds partial residency/budgets beneath a revision; AssetId does not change.

## Reimport and subsystem adoption

| Consumer | Safe adoption boundary |
| --- | --- |
| Rendering | Prepare GPU resources/bindings, switch extracted revision at a frame boundary, retire after leases and GPU fences |
| Audio | Prepare decoder/resource on control owner; enqueue replacement intent; callback never loads files or destroys shared resources |
| Animation | Validate skeleton/layout/provenance and prepare sampling state; switch at fixed tick; incompatible skeleton requires explicit reset, not blind time/state copy |
| Navigation | Validate tile/navmesh format and query ownership; replace at runtime owner boundary, invalidate/replan affected paths with diagnostics |
| Runtime UI | Prepare document/style/font/image candidate on UI owner; preserve old document on failure; restore only explicitly compatible UI state |
| Material/Texture/Shader | Validate reflected bindings as a candidate dependency set; switch only when compatible resources are ready |

Existing synchronous private caches remain until each real consumer is migrated with tests.
The first Phase7 mesh/texture CPU/GPU resource consumer justifies implementing the minimal
request/lease pool. This document does not authorize blanket subsystem rewrites.

## Implemented publication coordinator (Phase7 in progress)

The [asset-specific publication coordinator](asset-publication.md) now connects
validated DDC output, captured input revisions, catalog selection and durable
sidecar recovery. Its fixtures are executable; complete production providers,
resource adoption and editor workflows remain separate integration work.

## CPU resource and mesh implementation checkpoint

The [typed resource pool](runtime-resources.md) and [cooked mesh artifacts](mesh-assets.md)
now have executable asynchronous loading/lifetime consumers. CPU leases, last-good
retention and bounded mesh admission are implemented; GPU retirement, complete
production providers and editor/cook integration remain in progress.

### External build-tool revisions

`AssetBuildInput.tool_revisions` records up to64 named external-tool SHA-256 digests.
These are build inputs, distinct from source locators and logical asset dependencies.
For example, model animation includes the actual `gltf2ozz` executable digest. The
registered importer revision continues to identify the FORGE recipe and must match
at publication. Build documents with tool revisions use key version2; tool-free
inputs retain their exact version1 shape. Sidecars preserve the complete build input
and old artifacts remain immutable; changing a tool produces a new disposable key.
Names are bounded portable identifiers and revisions must be valid content digests.
The importer owns tool availability and before/after execution checks.

Selected cooked revisions can be loaded by key through the same bounded cache
reader and mandatory format validation used for import hits. This read path does
not require source/import settings discovery, run converters or change catalog
selection. Missing/corrupt selected data throws a diagnostic without moving the
entry; the authoring import path retains its separate quarantine-and-rebuild policy.
Returned owned bytes survive disk eviction. Runtime callers must also verify the
selected catalog binding and own adoption through their resource lifecycle.


### Existing family graph integration (Phase7)

Legacy Ozz conversion publishes typed Build dependencies on its Animation source,
Runtime clip-to-Skeleton dependencies, and exact captured glTF/buffer source edges.
It preserves its existing converter/admission/publication and rename-rejection
contract. Model import's newer source-family mapping is separate; there is no
silent migration of the legacy converter's generated identities.

Navigation publication records its Scene as a typed Build dependency with the
geometry revision. Its own bake and stale-geometry validation still govern
publication; a generic source importer cannot substitute for scene-aware baking.
Script registration records the root file's observed digest without executing it.
Native includes stay in the existing disposable preview worker, including the
approved pinned-source cleanup exception; no proprietary include parser or complete
static include graph is added. Re-registering refreshes that source observation.

UI source identity/observed dependency publication is described in
[Runtime UI](runtime-ui.md#phase7-common-asset-integration). Audio uses the shared
[cooked AudioClip route](audio.md#phase7-audioclip-import-and-selected-revisions).
Content status checks recorded source edges across these families. Scene/Prefab
continue their authored-document save/history and prefab candidate reconciliation;
ordinary Save is not relabeled as a source import.

## Source-independent content packages

[Runtime content packaging](runtime-content-packaging.md) defines the implemented
selected-cooked closure, trimmed catalog, explicit target/profile admission and
source-free relocation validation. This content tool does not constitute a full
standalone visual executable exporter.

## Cache maintenance

`maintain_asset_cache` is a private authoring/tool adapter requiring the project
writer and no active import/publication jobs. CLI commands acquire that lease;
they cannot overlap an editor writer. Pruning protects every catalog-selected key.
Explicit asset/all clearing removes disposable bytes while preserving catalog,
sidecars and source data; model members sharing a key are reported as affected.
Normal resource readers own admitted byte copies, so removal cannot invalidate
an existing CPU lease. A subsequent load can fail until explicit reimport.

Storage verification checks manifests, keys, bounded file sizes and hashes without
claiming importer-format admission. It neither selects a resource nor quarantines
unknown-provider data. Normal `find`/publication/loading still require their real
format validator. Orphan cleanup takes the same cache lock as publication and only
removes recognized flat staging (and, for clear-all, quarantine) entries. Unknown,
redirected, nested or excessive data is retained with diagnostics. Cache operations
are disposable-data maintenance, not authored transactions or scene Undo.

The [source and cooked-format matrix](asset-formats.md) lists concrete importer
coverage, limits, dependencies and executable fixtures. Linked-library capability
is not the same as an enabled FORGE format.
