# Editor performance evidence

Measurements are source- and workload-specific, not a claim
about physical-GPU FPS or every scene. See [CI](ci.md) for reproducible tooling.

## Dynamic 3D physics CPU reports — 2026-10-27

The optional `physics` workload in [CPU profiling](cpu-profiling.md) uses the
existing runtime, native Jolt owner and five collected host scopes. Original boxes
fall separately under gravity; independent equations check every final local/world
pose, active body count and unchanged controls. This covers synchronization and
integration of many independent bodies, with no contact/constraint/character load.

One Linux/QEMU Virtual CPU 2.5+ run used GCC12 Release, 24 logical processors and
allowed affinity 0..23, with no concurrent build or timing job. Jolt remains pinned
at 5.6.0/e77f175; double positions and the recorded baseline native options are
unchanged. Each size used three fresh processes, 8 warmup and 16 measured ticks per
process. All 9 runs verified 14,016 final bodies; 144 measured ticks retain 720 scope
samples. The maximum 272-tick sampling policy is checked separately with 4 bodies.
Collection overhead is included; native plan-observation ticks are discarded.

| Bodies | Fixed tick | Synchronization | Native step | Adoption | Pose capture |
| --- | ---: | ---: | ---: | ---: | ---: |
|64 |1.967 ms |1.084 ms |0.221 ms |0.057 ms |0.054 ms |
|512 |11.979 ms |7.212 ms |0.654 ms |0.435 ms |0.408 ms |
|4096 |90.636 ms |60.335 ms |3.888 ms |4.064 ms |3.165 ms |

Each column is a median over 48 samples. The complete tick includes its stages;
do not add it to those columns or add independent medians to reconstruct a tick.
PhysicsStep is joined native wall time; worker count/utilization was not measured.
Scene setup, full source/recovery serialization, pipe transport, readback checks and
fixture retirement/shutdown are outside the scopes. These are neither gameFPS nor
physical performance, nor evidence of contact-heavy throughput or improvement.

Raw report, original executable/tool/client/compiler/native/configuration metadata,
independent correctness and the matching source/binary receipt are under
`AgentFiles/build/phase164-physics-cpu-workloads`. Arbitrary report source hints
remain explicitly unverified; that matching receipt establishes this run's build
attribution. The original transform baseline below retains its own older binary.
Native Windows execution and controlled physical/game workload continuity remain
pending. The tool changes no runtime, native library, SDK, collector or game API.

## Repeatable transform CPU reports — 2026-10-27

### Owned pose capture and ordered history synchronization

The fixed-tick producer already returns a new owning map. Adopting it removes a
copy that was immediately discarded, while the legacy const-reference API retains
its copying behavior. Matching two sorted maps in one ordered pass removes two
sets of repeated tree lookups for retained/removed keys. Previous/current ownership,
reparent/new-generation snaps and interpolation outputs remain unchanged.

A frozen executable linked the original capture implementation; the final candidate
uses the same driver/compiler/native configuration and actual owned capture method.
Three process pairs alternate before/after, after/before, before/after on allowed
CPU0, with no concurrent build/timing. Each process/size verifies280 captures;
seven rounds retain32 timed captures after8warmup calls. The table is the median
of21 batch means per version/size, on the same shared-QEMU GCC12 Release runner.

| Nodes | Original method | Owned/ordered method | Change |
| --- | ---: | ---: | ---: |
|64 |4.655us |1.730us |62.8% less |
|512 |64.863us |14.368us |77.8% less |
|4096 |732.251us |134.249us |81.7% less |

This times the actual PresentationPoses method with two timer reads. Fresh producer
map setup, pose evaluation/correctness and legacy producer destruction are outside
timing. It is not a measurement of the complete PresentationPoseCapture scope,
which also gathers nodes, or a fixed-tick/gameFPS/physical-hardware speedup claim.
The earlier full runtime table below remains attributable to its original binary;
do not substitute these method timings for those columns. Raw paired samples,
frozen/current binary/source hashes and final validation are under
`AgentFiles/build/phase162-owned-pose-capture`. An intermediate copy-only candidate
and its evidence are retained separately; final source includes ordered history.

### Full runtime workload baseline

`tools/benchmark_runtime.py` records checked, repeatable scope reports through the
existing ordinary runtime client/collector. See [CPU profiling](cpu-profiling.md)
for invocation, sampling and comparison admission. Engine/collector source and
the measured executable are unchanged from local5453f78; the tool's receipt links
its exact hashes to that build. Checkout hints in an arbitrary report are not
independent binary-origin proof.

One local GCC12 Release run used Linux/QEMU Virtual CPU2.5+,24logical processors,
affinity0..23, no concurrent build/timing job. It measured32ticks after32warmup
ticks in each of three fresh processes per workload. Each row combines96 samples;
all18runs verified the exact source and every derived world affine matrix.
Native lifetime-plan observation ticks were discarded before measured chunks.
Collection overhead remains included.

