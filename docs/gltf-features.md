# glTF feature and extension matrix

Verified2026-09-22 against the [official registry at836573be](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/README.md).
All27 ratified entries are accounted for below. The list is unchanged from the
initial Phase7 research; current core geometry clarifications are recorded in
[glTF admission](gltf-admission.md). Diligent and codec pins are unchanged.

“Implemented” means the described bounded FORGE path exists. It does not claim
whole-extension conformance or final platform acceptance. **Final Phase7 Windows
validation remains pending.** The [model contract](model-import.md) and
[renderer/backend matrices](render-features.md) provide the runtime limits.

## Core format

| Area | Current implementation | Limits / executable coverage |
| --- | --- | --- |
| Containers/buffers | JSON glTF2 and GLB2, external/embedded buffers and images, supported data URIs, captured immutable inputs | Complete length/alignment/range checks; contained paths, no network; gltf_source/native/snapshot/accessor tests |
| Accessors | Float/integer scalars/vectors/matrices, normalization, padded matrices, strides, implicit zero and sparse overlays | Shape/count/alignment/finite/bounds validation before native access; gltf_accessors, gltf_native_capture |
| Mesh attributes | Positions, normals, tangents/sign, indexed UV/color sets, paired joints/weights, admitted custom streams | Current COLOR_0[0,1] and canonical nine-digit set indices; unsupported extension-qualified/custom uint32 profile rejects; gltf_mesh_processing |
| Topology/indices | Triangle lists, strips and fans converted to validated list geometry; indexed8/16/32-bit source and nonindexed input | Points/lines reject; forbidden restart values and out-of-range indices reject; gltf_mesh_processing, mesh_asset |
| Materials/textures/samplers | PBR factors and semantic texture roles, imported sampler/filter/wrap, selected extension layers, alpha/state policy | glTF core PNG/JPEG plus declared WebP/Basis; unsupported images/sampling state reject; gltf_surfaces, texture suites, native material fixtures |
| Nodes/scenes | Validated trees, multiple scenes/default selection, explicit TRS or representable matrix import, stable node provenance | Signed/zero TRS supported; matrix/ECS representability and cycles rejected explicitly; gltf_hierarchy, model_scene, model_recipe |
| Cameras/lights | Perspective/orthographic source cameras and declared punctual-light extension map to authored Flecs components | Explicit imported -Z camera/light basis and units; model_scene, native camera/light fixtures |
| Skins | Joint hierarchy, optional identity inverse binds, exact joint remap and rest/provenance checks | Four influences per vertex; default reject or explicit top-four reduction;256 joints/draw profile,1024 rig joints; gltf_ozz, model_recipe, native skin fixtures |
| Animation | Core TRS plus supported pointer equivalents; LINEAR/STEP/CUBICSPLINE via admitted canonical official Ozz conversion | Fixed runtime owner, bounded sampling/profile, stable clip identity; gltf_animation_admission, gltf_ozz, model_animation_process |
| Morphs | Position/normal/tangent and retained supported additional streams, default and animated weights, instancing weight fan-out | Runtime deformation consumes its declared streams; gltf_mesh_processing, morph_animation, native morph fixtures |

## Every ratified extension

Unsupported required extensions reject during source capture with their name.
An admitted extension still rejects unsupported payload/target combinations before
publication. Unsupported optional extensions retain a published notice; no optional
behavior is silently advertised as implemented. Raw source preservation is separate
from interpreted/runtime semantics.

