# Writing FORGE gameplay extensions

Use the smallest supported extension tier for your task.

| Tier | Use it for | Compatibility/lifetime |
|---|---|---|
| Built-in/source EngineModule | Engine subsystem composition and source-built features | Full source rebuild; injected world services and explicit dependency order. |
| Exact SDK gameplay module | Trusted C++ gameplay using Flecs components/systems and optional services | Matching SDK fingerprint/toolchain, one shared Flecs; restart runtime for registration changes. |
| Future broad plugin ABI | Not implemented | Will require stable function tables, ownership, version negotiation, dependency/provider/editor registration and retirement proofs. |

Native editor extensions remain trusted and restart-bound. Exact gameplay DLLs do not load into the editor process. The Windows editor and standalone runtime both use the exact-SDK gameplay path. Managed projects build in the Build panel; external SDK projects keep their own build recipe.

## Build the exact SDK sample

The Windows editor ZIP includes the matching SDK under `NativeSdk/`. Use the
compiler, architecture, configuration and CRT used to build that distribution.
Configure `NativeSdk/sdk/sample` with `-DFORGE_NATIVE_SDK=<NativeSdk-root>` and
`-DCMAKE_BUILD_TYPE=Release`, then build with Ninja. The `combined_gameplay` target
is the small combined example; individual probes remain regression examples.
An independently installed SDK uses the same `sdk/sample` layout relative to its
installation root. Configure errors report the expected and detected toolchain
values. Windows module-kit deployment requires CMake 4.3 or newer; select that
executable in the editor's **Build → Compiler setup**, or use it for both configure
and `cmake --install` in an external build.

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

Engine texture identities use the value-only `forge/texture_dimension.hpp`. They do
not expose the cooked texture loader. CMake checks that every public FORGE include
reachable from the installed header list is also installed. The relocated SDK
consumer independently verifies that boundary and the exact-version fingerprint.

## Local reflection probe values

Include `forge/reflection_probe.hpp` to access `forge::ReflectionProbe` from a
matching-SDK Flecs system. It contains a typed TextureAsset reference and authored
controls; it owns no GPU resource. Configure the installed sample normally and
build target `reflection_probe_gameplay` for a small fixed-phase example. The
system retains the assigned map while tuning intensity, yaw, box width, priority
and box projection. The new installed header changes the exact fingerprint;
rebuild existing modules with the matching SDK rather than editing their declared
fingerprint. The renderer resolves maps through its ordinary resource owner.

## Projected material values

Include `forge/decal.hpp` and use `forge::DecalProjector` on an entity with a
resolved node transform.
Its typed `AssetRef<MaterialAsset>` selects the channel source; resources remain
owned by the renderer. Width, height and depth are full local metres before node
scale, in 0.001–10000. The centered volume projects along local+Z. Tint, opacity and color/normal/roughness influences
are finite0–1. Influences default to1/0/0, preserving existing color behavior. Fade angles are radians with0≤start<end≤π/2 and must remain
distinguishable at GPU precision. Layers are a bit mask and priority is int32.
Higher priorities paint later, with stable EntityId ties.

Build the installed sample target `decal_gameplay`. Add its
`project.decal_control` component and `forge.decal` to an entity, assign a saved
ordinary PBR/unlit Material and use a physical receiving Mesh. The example keeps
the Material reference while tuning all fifteen scalar fields over three fixed
ticks. It demonstrates public data access, not a complete visual game.

The matching installed header list includes this value-only type. Rebuild modules
with the SDK matching the runtime; retain the generated exact fingerprint. The C
ABI table is unchanged. Invalid projection values are diagnosed before rendering.
The visual profile admits 128 active projectors per scene, independently of the
physical Mesh part's zero-to-eight projected map/sampler capacity. Matching native
views and sampler intent share a binding across color, normal and roughness maps.
Spatial selection uses the existing renderer/bounds owners. Custom Surface Shader
sources and unlit receivers are unsupported. Normal/roughness require capability 10
or later; larger lists require capability 11. Older receiving Shader cooks keep their
original capacities and complete-set fallback. Source normal scale and per-channel
UV transforms are retained; clearcoat and optics remain with the receiving material.

## Soft-shadow source dimensions

Include `forge/render_components.hpp` and use `forge::Light::shadow_source_radius`
for Point/Spot source radius in world metres, or `shadow_angular_radius` for the
Directional source half-angle in radians. Both default to zero; use finite values
in 0–10000 metres and 0–approximately0.785398 radians respectively. Scale does not
multiply either source dimension. Direct illumination remains punctual. Enable
`cast_shadows` and use a lit receiving mesh to see the effect.

Build target `soft_shadow_gameplay` in the installed sample. Its five fixed ticks
cycle Directional, Point, Spot, Rectangle and Sphere while updating shadow-source
and emitter dimensions on entities
that have `project.shadow_control` and `forge.light`. This is a small field-access
example, not a complete visual game. Rebuild project modules with the SDK matching
the runtime: these appended installed Light fields change the exact fingerprint.
Never repair a mismatch by editing the fingerprint declaration.

## Local diffuse probe values

`ReflectionProbe::diffuse_intensity` is a finite double multiplier from 0 to
1000000, default0. It reuses the assigned map and influence box for base diffuse
illumination. `intensity` still controls local base reflections independently.
Zero in both fields omits the probe; eight active assigned probes total are admitted.
Map yaw and edge fades apply to both paths; box projection is specular-only.
The installed reflection_probe.cpp sample writes both values and the existing live
service admits the diffuse scalar. Rebuild exact SDK modules for the appended
public layout. ABI1 tables and asset identities do not change. This is map-based
local lighting, not a sampled irradiance grid or automatic multi-bounce solver.

## Light image values

The installed Light value has a nullable `texture` reference, `image_width`,
`image_height` and `image_tiled`. Directional/Spot use a Color/HDR 2D Texture and
Point uses a Cube; clear the texture for Rectangle/Sphere. Image RGB multiplies
ordinary direct-light radiance and alpha is ignored. Directional full dimensions
are 0.001–10000 world metres, default1; tiling defaults to false and preserves the
Texture sampler's wrap intent. Node scale does not multiply these dimensions.

