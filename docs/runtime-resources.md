# Typed runtime resources

Phase7 currently implements the CPU resource pool and an actual cooked mesh
provider. Diligent GPU realization/fence retirement, existing audio/animation/UI
provider adapters and editor resource-inspection UI remain in progress.

## Identity and access

Authored `AssetRef<T>` remains durable AssetId/type identity. `ResourceLease<T>`
pins an immutable process-local revision while its owner scope is alive. The
identity contains a fresh ephemeral owner token, slot, generation, asset type/ID
and content revision. None is serialized into authored scenes or prefabs.

A `ResourceTicket` observes a coalesced load; it does not pin physical resource
data. A strong lease pins one revision across replacement or unload. A weak lease
can expire and promotes only on the owning thread. Checks occur before acquiring
a strong reference, so rejected access cannot transfer final destruction to a
background thread. An old ticket/weak lease never retargets a reused slot.

Owner shutdown revokes the scope, joins preparation workers and destroys selected
and retired data on the owner thread. Outstanding lease wrappers become invalid;
access reports that their scope is closed. Owners must close before their world,
module/provider code or device dependencies are destroyed.

## Loading and replacement

Concurrent requests for the same asset, source generation and revision share one
load. A conflicting/stale generation rejects; a newer generation supersedes any
unfinished request. Provider loaders perform bounded CPU preparation only.
`pump()` runs on the owner at an explicit adoption boundary, checks compatibility,
then rechecks request identity, cancellation and dependency readiness before
switching the selected revision. Compatibility callbacks cannot recursively pump
or close the owner. Source/settings/catalog selection checks belong to the provider
and asset-publication boundary, not an AssetId-only lookup.

States distinguish unloaded, queued, dependency pending, loading, ready, failed,
dependency failed, cancelled, stale, replacing and retiring. A dependency ticket
waits for actual owner adoption; a missing/failed/retired prerequisite does not
become a successful null resource. Dependency readiness is checked again before
adoption, and diagnostics carry the failed identity/revision chain. Providers that
retain a dependency's data must acquire and retain its typed lease as part of their
consumer binding; readiness observation alone is not lifetime ownership.

Failed preparation, content admission, compatibility or budget admission retains
the previous selected revision. `resolve()` can return a caller-supplied typed
fallback while retaining the requested asset's real state and diagnostic. The
fallback is explicit, and its strong lease pins its own actual revision.

`cancel(asset)` is an explicit cancellation of that asset's coalesced operation for
all observers. Dropping an observation is harmless. `unload(asset)` also detaches
selection and prevents late completion from resurrecting it; existing strong
leases keep their old data until released. A replacement retires its old readiness
ticket. Final collection changes that ticket to unloaded.

`wait(ticket, timeout)` is an explicit synchronous tooling convenience that pumps
owner adoption. A wait timeout does not implicitly cancel another observer's load.
Runtime consumers normally request asynchronously and pump at their safe boundary.

## Budgets and ownership

The pool bounds workers, pending requests, indexed asset/variant selections and retained bytes.
Statistics include selected, completed-candidate and retired revisions plus a
high-water count. Memory categories distinguish CPU asset data, GPU textures,
GPU buffers, shader/pipeline data and animation data; current mesh providers report
only their conservative retained CPU estimate. Zero GPU counters do not claim
that a GPU provider has been integrated.

All retained candidates/revisions share the byte budget. Preparation scratch space
is separately bounded by each provider's input limits and worker count; this is not
a process memory sandbox. A rejected candidate leaves the previous revision usable.
Idle eviction follows last-use order and skips strongly leased data. Manual unload
can detach selection while leased data remains accounted as retiring. Collection
releases retired CPU objects only after their final external strong lease is gone.

The current mesh loader takes an already-contained project/package artifact path,
verifies its expected content hash, admits the bounded cooked format and reports
retained allocations. Its caller supplies that contained path; this low-level native
API is not an unrestricted automation filesystem operation. No source decoder,
network download, catalog write, world mutation or device creation occurs in it.

## GPU boundary

The exact pinned DiligentCore744f079f already provides D3D12 native-resource release
queues keyed to command-buffer/fence completion. BufferD3D12Impl and
TextureD3D12Impl destructors call SafeReleaseDeviceObject; RenderDeviceNextGenBase
purges with the queue's actual completed fence value. Production integration must
use those semantics with correct immediate-context masks and owner teardown.
Resource statistics must also account for deferred/in-flight physical memory.
The CPU pool has no fake fence or frame-count assumption. Actual submitted-draw
replacement and shutdown fixtures remain required before claiming GPU retirement.

The source-level interfaces are internal integration APIs and are not yet part of
the installed gameplay SDK. Their eventual SDK/render providers must be verified
through the exact installed SDK boundary and the full Phase7 acceptance suite.

## Explicit variants

A logical image may be used as both sRGB color and linear data, or have different
backend artifacts. Pool selections therefore use `(AssetId, variant)` rather than
AssetId alone. The bounded variant key is an explicit provider recipe/profile key;
it is not a new persistent asset identity. It participates in the process-local
revision identity. The empty variant remains the ordinary default.

Requests coalesce and source generations conflict within one variant. Independent
variants share the same owner budget and can coexist. `current`, `inspect`,
`resolve`, `cancel` and `unload` take an optional variant; omitting it addresses only
the default selection. Acquiring a ticket always addresses its exact variant. A
reload/unload of the color selection cannot silently change the data selection.
The concrete texture provider verifies cooked bytes before adopting a variant.
