# Spatial depth of field

`rendering.depth_of_field` extends the existing Scene rendering version 1 object:
`enabled=false`, `focus_distance=10`, `focus_range=5`, `blur_radius=8` by default.
Distance and range are finite 0.001–1000000 metres; radius is finite 0–12 output
pixels. Bounds apply while disabled and before conversion from JSON double to
float. The existing nested Scene patch, validation, revisions, history, saving,
automation discovery and MCP binding own these fields. Unknown extension fields
remain opaque and reference inspection reports them as uninspected. No new
component, asset identity, simulation clock or authoritative settings store exists.

## Owners and composition

DepthOfField owns one view's disposable spatial scratch pair through the existing
presentation shader/pipeline/device interfaces. FrameRenderer extends its existing
per-camera derived state with a focus owner. Scene Viewport retains its independent
navigation view through the same pass. Settings apply separately to each view,
measuring positive view-space depth rather than radial distance.

MeshSceneRenderer owns queue extraction, culling, sorting, batching and the eligible
opaque/masked ordinary Mesh prefix. A borrowed PostMeshPass runs after optional
temporal resolve/motion blur and before current-frame layers; the renderer restores the camera
HDR/depth targets and rectangle even when optional work fails. It never reorders
draws to create a prefix. Material part metadata derives depth-test/write admission
from the existing prepared Material owner. Unsupported ordering or incomplete depth
produces an actionable whole-view warning before changing targets. Geometry errors
prevent adoption of the optional effect. No fabricated motion or depth input is
provided to another renderer or effect context.

Morphing and skinned Mesh geometry use their current rendered depth. Transparency,
transmission, Sprite/Tile, particles, sky and overlays retain current-frame behavior.
Both Game camera clears are required. Bloom follows camera geometry and its current
layers; exposure and display processing retain their existing owners. The spatial
pass adds no accumulation or readiness delay. Combined explicit temporal captures
still use the existing sample key and eight-frame readiness.

## Sampling and bounds

Input color is complete single-sample, single-mip 2D RGBA16F; depth is matching D32F
with a complete R32_FLOAT SRV. Existing frame/view depth targets request sampling
only when the native format reports support. Otherwise ordinary rendering remains
available and focus admission reports missing sampled depth. Camera projection
coefficients must describe finite positive forward near depth and increasing
depth, with finite or infinite far distance. Orthographic and perspective views
share algebraic depth reconstruction. Nonfinite or invalid reconstructed samples,
and clear depth 1, retain current color.

Each pixel's radius is `blur_radius * min(abs(z-focus_distance)/focus_range,1)`.
Radii below half a pixel retain the exact current sample. The original fixed disk
uses 29 integer-offset taps at most, rounded to pixels. Out-of-view, clear-depth,
opposite-focus-side and depth-inconsistent neighbours are excluded. The depth
tolerance is the larger of 0.01 m and 2 percent of the current positive depth. Accepted
linear HDR RGB values average equally; alpha always comes from the current center.
This preserves depth silhouettes but deliberately does not spread foreground blur
over another surface. It is not a physical lens, aperture model, translucent depth
reconstruction or temporal bokeh algorithm.

A camera-rectangle color copy initializes the private snapshot. Filtering writes
a distinct HDR target, then copies that rectangle back to composed HDR. No depth
subrectangle copy, cross-camera read or sampling/render-target alias is needed.
Changing textures use DYNAMIC bindings; the stable 48-byte uniform uses its existing
binding. Preparation validates formats, complete views, dimensions, projection,
thread, private-target alias and payload before allocation or source writes.
New target pairs stage before adoption. Frame resized owners also stage through
successful preparation; unapplied views release obsolete scratch.

The pair costs 16 texel bytes/pixel. Up to eight Game cameras share a 128 MiB current
scratch-payload estimate; Scene preview has its own 128 MiB estimate. Staged old/new
pairs, retired native resources, pipelines, driver allocations and other passes
are additional. These estimates do not establish physical GPU memory usage or
performance. Disabled/zero-radius paths do not create focus targets. A static
Scene retains its completed image; focus filtering introduces no clock or repeated
EDIT rendering.

## Validation boundaries

FORGE authors this pass independently through the approved generic Diligent API.
The pinned native depth-of-field unit requires PostFXContext readiness and couples
ImGui UI into that unit; it is not enabled, copied or patched. Dependency pins and
installed native SDK boundaries are unchanged. Existing custom color/depth surface
capabilities suffice; this spatial pass requires no new shader wrapper or cook.

Focused tests cover numeric settings/ownership, independent HDR/depth pixel
references, Scene/Game integration, camera rectangles and ordinary fallback,
current-frame layers, combined temporal capture, UI/MCP and original Gallery103.
Local software Vulkan and standalone UI fixtures are distinct from full Windows
Main/MSVC, FXC/D3D12, physical input/GPU and graphical exported-game acceptance.
The native stack's [open shutdown allocation report](dependency-known-issues.md#native-software-vulkan-shutdown-allocation-report)
remains unsuppressed; a functional rendering result is not a clean LSan result.