The installed `soft_shadows.cpp` sample writes matching built-in 2D/Cube references
and all image values through ordinary native Light access. Rebuild exact modules
for the appended layout/fingerprint. ABI1 callback tables remain unchanged; editing
a project fingerprint cannot make an older module compatible. Image dimensions
and tiling support the existing runtime property tuning service; editor texture
assignment remains an authored Edit operation.

## Area emitter dimensions

Use `LightKind::Rectangle` / `LightKind::Sphere` in `Light::kind`. Set full
`emitter_width` and `emitter_height` for Rectangle or `emitter_radius` for Sphere;
all are finite0.001–10000 world metres, independent of node scale. Defaults are
1/1/0.5. Aim Rectangle with its basis/rotation; Sphere needs no orientation.
Intensity is distant normal-axis candela for Rectangle and distant omnidirectional
candela for Sphere. Range uses center distance. Rectangle emits from one side;
Sphere is opaque and does not illuminate receivers on/inside it. Area shadows are
unsupported; the installed example clears `cast_shadows` for the two area kinds.

These appended C++ Light members require matching SDK/runtime fingerprints and a
module rebuild. The C ABI table is unchanged. The installed sample exercises all
five kinds and dimension fields through ordinary and SDK Play hosts, recovery and
live validation. It is a field-access example, not a complete visual game.

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
| Process2, UI1 and recovery sections | Private independently versioned protocols; no public compatibility/save-game claim. |
| Build yymmdd-counter | Distribution identity only; never substitutes for API/format compatibility. |

Renderer-only edits do not change the gameplay SDK contract. CMake rejects incompatible supported package configurations; runtime fingerprint checking verifies cooperative module declarations, not every possible compiler switch a malicious or misconfigured client could hide. New registering SDK code restarts the runtime; there is no project gameplay in-place DLL replacement.

## Thread/process and fixed-clock map

| Owner | Work |
|---|---|
| Runtime owner thread | Flecs, input latch, gameplay, navigation, physics synchronization/adoption, animation, transforms, audio commands, authoritative UI models/actions, diagnostics/profiles. |
| Jolt workers | Solver jobs; joined before adoption. No Flecs access from solver callbacks. |
| Audio callback/device | miniaudio-owned playback/atomic notifications; no gameplay clock or Flecs mutation. |
| Editor presentation thread during Play | Diligent viewport and reusable RmlUi presenter; copied models and semantic commands only. Native presentation faults share editor failure domain. |
| Headless | Same required gameplay composition, no ImGui/D3D12/RmlUi presenter or editor dependency. |
| Conversion/build workers | Bounded official gltf2ozz and navigation generation; results admitted before publication. |

Actual fixed tick order in RuntimeSimulation is: input latch → navigation synchronization → module input borrow → Flecs InputMonitor → Gameplay (SDK systems) → Animation → PostAnimation → Navigation movement → PrePhysics synchronization/queued commands → Physics → PhysicsAdoption → PostPhysics → final transforms → end module input borrow → particles → pose capture → audio synchronization → discontinuity snapping. No relative order is promised between unrelated systems in the same phase. Gameplay code declares its own Flecs dependencies when needed.

UI custom model values are published by their actual fixed gameplay system; they are not secretly reevaluated in a later presentation phase. The runtime adds authoritative tick/paused when extracting a UI snapshot. Presentation extracts/interpolates completed state independently. Ozz presentation sampling may evaluate a derived interpolated pose; it does not advance authoritative animation time.

Pause stops physics, agent motion, authoritative animation and gameplay. Audio gameplay group pauses; Step stays silent. Step executes exactly one gameplay tick. RmlUi continues presentation-time layout/input while paused. Stop destroys the runtime world and retires presentation. Recovery reconstructs a candidate first: physics uses exact bounded solver state; animation validates asset revisions/playback; navigation validates asset digests and recomputes paths; audio follows existing autoplay restart policy; UI reconstructs from new session/generation and discards stale commands/DOM state. Failed candidate publication retains the prior live world when available. This is transient recovery, not a save-game system or arbitrary native-state migration.

## Diagnostics and profiling

Built-in structured diagnostics retain appropriate EntityId/AssetId/source/property/module/tick where known. Exact text callbacks add module/role/current tick automatically; do not put raw library pointers in a record. Private UI command failures stay correlated to command/session/generation in their acknowledgement; they are not a second logging service.

Available CPU scopes include module Registration/Startup/Shutdown, FixedSimulationTick, TransformEvaluation, JoltUpdate, AudioAssetResolve/AudioSync, FixedPlayback/SampleAndLocalToModel, NavLoad/NavQuery/NavAgentUpdate, PresentationExtraction, UI ModelSnapshot/CommandValidation and SDK module-named samples. Records are bounded and opt-in; recording disabled still accepts valid instrumentation calls. These are CPU measurements, not GPU timing or a full profiler UI.

## Deliberately deferred

The exact SDK does not promise a stable third-party ABI, arbitrary in-place module
reload, a universal persistent AssetHandle, networking or a save-game system.
Use the installed headers and [contract inventory](extension-contracts.md) for the
available boundary. Engine asset import/cooking and editor transactions remain
owned by their existing subsystems; they are not general module callbacks.

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
resource limits before owner-thread schema admission. The editor admits the copied
schema through its existing authoring owner; supported custom values participate
in Inspector, Scene/Prefab persistence, history and explicit schema migration.
Live field updates clone the current native value through its copy hooks and write
only changed members recursively before committing. Other native state is retained;
unreflected runtime state does not become serialized or recoverable through this
operation. Native types must support the required copy semantics. Complete vector updates
set the requested count through the existing lifecycle owner, including shrinking
and clearing owned entries; selected-leaf animation does not change counts.

