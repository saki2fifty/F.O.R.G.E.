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