| Layout / entities | FixedSimulationTick median | PresentationPoseCapture median |
| --- | ---: | ---: |
| Flat64 |0.629ms |0.055ms |
| Flat512 |5.672ms |0.528ms |
| Flat4096 |33.659ms |4.257ms |
| Four-way hierarchy64 |0.706ms |0.067ms |
| Four-way hierarchy512 |5.114ms |0.631ms |
| Four-way hierarchy4096 |51.119ms |6.589ms |

These are static transform CPU workloads, not gameFPS, physical hardware or a
comparison claiming improvement. Pose capture is included in FixedSimulationTick;
do not add the columns together. Source setup, IPC, full recovery/Scene response
serialization and correctness readback are outside those scopes. The full source
tool run took about541seconds including that orchestration; default large jobs
are deliberately separate from the small correctness CTest. Broader game, asset,
animation, physics and rendering workloads remain unproved by this subset.

The immutable raw report is
`AgentFiles/build/phase161-runtime-workload-reports/default-workloads.json`;
its matching validation receipt owns source/build/configuration/environment hashes.
The five functional tool checks cover independent sums/statistics, malformed
reports and actual comparison/output preservation. No native code/ABI/dependency
change or new native sanitizer claim accompanies this Python consumer. Windows
source-tool execution and controlled physical measurements remain queued.

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

## Batched structural framing expansion — 2026-10-25

Compared `0e65132` with the framing correction in the
[October 25 audit batch](../changelog/20261025/README.md). Frame selected previously
called the single-root expansion once per selected object, rebuilding the same
child lookup and traversing overlapping descendants repeatedly. The existing
framing helper now accepts the whole selection, constructs the lookup once and
uses one visited set. Its single-root entry point forwards to the same algorithm.
No persistent hierarchy cache or ownership changes are introduced.

The Linux/GCC 12 Release probe compares the former caller expansion with the new
batch call, using the actual framing helper and 1,000-row structural fixtures.
Flat rows have no parents; the chain has each row parented to its predecessor.
The first 1, 64 or 1,000 rows are selected. Before/after/before/after processes each
warm up once, then collect six samples per case, with no concurrent compilation.
Medians combine 12 samples per version. Every expected expanded entity ID and
count is checked, and input rows remain unchanged.

| Hierarchy | Selected roots | Before | After |
| --- | ---: | ---: | ---: |
| Flat | 1 | 0.0116 ms | 0.0107 ms |
| Flat | 64 | 0.963 ms | 0.020 ms |
| Flat | 1,000 | 11.575 ms | 0.236 ms |
| Chain | 1 | 0.829 ms | 0.617 ms |
| Chain | 64 | 57.863 ms | 0.626 ms |
| Chain | 1,000 | 587.944 ms | 0.801 ms |

These are selection-expansion timings, excluding mesh/pose bounds, camera
fitting, rendering, and Windows input. The flat single-root difference is too
small for a strong latency claim. Native viewport compilation/camera review
remains pending. Exact source/library/binary attribution and samples are in the
local audit's `framing-performance-paired.json` and `framing-*-inputs.json`.

## Idle selection reconciliation — 2026-10-25

Compared `15eb105` with the selection-reconciliation correction in the
[October 25 audit batch](../changelog/20261025/README.md). The frame loop previously
called `Scene::document()` for selection maintenance every frame, despite already
owning an AuthoringSnapshot. Reconciliation then scanned all rows for every
selected ID and rebuilt the selection even when it was intact.

Frame-start reconciliation now borrows the existing revision-aware snapshot,
before document views borrow it. Event callbacks retain their detached reads.
Reconciliation borrows selected IDs, uses the existing temporary row index for
multiple targets and skips rebuilding intact normalized selections. Legacy writes
and stale-ID removal still normalize the selection and preserve the range anchor.
Neither the snapshot's ownership nor its invalidation rules change.

The Linux/GCC 12 Release probe uses a real unchanged 1,000-entity Scene and the
actual former/corrected caller plus EditorSelection. The last 0, 1, 64 or 1,000 IDs
are selected. Before/after/before/after processes each warm up three iterations,
then collect 20 samples per case, with no concurrent compilation. Medians combine
40 samples per version. Every sample checks exact selection order and primary;
Scene contents/revision remain unchanged.

| Selected entities, 1,000 scene rows | Before | After |
| --- | ---: | ---: |
| 0 | 40.591 ms | 0.110 ms |
| 1 | 38.405 ms | 0.184 ms |
| 64 | 43.800 ms | 0.397 ms |
| 1,000 | 81.495 ms | 0.615 ms |

These are steady-state selection-maintenance timings, including the snapshot
read, not total frame time, FPS or Windows responsiveness. A changed Scene still
requires snapshot refresh. Portable checks exercise native writes, reset,
delete/Undo/Redo and switching snapshot owners; full native editor compilation
and interaction remain pending. Inputs, binary hashes and samples are retained
in the local audit's `selection-reconcile-performance-paired.json` and
`selection-reconcile-*-inputs.json`.

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
