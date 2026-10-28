# 2026-10-28 — Sprite atlas source packing

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
asset registry, persisted recipe, SDK/ABI layout or identity migration. Trimming,
rotated frames, automatic repacking and atlas mip generation remain outside this
profile. This local source batch is separate from queued Windows/full Main/
physical validation. No numbered ZIP or remote promotion.