Editable scalar fields can also be bound in a Timeline clip. Floats/doubles
interpolate; Boolean, integer, enum and bitmask values hold until the next key with
exact native integer storage. Use its existing component/field/admission bindings
and source7 value categories for discrete fields. Native widths/ranges/choices still
apply. Source8 adds `member_path` arrays for editable struct leaves; readonly
ancestors are rejected. Source9 extends that prefix with an `element_path` beginning
at the first collection: positional indices for unkeyed entries and declared local
keys for keyed entries. Fixed arrays and admitted native Meta vectors reuse the
existing reflection/copy owner. Missing elements reject; animation never resizes a
collection. Arbitrary project opaque containers and UI bindings remain unsupported. Gameplay runs before Animation; an SDK
system that needs this tick's sample can use `host->post_animation_phase` and
`host->fixed_tag`. The canonical ComponentAnimation example's installed SDK target
uses that phase and the original project sources. See
`docs/transform-animation.md` in a source checkout and the
Timeline guide in the combined editor manual. Rebuild modules with the matching SDK.

## Cooked resources in gameplay

### Navigation Behavior

The installed value header `forge/navigation_behavior_components.hpp` declares
NavigationBehavior (graph, target, anchor, enabled) and the runtime-only
NavigationBehaviorCursor (state, last_transition, elapsed, transitions). Bind them
to `forge.navigation_behavior` and `forge.runtime.navigation_behavior_cursor` in
the shared Flecs world. Cursor progress does not inherit from templates and is
not an authored Scene/Prefab field or a durable save-game payload.

NavigationAgent remains the movement owner. A graph's actions only supply its
destination intent. Native code may read the cursor or update ordinary behavior
configuration at its declared fixed boundary. The engine-side graph parser is
not installed as an SDK implementation library.

Use `FORGE_SDK_RESOURCE_NAVIGATION_BEHAVIOR` (kind8) with the existing Resources
request/inspect/release callbacks to retain a CPU graph subscription. Request the
Navigation and Resources capabilities; inspect readiness and requested/retained
revisions before claiming a replacement. Tokens are scoped to the module/world;
they are not asset IDs or saved pointers. Releasing one token leaves other
subscriptions and copied engine leases intact. The C host table layout is unchanged;
rebuild against the exact matching SDK as usual. The installed
`navigation_behavior_gameplay` example checks the public layouts, reads realized
progress and observes a kind8 subscription through the checked host callbacks.

Graph creation and editing are described in the FORGE user manual's Navigation
Behavior chapter. Game modules do not need to reimplement graph evaluation.

### Tile Map cells (development)

An authored `TileMapRenderer` realizes an independent runtime map instance. Use
`sdk::Client::tile_map(scene_uuid, entity_uuid, info)` to inspect its copied CPU
status. Wait for `info.ready`; `info.current` separately indicates whether the
adopted asset matches the requested revision. Layer enumeration returns scoped
keys; cells use these keys and signed int32 local X/Y addresses. Cell queries and
observation release work while paused; submissions require a fixed callback and
the existing Resources permission/provider.

```cpp
ForgeSdkTileMapV1 info{};
ForgeSdkTileLayerV1 layer{};
if (client.tile_map(scene, entity, info) == 1 && info.ready &&
    client.tile_layer(scene, entity, info.generation, 0, layer) == 1) {
    ForgeSdkTileLocationV1 at{layer.key, -1, 0};
    ForgeSdkTileCellV1 cell{};
    if (client.tile_cells(scene, entity, info.generation, &at, &cell, 1) == 1) {
        // In a fixed callback; tile_key is an admitted Tile Set definition key.
        ForgeSdkTileWriteV1 write{at, {tile_key, 0, 0}};
        token = client.write_tiles(scene, entity, info.generation, &write, 1);
    }
}
```

Retain a returned nonzero token only in this module/world. Inspect with
`tile_edit`, then release it when finished. Pending release cancels; releasing
Applied does not undo cells. Stop cancels outstanding owned commands. Submissions
publish at the next fixed boundary and recheck the target/adopted generation.
`reset_tiles` explicitly discards edits and adopts current assets. Reads return
1 success, 0 unavailable/not ready, -1 invalid and -2 stale generation; an empty
cell is a successful zero tile. Batches are atomic and limited to4096 entries.
See the [development map contract](tilemap-foundation.md) for limits and readiness.
Map painting/rendering, Static/Kinematic collision and sensor regions use the same
instances and publication owners. Static or Kinematic nonsensor Map bodies also support the
existing PhysicsBody2D.one_way policy on painted rectangles and original
front-facing custom edges; public geometric
queries still see full shapes. See the [Map contract](tilemap-foundation.md)
for publication, examples and current limits, and the editor's Tile Maps manual
for the authoring workflow.

Kinematic one-way Boxes and full-cell/custom-polygon Maps use the same public PhysicsBody2D flag,
queued `move_2d` target controls and geometric queries as ordinary bodies. Keep
Sensor off. Dynamic and sensor one-way bodies remain unsupported. This
extends admission without adding a public field, callback or checkpoint payload;
rebuild modules against the exact SDK shipped with the matching runtime.

The installed `tile_map_probe.cpp` demonstrates copied reads and fixed submissions
without private headers. It is an integration probe, not an editor or save format.

### Ordered Sprite frames

In a fixed system that declares `.write<forge::SpriteRenderer>()`, copy the current
value, change it and set it back through Flecs:

```cpp
auto image = actor.get<forge::SpriteRenderer>();
image.region_enabled = false;
image.frame_regions = {{1, 1, 13, 14}, {18, 3, 12, 10}};
image.frame = 1;
image.mip_filter = std::uint32_t(forge::SpriteMipFilter::Linear);
actor.set(image);
```

`forge/render_components.hpp` includes the installed `forge/sprite_frames.hpp`
value header. Up to512 ordered rectangles use whole pixels relative to the current
image crop, or the full image when the crop is off. Empty restores the existing
grid; extents must be positive and every rectangle must fit the published image.
Sliced borders must leave a positive center in every entry. World size/pivot and
the image Material retain their owners. The normal integer frame Timeline binding
can animate these indices. Reordering changes the meaning of indices; it does not
remap animation keys. Rebuild modules against the matching SDK.

