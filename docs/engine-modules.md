# Engine modules and the internal native SDK

Phase6A adds registration and lifetime policy; Phase6B physics and Phase6C audio exercise it with concrete providers. Animation/navigation/runtime UI and a public SDK remain separate work. Apply to Prefab and AssetHandle remain deferred.

## Three categories

- **Built-in/source modules:** ordinary C++ compiled with FORGE, direct Flecs access, exact engine code. EngineModule describes dependencies, schema roles, active runtime roles and service permissions. It does not impose the DLL ABI on source code.
- **Project gameplay:** the existing limited ABI1 workflow stays supported. A separate experimental exact-SDK profile permits trusted registering DLLs/shared objects. Its contract is internal/unstable.
- **General binary plugins:** bounded versioned C API by default. Direct ECS access requires explicit adoption of the exact-SDK tier. Native editor extensions remain trusted and restart-bound; their broad panel/importer/drawer SDK is not implemented here.

## Registration and ownership

WorldContext builds a complete dependency order before calling registrations. Duplicate IDs, missing/role-excluded dependencies, cycles and missing required capabilities reject construction. Schema and active runtime masks are separate. Headless is a composition/capability choice, not a new mutually exclusive world role. Existing roles stay Authoring, Runtime, Preview and Validation.

Built-in Core/Transforms/Prefabs/Input registration uses Flecs imports once per world. Canonical existing authored component names, reflection and ownership are retained. FORGE owns dependency/lifetime policy; Flecs owns component/system/observer/module mechanics. Module implementation versions are separate from SDK compatibility. IDs are lowercase namespaced strings such as forge.transforms or project.sdk_probe; no new UUID identity is needed.

EngineServices remains explicitly injected and owner-thread restricted. Each module receives narrowed Diagnostics/Profiling access. Rendering is a requirement marker that headless composition does not supply; it does not construct a renderer or expose a speculative interface. No global subsystem pointers are introduced.

Startup occurs after all schemas register. Each started module must tolerate Stop after partial startup. Stop runs in reverse dependency order and must drain work without throwing. It must retain any state still needed by Flecs destruction callbacks. Module contexts and code leases survive the entire Flecs world finalization and are then released in reverse dependency order. Native callbacks never survive library unload. If bootstrap fails, the unpublished context is destroyed; arbitrary native side effects are not rolled back.

Module errors use structured diagnostic severity/category/text plus module, dependency and world_role context where known. SDK tick diagnostics include the fixed tick. Current runtime session envelope remains the process identity. Optional existing CPU scopes instrument registration/start/shutdown; no profiler UI is added.

## Static profile and ABI1

Default/core/windows-editor profiles retain static Flecs and the existing module_api.h contract. Generated gameplay.cpp, Build & Reload, disposable probe, first-live-tick activation and checkpoint/fallback behavior remain separate and compatible. ABI1 contains no arbitrary ECS registrations or module-owned surviving state. Its constrained replacement policy must never be applied to rich registering code.

A static-profile host rejects direct-Flecs SDK modules. SDK metadata validation does not make a native binary safe: these are trusted code, and native initializers can run when a library loads.

## Explicit native-sdk profile

Configure/build/test with the native-sdk preset. It builds shared Flecs at the unchanged4.1.6 commit, with no flecs_static target. Participating hosts and direct-access modules link that same shared library. Windows uses matching /MD Release settings (/MDd only for matching development Debug builds); the legacy default editor profile keeps its existing CRT policy.

The generated fingerprint includes the native boundary header, internal API identity, exact Flecs revision, linkage profile, compiler version/toolset, architecture, configuration, CRT and compiler/sanitizer settings. It deliberately excludes unrelated renderer/UI dependencies. An installed CMake package checks supported compiler/architecture/configuration/flags/CRT when building a client. Entry size/version, module identity/implementation, fingerprint and Flecs function/global identities are checked before registration. This is compatibility checking for cooperative trusted modules, not a sandbox against a malicious binary.

