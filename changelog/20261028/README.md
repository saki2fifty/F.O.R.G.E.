# 2026-10-28 — Animation, rendering and Scene controls

## C++ component field animation

Timeline clips bind up to64 editable scalar component fields by stable component,
field, optional nested member paths, collection selectors and inspected schema admission. Floats/doubles interpolate; Boolean, integer,
enum and bitmask values hold without floating conversion. Linear/cubic scalar values
and exact discrete keys use the existing
clip/player/sequence/history owners. Source9/cooked9 and importer recipev12 preserve
legacy readers and reject promotion collisions. Inspector/native updates now stage
from a native copy, recursively write differing members and retain unreflected
padding state and owned values. Nested projection reuses reflected extension
merging so opaque siblings survive in runtime and copied sequence previews. Copied
sequence preview uses engine-owned metadata without executing project callbacks.
MCP edits the same validated source document with ordinary permissions/revisions.
The native update semantics change the exact SDK fingerprint; rebuild project
modules with the matching SDK. Native C ABI signatures and component IDs are unchanged.

ComponentAnimation supplies the editable spinning-cube C++ project and selected
clip. Its canonical code compiles through the installed SDK, runs in both host
modes, matches independent speed/rotation equations and recovers without authoring
sources. Its Enabled and Direction keys pause/reverse the spin through the existing
post-animation phase. The example exposes these fields inside Settings, with an
explicit schema4/sample migration preserving all asset/entity/key identities. Its
array X/Y tracks change the local rotation axis. Fixed arrays and admitted native
Meta vectors use positional or declared local-key selectors, preserving keyed
targets after reordering. Complete native vector edits shrink/clear/grow through
their existing lifecycle owner. The
manual adds authentic indexed Timeline control captures. UI/DOM property bindings
remain pending; arbitrary project opaque containers are not admitted. Windows/full editor, physical GPU
and graphical export acceptance remain separate; no new numbered package is issued.

## Independent Sprite frame filtering

Sprite and Tile Map Renderer add default-off mip filtering with Nearest and Linear
level selection. Private frame chains restore logical canvases before filtering,
including crop, trim and clockwise storage. Complete asynchronous banks use the
existing texture pool and GPU residency; source/layout failures retain the last
complete draw. Shared frame views preserve consecutive instancing, with separate
draws for distinct views. Scene/Prefab history, reflected controls and discovery,
Sprite live edits and the matching installed C++ component layout share their
existing owners. Public component fields change the SDK fingerprint; rebuild
modules against the matching SDK. C ABI signatures and asset IDs remain unchanged.

Gallery99 compares the three modes using one original image sheet and ordinary
Material. The manual includes authentic control and Game captures. Local software
Vulkan, authoring, Inspector, MCP and installed SDK checks remain separate from
pending Windows/full editor/physical acceptance.

## Scene distance fog

Default-off radial geometry fog shares Scene validation/history, Scene lighting
controls, MCP and existing Mesh-based rendering. One shader helper applies linear
HDR fog before alpha blend/display while preserving mask and diagnostic behavior.
A 32-byte private buffer retains Object/instance layouts. Current cooked wrappers
record engine version 2 independently of optimized reflection; legacy version 1 remains
usable without fog and requires Shader/Material rebuild for fog. Gallery98 reuses
existing resources and preserves prior catalog records/IDs. Windows and physical
acceptance remain separate.

## Packed Sprite fills

Trimmed and clockwise-stored frames now work in existing Sliced and Tiled modes.
Fill/borders/density use the original logical canvas before retained-pixel/storage
mapping, including complete-fill flips, compressed borders and partial tiles.
Whole-bank logical border validation and Blend/no-depth-write trim admission retain
the existing last-good candidate rules. Simple keeps its prior mapping arithmetic.
One explicit private Sprite crop row changes its GPU stride144→160 bytes; ordinary
Mesh remains7rows and installed value/C ABI/fingerprint/asset IDs stay unchanged.

Actual controls, Scene/Prefab history/save/reopen, MCP, installed typed C++ writes,
both hosts/live512 and source-free Gallery97 use the same existing owners. The
gallery reuses original/packed assets without new cooks. GPU checks compare original
and packed pixels across modes, turns, trims, filters, flips, compressed borders,
partial tiles and mixed two-instance data. Windows/physical acceptance remains a
separate gate; no automatic repacking or atlas-aware mip generation was added.

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