`mip_filter` is Disabled (0) by default, Nearest (1) or Linear (2). Filtering prepares each
logical frame independently, including trimmed margins and clockwise storage.
`TileMapRenderer.mip_filter` uses the same enum for defined tile images; declare
`.write<forge::TileMapRenderer>()` before changing that component. The appended
fields change the exact SDK fingerprint/layout. Both components require modules
built against the matching installed headers and libraries. Resource/native
admission can retain the last complete visible draw while a new bank is pending
or invalid. The installed `sprite_image_probe.cpp` exercises both component layouts.

For per-object surface values, include `forge/render_components.hpp`; its shared
`MaterialParameter` DTO is installed in `forge/material_parameter.hpp`.
`MeshRenderer.material_parameters` stores up to64 `{slot,key,parameter}` rows.
Use the logical slot shown by Inspector, not a physical mesh/material ordinal.
Scalar/vector/linear-color types are distinct; unused float lanes are zero.
Changing these values does not clone the shared material or compile a shader.
The renderer admits keys/types/domains against its actual selected materials and
retains the last good resource/value selection on rejection.

The installed **ObjectMaterialValues.hpp** sample shows ordinary C++ set/revert
helpers. Build **material_value_gameplay** from `sdk/sample` to exercise changes at
fixed ticks. It requires no renderer service to edit component data; visible
rendering still belongs to the existing presentation host. The helper's success
means a shape-valid value was set, not that an arbitrary key exists in the selected
material. The source checkout's `docs/material-assets.md` describes the renderer's
supported effects and limits.

This trailing native collection changes the exact SDK fingerprint. Rebuild modules
with the matching SDK; changing project settings to claim a new fingerprint does
not make an old binary compatible. The ABI1 host table is unchanged.

Copied reflection can represent engine-owned keyed collections with the existing
single-string `element_key` or additive `element_keys` containing2–4 distinct
string fields. Native projection, copied-schema reconstruction and extension merge
validate the same tuple. Existing single-key schema bytes are unchanged. This
does not admit arbitrary user opaque C++ containers or create row UUIDs.

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

Supported kinds are Mesh, Material, Texture, Shader and Collision. Typed identity is checked
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
existing asset-publication notification refreshes the captured catalog. Collision
subscriptions delegate to the physics owner: preparation can advance while paused,
but an existing body adopts replacement geometry only at a physics synchronization
boundary. Collision uses its existing256MiB native-shape pool rather than another
render-resource cache. Compare
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
require rebuilding native modules.

## Creating registered runtime renderables

Use `sdk::Client::request_entity(name, scene_uuid)` to queue an empty scene member.
This is the intrinsic `RuntimeEntities` capability, not an optional provider or a
descriptor permission bit. Calls are owner-thread only during startup/running;
requests from deferred fixed systems are supported. The runtime publishes pending
requests **before the next fixed tick**. Paused presentation does not create them;
Step does. Startup may request before the scene has loaded.

To create a prepared reusable hierarchy, use
`client.request_prefab(prefab_uuid, name, scene_uuid)`. It returns the same kind
of token, inspected/released through `inspect_entity` and `release_entity`.
Ready identifies the instance root. Each instance gets fresh member identities
and resolves portable internal links through its own existing member map.
The Prefab must already be admitted to the selected Scene. For a C++-selected
Prefab with no authored instance, add a required typed Prefab entry to the saved
Scene's **Runtime Dependencies**. Play and export consume that same graph; the
engine does not scan C++ strings or import source assets during spawning.

Source component inheritance, root transforms and spatial policies are retained.
The request overrides only the root display name. In Gameplay after Ready, set
native root transforms using the iterator's stage before physics runs. World-space
children keep their explicit world positions; moving a root does not move dynamic
World-space children. Place those explicitly using the existing transform/physics
rules. A detached group is validated before Scene publication; invalid preparation
does not publish some of its members. Trusted arbitrary native observer faults
are outside that guarantee. Empty and Prefab requests share the observation limits
below; publication admits at most4096 new rows per boundary and leaves excess work
pending in order. See Gallery64's optional `CodeExamples/SpawnPrefabs.cpp` for
delayed creation, placement and independent rotation.

The installed SDK's `sdk/sample` CMake project also provides `prefab_gameplay`,
which compiles the same Gallery source with its small integration driver. Runtime
Ready means the Scene group is registered; render/collision resource readiness
and native physics realization still belong to their existing services.

To destroy a created group, use ordinary Flecs destruction of its root through
the current stage. This retires its structural children; releasing an observation
alone does not. Later creation preserves native changes on existing instances,
including fields outside an authored property mask. The original Prefab definition
and its version remain unchanged.

The optional scene UUID is a scene AssetId. Null/empty selects the only loaded
scene at publication. An absent or ambiguous scene fails with a copied diagnostic;
the host never guesses between multiple instances of the same scene asset. Names
are1–255 UTF-8 bytes; the host generates a fresh EntityId. Poll the returned token:

```cpp
ForgeSdkEntityV1 entity{};
if (client.inspect_entity(token, entity) && entity.state == FORGE_SDK_ENTITY_READY) {
    forge::MeshRenderer renderer;
    renderer.mesh = forge::engine_primitive(0);
    world.entity(entity.native_entity)
        .set<forge::MeshRenderer>(renderer)
        .set<forge::LocalTranslation>({4, 5, 6});
    client.release_entity(token);
}
```

Include `forge/render_components.hpp`, `forge/transform_components.hpp` and
`forge/engine_assets.hpp` for those value types. Use the fixed iterator's world
when writing in a deferred system, and declare component writes in the Flecs
system. LocalTranslation, LocalRotation and LocalScale remain separate components:
a move must not write rotation or scale. Native trusted code must supply finite,
valid values and a normalized quaternion. The exact SDK does not make arbitrary
native mutations transactional or safe merely because they use reflected types.

