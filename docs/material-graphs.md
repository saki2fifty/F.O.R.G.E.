# Material graphs

Phase10 extends the existing Shader asset, MaterialSource/MaterialDocument,
AssetSourceDocument history, DocumentWorkspace, bounded shader worker, AssetPublisher,
resource leases and renderer. It introduces no scene component, gameplay VM, public
plugin ABI or parallel asset/history owner.

Build 260929-000127 passed the Linux/Windows delivery gate. Supplemental native
Windows/D3D12 WARP checks exercised authoring, imported UV-less mesh assignment and
exported graph rendering after source removal and two installation relocations.
These checks do not establish physical GPU/DPI acceptance or Vulkan GPU execution.

## Authored contract

A `.shader.json` source version3 contains `format: forge.shader`, its persistent
Shader AssetId and a `forge.material.graph` version1 graph. The graph is the sole
program authority: authored HLSL stages, `surface` and `source_root` are forbidden
alongside it. Generated HLSL/interface data is an immutable build projection,
not a second editable source. Existing source versions1/2 remain supported.

Node, edge and function identities are separate UUIDv4 families. Ports use stable
schema keys, with scalar, vector2/3/4 and linear-color3/4 types. Equal widths do not
imply equal semantics: conversions are explicit. Nodes retain schema key/version,
position and configuration. Unknown schemas/versions and extension payload survive
source history/save/copy; compilation refuses unsupported content. Copy remaps known
node/edge IDs and duplicated parameter/texture binding keys; opaque references are
not guessed or rewritten. Function definitions retain their stable identities and
conflicting definitions cannot silently replace one another. Whole Shader asset duplication uses the existing asset-file transaction owner and generates new node, edge and function IDs, remapping known internal links while retaining opaque payloads and the same parameter binding contract.

A parameter or texture has a persistent binding key independent of its display
label. MaterialSource stores values/references using that key, so renaming a label
does not retarget material instances or change the GPU layout digest. Material
variations use existing sparse base/override inheritance and independent Revert.

## Nodes and functions

The UI-independent schema/projection layer provides constants, parameters, texture
sampling (2D, 2D array, cube, cube array and3D), UVs, camera-relative positions,
normals, vertex colors, normal maps, typed conversions, channel composition/extraction,
arithmetic, mix, clamp-to-unit, trigonometry, absolute/fraction/floor/ceiling/round,
thresholds, smooth thresholds, dot/cross and
safe normalization. Divide selects zero for a zero denominator per lane, rather than multiplying a possibly overflowed result by zero.

Step, Smoothstep, Ceiling and Round accept all six existing scalar/vector/color
types, with exact semantic types on each port. Step returns0 below its threshold
and1 at or above it. Smoothstep defaults to low0, high1, value0: an ordered range
smoothly rises from0 to1, a reversed range falls from1 to0, and equal endpoints
act as Step. Each lane selects a nonzero normalization divisor before calling the
standard HLSL smoothstep intrinsic on0–1, including eagerly evaluated branches.
Ceiling rounds toward positive infinity; Round chooses the nearest integer with
halfway ties to even. Unconnected Step ports default to0; its result is therefore1.
Ceiling and Round default to0. These are floating values, not integer port types.
They work through existing copy/paste, function expansion and source history.
Local DXC probes validate generated numeric results and color/depth compilation;
new-node FXC5.1/Windows and physical GPU acceptance remain pending.

Length and Distance accept any of the six scalar/vector/color input types and
always output Scalar. Length returns Euclidean magnitude (absolute value for a
scalar); Distance returns the magnitude of A minus B. Color inputs use every
channel, including alpha for Color4; these nodes do not calculate luminance.
Factory inputs default to Vector3, and unconnected inputs default to zero.

Reflect and Refract use fixed Vector3 Incident and Normal ports. Refract also takes
a Scalar Ratio: the incident medium's refractive index divided by the transmitted
medium's index. Defaults are Incident(0,0,-1), Normal(0,0,1) and Ratio1. Their HLSL
intrinsics do not normalize inputs: use unit directions and normals for physical
ray calculations. Refract returns zero under total internal reflection. They
calculate directions for graph composition; they do not add a transparency pass.
All four nodes use the existing typed links, source history, copy and function
expansion. Local source/editor and actual DXC probes cover their output; native
Windows/FXC5.1 and physical GPU acceptance remain pending.

