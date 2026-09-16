# Dependency baselines

CMake fetches immutable revisions. These selections are tested baselines, not claims that each is the latest release.

| Dependency | Baseline | Used capabilities | Official reference |
|---|---|---|---|
| Flecs | v4.1.6 / `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8` | Worlds, C++ components, reflection, prefab inheritance, deferred mutations | [Source/docs](https://github.com/SanderMertens/flecs/tree/v4.1.6/docs) |
| nlohmann/json | v3.12.0 / `55f93686c01528224f448c19128836e7df245f72` | Scene and IPC JSON | [Official docs](https://json.nlohmann.me/) |
| Diligent Engine | `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06` and its submodules | D3D12 device, swapchain, textures, shaders, ImGui integration | [Source](https://github.com/DiligentGraphics/DiligentEngine/tree/a279e5fa8593cbc758ec46ea1eba0b435cbc2f06) |
| SDL3 | release-3.4.16 / `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` | Windows, events, preference paths, dialogs, process pipes | [Official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16) |
| Dear ImGui | v1.92.9b-docking / `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | Docking, tables, controls, texture-backed preview | [Source](https://github.com/ocornut/imgui/tree/v1.92.9b-docking) |

Flecs, JSON and ImGui use MIT; SDL uses zlib; Diligent uses Apache-2.0 with separately licensed third-party dependencies. Preserve upstream notices when distributing binaries. The Runtime install component includes the Flecs and JSON license texts. A complete editor distribution notice bundle remains a release gate. Diligent's native Metal backend is commercial; this build selects D3D12 only. DiligentFX/PBR and other advanced upstream capabilities are not enabled by this foundation.

Build tooling: CMake presets, Ninja incremental targets, Python 3.10+ subprocess/path tooling, MSVC for Windows and GCC for portable tests. Formatting uses clang-format 23.1.1. GitHub Actions uses pinned checkout/setup actions and an explicit Windows compiler environment. Tool versions and host SDK versions should be recorded with release evidence.

Future selections (not fetched or integrated): GLM, Jolt, miniaudio, ozz-animation, Recast/Detour, RmlUi, Tracy, Catch2, Box2D and GameNetworkingSockets. Resolve versions and license requirements before adding them. Current behavior tests use CTest with simple C++ assertions and Python subprocess fixtures; Catch2 integration remains deferred.

Flecs 4.1.6 compatibility: Position registration explicitly requests member entities for attached documentation. Other reflected fields use `EcsStruct` member data. New `Parent` storage, automatic C++ reflection and callback-update APIs are not yet adopted; existing `ChildOf`, prefab and scene semantics remain.

ImGui 1.92.9b compatibility: the editor explicitly keeps the legacy bitmap face and enables `ImGuiItemFlags_LiveEditOnInputScalar` for its frame. Diligent supplies the renderer backend; the upstream `imgui_impl_dx12` backend is not linked. SDL3 supplies the platform backend. Docking is enabled; multi-native-window support remains deferred.

## Toolchain baseline and update review (2026-09-16)

CI uses CMake **4.4.3**, Ninja **1.13.2**, Ninja generator, Windows Server 2022 runners and explicitly selects Visual Studio 2022 MSVC **14.44.35207** / Windows SDK **10.0.26100.0**. The editor uses the static MSVC runtime (`MultiThreaded`). CMake 3.24 is the project minimum, not the CI version. C++20 and C17 remain required. Hosted images and compiler servicing are not immutable; `ci_cache_key.py` fingerprints actual tools/SDK/image and invalidates incompatible caches. An unavailable selected toolset should fail instead of silently changing compiler families.

Official release review found Flecs4.1.6, SDL3.4.16, ImGui1.92.9b, JSON3.12.0, CMake4.4.3 and Ninja1.13.2 current stable. ImGui uses the corresponding exact docking tag, following the [upstream docking guidance](https://github.com/ocornut/imgui/wiki/Docking). Newer maintenance releases on older CMake branches do not supersede4.4.3. Planned physics/audio/animation/navigation/game-UI libraries remain unintegrated.

[Visual Studio2026 release notes](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes) list18.10.1. [MSVC versioning](https://learn.microsoft.com/en-us/cpp/overview/compiler-versions) lists14.51 supported and14.52 preview;14.44 remains supported. The [Windows SDK](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads) offers10.0.28000.2705 (and serviced26100 releases). Retain the tested VS2022/26100 build baseline: no currently implemented FORGE API requires the newer compiler/SDK. Build SDK selection is **not** a minimum client Windows version. This pass changes neither OS feature targeting nor runtime requirements; a minimum client OS/driver matrix remains unverified and must be established before claiming support.

## Coordinated Diligent snapshot

The latest numbered release is2.5.6; FORGE intentionally uses a later coordinated superproject snapshot. On2026-09-16 upstream master resolved to `dccb5a9795ab5ee85caca5b9710b99b3106151b4`, with the **same Core and Tools** as the retained `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06`. New FX/Samples changes are unused, so no renderer upgrade is warranted. Core API remains **256020**.

| Submodule | Revision owned by retained superproject | Enabled use |
|---|---|---|
| DiligentCore | `744f079f61cdbda15d371383682418fc927e4a61` | D3D12 |
| DiligentTools | `7d1139064f36b14f911e5bca095be9c9dcfc5112` | ImGui integration |
| DiligentFX | `aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da` | Disabled |
| DiligentSamples | `73b08a788380b6db5aaa3274335749aaf7fc0056` | Disabled |

FetchContent initializes the superproject's gitlinks recursively; nested third-party revisions are owned by those pins. Do not independently update submodules. `DILIGENT_DEAR_IMGUI_PATH` directs Tools to FORGE's selected ImGui source, so its bundled ImGui revision is not the active UI dependency.
