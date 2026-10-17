# Scene color adjustments

Scene rendering version 1 accepts optional `color_adjustments`: `enabled` defaults
to false; `filter` is three linear RGB multipliers, each 0–4, default `[1,1,1]`;
`saturation` and `contrast` each range 0–2 and default to 1. Values must be finite;
the parser checks doubles before converting to native floats. Omitted settings
retain existing output. Older consumers preserve these optional settings as unknown
data but do not apply them.

`scene.rendering.set` merges the named section through the existing SceneDraft.
Native validation, one ordinary Scene Undo/Redo per committed operation, explicit
Save/reopen and runtime packaging own publication. Reset updates all four known
properties together and preserves opaque extensions. No asset type, migration,
shader-source rewrite or gameplay ABI/function-table extension is introduced.

## One display pass

The existing DisplayResolve consumes these settings for Scene View and the composed
Game image. The order is:

1. Apply existing exposure and the enabled linear RGB filter to HDR lighting.
2. Run the existing pinned native PBR Neutral tone mapper.
3. Adjust mapped linear RGB saturation around Rec.709 luminance using weights
   `[0.2126,0.7152,0.0722]`.
4. Adjust linear contrast around 18% gray, clamp to the SDR 0–1 range, and encode
   sRGB exactly once. Preserve the existing clamped source alpha.

This is original FORGE adjustment code around the existing attributed native mapper;
no competitor implementation is copied. Disabled or enabled-neutral settings skip
the new arithmetic and retain the earlier output path. Active grading uses one
additional uniform row in the existing pass, with no extra target, copy, draw,
shader variation, LUT, frame history or renderer manager. Invalid native settings
reject before replacing resources or pixels. Nonfinite HDR/mapped light retains
magenta diagnostics instead of being concealed by a black filter or SDR clamp.

Settings affect all composed camera rectangles, sky, transparent geometry and bloom.
They run once after camera composition; this is not a separate grade for each
camera. Editor overlays/controls and the later runtime UI keep their own colors.
Scene View's personal exposure remains independent from authored Game exposure.
Clamping can remove dark/highlight detail at strong settings. There is no automatic
exposure, white-balance temperature model, 3D LUT or HDR-monitor output claim.

## Authoring and evidence

Use **Tools → Scene lighting... → Color adjustments**. Controls are read-only during
Play. See [the lighting guide](../manual/editor/lighting.md) and original editable
Gallery69. It reuses the Gallery's shared Material and normal Mesh/Camera/Light path.

Normal/sanitizer Scene and UI checks, software Vulkan pixel comparisons, actual
Game/split-camera images and relocated source-free Gallery output pass locally.
The numerical test measures the unchanged mapper as an 8-bit baseline and allows
bounded quantization error in a double-precision linear adjustment reference; it
does not duplicate the native mapper. D3D12/editor native captures and physical
Windows acceptance remain separate pending gates.
