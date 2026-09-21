# Writing FORGE gameplay extensions

Use the smallest supported extension tier for your task.

| Tier | Use it for | Compatibility/lifetime |
|---|---|---|
| Built-in/source EngineModule | Engine subsystem composition and source-built features | Full source rebuild; injected world services and explicit dependency order. |
| Exact SDK gameplay module | Trusted C++ gameplay using Flecs components/systems and optional services | Matching SDK fingerprint/toolchain, one shared Flecs; restart runtime for registration changes. |
| ABI1 | Existing editor Gameplay Code / Build & Reload workflow | Constrained version1 C callbacks; transactional candidate activation and existing recovery. |
| Future broad plugin ABI | Not implemented | Will require stable function tables, ownership, version negotiation, dependency/provider/editor registration and retirement proofs. |

Native editor extensions remain trusted and restart-bound. Exact gameplay DLLs do not load into the editor process. The ordinary Windows editor package still uses ABI1; the separate exact SDK artifact provides the richer headless runtime workflow.

## Build the exact SDK sample

Extract the matching experimental SDK archive. Use the compiler, architecture, configuration and CRT used to build it. Configure `sdk/sample` with `-DFORGE_NATIVE_SDK=<extracted-root>` and `-DCMAKE_BUILD_TYPE=Release`, then build with Ninja. The `combined_gameplay` target is the small combined example; individual probes remain regression examples.

A new client links `ForgeNativeSdk::Client`, which supplies shared Flecs and the small static identity-value library. No subsystem implementation library is linked into your module. That static value library owns no world, registry, cache or global resource. It does not introduce another Flecs implementation. `AssetId::generate/parse/str` work directly; do not serialize through reflection merely to format an ID.

Include `forge/native_sdk.h`, `forge/native_sdk_identity.h`, `forge/sdk_client.hpp` and the authored component headers you use. `forge/asset_ref.hpp` contains typed values. Engine AssetCatalog/WorldContext/renderer implementations are deliberately not installed. Do not link a private static Flecs, change its feature macros, use a different CRT, or override ABI-changing compiler flags on an individual target.

Declare the module in project version2 `modules` using `sdk: "experimental-1"`, exact implementation/fingerprint, contained library path and matching dependencies. A declaration is shown below. For the combined example use `project.example`, implementation `1`, dependencies `forge.input` and `forge.transforms`. Run the distribution's `bin/forge_runtime --sdk-project <project>`; add `--ui on` or `--audio offline/device` only when wanted. This is a headless developer workflow; it does not create a standalone visual game.

For example, a project module entry is:

```json
{
  "id": "project.example",
  "implementation": "1",
  "sdk": "experimental-1",
  "fingerprint": "<matching runtime --sdk-info fingerprint>",
  "library": "Native/combined_gameplay.dll",
  "dependencies": ["forge.input", "forge.transforms"]
}
```

Use `combined_gameplay.so` on Linux. Place this object in the project's `modules` array. Module paths stay inside the project. The installed guide and contract inventory are self-contained; full engine/subsystem documentation is available in the FORGE source checkout.

## Small combined example

`combined.cpp` registers an ordinary Flecs State component and a fixed-phase/tag system. The system reads a mapped ActionId, updates that component, queries physics/navigation when present, polls an allowed UI action, and publishes a copied UI value. It records diagnostics and CPU timing through the host. Scene entities still own authored state; the UI is a copy, not another authority. The example's State is transient and deliberately has no invented save/recovery serializer.

The sample action UUID is `12345678-1234-4234-8234-123456789abc`; map it in project input settings. A UI document may display `{{example_ticks}}` and send `command('ExampleIncrement')`. UI action registration occurs during start. Query `available()` for optional provider discovery; query `callable()` for current fixed execution. Never retain an action/component pointer across a structural change or tick.

## Lifecycle and ownership

1. Host loads trusted code and checks descriptor size/version, exact fingerprint, identity/dependencies and Flecs function/global addresses.
2. All selected schemas register into one host-owned world using ordinary Flecs imports/registration.
3. Runtime modules start in dependency order. Use the supplied fixed phase and FixedSimulation tag for gameplay systems.
4. Host lends input only during fixed execution. Diagnostics/profile callbacks copy bounded values; simulation callbacks return their documented statuses.
5. Stop runs in reverse dependency order, including partial startup. Drain work without throwing. Stop is not unload: Flecs may still call component destructors/observer/system finalizers.
6. Host retains code and bridge contexts through `ecs_fini`, then releases contexts/code in reverse order.

A borrowed `ForgeSdkWorldV1`, Client, Flecs world/entity wrapper, callback or service must not escape its world lifetime. Use stack-local ProfileScope only within that borrow. Native entrypoints and Flecs callbacks must not let exceptions escape. Host C callbacks catch their own exceptions; they cannot catch a native access violation or make arbitrary plugin pointers safe.

