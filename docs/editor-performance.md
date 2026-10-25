# Editor performance evidence

Measurements are source- and workload-specific, not a claim
about physical-GPU FPS or every scene. See [CI](ci.md) for reproducible tooling.

## Scene history and Content discovery — 2026-10-25

Compared baseline `91e6b57487b606637e2e9508621b1f8081c6b7cf` with the immutable
history and bounded Content discovery changes in the
[October 25 audit batch](../changelog/20261025/README.md). These are calls to the
actual `Scene::undo`/`redo` and `scan_content_catalog` implementations, with the
same existing libraries and deterministic input documents.

- Linux 5.15 VM, QEMU Virtual CPU 2.5+, GCC 12, Release `-O3 -DNDEBUG`.
- Each workload alternates before/after/after/before in separate processes.
  Each launch records seven operation samples; table medians combine fourteen
  samples per version. No compilation ran during the comparison.
- History uses 1,000 entities, each with a translation and a 256-byte opaque
  component payload. Setup creates the indicated history depth. Timed work is
  one Undo/Redo pair, checked against the original complete document afterward.
- Content uses 100 scene files with 100 entities each. All four launches use
  identical fixture bytes and verify the resulting record count and diagnostics.
  File creation is outside the timed region. This is a warmed scan workload.

| Workload | Before median | After median | Observation |
| --- | ---: | ---: | --- |
| Undo/Redo, 1 history entry | 243 ms | 258 ms | 6.2% slower in this run |
| Undo/Redo, 100 history entries | 722 ms | 330 ms | 54.2% less time |
| Content scan, 100 scenes | 690 ms | 256 ms | 62.9% less time |

For 100 history entries, peak process resident memory was 358.5 MiB before and
202.4–202.5 MiB after. Peaks at the end of history setup remained about 200 MiB:
the improvement removes transient history-stack document copies during replay,
not the retained full snapshots. Each entry still keeps a full document, with the
existing 100-entry cap. The one-entry result is included to avoid claiming that
all edit/history workloads became faster. Scene serialization and reconciliation
remain material costs; these numbers do not establish interactive large-scene
latency, Windows performance, GPU performance or physical-hardware acceptance.

Content reads each candidate once, shares schema admission with explicit scene
registration, and uses the existing catalog's bulk validation rather than repeated
per-scene graph rebuilding. No identity, file format, dependency pin or persistent
ownership changes are involved. The portable owner/consumer tests cover last-good
results, preserved catalog metadata, cancellation, history boundaries and prefab
conflict retries. Raw samples, binary/input hashes and benchmark tooling are kept
in `AgentFiles/audits/2026-10-25-project/performance-paired.json` and its sibling
audit files; these are local audit evidence, not a numbered Windows delivery.

## Read-only entity selection queries — 2026-10-25

Compared `d08583205f468a8190433238f4c1efc633e3a54c` with the selection query
correction in the [October 25 audit batch](../changelog/20261025/README.md).
Hierarchy asks whether each row is selected and whether there is at most one
selected entity. Previously both queries copied the complete selection vector
and its strings. Membership now searches a private read-only view; count reads
its size. Callers needing an owning snapshot still receive one from `entities()`.
The legacy single-entity writer, selection ordering and mutation rules are unchanged.

The probe calls the actual `EditorSelection` implementation on the same Linux VM
and GCC 12 Release configuration as the history measurements. Each sample makes
four passes over every selected row, checking membership and count. Input IDs are
36-byte strings; selection construction is outside timing. Before/after/before/after
processes each collect five samples, with no concurrent compilation. Medians below
combine ten samples per version; returned membership/count totals are checked.

| Selected entities / rows | Before, four passes | After, four passes |
| --- | ---: | ---: |
| 1 | 0.00060 ms | 0.00016 ms |
| 64 | 1.974 ms | 0.063 ms |
| 1,000 | 415.685 ms | 14.051 ms |

This measures only the two queries, not a whole Hierarchy frame, scene preparation,
filtering, tree construction, rendering or Windows responsiveness. Membership
still performs a linear search; the single-row timings are too short for a strong
latency claim. Native large-selection timing remains pending. Raw samples and
attribution are in the local audit's `selection-performance-paired.json` and
`selection-probe-*-inputs.json`. The portable `selection_editor` check exercises
actual Hierarchy Ctrl/Shift clicks and the existing batch-edit/Undo/prefab tests.

## Selection overlay row lookup — 2026-10-25

Compared `f929116` with the selection-overlay correction in the
[October 25 audit batch](../changelog/20261025/README.md). SceneTools previously
scanned the complete entity list twice for every selected object: once for its
position, then again for its primitive bounds. It now reuses the existing
temporary row index for multiple selections and one direct lookup for a single
selection. The borrowed lookup lasts only for the draw; no persistent cache is
introduced.

