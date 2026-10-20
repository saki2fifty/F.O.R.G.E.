# Tile Map foundation — development source

Tile Sets define named cells of ordinary image materials. Sparse Tile Maps store
layered cells without an entity per cell. The central document editors provide
palette selection, paint/erase, pan/zoom, bounded complete-stroke Undo/Redo,
cancellation, Inspector settings and Save/Publish. Content placement creates an
ordinary transformed Tile Map Renderer. Original Gallery74 compares a course and
a mirrored instance of the same map.

Native runtime instances own independent cell state and immutable presentation
views. Matching C++ SDK modules can query cells and queue bounded fixed-step
changes. Stop discards those changes. Both legacy and shared-SDK Editor Play hosts
transfer frozen pages and structural deltas through the existing correlated pipe.
The receiver admits complete immutable frames; runtime views never substitute
authored cells when their data is missing. Candidate manifests enter the existing
8 MiB freeze; process responses retain the existing 16 MiB limit.

Focused Linux checks cover source identity/history, actual worker publication,
resources, native C++ cells, central editor input at three scales, native pixels
and grid selection, maximum maps, frozen copy/shared accounting, real Play pipes,
source replacement and Stop/restart. Scoped own-code sanitizer/leak checks pass.
Gallery74 source edits and its five-asset cooked closure work after source removal
and relocation. Software-Vulkan readbacks at two resolutions verify its actual
layered markers and mirrored copy. The README includes an unmodified native Game
capture.

The fresh installed shared SDK passes an installed-only module/helper compile
and relocated native fixture. The coherent Windows/editor/production-game gates
remain in progress. No physical graphics-card performance claim is made. This profile
does not add autotiling, animated tiles, 2D collision or streaming. Use matching
engine/SDK/module builds. Delivered Build261020-000194 predates this work.
