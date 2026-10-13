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

Exactly one Surface Output defines base color/alpha, metallic, roughness, normal,
emission and occlusion. Graph values feed the existing metallic/roughness PBR
geometry/lighting helpers; this is not a copied BRDF or custom vertex deformation.
Physical surface fields are bounded at their established shader boundary; output
nonfinite checks provide the existing diagnostic color instead of nonfinite pixels.
UV numbers remain logical semantics, projected to compact varyings. A graph with no
planar-coordinate consumer does not require mesh UV channels; cube/volume coordinates
do not create an artificial UV0 requirement. An unconnected Normal Map UV input
explicitly requires UV0. Texture2D
implicit sampling respects Material UV transforms and overrides.

Extract Function turns a selected pure calculation with one outward result into
a reusable typed function inside the same Shader document. Parameters, textures
and Surface Output stay outside functions. Calls share pure code, not mutable
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