Exactly one output chooses the domain. Surface Output defines base color/alpha, metallic, roughness, normal,
emission, occlusion and optional imported lightmap irradiance. Graph values feed the existing metallic/roughness PBR
geometry/lighting helpers; this is not a copied BRDF or custom vertex deformation.
The wrapper specializes its fixed engine lighting defaults as shader constants,
so unused lighting layers can be removed during compilation. Authored graph
outputs do not expose clearcoat or iridescence, so graph wrappers omit those
fixed-zero layers before native lighting helper expansion. Authored graph
parameters keep their reflected Material uniforms and remain adjustable without
recompiling the Shader. Built-in Materials retain their uniform parameter path.
Physical surface fields are bounded at their established shader boundary; output
nonfinite checks provide the existing diagnostic color instead of nonfinite pixels.
Imported irradiance is checked by the shared lightmap helper after lighting setup,
including negative/nonfinite input and finite-contribution overflow. Rejected
irradiance produces diagnostic color and zero lightmap radiance in indirect output.
These bounds use the shared binary32 classification helpers, preserving NaN/Inf
rejection under native finite-math optimization. Invalid operands are replaced
before diffuse multiplication; the helper assigns its output once on every path.
UV numbers remain logical semantics, projected to compact varyings. A graph with no
planar-coordinate consumer does not require mesh UV channels; cube/volume coordinates
do not create an artificial UV0 requirement. An unconnected Normal Map UV input
explicitly requires UV0. Texture2D
implicit sampling respects Material UV transforms and overrides.

The optional `lightmap_irradiance` port is linear `color3`, default zero. HDR
texture nodes return `color4` and use an explicit Convert before this port.
See [imported diffuse lightmaps](imported-lightmaps.md) for units, composition,
numeric validation, existing Material bindings and runtime-kit admission.

Sprite Output instead emits a final linear RGBA function over `ForgeSpriteInput`.
Its `data.image` names a declared primary Color Texture2D binding by stable key;
`data.tint` defaults true and multiplies the result by `input.Color` once. The
default Sprite factory connects image and white Color through Multiply. Only
logical UV0 is available. Primary explicit-coordinate sampling carries the Sprite
input context through frame/fill/trim/turn/mip mapping; auxiliary texture helpers
retain ordinary Material UV/sampler behavior. World Normal, Normal Map and nonzero
UV sets reject across authored nodes and unused functions. Cube sampling requires
an explicit direction. A graph cannot mix domain outputs.

UI Output emits final linear RGBA over the existing ForgeUiInput. It selects the
same numeric definition4/wrapper17 or ordinary2D Texture definition5/wrapper18
interfaces as UI HLSL sources. UI Time and UI Size lower to the existing presenter
time and element-pixel fields; UV0 lowers to the element's coordinates. Typed
math/functions/parameters and ordinary Texture2D helpers remain the existing graph
implementation. Mesh geometry, Vertex Color, other UV sets and non2D Texture nodes
reject with located ui_domain diagnostics, across disconnected authored nodes and
uncalled function definitions. Mesh/Sprite graphs reject UI Time/Size. UI Output
cannot be extracted into a function or mixed with another domain output.

create_ui_material_graph and MaterialGraphSource::create_ui provide a numeric Tint
factory. The graph/source versions and identities remain unchanged; new schema
keys use ordinary version1 node discovery/opaque-future preservation. No generated
HLSL is stored as a second authority. Editor Domain UI and automation/MCP domain ui
use the same factory/document/history/publication path. Compilation still uses the
normal isolated worker and Shader/Material/runtime dependency owners. Geometry
preview remains for Mesh/Sprite; UI compilation reports its distinct status and
does not send UI Materials into that preview renderer. UI effects are viewed in
the assigned UI element through existing Source Preview/Play.

Copying a texture declaration creates a new binding key. A copied Sprite Output
remaps its primary-image reference only when that declaration was copied too;
opaque fields remain unchanged. Whole Shader duplication retains the binding
contract. Sprite previews use the shared renderer's Sprite Quad, with the original
Mesh preview geometry choices retained for Mesh materials.

