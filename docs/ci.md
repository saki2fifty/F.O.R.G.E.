# Build checks and Windows packages

Pushes and pull requests run Linux/Windows core tests, cache-invalidation tests, and formatting. They do not compile the D3D12 editor or publish a ZIP. Superseded push/PR runs on the same ref are cancelled; manual builds are not cancelled by a code push.

Request a Windows package using **Build and test → Run workflow**, with **windows_package** enabled. From the GitHub CLI:

```sh
gh workflow run build.yml --ref forge/windows-build -f windows_package=true -f build_id=YYMMDD-NNNNNN
```

Reserve the build identifier with the release coordinator before dispatch; the example above is a placeholder. The coordinator maintains one persistent, monotonically increasing counter and never reuses failed reservations. Use your intended branch in place of `forge/windows-build`. GitHub's Run workflow UI requires the workflow on the default branch; branch-specific dispatch can also be requested through the API/CLI. The package artifact remains `FORGE-Windows-x64`. No new package is needed merely to validate documentation or portable code edits.

## Cache behavior

Requested Windows builds restore the dependency checkout/build directory and CMake/Ninja build directory under the runner's short `AgentFiles` paths. Configure, build, tests, and packaging always execute even on a cache hit. Product source is freshly checked out rather than restored from cache, so Ninja sees fresh source files and recompiles them.

The compatibility key includes the runner image, architecture, MSVC version, Windows SDK, CMake/Ninja versions, absolute checkout path, and CMake configuration/dependency pins. The source commit is appended to the cache entry key; a compatible prior commit can supply a restore fallback. There is no fallback across different compatibility keys. Only successful tested/package builds save a cache. Cache upload failures do not prevent artifact delivery; a failed restore is discarded before a fresh configure.

Enable **clean_build** to bypass both cache restore and cache save for a clean verification. Cache misses after runner/toolchain updates or cache eviction are expected. The first build has to populate the cache.

This uses the official [cache restore/save actions](https://github.com/actions/cache), pinned to v6.1.0 commit `55cc8345863c7cc4c66a329aec7e433d2d1c52a9`. Cache matching and scope follow [GitHub's dependency-cache rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).

The repository name ends in a period, so Windows cache actions use a scoped Node preload to set their working directory to the short checkout. The preload affects only those cache processes. **Cache transport check** verifies a real save/delete/restore round trip whenever that shim or its diagnostic workflow changes; it can also be dispatched manually.

## Measured results

On September 15, 2026, the [cold Windows editor job](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34989524125) took **8m50s**. The [next-commit cached job](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34990556960) took **3m00s**, approximately 66% less time, including setup, cache transfer, tests, and packaging. Ninja executed 14 build steps instead of 579; configuration fell from 3m24s to 27s and compilation from 4m10s to 35s. Both editor test suites passed. This measures dependency reuse across a documentation-only commit with freshly checked-out product sources; timings vary with changes and runner conditions.

A [routine push](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/34987648929) completed its core/format checks in **1m52s**, with editor packaging skipped. These are separate workflows in practice: fast validation on pushes and an explicit package build when a Windows ZIP is needed.


## Build identity and manual

Product identity is `yymmdd-counter`: UTC date plus a counter padded to at least six digits. It has no release-channel suffix, and the counter never resets with the date. A packaging attempt consumes a reserved identifier; re-downloads retain it. The existing sequence continues after build 000005. Scene formats, module ABI, and IPC retain independent compatibility versions.

The `build_id` workflow input is required for Windows packaging. CMake embeds it and the source commit in both binaries and writes `build.json`. CI checks both `--version` outputs before packaging. Unassigned local builds cannot be packaged by the release script. The artifact contains `yymmdd-counter-FORGE-Windows-x64.zip`; manifest hashes cover the build metadata and manual. Publication verifies the reserved source and exact artifact bytes before accepting a numbered delivery.

End-user source pages live in `manual/`, separate from these technical documents. `python tests/manual_test.py` checks supported formatting, navigation, escaping, and identity validation. Packaging renders the current pages with `tools/build_manual.py` into a standalone offline HTML manual and includes the Markdown sources. The renderer intentionally supports headings, paragraphs, flat lists, fenced code, bold/inline code, and local page links; unsupported block forms fail validation. No web service or extra documentation dependency is required.

For an unpackaged local editor build, generate the manual beside the executable with `python tools/build_manual.py --output /path/to/build/manual --build-id unassigned`. Help opens `manual/index.html` through the OS handler. A browser-launch success only confirms dispatch to that handler; desktop opening remains an interactive check.
