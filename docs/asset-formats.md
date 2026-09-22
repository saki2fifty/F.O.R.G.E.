# Asset source and cooked-format matrix

This matrix describes concrete FORGE importers and loaders. It does not imply that
all formats understood by a linked dependency are enabled. Phase 7 final Windows
acceptance remains pending; backend evidence is tracked in [render backends](render-backends.md).
Logical AssetIds remain independent of source paths, cooked files and backend profiles.

| Source format/type | FORGE path | Supported content | Limits / rejected content | Dependency / executable evidence |
| --- | --- | --- | --- | --- |
| glTF 2.0 JSON / GLB 2 Model | `forge.model.gltf` model importer and isolated worker | Captured buffers/images, multiple scenes, hierarchy, meshes, materials, textures, cameras, lights, skeletons, TRS clips, morphs, variants and authored mesh LODs | Point/line/triangle list output, including normalized source loops/strips/fans; unsupported required extensions reject; bounded workers and explicit influence/joint limits | Pinned Diligent GLTF, meshoptimizer, Draco, Ozz and image codecs; `gltf_*`, `model_bundle`, `model_recipe`, `model_pipeline`, official sample corpus |
| PNG Texture | Texture image recipe | Gray/RG/RGB/RGBA, alpha, admitted 8/16-bit channels, explicit color/data/normal usage and prepared mips | Strict CRC/truncation/dimension checks; high-precision color requires linear storage | Pinned libpng bounded reader and Diligent processing; `texture_import`, `texture_pipeline`, robustness corpus |
| JPEG Texture | Texture image recipe | Bounded still-image RGB decode, prepared semantic variants/mips | No alpha; no general color-management or EXIF-orientation workflow | Private pinned stb JPEG path; `texture_import`, `texture_pipeline` |
| TGA Texture | Texture image recipe | Admitted uncompressed/RLE source, canonical pixels and mip preparation | Bounded supported header/layout subset; no generalized metadata editing | Pinned Diligent/stb path; `texture_import` |
| BMP Texture | Texture image recipe | Bounded palette, RGB/RGBA and supported bitfield layouts | Unsupported compression/layout rejects; validated payload extents | FORGE admission plus pinned native decoding; `texture_bmp` |
| WebP Texture | Texture image recipe | Lossy/lossless still image, straight alpha and shared mip/compression pipeline | Animated files and ICC conversion reject; source metadata is preserved without claiming orientation conversion | libwebp1.6.0; `texture_webp`, `texture_recipe`, `model_recipe` |
| HDR/RGBE Texture | Texture image recipe | Floating-point RGBE, flat/RLE rows, HDR mips | Canonical orientation; unsupported old repeat markers, malformed rows and unsupported encoding reject | Pinned stb/Diligent processing; `texture_import`, `texture_recipe` |
| DDS Texture | Texture container recipe | Supported native formats, mip chains, arrays, cube faces and volume slices | Validated header/profile/complete payload; device admission remains separate | Pinned Diligent DDS reader; `texture_dds`, `texture_recipe` |
| KTX1 / KTX2 Texture | Texture container recipe | Supported raw formats, BC passthrough including BC6H, mip chains, arrays, cubes and volumes; KTX2 Zstd, ETC1S/BasisLZ and UASTC decode/transcode | No 1D profile or Basis volume transcode; orientation/channel/color metadata must match admitted semantics | KTX Software4.4.2; `texture_ktx`, `texture_recipe`, `gltf_surfaces` |
| Prepared texture → Basis KTX2 | Texture recipe compression choice | Official ETC1S/UASTC encoding for RGBA8 2D mip chains | No HDR/BC6H encoder or arbitrary dimensional encoding; exact build-profile repeatability, not cross-platform byte identity | Official bundled Basis encoder; `texture_ktx`, `texture_recipe` |
| `.material.json` Material | Material source publisher | Sparse base inheritance, independent parameters/textures/state overrides, built-in PBR/unlit or admitted custom surface Shader | Cycle/type/dimension/semantic/interface validation; no graph editor | Existing resource/publication services; `material_asset`, `material_pipeline`, `material_editor`, native surface fixtures |
| `.shader.json` Shader and contained HLSL/includes | Shader source importer/compiler worker | Stage sets, entry points, captured include dependencies, selected permutations, reflected layouts and custom surface roles | Current cooked compiler profile Windows D3D12/FXC5.1; no DXIL/mesh/ray stages; runtime loads cooked artifacts | Pinned Diligent compiler/reflection adapter; `shader_assets`, `shader_pipeline`, `shader_worker`, native shader/surface tests |
| Audio source / AudioClip | Audio recipe and existing runtime loader | Admitted WAV decode, immutable clip publication, audition and source-free runtime loading | WAV only; MP3/FLAC remain disabled; no audio workstation or graph tooling | Pinned miniaudio; `audio_recipe`, `audio_pipeline`, `audio_tools`, `audio`, native audition fixture |
| RML/RCSS UI documents and font/image dependencies | UI catalog recipe and runtime presenter | Captured document/style/font/image dependency closure, published revision and runtime loading | Existing admitted RmlUi resource/URL policy; no visual UI designer | Pinned RmlUi/FreeType; `ui_asset_catalog`, `ui_catalog_presenter`, `runtime_ui`, presenter/process fixtures |
| Scene / structured Prefab JSON | Authored document operations | Persistent AssetId/EntityId/member identity, source preservation, explicit prefab overrides and candidate publication | Authored format and history ownership remain separate from generic binary import | `persistent_identity`, `structured_prefabs`, `hierarchical_transforms`, authoring tests |
| Ozz skeleton/clip archives | Animation admission, selected-resource loader and official conversion worker | Exact0.17 archive subset, skeleton compatibility/provenance, bounded playback and recovery | Validated before Ozz load; no arbitrary Ozz-version compatibility | Pinned Ozz0.17.0; `animation_archive`, `animation_worker`, `gltf_ozz`, `model_animation_process` |
| Navigation data | Navigation build/publication and runtime load | Existing versioned Recast/Detour artifacts, queries and agent integration | Existing navigation geometry/build profile; not an alternate model source importer | Pinned Recast/Detour; `navigation`, `navigation_mesh` |

## Cooked data and shipping

Runtime Model families, Mesh, Texture, Material, Shader and Audio resources load
validated immutable cooked data through the catalog and typed resource pools.
[Runtime content packaging](runtime-content-packaging.md) records the exact package closure
and supported adapters. Engine-owned primitive/material/texture references use
reserved UUIDs and immutable engine recipes; no project source file is required.
A format being accepted by an authoring converter does not authorize runtime source
decoding, compiler loading or a second object hierarchy.

See the [complete core/extension matrix](gltf-features.md), [glTF admission](gltf-admission.md), [model import](model-import.md),
[texture formats and exact limits](texture-assets.md), [material assets](material-assets.md)
and [shader assets](shader-assets.md) for the detailed source/ownership contracts.