Ready copies a world-local borrowed Flecs handle plus scene/entity UUIDs. The
handle is not persistent and must not be transferred to another world. Gone means
the original entity or loaded scene retired; an old request never retargets a new
scene instance. Failed requests retain their diagnostic until released. These
entities use normal Scene membership, identity resolution and presentation;
unregistered raw `world.entity()` objects are still valid internal ECS objects but
do not automatically become authored-scene/presentation members.

Releasing Pending cancels an unstarted request. Releasing Ready drops observation
only; it does not delete the entity. Stop cancels pending requests and releases all
observations; completed entities follow ordinary runtime-world lifetime. At most
64 outstanding requests per module and256 per world are admitted; release completed
tokens promptly. Token0/false reports rejection or unavailable context. This API
does not alter the editor's authored scene, add scene Undo, write project files,
or promise generic recovery of custom native runtime state. No Diligent pointer or
resource ownership is exposed. The appended callbacks and value headers are part
of the exact fingerprint; rebuild matching modules. The retired movement-only ABI1 path is absent.

## Character mechanics and filtered physics queries

Declare the Physics capability and provider dependency for fixed-tick calls. The
size-checked exact SDK exposes `Client::character` and `character_command` with
operation0 (planar velocity),1 (jump),2 (crouch), or3 (checked placement),
as documented in `native_sdk.h`. Observations copy pose, velocity,
ground classification/normal/velocity/position, supporting EntityRef, crouch state
and blocked/jump results. Commands request planar world velocity, grounded jump,
crouch or checked placement. No input binding or FPS camera is implied.

`raycast_filtered` accepts a layer mask and sensor policy. `shape_cast` accepts a
centered Box/Sphere/Capsule/Cylinder with fixed orientation and linear displacement;
results are copied entity/point/normal/fraction values. No mesh or angular sweep is
claimed. These APIs share the owner/fixed-tick guards and existing PhysicsService;
Jolt pointers and native BodyIDs never cross this boundary.

`FORGE_SDK_RESOURCE_COLLISION` requests a cooked asset through the resource service,
with ordinary module/world token ownership. Observe readiness before introducing a
new unloaded AssetCollider. Optional failed preloads do not fault unrelated physics.
The sample `samples/native_sdk/physics_probe.cpp` executes controller/query callbacks;
the physics acceptance runner checks all seven collision families through actual
shared-SDK resource callbacks before and after source-free relocation.

## Animation timing observations

Declare `FORGE_SDK_ANIMATION` in allowed capabilities. Requiring it additionally
needs dependency `forge.animation`. Register marker consumers on
`host->post_physics_phase` and add `host->fixed_tag`. `Client::animation_event`
returns bounded copied, current-tick broadcast observations; it does not consume
an occurrence. The source checkout's `docs/animation-markers.md` describes bounds, offsets,
removed-target behavior, lifetime and recovery rules.

## Navigation agent progress in C++

Use `Client::entity_reference` during the fixed callback to obtain the registered
scene/entity UUIDs, then `Client::navigation_agent` with `ForgeSdkNavigationAgentV1`.
The call returns1 for a copied observation,0 for unavailable/absent, and-1 for
invalid arguments/context. A valid-size failed output is cleared. Navigation must
be permitted and available; startup, controls and foreign-thread calls cannot read
fixed samples. The C boundary uses the same rules and requires exact output size.

`phase` uses `FORGE_SDK_NAV_AGENT_*`; `status` is the independent route/admission
status. Test `sampled && has_path` before reading position, net world velocity,
remaining waypoint-route distance or projected endpoint. Pending/Idle/Disabled
have no invented metrics. Failed carries the attempted tick and reason status.
Changed agent settings immediately invalidate a prior arrival. Paused denotes
zero configured speed; game pause simply freezes the last completed observation.

Gameplay reads the previous completed navigation tick. PostPhysics reads this tick
because navigation movement runs earlier. `navigation_probe.cpp` demonstrates an
original C++ return journey: when the prior tick reports Arrived, ordinary Flecs
NavigationAgent destination fields change, then navigation replans/moves on the
next boundary. Observation does not queue movement or introduce an AI controller.
The callback changes the exact private SDK fingerprint: rebuild modules with the
matching SDK. Existing authored NavigationAgent layout and NavMesh format are unchanged.

### XY navigation assets

The same `navigation_query` and copied agent APIs operate in world XY when the
selected asset was baked with Plane2D(XY). Projection returns input visual Z;
paths and movement keep starting Z; copied velocity Z is0. Destination Z does not
change visual depth. Both endpoints must still contain finite bounded values.
A Navigation Agent retains sole nonphysics movement authority. Character-controller
code can query routes without adding an active Navigation Agent writer.

`NavigationRegion2D` is public reflected source data, separate from Sprite art and
colliders. Its owning `std::vector<NavigationRegionVertex>` uses the matching Flecs
C++ lifecycle and CRT; copy values normally, never memcpy its storage. Empty points
keeps the centered Width/Height rectangle; nonempty3–64point simple CCW contours
select a polygon. Values, Meta offsets and deep-copy/removal are checked by the
installed planar sample. Rebuild modules against the matching exact kit after this
layout change; the C host callback table does not change. Bake static regions in the editor/MCP before Play. Runtime source changes
invalidate that asset; they do not rebuild it. The installed sample target
`planar_navigation_gameplay` exercises queries, region layout and copied progress
through the matching shared SDK. Rebuild modules against that exact SDK; authored
NavigationAgent layout and recovery2 stay unchanged. XY assets use the guarded
navigation envelope3; existing3D envelope1/2 assets retain their original semantics.

## Overlapping audio effects

During a fixed owner callback, `forge::sdk::Client(host).audio_one_shot(scene_uuid,
entity_uuid)` queues a nonlooping instance of the target AudioSource clip alongside
its primary sound. The existing C callback accepts `FORGE_SDK_AUDIO_ONE_SHOT` (4);
Stop/Restart/Pause/Resume remain0–3. A successful enqueue is not a physical-output
guarantee: invalid/admission/voice-budget failures are diagnostic at execution.
Effects follow source values, share PCM, honor game/source pause and retire at the
end. Stop clears the source's effects; Restart affects its primary only. The
audio_probe sample exercises these controls. Matching SDK rebuilds are required
when the installed header/bridge fingerprint changes; ABI1 and table sizes remain.

