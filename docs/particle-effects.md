# CPU and GPU mesh particle effects

ParticleEffect is a reusable logical asset. A native `ParticleEmitter` references
it. Particles are transient render instances, with no Scene entities or saved
particle UUIDs. Flecs owns the component, the fixed clock owns emission age, and
the shared MeshSceneRenderer owns drawing and GPU residency.

## Source and publication

Sources use `.particles.json`, format `forge.particle_effect`. Existing version 1,
`cpu_mesh1` sources remain valid. New sources start with version 2, `cpu_mesh2`;
explicit GPU evaluation uses source3/profile `gpu_mesh1`. Authored particle
graphs use source4 and active values4/profile `gpu_graph1` (see below).
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

New cooking uses `forge.particles.mesh.v3` and a version3 cooked envelope containing
`format`, `version`, `asset_id` and `values`. Values are explicit CPU2, GPU3 or graph4;
legacy CPU1 normalizes to CPU2 without rewriting editable source. Publication
metadata carries envelope `version:3` and the exact `value_version`. Source/stop
UUIDs and extensions remain source-only. The persisted importer identifier remains
`forge.particles.cpu-mesh`: selecting evaluation must not change saved source
settings or sidecar identity.

Existing immutable v1/v2 artifacts retain their exact original recipes and raw
value profiles. Resource admission checks recipe/envelope/metadata/value versions,
source/dependency digests and selected generation together; mixed combinations
reject. Publishing creates a new immutable selection through the same existing
owner. It does not migrate earlier sources or overwrite old artifacts. New
publications require `runtime_particle_effect:3`; GPU values additionally require
`runtime_particle_gpu:1` in the selected runtime kit. The exporter rejects a
missing/mismatched capability before delivery. See [ADR060](decisions/060-gpu-particle-profile.md).

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
diagnostics. Collision, trails, texture-sheet animation and per-particle opacity
are outside this profile. The bounded stateless graph profile below extends
position, size and RGB without adding those capabilities.

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
active ordinal window is admitted, including at large times. In CPU profiles,
seed and the 64-bit emitter-scoped ordinal initialize a standard-library random engine per birth.
Repeated local evaluations agree; cross-platform lockstep is not a contract.

Fixed ticks advance previous/current age by `fixed_dt * playback_speed`.
Presentation interpolates those ages without advancing simulation. Speed 0
freezes the emitter. Reference/seed changes, disable, removal or native entity
generation changes restart or retire it. Recovery restarts visual emission from
restored native values, without serializing particle arrays. Captures contain
compact time, revision/generation and seed; selection mismatch or seed overflow
rejects before rendering.

## GPU evaluation and renderer lifetime

Source3 uses a separately specified mathematical generator. FNV-1a hashes the
little-endian seed32/ordinal64 bytes; a48271/2147483647 linear congruential sequence
produces birth samples. The independently authored HLSL implements bounded32bit
Schrage arithmetic, checked against a64bit modular reference. It evaluates Point,
Box, Sphere and Disc origins, cone velocity, analytic acceleration/drag, lifetime
Step/Linear size/RGB and complete affine instance transforms. CPU profiles retain
their original standard-library sequence; backend/profile visual patterns differ.
Float GPU evaluation does not provide cross-device lockstep.

CPU double precision determines only the active ordinal window and short relative
ages from the existing presentation clock. No billion-second float clock, second
World, per-particle entity, CPU transform array or independent simulation manager
is introduced. Source3 stop times must remain distinct as floats; malformed or
unrepresentable packets reject before publication or drawing.

The existing renderer's ParticleSource retains strong effect/Mesh/Material leases,
selection epoch/revision/generation and one bounded per-emitter vertex/UAV buffer
realization. Presentation retains shared compute shader/pipeline caches. One proxy
per emitter uses the existing seven-row static instance layout; MeshDraw accepts
an exclusive validated buffer range and preserves the64-instance draw bound.
Each camera gets its own compute evaluation and target/viewport restoration.
Lifetime bounds, including the admitted mesh and float view padding, govern GPU
culling. Compute cannot be merged with CPU batches, temporal meshes or indirect
visibility packets. Particles continue to exclude shadow casting and temporal
Mesh accumulation. Unsupported compute is an explicit diagnostic, without a
silent CPU profile substitution. Failed submissions never queue old rows as new
time; removal, Scene replacement and source epochs retire the same renderer owners.

Native **Evaluation** and `asset.document.particle_evaluation` both call the source
operation under AssetSourceDocument history. Both preserve source/stop IDs,
extensions, curves and normal revision/grant/Undo/publication semantics.

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
CPU instances are culled during drawing; GPU emitters use their lifetime envelope. Unsupported profiles and overflow produce
diagnostics instead of successful silent truncation.

The central Particle Effect document owns source controls and isolated playback,
seek and reset. Preview uses the same evaluator/resources with one private draft
override; it does not mutate or tick live Play. Assignment is one Scene Undo
operation. C++ edits the native value through the matching SDK. Public value/header
changes require matching module rebuilds; ABI 1/C function-table layouts remain.

Version2/3 source/evaluator values do not add native ParticleEmitter fields,
installed SDK inputs or ABI changes. Existing matching C++ modules still adjust
the emitter component; they do not need access to source stop arrays.