## Compatibility and rebuild rules

| Contract | Change policy |
|---|---|
| Exact SDK fingerprint | Installed FORGE header, identity helper or bridge semantics, pinned Flecs, compiler/version/toolset, OS/architecture, config/CRT/flags/sanitizer profile change requires matching rebuild; mismatch rejects before registration. |
| Native descriptor/host layout version1 | Size checked; this exact tier also rejects changed header fingerprints. Version1 is not a cross-release binary stability promise. |
| Capability contract version1 | Query exact requested version; unsupported version reports unavailable. Independent from descriptor/build number. |
| Module implementation | Exact declared string must match binary; independent from SDK fingerprint. |
| ABI1 | Existing module_api.h version/layout unchanged. Native reload semantics stay constrained. |
| Process2, UI1 and recovery sections | Private independently versioned protocols; no public compatibility/save-game claim. |
| Build yymmdd-counter | Distribution identity only; never substitutes for API/format compatibility. |

Renderer-only edits do not change the gameplay SDK contract. CMake rejects incompatible supported package configurations; runtime fingerprint checking verifies cooperative module declarations, not every possible compiler switch a malicious or misconfigured client could hide. New registering SDK code restarts the runtime; do not apply ABI1's in-place replacement to rich Flecs registrations.

## Thread/process and fixed-clock map

| Owner | Work |
|---|---|
| Runtime owner thread | Flecs, input latch, gameplay, navigation, physics synchronization/adoption, animation, transforms, audio commands, authoritative UI models/actions, diagnostics/profiles. |
| Jolt workers | Solver jobs; joined before adoption. No Flecs access from solver callbacks. |
| Audio callback/device | miniaudio-owned playback/atomic notifications; no gameplay clock or Flecs mutation. |
| Editor presentation thread during Play | Diligent viewport and reusable RmlUi presenter; copied models and semantic commands only. Native presentation faults share editor failure domain. |
| Headless | Same required gameplay composition, no ImGui/D3D12/RmlUi presenter or editor dependency. |
| Conversion/build workers | Bounded official gltf2ozz and navigation generation; results admitted before publication. |

Actual fixed tick order in RuntimeSimulation is: input latch → navigation synchronization → module input borrow → Flecs InputMonitor → Gameplay (SDK systems and ABI1) → Navigation movement → PrePhysics synchronization/queued commands → Physics → PhysicsAdoption → PostPhysics → final transforms → end module input borrow → animation playback → pose capture → audio synchronization → discontinuity snapping. No relative order is promised between unrelated systems in the same phase. Gameplay code declares its own Flecs dependencies when needed.

UI custom model values are published by their actual fixed gameplay system; they are not secretly reevaluated in a later presentation phase. The runtime adds authoritative tick/paused when extracting a UI snapshot. Presentation extracts/interpolates completed state independently. Ozz presentation sampling may evaluate a derived interpolated pose; it does not advance authoritative animation time.

Pause stops physics, agent motion, authoritative animation and gameplay. Audio gameplay group pauses; Step stays silent. Step executes exactly one gameplay tick. RmlUi continues presentation-time layout/input while paused. Stop destroys the runtime world and retires presentation. Recovery reconstructs a candidate first: physics uses exact bounded solver state; animation validates asset revisions/playback; navigation validates asset digests and recomputes paths; audio follows existing autoplay restart policy; UI reconstructs from new session/generation and discards stale commands/DOM state. Failed candidate publication retains the prior live world when available. This is transient recovery, not a save-game system or arbitrary native-state migration.

## Diagnostics and profiling

Built-in structured diagnostics retain appropriate EntityId/AssetId/source/property/module/tick where known. Exact text callbacks add module/role/current tick automatically; do not put raw library pointers in a record. Private UI command failures stay correlated to command/session/generation in their acknowledgement; they are not a second logging service.

Available CPU scopes include module Registration/Startup/Shutdown, FixedSimulationTick, TransformEvaluation, JoltUpdate, AudioAssetResolve/AudioSync, FixedPlayback/SampleAndLocalToModel, NavLoad/NavQuery/NavAgentUpdate, PresentationExtraction, UI ModelSnapshot/CommandValidation and SDK module-named samples. Records are bounded and opt-in; recording disabled still accepts valid instrumentation calls. These are CPU measurements, not GPU timing or a full profiler UI.

## Deliberately deferred

Generic AssetHandle, importer/cooker/streaming, cross-document transactions/Apply, stable third-party ABI, editor/provider extension registries, world-space UI/visual designer, advanced audio/animation/navigation, networking and save games remain separately scoped. See the [contract inventory](extension-contracts.md) and subsystem docs. Phase6 ends here; Phase7 has not begun.

## Pinned Flecs capabilities