## XY 2D physics

The matching SDK exposes separate 2D values and `forge::sdk::Client` methods.
Request the existing Physics capability and depend on `forge.physics`; there is
no separate 2D service or clock. `PhysicsBody2D`, `BoxCollider2D`,
`CircleCollider2D`, `CapsuleCollider2D` and `PhysicsJoint2D` are authored values in
`<forge/physics_components.hpp>`. Vendor headers and native handles stay private.

Positions and linear velocities use world XY meters and meters per second.
Angles and angular velocity use radians about positive Z. Physical simulation
preserves authored depth and presentation scale. Submit movement, forces,
impulses and velocity commands from an owner-thread fixed system before Physics.
Each command queues independently in the shared physics FIFO. Read copied body
state, point velocity, filtered rays, complete overlaps and contact observations
from the supported fixed contexts; PostPhysics observes the completed tick.

`ForgeSdkPhysicsContact2DV1` carries canonical references, realization epochs,
availability flags and the simulation tick. Reading a slot does not consume it.
An End may refer to a retired realization while the same authored UUID already
has a replacement: check its epoch and availability before acting on a current
body. Observations expire at the next admitted synchronization. They are neither
durable identities nor a general event bus.

The installed `sample/Physics2D.hpp` contains stateless launch and sensor-reading
helpers; `physics_2d_gameplay` and `physics_2d_denied_gameplay` in the installed
sample CMake project exercise the real host with allowed and denied permissions.
These are validation probes with a specific fixture, not behaviors to attach
unchanged to Gallery75. Gallery75 itself needs no optional module. Compiled
project modules are included by the ordinary export path; players need no SDK
or compiler.

`sample/Physics2DJoints.hpp` provides typed Hinge/Slider velocity-motor and
Distance-spring helpers. Register the engine's `forge.physics_joint_2d` alias and
edit runtime values in the owner-thread fixed Gameplay phase before Physics.
The existing Physics boundary validates and realizes the full candidate; these
helpers neither update saved source nor synchronously call the native solver.
Hinge/frame/limit values use degrees and Hinge motor speed uses degrees/s; Slider
speed uses m/s, with N*m and N actuator budgets. The installed
`physics_2d_joint_gameplay` target is a dedicated both-host acceptance fixture,
not a behavior to attach unchanged to Gallery76. That Scene runs its authored
settings without a module. Rebuild matching modules for the added builtin header
fingerprint; ABI1 remains unchanged. See the contract for scalar limits and
fresh-candidate checkpoint6.

Use the matching SDK after this callback-table/fingerprint update; native ABI1
is retained. The first domain profile supports 1024 2D bodies within the combined
body limit, the shared 4096-command FIFO, 256 complete overlap results and bounded
copied events. Bullet does not provide exact projectile timing or bullet-to-bullet
CCD. The native fast-sensor path retains eight candidates per body per step.
See the installed [2D contract](physics-2d.md) for admission, bounds and recovery
scope. Arbitrary native gameplay state still has no automatic SDK Play recovery.

## Timers during pause and slow motion

Use the existing `game_query` callback with `FORGE_SDK_GAME` from a module's
`controls` callback. Its copied `clock` object provides `unscaled_time` and
`unscaled_delta` in seconds, plus unsigned `unscaled_sequence`. Pause and zero
simulation speed do not stop these observations. Fixed systems still receive the
usual fixed tick delta. Repeated queries/catch-up callbacks can share a sequence;
consume an interval once or subtract elapsed observations to bridge skipped samples.

Build installed `sdk/sample` target `time_scale_gameplay` for an original example
of input-driven speed changes and a per-entity timer. The timer starts on its first
control callback. Observations are raw elapsed monotonic time, including long
stalls; choose any UI clamp in project code. Same-session Scene switches retain
the host timeline, while new Play/process starts at zero. Timer state is ordinary
project state and is not automatically saved or recovered. No new ABI callback or
SDK layout is required for these copied status fields.

## Gamepad feedback

Declare `forge.game` and `FORGE_SDK_GAME` and submit the existing `game_request`
JSON operation `rumble`, with numeric `low`/`high` strengths0..1 and integer
`duration_ms` up to5000. Use zero strengths to stop; nonzero strength requires a
positive duration. Poll/release the module-owned receipt; an unsupported device
is a normal failed result that should not stop your game. `game_query` reports
`gamepad_rumble_host` for route availability, not physical device capability.

The installed `gamepad_feedback_gameplay` sample target reads two control-domain
actions (`12345678-1234-4234-8234-123456789b01` pulse and
`12345678-1234-4234-8234-123456789b02` stop), submits bounded feedback and records
completion/failure counters. Its fixture binds R/gamepad South and T/gamepad East.
Optional control actions ending `b03`/`b04` bind Y to assign the first queried
connection to logical slot 2 and U to pulse that slot. They use the same request,
receipt and nonfatal failure counters; they do not retain a native handle.
It is a development example with a matching input map, not a drop-in Cube behavior.
Editor playback requires a focused visible Game panel with routed input; the
standalone host requires its game window's focus. Stop and focus/device/session
changes stop owned feedback. No trigger motors, effect mixing or persistent
haptic data are supplied. No new SDK C callback/layout is needed.

## Local controller slots

Bind independent actions to `pad.1.*` through `pad.16.*`; `pad.*` remains the
active-controller alias. Sticks range -1..1 and triggers0..1. Pair both axes of
one indexed stick for radial conditioning. Game/user input defaults can replace
the binding's inner deadzone. `Client::action` copies current fixed-domain
values; `Client::control` copies control-domain values in the control callback.
Use numeric X/Y for movement, rather than digital held/edge flags on analog actions.

