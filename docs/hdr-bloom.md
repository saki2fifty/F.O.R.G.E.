# Spatial HDR bloom

Scene rendering version1 accepts optional `bloom`: `enabled` (false), `threshold`
(1), `intensity` (.25) and `radius` (2). Threshold is finite0–65504 linear light,
intensity0–4, and radius0.5–16 half-resolution pixels. Omission retains old output.
The existing `scene.rendering.set` command validates complete candidates and merges
known nested fields; opaque extensions remain. Scene Save/Undo and ordinary runtime
packaging own these values. No new asset type or installed gameplay SDK input is added.

## Original pass and composition

`HdrBloom` is original FORGE code. It uses linear RGBA16F color without depth,
motion vectors or frame history. It does not integrate or copy DiligentFX Bloom;
that utility's temporal context remains gated by the
[postprocessing audit](rendering-postprocessing.md).

The current presentation owner creates/caches shaders and PSOs. One view-local HDR
snapshot avoids SRV/RTV aliasing. A2x2 average extracts highlights into ceil(width/2)
by ceil(height/2); horizontal and vertical filtering precede composition into the
same camera rectangle. Execution submits one copy and four draws. Extraction removes
the threshold proportionally using the largest positive RGB channel. The original
separable kernel has weights1/4/6/4/1 divided by16; radius controls tap spacing.
Bilinear reconstruction and edge replication stay inside the view. Composite adds
intensity-scaled glow, preserves alpha and bounds finite RGB to65504. Nonfinite
original pixels remain available to the existing display-resolve diagnostic.

Game applies bloom after each camera's geometry and before final exposure/tone
mapping/sRGB resolve. Camera order remains authoritative: an overlapping later
camera processes its then-composed rectangle, including retained earlier color.
This can add glow again in an overlap; there is no grouped camera-stack effect
contract. Separate view borders and letterbox pixels remain isolated. Scene View
uses its one HDR view before display resolve and editor overlays. Runtime UI is
composed afterward. Disabled or zero-intensity bloom creates no shaders/targets
and submits no copy/draw. The legacy LDR comparison viewport does not execute it.

## Resources and admission

Each pass owns at most eight cached texture sets by view dimensions, each containing
one full-resolution snapshot and two half-resolution RGBA16F targets. Repeated and
alternating camera sizes reuse their sets. Least-recently-used sets retire when the
eight-set bound or128MiB aggregate retained payload would be exceeded. A smaller
backend/test budget may be selected; it cannot exceed128MiB. Dimensions, format/
binding support, rectangle bounds and bytes are checked first. Complete new sets
are allocated before old sets are evicted/replaced. In-flight work/bindings keep
their normal strong references. Admission failure retains prior resources/pixels.

The payload excludes texture alignment, driver overhead, candidates, other renderer
resources and in-flight retired objects; it is not a total VRAM ceiling. No temporal
clock/history is retained. Backend failures use current presentation error handling.
Internal creation callbacks let the identical pass run with explicitly precompiled
SPIR-V in the optional Linux probe. Shipped creation uses `DiligentPresentation`;
there is no second renderer service or compiler in the exported game. Local Vulkan
pixel checks do not replace Windows/D3D12 or physical acceptance.

Gallery48 uses two original emissive Material sources/cooks, an ordinary cube, floor,
camera and light, through existing publication, typed references and source-free
export. See the [Gallery guide](../samples/projects/FeatureGallery/README.md) and
[lighting walkthrough](../manual/editor/lighting.md).