No STL ownership, implementation classes or exceptions cross native_sdk.h entry/callback boundaries. Strings and world pointers are borrowed. A module must not destroy the host world. Flecs API access uses its own exact headers; FORGE does not wrap the whole ECS API. Allocation must be freed by its owner; shared CRT does not waive that rule.

## Input and fixed execution

The host supplies the existing fixed phase and tag. A module's systems opt into that pipeline and receive Flecs' fixed delta. read_action reads a UUID ActionId from the immutable current snapshot; it returns unavailable outside an actual fixed tick or for an absent action. No SDL events or scan codes enter this boundary. Pause/Step/Resume and edge-once semantics remain Phase5.5 behavior.

The SDK sample is a test-only transient component/system/observer. Its fixed action ID is sample data, not an engine-defined action. The sample proves runtime access; it does not add custom component serialization, editor drawers or a player controller.

## Project declarations

Existing forge.project.json version2 remains the owner. Legacy modules strings core/transforms/input are accepted as aliases for forge.core/forge.transforms/forge.input. Built-ins may use canonical IDs. Duplicate declarations reject, including an alias plus its canonical name.

An experimental project module uses an exact local declaration:

```json
{
  "id": "project.sdk_probe",
  "implementation": "1",
  "sdk": "experimental-1",
  "fingerprint": "<64-character fingerprint from the matching runtime --sdk-info>",
  "library": "Native/gameplay.dll",
  "dependencies": ["forge.input", "forge.transforms"]
}
```

Use the platform's shared-object filename on Linux. Paths remain confined by ProjectPaths. Dependencies must exist and match the binary descriptor; implementation and SDK compatibility are independent exact values. Unknown payload fields remain preserved. There is no package/version solver or second manifest.

The experimental headless workflow is `forge_runtime --sdk-project PROJECT`. It loads declarations before constructing/publishing its world. Changing registration/schema/hooks requires terminating that runtime and starting a new process. No rich in-place reload command exists. Normal editor Play/Build & Reload continues to use ABI1. Editor Play rejects projects requiring experimental SDK modules with a clear status message instead of silently omitting their gameplay. Do not interpret the experimental command as a new editor workflow.

## Installation and linkage evidence

Install the NativeSdk component. Its bin contains the runtime and shared Flecs; sdk contains required headers, Windows import library, generated fingerprint, CMake configuration and sample. Release Windows installation stages compiler runtime redistributables through CMake's InstallRequiredSystemLibraries. Linux Flecs SONAME is libflecs.so.4, with installed host $ORIGIN lookup. Debug runtime redistribution is not supported.

The experimental artifact contains a tar.gz archive so executable permissions and SONAME links survive download. Extract it before using the SDK.

The package test builds the sample using only the installed SDK, inspects PE/ELF imports/exports, relocates the installed package, strips developer search-path overrides and executes it there. Runtime checks compare the host/module Flecs API/global addresses and operate on host-created worlds. Tests cover independent WorldContexts and teardown sentinels, including failed bootstrap and a crashing isolated runtime. This establishes the supported build's single implementation; it does not prove arbitrary untrusted DLL internals.

See [core services](core-services.md), [fixed input](input.md), and [runtime timing](runtime-timing.md). The renderer stays explicitly owned by its existing Diligent boundary. No subsystem library is added in6A.

## Physics provider

`forge.physics` is the first simulated subsystem provider. See [Physics](physics.md) for its world-scoped capability, lifecycle, fixed pipeline and exact-SDK extensions.

## Audio provider

`forge.audio` supplies schema-only contexts and explicit device/offline runtime compositions. Its world-scoped Audio capability exposes owner-thread source commands. See [Audio](audio.md) for asset resolution, callback retirement, Pause/Step/Resume, recovery policy and exact-SDK use. Required consumers verify actual provider availability before startup; optional device failure does not fabricate capability.
