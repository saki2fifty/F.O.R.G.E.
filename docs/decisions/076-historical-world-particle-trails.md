# ADR076 — Historical world-space particle trails

Status: accepted; implementation and the coherent local integration gate passed.
Native Windows and physical acceptance remain separate.

## Ownership and compatibility

Extend the existing world-owned particle history from
[ADR073](073-scene-particle-collision.md) and
[ADR075](075-xy-scene-particle-collision.md). Each ParticleRuntime entry retains
its birth identity, fixed-update clock, contact traces and detached candidate
admission. No additional World, particle entities, clock, registry or
presentation-time physics query is introduced.

Append source19 and immutable values54/55 for historical 3D/XY ribbons. Preserve
all prior source/value meanings, importer identities and local analytic trails
from [ADR066](066-analytic-particle-trails.md). Retain a separate
`world_trail_enabled` choice and shared `trail` settings. The existing source
owner selects Scene response before enabling historical geometry. Explicit local
trail or plane activation deactivates World trails; inactive settings survive.
Query-space changes preserve source19. These authoring/capture headers are
outside the installed gameplay SDK boundary; no component layout, service slot
or installed ABI changes.

## Bounded history and presentation

Keep flat previous/current snapshots inside the existing Scene history. The
previous snapshot supplies earlier tail positions; current interval traces
supply arbitrary presentation alpha within the new fixed step. Pruning against
the current clock alone would erase valid earlier presentation tails.

Retain a birth-age anchored position grid over the visible duration, its lower
bracketing point, exact contact markers and terminal endpoint. Each snapshot has
at most `2*segments+4` points per retained birth. Generate only the final bounded
grid window even when a large update crosses many grid intervals. Frozen birth
X-column magnitudes scale width; later emitter transforms do not alter positions
or widths already born. After lifetime or contact death, retain a shrinking tail
until the original end age plus trail duration. Appearance uses original birth
age/lifetime, with live emitter modulation applied once by rendering.

Mandatory path vertices are clipped tail/head endpoints and every interior
contact corner. Divide the longest remaining temporal gaps up to the selected
segment count. A candidate that cannot preserve all mandatory corners rejects
explicitly before any particle clock/history is adopted. Validate all intermediate
presentation windows, including births whose entire tail expires before the new
clock. Never silently truncate geometry. Retained samples remain a bounded
piecewise-linear approximation of curved motion and appearance.

The original exclusive head-death sampler remains unchanged. Historical geometry
explicitly requests the closed terminal sample. Ribbon point velocities are zero;
the path carries positions and appearance, while the existing head observation
retains its motion velocity.

## Transport, geometry and budgets

Extend the existing derived playback transport with `world_paths`: ordered flat
birth ranges `[birth,first,count,width]` and point tuples
`[age,x,y,z,size,r,g,b,opacity]`. Validate exact tuple shape, finite fields, age and
birth ordering, immutable source/seed/generation binding and resource budgets.
Heads and paths share a birth reservation; do not count their union twice.
Authored opaque playback remains nonauthoritative.

Reuse ordinary ribbon vertex generation, endpoint joins, per-camera side vectors,
Material validation, cross-object sorting and actual-corner culling. World paths
use identity positioning and their frozen width scales. Conservative whole-path
bounds retain selection/framing when only a tail survives. Charge retained path
and per-view geometry to the existing 8 MiB frame budget. Reserve
`capacity*segments` against the existing 8192 instances; retain existing emitter,
Scene birth, trace, per-birth query and World query limits. Whole-candidate and
whole-frame failures preserve the prior accepted observations.

## Authoring and export

Native controls and `asset.document.particle_trails` share the source/document
history owner. Optional `space=world` requires CPU; omitted `space` keeps the
existing local behavior. The existing Scene collision operation selects 3D/XY
query space and response settings. Sessions, grants, exact revisions, Undo/Redo,
save/publication and reopen use the existing workflow. Stopped views explain the
Play requirement; no isolated physics preview is created.

Active World trails require effect3, alpha1, opacity1, trail1,
`runtime_particle_scene_collision:1` and `runtime_particle_scene_trail:1`, plus
the XY capability for values55. Missing, malformed or unsupported kit capabilities
reject before output replacement. Native/rendered/export/Gallery/MCP/manual and
graceful scoped sanitizer evidence are required for a complete implementation;
the design and focused checks alone do not establish delivery or physical acceptance.
