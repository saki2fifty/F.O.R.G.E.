# Dependency baselines

All integration and upgrade work follows the permanent
[dependency source-of-truth policy](dependency-policy.md). The exact pinned
source and its selected build options take precedence over live documentation.

CMake fetches immutable revisions. These selections are tested baselines, not claims that each is the latest release.

| Dependency | Baseline | Used capabilities | Official reference |
|---|---|---|---|
| Draco | 1.5.7 / `8786740086a9f4d83f44aa83badfbea4dce7a1b5` | Private glTF mesh decompression | [Source](https://github.com/google/draco/tree/8786740086a9f4d83f44aa83badfbea4dce7a1b5) |
| meshoptimizer | 1.2 / `9d9890c73011d75920af614485296d1e03e95448` | Private EXT buffer decode, Mikk-compatible tangents and coherent remapping | [Source](https://github.com/zeux/meshoptimizer/tree/9d9890c73011d75920af614485296d1e03e95448) |
| KTX Software | 4.4.2 / `4d6fc70eaf62ad0558e63e8d97eb9766118327a6` | Private containers and Basis codecs | [Source](https://github.com/KhronosGroup/KTX-Software/tree/4d6fc70eaf62ad0558e63e8d97eb9766118327a6) |
| libwebp | 1.6.0 / `4fa21912338357f89e4fd51cf2368325b59e9bd9` | Private still-image decode and native demux | [Source](https://chromium.googlesource.com/webm/libwebp/+/4fa21912338357f89e4fd51cf2368325b59e9bd9/) |
| RmlUi | 6.3 / `ba95ffe8bfb6370efb2cdcca927eaad4710c5413` | Screen-space game documents, bindings and input; private Diligent adapter | [Official release](https://github.com/mikke89/RmlUi/releases/tag/6.3) |
| FreeType | 2.14.3 / `0a0221a1347e2f1e07c395263540026e9a0aa7c7` | Pinned RmlUi font backend, FTL license option | [Official source](https://github.com/freetype/freetype/tree/0a0221a1347e2f1e07c395263540026e9a0aa7c7) |
| Recast Navigation | v1.6.0 / `6dc1667f580357e8a2154c28b7867bea7e8ad3a7` | Static single-tile generation and private Detour queries | [Official source](https://github.com/recastnavigation/recastnavigation/tree/v1.6.0) |
| Ozz Animation | v0.17.0 / `744eb9d99f606eda849acb0b1204f7a3dc20bca1` | Private skeletal sampling/local-to-model, validated runtime archives, official gltf2ozz conversion | [Official source](https://github.com/guillaumeblanc/ozz-animation/tree/744eb9d99f606eda849acb0b1204f7a3dc20bca1) |
| miniaudio | v0.11.25 / `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` | WAV decoding, engine/group mixing, spatialization, WASAPI/PulseAudio/ALSA, offline tests | [Official source](https://github.com/mackron/miniaudio/tree/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d) |
| Jolt Physics | v5.6.0 / `e77f175595e64cb44218cc9d9d56fc365ad0e36a` | CPU rigid bodies, primitive shapes, queries, state recording | [Official source](https://github.com/jrouwe/JoltPhysics/tree/v5.6.0) |
| Flecs | v4.1.6 / `fb55f3c25660425cfe1bc4cf5e6bff8b3f18a9b8` | ECS/Meta/Doc/Units/Ranges, ordered hierarchies/prefabs, queries/timers, Script Math, optional diagnostics | [Source/docs](https://github.com/SanderMertens/flecs/tree/v4.1.6/docs) |
| nlohmann/json | v3.12.0 / `55f93686c01528224f448c19128836e7df245f72` | Scene and IPC JSON | [Official docs](https://json.nlohmann.me/) |
| Diligent Engine | `a279e5fa8593cbc758ec46ea1eba0b435cbc2f06` and its submodules | D3D12 device, swapchain, textures, shaders, ImGui integration | [Source](https://github.com/DiligentGraphics/DiligentEngine/tree/a279e5fa8593cbc758ec46ea1eba0b435cbc2f06) |
| SDL3 | release-3.4.16 / `fa2c02bb6e21974a89ea9824bc53c9932abe5f9c` | Windows, events, preference paths, dialogs, process pipes | [Official release](https://github.com/libsdl-org/SDL/releases/tag/release-3.4.16) |
| Dear ImGui | v1.92.9b-docking / `b48d1afbe8ee8b238e2961dc363a949dd7304e23` | Docking, tables, controls, texture-backed preview | [Source](https://github.com/ocornut/imgui/tree/v1.92.9b-docking) |

Ozz, Jolt, Flecs, JSON and ImGui use MIT; miniaudio uses its MIT-0 option; SDL uses zlib; Diligent uses Apache-2.0 with separately licensed third-party dependencies. Preserve upstream notices when distributing binaries. The Runtime install component includes the Flecs and JSON license texts. A complete editor distribution notice bundle remains a release gate. Diligent's native Metal backend is commercial; this build selects D3D12 only. Phase7 composes the native DiligentFX PBR utility subset described below; other advanced upstream capabilities are not implicitly enabled.

Build tooling: CMake presets, Ninja incremental targets, Python 3.10+ subprocess/path tooling, MSVC for Windows and GCC for portable tests. Formatting uses clang-format 23.1.1. GitHub Actions uses pinned checkout/setup actions and an explicit Windows compiler environment. Tool versions and host SDK versions should be recorded with release evidence.

Future selections (not fetched or integrated): GLM, Tracy, Catch2, Box2D and GameNetworkingSockets. Resolve versions and license requirements before adding them. Current behavior tests use CTest with simple C++ assertions and Python subprocess fixtures; Catch2 integration remains deferred.

Flecs 4.1.6 compatibility: FORGE authoring types now explicitly request member entities for Doc/Units/ranges and member-based tooling. Global automatic member creation remains disabled. See the [integration contract](flecs-integration.md) for ownership, selected options and live-documentation exceptions. Phase5 uses `Parent` for validated structured prefab interiors and retains dynamic `ChildOf` attachments and component-granular inheritance. Phase3 independent local TRS and derived world transforms remain the spatial authority.

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
| DiligentTools | `7d1139064f36b14f911e5bca095be9c9dcfc5112` | ImGui integration; Phase7 native CPU glTF Document/VertexDataConverter adapter |
| DiligentFX | `aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da` | Native PBR utility/source-factory subset; umbrella target disabled |
| DiligentSamples | `73b08a788380b6db5aaa3274335749aaf7fc0056` | Disabled |

Phase7 tooling verification2026-09-20: `FORGE_BUILD_ASSET_TOOLS` adds an optional
headless native-loader target, defaulting on with the Windows editor. Standalone
Linux tools compose exact native ThirdParty/TextureLoader/AssetLoader targets,
configure the required Vulkan backend, disable GLSLang/HLSL, archiver, super
resolution, Draco, RapidJSON, samples and FX, and never initialize a GPU for model
admission. Editor builds reuse their D3D12/ImGui/shader profile. Native loader JSON
uses its private `JSON_DIAGNOSTICS=1`; FORGE does not exchange JSON C++ objects
across that implementation boundary. See [glTF admission](gltf-admission.md) for
actual capabilities and remaining importer integration. The transitive Core
Abseil source remains its selected `07d2ef8bd61ab88f0b81b0d8c7fb2c7e19b1d01e`.

FetchContent initializes the superproject's gitlinks recursively; nested third-party revisions are owned by those pins. Do not independently update submodules. `DILIGENT_DEAR_IMGUI_PATH` directs Tools to FORGE's selected ImGui source, so its bundled ImGui revision is not the active UI dependency.

## Upgrade validation

[Clean Windows Build 260916-000019](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35152168546) passed with all selected revisions. Windows/Linux core suites, Windows editor/native/authoring suites, four FXC shaders and D3D12 WARP pixel tests passed. The 24 existing grid/scene render fixtures exactly match Build 17; new font/external-texture fixtures pass at multiple UI scales. Actual compiler identity was MSVC19.44.35228.0 under toolset14.44.35207 / SDK10.0.26100.0.

Physical input, native dialogs, mixed-monitor DPI and interactive editor startup/docking on the user's GPU still require desktop acceptance. WARP rendering and injected SDL events do not establish those results. New Flecs hierarchy/registration APIs remain available for review, not automatically adopted FORGE features.

## Phase 6B physics selection

Jolt is now pinned and integrated. See [Physics build/lifetime contract](physics.md). Linux uses GCC12+; Windows retains VS2022. Double positions and SSE2 keep the engine coordinate model and avoid an unannounced AVX2 requirement. Jolt licenses are included in runtime/editor/experimental-SDK packages. Advanced upstream shapes and compute capabilities remain unexposed.

## Phase 6C audio selection

miniaudio0.11.25 is the pinned audio implementation. See [Audio](audio.md) for enabled backends, tested PCM WAV formats, compile switches, threading/lifetime, bounds, paused playback and deferred capabilities. AudioClip identity uses the existing asset catalog. Phase7 adds a shared supervised WAV import/cook/reimport route and immutable PCM selection; runtime voices retain their existing cache/lifetime. Generic asynchronous AudioClip handles remain deferred. Exact source rechecked2026-09-21: WAV-specific decoder selection, frame-length/read/uninit and384000Hz upstream admission bound. No pin/build-option change. Automated offline/null testing is distinct from physical speaker/headphone acceptance.

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


Flecs configuration and capability review verified2026-09-19: default stable addons
plus `FLECS_SCRIPT_MATH`; explicit per-type member entities, global
`FLECS_CREATE_MEMBER_ENTITIES` off. Static and single-shared SDK profiles carry
the same public flags; Script Math participates in the exact SDK fingerprint.
See [Flecs integration](flecs-integration.md) for adopted, SDK-only, measured and
unavailable capabilities, the approved exact-path managed-include buffer-leak
exception, and development-documentation drift. No dependency pin changed.

## Phase7 image-boundary corrections (2026-09-20)

The[texture contract](texture-assets.md#exact-source-correction) records source-
verified limitations in pinned Diligent PNG/TIFF callbacks and IJG libjpeg integer
DCT shifts. Keep all pins. Texture imports use the existing pinned libpng1.6.55
(65bc84e803c0ccbf7aa1023e91b5808586ea1b66) with an official bounded-read adapter;
JPEG uses the existing pinned stb2.29 JPEG decoder (46fcb30365c5f35425751d275eecd8e5f8efc786),
with private symbols and no SIMD. The libjpeg float-DCT experiment still exposed
Huffman encoder signed-shift errors and was not selected. Native Diligent still provides pixel,
mip and basic BC processing. These corrections do not claim the old callback or
integer path has been fixed upstream. KTX4.4.2 is selected below for the private
container/Basis adapter; production asset/editor integration remains in progress.

## Phase7 KTX/Basis selection —2026-09-20

Official stable **KTX Software4.4.2**, immutable
`4d6fc70eaf62ad0558e63e8d97eb9766118327a6`; official latest stable release rechecked
on2026-09-20. Codeload archive SHA256
`4d0a3c4470c67e0f1544d2a92f379dc919c9627a5c3fa5c4fcaf4c22324827f5`.
[Release](https://github.com/KhronosGroup/KTX-Software/releases/tag/v4.4.2) /
[exact source](https://github.com/KhronosGroup/KTX-Software/tree/4d6fc70eaf62ad0558e63e8d97eb9766118327a6).

Private static worker codec; KTX1/2 read/write, bundled Basis ETC1S/UASTC and
Zstandard container handling (zlib-supercompressed KTX is explicitly excluded). Disable graphics upload, tools/tests/bindings,
OpenCL/SSE and optional Ericsson ETC unpacker. ASTC library uses its scalar target;
no new AVX2 requirement. Select the native byte-wise Basis read path explicitly.
KTX is fetched only for asset tools/editor; runtime and installed gameplay SDK do
not depend on its headers or API. Existing Flecs shared/static profile is restored
around the upstream CMake subdirectory. All previous pins remain unchanged.

Apache2.0 with bundled permissive notices; the non-open-source `lib/etcdec.cxx`
is excluded. Package the upstream LICENSES directory, license files and NOTICE.
No live/main-only HDR Basis features are assumed. Current assumptions and selected
API boundaries are in [textures](texture-assets.md) and
[known issues](dependency-known-issues.md#ktx-software442-selection).
Source layout/private Basis headers are coupled to this exact revision; re-audit
before upgrading. No public ABI1 or persistent identity change is involved.

The upstream CMake project requires Bash for version generation; build hosts need
Bash (Git for Windows supplies it). It is not required to run a packaged editor.

Archive builds set the official `KTX_GIT_VERSION_FULL=v4.4.2` override so generated
metadata does not depend on a parent checkout's tags or an absent Git database.

## Phase7 WebP selection —2026-09-20

Official stable libwebp1.6.0, released2025-06-30, exact peeled tag
`4fa21912338357f89e4fd51cf2368325b59e9bd9`. Official tag listing was checked on
2026-09-20;1.6.0 is the newest stable tag. Immutable official-mirror archive SHA256
`923f3382a47a2af185c3240c954cf004428b237bd7317413a95146d01eb4b94b`.
Read exact NEWS, CMake, decode/demux/encode declarations and native implementation;
no live-only API assumptions.1.6 adds `WebPValidateDecoderConfig`;1.5/1.4 improve
hardening/optimizations and follow1.3.2's lossless decoder security fix.

BSD3-Clause plus PATENTS grant; package COPYING, PATENTS and AUTHORS. Private
static worker codec with native SIMD dispatch, threading disabled. CLI tools,
image-library discovery, animation utilities, mux, extras, JavaScript and fuzztest
builds are off. Demux's native CMake link includes the codec/SharpYUV libraries;
FORGE does not patch that graph. Previous pins and runtime/SDK boundaries remain
unchanged. Lossy/lossless still RGBA is selected; animation, ICC conversion and
WebP export are not delivered. See[texture contracts](texture-assets.md).

## Meshoptimizer geometry tooling — verified2026-09-20

Official stable1.2 is pinned at`9d9890c73011d75920af614485296d1e03e95448`.
The MIT license is included in Windows notices. Static tooling only;
MESHOPT_BUILD_DEMO, MESHOPT_BUILD_GLTFPACK, MESHOPT_BUILD_SHARED_LIBS,
MESHOPT_INSTALL and MESHOPT_WERROR are disabled. Upstream default CPU dispatch
remains enabled. Strict profiles instrument the native library as well as FORGE.

FORGE calls native buffer decoders and filters after its bounded admission;
no API or native type enters the gameplay SDK. No global encoder-version or allocator
configuration is changed. Original captured sources remain immutable. Runtime
cooked Mesh format and scene/component formats are unchanged by this addition.
The private mesh preparation pass also uses native custom equality remapping,
vertex-fetch optimization, explicit order-independent triangle-cache optimization
and Mikk-compatible tangent generation. Simplification and LOD creation are not
implemented merely by linking the library. Tangent generation is explicitly
experimental in this stable library; no such API enters the SDK.

Known specification drift: the checked Khronos registry still labels
KHR_meshopt_compression a release candidate. The ratified EXT format requires
attribute bitstream0, index bitstream1 and its existing four filters, despite the
library supporting newer variants. See[glTF admission](gltf-admission.md).

## Draco private glTF codec — verified2026-09-20

Official stable1.5.7, exact commit
`8786740086a9f4d83f44aa83badfbea4dce7a1b5`, archiveSHA256
`b9c2392dbfcf454aaec68823d832de9d62614054b33807b7c9776799b8e0bbca`.
[Official release](https://github.com/google/draco/releases/tag/1.5.7).
Apache-2.0 license/notices are included by Windows dependency packaging.

Static private asset tooling selects `DRACO_GLTF_BITSTREAM`, mesh compression and
standard Edgebreaker. Point-cloud, backward compatibility, predictive Edgebreaker,
transcoder, animation, plugin/binding/test/install options are disabled; native CLI
tools are not built.
Upstream CMake still compiles some feature-guarded translation units into the static
archive; that does not enable their unavailable entry points. Native encoding is
used by regression fixtures; production uses decode. No second glTF parser is
introduced through Draco's optional transcoder or its own TinyGLTF dependency.

FORGE creates Draco's target after Diligent AssetLoader so the latter's optional
TinyGLTF bridge stays disabled. See [admission](gltf-admission.md#draco-compressed-primitives)
for exact-source reasons and the checked boundary. Native codec objects, declarations
and the experimental asset-tool adapter do not cross the gameplay SDK. No authored
scene, prefab or identity format changes. The authoritative extension is ratified
KHR bitstream2.2; older bitstreams are outside this selected glTF-only profile.

Strict profiles use upstream `DRACO_SANITIZE=address,undefined` for native object
libraries and retain LeakSanitizer. CMake's check-state supplies matching sanitizer
link options to upstream's compiler-flag probe, which otherwise links instrumented
probe objects without the sanitizer runtime. This is build configuration, with no
vendor patch or sanitizer suppression. No live-documentation drift is relied on.
Windows validation and complete model-pipeline adoption remain release gates.

## Shader compiler adapter — verified source 2026-09-20

The selected Core744f079 revision supplies FXC shader-model5.1 compilation, native
memory source factories and constant-buffer member reflection. FORGE selects that
explicit D3D12 profile and records the actual loaded D3DCompiler47 DLL digest,
packing, optimization and compiler Debug setting in build identity. Supplementary
Windows SDK reflection supplies dimensions, groups and bytecode version checks.
No dependency pin changes or DXC/DXIL support are implied. CPU admission tests pass;
the native Windows adapter is undergoing execution validation. See
[shader asset contracts and remaining integration](shader-assets.md).

### Optional Vulkan portability compiler — verified 2026-09-21

The optional Linux Vulkan probe uses Microsoft's official DXC release
[`v1.8.2505.1`](https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.8.2505.1),
commit `b106a961d09221b3c5bdb37be45b679257da08b8`. The official
`linux_dxc_2025_07_14.x86_64.tar.gz` archive SHA256 is
`f2213da1fc99dc8778c8823078e16ba97c7f80f86a1d4520ab1adf4b462bc48c`.
Its version banner says `1.9(dev;4950-b106a961)` despite that stable release tag;
the exact release/commit/checksum identifies the tool. No development branch was
selected. The executable is supplied explicitly to the test and is not downloaded
or shipped by FORGE. Its MIT/LLVM notices remain with the local tool installation.
Probe flags are `-spirv -fspv-target-env=vulkan1.1 -T vs_6_0/ps_6_0`; compiled
SPIR-V passes through the pinned Diligent Vulkan interfaces. This does not change
the shipping Shader asset cook profile or the Diligent pin. See the
[backend matrix](render-backends.md) for the precise execution boundary.

### Native PBR/cache composition — verified source2026-09-20

Exact Core/FX revisions above are unchanged. `DILIGENT_BUILD_FX=OFF` avoids the
umbrella's unconditional EnTTv3.16.0 fetch and public ImGui/AssetLoader dependencies.
`cmake/pbr.cmake` selects unmodified native PBR_Renderer/source-factory C++ files
and the native `convert_shaders_to_headers` pipeline. Diligent owns GGX/sheen LUTs,
default textures, cubemap convolution, native content-hashed shader/PSO caches and
resource lifetime. The backend-private FORGE target links the matching Archiver
DLL; packaging must include it. `EnableHotReload=false`, hash by content, no disk
cache loading. This is source-level build composition, not a public engine ABI or
an upstream patch. No live-documentation drift was used to select APIs. Windows
execution of this addition is pending; full mesh PBR/skinning is not yet claimed.
See[render ownership and limitations](rendering-foundation.md).
