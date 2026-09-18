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

## Phase 6F — runtime UI

- Added pinned RmlUi 6.3 and FreeType 2.14.3, licensed engine Lato font, reusable ImGui-free presenter and Diligent geometry/texture/scissor/transform/stencil adapter.
- Added reflected UiDocument assets/components, runtime-only UiService, bounded copied models, semantic commands with session/generation/incarnation correlation, acknowledgements and duplicate suppression.
- Preserved runtime gameplay isolation and headless presentation-free linkage. UI presentation remains responsive while paused; gameplay command processing obeys fixed ticks.
- Added project-contained resource admission and candidate reload with initial geometry/texture preparation; failed replacements preserve the prior usable HUD.
- Added Content HUD creation/registration, Inspector asset/visibility/layer editing, Play reload, explicit input ownership and release/focus handling. Added the Runtime UI user manual and technical dependency/ownership docs.
- Added runtime/presenter/process/SDK/prefab/input tests and separate D3D12 UI fixtures. Final local normal suites: core30/30, exact SDK38/38, editor/input3/3. UI/core/presenter checks pass with ASan/UBSan/leak instrumentation; full sanitizer suites and the converter-override retest are recorded in validation evidence. Windows verification is pending; no Windows delivery claim yet. Phase 6G remains outside this change.

### UI integration validation corrections

- Candidate first-render preparation catches lazy image/font/geometry failures before publication; failed structural candidates wait for explicit retry instead of reparsing every frame.
- Keep command sequencing across hidden viewport/empty document intervals; clear queued stale requests on native-reload generation changes. Added a real editor-process reload-order regression.
- Use RmlUi6.3's actual project-root font-source path policy and quoted @font-face syntax; include UiDocument in the installed experimental SDK and build/run its consumer. Relocate the unchanged StableId declaration into the identity header for that consumer.
- Apply the existing unsanitized converter override to animation-admission fixture generation too. Strict UBSan flags exposed Ozz0.17.0's zero-byte file-write call with a null pointer in the converter. FORGE admission and Ozz runtime remain instrumented; no upstream suppression or dependency change is introduced.

### Final local Phase6F validation

Core30/30, shared exact SDK38/38, editor/input3/3 passed. Static sanitizer29 + corrected admission1 and shared sanitizer36 + corrected admission1 passed with ASan/UBSan/leak checks. RmlUi and FreeType are instrumented. Manual3, C17 headers, format/workflow/cache checks and headless/presenter link separation passed. Windows/D3D12 execution remains the next gate.

### Windows renderer naming correction

Build51 is withheld. All four Windows/Linux core and SDK jobs and the editor process-controller tests passed. The editor compilation caught Windows' `interface` macro expanding a new renderer method name. Renamed that private accessor to `render_interface`; Linux syntax checks also define the Windows macro to cover this collision. A new numbered Windows build will verify the correction; no scene/ABI1/rendering behavior change.
