# CPU and GPU mesh particle effects

ParticleEffect is a reusable logical asset. A native `ParticleEmitter` references
it. Particles are transient render instances, with no Scene entities or saved
particle UUIDs. Flecs owns the component, the fixed clock owns emission age, and
the shared MeshSceneRenderer owns drawing and GPU residency.

## Source and publication

Sources use `.particles.json`, format `forge.particle_effect`. Existing version 1,
`cpu_mesh1` sources remain valid. New sources start with version 2, `cpu_mesh2`;
explicit GPU evaluation uses source3/profile `gpu_mesh1`. Authored particle
graphs use source4 and active values4/profile `gpu_graph1` (see below). Source5
adds explicit CPU values5/profile `cpu_alpha1` for sorted transparent meshes.
Source6 adds CPU values6/`cpu_alpha2` for opacity over life while retaining inactive
opacity and graph settings across evaluation changes. Source7 adds camera-plane
CPU values7/`cpu_billboard1` with retained original Mesh and alignment selection.
Source8 retains separate CPU/GPU alignment selections and projects GPU values8/
`gpu_billboard1` or graph values9/`gpu_graph_billboard1` when GPU alignment is active.
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
`format`, `version`, `asset_id` and `values`. Values are explicit CPU2, GPU3, graph4
or sorted-alpha CPU5/opacity CPU6/camera-plane CPU7/GPU camera-plane8/graph camera-plane9;
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

All profiles admit one static triangle part, one LOD and one material slot with
built-in PBR or Unlit Materials. CPU1/2 and GPU3/4/8/9 require opaque/masked Materials.
CPU5 additionally admits Blend with depth testing and no depth writes; CPU6
requires this Blend profile for opacity. CPU7 also permits opaque/masked Materials
with identity opacity; nonidentity opacity requires the same Blend profile. Skinning,
morphs, point/line topology, transmission and custom Shader surfaces reject with
diagnostics. Collision, trails and texture-sheet animation
are outside these profiles. The bounded stateless graph profile below extends
position, size and RGB without adding those capabilities.

## Sorted-alpha CPU profile

[ADR062](decisions/062-sorted-alpha-particles.md) introduces explicit values5 and
source5 without changing earlier value meanings, IDs, recipe3/envelope3 or the
ParticleEmitter/gameplay ABI. `particle_uses_gpu` identifies GPU3/4/8/9 explicitly;
CPU5 keeps the CPU random sequence, double clock and lifetime-stop precision.
Source5 retains optional inactive graph settings across evaluation changes.

CPU5 uses existing per-particle poses and the shared Mesh sort queue, including
ordinary Blend Meshes. Blend particles retain individual submissions rather than
opaque instance batching. Sorting is per Mesh part; intersecting triangles retain
ordinary sorted-alpha limits. Transparent particles trigger existing whole-view
weighted fallback. No GPU sorting/readback or second simulation owner is added.

Material owns alpha and textures. The existing seven-row tint packet uses flag2
for CPU5 RGB modulation without the legacy Unlit blockout light. Older profiles
keep flag1. Single, batched and fallback CPU5 draws share one instance projection.
Publication and renderer admission call the same Material profile validator.
Export requires `engine.runtime_particle_alpha:1` for values5, independently of
GPU/graph support. Older kits reject CPU5; old cooks keep their original bytes.
Gallery240 supplies a shared translucent Material and two colored emitters.

## CPU opacity over life

[ADR063](decisions/063-particle-opacity.md) extends sorted-alpha evaluation with
explicit values6/`cpu_alpha2`. The existing CPU sampler evaluates normalized
start/end opacity or 2–16 Step/Linear stops at age/lifetime. Material, texture and
vertex alpha multiply this per-birth value; RGB and size remain independent.
Opacity does not consume random numbers or change births, motion or clocks.

Source6 retains `opacity_enabled`, `opacity_start`, `opacity_end`, `opacity_curve`
and local stop UUIDs, including across GPU/Graph switches. The compiled projection
contains opacity fields only for active values6. Disabled CPU alpha projects values5.
Inactive opacity remains validated with double CPU precision. Enabling opacity
selects CPU sorted-alpha through one shared native/MCP source operation; ordinary
validated patch/history operations edit its start/end and curve fields.

