# Runtime, extension and project foundation

## Phase8 implementation checkpoint

The shared Play/standalone runtime-world composition, portable game-session owner,
validated game defaults/user overrides and independent save/settings storage are
implemented in [game foundation](game-foundation.md). Phase8 remains in progress;
graphical standalone hosting and complete game export are not delivered yet.
The historical design commitments below remain useful context.

Design commitments for later authorized consumers, alongside existing
[EngineModule](engine-modules.md), [core services](core-services.md) and
[extension contracts](extension-contracts.md). This does not implement Phase7.

## Execution ownership

| Owner | Permitted access | Synchronization / callbacks |
| --- | --- | --- |
| Runtime owner thread | World mutation, module/provider lifecycle, input adoption, fixed tick, checkpoint/replacement | Apply queued intents at declared boundaries; publish immutable presentation snapshots |
| Flecs worker/stage | Declared ECS reads/writes only through native stage/defer and access declarations | Native pipeline merge; no direct Diligent/audio-control/provider calls unless their contract explicitly allows it |
| Editor main thread | Authored commands, personal workspace and ImGui; editor presentation/device ownership | Never shares live gameplay DLL/world pointers; adopts IPC results with session/revision checks |
| Presentation/render owner | Diligent device/context, immutable extracted scene/UI inputs, GPU object realization/retirement | Frame boundary and completion fences; no arbitrary ECS mutation from draw callbacks |
| Audio callback | Bounded miniaudio mixing and already-prepared audio data | No filesystem, editor command, world mutation, blocking import or resource destruction; control thread queues prepared changes |
| Import/cook workers | Declared source snapshots and contained staging | Bounded result protocol; publisher validates generation and output before catalog changes |
| Script preview worker | Private world and contained native Script hooks | One candidate/process; parent retains last-good output on error/timeout |
| Future GPU upload tasks | Prepared immutable buffers on renderer-approved context/queue | Owner adopts completion; fence pins old/new resources until safe; no raw resource publication from generic background task |

Jolt job execution stays inside Jolt's world/provider ownership; resulting transforms are adopted
at the physics phase. Ozz sampling contexts and Detour query state are not shared unsafely across
jobs. RmlUi document/control ownership is separate from its presentation command transport.
No generic engine job system is introduced just to wrap existing bounded workers. Reusable job
request/cancellation/result vocabulary is the contract; parallelism starts with a measured consumer.

## Diagnostics and profiling

Problems owns actionable current conditions, deduplicated by stable diagnostic code, source,
entity/asset/property and revision. Resolution clears current state; it does not erase the Console
log. Console is chronological detail. Native Flecs Alerts generate ECS-state conditions and feed
Problems; they remain native Alerts rather than another parallel condition evaluator.
Compiler/import diagnostics carry job/tool/source location and severity, with navigation to the
relevant source/asset/document. Runtime faults identify session/world/module without exposing raw
pointers as durable identity. A stale diagnostic cannot select an unrelated recycled entity.

Flecs Stats reports native ECS internals; Metrics reports declared ECS measurements. FORGE profiling
measures engine/provider/IPC/import scopes not already supplied by Flecs. CPU wall intervals are
labeled CPU submission/wait time. Future GPU timestamps are delayed GPU measurements with device
capability/disjoint validity, never CPU Present duration relabeled as GPU time. Import/build
profiling records queue/conversion/validation/publication separately. Problems, logs, telemetry
and performance do not collapse into one text stream or one permanent default panel each.

## Build profiles and activation

| Profile | Composition and activation |
| --- | --- |
| Editor development | ImGui shell, authoring, workers, Diligent preview; native ECS inspection opt-in; no REST listener on startup |
| Headless development | Runtime/tools/providers and tests without editor UI; Script/Stats capability compiled where configured, activated explicitly |
| Exact SDK development | One matching shared Flecs/core host, exact compiler/options/source fingerprint; installed headers/provider permissions; rich registration startup-bound |
| Standalone game development — future | Visual host+simulation/providers+cooked resource reader; explicit development diagnostics switches; no authoring editor required |
| Shipping game — future | Same runtime data contracts, selected cooked content and startup Scene; no editor/importers/compiler/Explorer or auto-started development listeners |

