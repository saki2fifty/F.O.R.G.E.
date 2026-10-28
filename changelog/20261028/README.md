# 2026-10-28 — Sprite atlas metadata and automatic exposure

## Automatic Game exposure

Default-off Scene exposure now meters composed HDR light with a bounded original
GPU histogram and percentile-clipped log mean. Authored gain limits, target and
asymmetric exponential rates share Scene Save/Undo, runtime extraction/export and
the existing automation/MCP command. Manual EV adds compensation; Scene View keeps
its personal manual preview. Host unscaled time and explicit view/resource reset
rules own scalar history. Existing display/color/SDR behavior remains authoritative.

Scene lighting provides enable, seven numeric controls and one-step reset, with
percentages displayed for normalized percentile values. Gallery96 uses an original
ordinary held light-intensity clip and editable Scene, requiring no C++ module.
The native GPU probe covers independent metering/adaptation equations, partial bins,
composition, reset boundaries and retained pixels; CPU/UI/MCP/runtime-package checks
cover authoring and delivery consumers. Portable lighting-control registration also
lets the same ImGui owner run outside the Windows Main test target. Windows/D3D12
and physical acceptance remain separate; HDR-monitor output is not implemented.

## Trimmed canvases and stored quarter turns

Simple Sprites support additive original-canvas dimensions, retained offsets and
clockwise stored turns. Old four-field rectangles retain defaults. World size,
pivot, bounds and picking retain the full logical rectangle; flips apply to both
the retained offset and image. Actual trim requires Blend with depth writes off.
Whole-bank material/value admission preserves the last usable draw on invalid
numeric edits or replacements. Existing nine-row instances and Tile Map behavior
are preserved.

Atlas generation adds default-off **Trim transparent edges** and **Allow stored
quarter turns** options through the same SourceImport/native/MCP operation. Exact
retained RGBA pixels, one available transparent filter ring, deterministic empty
canvas handling, ordered layoutv2 metadata and bounded legacyv1 paste share the
existing owners. The512-frame profile has an explicit128KiB/8192-element envelope;
unrelated value, schema and native-layout limits remain unchanged. Gallery95
provides original editable comparisons and ordinary packaged Texture/Material assets.

The public C++ SpriteFrameRegion layout changes16→36 bytes. Rebuild modules with
the current matching SDK; ABI1 C tables stay unchanged, with no old exact-profile
binary compatibility claim. Scene/Prefab/history, live tuning, cold recovery and
the relocated installed Sprite sample use the same metadata. No dependency or
identity migration is introduced. Sliced/Tiled metadata transforms, automatic
repacking and atlas-aware mip generation remain outside this first profile.

## Automatic atlas source packing

Content **Create / Register → Pack sprite atlas...** combines published independent
sRGB image textures into a reviewed normal PNG source. Deterministic bounded
packing preserves input frame order, exact RGBA pixels and optional edge padding.
The existing worker, source-copy/import/publication, Material, Sprite frame values
and Scene/prefab owners remain authoritative. MCP prepares through the same owner
and reuses import review/commit/cancel/close; results return the published Texture ID.

Native copy/paste checks the assigned image and applies the complete frame bank,
Frame0 and crop-off in one Scene Undo/Redo step. A Sprite without a Material can
show its controls safely. Original Gallery94 compares independent source images
with their packed frames. The illustrated manual explains the complete workflow.

Generation uses the existing4096-side/64MiB pixel PNG profile, no new dependency,
asset registry, persisted recipe or identity migration. Automatic repacking and
atlas mip generation remain outside this profile. This local source batch is
separate from queued Windows/full Main/
physical validation. No numbered ZIP or remote promotion.
