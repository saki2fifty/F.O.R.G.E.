# Asset foundation contract

Architecture for Phase7; importer/cooker/resource-pool implementation is not delivered
here. [Identity](identity-assets.md) remains the current contract. Decisions005–007
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