| Extension / official source | Classification | Implemented scope or reason | Tests / limits |
| --- | --- | --- | --- |
| [KHR_animation_pointer](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_animation_pointer/README.md) | IMPLEMENTED | Whole node TRS and morph weights, all existing interpolation modes, pointer integer-to-float conversion. Other properties and individual lanes reject. | gltf_animation_admission; gltf_ozz; model_recipe; model_pipeline |
| [KHR_draco_mesh_compression](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_draco_mesh_compression/README.md) | IMPLEMENTED | Native Draco triangle geometry decode with bounded attribute/index validation; supported fallback data and mixed primitives. No claim of arbitrary point-cloud rendering. | gltf_draco; model_recipe; gltf_official_corpus |
| [KHR_gaussian_splatting](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_gaussian_splatting/README.md) | NOT APPLICABLE TO CURRENT FORGE DOMAIN | Separate point/splat geometry, covariance projection, spherical-harmonic radiance and camera-distance sorting are outside this triangle MeshRenderer phase. No fake triangle interpretation or point fallback. | Required extension rejects; unsupported point topology also rejects |
| [KHR_interactivity](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_interactivity/Specification.adoc) | NOT APPLICABLE TO CURRENT FORGE DOMAIN | Requires a bounded event/behavior-graph execution domain and object-model mutation authority. Native gameplay and Ozz tracks are not that graph VM. Optional graph data is not executed. | Required extension rejects; optional notice retained |
| [KHR_lights_punctual](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_lights_punctual/README.md) | IMPLEMENTED | Directional/point/spot metadata becomes authored Light components with the imported -Z basis, units and range/cone semantics. | model_scene; model_recipe; native punctual_light_tests.hpp |
| [KHR_materials_anisotropy](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_anisotropy/README.md) | IMPLEMENTED | Anisotropy factor/direction/texture through pinned native PBR layers and tangent frame. | gltf_surfaces; material_asset; native mesh_draw_tests.hpp |
| [KHR_materials_clearcoat](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_clearcoat/README.md) | IMPLEMENTED | Clearcoat factor/roughness/normal textures and separate clearcoat normal basis. | gltf_surfaces; material_asset; native mesh_draw_tests.hpp |
| [KHR_materials_dispersion](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_dispersion/README.md) | IMPLEMENTED | Dispersion with admitted transmissive materials; finite screen-background/environment optical approximation. | gltf_surfaces; native transmission_render_tests.hpp |
| [KHR_materials_emissive_strength](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_emissive_strength/README.md) | IMPLEMENTED | Emissive factor/texture plus HDR strength in lighting; SDR tone-mapped display. | gltf_surfaces; material_asset; native material fixtures |
| [KHR_materials_ior](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_ior/README.md) | IMPLEMENTED | Exact material domain (zero or at least one), including its native reflectance/optical role. | gltf_surfaces; material_asset; native optics fixtures |
| [KHR_materials_iridescence](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_iridescence/README.md) | IMPLEMENTED | Factor/IOR/thickness range and texture; reversed thickness endpoints retained as specified. | gltf_surfaces; material_asset; native material fixtures |
| [KHR_materials_sheen](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_sheen/README.md) | IMPLEMENTED | Sheen color/roughness factors/textures through native PBR layer. | gltf_surfaces; material_asset; native material fixtures |
| [KHR_materials_specular](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_specular/README.md) | IMPLEMENTED | Specular weight and color factors/textures, explicit linear/sRGB usage. | gltf_surfaces; material_asset; native material fixtures |
| [KHR_materials_transmission](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_transmission/README.md) | IMPLEMENTED | Transmission factor/texture with opaque HDR background and environment fallback; not general multi-layer refraction. | gltf_surfaces; native transmission_render_tests.hpp |
| [KHR_materials_unlit](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_unlit/README.md) | IMPLEMENTED | Base factor, vertex color, texture, alpha mode and surface state without lighting. | gltf_surfaces; model_recipe; native mesh_draw_tests.hpp |
| [KHR_materials_variants](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_variants/README.md) | IMPLEMENTED | Stable logical variant assets; per-primitive/per-LOD material mapping, source default fallback, whole-model selection and independent manual slot overrides. | material_variant_bundle_tests.hpp; material_variant_pipeline_tests.hpp; native mesh_draw_tests.hpp |
| [KHR_materials_volume](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_materials_volume/README.md) | IMPLEMENTED | Thickness texture/factor, attenuation color/distance and optical transport; positive authored attenuation distance; an omitted distance represents no attenuation. | gltf_surfaces; material_asset; native transmission_render_tests.hpp |
| [KHR_mesh_quantization](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_mesh_quantization/README.md) | IMPLEMENTED | Declared required extension; admitted quantized core/morph attribute formats, conversion, bounds and tangent/normal treatment. | gltf_mesh_processing; gltf_official_corpus |
| [KHR_node_hoverability](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_node_hoverability/README.md) | NOT APPLICABLE TO CURRENT FORGE DOMAIN | No runtime 3D hover-ray/event owner in this phase. Editor selection/picking is a distinct operation; do not approximate hoverability with selectability. | Required extension rejects; optional notice retained |
| [KHR_node_selectability](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_node_selectability/README.md) | IMPLEMENTED | Authored local flag and ancestor-effective structural policy, independent of visibility; retained mesh picking respects it. No interactivity event VM. | model_scene; render_bounds; native frame_renderer_tests.hpp |
| [KHR_node_visibility](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_node_visibility/README.md) | IMPLEMENTED | Authored local flag and structural ancestor propagation, separate from active/enabled and spatial parenting. | model_scene; render_bounds; native frame_renderer_tests.hpp |
| [KHR_texture_basisu](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_texture_basisu/README.md) | IMPLEMENTED | Admitted KTX2 ETC1S/UASTC with semantic/DDF metadata checks, native desktop transcode or RGBA CPU target. | texture_ktx; gltf_surfaces; model_recipe |
| [KHR_texture_transform](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_texture_transform/README.md) | IMPLEMENTED | Offset/scale/rotation/UV-set override retained per material binding, with normal-map tangent UV selection. | gltf_surfaces; mesh_processing; native mesh material fixtures |
| [KHR_xmp_json_ld](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Khronos/KHR_xmp_json_ld/README.md) | APPLICABLE BUT DEFERRED WITH TECHNICAL REASON | Source metadata remains in captured/original source, but packet references, JSON-LD/XMP interpretation, metadata UI and shipping-provenance propagation have no consumer/contract yet. No external context fetch or conformance claim. | Required extension rejects; optional notice retained |
| [EXT_mesh_gpu_instancing](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Vendor/EXT_mesh_gpu_instancing/README.md) | IMPLEMENTED | Bounded TRS accessor expansion into ordinary authored child nodes, including signed/zero scale and shared morph-channel data. Skinned instancing rejects explicitly. Rendering may batch eligible static copies. | gltf_animation_admission; model_recipe; native frame_renderer_tests.hpp |
| [EXT_meshopt_compression](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Vendor/EXT_meshopt_compression/README.md) | IMPLEMENTED | Official meshoptimizer decode modes/filters with captured ranges and bounded output; normal model publication and rendering consume decoded content. | gltf_meshopt; model_recipe |
| [EXT_texture_webp](https://github.com/KhronosGroup/glTF/blob/836573be93954f26827e3dc16476f8620209a1f2/extensions/2.0/Vendor/EXT_texture_webp/README.md) | IMPLEMENTED | Declared still WebP images through libwebp with MIME/signature/semantic checks; animated/ICC inputs reject. | texture_webp; gltf_surfaces; model_recipe |

The separate-domain decisions do not require replacing or upgrading the current
asset/ECS/renderer architecture. They identify absent consumers rather than claim
that a graph VM, Gaussian renderer or hover-event system has been supplied by mesh
import. Future authorization must define those domain assets, execution/ownership,
budgets and backend mappings. No new general evaluator is hidden in Phase7.

## Additional admitted formats

`KHR_materials_pbrSpecularGlossiness` is archived upstream but remains an explicit
compatibility material path. `MSFT_lod` is a vendor extension, not a ratified
Khronos extension: FORGE admits validated geometry-only node alternatives, with
compatible hierarchy/skin/morph/material layout and authored screen thresholds.
Unsupported material-level replacement or independently animated alternatives reject.
Neither is counted among the27 ratified entries.

## Draft and proposal inventory

These are awareness records only; none is adopted as a stable production contract.
Unsupported required declarations reject under the ordinary capture rule.

| Extension / project | Official registry state | FORGE adoption |
| --- | --- | --- |
| KHR_accessor_float64 | Review Draft | Not enabled |
| KHR_audio_graph | Proposal | Not enabled |
| KHR_collision_shapes | Review Draft | Not enabled |
| KHR_materials_diffuse_transmission | Release Candidate | Not enabled |
| KHR_materials_subsurface | Initial Draft | Not enabled |
| KHR_physics_rigid_bodies | Review Draft | Not enabled |
| KHR_texture_procedurals | Initial Draft | Not enabled |
| EXT_texture_procedurals_mx_1_39 | Review Draft | Not enabled |
| KHR_texture_video | Initial Draft | Not enabled |
| glTF External References | Proposal | Not enabled |

Registry states must be rechecked against an exact official revision when future
work selects a feature. A proposal appearing in live documentation does not justify
a dependency upgrade or a local imitation of missing upstream APIs.
