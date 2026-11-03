# Fast approximate anti-aliasing

The optional `rendering.fast_approximate_anti_aliasing` section belongs to Scene
rendering version 1. Omitted values are `enabled:false`, `subpixel_strength:0.5`
and `search_steps:12`. Strength is finite 0–1; steps are an integer 1–16. Parsing
checks double precision before narrowing. Native typed callers use the same
validation. Older Scene documents retain their values and default output without
materializing a new section.

## Rendering boundary

DisplayResolve owns the independently authored filter, its single cached optional
RGBA8_UNORM target, parameter buffer, sampler and pipeline. It consumes the composed
display-encoded image after HDR effects, exposure, tone mapping and color transfer.
It does not filter linear HDR output, feed the exposure meter or change temporal
history. FrameRenderer and the Scene viewport pass their existing Scene settings;
Game UI, Scene grid and editor overlays draw afterward to the returned target.
Output and target identify the same image, including after enabling/disabling.
The existing Display resolve profiling scope includes the optional pass.

The shader gates low local luminance contrast, chooses an edge normal from its
neighborhood, searches both edge directions and reconstructs a bounded subpixel
offset. Optional local filtering softens isolated detail. The search uses at most
16 steps per direction. Central alpha is retained; neighborhoods differing by more
than one 8-bit alpha step keep their original color. Sampling clamps to the image
border. This is a current composed-image profile, not geometry reconstruction,
multisampling or a temporal stability guarantee. Camera viewport dividers are part
of that composed image. UI remains outside it.

The conceptual reference is [FXAA, Timothy Lottes, NVIDIA white paper](https://developer.download.nvidia.com/assets/gamedev/files/sdk/11/FXAA_WhitePaper.pdf).
FORGE's shader, configuration and resource integration are independently authored;
no supplied shader, preset or lookup asset is integrated.

## Ownership and budgets

Disabled filtering keeps the original display arithmetic and creates no filter
pipeline or target. Enabling prepares the optional resources through the existing
presentation owner. Tone-mapped and filtered resize candidates are allocated before
publishing either target or drawing. Invalid settings, oversized requests and target
allocation failure retain the prior target pair. Native draw/device failures retain
their existing presentation error semantics.

The filter admits at most 8,388,608 output pixels per display owner, including 4K
3840×2160. Its additional cached texture is at most 32 MiB; ordinary HDR/depth targets,
native allocation overhead and other views are separate. The cached target can be
reused after toggling the effect and is released with the display owner. No new
asset, resource database, material cook, source identity or render manager is added.

## Authoring and programmatic access

Scene lighting exposes Enable, Subpixel smoothing, Edge search steps and Reset.
The shared `scene.rendering.set` command validates partial patches, retains unknown
metadata and uses normal revisions, Undo/Redo and Save. Its existing MCP binding
`forge_scene_rendering_set` discovers the same fields and limits; permissions and
receipt replay stay unchanged. Authored controls follow the normal Play lock.
Source-free export carries saved Scene settings through the existing content owner.
These renderer-owned settings are outside the installed native SDK value boundary.
The SDK fingerprint and callback tables are unchanged. Existing compatible modules
remain compatible; the runtime must include the new renderer implementation.

## Evidence and current limits

`hdr_bloom` checks defaults, malformed/native values, precision bounds, partial
edits, history, extensions, reference inspection, reopening and discovery. Actual
lighting-input fixtures cover six scales/layouts. Both MCP framings check schemas,
replay, saving and rejected revisions through actual owners.

`fast_approximate_aa_vulkan` compares reconstructed neutral/blue and HDR edges with
independent integrated coverage, checks small/flat/border/alpha/resize behavior,
disabled identity, budget retention, real FrameRenderer/Viewport cache and history,
temporal-AA combination and unchanged linear HDR output. The optional
`fast_approximate_aa_ui_vulkan` uses actual UiPresenter/UiDiligent text and requires
identical post-display UI pixels. Gallery 143 checks actual animation and saved
settings after source-hidden relocated content export; moving Game image pairs
are compared separately.

Software Vulkan results do not establish Windows/MSVC/FXC/D3D12, physical GPU/input /
DPI or graphical standalone acceptance. Those reviews remain separate. The user
workflow is in the [lighting guide](../manual/editor/lighting.md).