Extract Function turns a selected pure calculation with one outward result into
a reusable typed function inside the same Shader document. Parameters, textures
and either output stay outside functions. Calls share pure code, not mutable
runtime state. Function inputs/output keep stable ports and cached signatures;
unsupported signature changes reject compilation rather than silently reconnecting
edges. The current editor supports extraction, call creation, interior editing and
rename; it does not expose arbitrary signature surgery. Expansion is deterministic,
acyclic and bounded; source errors map scratch nodes back to authored member IDs.

## Admission and publication

Persisted graph/source budget is1MiB, JSON nesting64, nodes1024/edges4096 per graph,
functions64, function expansion depth16, evaluation depth128. These are explicit
FORGE admission budgets, not graphics-device limits. Counts, types, duplicate IDs,
ports, single-input edges, cycles, finite values, binding conflicts and function
signatures are checked before projection. Device compilation/reflection and complete
GPU candidate admission still enforce actual target capabilities.

Preview compilation runs in the existing bounded worker after a300ms debounce.
Pure node-position edits in the surface or function bodies, including their
Undo/Redo, do not restart that preview worker. The editor compares admitted graph
drafts ignoring only declared node position fields. IDs, ordering, functions,
binding labels, properties and opaque data remain part of the comparison. Source
dirty state/hash and Save & Compile/publication still include layout; explicit
Refresh preview and material/texture preview changes still request preparation.
Generation/cancellation checks discard stale results. Preview uses an independent
MaterialPreview; it does not publish the asset or alter scene state. Preview texture
choices are temporary; durable choices belong in a MaterialSource. Previous usable
preview stays visible when new graph, worker or GPU preparation fails.

Save & Compile safely writes graph source, then requests existing Shader publication.
Source save and compiled publication are distinct: failed compilation leaves the
saved invalid source available for correction, marks unpublished work and retains
the previous usable immutable Shader/material revision. Dirty close offers save,
discard or continued editing; discard does not undo source already saved to disk.
Undo/Redo belongs to this source document, not scene history or asset-file publication.
Background Shader imports submit without opening import-settings UI and leave no
invisible dirty settings document after success or failure.

Project switching settles dirty sources through the workspace guard. Once the
active project changes, the graph owner closes the previous document, clears its
queued Save and cancels its preview worker. Pending opens retain their project
root as well as their source locator; a relative path is never resolved against a
different project. An explicit open in the new project waits for the cancelled
preview to drain, then loads through the normal document owner. Old results and
diagnostics cannot be adopted into the new graph. These are transient editor
requests; asset identity and the persisted graph format are unchanged.

## Graph selection and layout

Canvas selection/view/settings remain transient. Selection All/Clear/Invert and
rectangle replace/Ctrl-add/Alt-remove do not edit source/history. Rectangles
intersect node bodies and clip to the canvas; Escape, locking, changed source,
lost focus and canvas movement/resize/navigation cancel unfinished rectangles.
Node drags and port links also cancel on Escape, locking or changed source.

Arrange aligns selected rectangle edges/centers or spaces at least three centers
evenly on one axis. End centers remain exact; ties sort by node identity. Grid
placement snaps both top-left coordinates to the graph origin, spacing1–1024
authored units (default16), nearest with halfway ties away from zero. Layout works
inside functions and on preserved unavailable nodes. IDs/edges/opaque data and
unselected records remain; finite±1e6 source bounds and current budgets admit the
entire candidate before one source Undo. Aligned/no-op layouts add no history.
No automatic topology layout or new authored graph format is introduced.

## Backend and export contract

Surface interface metadata version2 opts into shared PBR and carries binding labels.
Version1 custom surfaces still return final linear RGBA and receive no implicit PBR.
Generated code uses Diligent named resources/shared HLSL helpers, not D3D12 handles,
register-space assumptions or backend synchronization. Logical sources remain neutral.
Current cooked Shader artifacts target D3D12/FXC5.1; SPIR-V compilation is a separate
Vulkan probe, not a shipped Vulkan renderer or portable bytecode claim. Metal/WebGPU
remain mapping targets subject to exact Diligent capabilities and future validation.

Materials embed the existing cooked Shader snapshot. Existing dependency closure,
resource loading, export manifests and relocation carry it to standalone games;
no graph editor, source compiler or runtime graph interpreter is required there.
See [surface shaders](surface-shaders.md), [materials](material-assets.md),
[backend matrix](render-backends.md) and the [user guide](../manual/editor/material-graphs.md).
