# 2026-10-28 — Sprite atlas packing and frame metadata

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