See [the user guide](../manual/editor/particles.md),
[particle graphs](../manual/editor/particle-graphs.md) and Gallery scenes54,56,238,239.
Native Vulkan readback evidence is separate from Windows/editor physical acceptance.

## Focused validation and capture provenance

`particle_gpu_vulkan` checks1,344parameter configurations and89,376particles against
independent uint64/double reference math, with maximum observed absolute row error
5.34058e-5 on software Vulkan. It also compares two exact CPU/compute-written
MeshDraw pixel pairs and rejects malformed buffer ranges. `particle_gpu_scene_vulkan`
uses actual MeshSceneRenderer and fixed-clock World presentation for source-hidden
Windows-targeted content, without claiming native Windows execution. The Gallery
portion checks576instances in10draws sharing one bundle; authored and source-hidden
package images match exactly. Existing CPU scene and codec tests retain compatibility.
`particle_effect_editor` uses actual widgets/history at regular/compact and scaled
layouts; `mcp_particle_evaluation_stdio` exercises both supported MCP formats.

The canonical manual image `manual/images/gpu-particles.png` is a lossless P6→PNG
conversion of `particle_vulkan_probe --gpu` Gallery238 at1.5fixed-clock seconds,
960×540, using the production MeshSceneRenderer and pinned Diligent Vulkan backend.
It depicts rendered content, without editor chrome or retouched controls. Capture
provenance: local focused native build31/check32 before source-only documentation
and sample promotion; software Vulkan, DX Shader Compiler1.9 observed in native logs.
The sample's source/cook bytes are exactly those exercised by that candidate gate.
Final build44/check45 uses expanded low-drag coverage and produces a Gallery image
byte-identical to that canonical capture; unchanged capture bytes are retained.
This establishes neither physical hardware performance nor Windows/whole-editor
visual acceptance. Stateful VFX simulation graphs and complete particle-system equivalence
are not claimed.

## Authored particle program profile

[ADR061](decisions/061-particle-graph-programs.md) extends the existing owners.
Shader source4 contains forge.particle.graph1 and compiles with the shared typed
graph Compiler. Root bounds are256nodes/1024edges/16functions; pure-function
expansion is limited to1024nodes. Version/domain validation occurs before
expansion; unsupported versions stay opaque in source/discovery and fail compile.
Material source3 and function format1 are unchanged.

`ParticleGraphProgram` contains one Shader AssetRef, symmetric local XYZ extent
and maximum unmodulated size. `set_graph_program(program, activate)` performs one
validated source/history operation. With activate=false, CPU2 or built-inGPU3
retains source4 settings but projects ordinary values and has no graph runtime
dependency. CPU stop times retain their double precision. Activating Graph admits
GPU stop precision and projects values4/profile gpu_graph1. Root/stop IDs, source
extensions, importer identity, neutral recipe3/envelope3 and native emitter/ABI
layouts retain their meaning.

Context and outputs are described in the user guide. Graph extent<=1e6m peraxis
and maximum size<=1e4m are finite/nonnegative, rejecting nonzero float underflow.
Outputs clamp to those bounds; nonfinite outputs fall back to analytic inputs.
RGB clamps to[0,65504] before per-emitter tint. Culling uses declared bounds and
maximum size with full affine transforms and emitter size. Device-envelope
validation retains numeric headroom and clears rejected submission counts.

Active effects declare the ordinary runtime dependency edge `particles.program`.
Import admission loads the selected immutable Shader through request_shader and
requires particle_graph_profile1: one64x1x1 Compute entry with exactly
ParticleValues, ParticleRows and ParticleGraphLimits and the required typed constant
layout. Native realization re-reflects cooked DXBC. The host's bounded Shader pool
retains leases; existing catalog epochs retire effects/programs and stale selections.
There is no authored-source compiler in the runtime and no alternate Vulkan path
for a D3D12 cook. Kits advertise runtime_particle_graph1 in addition to GPU/effect
profiles. See [Shader assets](shader-assets.md) and [standalone](standalone-host.md).

The editable Gallery239 graph uses an original Orbit offset pure function,
Age/Birth Random, shrink-over-life and a color blend. The two emitters share the
program/effect and keep separate appearance. The Windows package preparer cooks
Shader then Effect through the existing worker/publication services. Runtime
packaging follows exactly Scene→Effect→Shader with source hidden.

Focused checks include source/history/domain/version negatives, retained inactive
settings/CPU precision, exact Shader profile/publication metadata, GPU numeric
limits/culling, real canvas/Effect widgets, both MCP protocols and graceful native
owner exits. `particle_graph_vulkan` verifies generated Compute on software Vulkan;
it is not cooked Windows acceptance. `particle_graph_gallery_export` is the
Windows gate for actual worker cook, relocated source-hidden closure, immutable
bytecode realization,64particle numeric readback and production fixed-clock Game
rendering. Its native Windows execution is pending until a passing run is recorded.

Canonical `manual/images/particle-graph.png` is the actual MaterialGraphCanvas in
particle mode, displaying Gallery239 unchanged. `particle_graph_canvas` renders
ImGui draw data through the SDL software backend at1440×1000 and asserts display
does not edit the source. Local build21/check21 captured it and it was personally
reviewed. No retouch, complete-editor screenshot, Windows or physical GPU claim.
