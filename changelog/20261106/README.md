# 2026-11-06

## Runtime reflection capture

Reflection Probes can capture view-local GPU lighting during Play and in Scene
preview. Up to two runtime probes share the eight-probe profile. Complete six-face
HDR captures use the existing source renderer, GPU leases, physical budgets,
convolution and reflection/diffuse selection. Saved maps provide fallback. Source
layers reject camera-output feedback; camera outputs borrow completed parent maps.
Simulation-time intervals hold while paused, and session/configuration changes
invalidate stale captures. No runtime source files or Texture asset IDs are created.

Inspector, live tuning, authored Scene/Prefab values and discovered automation/MCP
fields share validation, history and persistence. The appended native value requires
an exact matching SDK rebuild; ABI1 function tables are unchanged. Runtime exports
require a matching capture-capable kit only when authored capture is enabled.
Gallery176 provides original rotating sources and a saved fallback. The illustrated
lighting guide covers controls, source masks, refresh timing, saving/export and
limits. Windows/full-editor and physical verification remain pending.
