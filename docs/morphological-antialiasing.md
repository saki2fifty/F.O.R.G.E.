# Morphological anti-aliasing

The optional `rendering.morphological_anti_aliasing` section extends Scene rendering
version 1. Defaults are `enabled:false`, `edge_threshold:0.1`, `search_steps:16` and
`corner_rounding:0`. Threshold is finite 0.01..0.5, search steps are integer 1..32,
and rounding is finite 0..1. Source checks precede float narrowing; native typed
callers use the same validation. Reading an older Scene does not author defaults.
Unknown section fields retain the existing opaque metadata/reference-inspection
policy.

## Existing ownership

DisplayResolve owns this independently authored three-pass 1x spatial profile,
its pipelines, parameter buffer and bounded optional images. It reads composed,
display-encoded RGBA8 after tone mapping, before runtime UI and editor overlays.
Linear HDR, exposure metering, motion and temporal history retain their owners.
Game and Scene preview pass their existing Scene settings. Returned output and
subsequent UI/guide target identify the same image.

Only one spatial filter may be enabled: fast approximate or morphological.
Scene parsing and native display requests reject conflicting methods before
replacing usable state. Scene lighting enables a method through one explicit
patch that disables the other; switching is one Undo entry. Automation/MCP callers
submit those two fields in the same existing `scene.rendering.set` operation.
Temporal anti-aliasing remains independent and may accompany either method.

## Independently derived reconstruction

The conceptual reference is [SMAA: Enhanced Subpixel Morphological Antialiasing](https://www.iryoku.com/smaa/downloads/SMAA-Enhanced-Subpixel-Morphological-Antialiasing.pdf),
Jimenez, Echevarria, Sousa and Gutierrez. No supplied implementation, shader, lookup
image or table generator is integrated, copied, translated or adapted.

The first pass detects north/west boundaries from maximum RGB-channel difference
and local contrast. The second walks bounded grid contours, distinguishes observed
endpoints from search truncation, protects extended corners and adjacent one-pixel
features, and recognizes connected diagonal staircases. Weights come from an
original analytic integration of a piecewise-affine boundary over a unit pixel:
positive and negative areas are trapezoids or sign-crossing triangles. Equal-sign
turns bend through the contour midpoint. Signed half-float weights tag diagonal
contributions; orthogonal junctions select one normal. The final pass reconstructs
neighbor colors while retaining central alpha. Immediate alpha discontinuities
bypass filtering. Samples stay inside the image.

This is the stated bounded current-image 1x profile. It does not promise reference
shader/table equivalence, multisample or temporal morphological variants, recovery
of hidden geometry, exact subpixel phase, or universal thin-feature reconstruction.
Conservatively retained one-pixel detail and truncated contours can remain aliased.
The display image includes composed camera viewport dividers; UI follows it.

## Resources and errors

Disabled filtering follows the original display arithmetic without preparing a
morphological pipeline/image. Both spatial methods reuse one RGBA8 output. The
morphological path adds RGBA8 edges and RGBA16F coverage, at most 8,388,608 output
pixels: 128 MiB optional cached logical pixel storage total. Base HDR/display/depth,
native overhead and other views are separate. Optional images remain reusable
when switching or disabling and retire with DisplayResolve. Resize stages every
requested target before publishing; temporary old/new allocation overlap is not
included in the cached-storage figure. Invalid settings, oversized requests and
allocation failure retain prior targets. Draw/device failure retains existing
presentation error semantics.

## Authoring, automation and export

Scene lighting exposes Enable, Edge threshold, Contour search steps, Corner rounding
and Reset. Existing partial-patch validation, revisions, Undo/Redo and Save apply.
The existing MCP tool `forge_scene_rendering_set` discovers the same fields; source
permissions, exact receipt replay and authored Play locks remain unchanged. No
parallel filter database, material program, lookup asset or new agent tool is added.
Source-free export carries saved Scene settings through the existing content owner.
Use a runtime kit built with this renderer. These controls stay outside installed
native SDK value/callback ABI and fingerprint boundaries.

## Scoped evidence

`hdr_bloom` checks defaults, bounds before narrowing, native invalid values,
conflicting methods, partial edits, unknown extensions, history, reopening,
reference inspection and discovery. `hdr_bloom_editor` and focused `morphological_aa_editor` drive actual controls at
six scale/layout combinations, including exact0.01/0.5 boundary entry and rejection of narrower out-of-range doubles, including atomic switching, rejection/recovery,
reset and Play locking. `mcp_morphological_aa_stdio` executes both framing formats
through native owners, including schemas, read-only denial, replay/revisions,
method conflicts/history and saving.

`morphological_aa_vulkan` compares forty neutral/chromatic/HDR/transposed edge
fixtures against independently integrated display coverage; it also checks low
contrast, corner controls, sharp boxes, thin/dot detail, flat/border/alpha behavior,
resize, budget/invalid-state retention, no history, actual Scene/Game, TAA coexistence
and unchanged HDR. `morphological_aa_ui_vulkan` requires identical subsequent native
UI text. Gallery168 tests actual animation and four exact source-hidden relocated
Game image pairs. The preceding fast approximate path has separate regression checks.
These software checks do not establish Windows/MSVC/FXC/D3D12, full-editor,
physical GPU/input/DPI, graphical EXE or performance acceptance. See the
[user workflow](../manual/editor/lighting.md) and
[capture record](images/readme/README.md).
