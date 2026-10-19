# 2026-10-19

## Scalable Sprite image fill — development, Windows/export validation pending

- Simple stretch, Sliced borders and Tiled whole-cell fill share the existing Sprite and Material path. Density uses pixels per meter; undersized borders compress together and flips mirror partial repeats.
- Inspector shows the relevant density/border fields. Source history, prefab intent and temporary live tuning retain their existing owners. Excessive repeat extents fail ordinary property validation.
- The existing nine-row Sprite instance layout carries numeric mapping data; modes add no material/resource keys or per-cell geometry. Ordinary Mesh lighting remains unchanged.
- Gallery73 uses original panel art to compare all modes, compressed borders and mirrored partial repeats. The installed SDK probe exercises real C++ writes, cold recovery and unknown-data preservation.

Local normal and affected-own-unit sanitizer checks cover actual mapped pixels,
resource failure/recovery, HDR Game rendering, Inspector input at three scales
and relocated SDK consumption. Software workloads with1000/4000Sprites retain
one bundle and16/63draw calls respectively. These are hosted software measurements,
not physical GPU FPS or a before/after speedup claim. Public C++ fields change the
exact SDK fingerprint and require matching module rebuilds; ABI1 is unchanged.
Build193 predates these modes and Scene73. Full Windows/export acceptance remains pending.

## Visual Sprite frame selection — Windows validation pending

- Image-sheet preview beside numeric Frame controls in Inspector, live tuning, prefab drafts and targeted Timeline key fields.
- Click a cell, or navigate a pending frame with the keyboard and confirm/cancel. The chooser shows image/cell dimensions and supports anchored wheel zoom, right-drag pan and Fit image.
- Existing Scene Undo, runtime submission, draft publication and Timeline Apply key retain ownership. Published Material/Texture resources and bounded cropped previews are reused; failed/stale replacements cannot select old frames.
- Gallery72 includes a short walkthrough of the four original arrow images. No new asset schema, SDK/runtime component, dependency, slicing or tilemap system.

Focused input/owner checks and actual software-Vulkan UI images pass normally and
with affected-own-unit ASan/UBSan/leak inspection; pinned/CPU libraries remain
normal. Six native chooser captures were visually reviewed. A dedicated Windows
fixture now covers real-host selection, pending confirmation, Undo/Redo and Cancel
at three scales. Its execution and full Windows shell acceptance are pending;
Build193 predates these visual controls.

## Orthographic Scene editing — Windows validation pending

- Scene View offers Perspective/Orthographic projection, visible height and six signed axis views. Orientation endpoints choose an orthographic view with a matching grid.
- World-zero XY/XZ/YZ grids share projection/depth with drawing, picking, overlays and tools. Pan, zoom, framing and retained images use the admitted view extent.
- Existing per-scene bookmarks preserve projection, height and grid plane; old bookmarks explicitly restore Perspective/XZ. Unknown metadata survives, and invalid updates preserve the previous file/view.
- Gallery71/72 ship useful front-view bookmarks separately from authored Sprites, animation and Game cameras. No SDK API, runtime component, dependency or authored Scene format changes.

Local math/input/bookmark and actual Content placement checks pass. Native wide/tall
axis/grid/depth/picking and perspective/orthographic Sprite cases pass normally and
with own-unit ASan/UBSan/leak inspection; pinned/native dependencies remain normal.
All24 normal/sanitizer render outputs match. Full actual Windows editor workflow
and visual acceptance remain pending. Build193 predates this batch.

## Standalone presentation display — delivered in Build193

- Optional `--show-fps` launch flag shows FPS, average frame time and the slowest frame in a small reporting window.
- The existing game UI renders the panel, including scenes without an authored HUD; it takes no gameplay input.
- Statistics use completed presentations and reset after minimizing. Simulation, VSync and device selection retain their existing settings.

Deterministic counter/launch checks pass normally and with ASan/UBSan and leak
inspection. The native RmlUi consumer covers literal text, no-canvas rendering,
input/focus, bounded toggles, candidate replacement and resource retirement.
Actual Vulkan pixels pass at seven sizes/densities, including changed text and exact
disabled-frame equivalence; representative captures were visually reviewed, including a tiny window at400% density.
The [Windows shared-game audit](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39186082370)
passed for source `cf5c8b1226b72549849c4f45d2dd792c10270342`: eleven focused
regressions, three reference checks, actual FPS/resize/fullscreen/input/minimize
captures, production startup with FPS enabled, source-free export and a fresh-runner
relaunch. Saved data survives a second move. Five native Windows captures were
reviewed. Both Windows/Linux core150/150 and Native SDK183/183 profiles passed at
the unchanged implementation source3501; cf5 only corrects the export test's
first-launch observation order, preserving its storage assertions.

