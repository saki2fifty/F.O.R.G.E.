# CPU mesh particle effects

ParticleEffect is a reusable logical asset. A native `ParticleEmitter` references
it. Particles are transient render instances, with no Scene entities or saved
particle UUIDs. Flecs owns the component, the fixed clock owns emission age, and
the shared MeshSceneRenderer owns drawing and GPU residency.

## Source and publication

Sources use `.particles.json`, format `forge.particle_effect`. Existing version 1,
`cpu_mesh1` sources remain valid. New sources use version 2, `cpu_mesh2`.
Values include a Mesh reference, optional Material override, emission
mode, capacity, burst count, rate, lifetime, direction/cone half-angle, speed range,
constant acceleration, start/end size and linear RGB. Version 2 adds spawn shape,
linear drag and optional normalized-lifetime size curves and RGB gradients.
Extension metadata survives
source history but is excluded from cooked execution values. Admission limits are
64 KiB, 4,096 JSON nodes and depth 16. Invalid edits leave the document intact.

AssetSourceDocument owns edit/Undo/Redo/Save. Existing import/publication owners
update immutable cooked selection and catalog/index transactionally. The artifact
contains `effect.json`; exact Mesh/Material dependency revisions and source
generation are verified. Failed publication retains the selected working revision.
Runtime packages include selected cooks and dependencies, requiring neither
editable sources nor a compiler.

New cooking uses the exact `forge.particles.cpu-mesh.v2` recipe and version 2
values, including when the authored source remains version 1. Cooking never
rewrites that source. Existing immutable v1 artifacts are admitted only with
`forge.particles.cpu-mesh.v1` and output/metadata/value version 1; new artifacts
require all three versions to be 2. Mixed combinations reject. Existing Gallery54
sources, cooks, records and UUIDs are retained. Editing an extended field in a
legacy document explicitly promotes the source to version 2 in that Undo step.
The existing source-update service can recook an older selected recipe into v2;
this retains its root/source identity and the old immutable artifact. Built-in
Mesh/Material dependency freshness compares exact engine revisions, without
requiring project catalog records for engine-owned assets.

Source lifetime stops contain `id`, `time`, `value` and `interpolation`. Each
nonempty channel has 2–16 stops, strictly ordered distinct times and endpoints
exactly 0/1. UUIDs are nonempty and unique across both channels, scoped to the
effect root; they survive edits/history/save and remain independent after a root
copy. Cooked stops contain only time/value/interpolation. Unknown fields, missing
source IDs, duplicate IDs and unknown interpolation reject atomically. Extensions
are opaque source metadata, with no runtime interpretation or reference guessing.

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

Point origins are zero. Box samples its full XYZ dimensions uniformly; Sphere
samples its volume uniformly; Disc samples area in the plane perpendicular to the
normalized Direction. Box dimensions and Sphere/Disc radius are in meters. Shape
draws follow the original velocity draws; Point with default extended values
preserves the v1 random sequence and ballistic equations exactly. Origins receive
the emitter affine once, together with motion and mesh size.

Drag `d >= 0` is linear velocity damping in reciprocal seconds:

```
alpha = (1 - exp(-d*a)) / d
beta = (a - alpha) / d
position = origin + v*alpha + g*beta
velocity = v*exp(-d*a) + g*alpha
```

The zero-drag branch uses the original ballistic math plus origin. `expm1` and a
small `d*a` series for beta prevent cancellation for tiny drag. Drag applies to
the velocity produced by acceleration too; it is not a per-frame multiplier.

An enabled size curve or color gradient evaluates at normalized age `u = a/L`.
Linear blends to the next stop; Step holds until the next stop's exact time.
An empty channel uses its original start/end fields. Enabling starts from those
fields; adding a stop splits the largest gap and retains the interpolated value
and outgoing mode. Endpoints keep their times but their values remain editable.
Interior times must stay between neighbors. Disabling restores the retained
fallback fields; Undo restores the exact previous stop identities and values.

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

## Per-emitter appearance

`ParticleEmitter::size_multiplier` is a finite factor in [0.01,16], default 1.
`tint_r`, `tint_g`, `tint_b` are finite linear RGB factors in [0,1], default 1.
Size multiplies the evaluated lifetime size; tint multiplies the evaluated
gradient. Neither changes origins, velocity, birth count, age, seed or resource
selection. The emitter affine transforms position and size once. Conservative
bounds include the peak mesh size after multiplication. Several emitters can use
one immutable effect and mesh/material bundle.

The ordinary Inspector edits saved values with normal Undo/prefab override rules.
Live gameplay exposes only these four appearance fields for particle emitters.
Owner-thread commands validate world generation, Scene identity, entity and field
range. Observation adopts edits without advancing emission clocks; Stop/next Play
reconstructs from authored values. Invalid edits retain valid state. Older saved
values backfill neutral defaults on admission without rewriting source files or
changing formats/identities. Unknown extensions remain preserved.

The installed C++ `render_components.hpp` includes the trailing fields. This
changes the exact native SDK fingerprint and value layout: rebuild gameplay
modules against the matching SDK. Mismatched modules remain rejected. The C ABI
version and entry/function table layouts remain unchanged; private sampling and
runtime headers are not installed as gameplay APIs.

Gallery57 demonstrates six differently sized/tinted emitters sharing one effect.
Exports include compiled effect values and saved emitter parameters; no editable
effect source or compiler is required to run the game.

## Limits and ownership

Capacity is 1–1,024; rate is 0.01–256/second; lifetime is 0.01–30 seconds; speed is
0–1,000; size is 0.001–100; cone half-angle is 0–180 degrees. Continuous admission
requires `ceil(rate*lifetime) <= capacity`. Burst count fits capacity when Burst
is selected; the inactive mode's count remains editable. Playback speed is 0–16;
visual time is 0–1e9 seconds.

Shape dimensions/radius are 0.001–1,000 meters; drag is 0–64/second. Curve sizes
use the same 0.001–100 limit; gradient RGB is linear and bounded to [0,1], with
no opacity channel. Inactive values are still admitted and retained. Dynamic
curve storage counts toward the effect ResourcePool byte budget.

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
acceleration, origin extent, peak curve size and the full emitter affine for preview framing; actual
instances are culled during drawing. Unsupported profiles and overflow produce
diagnostics instead of successful silent truncation.

The central Particle Effect document owns source controls and isolated playback,
seek and reset. Preview uses the same evaluator/resources with one private draft
override; it does not mutate or tick live Play. Assignment is one Scene Undo
operation. C++ edits the native value through the matching SDK. Public value/header
changes require matching module rebuilds; ABI 1/C function-table layouts remain.

Version 2 source/evaluator values do not add native ParticleEmitter fields,
installed SDK inputs or ABI changes. Existing matching C++ modules still adjust
the emitter component; they do not need access to source stop arrays.

See [the user guide](../manual/editor/particles.md) and Gallery scenes 54 and 56. Native
Vulkan readback evidence is separate from Windows/editor physical acceptance.
