# Renderer backends and capabilities

**D3D12-first validation, backend-neutral architecture.** Phase 7 is still in
progress. Windows/D3D12 is the primary editor and WARP acceptance profile. A
Vulkan probe is a portability check, not a shipped Linux editor or a promise of
feature parity. Metal and WebGPU are architectural targets where the selected
Diligent implementation supports them.

## Source contract

Verified 2026-09-21 against Diligent Engine
`a279e5fa8593cbc758ec46ea1eba0b435cbc2f06`, Core
`744f079f61cdbda15d371383682418fc927e4a61`. Follow the
[dependency policy](dependency-policy.md) before changing either pin.

- Shared renderer code uses Diligent devices, buffers, views, samplers, pipeline
  states, resource bindings, transitions and fences. Native graphics handles and
  API-specific synchronization belong only in platform/backend adapters.
- Mesh, Material and Texture logical data contain content semantics, not descriptor
  indices or native objects. Shader source documents describe stages, entry points,
  source files, defines and permutations. HLSL is a source language, not a D3D12
  runtime binding contract; individual custom shaders can still use unsupported
  features and must be admitted by the selected compiler/device.
- Cooked output may be specific to a platform, backend, compiler and profile.
  Existing Shader artifacts explicitly identify D3D12/FXC5.1 and contain DXBC and
  backend reflection. They must not be submitted as Vulkan SPIR-V or WebGPU WGSL.
  Another shader cook adapter can consume the same logical source identity without
  changing AssetId, scene references or authored material values.
- Shader compiler choice and resource-array declaration lowering are narrow backend
  policy. Shared passes must not choose FXC, encode register spaces or assume a
  native descriptor-table layout.

The [post-processing audit](rendering-postprocessing.md) records the additional
frame inputs required by pinned effects, separately from backend support.

## Capability matrix

“Mapping” below identifies a source-supported route, not executed acceptance.
Device features and resource/PSO creation still decide whether a particular
configuration is usable. Final Phase 7 acceptance is recorded separately.

| Feature | D3D12 | Vulkan | Metal | WebGPU | FORGE admission / current evidence |
| --- | --- | --- | --- | --- | --- |
| Buffers, textures, SRVs, SRBs, PSOs | Primary profile | Native Diligent mapping; local device/binding/draw probe passes | Diligent interface; native implementation not present in this checkout | Native Diligent mapping | Use Diligent resources; no raw handles in shared passes |
| 16/32-bit indices and nonindexed list draws | Native Diligent mapping | Native Diligent mapping | Shared interface; platform proof pending | Shared interface; platform proof pending | Shared Draw/DrawIndexed and explicit VALUE_TYPE; compact-boundary/readback/pixel fixtures |
| HLSL shader preparation | FXC5.1 production; DXC diagnostic route | Diligent GLSLang/DXC to SPIR-V | Selected Diligent compiler/translation route needs platform validation | Pinned HLSL → SPIR-V → Tint WGSL route | Compiler choice belongs in backend policy; shipping custom Shader cooks currently D3D12 only |
| Custom material pixel surfaces | Cooked color/depth roles; native acceptance pending | Generated interface compiles to SPIR-V; full custom cook adapter pending | Logical named bindings; platform proof pending | Named-element sampler lowering; platform proof pending | Engine-owned shared geometry; reflected parameter packing and named Diligent SRBs; see [surface contract](surface-shaders.md) |
| Bounded independent material samplers | Native sampler array | Native descriptor array; 19 plus environment/comparison passed local binding/draw probe | Not executed; compiler/resource limits require platform proof | Named scalar declarations grouped by Diligent array suffix | Deduplicate equal states, preserve distinct states, constant indices; no register-space contract |
| Runtime-sized / bindless arrays | Not required | Not required | Not required | Pinned factory disables runtime arrays | Bounded material arrays do not require this feature |
| Signed/zero static transforms and per-draw culling parity | Existing WARP fixtures | Same Diligent rasterizer contract; full fixture not yet executed | Needs platform fixture | Needs platform fixture | Logical transform semantics stay identical; no backend may rewrite authored scale |
| Signed blended-skin per-triangle winding | Geometry shader path | Available when GeometryShaders is enabled | Current path unavailable without geometry shaders | Pinned factory disables geometry shaders | Reject unsupported path clearly; future compute/CPU winding adapter requires its own proof |
| Compute passes / custom compute stages | Supported by primary profile | Query enabled ComputeShaders | Query selected implementation | Query enabled ComputeShaders and limits | Reject unsupported operation before shader creation; custom cook profile is separate |
| Texture dimensions, arrays, format use | Device queries | Device queries | Device queries | Device queries | Texture properties and GetTextureFormatInfoExt; no D3D12 maximum used as universal hardware capacity |
| Anisotropy, LOD bias, border sampling | Diligent anisotropy/support flags; SDK bias constants; unit-range border | Diligent flags; Linux probe queries native bias limit; pinned black/white border palette | Optional bias/border states not yet admitted | Optional bias/border states not yet admitted | Reject unsupported sampling before allocation; preserve authored values |
| HDR/depth targets, filtering, comparison shadows | Primary WARP fixtures | Native mapping; full pass validation pending | Format/usage proof pending | Format/usage proof pending | Query format/usage; candidate allocation/PSO failure retains prior usable resources |
| Resource retirement | Diligent fence/deferred release | Diligent fence/deferred release | Selected backend's Diligent implementation | Selected backend's Diligent implementation | Primary immediate graphics context only; no shared D3D12 queue/fence casts |
| Clip-space depth / viewport convention | Zero-to-one depth | Pinned backend flips Vulkan viewport height for Diligent's convention | Needs selected implementation proof | Needs native fixture | Inspect RenderDeviceInfo.NDC and keep conversion at the rendering boundary; do not add a second Vulkan Y flip |
| Editor window/swap chain | Shipped Windows host profile | Probe only | No shipped host | No shipped host | Platform startup remains distinct from reusable renderer code |