Follow the dependency source-of-truth policy before using an API from live documentation. The current pin is 4.1.6 at `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8`. This integration enables Script Math consistently and includes that option in the SDK fingerprint; rebuild clients with the matching SDK. `on_validate` and native Meta maps are unavailable in this pin. Do not patch or emulate them.

Register useful Meta/Doc/Units metadata and explicit member entities on custom types when member-targeted Metrics/Alerts are needed. Global member creation is off. Ranges describe values; validate candidates at the appropriate host boundary. A trusted native write is not automatically rejected by ranges. Query change detection requires a supported cached query and explicit `modified` after raw writes. CanToggle/Sparse/DontFragment need per-type semantics and lifecycle review.

Native timers/rate filters advance underneath fixed ticks. Gameplay systems still require the supplied fixed phase and tag. Do not call `world.progress`, change the pipeline, or turn on world threading from an arbitrary module callback. Scheduling configuration belongs to the runtime composition owner. The isolated multithreading probe does not change provider call permissions. Native scene roots now have an internal membership parent for sibling ordering; use authored references rather than interpreting `parent() != 0` as an authored hierarchy test.

Flecs Script editor previews own temporary worlds and do not implicitly become runtime gameplay. Native JSON and read-only REST are development inspection surfaces. The full source contract is documented in `docs/flecs-integration.md` in the source checkout.

## Opting a value component into schema inspection

During `register_schema`, register complete native Meta fields, then call the host's
`authoring_type` callback or `forge::sdk::Client::authoring_type`. Supply the native
type ID (worker-local only), stable namespaced type key, positive schema version,
bounded JSON defaults, category and a caller-owned error buffer. The host derives
structure and Doc/Units/ranges from native metadata. It does not accept a second
project-defined field description. Do not opt in pointers, resource owners, callback
objects or incompletely reflected values. Unknown default fields reject; f32 defaults
normalize to their actual native representation. Late opt-in outside registration
rejects. `probe.cpp` contains the `project.health` extraction regression example.

Run `bin/forge_runtime --inspect-sdk <project-folder>` for copied schema/default
output. The inspection world runs schema callbacks only; it does not start gameplay
or provide simulation services. Schema registrations must support the Validation role
and declare compatible dependencies. SDK fingerprint and library identity checks
still apply. The editor's worker supervisor additionally enforces cancellation and
resource limits before owner-thread schema admission. This extraction checkpoint
alone does not make a custom component editable, persisted or prefab-aware; those
consumers are still under implementation. Rebuild modules after this exact SDK change.

## Cooked render resources in gameplay

The exact SDK exposes CPU resource admission through `sdk::Client`. Add
`FORGE_SDK_RESOURCES` to the module's allowed capabilities. If startup requires
it, also add it to required capabilities and declare `forge.resources` as a
module dependency. Project runtimes compose this provider; schema inspection
and authoring worlds do not activate it.

```cpp
forge::sdk::Client client(host);
auto token = client.request_resource(FORGE_SDK_RESOURCE_MESH, mesh_uuid);
ForgeSdkResourceV1 status{};
if (token && client.inspect_resource(token, status)) {
    // status.state describes the requested CPU load.
    // retained_revision identifies pinned last-good bytes, if any.
}
client.release_resource(token);
```

Supported kinds are Mesh, Material, Texture and Shader. Typed identity is checked
before dispatch. Only already-cooked content is loaded; gameplay does not invoke
source importers or shader compilers. Texture requests default to color (HDR where
available). Pass `FORGE_SDK_TEXTURE_COLOR`, `FORGE_SDK_TEXTURE_DATA`,
`FORGE_SDK_TEXTURE_NORMAL` or `FORGE_SDK_TEXTURE_HDR_COLOR` as the optional third
`request_resource` argument for a specific published variant. This does not
generate a missing variant or reinterpret color data. Non-texture requests accept
only the default `FORGE_SDK_TEXTURE_AUTOMATIC` value. A ready material does not assert that all its
textures or shader pipelines are draw-ready. `Rendering` remains unavailable in
the headless simulation worker.

Requests and observations are owner-thread operations during native startup or
running state; they do not require a fixed tick. Adoption occurs before fixed
simulation and during presentation extraction, including while paused. The
existing asset-publication notification refreshes the captured catalog. Compare
revision strings to detect a successful replacement. While it is pending or
failed, `retained_revision` can still identify the previous usable resource.
A failed catalog refresh keeps the prior selection and emits a diagnostic.

Tokens belong only to the creating module and world, have no persistent meaning,
and must never be serialized. Releasing one subscription does not cancel another
subscriber's load. Stop releases outstanding subscriptions automatically. World
shutdown closes the resource scopes and drains workers. Limits are64subscriptions
per SDK module,256per world and128MiB CPU bytes per resource family. Token0 or a
false callback result means rejected/unavailable; output observations are cleared
on rejected inspections. These callbacks change the exact SDK fingerprint and
require rebuilding native modules; ABI1 is unchanged.
