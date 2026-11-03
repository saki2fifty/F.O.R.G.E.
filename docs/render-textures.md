# Scene camera render textures

A `RenderTextureAsset` is a logical saved camera-output configuration. It is
separate from `TextureAsset`, whose selected revision owns immutable image data.
`Camera.render_texture` is an optional typed reference; nil selects the main
presentation. The Scene's native Camera component remains the output authority.
See the [user workflow](../manual/editor/render-textures.md).

## Source, publication and compatibility

`forge.render-texture` version1 uses `.rendertexture.json`, a UUID, integer width
and height in1..2048, and the fixed `linear-hdr-opaque` profile. Sources are bounded
to16KiB. Bounded unknown extensions survive editing and copying. The existing
AssetSourceDocument, single-source import/publication, selected-artifact admission,
catalog and resource pool own saving, history, immutable configuration revisions,
validation and provenance. Duplicate creates a new configuration UUID; paths are
locators. No rendered pixels or device handles enter source, catalog or cooked data.

Material source3 and bundle3 explicitly add a texture-role binding:

```json
{"render_texture":"<published output UUID>","slot":{"semantic":0,"dimension":0}}
```

The shown slot is abbreviated; use the existing full Material slot serialization
or MCP catalog's `binding_template`, not a partial hand-built descriptor. Each
role owns exactly one static Texture or render texture. Both paths use ordinary
Material-owned UV, sampler, slot and Shader-interface validation. Only2D Color
slots accept the new binding. The in-memory maps are disjoint and the decoded
bundle rejects conflicting roles. Material inheritance, explicit null and static
replacement follow the existing source semantics.

Sources1/2 retain an unknown `render_texture` member as an opaque extension.
Explicit `enable_render_textures()` promotion rejects a discriminator collision
without modifying the old source. Choosing a supported output in the Material
editor promotes deliberately; selecting a surface Shader never downgrades source3.
Old static Materials and source-Shader bundles retain their supported formats.

A selected Material records a `material.render_texture:<role>` dependency with the
configuration's exact revision. After resizing/publishing an output, republish its
dependent Materials before export. Existing closure validation rejects stale
revision edges; it never silently changes the selected dependency or UUID.

## Frame ordering and native lifetime

Each view derives a pass plan from enabled Camera outputs and the actual selected
mesh Material candidates. There is no second persisted frame graph or image
registry. Reads are conservative for visible objects in the camera layer mask;
they do not depend on current frustum occlusion. Separate source and screen layers
are therefore required even when a feedback surface is offscreen.

Cameras sharing an output use `prepare_game_cameras` for that output's dimensions,
ascending Order then EntityId, normalized viewport, aspect fitting, layers and
color/depth clears. Main composition selects nil outputs. Camera Shot may direct
a main Camera; target producers run independently. Topological ordering renders
every producer before its consumers. Self/cross-output cycles, missing enabled
writers, absent configurations and invalid projections reject the affected plan.

The profile admits at most4 outputs,16 producer Cameras and4,194,304 configured
pixels. Each image is single-layer RGBA16_FLOAT, single-sample, opaque alpha1 and
one level. The child FrameRenderer uses existing scene lighting/effects and linear
HDR output. Alpha completion reuses the camera-clear pass; it does not apply
consumer exposure, tone mapping or display encoding. UI/grid/editor overlays are
outside the producer image.

MeshResourceHost owns one shared immutable CPU pool. Its read-only configuration
facade also supplies editor frustum guides; published resize refreshes guides even
without a Scene edit. Unavailable configuration hides the frustum rather than
inventing main-output dimensions. Fixed Camera aspect remains authoritative.

CameraRenderTextures owns view-local mutable realizations through existing
GpuResidency/leases. The physical image budget is64MiB and8 revisions, including
retained/in-flight output leases; selected configured image payload is at most
32MiB. FrameRenderer color/depth/effects targets remain separate existing owners;
these bounds are not a total VRAM budget. Draw bundles retain native leases beyond
their shader resource bindings. Unbinding precedes image copies, and real submitted
GPU fences govern retirement. No guessed frame delay, new simulation clock or
private copy of the resource manager is introduced.

Only a completed producer in the current frame authorizes sampling. Retaining an
old native view does not mark its pixels current. Strict CPU Material readiness
precedes planning, so asynchronous admission cannot introduce an unplanned image
read halfway through a frame. Scene and Game share CPU configurations and keep
independent physical images. Source/configuration loading and pass diagnostics
participate in ordinary presentation readiness and Problems.

## Consumers and supported access

MeshRenderer Materials accept render textures, including eligible selected custom
surface slots. Static TextureAsset remains the type for Sprite/Tile Map/particle/
Decal, environment and light-image workflows. These paths reject unsupported
dynamic inputs. An isolated Material preview has no Scene producer; inspect the
Material on an actual Scene/Game Mesh. Producer lighting is authored; the Scene
preview-light option affects only the navigation draw.

Content exposes the Render Texture document with Width/Height, one Apply dimensions
transaction, Cancel, Save / Publish and source Undo/Redo. GUI gestures block MCP
mutations through the same document owner. Camera Inspector, Scene history, Prefab
inheritance/override/Revert and saved references use existing native reflection.

`forge_asset_render_texture_catalog` describes format, limits, component field and
an actual full2D Color binding template with a null UUID placeholder. Replace null
with a published UUID before a Material patch. Ordinary source tools use editor
`render_texture`; Camera and Mesh assignments use the shared reflected authoring
operations. Read-only asset search/read methods are composed independently of
import mutation controllers and retain their existing project/ownership checks.

The installed C++ SDK exposes the appended Camera reference through existing
value-only headers. Exact contract hashing requires rebuilding project modules
against the matching SDK; ABI1 function tables and logical identities are unchanged.
GPU handles, renderer pools and RenderTextureSource authoring are not SDK services.
Native C++ may change the Camera's reference through the ordinary component.

Runtime closure includes saved configuration and Material edges. The runtime
recreates outputs after loading the Scene. Package relocation and source-hidden
rendering use normal catalog/resource owners, without authoring sources.

## Validation scope

Focused tests cover source/bundle compatibility, bounds, extension preservation,
explicit promotion, pure routing, native Camera history/Prefab controls, UI gestures,
MCP publication and assignment, ordinary/installed SDK values, and Gallery152.
Native software-Vulkan checks exercise actual linear color propagation, opaque
alpha, live values, resize, ordered shared-output viewports, output chains/cycles,
missing-writer recovery, independent views, Scene View and a relocated saved Scene
with its original source tree unavailable. These tests do not establish Windows,
physical GPU performance or desktop acceptance. See the current delivery gate and
pending Windows review before making platform acceptance claims.