The pinned public adapter interface exposes sampler state capabilities, but **does
not expose a universal per-stage sampler-descriptor-count query**. The number of
supported material texture roles is an engine content profile, not a claim about
every GPU. Diligent pipeline/resource-signature admission remains required. A
backend may reject a feature combination with a diagnostic while retaining the
last usable draw bundle. Do not bake D3D12's sampler/register limits into logical
material formats or silently merge unequal sampler states.

The public interface also omits numeric LOD-bias limits and border-color palettes.
`sampler_backend.cpp` owns those exceptions. Its Linux Vulkan capability adapter
obtains the existing physical device through Diligent's native interface and performs
a read-only property query; no native handle, resource creation or synchronization
escapes into shared passes. The primary Windows profile reads D3D12 SDK constants
inside that adapter. Other hosts currently admit zero bias/no border until their
optional state support has an equivalent source-backed implementation. The logical
asset domain remains unchanged across hosts; hardware support is a separate check.

Pinned Vulkan conversion otherwise logs an unsupported border color and substitutes
transparent black. FORGE rejects that request before native allocation. When border
addressing is unused, only the native descriptor is normalized to black. This does
not change authored sampler intent. Diligent's format/dimension queries, rather than
a universal D3D12 restriction, decide whether compressed volumes can upload.

## Exact pinned evidence

- [Shader compiler/version and WebGPU array-suffix contract](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngine/interface/Shader.h).
- [Native separate-texture/sampler tests, including WebGPU scalar-array emulation](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Tests/DiligentCoreAPITest/src/SeparateTextureSamplerTest.cpp).
- [WebGPU resource signature assigns one native binding per emulated array element](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngineWebGPU/src/PipelineResourceSignatureWebGPUImpl.cpp).
- [WebGPU feature admission](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngineWebGPU/src/EngineFactoryWebGPU.cpp).
- [Adapter/device feature and limit declarations](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngine/interface/GraphicsTypes.h).
- [Vulkan viewport conversion](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngineVulkan/src/DeviceContextVkImpl.cpp).
- [Pinned Vulkan sampler creation](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngineVulkan/src/SamplerVkImpl.cpp) and [border conversion](https://github.com/DiligentGraphics/DiligentCore/blob/744f079f61cdbda15d371383682418fc927e4a61/Graphics/GraphicsEngineVulkan/src/VulkanTypeConversions.cpp).
- [Vulkan sampler numeric requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html) and [D3D12 sampler requirements](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_sampler_desc). The supported native state is distinct from the backend-neutral asset representation.

## Validation boundary

The Linux probe target also depends on `forge_vulkan_renderer_compile`, which
compiles every source in the shared presentation library under the Linux/Vulkan
definitions. Both targets take that list from `cmake/presentation_sources.cmake`.
This check passed on2026-09-22, including frame composition, shadows, environment,
texture upload, mesh preparation/drawing and GPU retirement. It catches platform
header/API dependencies in shared code. It does **not** link or execute a complete
Vulkan frame, and does not replace native Windows acceptance.

The initial Linux llvmpipe probe compiled register-free HLSL to SPIR-V using the
selected DXC 1.8.2505.1 tool, then created native Diligent shaders, pipeline,
resources and bindings and submitted a draw with 19 material samplers plus
environment/comparison samplers. The repeatable `vulkan_material_binding` test
also uses FORGE's actual material generator, 17 independent material samplers,
distinct textures, environment/comparison sampling and native pixel readback.
Its center pixel matches the expected value (68). Local material/policy and
Vulkan tests pass2/2 (1.22s). This is not full frame-renderer acceptance.

The Windows FXC probe passes the register-free 19+2 declaration with ordinary
SM5.1 flags in run35659980484. Historical register-space alternatives cannot
satisfy its success condition. Full renderer validation remains in progress;
no Metal/WebGPU execution is claimed.

To reproduce the optional Linux probe, supply the verified external DXC tool and
a Vulkan ICD (the observed run uses Mesa llvmpipe), then configure:

```sh
cmake -S . -B <build-dir> -DFORGE_BUILD_EDITOR=OFF -DFORGE_BUILD_ASSET_TOOLS=ON \
  -DFORGE_BUILD_VULKAN_PROBES=ON -DFORGE_VULKAN_PROBE_DXC=<absolute-dxc-path>
cmake --build <build-dir> --target forge_vulkan_binding_probe
ctest --test-dir <build-dir> -R '^vulkan_material_binding$' --output-on-failure
```

No window, ImGui host or source importer executes inside the probe's draw path.
Unsupported devices fail with diagnostics; the test does not silently skip a
missing Vulkan driver or compiler.
