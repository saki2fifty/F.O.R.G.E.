# Tile Map foundation — validated source

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

Windows core 169/169 and SDK 202/202 pass. The shared runtime gate passes an
installed-only module/helper compile, normal Gallery74 export and two production
starts after removing source and kits. The same manifest passes two further
starts on a separate runner without checkout or SDK. The full Windows editor
audit passes 301/301, standalone-only 2/2 and 12 matched process samples. Actual
Tile Map document/Inspector captures were reviewed at 100/150/200 percent; the
README includes a complete Windows authoring capture.
No physical graphics-card performance claim is made. This profile
does not add autotiling, animated tiles, 2D collision or streaming. Use matching
engine/SDK/module builds. Delivered Build261020-000194 predates this work.

Windows asset publication also retains open readers across atomic metadata
replacement. Native checks verify the old reader sees the old bytes, new readers
see the new bytes and the existing interruption-recovery checks still pass.
Unsupported Windows filesystem/API capabilities retain the prior replacement
path. Asset identities, formats and publication ownership remain unchanged.

Final gates use compiled source `0b9397cf1852fa1db71f4ddcc9040fdc2984365f`:
[Windows core/SDK](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39519967945),
[full editor](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39519971030) and
[shared export/fresh runner](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/39519973866).
The final reconciliation changes documentation and one genuine screenshot only.

The same-runner comparison records responsive-window medians of 308.41/279.61 ms
for the accepted older baseline's empty/cube scenes and 450.58/424.69 ms for this
source. Median per-process settled working sets are 62.75/61.46 MiB versus
71.32/74.56 MiB. Each revision/workload has three processes with five idle samples.
These measurements include hosted software rendering and uncontrolled OS caches;
they are neither first-GPU-frame timing nor physical GPU FPS, and establish no
speedup claim.