Copied Game query includes `gamepads:{available,revision,active,devices}` and
`gamepad_assignment_host`. Device entries contain `{device,name,slot,connected,rumble}`;
device is an opaque connection token, not a saved identity. An unavailable native
adapter differs from an available empty list. To reassign, submit
`{"operation":"gamepad_assign","revision":7,"device":"copy-current-token","slot":2}`
through the existing Game request API. Zero unassigns. Inspect/release its scoped
receipt; replace the example revision/token with the current query values and
refresh on stale revision/connection or failure. An occupied
slot is displaced. The host stops only affected feedback and neutralizes old/new
logical input. Global focus/capture resets still release all input. Assignments
last for the host session; new connections fill the lowest free slot. Slots1..16
and16 tracked connections are bounded. No per-player UI-focus system is implied.
Raw sensor access is described under Controller motion.

Rumble accepts optional `slot` 1..16; omitted/0 targets the active alias. Zero
strengths with an indexed slot stop that device; an untargeted zero stops all
owned feedback. Native support/focus/routing still determine success. SDK Play
uses its existing correlated platform offer/ack lifecycle for assignment/output;
no callback/table/layout change is required.

Installed `multiple_gamepads_gameplay` builds `multiple_gamepads_probe.cpp` and
the original `MultipleGamepads.cpp` as module `project.multiple_gamepads`.
It needs `forge.input` and `forge.transforms`. Its saved ActionIds and scoped
marker references match Gallery92 and149; ControllerSlots.hpp carries the latter
scene's authored identities. Keep that header beside MultipleGamepads.cpp. Each marker moves at three world units per
second times its conditioned action vector, bounded to X±7/Y±4. The module uses
ordinary authored transforms in the runtime copy; it adds no authored schema.
Build/install the matching module kit and use current code when exporting.

## Music and UI audio during gameplay Pause

The installed sdk/sample directory contains unpaused_audio.cpp, its guide and an
input map. Build unpaused_audio_gameplay to exercise Gallery84's independent
music/UI sources through public control callbacks. AudioSource.pause_with_game
is on by default; only opted-out active sources accept control-domain audio
commands. Ordinary fixed audio calls keep queued semantics. Rebuild against the
matching SDK after the appended value/fingerprint change. See [the audio contract](extension-contracts.md).


## Source filter values

The installed audio_gameplay sample writes AudioSource.low_pass_enabled,
high_pass_enabled, low_pass_hz and high_pass_hz through ordinary fixed-domain
component values. Both filters default off; cutoff defaults are 20000/20 Hz and
admitted finite values range from 20 through 20000. Existing audio commands retain
their permission/pause policy, and one-shots inherit these source values. No
native filter node or coefficient crosses the SDK. Rebuild for the matching value
fingerprint; changing live filter values resets DSP history without restarting
the clip. See [the audio contract](extension-contracts.md).

The same sample sets AudioSource.echo_enabled, echo_seconds and echo_feedback.
Echo defaults off, interval .25seconds and feedback .35; finite admitted bounds
are .01..1seconds and0...75. Zero feedback bypasses echo. Each primary/one-shot
owns its effect history. Stop/Restart clears it, Pause freezes it and Resume can
continue a post-clip tail without replaying the finished clip. Native delay nodes
remain private; existing typed component and audio control APIs are unchanged.
Rebuild exact modules for the appended AudioSource value fingerprint.

## Touch action example

The installed sample target `touch_input_gameplay` builds `touch_input_probe.cpp`, `TouchCursor.cpp` and `TouchInput.hpp` using only the installed SDK. Module `project.touch_input` implementation1 depends on `forge.input` and `forge.transforms`. The four ActionIds in `TouchInput.hpp` match Gallery88's saved digital contact-down and separate Axis1 X/Y/pressure bindings. Call `sdk::Client::action` only during the fixed callback; copy values for the current tick. Positions and pressure are0..1; absent/released contacts are neutral. No raw SDL IDs or pointers cross the SDK.

`TouchCursor.cpp` registers a system that checks the ordinary scoped entity reference, moves Gallery88's marker within its authored16x10 canvas, resizes it by pressure and hides it after release. It changes only the runtime copy. Contact slots0..7 stay stable while held; new contacts reuse free slots. Bind the required `touch.N.*` controls in project input settings. Separate Axis1 actions preserve independent screen coordinates; Axis2 normalization is intended for directional vectors. Physical touch requires a desktop touch driver. Sensors/mobile hosting and multi-player assignment remain outside this input profile.

### Touch gesture sample

Installed target `touch_gestures_gameplay` builds `touch_gesture_probe.cpp`, `TouchGestures.cpp`, `SingleTouch.cpp` and `TouchGestures.hpp` as module `project.touch_gestures` implementation1. Dependencies are `forge.input` and `forge.transforms`; ActionIds match Gallery89's five saved actions. `read_touch_gesture` uses current fixed `Client::action` copies: Active is digital, pan is normalized image-center movement, pinch is log-scale delta, rotation is screen-clockwise radians. These are per-latch deltas, not rates: apply once without dt and retain movement from a gesture that already ended. Control-phase bindings can instead use the existing `Client::control` API in control callbacks.

The example affects only the scoped Gallery89 marker, bounds pan to X±32/Y±20 and scale0.25..4, and uses ordinary LocalTranslation/Rotation/Scale. It does not author source or impose engine camera behavior. Contact pairs use actual image pixel aspect, retain their pair until a member leaves, and rebase replacements or subpixel coincidence without jumps. No native pointer/identity crosses the SDK; C tables and fingerprint are unchanged by these additive semantic controls.

Gallery91 uses the same module's scoped `forge_register_system_SingleTouch` callback
and seven saved digital ActionIds. Tap rotates90degrees, Double tap resets all
transforms, Long press toggles scale1/.5 and directional swipes move one unit within
X±6/Y±3. Read `ForgeSdkActionV1::pressed`, not held, for each recognition pulse.
Double tap also emits Tap; this example gives reset precedence. The classifier is
owned by the physical input adapter; SDK/MCP semantic injection sends1/0 pulses
directly and does not simulate an OS contact. Source values remain unchanged.

