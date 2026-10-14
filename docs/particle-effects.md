# CPU mesh particle effects

ParticleEffect is a reusable logical asset. A native `ParticleEmitter` references
it. Particles are transient render instances, with no Scene entities or saved
particle UUIDs. Flecs owns the component, the fixed clock owns emission age, and
the shared MeshSceneRenderer owns drawing and GPU residency.

## Source and publication

Sources use `.particles.json`, format `forge.particle_effect`, version 1, profile
`cpu_mesh1`. Values include a Mesh reference, optional Material override, emission
mode, capacity, burst count, rate, lifetime, direction/cone half-angle, speed range,
constant acceleration, start/end size and linear RGB. Extension metadata survives
source history but is excluded from cooked execution values. Admission limits are
64 KiB, 4,096 JSON nodes and depth 16. Invalid edits leave the document intact.

AssetSourceDocument owns edit/Undo/Redo/Save. Existing import/publication owners
update immutable cooked selection and catalog/index transactionally. The artifact
contains `effect.json`; exact Mesh/Material dependency revisions and source
generation are verified. Failed publication retains the selected working revision.
Runtime packages include selected cooks and dependencies, requiring neither
editable sources nor a compiler.

The initial profile admits one static triangle part, one LOD and one material
slot, with built-in opaque/masked PBR or Unlit materials. Skinning, morphs,
point/line topology, Blend, transmission and custom Shader surfaces reject with
diagnostics. Graphs, GPU simulation, collision, trails, texture-sheet animation
and per-particle opacity are outside this profile.

## Equations and time

Motion is emitter-local before the complete affine transform. Units are meters
and seconds, with +Y up. Cone half-angle is in degrees; sampling is uniform in
solid angle around the normalized direction. For particle age `a`, initial
velocity `v`, acceleration `g` and lifetime `L`:

```
position = v*a + 0.5*g*a*a
velocity = v + g*a
u = a/L
size = lerp(size_start, size_end, u)
color = lerp(color_start, color_end, u)
world = emitter_affine * translate(position) * uniform_scale(size)
```

Burst births are at time 0 and expire at `a >= L`. Continuous ordinal
`n = 1,2,...` is born at `n/rate`. Admission compares time and lifetime in the same
emission-phase units, avoiding rounded birth-boundary ambiguity. Only the bounded
active ordinal window is sampled, including at large times. Seed and the 64-bit
emitter-scoped ordinal initialize a standard-library random engine per birth.
Repeated local evaluations agree; cross-platform lockstep is not a contract.

Fixed ticks advance previous/current age by `fixed_dt * playback_speed`.
Presentation interpolates those ages without advancing simulation. Speed 0
freezes the emitter. Reference/seed changes, disable, removal or native entity
generation changes restart or retire it. Recovery restarts visual emission from
restored native values, without serializing particle arrays. Captures contain
compact time, revision/generation and seed; selection mismatch or seed overflow
rejects before rendering.

## Limits and ownership

Capacity is 1–1,024; rate is 0.01–256/second; lifetime is 0.01–30 seconds; speed is
0–1,000; size is 0.001–100; cone half-angle is 0–180 degrees. Continuous admission
requires `ceil(rate*lifetime) <= capacity`. Burst count fits capacity when Burst
is selected; the inactive mode's count remains editable. Playback speed is 0–16;
visual time is 0–1e9 seconds.

A scene admits 64 enabled emitters, 8,192 reserved instances and 8 MiB retained
particle payload per frame. The existing bounded ResourcePool owns immutable
leases. Requests retire when no emitter needs the reference; late/cancelled
completion cannot revive removed state. Catalog admission is lazy, allowing
asset-free runtimes without a project catalog. Readiness includes queued/loading
requests and completed resources not yet acquired by presentation.

Drawing reuses Mesh/Material/Texture resources, camera-relative uploads,
parity-aware sorting, the shared 64-instance batch limit and fence ownership.
Ordinary meshes and emitters may coexist on an entity. Camera culling, visibility
and layers apply; hidden effects keep aging. This profile does not cast particle
shadows. A conservative outward-rounded lifetime envelope includes maximum speed,
acceleration, size and the full emitter affine for preview framing; actual
instances are culled during drawing. Unsupported profiles and overflow produce
diagnostics instead of successful silent truncation.

The central Particle Effect document owns source controls and isolated playback,
seek and reset. Preview uses the same evaluator/resources with one private draft
override; it does not mutate or tick live Play. Assignment is one Scene Undo
operation. C++ edits the native value through the matching SDK. Public value/header
changes require matching module rebuilds; ABI 1/C function-table layouts remain.

See [the user guide](../manual/editor/particles.md) and Gallery scene 54. Native
Vulkan readback evidence is separate from Windows/editor physical acceptance.
