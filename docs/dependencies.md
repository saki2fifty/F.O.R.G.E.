# Dependency baselines

CMake fetches immutable revisions. These selections are tested baselines, not claims that each is the latest release.

| Dependency | Baseline | Used capabilities | Official reference |
|---|---|---|---|
| Flecs | v4.1.0 / `9e06477db0fdf9317bf583ff4b1318622283165f` | Worlds, C++ components, reflection, prefab inheritance, deferred mutations | [Source/docs](https://github.com/SanderMertens/flecs/tree/v4.1.0/docs) |
| nlohmann/json | v3.12.0 / `55f93686c01528224f448c19128836e7df245f72` | Scene and IPC JSON | [Official docs](https://json.nlohmann.me/) |
| Diligent Engine | `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06` and its submodules | D3D12 device, swapchain, textures, shaders, ImGui integration | [Source](https://github.com/DiligentGraphics/DiligentEngine/tree/a279e5fa8593cbc758ec46ea1eba0b435cbc2f06) |
| SDL3 | release-3.2.20 / `96292a5b464258a2b926e0a3d72f8b98c2a81aa6` | Windows, events, preference paths | [Official API](https://wiki.libsdl.org/SDL3/FrontPage) |
| Dear ImGui | v1.92.2b-docking / `1f7f1f54af38b0350d8c0008b096a9af6de299c7` | Docking, tables, controls, texture-backed preview | [Source](https://github.com/ocornut/imgui/tree/v1.92.2b-docking) |

Flecs, JSON and ImGui use MIT; SDL uses zlib; Diligent uses Apache-2.0 with separately licensed third-party dependencies. Preserve upstream notices when distributing binaries. The Runtime install component includes the Flecs and JSON license texts. A complete editor distribution notice bundle remains a release gate. Diligent's native Metal backend is commercial; this build selects D3D12 only. DiligentFX/PBR and other advanced upstream capabilities are not enabled by this foundation.

Build tooling: CMake presets, Ninja incremental targets, Python 3.10+ subprocess/path tooling, MSVC for Windows and GCC for portable tests. Formatting uses clang-format 23.1.1. GitHub Actions uses pinned checkout/setup actions and an explicit Windows compiler environment. Tool versions and host SDK versions should be recorded with release evidence.

Future selections (not fetched or integrated): GLM, Jolt, miniaudio, ozz-animation, Recast/Detour, RmlUi, Tracy, Catch2, Box2D and GameNetworkingSockets. Resolve versions and license requirements before adding them. Current behavior tests use CTest with simple C++ assertions and Python subprocess fixtures; Catch2 integration remains deferred.
