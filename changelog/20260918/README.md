# 2026-09-18

## Phase 6E — navigation foundation

- Official-source audit selected Recast Navigation1.6.0 at6dc1667f580357e8a2154c28b7867bea7e8ad3a7. Adopt only private Recast authoring and Detour runtime libraries; Crowd/TileCache/sample UI remain deferred.
- Extract bounded asset byte reading and SHA256 from animation for reuse. Preserve existing admission semantics and ABI1.
- Implement the authorized static primitive source bridge, single-tile admission/provenance and navigation service/agents. Integration and full validation completed in Build50 below.

## Navigation implementation

- Added exact pinned private Recast/Detour libraries, source geometry selection and six asset-owned build settings.
- Added a bounded navigation worker, strict FORGENAV/native-tile admission, query proof, immutable revisions and catalog-last publication. Failed/stale candidates preserve previous assets.
- Added reflected NavigationSurface/NavigationAgent components, world-scoped service, fixed-tick nonphysics movement, reconstructed routes, exact SDK callbacks and optional viewport overlays.
- Added Inspector and Content controls and the function-based Navigation user manual.
- Shared bounded bytes and fixed-command worker plumbing with animation, preserving its admission path.
- Build/query/agent/publication tests, full regressions, sanitizers and Windows delivery verification passed; see the Build50 delivery record below.

### Integration corrections and validation

- Preserve inherited NavigationAgent queries through Flecs' normal query path; maintain independent translation/rotation/scale ownership.
- Capture source geometry at writable tick boundaries so gameplay SDK queries never write derived transforms inside read-only systems.
- Recover full semantic agent destinations and local translation when authored prefab property masks hide direct gameplay edits. Restore only into an unpublished candidate world; failed recovery preserves the active runtime.
- Validate detail boundary edges before Detour closest-point calls; use bounded surface projection for traversal-edge rounding.
- Local final normal suites: static **27/27**, shared exact SDK **34/34**, portable editor **2/2**. Shared ASan/UBSan/LeakSanitizer **33/33** passed. Manual **3/3**, cache invalidation, C17 headers, formatting, workflow lint and runtime link separation passed. Static ASan/UBSan/LeakSanitizer **27/27** also passed. Clean Windows delivery follows.

### Windows binary-fixture correction

Build48 is withheld: the corruption/recovery test read a binary NavMesh through a text-mode stream, allowing Windows newline/EOF translation to alter its saved copy. Production admission correctly rejected the changed digest. The fixture now reads binary and asserts exact restored bytes. Linux core27/SDK34 passed; Windows navigation mesh/process and unrelated regressions passed while this recovery fixture failed. Build49 subsequently verified the corrected test.

### Packaged-worker CI path correction

Build49 passed Windows/Linux core27/SDK34, Windows editor controllers2 and navigation/runtime/WARP25, shaders and compiled build identities. Its new navigation relocation step launched from GitHub's default directory instead of the explicit Windows checkout, so the test script was not found. The step now uses the same checkout working directory as the adjacent package/converter checks. Build49 is withheld; no runtime source changes were needed.

## Phase6E verified delivery — Build 260918-000050

Source `2c81647`, verified [run 35302622445](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35302622445). Windows/Linux static **27/27** and shared SDK **34/34**, Windows editor/process **2/2** and remaining navigation/runtime/viewport **25/25**, four shaders, executable build IDs and both packaged worker relocation checks passed. All **29** existing viewport fixtures match Build47 byte-for-byte. Local static27/sharedSDK34/editor2, static sanitizer27/shared sanitizer33, manual3 and supporting checks passed.

Verified ZIP CRC, all manifest hashes, exact source/build identity, five x64 programs including navigation worker and official converter, **30** matching manual source/HTML pages, Recast/Ozz/Jolt/miniaudio notices and both SDK packages. ZIP SHA256: `293539515f4103f01085f48b81fc7c79e6152d4854e6b102c39ee2de75b27e0f`. Previous Build47 archived; package cleanup complete.

Manual acceptance: [Navigation](../../manual/editor/navigation.md) provides floor/obstacle generation, agent destination, Play/Pause/Step/Resume, save/reopen and prefab override/Revert. No C++ editing required. This is basic nonphysics following, with no dynamic obstacles or crowd avoidance. **Phase6E complete; STOP before Phase6F.**

Build50 found no compatible editor cache and rebuilt from scratch, then saved the verified cache for later compatible iterations.