This implementation is included in Build193. Physical performance testing remains
deferred. Software rendering verifies behavior; these counters do not establish
GPU duration or smooth frame pacing. The combined frame-export/FPS editor job
[passed 273/273 tests](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39195937866)
at source `6c4f09f568240d3958124ee8046a0b79b75d0ab8`, including actual Game statistics,
followed by standalone-only validation. Seven fresh FPS captures and nine audio
captures were reviewed. A [corrected comparison-only run](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39206311150)
also passed, reusing the audit's benchmark after correcting a baseline artifact
reference. Build193 is the latest delivered ZIP.

## World sprites and image-sheet animation — delivered in Build193

The current source adds Sprite entities with ordinary image Materials, sheet
grids and whole frames, meter sizes, top-left pivots, flips, linear RGBA tint,
layers and equal-depth ordering. Sprite creation, Material presets and grouped
Inspector fields use the existing editor authoring/history owners. Timeline
adds a step-valued Sprite frame property, including preview, playback, presentation
and invalid-target recovery. Exact SDK fingerprints change; existing C++ modules
must be rebuilt against the matching SDK. ABI1 callback tables are unchanged.

Original Gallery scenes71/72 demonstrate still images and four-frame animation.
Local checks cover actual editor input at three scales, native software-Vulkan
pixels/picking in wide and tall views, failed resource/pose replacement and
relocated cooked content after deleting the authored copy. Sprite numeric updates
reuse the admitted image extent and unchanged Quad pose; 10,000 compatible images
share one draw bundle and render in157 calls without raising resource budgets.
This is a local renderer workload observation, not physical GPU or game FPS proof.

The four-way Linux/Windows core 154/154 and Native SDK 187/187 matrix passed at
source `fbec063`. Windows editor 279/279 and standalone-only 2/2 checks pass
at source `a7df8bddd8285f378c62d1e0025b3b828b1513e3` in
[audit39267449500](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39267449500).
Native Windows captures show the four frames and expected transparency in wide
and tall views. The sequence-frame walkthrough now waits for the actual export
callback before advancing after a click, retaining its original assertions and
deadlines. The final benchmark comparison also completed; Build193 passes the
numbered source and assembled-package gates below.
Build192 predates this Sprite family. The initial image profile is
unlit RGBA8/sRGB with ordinary opaque/straight alpha; compressed/HDR/custom-surface
images, atlas mip generation, slicing/tiling, tilemaps and 2D physics are outside
this first family.

## Build261019-000193 delivery

- Compiled source: `fd62990357dec0591f1acc6d617670114256264b`.
- [Source run39277082312](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39277082312): all eight required compiled-source jobs passed. Its package precheck initially stopped before assembly. No failed package was delivered.
- [Assembly run39286996320](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39286996320): reused the same compiled source/build artifacts through the established assembly-only path. Strict provenance, combined-package relocation, source-free Gallery exports/startup and final editor/SDK acceptance passed.
- ZIP: `261019-000193-FORGE-Windows-x64.zip`,61,762,122 bytes. SHA256: `19b2ddb33d2abe1b81770ee8350fddcffad132d6d579a7bf92271cae3768eaba`.
- Complete CRC/manifest/build/source verification covers1556 files,72 x64 binaries and488 Gallery files/72 scenes. The final SDK receipt reports complete=true with the exact package build/source. Existing ZIP-only promotion replaces192; the preserved148 extraction remains byte-identical.

The full unnumbered native audit compiled `a7df8bd`; the final compiled candidate
changes only documentation from that source. Audit binaries retain their original
identity. Its benchmark measures process launch to a responsive named window and
settled CPU/RSS, not first GPU frame. Hosted WARP samples and uncontrolled OS/driver
caches do not establish a performance improvement or physical game smoothness.
Physical desktop/GPU/listening acceptance remains user-deferred.
