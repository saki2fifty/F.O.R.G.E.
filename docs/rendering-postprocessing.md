# Post-processing integration boundary

Verified2026-09-22 against pinned DiligentFX
`aaa41d47a101d0bf1d12267c4a85b2d9b38cd1da`. This is an integration audit,
not a claim that every linked upstream effect is available in FORGE.

FORGE currently composes HDR color and depth, then uses its display resolve for
exposure, tone mapping and display encoding. A composed frame can contain multiple
camera rectangles. It does not currently supply motion vectors, previous-frame
depth/camera history or normal/material render targets. Shared code uses Diligent
resource and pipeline interfaces; these missing inputs are renderer work rather
than a reason for D3D12-specific interfaces.

## Exact-source findings

| Pinned utility | Required inputs / behavior | Phase7 disposition |
| --- | --- | --- |
| Bloom | Color input, plus a ready PostFXContext; a pending context returns the placeholder copy | Deferred with the temporal post-effect foundation; not exposed as a working toggle |
| TemporalAntiAliasing | Color, jittered projection, accumulation history, context motion/depth/camera data | Deferred until motion/history ownership and reset rules exist |
| ScreenSpaceAmbientOcclusion | Depth, normal buffer and temporal context | Deferred until the required render targets/history exist |
| ScreenSpaceReflection | Color, depth, normals, material buffer, motion vectors and temporal context | Deferred until the required render targets/history exist |
| DepthOfField | Color/depth and temporal context | Deferred with camera-history and depth preparation |

Bloom's public arguments alone look compatible with the current color target.
Its actual `Execute` method also requires `PostFXContext::IsPSOsReady()`.
That flag starts false and becomes ready in `PostFXContext::Execute`, which
requires current/previous depth, motion vectors and current/previous camera data
and runs reprojection. Calling only `PrepareResources` does not make Bloom ready.
Supplying fabricated motion/history merely to enable Bloom would conceal missing
contracts. Patching upstream or duplicating its implementation is not selected.

These effects are deliberately deferred under Phase7's optional advanced
post-processing clause. No Diligent upgrade is needed or proposed. Revisit them
with a coherent per-view temporal pipeline: previous posed geometry and camera
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