## Raw controller motion example

The installed `gamepad_motion_gameplay` sample wraps the original Gallery93
`GamepadMotion.cpp`. Register its fixed system and unscaled control callback.
It optionally admits Game, queries current motion capabilities, requests enabling
supported Player1 sensors, inspects/releases receipts and refreshes the shared
device revision between configuration requests. A failure at the same revision
is not retried continuously. No C ABI changes or private core input headers are
required: the module uses copied Game JSON and public transforms.

`gamepad_motion:{available,routed,revision,active,devices}` contains raw sensor
records `accelerometer`/`gyroscope`, each with supported/enabled/measured, rate_hz,
timestamp_ns, sensor_timestamp_ns and value[3]. Acceleration is m/s² including
gravity, gyro rad/s; natural device axes Xright/Yup/Ztoward user. Rates0 and sensor
timestamp0 mean unknown. Event timestamps are nanoseconds, with positive values
only for a measured sample. Unmeasured records have zero vectors/timestamps;
measured zero remains distinct. No orientation/calibration/fusion is supplied.

The `gamepad_sensor` request takes current revision/device, sensor and Boolean
enabled. A changed mode advances the same revision as gamepad assignment. Read
again before another request and never persist tokens. Capture/focus/Stop clear
readings; raw data needs a fresh native event in current routing. Desktop SDK
Play and standalone compose this host profile; ordinary simulation without Game
leaves the example neutral. The fixed sample maps X/Y at0.2/1 world units per SI
unit and bounds display positions to the canvas. Source-free export keeps Scene
and Sprite identities; native configuration is repeated in the new host.

## Change the game's UI language

The installed `forge/ui_components.hpp` supplies `UiLocalization`: a typed
catalog reference and an owning `std::string locale`, defaulting to `en`.
Use it on the Scene's single configured UI Localization controller. A fixed
system can assign a valid language tag and mark that component modified; UI
commands and live Inspector tuning write the same value. Do not publish a second
language model. The installed `ui_localization_gameplay` sample demonstrates
native access, exact layout and independent string copy/destruction.

These are exact-SDK C++ values with matching compiler, runtime library and Flecs
lifecycle hooks. Do not memcpy the component, borrow its string buffer across a
mutation or World retirement, or treat this as a portable C string ABI. No RmlUi,
DOM, catalog parser or resource manager is installed with the value declaration.
The C host table is unchanged. Rebuild the module when the SDK fingerprint
changes. Ordinary runtime recovery preserves the runtime Scene locale; prepared
SDK Play has no recovery checkpoint. Stop restores authored values unless a
supported Keep values operation deliberately retains the change.

## Application-state conditions

The installed `forge/application_state_ecs.hpp` supplies runtime-owned relationship
tags, schema-time semantic entity helpers, readonly current/membership queries and
owning `ApplicationStateEntered`/`ApplicationStateExited` values. Use an ordinary
Flecs query with a constant domain source to condition a native system:

```cpp
const auto domain = forge::application_state_domain(world, "app");
const auto game = forge::application_state_entity(world, "app", "game");
world.system<MyGameplayComponent>()
    .kind(host->fixed_phase)
    .with<forge::ApplicationStateMember>(game).src(domain)
    .each([](MyGameplayComponent& value) { /* update this component */ })
    .add(host->fixed_tag);
```

A published Application States definition owns the domain/states. Query helpers
resolve existing native relationships without creating runtime registrations or
using a module-local C++ type cache. They return owned strings/booleans and do not
install a host ABI. Lifecycle notifications run after the complete configuration
publishes; copy event values when retaining them beyond the observer callback.

`forge::sdk::Client(host).request_application_state(domain,state,revision)` queues
through the existing Game capability. It returns zero for invalid keys or an
unavailable service, otherwise an ordinary request token. Inspect/release its
receipt; stale/computed/inactive/unknown requests can fail after submission. The
Game service is available in shared-SDK Play and the standalone GameSession host;
legacy exact-SDK Play supports queries/events and host/UI controls but not this
queued service. Do not mutate active/membership pairs from project code.

The installed `application_states_gameplay` sample demonstrates native ancestor
conditions, owned lifecycle events, request receipts and fresh-World reset. See
[Application states](application-states.md) for definition and boundary semantics.

### Stock object activity

The installed `forge/application_state_components.hpp` also provides
`forge::ApplicationStateGate`. Bind its existing builtin during schema setup:

```cpp
world.component<forge::ApplicationStateGate>("forge.application_state_gate");
```

An admitted Ready runtime entity can receive the value through its Flecs stage:

```cpp
world.entity(ready.native_entity)
    .set<forge::ApplicationStateGate>({true, "app", "playing"});
```

The configured definition must contain those keys. The optional component is
disabled by default. A true condition uses current leaf/ancestor membership and
combines with structural parent conditions, including for World-space children.
Stock subsystems compose it with their existing enable/availability rules; their
normal disable/re-enable lifecycle applies. No authored enable field is overwritten.
An ordinary project query still sees the component on an inactive object, so
project systems need explicit query conditions of their own. Do not call the
host-only activation view from an SDK module or mutate runtime membership pairs.
The owning string values require the same exact SDK/Flecs/toolchain/CRT boundary
as other installed owning-string values.

### Runtime root lifetimes

For a Ready entity/Prefab creation request, `client.scope_runtime_entity(token,
domain, state, optional_revision)` queues an explicit state lifetime through Game.
Inspect the returned Game receipt before releasing creation observation. The root
and its structural children then retire when the selected active state or ancestor
exits. Observation release alone retains them; a common ancestor survives sibling
transitions. Scopes cannot be rebound, and Pending/foreign/released requests reject.
The matching installed `application_states_probe.cpp` sample exercises creation,
binding, receipt inspection, release and retirement. This requires the Game service;
legacy Play reports it unavailable and the helper returns zero. The C host table
layout remains unchanged.
