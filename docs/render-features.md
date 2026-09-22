# Rendering feature matrix

The implementation below uses the shared Diligent renderer for Scene and authored
Game cameras. **Final Phase 7 Windows acceptance is still pending.** Test names
identify executable coverage, not a substitute for running the final source.
[Backend capabilities](render-backends.md) distinguish D3D12/WARP acceptance from
Vulkan compile/device probes and unexecuted Metal/WebGPU mappings.

| Feature | Implementation and bounds | Executable coverage |
| --- | --- | --- |
| Static meshes | Validated immutable Mesh resources; compact16/32-bit indices or nonindexed point/line/triangle list draws; per-part material and shadow flags | `mesh_asset`, `render_bounds`; native `mesh_gpu_tests.hpp`, `mesh_draw_tests.hpp` |
| Signed/singular transforms | Camera-relative world placement, parity-specific culling and cofactor surface frames; zero/tiny/negative authored visual scale retained | Transform/render bounds; native `surface_frame_tests.hpp`, `mesh_draw_tests.hpp` |
| Skeletal skinning | Four-influence linear blend after morphs, validated joint palettes, actual deformed bounds; signed blend winding uses a geometry stage | `gltf_ozz`, `model_animation_process`, `render_bounds`; native `skin_render_tests.hpp` |
| Morph targets | Position/normal/tangent deltas and admitted weight curves, complete-pose adoption and dynamic bounds | `morph_animation`, model pipeline; native `morph_render_tests.hpp` |
| PBR | Pinned Diligent metallic-roughness and archived specular-glossiness BRDF paths, semantic textures and normal/tangent handling | Material/glTF surface tests; native mesh/material fixtures |
| Unlit | Material factors, vertex color and color texture without light dependence; engine error surface uses this path | Material tests; native `mesh_draw_tests.hpp`, `frame_renderer_tests.hpp` |
| Alpha mask | Shared alpha/cutoff in color and shadow depth paths | Native `shadow_render_tests.hpp` and mesh material fixtures |
| Alpha blend | Straight-alpha composition, depth testing, disabled imported BLEND depth writes and back-to-front part ordering | CPU queue tests; native mesh/transparency fixtures; no order-independent transparency |
| Advanced material layers | Clearcoat, sheen, anisotropy, iridescence, IOR/specular, emissive strength, transmission, volume and dispersion use their declared factors/textures | `gltf_surfaces`, material tests; native layered material/optics fixtures |
| Transmission/volume | Copied opaque HDR background plus environment fallback, thickness/attenuation and chromatic dispersion | Native `transmission_background_tests.hpp`, `transmission_render_tests.hpp`; screen-space approximation does not solve arbitrary layered refraction |
| Custom material surface Shader | Engine geometry with declared color/depth pixel roles, reflected parameter packing and named Diligent bindings | Shader/material pipeline; native `surface_mesh_draw_tests.hpp`; current custom cook adapter D3D12 only |
| Directional/point/spot lights | Physical authored intent, scene extraction, layers and bounded per-draw light lists (64) | Camera/light authoring and bounds; native `punctual_light_tests.hpp` |
| Shadows | Directional cascades, spot maps and six point faces; mask-aware depth, caster/receiver/layer policy,3x3 PCF, cascade blend/fade | Native `shadow_view_tests.hpp`, `shadow_render_tests.hpp`; profile caps8 shadow lights and256MiB D32 payload, independently of device limits |
| Environment/sky/IBL | Selected HDR/cube data, cached native prefiltering/BRDF LUT, sky and material illumination; scene-owned intensity/rotation | Resource/environment tests; native sky/IBL fixtures; no authored weather or atmospheric simulation |
| HDR and tone mapping | RGBA16F lighting, manual exposure, native PBR Neutral tone map, one sRGB encoding into SDR display | Native `display_resolve_tests.hpp`, `frame_renderer_tests.hpp`; no HDR-monitor output or auto exposure |
| Perspective/orthographic cameras | Explicit authored projection, viewport rectangle/order/clear policy, clipping, fixed aspect/letterbox and layer masks | Camera authoring/math; native `frame_renderer_tests.hpp` |
| Culling | Conservative current-pose world bounds, per-mesh/per-part frustum tests, layer and structural visibility filters | `render_bounds`, posed model tests; native frame workload; no occlusion hierarchy claim |
| Batching/instancing | Shared complete draw bundles; compatible static opaque/masked color parts use64-instance batches; separate authored entities/picking remain intact | Native batched-vs-individual pixel comparison and1024-instance workload in `frame_renderer_tests.hpp` |
| LOD | Decreasing authored screen-coverage thresholds select per-view geometry/material mappings; validated MSFT_lod mesh subset; Mesh viewer inspection | Model LOD tests, native close/far draw readbacks and editor captures; no automatic simplification-quality promise |
| Debug visualization | Editor grid/orientation, selection/framing, camera/light guides and model joints from extracted current pose; Game excludes authoring overlays | Viewport/editor scale/input tests; native captures and animation debug fixtures |
| Missing/failed resources | Initial material/semantic texture fallback, independent fallback identity, stale-candidate rejection and later known-good draw retention | `draw_fallbacks`, material/publication tests; native missing-material/texture and recovery readbacks |
| Async resource lifetime | Shared CPU requests, detached complete candidates, owner-thread native allocation, strong leases and fence-governed retirement | `resource_lifetime`, model candidate tests; native `gpu_residency_tests.hpp` |

## Explicit limits

Skinned winding currently requires enabled geometry shaders; the backend matrix
records unsupported targets. Shared code does not bypass that check or silently
change authored transform semantics. Static batching excludes morphing, skinning,
blended and transmissive color draws; shadow casters submit separately. Conventional
transparent part sorting cannot resolve all intersecting surfaces or cyclic overlaps.

The current forward pipeline has no motion-vector/previous-depth history. Advanced
Bloom/TAA/SSAO/SSR/DoF remain explicitly dispositioned in the
[exact-pin post-processing audit](rendering-postprocessing.md), rather than exposed
as controls that cannot work. Material/shader graph editing, path tracing, Gaussian
splatting, terrain and VFX authoring are separate domains, not implied by this table.

The [rendering contract](rendering-foundation.md), [cameras/lights](cameras-lights.md),
[materials](material-assets.md), [surface Shaders](surface-shaders.md) and
[resource lifetime](runtime-resources.md) describe the detailed boundaries. Resource budgets
are engine admission limits; they are not hardcoded universal D3D12 hardware limits.
