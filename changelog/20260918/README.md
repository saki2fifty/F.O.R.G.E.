# 2026-09-18

## Phase 6E — navigation foundation (in progress)

- Official-source audit selected Recast Navigation1.6.0 at6dc1667f580357e8a2154c28b7867bea7e8ad3a7. Adopt only private Recast authoring and Detour runtime libraries; Crowd/TileCache/sample UI remain deferred.
- Extract bounded asset byte reading and SHA256 from animation for reuse. Preserve existing admission semantics and ABI1.
- Implement the authorized static primitive source bridge, single-tile admission/provenance and navigation service/agents. Integration and full validation are in progress; no new build delivered yet.

## Navigation implementation (validation in progress)

- Added exact pinned private Recast/Detour libraries, source geometry selection and six asset-owned build settings.
- Added a bounded navigation worker, strict FORGENAV/native-tile admission, query proof, immutable revisions and catalog-last publication. Failed/stale candidates preserve previous assets.
- Added reflected NavigationSurface/NavigationAgent components, world-scoped service, fixed-tick nonphysics movement, reconstructed routes, exact SDK callbacks and optional viewport overlays.
- Added Inspector and Content controls and the function-based Navigation user manual.
- Shared bounded bytes and fixed-command worker plumbing with animation, preserving its admission path.
- Initial local build/query/agent/publication tests pass; full regression, sanitizer and Windows delivery verification are pending. No numbered build is delivered by this entry.

### Integration corrections and validation

- Preserve inherited NavigationAgent queries through Flecs' normal query path; maintain independent translation/rotation/scale ownership.
- Capture source geometry at writable tick boundaries so gameplay SDK queries never write derived transforms inside read-only systems.
- Recover full semantic agent destinations and local translation when authored prefab property masks hide direct gameplay edits. Restore only into an unpublished candidate world; failed recovery preserves the active runtime.
- Validate detail boundary edges before Detour closest-point calls; use bounded surface projection for traversal-edge rounding.
- Local final normal suites: static **27/27**, shared exact SDK **34/34**, portable editor **2/2**. Shared ASan/UBSan/LeakSanitizer **33/33** passed. Manual **3/3**, cache invalidation, C17 headers, formatting, workflow lint and runtime link separation passed. Static ASan/UBSan/LeakSanitizer **27/27** also passed. Clean Windows delivery follows.

### Windows binary-fixture correction

Build48 is withheld: the corruption/recovery test read a binary NavMesh through a text-mode stream, allowing Windows newline/EOF translation to alter its saved copy. Production admission correctly rejected the changed digest. The fixture now reads binary and asserts exact restored bytes. Linux core27/SDK34 passed; Windows navigation mesh/process and unrelated regressions passed while this recovery fixture failed. A new reserved build will verify the corrected test.

### Packaged-worker CI path correction

Build49 passed Windows/Linux core27/SDK34, Windows editor controllers2 and navigation/runtime/WARP25, shaders and compiled build identities. Its new navigation relocation step launched from GitHub's default directory instead of the explicit Windows checkout, so the test script was not found. The step now uses the same checkout working directory as the adjacent package/converter checks. Build49 is withheld; no runtime source changes were needed.
