# Phase 6 extension contract inventory

These classifications describe supported ownership, not a promise of permanent C++ binary compatibility.

- **A — Core engine contract:** shared semantics and source APIs. Persisted formats have their own versions. A does not mean an installable DLL API.
- **B — Exact SDK:** the installed value headers, matching Flecs, and checked host callbacks. Rebuild against the exact distribution when its fingerprint changes.
- **C — Built-in/private:** source-engine composition; not installed for binary gameplay consumers.
- **D — Implementation:** replaceable subsystem internals and private transport.
- **E — Legacy compatibility:** ABI1, retained unchanged.

| Contract | Class | Boundary and actual consumers |
|---|---|---|
| EntityId, AssetId, EntityRef, AssetRef | A; value subset B | Durable identity/typed asset expectations unchanged. SDK Values supplies UUID generation/parsing/formatting; no asset loading. |
| PrefabMemberId/Ref, StableId | A; value subset B | Authored prefab identity/textual entity mirror. SDK never rewrites unknown payloads. |
| EngineModule / ModuleContext / ModuleLifecycle | C | Source modules compose dependencies, schemas, role-specific startup/stop and code leases. |
| WorldContext / EngineContext | C | Own world, scene membership, transforms and module/service lifetime. Not a client-owned object or DLL factory. |
| EngineServices / ServiceAccess | C | Injected engine owner and narrowed world-scoped weak provider slots; same require/acquire/publication policy. |
| Diagnostic / Severity / DiagnosticContext | A source; B callback subset | Structured internal records; SDK copies bounded text and adds module/role/tick. No shared logger singleton. |
| ProfileScope / CPU samples | C; B bounded callback | Optional bounded CPU records; not simulation time or a profiler UI. |
| ProjectSettings / ProjectPaths | A source | Validated project configuration and contained resource resolution; not installed binary services. |
| SchemaRegistry / Flecs reflection | A source; exact Flecs B | Host document migration registry stays private to engine composition. SDK may register trusted Flecs types; this alone does not provide custom scene serialization/editor drawers. |
| AssetCatalog / AssetResolution | A source | Project metadata/admission. Removed accidental declaration exposure from installed SDK; value-only AssetRef header replaces it. |
| RuntimeInput / InputSnapshot / InputMonitor | C | Owner-thread latch/edge policy. SDK gets copied action values only during fixed execution. |
| ForgeSdkActionV1 / read_action | B | Intrinsic fixed-input contract, discoverable separately from optional provider permission bits. |
| PhysicsBody / Box, Sphere, CapsuleCollider | A authored; B values | No Jolt ID/pointer. Absent body means no separate simulation body; no invented enabled flag. |
| PhysicsService / PhysicsHit / PhysicsContact | C | Source ray/pose/contact operations; SDK has copied ray hit + queued pose subset. Contact vectors remain built-in. |
| Jolt world/body IDs/jobs/state recorder | D | World-owned implementation and private exact recovery. |
| AudioSource / AudioListener | A authored; B values | Typed clip and playback parameters. Source presence/autoplay differ deliberately from listener enabled. |
| AudioService / AudioRuntime | C; B command subset | Play/Stop are owner-thread commands. miniaudio device/voice/PCM handles are D. |
| Animator / skeleton and clip refs | A authored; B values | Fixed authoritative playback, derived pose. No artificial animation service: actual consumer writes Animator. Ozz objects remain D. |
| NavigationSurface / NavigationAgent | A authored; B values | Typed navmesh, destination/speed/enabled. |
| NavigationService / NavResult | C; B bounded query subset | Projection/path, explicit NavStatus, caller-owned output. Detour mesh/corridor/query objects D. |
| UiDocument | A authored; B values | Document AssetId, enabled/visible/layer. DOM/focus/model queues are not authored state. |
| UiService | C; B number/action subset | Runtime copied model and allowlisted commands. No RmlUi type or presentation pointer. |
| UiPresenter / UiDiligentRenderer / SDL input adapter | C/D | Reusable presenter + explicit host adapters; no binary gameplay access. |
| UI bridge / process protocol / recovery envelopes | D | Private independent versions and session/generation checks; not save-game/network/plugin protocols. |
| ForgeNativeSdkV1 / ForgeSdkWorldV1 / sdk::Client | B | Trusted exact registration + borrowed host and value callbacks, single shared Flecs. |
| SDK fingerprint / CMake Client target | B | Exact headers/value implementation/bridge/Flecs/toolchain/configuration identity. |
| module_api.h / Module / Build & Reload | E | ABI1 constrained native translation/tick path and existing transactional reload. |
| Broad plugin/provider/editor extension ABI | Deferred | Direction only; no marketplace ABI, provider hot swap or editor native reload. |

