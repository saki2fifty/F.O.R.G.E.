# 2026-11-05

## Runtime UI linear gradients

Runtime UI supports bounded horizontal, vertical, linear and repeating-linear
gradients through the existing source, presenter and rendering owners. Linear
profiles accept two through sixteen stops, alpha, model-driven angle changes and
native keyframes. Original Gallery164 demonstrates the workflow; the manual
covers source syntax, live tuning, saving, reload, clipping and resource limits.
Main and camera-image UI use the same path. Runtime kits advertise gradient
profile1 and reject incompatible exports before replacing a preceding destination.
Automation and MCP use their existing source/model workflows. No dependency pin,
installed native SDK ABI, scene schema or asset identity changes are introduced.
Windows/full-editor and physical acceptance remain pending.

## Runtime UI radial and conic gradients

The existing bounded gradient path also supports radial/elliptical and conic
colors, with repeating variants, shifted centers, alpha, live values and native
keyframes. Original Gallery165 and the illustrated manual cover the workflow.
Additive gradient profile2 preserves static linear1 and requires2 for the new
types and dynamic decorator bindings. Affected metadata refreshes through the
existing source owner without changing asset identity. Shared buffers, stop,
resource, clipping, package and MCP owners remain authoritative. No dependency,
installed SDK, C ABI or scene-schema change is introduced. Windows/full-editor
and physical acceptance remain pending.

## Runtime UI alpha masks

Runtime UI supports bounded gradient/image alpha masks through existing layer,
source, candidate, model/MCP and package owners. The shared scratch pool retains
copied snapshots for compiled-filter lifetime and rejects stale-frame use. Native
preparation remains non-drawing. Additive filter profile4 preserves prior1..3 and
rejects masks on older kits before destination replacement. Original Gallery166
and the illustrated manual cover live tuning, alpha semantics and shared limits.
No dependency, installed SDK, scene-schema or asset-identity change is introduced.
Windows/full-editor and physical acceptance remain pending.

## Runtime UI box shadows

Runtime UI supports bounded box shadows with offsets, spread, inset, rounded
corners and live model-bound blur through existing source, renderer and package
owners. Cropped snapshots share the existing bounded layer pool and native cache
lifetimes. Non-drawing preparation defers capture until the first actual render.
Filter profile5 preserves prior profiles; older kits reject required5 before
export replacement. Original Gallery167 and the illustrated manual demonstrate
the workflow and viewport/resource limits. Native box-shadow keyframes and
transitions remain unavailable. No dependency, installed SDK ABI, scene-schema
or asset-identity change is introduced. Windows/full-editor/physical review is
pending.

## Morphological anti-aliasing

Scene and Game display support an independently authored bounded 1x morphological
filter through existing DisplayResolve and Scene owners. Contrast-adapted edges,
analytic contour areas, sharp/fine-detail protection and connected diagonals use
no supplied shader/code/lookup assets. Scene lighting, shared authoring/MCP schema,
atomic method switching, history/save and Gallery168 use existing integrations.
UI follows filtering; linear HDR and temporal history retain their paths. Default
disabled output remains unchanged. Resource/current-image limits and authentic
controls/gallery captures are documented. Windows/full-editor/physical review is
pending; reference shader parity and multisample/temporal variants are not claimed.
