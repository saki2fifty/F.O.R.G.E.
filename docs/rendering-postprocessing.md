# Post-processing integration boundary

[Distance fog](distance-fog.md) is a geometry-surface operation before blending
and HDR composition. It adds no depth/motion history or post-processing targets.

## FORGE passes and upstream integration

FORGE supplies independently authored [spatial HDR bloom](hdr-bloom.md),
[automatic exposure](scene-auto-exposure.md), [local reflection probes](local-reflection-probes.md), [screen-space reflections](screen-space-reflections.md), [ambient occlusion](ambient-occlusion.md), [spatial depth of field](depth-of-field.md), [motion blur](motion-blur.md) and
[temporal anti-aliasing](temporal-antialiasing.md) through the existing rendering
owners. Bloom uses color only. Automatic exposure meters composed Game HDR into
a scalar gain history. Temporal anti-aliasing supplies real per-view motion,
previous depth/camera data and previous posed Mesh geometry for its supported
opaque/masked profile. These are separate passes with their documented admission,
reset and budget rules. Spatial ambient occlusion uses current depth and separately resolved environment radiance,
with optional current material-normal refinement and no temporal occlusion history. Spatial depth of field uses each view's current HDR/depth
without adding temporal history. Reflections consume real current material normal,
roughness, unit response and replaceable base specular, updating HDR and indirect
before occlusion. They do not expose the upstream effects as working toggles.

The exact-source audit below was verified on2026-09-22 against pinned DiligentFX
`aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`. It describes the upstream integration
boundary, not the availability of FORGE's original passes. FORGE's derived temporal
inputs remain with its own per-view frame/mesh owners; they are not currently
published through upstream PostFXContext. FORGE supplies its own current material targets for its bounded spatial
reflection profile; the additional upstream temporal/context inputs remain
separate work. Windows
acceptance is distinct from local software Vulkan pixel checks.

## Exact-source findings

| Pinned utility | Required inputs / behavior | Phase7 disposition |
| --- | --- | --- |
| Bloom | Color input, plus a ready PostFXContext; a pending context returns the placeholder copy | Deferred with the temporal post-effect foundation; not exposed as a working toggle |
| TemporalAntiAliasing | Color, jittered projection, accumulation history, context motion/depth/camera data | Upstream module not enabled; FORGE supplies its own bounded temporal profile |
| ScreenSpaceAmbientOcclusion | Depth, normal buffer and temporal context | Upstream module not enabled; FORGE supplies its own bounded current-depth spatial profile with optional material-normal refinement |
| ScreenSpaceReflection | Color, depth, normals, material buffer, motion vectors and temporal context | Upstream module not enabled; FORGE supplies its own bounded current-depth base-specular profile |
| DepthOfField | Color/depth and temporal context | Upstream module not enabled; FORGE supplies its own bounded spatial focus profile |

Bloom's public arguments alone look compatible with the current color target.
Its actual `Execute` method also requires `PostFXContext::IsPSOsReady()`.
That flag starts false and becomes ready in `PostFXContext::Execute`, which
requires current/previous depth, motion vectors and current/previous camera data
and runs reprojection. Calling only `PrepareResources` does not make Bloom ready.
Supplying fabricated motion/history merely to enable Bloom would conceal missing
contracts. Patching upstream or duplicating its implementation is not selected.

These upstream effects remain deferred under the optional advanced
post-processing integration boundary. No Diligent upgrade is needed or proposed. Revisit their exact inputs and lifecycle before extending the current per-view
temporal profile: previous posed geometry and camera
matrices, motion/depth conventions, camera-cut/resize/reload/history invalidation,
multiple-camera composition, format/capability admission and budget/retirement.
That work must cover moving, skinned and morphing content, not only a static
single-camera demonstration. This does not defer required material, lighting,
shadow, texture, shader or ordinary display-resolve work.

## Pinned references

- [Bloom execution and readiness gate](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/Bloom/src/Bloom.cpp).
- [PostFXContext inputs and state](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/Common/interface/PostFXContext.hpp) and [execution/reprojection](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/Common/src/PostFXContext.cpp).
- [Temporal anti-aliasing](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/TemporalAntiAliasing/src/TemporalAntiAliasing.cpp).
- [Ambient occlusion inputs](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/ScreenSpaceAmbientOcclusion/interface/ScreenSpaceAmbientOcclusion.hpp).
- [Screen-space reflection inputs](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/ScreenSpaceReflection/interface/ScreenSpaceReflection.hpp).
- [Depth-of-field inputs](https://github.com/DiligentGraphics/DiligentFX/blob/aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da/PostProcess/DepthOfField/interface/DepthOfField.hpp).

See the [backend capability matrix](render-backends.md) for the distinction between
source-supported mappings, compilation probes and executed native acceptance.