## Services and capability discovery

Built-ins use `ServiceAccess`: `available(Capability)`, `require`, and typed `physics/audio/navigation/ui` acquisition. One templated slot implements publication checks; the four typed methods remain readable and source-compatible. Access is owner-thread only; availability is false on a foreign thread, invalid/composite capability, expired engine, missing provider or denied permission. Slots are separate per world. A retained service object does not keep its world operational: module shutdown stops its implementation before world retirement.

Exact clients use `sdk::Client::query(Capability)` or `query_capability`. Result version describes the recognized contract; `available` means a live allowed provider, and `callable` additionally checks fixed-tick context for simulation operations. Version mismatch returns the supported version with both booleans false. Unknown/composite IDs and invalid result layouts fail. Do not cache availability across startup, Stop or ticks. The old `host.capabilities` bitmap is a startup snapshot retained for existing rebuilt consumers, not the authoritative live query.

Input is intrinsic to a runtime's fixed pipeline: query ID `FORGE_SDK_INPUT` is **not** a descriptor permission/provider bit. It reports availability after native startup and callability only during a fixed tick. Its read callback remains unavailable for a missing action. This preserves the existing ModuleContext input borrow instead of inventing a second input service/authority. Animation similarly uses authored Animator values and has no fabricated query service.

Optional provider bits are allowed permissions, not dependencies. A module can discover a provider that starts later; operations requiring it at startup must declare that provider dependency and required bit. Rendering remains an unavailable marker in headless composition. UI action registration is startup-only; UI `callable` describes fixed publish/poll, not startup registration. Diagnostics/profiling remain usable during teardown while the borrowed host exists, so finalization callbacks can report safely.

## Results: deliberately retain domain meaning

| Operation | Meaning |
|---|---|
| Capability query | Recognized contract vs invalid request; separate version/available/callable. |
| Input read | 1 copied action, 0 absent/outside tick/invalid. |
| Physics ray | 1 hit, 0 valid miss, -1 invalid/unavailable. |
| Physics pose / audio command | 1 queued, 0 rejected; realization may diagnose invalid authored configuration later. |
| Navigation | Callback delivery boolean plus named NavStatus. Partial/failure gives no usable points; capacity limit never truncates a successful path. |
| UI poll | 1 consumed matching event, 0 none/unavailable, -1 invalid. |
| UI publish/allow | 1 accepted, 0 rejected. |
| Asset resolution/admission | Typed metadata state or source-located exception before publication. Not a physics miss or empty event. |

No universal Result framework is introduced. Client convenience wrappers initialize outputs; C callbacks validate layout and catch C++ exceptions. These are cooperative trusted-code contracts, not a memory-safety sandbox for arbitrary pointers.

## Component authoring audit

All Phase6 components store authored values/references only and use the existing reflection, scene/prefab serialization and override/Revert paths. Defaults match their schema declarations. Null typed references mean unassigned, while wrong asset types/revisions diagnose instead of silently reinterpreting bytes. Physics has body/collider presence; AudioSource has play_on_start and commands; AudioListener/Animator/NavigationAgent/UiDocument have their domain-specific enabled controls. UiDocument.visible only controls presentation. Making all of these an identical enabled field would change semantics and is unnecessary.

Runtime velocities/contact state, decoded PCM/voices, Ozz sampling buffers, Detour corridors, UI models/DOM and graphics handles are not durable components. WorldTransform remains derived; local TRS ownership, prefab equal-value intent and independently reverted channels are unchanged. No new persistent identity, scene version or prefab Apply operation is introduced.

## AssetHandle decision

Still deferred. Current consumers resolve AssetIds into subsystem-owned immutable bytes/caches and private implementation handles. No cross-module loaded-resource lifetime needs a general shared handle. Adding one now would freeze Phase7 loading/streaming policy without a consumer.

## Flecs integration configuration

The exact pin remains 4.1.6 (`fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`). Script Math is enabled and fingerprinted. Global member-entity creation remains off; built-in authored types opt in explicitly. Native ranges are metadata, not host transaction rollback. Native root membership/ordered children and fixed Timer system admission are host semantics; modules must not replace them. `on_validate` and native Meta maps remain unavailable. See the source checkout's `docs/dependency-policy.md` and `docs/flecs-integration.md`; a richer live document is not permission to change the SDK pin or feature macros.
