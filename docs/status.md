# Implementation status

## Verified on Linux and Windows
- C++20 core and C17 sample module build.
- Scene save/load, unknown data preservation, invalid-document rejection, undo/redo.
- Flecs reflection metadata and per-instance prefab position overrides.
- Runtime JSON-lines protocol, module invocation and rejection of missing modules.
- Generated C++ project compilation, failed-build retention, compatible code replacement, schema-change restart, crash-probe rejection, killed-runtime recovery.
- Plugin checksum checks, staged updates, startup ordering API, restart-bound disable and safe mode.

## Windows compilation verified; interactive GPU verification pending
SDL3/D3D12 editor; pinned ImGui docking; initial workspace; reflected Position Inspector; atomic Save; undo/redo; persistent Tooltips toggle; Diligent offscreen cube preview.
The preview renderer has passed a Linux C++ syntax check against the pinned Diligent headers. Windows compilation and linking passed in [run 34942452023](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34942452023). Real GPU execution, DPI and docking behavior still require a Windows desktop check.

## Not implemented
- Editor connection to the separate runtime, play controls, IPC viewport frame transport and build-output panels.
- Arbitrary gameplay component/system/observer registration, general reflected schema migration and lifecycle-aware DLL retirement. Current ABI only supports stateless callbacks over host-owned Position data.
- Actual native editor-plugin loading, registration APIs, automatic startup-crash recovery and package UI.
- Asset browser/import/caching/cooking; project wizard; autosave recovery; gizmos/snapping; complete prefab overrides; multi-edit.
- PBR game rendering, Jolt, audio, animation, navigation, game UI and standalone playable export.
- First-person reference game, advanced 3D tools, Linux graphics, dedicated 2D editing and multiplayer.

## Known foundation constraints
Scene edits reconstruct the authoring world and retain at most 100 undo snapshots. This prioritizes transactional correctness over large-scene performance. Module tick callbacks must be stateless and must not retain host pointers, create unmanaged threads, or register external callbacks. Native iteration currently pauses stepping while building. IPC is local JSON lines over inherited pipes, not the planned named-pipe/frame-transport service. Plugin packages are trusted local inputs; metadata validation is not executable safety validation.

## Windows downloads
The Windows CI job uploads `FORGE-Windows-x64.zip` containing Release executables, adjacent DLLs and dependency notices. Run 34942452023 produced the first successful Release package, from commit `0b4043142c24d0d237ecb891f0c81f072840a6ad`. Extract the entire ZIP on Windows; the editor executable depends on the packaged DLLs. Native D3D12 compilation requires Microsoft ATL, which is unavailable in the current Linux MinGW toolchain.

The Diligent Release defaults enable AVX2 CPU instructions. This development build requires an AVX2-capable x64 processor and a D3D12-capable graphics driver. Executable and DLL import tables were checked: the package does not require separate MSVC runtime DLLs.
