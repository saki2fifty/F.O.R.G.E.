# Core services foundation

Phase 5.5 extends the existing context and process boundaries. Dependency pins, native ABI1, protocol2, scene/prefab semantics and the editor layout remain unchanged. User workflows: [Project settings](../manual/editor/project-settings.md) and [Gameplay input](../manual/editor/input.md).

## Ownership

| Scope | Authority |
| --- | --- |
| Defaults | Engine code: 60Hz, fixed-clock catch-up policy, empty action map |
| Project | `forge.project.json` v2: name, simulation_hz, startup scene AssetId + locator hint, input map; unknown fields retained |
| User/editor | Existing SDL preference directory settings/layout; `.forge/editor-views.json` is local workspace state, not shared project behavior |
| Runtime/session | RuntimeClock tick/pause, action snapshots, process/session IDs, diagnostic/profile buffers and library resources |

ProjectSettings is UI-independent and validates candidates before atomic single-file save. SceneDocument holds the project writer lease and is the editor's persistent mutation entry point. A direct headless caller must obtain ProjectLease before saving. Baseline checks detect non-cooperating changes but do not claim filesystem compare-and-swap. Settings history is not scene history; no cross-document transaction is introduced.

Startup references use AssetId, never filename identity. Resolution checks the source contents and detects duplicate matching IDs. A bounded project scan follows moves; missing/ambiguous identity is an error. A null startup opens an empty untitled scene. Legacy manifest1 uses its old locator once to resolve the scene identity, preserving the existing scene migration journal. It stays unmodified until explicit settings Save, which retains the exact original manifest in `.v1.backup`. Older/future versions are never silently downgraded.

## Services and built-in dependencies

EngineContext owns EngineServices before WorldContext. Its typed ServiceAccess exposes only Diagnostics and Profiling capabilities. Handles are weak: they do not extend owner lifetime; expired or restricted required access fails explicitly. Optional profiling can be absent. All access/mutation is on the owner thread. Consumers receive narrowed access or concrete borrowed references; there is no string-to-pointer service locator and no global subsystem singleton.

World bootstrap validates Core → Transforms → Prefabs dependencies before registration and executes that order in the same persistent Flecs world. The module-order helper rejects missing prerequisites, duplicate identities, missing required capabilities and cycles. It is a built-in composition mechanism, not a package solver. Existing registered Flecs types, phases, relationships and pipeline remain the ECS machinery. It does not replace Flecs scheduling.

AssetCatalog stays an explicitly project-owned metadata service. The renderer stays explicitly owned by editor composition with Diligent smart pointers. RuntimeInput is explicitly owned by RuntimeSimulation; runtime-only construction enforces the world role. Headless contexts construct no renderer, window or ImGui service. Optional project `modules` declares requirements from the current fixed set core/transforms/input, not dynamically enabled plugins.

## Diagnostics and failures

Diagnostic has severity (Trace/Info/Warning/Error/Fatal), category, text and optional typed EntityId/AssetId/PrefabMemberId, source locator, property ID, tick/session and module context. No native pointer is an identity. Storage retains the most recent 256 records, bounded to16KiB each (message capped at8KiB). Runtime errors retain existing `error` text and add structured `diagnostic`; editor command failures bridge into the same contract while keeping the existing Console status behavior. Existing format/API-specific error codes remain owned by their formats rather than being rewritten wholesale.

Malformed content, missing assets and unsupported user configuration are recoverable validation errors. Internal invariant failure is a programmer error: `fail_invariant` emits Fatal and throws logic_error so its owner boundary unwinds. Its current use is bootstrap construction, before a live context is exposed. Do not catch it as a successful content operation or continue mutating a corrupt world. Existing Flecs assertions remain upstream invariants.

## Instrumentation

Optional ProfileScope records category/name, duration, count and tick in a512-record ring. Runtime fixed ticks and presentation extraction, derived transform evaluation and prefab reconciliation have scopes. Enable through the context's Profiling capability; the default is off. `FORGE_DISABLE_PROFILING=ON` compiles collection out. Disabled scopes do not invoke the clock; tests inject a clock rather than sleeping. Scope destruction never propagates instrumentation failures. This is owner-thread CPU wall time, not GPU timing, flame graphs, allocation tracking or a production profiler. Existing editor frame averages remain separate and unchanged.

## Document schema convention

SchemaRegistry registers kind/current/readable versions, validator, detached migration and policy text. `forge_tools --document-schemas` exposes the five current contracts without editor or file writes. Scene preparation reuses `migrate_scene`; plain scenes stay v3 and structured scenes stay v4. Prefab/input/index currently have no historical migration. AssetCatalog uses the index contract before its project-specific validation. Project v1 needs source identity resolution and remains ProjectSettings-owned; the registry explicitly describes that path and accepts detached v2.

Read → validate → detached candidate → validate remains the common mechanism. Publication and backups are format-specific: the registry never writes files, allocates a second ECS world, or coordinates documents. Existing identity journals and scene backups remain unchanged. Unknown payloads remain opaque; no recursive UUID rewriting is added.

## Project paths

ProjectPaths owns canonical root and project-relative locator semantics. Persistent locators use forward slashes; backslashes normalize on all platforms. Internal dot segments normalize; root escape, absolute/drive/UNC paths, alternate data streams, invalid portable filename characters and Windows device names are rejected. Real filesystem canonicalization rejects symlink escape. Operation-specific extensions/reserved destinations still apply: prefab source `.prefab.json`, scene JSON excluding the manifest and local control area.

Windows locator comparison is ordinal case-insensitive (including duplicate metadata prevention); Linux uses exact case plus filesystem equivalence. Case-distinct Linux assets may conflict when moved to Windows and are not promised portable. Source spelling is preserved. Paths remain locators, never AssetIds.

Current conventions are reused: Assets; Scenes; Native; `.forge/recovery` (Saved); `.forge/cache` (derived cache); `.forge/native` (native build/artifacts). Merely asking for a directory does not create it. Local `.forge` contents must not be treated as shared project settings. This is not a VFS: archives, mounts, remote assets and cooked paths remain deferred. Filesystem checks assume a cooperative local project, not protection against adversarial concurrent symlink substitution.

## Resource and asynchronous work boundary

Persistent ECS components contain authored intent, stable references and parameters. Subsystems own device/library/solver objects and caches, release them before their device/library/code owner, and clear any transient links before scene/world teardown. WorldTransform remains derived. Existing Diligent resources, immutable compiled prefabs and gameplay code retain their explicit owners. **AssetHandle<T> remains deferred**: the current catalog resolves metadata, not shared loaded resources needing generation handles.

Worker work produces immutable candidate results, with bounded ownership and cancellation, then the owning thread validates and publishes. NativeBuild already pumps a separate compiler process; FileDialog callbacks hand results back through synchronized state; LiveAuthoring applies on the editor owner thread. No custom scheduler or speculative task abstraction is added. Future importing must define cancellation and resource teardown at its real consumer boundary.

## Deliberate boundaries

Phase5.5 introduced no Apply to Prefab, general transaction framework, gameplay/plugin SDK, loaded-resource handles, VFS, replay/networking or new subsystem integration. Phase6A subsequently adds the bounded internal registration/SDK profile described in [Engine modules](engine-modules.md); later subsystem phases still require separate authorization.

## Navigation capability

Navigation (`32`) is an optional world-scoped weak service slot. The provider owns admitted assets and query scratch; query results are owned FORGE values. Restricted modules cannot query through an absent capability. Runtime query use requires the bound owner thread. The exact SDK exposes bounded POD results; no Detour object escapes. See [Navigation](navigation.md).