The seven-row packet remains unchanged: new tint flag3 admits opacity in the first
lighting-basis row's unused W lane; the vertex path multiplies Color.a. Existing
flags0/1/2, Sprite/wind/temporal profiles and GPU cooked output keep their meaning.
No per-particle Material copy or second queue is introduced. Values6 require the
independent `runtime_particle_opacity:1` capability plus effect3/alpha1. Gallery241
reuses Gallery240's glass Material with a separate editable fading effect.

## Camera-plane CPU profile

[ADR064](decisions/064-camera-plane-particles.md) adds explicit values7 and source7.
The shared CPU sampler remains unchanged; detached per-view transforms align the
fixed Engine/Quad with admitted camera right/up axes. Width/height retain original
world X/Y column lengths, while the complete emitter affine still positions births.
The source Mesh, enabled opacity and graph selection survive backend switches.
Disabled opacity projects identity values without discarding source stops.

Instance and part culling, sorting/parity and actual geometry share one view-local
world matrix. The lifetime envelope pads transformed motion with an isotropic
quad radius, covering reflected/sheared and zero-Z-scale emitters from every view.
Static overrides reject skin/wind consumers. Matrix storage belongs to each view,
counts toward the existing CPU budget and never mutates cached simulation poses.

The native **Camera-aligned quads** control and
`asset.document.particle_billboards` share validation/revision/Undo/Redo ownership.
The active Mesh picker is read-only; toggling alignment restores the original Mesh.
Values7 require effect3/alpha1/opacity1 and `runtime_particle_billboard:1` in the kit.
No dependency, gameplay ABI, importer or earlier immutable artifact changes.

## Camera-plane GPU profiles

[ADR065](decisions/065-gpu-camera-plane-particles.md) adds explicit values8/9.
The original built-in or authored evaluator retains its 47-row constants, RNG and
seven-row device output. A second bounded compute dispatch projects those same
rows in place. Separate 64-byte camera constants and the existing same-UAV barrier
pattern order the projection. Width/height retain original world-column lengths;
camera right/up/minus-forward determine orientation. Device centers and RGB stay
unchanged. No CPU readback, particle transform array or second simulation is used.

The shared renderer supplies per-view placeholder parity and lifetime bounds,
including reflected/sheared and zero-Z emitters. Tint flag2 applies Material RGB.
The published instance count is cleared before preparation and becomes visible
only after both dispatches succeed; existing target restoration covers failures.
GPU transparency and per-particle opacity remain unsupported. Cross-emitter/
ordinary-Mesh transparency cannot be replaced with emitter-only sorting.

Source8 retains `gpu_billboard_enabled` independently from CPU `billboard_enabled`,
opacity stops, the original Mesh and graph selection. Reading source7 never enables
GPU alignment implicitly. Enabling GPU alignment preserves an active Graph or
selects built-in GPU evaluation; disabling restores GPU3/graph4 Mesh geometry.
The native checkbox targets the selected backend. Automation's optional
`backend:"gpu"` calls the same source operation; omission preserves the CPU default.
History, publication, grants, revisions and identities retain their existing owners.

Values8/9 require effect3/GPU1 and typed `runtime_particle_gpu_billboard:1` support;
values9 also require graph1. CPU alpha/opacity/billboard capabilities are independent.
Missing, malformed and unsupported kit values reject before distribution replacement.
Gallery243 supplies a built-in effect and shared masked Material; Gallery244 reuses
the Material and Gallery239 program. Their texture remains Gallery242's original
logical asset. The existing Windows package preparers cook the Shader before the
graph Effect. Actual graph9 bytecode realization and export require native Windows
acceptance, separately from generated-program software-device tests.

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
rendering. Native Windows execution requires its own passing recorded run;
generated-program software-device checks do not establish it.

Canonical `manual/images/particle-graph.png` is the actual MaterialGraphCanvas in
particle mode, displaying Gallery239 unchanged. `particle_graph_canvas` renders
ImGui draw data through the SDL software backend at1440×1000 and asserts display
does not edit the source. Local build21/check21 captured it and it was personally
reviewed. No retouch, complete-editor screenshot, Windows or physical GPU claim.
