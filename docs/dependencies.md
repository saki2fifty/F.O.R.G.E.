# Dependency baselines

CMake fetches immutable revisions. These selections are tested baselines, not claims that each is the latest release.

| Dependency | Baseline | Used capabilities | Official reference |
|---|---|---|---|
| RmlUi | 6.3 / `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` | Screen-space game documents, bindings and input; private Diligent adapter | [Official release](https://github.com/mikke89/RmlUi/releases/tag/6.3) |
| FreeType | 2.14.3 / `0a0221a1347e2f1e07c395263540026e9a0aa7c7` | Pinned RmlUi font backend, FTL license option | [Official source](https://github.com/freetype/freetype/tree/0a0221a1347e2f1e07c395263540026e9a0aa7c7) |
| Recast Navigation | v1.6.0 / `6dc1667f580357e8a2154c28b7867bea7e8ad3a7` | Static single-tile generation and private Detour queries | [Official source](https://github.com/recastnavigation/recastnavigation/tree/v1.6.0) |
| Ozz Animation | v0.17.0 / `744eb9d99f606eda849acb0b1204f7a3dc20bca1` | Private skeletal sampling/local-to-model, validated runtime archives, official gltf2ozz conversion | [Official source](https://github.com/guillaumeblanc/ozz-animation/tree/744eb9d99f606eda849acb0b1204f7a3dc20bca1) |
| miniaudio | v0.11.25 / `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | WAV decoding, engine/group mixing, spatialization, WASAPI/PulseAudio/ALSA, offline tests | [Official source](https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d) |
| Jolt Physics | v5.6.0 / `e77f175595e64cb44218cc9d9d56fc365ad0e36a` | CPU rigid bodies, primitive shapes, queries, state recording | [Official source](https://github.com/jrouwe/JoltPhysics/tree/v5.6.0) |
| Flecs | v4.1.6 / `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8` | Worlds, C++ components, reflection, prefab inheritance, deferred mutations | [Source/docs](https://github.com/SanderMertens/flecs/tree/v4.1.6/docs) |
| nlohmann/json | v3.12.0 / `55f93686c01528224f448c19128836e7df245f72` | Scene and IPC JSON | [Official docs](https://json.nlohmann.me/) |
| Diligent Engine | `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06` and its submodules | D3D12 device, swapchain, textures, shaders, ImGui integration | [Source](https://github.com/DiligentGraphics/DiligentEngine/tree/a279e5fa8593cbc758ec46ea1eba0b435cbc2f06) |
| SDL3 | release-3.4.16 / `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` | Windows, events, preference paths, dialogs, process pipes | [Official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16) |
| Dear ImGui | v1.92.9b-docking / `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | Docking, tables, controls, texture-backed preview | [Source](https://github.com/ocornut/imgui/tree/v1.92.9b-docking) |

Ozz, Jolt, Flecs, JSON and ImGui use MIT; miniaudio uses its MIT-0 option; SDL uses zlib; Diligent uses Apache-2.0 with separately licensed third-party dependencies. Preserve upstream notices when distributing binaries. The Runtime install component includes the Flecs and JSON license texts. A complete editor distribution notice bundle remains a release gate. Diligent's native Metal backend is commercial; this build selects D3D12 only. DiligentFX/PBR and other advanced upstream capabilities are not enabled by this foundation.

Build tooling: CMake presets, Ninja incremental targets, Python 3.10+ subprocess/path tooling, MSVC for Windows and GCC for portable tests. Formatting uses clang-format 23.1.1. GitHub Actions uses pinned checkout/setup actions and an explicit Windows compiler environment. Tool versions and host SDK versions should be recorded with release evidence.

Future selections (not fetched or integrated): GLM, Tracy, Catch2, Box2D and GameNetworkingSockets. Resolve versions and license requirements before adding them. Current behavior tests use CTest with simple C++ assertions and Python subprocess fixtures; Catch2 integration remains deferred.

Flecs 4.1.6 compatibility: LocalTranslation registration explicitly requests member entities for attached documentation. Other reflected fields use `EcsStruct` member data. Phase5 adopts `Parent` for validated structured prefab interiors and retains dynamic `ChildOf` attachments and component-granular inheritance. Automatic C++ reflection and callback-update APIs remain deferred. Phase3 independent local TRS and derived world transforms remain the spatial authority.

ImGui 1.92.9b compatibility: the editor explicitly keeps the legacy bitmap face and enables `ImGuiItemFlags_LiveEditOnInputScalar` for its frame. Diligent supplies the renderer backend; the upstream `imgui_impl_dx12` backend is not linked. SDL3 supplies the platform backend. Docking is enabled; multi-native-window support remains deferred.

## Toolchain baseline and update review (2026-09-16)

CI uses CMake **4.4.3**, Ninja **1.13.2**, Ninja generator, Windows Server 2022 runners and explicitly selects Visual Studio 2022 MSVC **14.44.35207** / Windows SDK **10.0.26100.0**. The editor uses the static MSVC runtime (`MultiThreaded`). CMake 3.30 is the project minimum, not the CI version. C++20 and C17 remain required. Hosted images and compiler servicing are not immutable; `ci_cache_key.py` fingerprints actual tools/SDK/image and invalidates incompatible caches. An unavailable selected toolset should fail instead of silently changing compiler families.

Official release review found Flecs 4.1.6, SDL 3.4.16, ImGui 1.92.9b, JSON 3.12.0, CMake 4.4.3 and Ninja 1.13.2 current stable. ImGui uses the corresponding exact docking tag, following the [upstream docking guidance](https://github.com/ocornut/imgui/wiki/Docking). Newer maintenance releases on older CMake branches do not supersede 4.4.3. At that modernization checkpoint, the planned subsystem libraries were unintegrated. Jolt, miniaudio and Ozz were subsequently added in Phase6B/6C/6D below; navigation/game UI were subsequently added in Phase 6E/6F.

[Visual Studio 2026 release notes](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes) list 18.10.1. [MSVC versioning](https://learn.microsoft.com/en-us/cpp/overview/compiler-versions) lists 14.51 supported and 14.52 preview; 14.44 remains supported. The [Windows SDK](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads) offers 10.0.28000.2705 (and serviced 26100 releases). Retain the tested VS2022/26100 build baseline: no currently implemented FORGE API requires the newer compiler/SDK. Build SDK selection is **not** a minimum client Windows version. This pass changes neither OS feature targeting nor runtime requirements; a minimum client OS/driver matrix remains unverified and must be established before claiming support.

## Coordinated Diligent snapshot

The latest numbered release is 2.5.6; FORGE intentionally uses a later coordinated superproject snapshot. On 2026-09-16 upstream master resolved to `dccb5a9795ab5ee85caca5b9710b99b3106151b4`, with the **same Core and Tools** as the retained `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06`. New FX/Samples changes are unused, so no renderer upgrade is warranted. Core API remains **256020**.

| Submodule | Revision owned by retained superproject | Enabled use |
|---|---|---|
| DiligentCore | `744f079f61cdbda15d371383682418fc927e4a61` | D3D12 |
| DiligentTools | `7d1139064f36b14f911e5bca095be9c9dcfc5112` | ImGui integration |
| DiligentFX | `aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da` | Disabled |
| DiligentSamples | `73b08a788380b6db5aaa3274335749aaf7fc0056` | Disabled |

FetchContent initializes the superproject's gitlinks recursively; nested third-party revisions are owned by those pins. Do not independently update submodules. `DILIGENT_DEAR_IMGUI_PATH` directs Tools to FORGE's selected ImGui source, so its bundled ImGui revision is not the active UI dependency.

## Upgrade validation

[Clean Windows Build 260916-000019](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35152168546) passed with all selected revisions. Windows/Linux core suites, Windows editor/native/authoring suites, four FXC shaders and D3D12 WARP pixel tests passed. The 24 existing grid/scene render fixtures exactly match Build 17; new font/external-texture fixtures pass at multiple UI scales. Actual compiler identity was MSVC19.44.35228.0 under toolset14.44.35207 / SDK10.0.26100.0.

Physical input, native dialogs, mixed-monitor DPI and interactive editor startup/docking on the user's GPU still require desktop acceptance. WARP rendering and injected SDL events do not establish those results. New Flecs hierarchy/registration APIs remain available for review, not automatically adopted FORGE features.

## Phase 6B physics selection

Jolt is now pinned and integrated. See [Physics build/lifetime contract](physics.md). Linux uses GCC12+; Windows retains VS2022. Double positions and SSE2 keep the engine coordinate model and avoid an unannounced AVX2 requirement. Jolt licenses are included in runtime/editor/experimental-SDK packages. Advanced upstream shapes and compute capabilities remain unexposed.

## Phase 6C audio selection

miniaudio0.11.25 is the pinned audio implementation. See [Audio](audio.md) for enabled backends, tested PCM WAV formats, compile switches, threading/lifetime, bounds, paused playback and deferred capabilities. AudioClip identity uses the existing asset catalog; the generic AssetHandle remains deferred. Automated offline/null testing is distinct from physical speaker/headphone acceptance.

## Phase6D animation dependency

Ozz Animation **0.17.0**, exact commit `744eb9d99f606eda849acb0b1204f7a3dc20bca1`.
MIT licensed; official gltf2ozz also carries its bundled third-party notices. Runtime
and converter share the pin. CMake3.30 is required. Enable private runtime and glTF
tools; disable FBX SDK, upstream samples/howtos/tests/data generation. No Ozz headers
are installed in the gameplay SDK. SIMD follows the upstream portable baseline.

Adopted: runtime Skeleton/Animation, SamplingJob reusable contexts, LocalToModelJob,
official glTF conversion. FORGE owns safe archive admission and provenance. Optional
seek iframes are disabled; no arbitrary .ozz imports. Graphs/controllers/renderer/mesh
import are not provided by this integration. Root-motion extraction, blending/IK,
FBX and sample skinning remain deferred. See [Animation contract](animation.md) for
format limits, ownership, conversion settings, compatibility and upgrade gates.

## Navigation adoption (Phase 6E)

Recast and Detour use the zlib license. FORGE compiles their source libraries privately; Crowd, TileCache, DebugUtils and the SDL2/OpenGL demo are disabled. Runtime links Detour; Recast is authoring worker code. See [Navigation](navigation.md) for formats, ownership, bounds, source provenance and deferred features.

## Runtime UI adoption (Phase 6F)

RmlUi 6.3 is the audited stable release (2026-08-22); FreeType 2.14.3 is the selected current font backend. The exact commits above are used together. RmlUi uses MIT, FreeType its FTL option, and packaged Lato Latin uses SIL OFL 1.1. Preserve all three notices. FreeType zlib/bzip2/PNG/HarfBuzz/Brotli dependencies are disabled. RmlUi Lua, SVG, Lottie, samples and optional shaping are disabled; the upstream debugger library may build but is not linked into FORGE. C++17 upstream is compatible with FORGE C++20.

See [Runtime UI](runtime-ui.md) for adopted vs deferred features, source references, memory limits, thread/ownership and lifetime rules. The renderer target and presenter are independent of ImGui; the headless runtime does not link either. Existing world rendering and all dependency pins outside these two additions remain unchanged.

Runtime UI pins are verified together in Build260918-000053: Windows/Linux core30/SDK38, actual D3D12 WARP UI rendering, relocated bundled-font rendering and local ASan/UBSan/leak checks. RmlUi and FreeType are instrumented in local sanitizer profiles. No machine-installed font is required.

Phase6G changes no dependency pins. SDK packaging now installs only the enumerated FORGE value boundary, a pure identity helper library and the matching shared Flecs runtime. No Jolt/miniaudio/Ozz/Detour/RmlUi/Diligent types enter that boundary. [SDK contracts](extension-contracts.md).
