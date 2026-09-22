# Editor performance evidence

Verified 2026-09-22. Measurements are source- and workload-specific, not a claim
about physical-GPU FPS or every scene. See [CI](ci.md) for reproducible tooling.

## Matched startup and idle comparison

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
