# ADR058 — Explicit world-private navigation rebuilding

Status: accepted for the bounded native runtime profile. Windows/full-editor and
physical-device acceptance remain separate validation claims.

## Decision and ownership

Extend ADR026/027's navigation source and derived-query owners with an explicit
runtime request. Reuse the selected Nav asset's existing settings and supported
static sources. Preserve its logical AssetId, authored artifact and catalog
revision. The resulting immutable Mesh and mutable Query remain private to the
requesting NavigationRuntime World. Other Worlds and authoring data are unchanged.

NavigationCandidate and runtime requests share private bake metadata assembly,
worker execution, admission, provenance and query proof. Only NavigationCandidate
owns asset publication. Existing RuntimeResourceService and PhysicsRuntime own
Mesh/Collision readiness and leases; existing runtime Map instances own cells and
variants. Copy bounded geometry on the writable owner thread before launching
file-only work. Never give an IO worker borrowed ECS values or native World handles.

Use the existing WorkerStaging/lease/cleanup owner under an OS-temporary namespace,
separate from the read-only game content root. Trusted hosts select the matching
installed worker beside themselves. Game code and agent requests cannot select a
program, shell command or staging directory. Runtime kits include that worker and
its normal resolved runtime dependencies and notices.

## Request, validity and adoption

One request may prepare/build/retire per World. Keep only the latest observation;
opaque process-local tokens are not persistent IDs. Requests queue without geometry
capture. Reads do not progress work. Fixed synchronization and explicit host polling,
including Pause, own preparation/adoption and do not advance gameplay time.

The existing finite source/resource/native geometry limits apply. Resource
preparation has a 30-second deadline; the existing worker retains its own memory,
file, CPU, wall-time and cancellation limits. Cancellation and stale-source failure
retire owned work before another request can take its place. Shutdown or Scene
rebinding cancels and joins work before resources are released.

Revalidate Scene identity, current source geometry and exact selected catalog and
resource revisions before adoption. A changed source or selection discards the
candidate. Stale geometry cannot be queried while a rebuild is pending. Preserve
the admitted basis when preparing/rejecting a replacement; it is usable only when
its source validity checks still pass.

Construct the new native query and apply the complete current obstacle snapshot
before its transactional replacement. Advance the local query generation so
retained routes replan. Reset prior failed obstacle-update state. Do not expose a
new base with the previous base's obstacle snapshot or polygon references.

## Integration and limits

Gameplay services, exact-kit SDK callbacks, native Play controls, automation and MCP
route through this owner. SDK tokens belong to the requesting module instance.
Play retains its session/sequence/world-generation gates; automation also retains
its launch identity and execution grant. Authoring Undo is unaffected because no
source or catalog write occurs. Appended SDK callbacks and changed public headers
require a matching fingerprint and installed/relocated validation.

This does not broaden source admission to Dynamic/Kinematic or deformed geometry,
create physics collision, edit bake settings live, or implement world streaming.
Runtime-edited Map instances remain excluded from baked source admission until
reset. Rebuilding can use supported changed source poses/selections; it does not
make those sources continuous moving navigation platforms.

Private rebuilt bases are not recoverable catalog artifacts. Pending requests and
Worlds with an adopted runtime basis refuse a partial private crash checkpoint;
restart clean Play after a crash. Existing authored/SDK restart behavior and older
positive checkpoints remain unchanged. General save-game/native-state migration is
outside this profile. A future recovery extension must reconstruct the exact basis
in an unpublished candidate before claiming support.