Debug information, sanitizer instrumentation and profiling are build choices; REST/Stats/Script
being compiled does not start them. Shipping may retain native APIs needed by gameplay while
removing unrelated tools; a reduced addon profile requires its own compatibility fingerprint and
full tests, not per-client macro changes. Sanitizers remain separate validation builds. Build
identity is the global monotonically increasing yymmdd-counter, not ABI/schema/SDK version.

## Standalone visual executable

Choose a reusable visual host around the runtime simulation, with a single process as the initial
standalone game direction. It owns SDL window/input, Diligent presentation, resource pools,
RmlUi presenter and the existing Flecs/Jolt/miniaudio/Ozz/Detour runtime modules. Editor Play keeps
its separate simulation process for crash isolation; process topology is a host policy, not an
asset-format dependency. Share simulation/modules, render extraction, input action mapping,
resource loading and UI command interpretation. ImGui, authoring history, Content and editor
selection are editor-specific and excluded.

A cooked package manifest names the startup Scene AssetId, platform/profile, selected artifact
revisions and module fingerprints. Runtime resolves contained package paths from that manifest,
not developer source paths or current working directory. Missing startup content fails clearly.
The visual host's frame loop collects input, advances runtime fixed ticks, consumes interpolated
presentation and renders active game cameras/UI. A headless server remains possible without the
visual host. Full exporter/cooker/visual executable is deferred to the first authorized playable
standalone milestone; architecture alone is not evidence it already works.

## Extensions

Built-in EngineModule supplies dependencies, capabilities, lifecycle and native Flecs imports.
ABI1 remains the restricted versioned C interface. Exact SDK is deliberately compiler/build
matched, trusted C++ and startup-bound. Future third-party gameplay plugins must choose one of
those contracts with manifest/ownership validation, not silently mix private Flecs copies.
Trusted native editor extensions load at startup and require restart for changes; they can crash
the editor. Gameplay modules remain outside its process.

Internal editor registries may contribute commands, components/drawers, asset types/editors,
importers, viewport overlays/tools and Problems providers. Registrations have stable namespaced
keys, declared context/availability and ownership/lifetime; duplicate keys fail rather than win
by load order. Commands delegate to application mutations, drawers do not invent persistence,
and documents own independent Save/history/selection. Keep these private C++ interfaces until
real external consumers and compatibility tests justify freezing a public ABI. Native importer
code may execute in workers, but is still trusted code with process containment, not a security
sandbox promise.

## Project and source-control semantics

| Conceptual location | Ownership / source control |
| --- | --- |
| `forge.project.json`, project settings | Shared project intent; commit |
| `Assets/`, `Scenes/`, prefab/source assets and identity/import sidecars | Authored inputs and durable IDs; commit |
| `Native/` | Gameplay source/build description; commit |
| Declared packages/plugins | Versioned project dependencies/lock metadata; commit references and distributable sources as licensing permits |
| `.forge/cache/` | Reproducible local derived cache; ignore |
| `.forge/jobs/`, intermediate native builds | Disposable bounded staging/build output; ignore |
| Personal editor settings/layout/machine SDK paths | User/machine-owned; ignore, never silently written into shared project settings |
| Export/cooked build output | Explicit chosen output root; normally generated/ignored, release artifacts archived separately |

These are conceptual homes, not an automatic relocation of existing user projects. ProjectPaths
contains source, metadata and worker paths; reject escapes/symlink traversal according to existing
policy. External source references require an explicit future project policy, not accidental
absolute paths. Move/rename updates logical locators and sidecars while preserving AssetId;
case-only moves must be tested on Windows. Delete enumerates references, requires explicit
acceptance where destructive, and retains missing-target diagnostics. No Git UI is implemented.

## Test tiers

Focused iteration runs changed-domain unit/integration tests and cached fixtures. Pull requests
run core/profile/format/manual boundaries. Release/foundation gates run clean static and shared
SDK profiles on Linux/Windows, strict sanitizers plus separately classified known upstream
exceptions, editor controller/gesture tests, shader compilation/WARP render fixtures and relocated
packages. Future importers add small valid/malformed/truncated/cancel/stale/dependency fixtures,
deterministic artifact hash checks and resource lifetime/fence tests. Large conversion corpora
run on scheduled/release gates with immutable input/tool/cache keys. Performance compares repeated
same-profile workloads; thresholds require stable hardware/noise baselines, not hosted-runner FPS.
Physical GPU/DPI/input/browser/audio acceptance stays an explicit human checklist.