The probe creates a real 1,000-entity Scene and obtains its effective projection.
It times the actual `SceneTools::draw` call, including ImGui geometry generation,
on the same Linux VM/GCC 12 Release configuration. The last 1, 64 or 1,000 rows
are selected. Before/after/before/after processes each warm up three frames, then
collect 20 samples per case, with no concurrent compilation. Medians combine
40 samples per version. Vertex positions, UVs, colors, indices and command ranges
have identical hashes before and after in every measured case.

| Selected entities, 1,000 scene rows | Before | After |
| --- | ---: | ---: |
| 1 | 0.226 ms | 0.120 ms |
| 64 | 14.778 ms | 1.115 ms |
| 1,000 | 169.897 ms | 13.079 ms |

This measures CPU selection-overlay drawing only. It excludes scene extraction,
the rest of the editor frame, GPU submission/execution and native Windows input.
Full-frame and Windows review remain separate. Raw samples, binary hashes and
exact source/library inputs are retained in the local audit's
`selection-overlay-performance-paired.json` and `selection-overlay-*-inputs.json`.

## Matched startup and idle comparison

Verified 2026-09-22.

[Run35779709368](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35779709368)
used the driver from881be64, reusing both previously built executables:

- Accepted pre-Phase-7 source: `a98be9a672394d3d91c2b9067331d0252f9b4313`.
- Phase-7 source: `96b24dbc0d943977d41b806d48f13c065ac6c3a8`.
- Each source uses its own existing WARP adapter in a disposable test executable.
  Neither executable is represented as an original shipped ZIP.
- Same Windows2022 runner, OS10.0.20348, image20260913.307.1, four logical CPUs.
- 1440×900 outer window,100% UI, VSync off, preview lighting off; three alternating
  launches of each workload/version. Startup polls window responsiveness every50ms.
- Ten-second settling, followed by five one-second CPU/memory samples per launch.

| Workload | Source | Median responsive-window startup | Median CPU, whole-machine share | Median working set | Median private memory |
| --- | --- | ---: | ---: | ---: | ---: |
| Empty scene | Baseline |323ms|87.8%|61.1MiB|43.5MiB|
| Empty scene | Phase7 |381ms|84.9%|67.7MiB|47.9MiB|
| One legacy cube | Baseline |314ms|84.7%|61.7MiB|44.1MiB|
| One legacy cube | Phase7 |441ms|84.0%|69.9MiB|49.7MiB|

Startup medians use three launches; idle medians use fifteen samples per row.
CPU is process CPU time divided by wall time and logical CPU count. Uncapped
rendering on a software device keeps CPU usage high even without user input.
This small sample and coarse startup polling do not justify universal percentage
claims. Startup is **not first completed GPU frame**. OS/driver caches were not
controlled. Executable hashes and raw samples accompany the run artifact.

The cube uses the legacy primitive representation shared by both versions. It
measures a comparable existing path, **not** the new MeshRenderer/PBR primitive
recipe, imported models or skinning. No frame-wait errors occurred in these twelve
launches. Later menu/helper-label polish does not execute in these empty/legacy
workloads; these remain measurements of the identified96b24db executable.

The earlier three-second-settling comparison in
[run35777476740](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35777476740)
is retained as exploratory evidence. The longer interval avoids conflating idle
samples with the multi-second first-use backlog seen in the separate mesh workflow.

## Shader compile versus cooked realization

The11932c9 native shader fixture in
[run35774090069](https://github.com/saki2fifty/F.O.R.G.E./actions/runs/35774090069)
measured one vertex/pixel program, two includes and `WARM=1`, using D3D12/WARP,
FXC5.1 and Release compiler settings. Six samples:

- Compile:79.05–80.48ms.
- Decode and realize an already cooked shader:0.125–0.216ms, excluding encoding.

The FORGE derived-data cache was bypassed. The first sample is the first compile
of this workload in that process, not a guaranteed cold OS/driver state. These
numbers do not measure driver pipeline/JIT cost, all PBR permutations, or total
import time. Existing `shader_worker` checks separately measure artifact
publication and derived-cache hits. The exact compiler digest is retained in the
native test output.

## Hosted WARP queue diagnostics

See [the backend diagnostic contract](render-backends.md#hosted-warp-frame-wait-diagnostics).
The actual input and full-render captures exercise the new mesh paths separately
from the matched legacy workload. Their observed GPU backlog must not be hidden
by quoting the small shader compilation or common-scene startup measurements.
